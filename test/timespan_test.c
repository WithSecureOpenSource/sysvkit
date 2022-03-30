#define _GNU_SOURCE

#include "noise.h"
#include "timespan.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static unsigned int ec;

#define TS_PI_STR1 "31y4M1w5d9h26m53s589ms793us"
#define TS_PI_STR2 "31y 4M 1w 5d 9h 26m 53s 589ms 793us"
#define TS_PI_STR3 "31 y 4 M 1 w 5 d 9 h 26 m 53 s 589 ms 793 us"
#define TS_PI                                                           \
    (31 * TS_YEAR + 4 * TS_MONTH + 1 * TS_WEEK + 5 * TS_DAY + 9 * TS_HR \
     + 26 * TS_MIN + 53 * TS_SEC + 589 * TS_MSEC + 793 * TS_USEC)

// as above, but skip some
#define TS_PI_SKIP_STR "31y4w15h9s265us"
#define TS_PI_SKIP \
    (31 * TS_YEAR + 4 * TS_WEEK + 15 * TS_HR + 9 * TS_SEC + 265 * TS_USEC)

static struct test_case_from_str {
    const char *str; // input string
    usec_t ts;       // return value
    int err;         // errno if ts == TS_INVALID
} test_cases_from_str[] = {
    // empty or blank
    { "", TS_INVALID, 0 },
    { " ", TS_INVALID, 0 },
    { "\t", TS_INVALID, 0 },
    { " \t ", TS_INVALID, 0 },
    // leading or trailing space
    { " 1", TS_SEC, 0 },
    { "1 ", TS_SEC, 0 },
    { " 1 ", TS_SEC, 0 },
    // mixed up
    { "0 0", TS_INVALID, EINVAL },
    { "0s 0s", 0, 0 },
    // negative numbers
    { "-0", TS_INVALID, EINVAL },
    { "-0s", TS_INVALID, EINVAL },
    { "0s -0s", TS_INVALID, EINVAL },
    // missing magnitude
    { "s", TS_INVALID, EINVAL },
    // misspelled unit
    { "0 sic", TS_INVALID, EINVAL },
    // minimal valid cases
    { "0", 0, 0 },
    { "1", TS_SEC, 0 },
    { TS_PI_STR1, TS_PI, 0 },
    { TS_PI_STR2, TS_PI, 0 },
    { TS_PI_STR3, TS_PI, 0 },
    { TS_PI_SKIP_STR, TS_PI_SKIP, 0 },
    // all units
    { "0usec", 0, 0 },
    { "0us", 0, 0 },
    { "0msec", 0, 0 },
    { "0ms", 0, 0 },
    { "0seconds", 0, 0 },
    { "0second", 0, 0 },
    { "0sec", 0, 0 },
    { "0s", 0, 0 },
    { "0minutes", 0, 0 },
    { "0minute", 0, 0 },
    { "0min", 0, 0 },
    { "0m", 0, 0 },
    { "0hours", 0, 0 },
    { "0hour", 0, 0 },
    { "0hr", 0, 0 },
    { "0h", 0, 0 },
    { "0days", 0, 0 },
    { "0day", 0, 0 },
    { "0d", 0, 0 },
    { "0weeks", 0, 0 },
    { "0week", 0, 0 },
    { "0w", 0, 0 },
    { "0months", 0, 0 },
    { "0month", 0, 0 },
    { "0M", 0, 0 },
    { "0years", 0, 0 },
    { "0year", 0, 0 },
    { "0y", 0, 0 },
    { "0 usec", 0, 0 },
    { "0 us", 0, 0 },
    { "0 msec", 0, 0 },
    { "0 ms", 0, 0 },
    { "0 seconds", 0, 0 },
    { "0 second", 0, 0 },
    { "0 sec", 0, 0 },
    { "0 s", 0, 0 },
    { "0 minutes", 0, 0 },
    { "0 minute", 0, 0 },
    { "0 min", 0, 0 },
    { "0 m", 0, 0 },
    { "0 hours", 0, 0 },
    { "0 hour", 0, 0 },
    { "0 hr", 0, 0 },
    { "0 h", 0, 0 },
    { "0 days", 0, 0 },
    { "0 day", 0, 0 },
    { "0 d", 0, 0 },
    { "0 weeks", 0, 0 },
    { "0 week", 0, 0 },
    { "0 w", 0, 0 },
    { "0 months", 0, 0 },
    { "0 month", 0, 0 },
    { "0 M", 0, 0 },
    { "0 years", 0, 0 },
    { "0 year", 0, 0 },
    { "0 y", 0, 0 },
    // all units, but with a non-zero value
    { "1usec", TS_USEC, 0 },
    { "1us", TS_USEC, 0 },
    { "1msec", TS_MSEC, 0 },
    { "1ms", TS_MSEC, 0 },
    { "1seconds", TS_SEC, 0 },
    { "1second", TS_SEC, 0 },
    { "1sec", TS_SEC, 0 },
    { "1s", TS_SEC, 0 },
    { "1minutes", TS_MIN, 0 },
    { "1minute", TS_MIN, 0 },
    { "1min", TS_MIN, 0 },
    { "1m", TS_MIN, 0 },
    { "1hours", TS_HR, 0 },
    { "1hour", TS_HR, 0 },
    { "1hr", TS_HR, 0 },
    { "1h", TS_HR, 0 },
    { "1days", TS_DAY, 0 },
    { "1day", TS_DAY, 0 },
    { "1d", TS_DAY, 0 },
    { "1weeks", TS_WEEK, 0 },
    { "1week", TS_WEEK, 0 },
    { "1w", TS_WEEK, 0 },
    { "1months", TS_MONTH, 0 },
    { "1month", TS_MONTH, 0 },
    { "1M", TS_MONTH, 0 },
    { "1years", TS_YEAR, 0 },
    { "1year", TS_YEAR, 0 },
    { "1y", TS_YEAR, 0 },
    { "1 usec", TS_USEC, 0 },
    { "1 us", TS_USEC, 0 },
    { "1 msec", TS_MSEC, 0 },
    { "1 ms", TS_MSEC, 0 },
    { "1 seconds", TS_SEC, 0 },
    { "1 second", TS_SEC, 0 },
    { "1 sec", TS_SEC, 0 },
    { "1 s", TS_SEC, 0 },
    { "1 minutes", TS_MIN, 0 },
    { "1 minute", TS_MIN, 0 },
    { "1 min", TS_MIN, 0 },
    { "1 m", TS_MIN, 0 },
    { "1 hours", TS_HR, 0 },
    { "1 hour", TS_HR, 0 },
    { "1 hr", TS_HR, 0 },
    { "1 h", TS_HR, 0 },
    { "1 days", TS_DAY, 0 },
    { "1 day", TS_DAY, 0 },
    { "1 d", TS_DAY, 0 },
    { "1 weeks", TS_WEEK, 0 },
    { "1 week", TS_WEEK, 0 },
    { "1 w", TS_WEEK, 0 },
    { "1 months", TS_MONTH, 0 },
    { "1 month", TS_MONTH, 0 },
    { "1 M", TS_MONTH, 0 },
    { "1 years", TS_YEAR, 0 },
    { "1 year", TS_YEAR, 0 },
    { "1 y", TS_YEAR, 0 },
    // infinity
    { "infinity", TS_INFINITY, 0 },
    // largest possible value less than infinity
    { "18446744073709551613us", 18446744073709551613ULL, 0 },
    { "18446744073708551613us 1s", 18446744073709551613ULL, 0 },
    // these work out to TS_INVALID
    { "18446744073709551615us", TS_INVALID, ERANGE },
    { "18446744073708551615us 1s", TS_INVALID, ERANGE },
    // these overflow during addition
    { "18446744073709551613us 1s", TS_INVALID, ERANGE },
    { "18446744073708551616us 1s", TS_INVALID, ERANGE },
    // this overflows strtoul()
    { "18446744073709551616", TS_INVALID, ERANGE },
    { "0s 18446744073709551616us", TS_INVALID, ERANGE },
};

static void test_from_str(void)
{
    struct test_case_from_str *tc;
    usec_t ret;
    unsigned int i, n;

    n = sizeof(test_cases_from_str) / sizeof(test_cases_from_str[0]);
    printf("1..%u\n", n);
    for (i = 0, tc = test_cases_from_str; i < n; i++, tc++) {
        errno = 0;
        ret = timespan_from_str(tc->str);
        if (ret == tc->ts && errno == tc->err) {
            printf("ok %u - \"%s\" -> ", i, tc->str);
            if (ret == TS_INVALID) {
                printf("errno %d", errno);
            } else {
                printf("%llu", ret);
            }
        } else {
            printf("not ok %u - \"%s\" expected ", i, tc->str);
            if (tc->ts == TS_INVALID) {
                printf("errno %d", tc->err);
            } else {
                printf("%llu", tc->ts);
            }
            printf(" got ");
            if (ret == TS_INVALID) {
                printf("errno %d", errno);
            } else {
                printf("%llu", ret);
            }
            ec++;
        }
        printf("\n");
    }
}

static struct test_case_to_str {
    unsigned long ts; // input value
    size_t size;      // input buffer size
    const char *str;  // result string
    int ret;          // return value
    int err;          // errno if ret < 0
} test_cases_to_str[] = {
    // minimum case
    { 0, 0, NULL, 1, 0 },
    // just the length
    { TS_PI, 0, NULL, sizeof(TS_PI_STR1) - 1, 0 },
    // kitchen sink
    { TS_PI, SIZE_MAX, TS_PI_STR1, sizeof(TS_PI_STR1) - 1, 0 },
    { TS_PI, 4, TS_PI_STR1, sizeof(TS_PI_STR1) - 1, 0 },
    { TS_PI_SKIP, SIZE_MAX, TS_PI_SKIP_STR, sizeof(TS_PI_SKIP_STR) - 1, 0 },
    // infinity
    { TS_INFINITY, SIZE_MAX, "infinity", sizeof("infinity") - 1, 0 },
    // XXX add more cases, can't be bothered rn as it sees little use
};

static void test_to_str(void)
{
    char bytes[256], *buf;
    struct test_case_to_str *tc;
    size_t size;
    unsigned int i, n;
    int ret;

    n = sizeof(test_cases_to_str) / sizeof(test_cases_to_str[0]);
    printf("1..%u\n", n);
    for (i = 1, tc = test_cases_to_str; i <= n; i++, tc++) {
        memset(bytes, 0, sizeof(bytes));
        buf = bytes;
        if (tc->str == NULL) {
            buf = NULL;
        }
        size = tc->size;
        if (size > sizeof(bytes)) {
            size = sizeof(bytes);
        }
        errno = 0;
        ret = timespan_to_str(buf, size, tc->ts);
        if (ret == tc->ret && (ret >= 0 || errno == tc->err)
            && (buf == NULL || strncmp(buf, tc->str, size - 1) == 0)) {
            printf("ok %u - %lu ", i, tc->ts);
            if (ret < 0) {
                printf("-> errno %d", errno);
            } else if (buf == NULL) {
                printf("(no buffer) -> %d", ret);
            } else {
                printf(" -> \"%s\" (%d)", buf, ret);
            }
        } else {
            printf("not ok %u - %lu expected ", i, tc->ts);
            if (tc->ret < 0) {
                printf("errno %d", tc->err);
            } else if (tc->ret > 0 && tc->str != NULL && tc->size > 0) {
                printf("\"%.*s\" (%d)", tc->ret, tc->str, tc->ret);
            } else {
                printf("%d", tc->ret);
            }
            printf(" got ");
            if (ret < 0) {
                printf("errno %d", errno);
            } else if (ret > 0 && buf != NULL && size > 0) {
                printf("\"%s\" (%d)", buf, ret);
            } else {
                printf("%d", ret);
            }
            ec++;
        }
        printf("\n");
    }
}

static void usage(void) __attribute__((__noreturn__));
static void usage(void)
{
    fprintf(stderr, "usage: %s [-dhqv]\n", program_invocation_short_name);
    exit(1);
}

int main(int argc, char *argv[])
{
    int opt;

    while ((opt = getopt(argc, argv, "dhqv")) != -1) {
        switch (opt) {
            case 'd':
                if (noisy >= DEBUG) {
                    noisy++;
                } else {
                    noisy = DEBUG;
                }
                break;
            case 'h':
                usage();
                break;
            case 'q':
                noisy = QUIET;
                break;
            case 'v':
                noisy = VERBOSE;
                break;
            default:
                usage();
                break;
        }
    }
    argc -= optind;
    argv += optind;
    if (argc > 0) {
        usage();
    }

    test_from_str();
    test_to_str();
    exit(ec == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
