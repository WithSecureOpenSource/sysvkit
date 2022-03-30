#pragma once

#include <fsdyn/list.h>

#include <stdbool.h>
#include <sys/types.h>

struct process {
    pid_t pid;        // process (thread group) id
    pid_t ppid;       // parent pid
    pid_t sid;        // session id
    list_t *children; // child processes, in order of creation
    int wstatus;      // wait status if exited
};

typedef enum {
    PROCWATCH_EVENT_EXEC,
    PROCWATCH_EVENT_SETSID,
} procwatch_event;

typedef enum {
    PROCWATCH_ACTION_DEFAULT,
    PROCWATCH_ACTION_DROP,
} procwatch_action;

typedef procwatch_action (*procwatch_callback)(procwatch_event,
                                               const struct process *,
                                               void *);

size_t process_count(void);
struct process *process_get(pid_t);
struct process *process_collect(void);
void process_destroy(struct process *);
void process_foreach(void (*)(struct process *, void *), void *);
bool process_remove(pid_t);
bool process_drop(pid_t);

void procwatch_set_callback(procwatch_callback, void *);
bool procwatch_start(void);
void procwatch_stop(void);
bool procwatch_reconnect(void);
bool procwatch_ingest(int);
void procwatch_drain(void);
int procwatch_fd(void);
