#pragma once

#include "clock.h"

#include <stddef.h>

#define TS_USEC (usec_t)1
#define TS_MSEC (1000 * TS_USEC)
#define TS_SEC (1000 * TS_MSEC)
#define TS_MIN (60 * TS_SEC)
#define TS_HR (60 * TS_MIN)
#define TS_DAY (24 * TS_HR)
#define TS_WEEK (7 * TS_DAY)
#define TS_MONTH (3044 * TS_DAY / 100)
#define TS_YEAR (36525 * TS_DAY / 100)

#define TS_INFINITY (usec_t)(-2)
#define TS_INVALID (usec_t)(-1)

usec_t timespan_from_str(const char *);
int timespan_to_str(char *, size_t, usec_t);
