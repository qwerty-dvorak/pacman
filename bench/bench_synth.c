#include "bench/bench_common.h"
#include "audio/synth.h"
#include <stdio.h>
#include <stdlib.h>

void BenchSynth(void)
{
    Synth synth = { 0 };
    SynthInit(&synth);
    SynthCommand music = { .type = SYNTH_COMMAND_MUSIC, .id = MUSIC_MENU, .param = 1 };
    SynthCommand ambience = { .type = SYNTH_COMMAND_AMBIENCE, .id = AMBIENCE_SIREN, .param = 4 };
    SynthApply(&synth, &music);
    SynthApply(&synth, &ambience);
    Samples samples = { 0 };
    int16_t buffer[1024] = { 0 };
    if (!SamplesInit(&samples, MICRO_BATCHES)) exit(1);
    for (int i = 0; i < MICRO_BATCHES; i++)
    {
        SynthCommand sfx = { .type = SYNTH_COMMAND_SFX, .id = (uint8_t)(i%SFX_COUNT) };
        SynthApply(&synth, &sfx);
        uint64_t start = CycleNow();
        SynthRender(&synth, buffer, 1024);
        SamplesPush(&samples, CycleNow() - start);
        DoNotOptimize(buffer);
    }
    printf("\n== synth 1024 sample blocks ==\n");
    SummaryPrintHeader(stdout);
    BenchPrintCycles("SynthRender", &samples);
    SamplesFree(&samples);
}
