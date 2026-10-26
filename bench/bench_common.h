#ifndef BENCH_BENCH_COMMON_H
#define BENCH_BENCH_COMMON_H

#include "core/clock.h"
#include "core/stats.h"

// Shared by every suite linked into bench_cpu. Samples are recorded in TSC cycles and
// converted to nanoseconds when printed.
extern double benchNsPerCycle;

// Calls per timed batch in micro benchmarks; the per-call average is recorded per batch.
#define MICRO_BATCH     256
#define MICRO_BATCHES   4096

void BenchPrintCycles(const char *label, Samples *samples);

// Suites implemented in their own files.
void BenchMazeArt(void);
void BenchSynth(void);

#endif // BENCH_BENCH_COMMON_H
