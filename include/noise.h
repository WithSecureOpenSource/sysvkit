#pragma once

#include <stdio.h>

#define NOISE_ENVVAR "SYSVKIT_NOISE"

extern enum noise {
    SILENT = -1,
    QUIET = -1,
    NORMAL = 0,
    VERBOSE = 1,
    DEBUG = 2
} noisy;

int noise_set_level(char);
int noise_override(const char *);

extern FILE *noisef;

int fs_debug(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
int fs_verbose(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
int fs_info(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
int fs_warning(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
int fs_error(const char *, ...) __attribute__((__format__(__printf__, 1, 2)));
void fs_fatal(const char *, ...)
    __attribute__((__format__(__printf__, 1, 2), __noreturn__));
void fs_fatalx(int, const char *, ...)
    __attribute__((__format__(__printf__, 2, 3), __noreturn__));

#ifndef NDEBUG
#define debug(...)                 \
    do {                           \
        if (noisy >= DEBUG) {      \
            fs_debug(__VA_ARGS__); \
        }                          \
    } while (0)
#define debug2(...)                \
    do {                           \
        if (noisy >= DEBUG + 1) {  \
            fs_debug(__VA_ARGS__); \
        }                          \
    } while (0)
#else
#define debug(...) (void)0
#define debug2(...) (void)0
#endif

#define verbose(...)                 \
    do {                             \
        if (noisy >= VERBOSE)        \
            fs_verbose(__VA_ARGS__); \
    } while (0)
#define info(...)                 \
    do {                          \
        if (noisy >= NORMAL)      \
            fs_info(__VA_ARGS__); \
    } while (0)
#define warning(...)                 \
    do {                             \
        if (noisy >= QUIET)          \
            fs_warning(__VA_ARGS__); \
    } while (0)
#define error(...) fs_error(__VA_ARGS__)
#define fatal(...) fs_fatal(__VA_ARGS__)
#define fatalx(...) fs_fatalx(__VA_ARGS__)
