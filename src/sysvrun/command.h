#pragma once

#include <fsdyn/bytearray.h>
#include <fsdyn/list.h>

#include <sys/types.h>

struct service;

struct command {
    struct service *svc;
    char *path;
    char *rootdir, *workdir;
    char *pidfile;
    list_t *args;
    struct environment *env;
    unsigned int flags;
    uid_t uid;
    gid_t gid;
    int umask;
    // after termination
    int wstatus;
};

enum execflag {
    EF_AT = 0x0001,
    EF_DASH = 0x0002,
    EF_COLON = 0x0004,
    EF_PLUS = 0x0008,
    EF_BANG = 0x0010,
    // we do not support !!
};

struct command *command_from_service(struct service *, const char *);
void command_free(struct command *);
int command_exec_func(void *);
pid_t command_fork(struct command *);
pid_t command_daemonize(struct command *);
int command_run(struct command *);
int command_kill(struct command *, int);
int command_killpg(struct command *, int);
byte_array_t *command_to_byte_array(struct command *, byte_array_t *);
char *command_to_string(struct command *);
void command_verbose(struct command *);

pid_t command_getpid(struct command *);
int command_rmpid(struct command *);
