#pragma once

#include "common.h"
#include "noise.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

extern const char *root;

struct command {
    const char *name;
    int (*main)(const struct command *, int, char **);
};

int getopt_none(const struct command *, int, char **);

extern const struct command cmd_start, cmd_stop, cmd_restart, cmd_try_restart;
extern const struct command cmd_reload, cmd_reload_or_restart,
    cmd_reload_or_try_restart, cmd_try_reload_or_restart;
extern const struct command cmd_enable, cmd_disable;
extern const struct command cmd_status, cmd_is_enabled, cmd_is_active;
extern const struct command cmd_daemon_reload;
extern const struct command cmd_show;

struct service {
    char *name;
    char *path;
    struct stat sb;
};

struct service *service_find(const char *);
void service_free(struct service *);
int service_invoke(struct service *, const char *, bool);
int service_is_enabled_rl(struct service *, int);
int service_is_enabled(struct service *);
int service_disable_rl(struct service *, int);
int service_disable(struct service *);
int service_enable_rl(struct service *, int);
int service_enable(struct service *);
