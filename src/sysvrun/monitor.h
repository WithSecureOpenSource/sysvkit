#pragma once

#include <sys/types.h>

struct command;
struct service;

typedef enum {
    MS_ERROR = -1,
    //
    MS_IDLE,
    MS_RESTARTING,
    MS_STARTING,
    MS_RUNNING,
    MS_REMAINING,
    MS_STOPPING,
    MS_STOPPED,
    MS_FAILED,
    MS_DEAD,
    //
    MS_NUM_STATES
} monitor_state;

monitor_state monitor_state_from_name(const char *);
const char *monitor_state_name(monitor_state);

pid_t command_monitor(struct command *);
char *monitor_control(struct service *, const char *);
unsigned int monitor_control_identify(struct service *, pid_t *, int *);
monitor_state monitor_control_get_state(struct service *);
monitor_state monitor_control_wait(struct service *, int, ...);
monitor_state monitor_control_stop(struct service *, int);
monitor_state monitor_control_restart(struct service *, int);
