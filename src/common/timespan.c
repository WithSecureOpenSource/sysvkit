#include "timespan.h"

#include "common.h"
#include "noise.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INFINITY_STR "infinity"
#define INFINITY_STRLEN (sizeof(INFINITY_STR) - 1)

struct timespan_unit {
    const char *name;
    usec_t value;
};

static struct timespan_unit timespan_units[] = {
    { "usec", TS_USEC },  { "us", TS_USEC },      { "msec", TS_MSEC },
    { "ms", TS_MSEC },    { "seconds", TS_SEC },  { "second", TS_SEC },
    { "sec", TS_SEC },    { "s", TS_SEC },        { "minutes", TS_MIN },
    { "minute", TS_MIN }, { "min", TS_MIN },      { "m", TS_MIN },
    { "hours", TS_HR },   { "hour", TS_HR },      { "hr", TS_HR },
    { "h", TS_HR },       { "days", TS_DAY },     { "day", TS_DAY },
    { "d", TS_DAY },      { "weeks", TS_WEEK },   { "week", TS_WEEK },
    { "w", TS_WEEK },     { "months", TS_MONTH }, { "month", TS_MONTH },
    { "M", TS_MONTH },    { "years", TS_YEAR },   { "year", TS_YEAR },
    { "y", TS_YEAR },     { NULL, TS_INVALID },
};

// locale-agnostic and inlinable ctype workalikes
static inline int isblank(int ch)
{
    return (ch == ' ' || ch == '\t' || ch == '\n');
}
static inline int isalpha(int ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z');
}
static inline int isdigit(int ch)
{
    return (ch >= '0' && ch <= '9');
}

// Parses a Systemd time span and returns its value in microseconds.  On error,
// returns TS_INVALID and sets errno to 0 if the string is empty, EINVAL if it
// contains a syntax error, or ERANGE if the result overflows.
//
// See <https://www.freedesktop.org/software/systemd/man/systemd.time.html>.
usec_t timespan_from_str(const char *str)
{
    const struct timespan_unit *tsu;
    uintmax_t num;
    usec_t ts = 0;
    const char *p, *q, *r, *end, *err;

    // trim leading and trailing space, check for empty string
    for (p = str; isblank((unsigned char)*p); p++) {
        // nothing
    }
    for (end = q = p; *q != '\0'; q++) {
        if (!isblank((unsigned char)*q)) {
            end = q + 1;
        }
    }
    if (end == p) {
        debug("empty time span");
        errno = 0;
        return TS_INVALID;
    }

    // special case: infinity
    if (q - p == INFINITY_STRLEN
        && strncmp(p, INFINITY_STR, INFINITY_STRLEN) == 0) {
        return TS_INFINITY;
    }

    // special case: unitless == seconds
    if (isdigit(*p)) {
        num = strtoumax(p, DQ(&q), 10);
        if (num == UINTMAX_MAX && errno == ERANGE) {
            return TS_INVALID;
        }
        if (q != p) {
            for (; isblank((unsigned char)*q); q++) {
                // nothing
            }
            if (*q == '\0') {
                return num * TS_SEC;
            }
        }
    }

    // start over
    while (p < end) {
        err = p;
        if (!isdigit(*p)) {
            goto syntax;
        }
        // magnitude and optional space
        num = strtoumax(p, DQ(&q), 10);
        if (num == UINTMAX_MAX && errno == ERANGE) {
            return TS_INVALID;
        }
        if (q == p) {
            goto syntax;
        }
        for (; q < end && isblank((unsigned char)*q); q++) {
            // nothing
        }
        err = q;
        // unit
        for (r = q; r < end && isalpha((unsigned char)*r); r++) {
            // nothing
        }
        if (r == q) {
            err = q;
            goto syntax;
        }
        // validate unit
        for (tsu = timespan_units; tsu->name != NULL; tsu++) {
            if (strncmp(tsu->name, q, r - q) == 0 && tsu->name[r - q] == '\0') {
                break;
            }
        }
        if (tsu->name == NULL) {
            err = q;
            goto syntax;
        }
        debug("%llu us + %ju %s", ts, num, tsu->name);
        num *= tsu->value;
        if (TS_INVALID - ts <= num) {
            // overflow
            errno = ERANGE;
            return TS_INVALID;
        }
        ts += num;
        // optional space
        for (p = r; p < end && isblank((unsigned char)*p); p++) {
            // nothing
        }
    }
    return ts;
syntax:
    debug("invalid time span: '%.*s>%.*s'",
          (int)(err - str),
          str,
          (int)(end - err),
          err);
    errno = EINVAL;
    return TS_INVALID;
}

int timespan_to_str(char *buf, size_t size, usec_t ts)
{
    struct timespan_unit *tsu;
    size_t len = 0;
    int res;

    if (buf == NULL || size == 0) {
        buf = NULL;
        size = 0;
    }
    if (ts == 0) {
        return snprintf(buf, size, "0");
    }
    if (ts == TS_INVALID) {
        errno = EINVAL;
        return -1;
    }
    if (ts == TS_INFINITY) {
        return snprintf(buf, size, "%s", INFINITY_STR);
    }
    for (tsu = timespan_units; tsu->name != NULL; tsu++) {
        // nothing
    }
    while (ts > 0) {
        while (tsu > timespan_units && tsu->value > ts) {
            tsu--;
        }
        res = snprintf(buf, size, "%llu%s", ts / tsu->value, tsu->name);
        ts = ts % tsu->value;
        if (res < 0) {
            return -1;
        }
        len += res;
        if (buf != NULL) {
            if ((size_t)res < size) {
                buf += res;
                size -= res;
            } else {
                // leave only enough for the terminator
                buf += size - 1;
                size = 1;
            }
        }
    }
    if (buf != NULL) {
        *buf = '\0';
    }
    return len;
}
