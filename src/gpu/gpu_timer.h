#ifndef GPU_GPU_TIMER_H
#define GPU_GPU_TIMER_H

#include <stdbool.h>
#include <stdint.h>

// GL_TIME_ELAPSED queries in a ring, read back without stalling: a result is collected
// only once the GPU reports it available, typically a few frames after submission.
#define GPU_TIMER_RING  8

typedef struct GpuTimer {
    unsigned int queries[GPU_TIMER_RING];
    uint32_t head;          // Next query to begin.
    uint32_t tail;          // Oldest query not yet read back.
    bool active;
} GpuTimer;

void GpuTimerInit(GpuTimer *timer);
void GpuTimerShutdown(GpuTimer *timer);
void GpuTimerBegin(GpuTimer *timer);
void GpuTimerEnd(GpuTimer *timer);

// Collects finished queries; returns how many nanosecond results were written to out.
int GpuTimerCollect(GpuTimer *timer, uint64_t *out, int capacity);

#endif // GPU_GPU_TIMER_H
