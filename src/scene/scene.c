#include "scene/scene.h"

#include <math.h>
#include <string.h>

// Pixel of a 16x16 sprite placed on an actor's centre pixel. The sprites' 13 px bodies span
// rows/columns 1-13, so an offset of 7 centres them in the maze corridors (measured on the
// maze image: corridor around tile row 1 is open from y=4 to y=21).
#define SPRITE_ANCHOR       7

typedef struct Visibility {
    bool ghosts;
    bool pacman;
    bool energizers;
    bool flashMaze;
    int deathFrame;         // -1: regular Pac-Man.
} Visibility;

static Visibility ComputeVisibility(const Game *game)
{
    Visibility vis = { .ghosts = true, .pacman = true, .energizers = true, .flashMaze = false, .deathFrame = -1 };
    const uint32_t t = game->phaseTicks;

    switch (game->phase)
    {
        case PHASE_READY:
        {
            if (game->firstReady && (t < READY_TICKS)) vis.ghosts = vis.pacman = false;
        } break;
        case PHASE_PLAYING: vis.energizers = ((game->tick/10) & 1u) == 0; break;
        case PHASE_GHOST_EATEN: vis.pacman = false; break;
        case PHASE_DYING:
        {
            if (t >= DEATH_FREEZE_TICKS)
            {
                const uint32_t frame = (t - DEATH_FREEZE_TICKS)/DEATH_FRAME_TICKS;
                vis.ghosts = false;
                vis.pacman = frame < DEATH_FRAMES;
                vis.deathFrame = (int)MIN(frame, (uint32_t)DEATH_FRAMES - 1);
            }
        } break;
        case PHASE_LEVEL_DONE:
        {
            if (t >= LEVEL_DONE_FREEZE_TICKS)
            {
                vis.ghosts = false;
                vis.flashMaze = (((t - LEVEL_DONE_FREEZE_TICKS)/LEVEL_FLASH_TICKS) & 1u) != 0;
            }
        } break;
        case PHASE_GAME_OVER: vis.ghosts = vis.pacman = false; break;
        default: break;
    }
    return vis;
}

// Sub-pixel arcade position snapped to framebuffer pixels.
FORCE_INLINE void ArcadeQuadF(SpriteBatch *batch, float ax, float ay, AtlasRect src, uint32_t color)
{
    const int s = batch->scale;
    const int x = batch->originX + (int)lrintf(ax*(float)s);
    const int y = batch->originY + (int)lrintf(ay*(float)s);
    SpriteBatchQuad(batch, x, y, src.w*s, src.h*s, src, color);
}

// Draws a 16x16 sprite centred on an actor, interpolated in screen-space arcade pixels.
// Tunnel wraps jump instead of sliding across the screen.
static void DrawActorSprite(SpriteBatch *batch, const Actor *actor, float alpha, AtlasRect src)
{
    const int dx = actor->x - actor->prevX;
    const int dy = actor->y - actor->prevY;
    const bool wrapped = (dx > 2*TILE_SIZE) || (dx < -2*TILE_SIZE);
    const float x = wrapped? (float)actor->x : (float)actor->prevX + (float)dx*alpha;
    const float y = (float)actor->prevY + (float)dy*alpha + (float)MAZE_OFFSET_Y;
    ArcadeQuadF(batch, x - (float)SPRITE_ANCHOR, y - (float)SPRITE_ANCHOR, src, COLOR_OPAQUE);
}

// The renderer never clears, so every framebuffer pixel outside the opaque maze image is
// painted here once with opaque black: letterbox bars and the two HUD bands.
static void DrawBackground(SpriteBatch *batch)
{
    const uint32_t black = RGBA(0, 0, 0, 255);
    const int s = batch->scale;
    const int left = batch->originX;
    const int top = batch->originY;
    const int right = left + SCREEN_WIDTH*s;
    const int bottom = top + SCREEN_HEIGHT*s;
    const int mazeBottom = top + (MAZE_OFFSET_Y + MAZE_HEIGHT)*s;

    if (left > 0) SpriteBatchRectPx(batch, 0, 0, left, batch->framebufferHeight, black);
    if (right < batch->framebufferWidth) SpriteBatchRectPx(batch, right, 0, batch->framebufferWidth - right, batch->framebufferHeight, black);
    if (top > 0) SpriteBatchRectPx(batch, left, 0, right - left, top, black);
    if (bottom < batch->framebufferHeight) SpriteBatchRectPx(batch, left, bottom, right - left, batch->framebufferHeight - bottom, black);

    SpriteBatchRectPx(batch, left, top, right - left, MAZE_OFFSET_Y*s, black);
    SpriteBatchRectPx(batch, left, mazeBottom, right - left, bottom - mazeBottom, black);
}

static void DrawDots(SpriteBatch *batch, const Maze *maze)
{
    const AtlasRect dot = AtlasCell(CELL_DOT);
    for (int ty = 0; ty < MAZE_ROWS; ty++)
    {
        // Visit only set bits (tzcnt + blsr).
        uint32_t bits = maze->pellets[ty] & ~maze->energizers[ty];
        while (bits != 0)
        {
            const int tx = __builtin_ctz(bits);
            bits &= bits - 1;
            SpriteBatchArcade(batch, tx*TILE_SIZE, MAZE_OFFSET_Y + ty*TILE_SIZE, dot, COLOR_OPAQUE);
        }
    }
}

static void DrawEnergizers(SpriteBatch *batch, const Maze *maze)
{
    const AtlasRect energizer = AtlasCell(CELL_ENERGIZER);
    for (int ty = 0; ty < MAZE_ROWS; ty++)
    {
        uint32_t bits = maze->pellets[ty] & maze->energizers[ty];
        while (bits != 0)
        {
            const int tx = __builtin_ctz(bits);
            bits &= bits - 1;
            SpriteBatchArcade(batch, tx*TILE_SIZE, MAZE_OFFSET_Y + ty*TILE_SIZE, energizer, COLOR_OPAQUE);
        }
    }
}

static AtlasRect GhostSprite(const Game *game, const Ghost *ghost, int id)
{
    const int frame = (int)((game->tick >> 3) & 1u);
    const Dir dir = (ghost->actor.dir == DIR_NONE)? DIR_LEFT : ghost->actor.dir;

    if ((ghost->state == GHOST_EYES) || (ghost->state == GHOST_ENTERING)) return AtlasSprite(SPRITE_COL_EYES + dir, SPRITE_ROW_GHOST + 1);
    if (ghost->frightened)
    {
        const int white = GameFrightFlashWhite(game)? 2 : 0;
        return AtlasSprite(SPRITE_COL_FRIGHT + white + frame, SPRITE_ROW_GHOST);
    }
    return AtlasSprite(dir*2 + frame, SPRITE_ROW_GHOST + id);
}

static AtlasRect PacSprite(const Game *game, const Visibility *vis)
{
    if (vis->deathFrame >= 0) return AtlasSprite(SPRITE_COL_DEATH + vis->deathFrame, 0);

    const Pacman *pac = &game->pac;
    const bool animated = (game->phase == PHASE_PLAYING) || (game->phase == PHASE_DYING) || (game->phase == PHASE_GHOST_EATEN);
    if (!animated || (pac->actor.dir == DIR_NONE)) return AtlasSprite(SPRITE_COL_PAC_CLOSED, 0);

    // Mouth cycle: wide, half, closed, half; advanced by distance travelled.
    static const int mouth[4] = { SPRITE_COL_PAC_WIDE, SPRITE_COL_PAC_HALF, SPRITE_COL_PAC_CLOSED, SPRITE_COL_PAC_HALF };
    return AtlasSprite(mouth[(pac->animDistance >> 1) & 3u], SPRITE_ROW_PAC_RIGHT + pac->actor.dir);
}

static void DrawActors(SpriteBatch *batch, const Game *game, const Visibility *vis, float alpha)
{
    if (game->fruitTicksLeft > 0)
    {
        SpriteBatchArcade(batch, FRUIT_X - SPRITE_ANCHOR, MAZE_OFFSET_Y + FRUIT_Y - SPRITE_ANCHOR,
                          AtlasSprite(SPRITE_COL_FRUIT + game->spec->fruit, SPRITE_ROW_FRUIT), COLOR_OPAQUE);
    }
    else if (game->fruitScoreTicksLeft > 0)
    {
        const AtlasRect label = AtlasFruitScore((FruitKind)game->spec->fruit);
        SpriteBatchArcade(batch, FRUIT_X - label.w/2, MAZE_OFFSET_Y + FRUIT_Y - SPRITE_ANCHOR, label, COLOR_OPAQUE);
    }

    if (vis->pacman) DrawActorSprite(batch, &game->pac.actor, alpha, PacSprite(game, vis));
    if (!vis->ghosts) return;

    // Reverse order so Blinky ends up on top, as in the arcade.
    for (int i = GHOST_COUNT - 1; i >= 0; i--)
    {
        const Ghost *ghost = &game->ghosts[i];
        if ((game->phase == PHASE_GHOST_EATEN) && (i == game->eatenGhost))
        {
            const int index = (int)MIN(game->ghostsEatenInFright, 4u) - 1;
            DrawActorSprite(batch, &ghost->actor, alpha, AtlasSprite(index, SPRITE_ROW_GHOST_SCORE));
        }
        else DrawActorSprite(batch, &ghost->actor, alpha, GhostSprite(game, ghost, i));
    }
}

static void DrawHud(SpriteBatch *batch, const Game *game)
{
    const bool blinkOn = (game->phase != PHASE_PLAYING) || (((game->tick >> 4) & 1u) == 0);
    if (blinkOn) SpriteBatchText(batch, 3, 0, "1UP", COLOR_WHITE);
    SpriteBatchText(batch, 9, 0, "HIGH SCORE", COLOR_WHITE);
    SpriteBatchNumber(batch, 6, 1, game->score, 2, COLOR_WHITE);
    if (game->highScore > 0) SpriteBatchNumber(batch, 16, 1, game->highScore, 2, COLOR_WHITE);

    const int bottom = SCREEN_HEIGHT - 2*TILE_SIZE;
    const bool inGame = (game->phase != PHASE_GAME_OVER);
    const int reserve = inGame? MIN(game->lives - 1, 5) : 0;
    const AtlasRect lifeIcon = AtlasSprite(SPRITE_COL_PAC_HALF, SPRITE_ROW_PAC_RIGHT + DIR_LEFT);
    for (int i = 0; i < reserve; i++) SpriteBatchArcade(batch, 2*TILE_SIZE + i*SPRITE_SIZE, bottom, lifeIcon, COLOR_OPAQUE);

    // Fruit of the current and up to six previous levels, newest on the right.
    const int firstLevel = MAX(1, game->level - 6);
    for (int level = game->level, slot = 0; level >= firstLevel; level--, slot++)
    {
        const int x = SCREEN_WIDTH - 2*TILE_SIZE - SPRITE_SIZE - slot*SPRITE_SIZE;
        SpriteBatchArcade(batch, x, bottom, AtlasSprite(SPRITE_COL_FRUIT + LevelGetSpec(level)->fruit, SPRITE_ROW_FRUIT), COLOR_OPAQUE);
    }

    // Messages sit on maze row 17 (below the house) and maze row 11 (above it).
    const int messageRow = 3 + 17;
    switch (game->phase)
    {
        case PHASE_READY:
        {
            if (game->firstReady && (game->phaseTicks < READY_TICKS)) SpriteBatchText(batch, 9, 3 + 11, "PLAYER ONE", COLOR_CYAN);
            SpriteBatchText(batch, 11, messageRow, "READY!", COLOR_YELLOW);
        } break;
        case PHASE_GAME_OVER: SpriteBatchText(batch, 9, messageRow, "GAME  OVER", COLOR_RED); break;
        default: break;
    }
}

void SceneDrawBackdrop(SpriteBatch *batch)
{
    SpriteBatchRectPx(batch, 0, 0, batch->framebufferWidth, batch->framebufferHeight, RGBA(0, 0, 0, 255));
}

void SceneDrawMaze(SpriteBatch *batch, bool flash)
{
    DrawBackground(batch);
    const AtlasRect maze = { flash? ATLAS_MAZE_FLASH_X : ATLAS_MAZE_X, ATLAS_MAZE_Y, MAZE_WIDTH, MAZE_HEIGHT };
    SpriteBatchArcade(batch, 0, MAZE_OFFSET_Y, maze, COLOR_OPAQUE);
}

void SceneDrawPellets(SpriteBatch *batch, const Maze *maze)
{
    DrawDots(batch, maze);
    DrawEnergizers(batch, maze);
}

// Versions are unique across all layers so a GPU slot never mistakes one layer's content
// for another's.
static uint32_t nextLayerVersion = 1;

void SceneUpdateStaticLayer(StaticLayer *layer, const Game *game, int framebufferWidth, int framebufferHeight)
{
    const Visibility vis = ComputeVisibility(game);
    uint32_t dots[MAZE_ROWS] = { 0 };
    for (int y = 0; y < MAZE_ROWS; y++) dots[y] = game->maze.pellets[y] & ~game->maze.energizers[y];
    const bool same = (layer->version != 0) && (layer->flashMaze == vis.flashMaze) &&
                      (layer->framebufferWidth == framebufferWidth) && (layer->framebufferHeight == framebufferHeight) &&
                      (memcmp(layer->pellets, dots, sizeof(dots)) == 0);
    if (same) return;

    SpriteBatch batch;
    SpriteBatchBegin(&batch, layer->vertices, STATIC_LAYER_QUADS, framebufferWidth, framebufferHeight);
    SceneDrawMaze(&batch, vis.flashMaze);
    DrawDots(&batch, &game->maze);

    layer->quadCount = batch.quadCount;
    layer->flashMaze = vis.flashMaze;
    layer->framebufferWidth = framebufferWidth;
    layer->framebufferHeight = framebufferHeight;
    memcpy(layer->pellets, dots, sizeof(dots));
    layer->version = nextLayerVersion++;
}

void SceneDrawDynamic(SpriteBatch *batch, const Game *game, float alpha)
{
    const Visibility vis = ComputeVisibility(game);
    if (vis.energizers) DrawEnergizers(batch, &game->maze);
    DrawActors(batch, game, &vis, alpha);
    DrawHud(batch, game);
}
