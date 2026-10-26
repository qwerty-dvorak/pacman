#include "core/stats.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

bool SamplesInit(Samples *samples, size_t capacity)
{
    memset(samples, 0, sizeof(*samples));
    samples->values = malloc(capacity*sizeof(uint64_t));
    if (samples->values == NULL) return false;

    // Touch every page up front so the first measured pushes do not page-fault.
    memset(samples->values, 0, capacity*sizeof(uint64_t));
    samples->capacity = capacity;
    return true;
}

void SamplesFree(Samples *samples)
{
    free(samples->values);
    memset(samples, 0, sizeof(*samples));
}

void SamplesReset(Samples *samples)
{
    samples->count = 0;
    samples->dropped = 0;
}

static int CompareU64(const void *a, const void *b)
{
    const uint64_t x = *(const uint64_t *)a;
    const uint64_t y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

static uint64_t Percentile(const uint64_t *sorted, size_t count, double q)
{
    size_t index = (size_t)(q*(double)(count - 1) + 0.5);
    return sorted[MIN(index, count - 1)];
}

Summary SamplesSummarize(Samples *samples)
{
    Summary summary = { 0 };
    const size_t count = samples->count;
    if (count == 0) return summary;

    uint64_t *values = samples->values;
    qsort(values, count, sizeof(uint64_t), CompareU64);

    uint64_t total = 0;
    for (size_t i = 0; i < count; i++) total += values[i];

    const double mean = (double)total/(double)count;
    double variance = 0.0;
    for (size_t i = 0; i < count; i++)
    {
        const double d = (double)values[i] - mean;
        variance += d*d;
    }

    summary.count = count;
    summary.total = total;
    summary.mean = mean;
    summary.stddev = sqrt(variance/(double)count);
    summary.min = values[0];
    summary.p50 = Percentile(values, count, 0.50);
    summary.p90 = Percentile(values, count, 0.90);
    summary.p99 = Percentile(values, count, 0.99);
    summary.p999 = Percentile(values, count, 0.999);
    summary.max = values[count - 1];
    return summary;
}

void SummaryPrintHeader(FILE *out)
{
    fprintf(out, "%-26s %10s %9s %9s %9s %9s %9s %9s %9s\n",
            "metric (us)", "samples", "mean", "stddev", "min", "p50", "p99", "p99.9", "max");
}

void SummaryPrintRow(FILE *out, const char *label, const Summary *summary)
{
    fprintf(out, "%-26s %10zu %9.3f %9.3f %9.3f %9.3f %9.3f %9.3f %9.3f\n", label, summary->count,
            summary->mean/1e3, summary->stddev/1e3, (double)summary->min/1e3, (double)summary->p50/1e3,
            (double)summary->p99/1e3, (double)summary->p999/1e3, (double)summary->max/1e3);
}
