#define _POSIX_C_SOURCE 200809L

#include "core/clock.h"

#include <time.h>
#include <errno.h>

uint64_t ClockNowNs(void)
{
    struct timespec ts = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec*NS_PER_SECOND + (uint64_t)ts.tv_nsec;
}

#define SPIN_WINDOW_NS  250000ull

void SleepUntilNs(uint64_t deadline)
{
    const uint64_t now = ClockNowNs();
    if (deadline <= now) return;

    if (deadline - now > SPIN_WINDOW_NS)
    {
        const uint64_t wake = deadline - SPIN_WINDOW_NS;
        const struct timespec ts = { .tv_sec = (time_t)(wake/NS_PER_SECOND), .tv_nsec = (long)(wake%NS_PER_SECOND) };
        while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) == EINTR) { }
    }
    while (ClockNowNs() < deadline) __builtin_ia32_pause();
}

double CyclesPerNs(uint32_t calibrationMs)
{
    const uint64_t ns0 = ClockNowNs();
    const uint64_t c0 = CycleNow();
    const uint64_t target = ns0 + (uint64_t)calibrationMs*1000000ull;

    uint64_t ns1 = ns0;
    while (ns1 < target) ns1 = ClockNowNs();
    const uint64_t c1 = CycleNow();

    return (double)(c1 - c0)/(double)(ns1 - ns0);
}
