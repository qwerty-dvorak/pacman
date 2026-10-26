#include "bench/bench_common.h"
#include "game/autopilot.h"
#include "scene/scene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LevelPack pack;
static StaticLayer layer;
static Vertex vertices[8192];

static void CheckPack(void)
{
    for (int i = 0; i < pack.mazeCount; i++)
    {
        const PackMaze *maze = &pack.mazes[i];
        MazeCheck check = MazeValidate(&maze->layout);
        char text[2048] = { 0 };
        char name[MAZE_NAME_SIZE] = { 0 };
        uint32_t color = 0;
        MazeLayout decoded = { 0 };
        size_t size = MazeFileFormat(&maze->layout, maze->name, maze->wallColor, text, sizeof(text));
        if (!check.ok || size == 0 || !MazeFileParse(text, &decoded, name, sizeof(name), &color) ||
            memcmp(&decoded, &maze->layout, sizeof(decoded)) != 0 || color != maze->wallColor)
        {
            fprintf(stderr, "invalid maze %s: %s\n", maze->name, check.message);
            exit(1);
        }
    }
    printf("maze validation and file roundtrip: %d layouts passed\n", pack.mazeCount);
}

static uint64_t Play(uint32_t seed, Samples *ticks, Samples *scene, Samples *cache)
{
    Game game = { 0 };
    GameConfig config = { .pack = &pack, .seed = seed, .startLevel = 1, .lives = 3 };
    GameNew(&game, &config);
    uint64_t hash = 0;
    for (int t = 0; t < 216000 && game.phase != PHASE_GAME_OVER; t++)
    {
        GameInput input = { .dir = game.phase == PHASE_PLAYING? AutopilotDecide(&game) : DIR_NONE };
        uint64_t start = CycleNow();
        GameTick(&game, &input);
        if (ticks != NULL) SamplesPush(ticks, CycleNow() - start);
        hash = hash*1099511628211ull ^ game.score ^ (uint32_t)game.pac.actor.x ^ ((uint64_t)(uint32_t)game.pac.actor.y << 32);
        if (scene != NULL)
        {
            start = CycleNow();
            SceneUpdateStaticLayer(&layer, &game, 672, 864);
            SamplesPush(scene, CycleNow() - start);
            start = CycleNow();
            SceneUpdateStaticLayer(&layer, &game, 672, 864);
            SamplesPush(cache, CycleNow() - start);
            SpriteBatch batch = { 0 };
            SpriteBatchBegin(&batch, vertices, 2048, 672, 864);
            SceneDrawDynamic(&batch, &game, 0.5f);
            DoNotOptimize(vertices);
        }
    }
    return hash;
}

int main(int argc, char **argv)
{
    int games = 8;
    uint32_t seed = 1;
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--games") == 0 && i + 1 < argc)
        {
            int value = atoi(argv[++i]);
            games = CLAMP(value, 1, 128);
        }
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) seed = (uint32_t)strtoul(argv[++i], NULL, 10);
        else return 2;
    }
    benchNsPerCycle = 1.0/CyclesPerNs(100);
    LevelPackInitBuiltin(&pack, BUILTIN_TOUR);
    CheckPack();
    uint64_t a = Play(seed, NULL, NULL, NULL);
    uint64_t b = Play(seed, NULL, NULL, NULL);
    if (a != b) return 1;
    printf("determinism passed: %016llx\n", (unsigned long long)a);
    Samples ticks = { 0 }, scene = { 0 }, cache = { 0 };
    if (!SamplesInit(&ticks, (size_t)games*216000) || !SamplesInit(&scene, (size_t)games*216000) || !SamplesInit(&cache, (size_t)games*216000)) return 1;
    uint64_t start = ClockNowNs();
    for (int g = 0; g < games; g++) Play(seed + (uint32_t)g, &ticks, &scene, &cache);
    printf("%zu simulation ticks, %.2f ms including autopilot and scene benchmarks\n", ticks.count, (double)(ClockNowNs() - start)/1e6);
    SummaryPrintHeader(stdout);
    BenchPrintCycles("GameTick", &ticks);
    BenchPrintCycles("Static layer update", &scene);
    BenchPrintCycles("Static cache hit", &cache);
    SamplesFree(&ticks);
    SamplesFree(&scene);
    SamplesFree(&cache);
    BenchMazeArt();
    BenchSynth();
    return 0;
}
