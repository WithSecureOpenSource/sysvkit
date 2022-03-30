#include "noise.h"

#include "clock.h"

#include <fsdyn/charstr.h>

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

// How noisy do we want to be?
enum noise noisy;

// Which file to print to; set to NULL for syslog.
FILE *noisef;

// Initialize noisef to stderr on startup as it is not a compile-time constant.
static void noise_init(void) __attribute__((__constructor__));
static void noise_init(void)
{
    noisef = stderr;
}

static const char *prefix[] = {
    [LOG_DEBUG] = "# ",          [LOG_INFO] = "",       [LOG_NOTICE] = "",
    [LOG_WARNING] = "WARNING: ", [LOG_ERR] = "ERROR: ",
};

static int fs_vlog(int pri, const char *, va_list)
    __attribute__((__format__(printf, 2, 0)));
static int fs_vlog(int pri, const char *fmt, va_list ap)
{
    char *p, *q;
    char *msg;
    usec_t now;
    int n, res, serrno;

    serrno = errno;
    msg = charstr_vprintf(fmt, ap);
    for (res = n = 0, p = q = msg; *q && *p; p = q + 1, res += n) {
        for (q = p; *q != '\0' && *q != '\n'; q++) {
            // suppress non-printable characters
            if (*q < ' ') {
                *q = ' ';
            }
        }
        if (noisef != NULL) {
            now = clock_realtime_usec();
            fprintf(noisef,
                    "%llu.%06llu [%u] %s%.*s\n%n",
                    now / 1000000,
                    now % 1000000,
                    (unsigned int)getpid(),
                    prefix[pri],
                    (int)(q - p),
                    p,
                    &n);
        } else {
            syslog(pri, "%.*s%n", (int)(q - p), p, &n);
        }
    }
    fsfree(msg);
    if (noisef != NULL && res > 0) {
        fflush(noisef);
    }
    errno = serrno;
    return res;
}

int fs_debug(const char *fmt, ...)
{
    va_list ap;
    int res;

    if (noisy < DEBUG) {
        return 0;
    }
    va_start(ap, fmt);
    res = fs_vlog(LOG_DEBUG, fmt, ap);
    va_end(ap);
    return res;
}

int fs_verbose(const char *fmt, ...)
{
    va_list ap;
    int res;

    if (noisy < VERBOSE) {
        return 0;
    }
    va_start(ap, fmt);
    res = fs_vlog(LOG_INFO, fmt, ap);
    va_end(ap);
    return res;
}

int fs_info(const char *fmt, ...)
{
    va_list ap;
    int res;

    if (noisy < NORMAL) {
        return 0;
    }
    va_start(ap, fmt);
    res = fs_vlog(LOG_NOTICE, fmt, ap);
    va_end(ap);
    return res;
}

int fs_warning(const char *fmt, ...)
{
    va_list ap;
    int res;

    if (noisy < QUIET) {
        return 0;
    }
    va_start(ap, fmt);
    res = fs_vlog(LOG_WARNING, fmt, ap);
    va_end(ap);
    return res;
}

int fs_error(const char *fmt, ...)
{
    va_list ap;
    int res;

    va_start(ap, fmt);
    res = fs_vlog(LOG_ERR, fmt, ap);
    va_end(ap);
    return res;
}

void fs_fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    (void)fs_vlog(LOG_ERR, fmt, ap);
    va_end(ap);
    _exit(EXIT_FAILURE);
}

void fs_fatalx(int code, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    (void)fs_vlog(LOG_ERR, fmt, ap);
    va_end(ap);
    _exit(code);
}

// Sets the noise level as specified by the argument: 's'ilent, 'q'uiet,
// 'v'erbose, 'd'ebug.  If called multiple times, only the last call applies,
// with one exception: multiple calls with 'd' will increase the noise level
// beyond DEBUG, which may result in an unmanageable amount of detail.  Returns
// zero if the argument was valid; otherwise, returns a negative value and sets
// errno to EINVAL.
int noise_set_level(char ch)
{
    switch (tolower(ch)) {
        case 'd':
            if (noisy >= DEBUG) {
                noisy++;
            } else {
                noisy = DEBUG;
            }
            break;
        case 'q':
            noisy = QUIET;
            break;
        case 's':
            noisy = SILENT;
            break;
        case 'v':
            noisy = VERBOSE;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    return 0;
}

// Processes a noise override string, which is either one of the words “silent”,
// “quiet”, “normal”, “verbose”, or “debug”, or a sequence of characters each of
// which is a valid argument to noise_set_level().  If the argument is NULL,
// uses the value of the SYSVKIT_NOISE environment variable instead.  Returns
// zero if the argument is valid or empty, or the argument is NULL but the value
// of the SYSVKIT_NOISE environment variable is valid or empty, or the argument
// is NULL and the SYSVKIT_NOISE environment variable is unset; otherwise,
// returns a negative value, sets errno to EINVAL, and leaves the noise level
// unchanged.
int noise_override(const char *str)
{
    enum noise snoisy;

    if (str == NULL) {
        str = getenv(NOISE_ENVVAR);
        if (str == NULL) {
            return 0;
        }
    }
    snoisy = noisy;
    if (strcasecmp(str, "debug") == 0) {
        noisy = DEBUG;
    } else if (strcasecmp(str, "verbose") == 0) {
        noisy = VERBOSE;
    } else if (strcasecmp(str, "normal") == 0) {
        noisy = NORMAL;
    } else if (strcasecmp(str, "quiet") == 0) {
        noisy = QUIET;
    } else if (strcasecmp(str, "silent") == 0) {
        noisy = SILENT;
    } else {
        while (*str != '\0') {
            if (noise_set_level(*str) != 0) {
                noisy = snoisy;
                errno = EINVAL;
                return -1;
            }
            str++;
        }
    }
    return 0;
}
