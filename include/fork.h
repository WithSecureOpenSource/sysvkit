#pragma once

#include <unistd.h>

typedef int (*child_func)(void *);

typedef union {
    int pipe[2];
    struct {
        int parent, child;
    };
} fork_pipe;

typedef struct {
    fork_pipe in, out, err;
} fork_io;

#define REPORT_FILENO 3

pid_t daemonize_function(child_func, void *, fork_io *);
pid_t fork_function(child_func, void *, fork_io *);
void report_ready(void);
