/*
 * gpsstats - statistics for GPS daemon
 *
 * Copyright: (C) 2019 jawi
 *   License: Apache License 2.0
 */

#ifndef _TIMESPEC_H
#define _TIMESPEC_H

#include <time.h>

// The following code is taken from gpsd's timespec.h

#define NS_IN_SEC       1000000000L     /* nanoseconds in a second */

static inline void TS_NORM(struct timespec *ts) {
    if ((1 <= ts->tv_sec) || ((0 == ts->tv_sec) && (0 <= ts->tv_nsec))) {
        /* result is positive */
        if (NS_IN_SEC <= ts->tv_nsec) {
            /* borrow from tv_sec */
            ts->tv_nsec -= NS_IN_SEC;
            ts->tv_sec++;
        } else if (0 > ts->tv_nsec) {
            /* carry to tv_sec */
            ts->tv_nsec += NS_IN_SEC;
            ts->tv_sec--;
        }
    }  else {
        /* result is negative */
        if (-NS_IN_SEC >= ts->tv_nsec) {
            /* carry to tv_sec */
            ts->tv_nsec += NS_IN_SEC;
            ts->tv_sec--;
        } else if (0 < ts->tv_nsec) {
            /* borrow from tv_sec */
            ts->tv_nsec -= NS_IN_SEC;
            ts->tv_sec++;
        }
    }
}

/* subtract two timespec */
#define TS_SUB(r, ts1, ts2) \
    do { \
        (r)->tv_sec = (ts1)->tv_sec - (ts2)->tv_sec; \
        (r)->tv_nsec = (ts1)->tv_nsec - (ts2)->tv_nsec; \
        TS_NORM(r); \
    } while (0)

/* convert a timespec to a double.
 * if tv_sec > 2, then inevitable loss of precision in tv_nsec
 * so best to NEVER use TSTONS()
 * WARNING replacing 1e9 with NS_IN_SEC causes loss of precision */
#define TSTONS(ts) ((double)(ts)->tv_sec + ((double)(ts)->tv_nsec / 1e9))
//#define TSTONS(ts) ((double)(((ts)->tv_sec * NS_IN_SEC) + (ts)->tv_nsec) / 1e9)

#endif