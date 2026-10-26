#include "bench/bench_common.h"
#include "scene/maze_art.h"
#include <stdio.h>
#include <stdlib.h>

void BenchMazeArt(void)
{
    uint8_t pixels[MAZE_WIDTH*MAZE_HEIGHT*4] = { 0 };
    Samples samples = { 0 };
    if (!SamplesInit(&samples, 128)) exit(1);
    for (int i = 0; i < 128; i++)
    {
        uint64_t start = CycleNow();
        MazeArtRender(&mazeLayoutClassic, RGBA(33,33,255,255), RGBA(255,183,255,255), pixels);
        SamplesPush(&samples, CycleNow() - start);
        DoNotOptimize(pixels);
    }
    printf("\n== maze artwork ==\n");
    SummaryPrintHeader(stdout);
    BenchPrintCycles("MazeArtRender", &samples);
    SamplesFree(&samples);
}
