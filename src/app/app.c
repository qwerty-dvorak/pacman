#include "app/app.h"
#include "app/editor.h"
#include "app/storage.h"
#include "audio/audio.h"

#include "core/clock.h"
#include "core/stats.h"
#include "game/autopilot.h"
#include "game/game.h"
#include "gpu/gpu_renderer.h"
#include "scene/scene.h"
#include "scene/maze_art.h"

#include "raylib.h"

#include <stdio.h>
#include <string.h>

#define MAX_TICKS_PER_FRAME     5           // Beyond this the simulation drops time instead of spiralling.
#define OVERLAY_PERIOD_NS       (NS_PER_SECOND/2)
#define BENCH_WARMUP_NS         NS_PER_SECOND
#define BENCH_SAMPLES_PER_SEC   50000

typedef enum Metric {
    METRIC_FRAME = 0,
    METRIC_POLL,
    METRIC_SIM,
    METRIC_ACQUIRE,
    METRIC_BUILD,
    METRIC_SUBMIT,
    METRIC_SWAP,
    METRIC_GPU,
    METRIC_COUNT
} Metric;

static const char *const metricLabels[METRIC_COUNT] = {
    "frame (cpu wall)", "poll events", "simulate", "acquire (fence wait)", "build vertices", "submit (draw call)",
    "swap buffers", "gpu (draw, timer query)"
};
static const char *const overlayLabels[METRIC_COUNT] = { "FRAME", "POLL", "SIM", "FENCE", "BUILD", "SUBMIT", "SWAP", "GPU" };

// Rolling window behind the on-screen overlay.
typedef struct OverlayStats {
    uint64_t windowStart;
    uint64_t frames;
    uint64_t sum[METRIC_COUNT];
    uint64_t max[METRIC_COUNT];
    uint64_t count[METRIC_COUNT];
    char lines[METRIC_COUNT + 2][32];
    int lineCount;
} OverlayStats;

typedef struct Bench {
    bool enabled;
    bool recording;
    uint64_t startNs;
    uint64_t endNs;
    uint64_t frames;
    uint64_t quads;
    int framebufferWidth;
    int framebufferHeight;
    int scale;
    Samples samples[METRIC_COUNT];
} Bench;

typedef struct InputState {
    Dir pendingDir;
} InputState;

//----------------------------------------------------------------------------------
// Input
//----------------------------------------------------------------------------------
static void PollKeys(InputState *input)
{
    static const struct { int key; Dir dir; } bindings[] = {
        { KEY_RIGHT, DIR_RIGHT }, { KEY_D, DIR_RIGHT }, { KEY_LEFT, DIR_LEFT }, { KEY_A, DIR_LEFT },
        { KEY_UP, DIR_UP }, { KEY_W, DIR_UP }, { KEY_DOWN, DIR_DOWN }, { KEY_S, DIR_DOWN },
    };

    // Presses are latched until the next simulation tick consumes them, so a tap shorter
    // than a tick (common at thousands of frames per second) is never lost.
    for (size_t i = 0; i < ARRAY_COUNT(bindings); i++)
    {
        if (IsKeyPressed(bindings[i].key)) input->pendingDir = bindings[i].dir;
    }
}

static GameInput ConsumeInput(InputState *input, const Game *game, bool autoplay)
{
    GameInput result = { .dir = input->pendingDir };
    input->pendingDir = DIR_NONE;

    if (autoplay)
    {
        result.dir = (game->phase == PHASE_PLAYING)? AutopilotDecide(game) : DIR_NONE;
    }
    return result;
}

//----------------------------------------------------------------------------------
// Measurement
//----------------------------------------------------------------------------------
static void RecordMetric(Bench *bench, OverlayStats *overlay, Metric metric, uint64_t ns)
{
    overlay->sum[metric] += ns;
    overlay->count[metric]++;
    if (ns > overlay->max[metric]) overlay->max[metric] = ns;
    if (bench->recording) SamplesPush(&bench->samples[metric], ns);
}

static void RefreshOverlay(OverlayStats *overlay, uint64_t now, int quads)
{
    const uint64_t elapsed = now - overlay->windowStart;
    if (elapsed < OVERLAY_PERIOD_NS) return;

    int line = 0;
    snprintf(overlay->lines[line++], sizeof(overlay->lines[0]), "FPS    %8.0f", (double)overlay->frames*1e9/(double)elapsed);
    for (int m = 0; m < METRIC_COUNT; m++)
    {
        if (overlay->count[m] == 0) continue;
        const double avg = (double)overlay->sum[m]/(double)overlay->count[m]/1e3;
        snprintf(overlay->lines[line++], sizeof(overlay->lines[0]), "%-6s %8.2f %7.1f", overlayLabels[m], avg, (double)overlay->max[m]/1e3);
    }
    snprintf(overlay->lines[line++], sizeof(overlay->lines[0]), "QUADS  %8d", quads);
    overlay->lineCount = line;

    // The formatted lines persist; only the accumulators restart.
    overlay->windowStart = now;
    overlay->frames = 0;
    memset(overlay->sum, 0, sizeof(overlay->sum));
    memset(overlay->max, 0, sizeof(overlay->max));
    memset(overlay->count, 0, sizeof(overlay->count));
}

static void DrawOverlay(SpriteBatch *batch, const OverlayStats *overlay)
{
    if (overlay->lineCount == 0) return;

    const int glyphScale = MAX(1, batch->scale/2);
    const int lineHeight = (CELL_SIZE + 2)*glyphScale;
    const int width = 24*CELL_SIZE*glyphScale;
    SpriteBatchRectPx(batch, 0, 0, width, lineHeight*(overlay->lineCount + 1) + 2*glyphScale, RGBA(0, 0, 40, 255));
    SpriteBatchTextPx(batch, 2*glyphScale, 2*glyphScale, glyphScale, "US       AVG     MAX", COLOR_YELLOW);
    for (int i = 0; i < overlay->lineCount; i++)
    {
        SpriteBatchTextPx(batch, 2*glyphScale, 2*glyphScale + (i + 1)*lineHeight, glyphScale, overlay->lines[i], COLOR_WHITE);
    }
}

static bool BenchInit(Bench *bench, double seconds)
{
    *bench = (Bench){ 0 };
    if (seconds <= 0.0) return true;

    bench->enabled = true;
    const size_t capacity = (size_t)(seconds*BENCH_SAMPLES_PER_SEC) + 1024;
    for (int m = 0; m < METRIC_COUNT; m++)
    {
        if (!SamplesInit(&bench->samples[m], capacity)) return false;
    }
    return true;
}

// Returns false when nothing was measured (window closed during warm-up).
static bool BenchReport(Bench *bench, const AppOptions *options, const Game *game)
{
    if (bench->frames == 0)
    {
        fprintf(stderr, "render benchmark: no frames recorded (window closed during warm-up)\n");
        return false;
    }

    const double seconds = (double)(bench->endNs - bench->startNs)/1e9;
    printf("\n== render benchmark: %.2f s measured after %.1f s warm-up ==\n", seconds, (double)BENCH_WARMUP_NS/1e9);
    static const char *const windowModes[] = { "fixed", "resizable", "fullscreen" };
    printf("framebuffer %dx%d (scale %d, %s window), vsync %s, overlay %s\n", bench->framebufferWidth,
           bench->framebufferHeight, bench->scale, windowModes[options->windowMode], options->vsync? "on" : "off",
           options->overlay? "on" : "off");
    printf("frames %lu, %.0f fps, %.1f quads/frame, sim reached level %d score %u\n", (unsigned long)bench->frames,
           (double)bench->frames/seconds, (double)bench->quads/(double)MAX(bench->frames, 1u), game->level, game->score);

    SummaryPrintHeader(stdout);
    for (int m = 0; m < METRIC_COUNT; m++)
    {
        Samples *samples = &bench->samples[m];
        if (samples->count == 0) continue;
        const Summary summary = SamplesSummarize(samples);
        SummaryPrintRow(stdout, metricLabels[m], &summary);
        if (samples->dropped > 0) printf("  (%zu samples dropped)\n", samples->dropped);
    }
    fflush(stdout);
    return true;
}

//----------------------------------------------------------------------------------
// Application screens
//----------------------------------------------------------------------------------
typedef enum AppScreen {
    SCREEN_MAIN = 0,
    SCREEN_PLAY,
    SCREEN_PAUSE,
    SCREEN_OPTIONS,
    SCREEN_SCORES,
    SCREEN_HELP,
    SCREEN_SLOTS,
    SCREEN_EDITOR,
    SCREEN_INITIALS,
    SCREEN_GAME_OVER
} AppScreen;

typedef struct AppState {
    Settings settings;
    ScoreTable scores;
    Audio audio;
    Game game;
    LevelPack pack;
    Editor editor;
    StaticLayer staticLayer;
    AppScreen screen;
    AppScreen optionsReturn;
    int selection;
    int editorSlot;
    int artMazeIndex;
    int initialsCursor;
    int scoreRank;
    char initials[SCORE_NAME_LENGTH + 1];
    char message[40];
    bool testing;
    bool quit;
    bool autoplay;
    bool slotSaved[MAZE_SLOTS];
    uint32_t seed;
    uint8_t mazePixels[2][MAZE_PIXELS_BYTES];
} AppState;

static const char *const packNames[PACK_CHOICE_COUNT] = { "ARCADE", "TOUR", "CUSTOM" };
static const int fpsLimits[] = { 0, FPS_LIMIT_VSYNC, 60, 120, 144, 240, 360 };

static bool MenuConfirm(void)
{
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
}

static int MenuHorizontal(void)
{
    return (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D)) -
           (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A));
}

static void MenuMove(AppState *app, int count)
{
    const int move = (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S)) -
                     (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W));
    if (move == 0) return;
    app->selection = (app->selection + move + count)%count;
    AudioPlaySfx(&app->audio, SFX_MENU_MOVE);
}

static void ChangeScreen(AppState *app, AppScreen screen)
{
    const bool wasPlaying = app->screen == SCREEN_PLAY;
    app->screen = screen;
    app->selection = 0;
    app->message[0] = '\0';
    if (screen == SCREEN_SLOTS)
    {
        for (int i = 0; i < MAZE_SLOTS; i++) app->slotSaved[i] = MazeSlotExists(i + 1);
    }
    const bool playing = screen == SCREEN_PLAY;
    AudioSetPaused(&app->audio, !playing);
    if (wasPlaying != playing)
    {
        AudioPlayMusic(&app->audio, playing? MUSIC_NONE : MUSIC_MENU, !playing);
    }
    if (!playing) AudioSetAmbience(&app->audio, AMBIENCE_NONE, 0);
}

static void ApplySettings(AppState *app)
{
    AudioSetVolumes(&app->audio, (float)app->settings.musicVolume/10.0f, (float)app->settings.sfxVolume/10.0f);
    if (app->settings.fpsLimit == FPS_LIMIT_VSYNC) SetWindowState(FLAG_VSYNC_HINT);
    else ClearWindowState(FLAG_VSYNC_HINT);
}

static void SaveSettings(AppState *app)
{
    ApplySettings(app);
    if (!SettingsSave(&app->settings)) snprintf(app->message, sizeof(app->message), "SETTINGS COULD NOT BE SAVED");
}

static void StartGame(AppState *app, bool reloadPack)
{
    if (reloadPack)
    {
        app->testing = false;
        if (app->settings.pack == PACK_CHOICE_CUSTOM)
        {
            if (CustomPackLoad(&app->pack) == 0)
            {
                snprintf(app->message, sizeof(app->message), "SAVE A VALID MAZE IN EDITOR");
                return;
            }
        }
        else
        {
            LevelPackInitBuiltin(&app->pack, (app->settings.pack == PACK_CHOICE_TOUR)? BUILTIN_TOUR : BUILTIN_ARCADE);
        }
    }
    const GameConfig config = {
        .pack = &app->pack, .seed = app->seed,
        .startLevel = app->testing? 1 : app->settings.startLevel,
        .lives = app->settings.lives, .highScore = ScoresBest(&app->scores)
    };
    GameNew(&app->game, &config);
    app->artMazeIndex = -1;
    app->staticLayer.version = 0;
    ChangeScreen(app, SCREEN_PLAY);
    AudioPlayMusic(&app->audio, MUSIC_INTRO, false);
}

static void ReturnToEditor(AppState *app)
{
    app->testing = false;
    app->editor.artDirty = true;
    app->artMazeIndex = -1;
    ChangeScreen(app, SCREEN_EDITOR);
}

static void UploadArtwork(AppState *app, GpuRenderer *gpu)
{
    if (app->screen == SCREEN_EDITOR)
    {
        if (!app->editor.artDirty) return;
        MazeArtRender(&app->editor.layout, app->editor.wallColor, RGBA(255, 184, 255, 255), app->mazePixels[0]);
        MazeArtRender(&app->editor.layout, COLOR_OPAQUE, RGBA(255, 184, 255, 255), app->mazePixels[1]);
        GpuRendererUploadMaze(gpu, app->mazePixels[0], app->mazePixels[1]);
        app->editor.artDirty = false;
        return;
    }
    if ((app->screen != SCREEN_PLAY) && (app->screen != SCREEN_PAUSE) &&
        (app->screen != SCREEN_INITIALS) && (app->screen != SCREEN_GAME_OVER)) return;
    if (app->artMazeIndex == app->game.mazeIndex) return;
    const PackMaze *maze = &app->pack.mazes[app->game.mazeIndex];
    if (maze->classicArt) GpuRendererUploadMaze(gpu, gpu->classicMaze[0], gpu->classicMaze[1]);
    else
    {
        MazeArtRender(&maze->layout, maze->wallColor, RGBA(255, 184, 255, 255), app->mazePixels[0]);
        MazeArtRender(&maze->layout, COLOR_OPAQUE, RGBA(255, 184, 255, 255), app->mazePixels[1]);
        GpuRendererUploadMaze(gpu, app->mazePixels[0], app->mazePixels[1]);
    }
    app->artMazeIndex = app->game.mazeIndex;
    app->staticLayer.version = 0;
}

static void PlayGameEvents(AppState *app)
{
    const uint32_t events = app->game.events;
    if (events & (GAME_EVENT_DOT | GAME_EVENT_ENERGIZER)) AudioPlaySfx(&app->audio, SFX_WAKA);
    if (events & GAME_EVENT_FRUIT) AudioPlaySfx(&app->audio, SFX_FRUIT);
    if (events & GAME_EVENT_GHOST_EATEN) AudioPlaySfx(&app->audio, SFX_GHOST_EATEN);
    if (events & GAME_EVENT_DEATH) AudioPlaySfx(&app->audio, SFX_DEATH);
    if (events & GAME_EVENT_EXTRA_LIFE) AudioPlaySfx(&app->audio, SFX_EXTRA_LIFE);
}

static void UpdateAmbience(AppState *app)
{
    if (app->screen != SCREEN_PLAY)
    {
        AudioSetAmbience(&app->audio, AMBIENCE_NONE, 0);
        return;
    }
    Ambience ambience = AMBIENCE_NONE;
    if (app->game.phase == PHASE_PLAYING)
    {
        ambience = (app->game.frightTicksLeft > 0)? AMBIENCE_FRIGHT : AMBIENCE_SIREN;
        for (int i = 0; i < GHOST_COUNT; i++)
        {
            if ((app->game.ghosts[i].state == GHOST_EYES) || (app->game.ghosts[i].state == GHOST_ENTERING))
            {
                ambience = AMBIENCE_EYES;
                break;
            }
        }
    }
    const int total = app->pack.mazes[app->game.mazeIndex].maze.pelletsLeft;
    const int level = (total > 0)? CLAMP((int)app->game.dotsEaten*5/total, 0, 4) : 0;
    AudioSetAmbience(&app->audio, ambience, level);
}

static void FinishGame(AppState *app)
{
    if (app->autoplay)
    {
        StartGame(app, false);
        return;
    }
    if (app->testing)
    {
        ReturnToEditor(app);
        return;
    }
    app->scoreRank = ScoresRank(&app->scores, app->game.score);
    if (app->scoreRank < 0) ChangeScreen(app, SCREEN_GAME_OVER);
    else
    {
        memcpy(app->initials, "AAA", sizeof(app->initials));
        app->initialsCursor = 0;
        ChangeScreen(app, SCREEN_INITIALS);
        // Text events from the last gameplay frame must not become initials.
        while (GetCharPressed() != 0) { }
    }
}

static void UpdateOptions(AppState *app)
{
    MenuMove(app, 6);
    if (IsKeyPressed(KEY_ESCAPE) || ((app->selection == 5) && MenuConfirm()))
    {
        ChangeScreen(app, app->optionsReturn);
        return;
    }
    int delta = MenuHorizontal();
    if (MenuConfirm()) delta = 1;
    if (delta == 0) return;
    switch (app->selection)
    {
        case 0: app->settings.musicVolume = CLAMP(app->settings.musicVolume + delta, 0, 10); break;
        case 1: app->settings.sfxVolume = CLAMP(app->settings.sfxVolume + delta, 0, 10); break;
        case 2:
        {
            int index = 0;
            for (size_t i = 0; i < ARRAY_COUNT(fpsLimits); i++)
            {
                if (app->settings.fpsLimit == fpsLimits[i]) index = (int)i;
            }
            index = (index + delta + (int)ARRAY_COUNT(fpsLimits))%(int)ARRAY_COUNT(fpsLimits);
            app->settings.fpsLimit = fpsLimits[index];
        } break;
        case 3: app->settings.overlay = !app->settings.overlay; break;
        case 4: app->settings.lives = (app->settings.lives - 1 + delta + 5)%5 + 1; break;
        default: return;
    }
    SaveSettings(app);
    AudioPlaySfx(&app->audio, SFX_MENU_MOVE);
}

static void UpdateInitials(AppState *app)
{
    const int horizontal = MenuHorizontal();
    if (horizontal != 0) app->initialsCursor = (app->initialsCursor + horizontal + SCORE_NAME_LENGTH)%SCORE_NAME_LENGTH;
    const int vertical = (IsKeyPressed(KEY_UP)? 1 : 0) - (IsKeyPressed(KEY_DOWN)? 1 : 0);
    if (vertical != 0)
    {
        char *letter = &app->initials[app->initialsCursor];
        *letter = (char)('A' + (*letter - 'A' + vertical + 26)%26);
        AudioPlaySfx(&app->audio, SFX_MENU_MOVE);
    }
    if (IsKeyPressed(KEY_BACKSPACE)) app->initialsCursor = MAX(0, app->initialsCursor - 1);
    for (int key = GetCharPressed(); key != 0; key = GetCharPressed())
    {
        if ((key >= 'a') && (key <= 'z')) key += 'A' - 'a';
        if ((key < 'A') || (key > 'Z')) continue;
        app->initials[app->initialsCursor] = (char)key;
        app->initialsCursor = MIN(SCORE_NAME_LENGTH - 1, app->initialsCursor + 1);
    }
    if (!IsKeyPressed(KEY_ENTER)) return;
    ScoresInsert(&app->scores, app->scoreRank, app->initials, app->game.score, app->game.level);
    const bool saved = ScoresSave(&app->scores);
    ChangeScreen(app, SCREEN_SCORES);
    if (!saved) snprintf(app->message, sizeof(app->message), "SCORES COULD NOT BE SAVED");
    AudioPlaySfx(&app->audio, SFX_MENU_SELECT);
}

static void UpdateScreens(AppState *app)
{
    if (app->screen == SCREEN_PLAY)
    {
        if (app->testing && IsKeyPressed(KEY_ESCAPE)) ReturnToEditor(app);
        else if (!app->autoplay && (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P) || !IsWindowFocused()))
        {
            ChangeScreen(app, SCREEN_PAUSE);
        }
        return;
    }
    if (app->screen == SCREEN_OPTIONS)
    {
        UpdateOptions(app);
        return;
    }
    if (app->screen == SCREEN_INITIALS)
    {
        UpdateInitials(app);
        return;
    }
    if (app->screen == SCREEN_EDITOR)
    {
        const EditorAction action = EditorUpdate(&app->editor);
        if (action == EDITOR_EXIT) ChangeScreen(app, SCREEN_MAIN);
        else if (action == EDITOR_TEST)
        {
            app->editor.check = MazeValidate(&app->editor.layout);
            if (!app->editor.check.ok)
            {
                snprintf(app->editor.message, sizeof(app->editor.message), "%s", app->editor.check.message);
                return;
            }
            LevelPackInitEmpty(&app->pack, "EDITOR TEST", 1);
            if (!LevelPackAdd(&app->pack, &app->editor.layout, app->editor.name, app->editor.wallColor, false)) return;
            app->testing = true;
            StartGame(app, false);
        }
        return;
    }
    if (app->screen == SCREEN_SLOTS)
    {
        MenuMove(app, MAZE_SLOTS);
        const int horizontal = MenuHorizontal();
        if (horizontal != 0) app->selection = (app->selection + MAZE_SLOTS/2)%MAZE_SLOTS;
        if (IsKeyPressed(KEY_ESCAPE)) ChangeScreen(app, SCREEN_MAIN);
        else if (MenuConfirm())
        {
            app->editorSlot = app->selection + 1;
            EditorInit(&app->editor, app->editorSlot);
            ChangeScreen(app, SCREEN_EDITOR);
            AudioPlaySfx(&app->audio, SFX_MENU_SELECT);
        }
        return;
    }
    if ((app->screen == SCREEN_SCORES) || (app->screen == SCREEN_HELP))
    {
        if (IsKeyPressed(KEY_ESCAPE) || MenuConfirm()) ChangeScreen(app, SCREEN_MAIN);
        return;
    }
    if (app->screen == SCREEN_PAUSE)
    {
        MenuMove(app, 4);
        if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P))
        {
            if (IsWindowFocused()) ChangeScreen(app, SCREEN_PLAY);
            return;
        }
        if (!MenuConfirm()) return;
        AudioPlaySfx(&app->audio, SFX_MENU_SELECT);
        switch (app->selection)
        {
            case 0: if (IsWindowFocused()) ChangeScreen(app, SCREEN_PLAY); break;
            case 1: StartGame(app, false); break;
            case 2: app->optionsReturn = SCREEN_PAUSE; ChangeScreen(app, SCREEN_OPTIONS); break;
            case 3:
                if (app->testing) ReturnToEditor(app);
                else ChangeScreen(app, SCREEN_MAIN);
                break;
            default: break;
        }
        return;
    }
    if (app->screen == SCREEN_GAME_OVER)
    {
        MenuMove(app, 2);
        if (IsKeyPressed(KEY_ESCAPE)) ChangeScreen(app, SCREEN_MAIN);
        else if (MenuConfirm())
        {
            if (app->selection == 0) StartGame(app, false);
            else ChangeScreen(app, SCREEN_MAIN);
        }
        return;
    }

    MenuMove(app, 8);
    const int horizontal = MenuHorizontal();
    const bool confirm = MenuConfirm();
    if ((app->selection == 1) && ((horizontal != 0) || confirm))
    {
        app->settings.pack = (app->settings.pack + ((horizontal != 0)? horizontal : 1) + PACK_CHOICE_COUNT)%PACK_CHOICE_COUNT;
        SaveSettings(app);
        AudioPlaySfx(&app->audio, SFX_MENU_MOVE);
    }
    else if ((app->selection == 2) && ((horizontal != 0) || confirm))
    {
        app->settings.startLevel = (app->settings.startLevel - 1 + ((horizontal != 0)? horizontal : 1) + MAX_START_LEVEL)%MAX_START_LEVEL + 1;
        SaveSettings(app);
        AudioPlaySfx(&app->audio, SFX_MENU_MOVE);
    }
    else if (confirm)
    {
        AudioPlaySfx(&app->audio, SFX_MENU_SELECT);
        switch (app->selection)
        {
            case 0: StartGame(app, true); break;
            case 3: ChangeScreen(app, SCREEN_SLOTS); break;
            case 4: app->optionsReturn = SCREEN_MAIN; ChangeScreen(app, SCREEN_OPTIONS); break;
            case 5: ChangeScreen(app, SCREEN_SCORES); break;
            case 6: ChangeScreen(app, SCREEN_HELP); break;
            case 7: app->quit = true; break;
            default: break;
        }
    }
}

static void CenterText(SpriteBatch *batch, int row, const char *text, uint32_t color)
{
    SpriteBatchText(batch, MAX(0, (MAZE_COLS - (int)strlen(text))/2), row, text, color);
}

static void MenuLine(SpriteBatch *batch, int row, const char *text, bool selected)
{
    SpriteBatchText(batch, 2, row, selected? ">" : " ", COLOR_YELLOW);
    SpriteBatchText(batch, 4, row, text, selected? COLOR_YELLOW : COLOR_WHITE);
}

static void DrawScreen(AppState *app, SpriteBatch *batch)
{
    if (app->screen == SCREEN_EDITOR)
    {
        EditorDraw(&app->editor, batch);
        return;
    }
    SceneDrawBackdrop(batch);
    char text[40] = { 0 };
    switch (app->screen)
    {
        case SCREEN_MAIN:
            CenterText(batch, 4, "PAC-MAN", COLOR_YELLOW);
            MenuLine(batch, 9, "START GAME", app->selection == 0);
            snprintf(text, sizeof(text), "PACK: %s", packNames[app->settings.pack]);
            MenuLine(batch, 11, text, app->selection == 1);
            snprintf(text, sizeof(text), "START LEVEL: %d", app->settings.startLevel);
            MenuLine(batch, 13, text, app->selection == 2);
            MenuLine(batch, 15, "MAZE EDITOR", app->selection == 3);
            MenuLine(batch, 17, "OPTIONS", app->selection == 4);
            MenuLine(batch, 19, "HIGH SCORES", app->selection == 5);
            MenuLine(batch, 21, "HELP", app->selection == 6);
            MenuLine(batch, 23, "QUIT", app->selection == 7);
            CenterText(batch, 28, "ARROWS MOVE / CHANGE", COLOR_CYAN);
            CenterText(batch, 30, "ENTER SELECTS", COLOR_CYAN);
            break;
        case SCREEN_PAUSE:
            CenterText(batch, 7, "PAUSED", COLOR_YELLOW);
            MenuLine(batch, 12, "RESUME", app->selection == 0);
            MenuLine(batch, 15, "RESTART", app->selection == 1);
            MenuLine(batch, 18, "OPTIONS", app->selection == 2);
            MenuLine(batch, 21, app->testing? "RETURN TO EDITOR" : "MAIN MENU", app->selection == 3);
            CenterText(batch, 28, "ESC OR P TO RESUME", COLOR_CYAN);
            break;
        case SCREEN_OPTIONS:
            CenterText(batch, 5, "OPTIONS", COLOR_YELLOW);
            snprintf(text, sizeof(text), "MUSIC VOLUME: %d", app->settings.musicVolume);
            MenuLine(batch, 10, text, app->selection == 0);
            snprintf(text, sizeof(text), "SFX VOLUME: %d", app->settings.sfxVolume);
            MenuLine(batch, 13, text, app->selection == 1);
            if (app->settings.fpsLimit == FPS_LIMIT_VSYNC) snprintf(text, sizeof(text), "FPS CAP: VSYNC");
            else if (app->settings.fpsLimit == 0) snprintf(text, sizeof(text), "FPS CAP: UNLIMITED");
            else snprintf(text, sizeof(text), "FPS CAP: %d", app->settings.fpsLimit);
            MenuLine(batch, 16, text, app->selection == 2);
            snprintf(text, sizeof(text), "OVERLAY: %s", app->settings.overlay? "ON" : "OFF");
            MenuLine(batch, 19, text, app->selection == 3);
            snprintf(text, sizeof(text), "STARTING LIVES: %d", app->settings.lives);
            MenuLine(batch, 22, text, app->selection == 4);
            MenuLine(batch, 25, "BACK", app->selection == 5);
            CenterText(batch, 29, "LEFT / RIGHT TO CHANGE", COLOR_CYAN);
            CenterText(batch, 30, "LIVES APPLY TO NEW GAMES", COLOR_CYAN);
            break;
        case SCREEN_SCORES:
            CenterText(batch, 4, "HIGH SCORES", COLOR_YELLOW);
            SpriteBatchText(batch, 2, 7, "RK  NAME   SCORE  LEVEL", COLOR_CYAN);
            for (int i = 0; i < app->scores.count; i++)
            {
                const ScoreEntry *entry = &app->scores.entries[i];
                snprintf(text, sizeof(text), "%2d  %s %8u %4d", i + 1, entry->name, entry->score, entry->level);
                SpriteBatchText(batch, 2, 10 + i*2, text, COLOR_WHITE);
            }
            CenterText(batch, 31, "ENTER / ESC TO RETURN", COLOR_CYAN);
            break;
        case SCREEN_HELP:
            CenterText(batch, 4, "HOW TO PLAY", COLOR_YELLOW);
            SpriteBatchText(batch, 2, 8, "ARROWS / WASD: MOVE", COLOR_WHITE);
            SpriteBatchText(batch, 2, 11, "EAT EVERY DOT TO ADVANCE", COLOR_WHITE);
            SpriteBatchText(batch, 2, 14, "POWER DOTS TURN GHOSTS", COLOR_WHITE);
            SpriteBatchText(batch, 2, 15, "BLUE. EAT THEM FOR POINTS", COLOR_WHITE);
            SpriteBatchText(batch, 2, 18, "FRUIT GIVES BONUS POINTS", COLOR_WHITE);
            SpriteBatchText(batch, 2, 21, "10000 POINTS: EXTRA LIFE", COLOR_WHITE);
            SpriteBatchText(batch, 2, 24, "ESC / P: PAUSE", COLOR_WHITE);
            SpriteBatchText(batch, 2, 26, "F1: PERFORMANCE OVERLAY", COLOR_WHITE);
            CenterText(batch, 31, "ENTER / ESC TO RETURN", COLOR_CYAN);
            break;
        case SCREEN_SLOTS:
            CenterText(batch, 3, "MAZE EDITOR SLOTS", COLOR_YELLOW);
            for (int i = 0; i < MAZE_SLOTS; i++)
            {
                const int col = 1 + (i/(MAZE_SLOTS/2))*14;
                const int row = 7 + (i%(MAZE_SLOTS/2))*2;
                snprintf(text, sizeof(text), "%s%02d %s", (app->selection == i)? ">" : " ", i + 1,
                         app->slotSaved[i]? "SAVED" : "EMPTY");
                SpriteBatchText(batch, col, row, text, (app->selection == i)? COLOR_YELLOW : COLOR_WHITE);
            }
            CenterText(batch, 32, "ENTER EDIT / ESC BACK", COLOR_CYAN);
            break;
        case SCREEN_INITIALS:
            CenterText(batch, 7, "NEW HIGH SCORE", COLOR_YELLOW);
            snprintf(text, sizeof(text), "SCORE %u", app->game.score);
            CenterText(batch, 11, text, COLOR_WHITE);
            CenterText(batch, 15, "ENTER YOUR INITIALS", COLOR_CYAN);
            SpriteBatchText(batch, 12, 19, app->initials, COLOR_YELLOW);
            SpriteBatchText(batch, 12 + app->initialsCursor, 20, "-", COLOR_CYAN);
            CenterText(batch, 25, "TYPE THREE LETTERS", COLOR_WHITE);
            CenterText(batch, 27, "ARROWS ADJUST / ENTER SAVE", COLOR_WHITE);
            break;
        case SCREEN_GAME_OVER:
            CenterText(batch, 8, "GAME OVER", COLOR_RED);
            snprintf(text, sizeof(text), "SCORE %u", app->game.score);
            CenterText(batch, 12, text, COLOR_WHITE);
            MenuLine(batch, 18, "PLAY AGAIN", app->selection == 0);
            MenuLine(batch, 21, "MAIN MENU", app->selection == 1);
            break;
        default: break;
    }
    if (app->message[0] != '\0') CenterText(batch, 34, app->message, COLOR_RED);
}

//----------------------------------------------------------------------------------
// Main loop
//----------------------------------------------------------------------------------
static bool OpenWindow(const AppOptions *options)
{
    unsigned int flags = options->vsync? FLAG_VSYNC_HINT : 0;
    if (options->windowMode == WINDOW_RESIZABLE) flags |= FLAG_WINDOW_RESIZABLE;
    if (options->windowMode == WINDOW_FULLSCREEN) flags |= FLAG_FULLSCREEN_MODE;
    SetConfigFlags(flags);

    const int initialScale = (options->scale > 0)? options->scale : 3;
    if (options->windowMode == WINDOW_FULLSCREEN) InitWindow(0, 0, "Pac-Man");
    else InitWindow(SCREEN_WIDTH*initialScale, SCREEN_HEIGHT*initialScale, "Pac-Man");
    if (!IsWindowReady()) return false;
    SetExitKey(KEY_NULL);

    if ((options->windowMode != WINDOW_FULLSCREEN) && (options->scale <= 0))
    {
        const int fit = MAX(1, (GetMonitorHeight(GetCurrentMonitor()) - 96)/SCREEN_HEIGHT);
        if (fit != initialScale) SetWindowSize(SCREEN_WIDTH*fit, SCREEN_HEIGHT*fit);
    }
    return true;
}

int AppRun(const AppOptions *options)
{
    Bench bench = { 0 };
    if (!BenchInit(&bench, options->benchSeconds))
    {
        fprintf(stderr, "out of memory for benchmark samples\n");
        for (int m = 0; m < METRIC_COUNT; m++) SamplesFree(&bench.samples[m]);
        return 1;
    }

    SetTraceLogLevel(bench.enabled? LOG_INFO : LOG_WARNING);
    if (!OpenWindow(options))
    {
        for (int m = 0; m < METRIC_COUNT; m++) SamplesFree(&bench.samples[m]);
        return 1;
    }

    char spriteDir[1024] = { 0 };
    snprintf(spriteDir, sizeof(spriteDir), "%sassets/sprites", GetApplicationDirectory());
    GpuRenderer gpu = { 0 };
    if (!GpuRendererInit(&gpu, spriteDir))
    {
        GpuRendererShutdown(&gpu);
        for (int m = 0; m < METRIC_COUNT; m++) SamplesFree(&bench.samples[m]);
        CloseWindow();
        return 1;
    }

    // Concrete storage keeps the editor history, pack and artwork off the thread stack.
    static AppState app = { 0 };
    memset(&app, 0, sizeof(app));
    app.autoplay = options->autoplay || bench.enabled;
    app.seed = options->seed;
    app.artMazeIndex = -1;
    app.editorSlot = 1;
    if (app.autoplay)
    {
        // Reproducible benchmarks never inherit the player's pack, lives or FPS cap.
        SettingsDefault(&app.settings);
        app.settings.overlay = options->overlay;
        app.settings.fpsLimit = options->vsync? FPS_LIMIT_VSYNC : 0;
    }
    else
    {
        SettingsLoad(&app.settings);
        ScoresLoad(&app.scores);
        app.settings.overlay = app.settings.overlay && options->overlay;
        if (options->vsync) app.settings.fpsLimit = FPS_LIMIT_VSYNC;
    }
    if (!bench.enabled) AudioInit(&app.audio);
    ApplySettings(&app);
    if (app.autoplay) StartGame(&app, true);
    else
    {
        app.screen = SCREEN_MAIN;
        AudioSetPaused(&app.audio, true);
        AudioPlayMusic(&app.audio, MUSIC_MENU, true);
    }

    InputState input = { .pendingDir = DIR_NONE };
    OverlayStats overlay = { 0 };
    SpriteBatch batch = { 0 };
    uint64_t gpuResults[GPU_TIMER_RING] = { 0 };

    // Fixed 60 Hz simulation; time is accumulated as ns*60 so one tick is exactly 1e9 units.
    uint64_t accumulator = 0;
    uint64_t lastNs = ClockNowNs();
    overlay.windowStart = lastNs;
    if (bench.enabled) bench.startNs = lastNs + BENCH_WARMUP_NS;

    while (!app.quit && !WindowShouldClose())
    {
        const uint64_t t0 = ClockNowNs();
        PollInputEvents();
        const uint64_t t1 = ClockNowNs();
        const AppScreen previousScreen = app.screen;
        UpdateScreens(&app);
        if (app.quit) break;
        if (IsKeyPressed(KEY_F1) && (app.screen != SCREEN_EDITOR))
        {
            app.settings.overlay = !app.settings.overlay;
            if (!app.autoplay) SaveSettings(&app);
        }

        if ((app.screen != SCREEN_PLAY) || (previousScreen != SCREEN_PLAY))
        {
            accumulator = 0;
            input.pendingDir = DIR_NONE;
        }
        else
        {
            PollKeys(&input);
            accumulator += (t1 - lastNs)*TICKS_PER_SECOND;
            for (int ticks = 0; accumulator >= NS_PER_SECOND; ticks++)
            {
                if (ticks == MAX_TICKS_PER_FRAME)
                {
                    accumulator = 0;
                    break;
                }
                const GameInput gameInput = ConsumeInput(&input, &app.game, app.autoplay);
                GameTick(&app.game, &gameInput);
                PlayGameEvents(&app);
                accumulator -= NS_PER_SECOND;
                if (app.game.phase == PHASE_GAME_OVER)
                {
                    FinishGame(&app);
                    accumulator = 0;
                    input.pendingDir = DIR_NONE;
                    break;
                }
            }
        }
        lastNs = t1;
        UpdateAmbience(&app);
        UploadArtwork(&app, &gpu);
        const float alpha = (float)((double)accumulator/(double)NS_PER_SECOND);
        const uint64_t t2 = ClockNowNs();

        const int framebufferWidth = GetRenderWidth();
        const int framebufferHeight = GetRenderHeight();
        const StaticLayer *layer = NULL;
        if (app.screen == SCREEN_PLAY)
        {
            SceneUpdateStaticLayer(&app.staticLayer, &app.game, framebufferWidth, framebufferHeight);
            layer = &app.staticLayer;
        }
        const uint64_t staticReadyNs = ClockNowNs();
        const uint64_t fenceWait = GpuRendererAcquire(&gpu, &batch, framebufferWidth, framebufferHeight, layer);
        const uint64_t t3 = ClockNowNs();

        if (app.screen == SCREEN_PLAY) SceneDrawDynamic(&batch, &app.game, alpha);
        else DrawScreen(&app, &batch);
        if (app.settings.overlay && ((app.screen == SCREEN_PLAY) || (app.screen == SCREEN_PAUSE))) DrawOverlay(&batch, &overlay);
        const uint64_t t4 = ClockNowNs();

        GpuRendererSubmit(&gpu, &batch);
        const uint64_t t5 = ClockNowNs();
        SwapScreenBuffer();
        const uint64_t swappedNs = ClockNowNs();
        if (!bench.enabled)
        {
            int frameLimit = MAX(0, app.settings.fpsLimit);
            if (!IsWindowFocused()) frameLimit = (frameLimit > 0)? MIN(frameLimit, 30) : 30;
            else if (app.screen != SCREEN_PLAY) frameLimit = (frameLimit > 0)? MIN(frameLimit, 120) : 120;
            if (frameLimit > 0) SleepUntilNs(t0 + NS_PER_SECOND/(uint64_t)frameLimit);
        }
        const uint64_t t6 = ClockNowNs();

        bench.recording = bench.enabled && (t0 >= bench.startNs);
        RecordMetric(&bench, &overlay, METRIC_POLL, t1 - t0);
        RecordMetric(&bench, &overlay, METRIC_SIM, t2 - t1);
        RecordMetric(&bench, &overlay, METRIC_ACQUIRE, fenceWait);
        RecordMetric(&bench, &overlay, METRIC_BUILD, (t4 - t3) + (staticReadyNs - t2));
        RecordMetric(&bench, &overlay, METRIC_SUBMIT, t5 - t4);
        RecordMetric(&bench, &overlay, METRIC_SWAP, swappedNs - t5);
        RecordMetric(&bench, &overlay, METRIC_FRAME, t6 - t0);

        const int gpuCount = GpuTimerCollect(&gpu.timer, gpuResults, GPU_TIMER_RING);
        for (int i = 0; i < gpuCount; i++) RecordMetric(&bench, &overlay, METRIC_GPU, gpuResults[i]);
        overlay.frames++;
        RefreshOverlay(&overlay, t6, batch.quadCount);

        if (bench.recording)
        {
            bench.frames++;
            bench.quads += (uint64_t)batch.quadCount;
            bench.endNs = t6;
            if ((double)(t6 - bench.startNs) >= options->benchSeconds*1e9) break;
        }
    }

    bool ok = true;
    if (bench.enabled)
    {
        bench.framebufferWidth = batch.framebufferWidth;
        bench.framebufferHeight = batch.framebufferHeight;
        bench.scale = batch.scale;
        ok = BenchReport(&bench, options, &app.game);
    }
    for (int m = 0; m < METRIC_COUNT; m++) SamplesFree(&bench.samples[m]);
    AudioShutdown(&app.audio);
    GpuRendererShutdown(&gpu);
    CloseWindow();
    return ok? 0 : 1;
}
