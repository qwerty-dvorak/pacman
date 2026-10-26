#ifndef CORE_STATS_H
#define CORE_STATS_H

#include "core/base.h"

#include <stdio.h>

// Fixed-capacity sample recorder. Push is branch-light and never allocates, so it is safe
// inside measured loops; summarizing sorts the samples in place.
typedef struct Samples {
    uint64_t *values;
    size_t count;
    size_t capacity;
    size_t dropped;
} Samples;

typedef struct Summary {
    size_t count;
    double mean;
    double stddev;
    uint64_t min;
    uint64_t p50;
    uint64_t p90;
    uint64_t p99;
    uint64_t p999;
    uint64_t max;
    uint64_t total;
} Summary;

bool SamplesInit(Samples *samples, size_t capacity);
void SamplesFree(Samples *samples);
void SamplesReset(Samples *samples);

FORCE_INLINE void SamplesPush(Samples *samples, uint64_t value)
{
    if (LIKELY(samples->count < samples->capacity)) samples->values[samples->count++] = value;
    else samples->dropped++;
}

Summary SamplesSummarize(Samples *samples);

// Prints one aligned row; values are nanoseconds and shown in microseconds.
void SummaryPrintHeader(FILE *out);
void SummaryPrintRow(FILE *out, const char *label, const Summary *summary);

#endif // CORE_STATS_H
