#pragma once

/*
 * Equivalent to <linux/cn_proc.h> but with a more logical struct definition,
 * taking advantage of anonymous structs and unions to shorten names.  Includes
 * all functionality present in Linux 5.8; older kernels may not produce all of
 * these events, but the struct is compatible.
 *
 * Note: this has only been tested on x86_64.  You may encounter alignment or
 * padding issues on other platforms.
 */

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define PROC_CN_MCAST_LISTEN 1
#define PROC_CN_MCAST_IGNORE 2

struct proc_ctl {
    uint32_t op;
};

#define PROC_EVENT_NONE 0x00000000
#define PROC_EVENT_FORK 0x00000001
#define PROC_EVENT_EXEC 0x00000002
#define PROC_EVENT_UID 0x00000004
#define PROC_EVENT_GID 0x00000040
#define PROC_EVENT_SID 0x00000080
#define PROC_EVENT_PTRACE 0x00000100
#define PROC_EVENT_COMM 0x00000200
#define PROC_EVENT_COREDUMP 0x40000000
#define PROC_EVENT_EXIT 0x80000000

typedef struct {
    uint32_t tid;
    uint32_t tgid;
} cn_procid;

typedef struct {
    uint32_t uid;
    uint32_t gid;
} cn_procugid;

struct proc_event {
    uint32_t what;
    uint32_t cpu;
    uint64_t timestamp;
    union {
        struct {
            uint32_t err;
        } ack;
        cn_procid actor;
        struct {
            cn_procid parent;
            cn_procid child;
        } fork;
        struct {
            cn_procid process;
        } exec;
        struct {
            cn_procid process;
            cn_procugid r;
            cn_procugid e;
        } id;
        struct {
            cn_procid process;
        } sid;
        struct {
            cn_procid process;
            cn_procid tracer;
        } ptrace;
        struct {
            cn_procid process;
            char comm[16];
        } comm;
        struct {
            cn_procid process;
            cn_procid parent;
        } coredump;
        struct {
            cn_procid process;
            uint32_t code;   // equivalent to wait() status
            uint32_t signal; // this is not what you think it is
            cn_procid parent;
        } exit;
    };
};

// struct proc_event grows over time as new event types with larger
// corresponding sub-structs are added, so we can't compare the length of the
// received data to sizeof(struct proc_event).  We can however compare it to the
// size of the smallest possible version of proc_event that can exist: event
// type, cpu id, timestamp, tid / tgid of the process it references.
struct _proc_event_min {
    uint32_t what;
    uint32_t cpu;
    uint64_t timestamp;
    union {
        struct {
            uint32_t err;
        } ack;
        cn_procid actor;
    };
};

#define PROC_EVENT_MIN_SIZE sizeof(struct _proc_event_min)

bool cn_proc_connect(void);
void cn_proc_disconnect(void);
ssize_t cn_proc_send(const void *, size_t);
ssize_t cn_proc_receive(void *, size_t, int);
bool cn_proc_receive_event(struct proc_event *, int);
bool cn_proc_listen(bool, int);
int cn_proc_fd(void);
