#include "clock.h"

#include <time.h>

// Return a monotonically increasing timer in microseconds.  The
// origin and granularity are unspecified, but on Linux, the origin is
// the system boot time (minus any time the system was suspended) and
// the granularity is 1 microsecond.
usec_t clock_usec(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return s2us((usec_t)ts.tv_sec) + ns2us((usec_t)ts.tv_nsec);
}

// Returns the current time in microseconds since the Unix Epoch.
// This may go backward if the clock is set, or jump forward if the
// system is suspended.
usec_t clock_realtime_usec(void)
{
    struct timespec ts;

    // Supported since Linux 2.6.32
    clock_gettime(CLOCK_REALTIME_COARSE, &ts);
    return s2us((usec_t)ts.tv_sec) + ns2us((usec_t)ts.tv_nsec);
}

