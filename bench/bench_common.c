#include "bench/bench_common.h"

#include <stdio.h>

double benchNsPerCycle = 1.0;

void BenchPrintCycles(const char *label, Samples *samples)
{
    Summary s = SamplesSummarize(samples);
    const double k = benchNsPerCycle;
    s.mean *= k;
    s.stddev *= k;
    s.min = (uint64_t)((double)s.min*k);
    s.p50 = (uint64_t)((double)s.p50*k);
    s.p90 = (uint64_t)((double)s.p90*k);
    s.p99 = (uint64_t)((double)s.p99*k);
    s.p999 = (uint64_t)((double)s.p999*k);
    s.max = (uint64_t)((double)s.max*k);
    SummaryPrintRow(stdout, label, &s);
}
