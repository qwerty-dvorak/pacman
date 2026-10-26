#ifndef CORE_CLOCK_H
#define CORE_CLOCK_H

#include "core/base.h"

#if defined(__x86_64__) || defined(__i386__)
    #include <x86intrin.h>
#endif

#define NS_PER_SECOND   1000000000ull

// Monotonic clock in nanoseconds (vDSO clock_gettime, ~20 ns per call).
uint64_t ClockNowNs(void);

// Sleeps until the monotonic clock reaches deadline: the kernel sleeps through most of the
// wait (no CPU use), the last ~250 us are spun for sub-10 us precision.
void SleepUntilNs(uint64_t deadline);

// Raw invariant TSC: ~7 ns per read, for timing sub-microsecond work. Not serializing;
// callers measure whole operations, not single instructions.
FORCE_INLINE uint64_t CycleNow(void)
{
#if defined(__x86_64__) || defined(__i386__)
    return __rdtsc();
#else
    return ClockNowNs();
#endif
}

// Measures the cycle counter rate against the monotonic clock (blocks ~calibrationMs).
double CyclesPerNs(uint32_t calibrationMs);

// Keeps a value alive so the optimizer cannot delete benchmarked work.
FORCE_INLINE void DoNotOptimize(const void *p)
{
    __asm__ volatile("" : : "r"(p) : "memory");
}

#endif // CORE_CLOCK_H
