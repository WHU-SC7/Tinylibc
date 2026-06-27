#ifndef __TIME_H
#define __TIME_H

#include "tlibc_types.h"  /* time_t, clockid_t */

struct timespec {
    time_t tv_sec;
    long   tv_nsec;
};

/* clock_gettime / clock_nanosleep 时钟 ID */
#define CLOCK_REALTIME                  0
#define CLOCK_MONOTONIC                 1
#define CLOCK_PROCESS_CPUTIME_ID        2
#define CLOCK_THREAD_CPUTIME_ID         3
#define CLOCK_MONOTONIC_RAW             4
#define CLOCK_REALTIME_COARSE           5
#define CLOCK_MONOTONIC_COARSE          6
#define CLOCK_BOOTTIME                  7
#define CLOCK_REALTIME_ALARM            8
#define CLOCK_BOOTTIME_ALARM            9

#endif /* __TIME_H */
