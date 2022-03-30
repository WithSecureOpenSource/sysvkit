#include "procwatch.h"

#include "cn_proc.h"
#include "common.h"
#include "noise.h"

#include <fsdyn/bytearray.h>
#include <fsdyn/fsalloc.h>
#include <fsdyn/hashtable.h>

#include <errno.h>
#include <signal.h>
#include <unistd.h>

static hash_table_t *processes;
static list_t *ready;

static struct process *proc_init, *proc_self;

// Frees the memory used by a process.
void process_destroy(struct process *proc)
{
    destroy_list(proc->children);
    fsfree(proc);
}

// Returns the number of processes in the process table, not counting self and
// init.
size_t process_count(void)
{
    return hash_table_size(processes) - 2;
}

// Looks up a process in the process table.
struct process *process_get(pid_t pid)
{
    hash_elem_t *e;

    e = hash_table_get(processes, as_unsigned(pid));
    if (e == NULL) {
        errno = ESRCH;
        return NULL;
    }
    return DQ(hash_elem_get_value(e));
}

// Reparent children of a given process to init.
static void process_reparent_children(struct process *parent)
{
    struct process *proc;

    while ((proc = DQ(list_pop_first(parent->children))) != NULL) {
        proc->ppid = 1;
        list_append(proc_init->children, proc);
    }
}

// Detaches a process from its parent.
static void process_unparent(struct process *proc)
{
    struct process *parent;
    list_elem_t *e;

    if ((parent = process_get(proc->ppid)) != NULL) {
        if ((e = list_get(parent->children, proc)) != NULL) {
            list_remove(parent->children, e);
        }
    }
}

// Returns a process that has exited.  The process is removed from the table,
// but the caller is responsible for freeing the struct.  If there are processes
// in the table but none that are ready to be collected, returns NULL and sets
// errno to EAGAIN.  If there are no threads left in the table except ourselves
// and init, returns NULL and sets errno to ECHILD.
struct process *process_collect(void)
{
    struct process *proc;

    // assert(hash_table_size(processes) >= 2);
    if (hash_table_size(processes) == 2) {
        errno = ECHILD;
        return NULL;
    }
    if ((proc = DQ(list_pop_first(ready))) == NULL) {
        errno = EAGAIN;
        return NULL;
    }
    debug("collect pid %u ppid %u status 0x%04x",
          proc->pid,
          proc->ppid,
          proc->wstatus);
    process_unparent(proc);
    hash_table_pop(processes, as_unsigned(proc->pid));
    return proc;
}

// Inserts a process into the process table, or updates it if it is already
// there.
struct process *process_insert(pid_t pid, pid_t ppid, pid_t sid)
{
    struct process *proc, *parent = NULL;

    if ((proc = process_get(pid)) != NULL) {
        // process is already in table, update it
        if (ppid != 0 && ppid != proc->ppid) {
            // process is being reparented; new parent should be init
            if (ppid != 1) {
                error("process %u reparented to non-init process %u",
                      pid,
                      ppid);
                errno = EINVAL;
                return NULL;
            }
            process_unparent(proc);
            list_append(proc_init->children, proc);
            proc->ppid = 1;
        }
        if (sid != 0 && sid != proc->sid) {
            // process changed sid; new sid should be equal to pid
            if (sid != pid) {
                error("process %u moved from sid %u to %u",
                      pid,
                      proc->sid,
                      sid);
                errno = EINVAL;
                return NULL;
            }
            proc->sid = sid;
        }
        return proc;
    }
    // find parent process; init and self will have pid == ppid
    if (ppid != pid) {
        if ((parent = process_get(ppid)) == NULL) {
            warning("parent process %u for %u not found", ppid, pid);
            return NULL;
        }
        // at creation, sid must match parent
        if (sid == 0) {
            sid = parent->sid;
        } else if (sid != parent->sid) {
            error("process %u sid %u does not match parent sid %u",
                  pid,
                  sid,
                  parent->sid);
            errno = EINVAL;
            return NULL;
        }
    }
    proc = fscalloc(1, sizeof(*proc));
    proc->pid = pid;
    proc->ppid = ppid;
    proc->sid = sid;
    proc->children = make_list();
    proc->wstatus = -1;
    if (parent != NULL) {
        list_append(parent->children, proc);
    }
    hash_table_put(processes, as_unsigned(pid), proc);
    debug("process %u (ppid %u) inserted", proc->pid, proc->ppid);
    return proc;
}

// Recursively drops a process and its descendants from the process
// table and destroys them.
static void process_drop_recursive(struct process *proc)
{
    struct process *child;
    hash_elem_t *he;
    list_elem_t *le;

    proc->ppid = 0;
    while ((child = DQ(list_pop_first(proc->children))) != NULL) {
        process_drop_recursive(child);
    }
    if ((he = hash_table_pop(processes, as_unsigned(proc->pid))) != NULL) {
        destroy_hash_element(he);
    }
    if ((le = list_get(ready, proc)) != NULL) {
        debug("dropping ready process %u", (unsigned int)proc->pid);
        list_remove(ready, le);
    } else {
        debug("dropping process %u", (unsigned int)proc->pid);
    }
    process_destroy(proc);
}

// Iterates over all process except init and self and calls the provided
// function for each.
void process_foreach(void (*func)(struct process *, void *), void *ptr)
{
    struct process *proc;
    hash_elem_t *e;

    for (e = hash_table_get_any(processes); e != NULL;
         e = hash_table_get_other(e)) {
        proc = DQ(hash_elem_get_value(e));
        if (proc != proc_init && proc != proc_self) {
            func(proc, ptr);
        }
    }
}

// Removes a process from the process table and frees it.
bool process_remove(pid_t pid)
{
    struct process *proc;
    hash_elem_t *he;
    list_elem_t *le;

    if ((he = hash_table_get(processes, as_unsigned(pid))) == NULL) {
        errno = ESRCH;
        return false;
    }
    proc = DQ(hash_elem_get_value(he));
    if (proc == proc_init) {
        fatal("attempted to remove init from process table");
    }
    if (proc == proc_self) {
        fatal("attempted to remove self from process table");
    }
    hash_table_remove(processes, he);
    if ((le = list_get(ready, proc)) != NULL) {
        list_remove(ready, le);
    }
    process_reparent_children(proc);
    debug("process %u removed", proc->pid);
    process_destroy(proc);
    return true;
}

// Stops tracking a process and all its descendants and removes them
// from the table.  They will not be collected.
bool process_drop(pid_t pid)
{
    struct process *proc;

    if ((proc = process_get(pid)) == NULL) {
        return false;
    }
    process_unparent(proc);
    process_drop_recursive(proc);
    return true;
}

// Validates a thread exit event, and if it was the last thread in the process,
// places the process on the ready list for collection.
static bool process_exit(pid_t pid, int wstatus)
{
    struct process *proc;

    if ((proc = process_get(pid)) == NULL) {
        error("process %u not found", pid);
        return false;
    }
    proc->wstatus = wstatus;
    process_reparent_children(proc);
    list_append(ready, proc);
    return true;
}

// Initializes the process table.
static void processes_init(void)
{
    pid_t pid, sid;

    pid = getpid();
    sid = getsid(0);
    processes =
        make_hash_table(500, (void *)hash_unsigned, (void *)unsigned_cmp);
    ready = make_list();
    proc_init = process_insert(1, 1, 1);
    proc_self = process_insert(pid, pid, sid);
}

// Empties and frees the thread table.
static void processes_fini(void)
{
    struct process *proc;
    hash_elem_t *e;

    while ((e = hash_table_pop_any(processes)) != NULL) {
        proc = DQ(hash_elem_get_value(e));
        destroy_hash_element(e);
        process_destroy(proc);
    }
    destroy_hash_table(processes);
    processes = NULL;
    destroy_list(ready);
    ready = NULL;
    proc_init = NULL;
    proc_self = NULL;
}

// Dumps a list of known processes.
static void process_dump(void)
{
    byte_array_t *ba;
    hash_elem_t *e;
    struct process *proc;

    ba = make_byte_array(SIZE_MAX);
    byte_array_appendf(ba, "processes:");
    e = hash_table_get_any(processes);
    while (e != NULL) {
        proc = DQ(hash_elem_get_value(e));
        byte_array_appendf(ba, " %u(%u)", proc->pid, proc->ppid);
        e = hash_table_get_other(e);
    }
    debug("%s", (const char *)byte_array_data(ba));
    destroy_byte_array(ba);
}

bool procwatch_reconnect(void)
{
    cn_proc_disconnect();
    if (cn_proc_connect()) {
        if (cn_proc_listen(true, 1000)) {
            return true;
        }
        error("failed to enable process events");
    }
    return false;
}

// Starts monitoring process events.
bool procwatch_start(void)
{
    if (processes != NULL) {
        fatal("procwatch_start() called twice");
    }
    processes_init();
    if (!procwatch_reconnect()) {
        processes_fini();
        return false;
    }
    return true;
}

// Stops monitoring process events and releases all resources.
void procwatch_stop(void)
{
    cn_proc_disconnect();
    processes_fini();
}

static procwatch_callback callback_function;
static void *callback_data;

void procwatch_set_callback(procwatch_callback function, void *data)
{
    callback_function = function;
    callback_data = data;
}

static procwatch_action callback(procwatch_event event,
                                 const struct process *proc)
{
    if (callback_function != NULL) {
        return callback_function(event, proc, callback_data);
    }
    return PROCWATCH_ACTION_DEFAULT;
}

// Receives and processes a single process event.  The timeout is in
// milliseconds with the same semantics as for poll(2).
bool procwatch_ingest(int timeout)
{
    struct proc_event ev;
    struct process *proc;

    if (!cn_proc_receive_event(&ev, timeout)) {
        return false;
    }
    if (ev.what == PROC_EVENT_NONE) {
        // This means another process either started or stopped listening.
        // Either way, the ack to their control message will also be broadcast
        // to existing listeners.
        debug2("ack %u", ev.ack.err);
        return true;
    }
    if (process_get(ev.actor.tgid) == NULL) {
        debug2("ignoring event for process %u", ev.actor.tgid);
        return true;
    }
    if (noisy > DEBUG) {
        process_dump();
    }
    switch (ev.what) {
        case PROC_EVENT_FORK:
            if (ev.fork.child.tgid != ev.fork.child.tid) {
                // new thread in existing process
                break;
            }
            if (ev.fork.parent.tgid == 1) {
                debug2("ignoring process %u forked by init",
                       ev.fork.child.tgid);
                break;
            }
            debug2("proc %u fork %u", ev.fork.parent.tgid, ev.fork.child.tgid);
            process_insert(ev.fork.child.tgid,
                           ev.fork.parent.tgid,
                           0 /* sid unknown, will copy from parent */);
            break;
        case PROC_EVENT_EXEC:
            debug2("proc %u exec", ev.exec.process.tgid);
            proc = process_get(ev.exec.process.tgid);
            if (proc != NULL) {
                switch (callback(PROCWATCH_EVENT_EXEC, proc)) {
                    case PROCWATCH_ACTION_DEFAULT:
                        break;
                    case PROCWATCH_ACTION_DROP:
                        process_drop(proc->pid);
                        break;
                    default:
                        /* error? */
                        break;
                }
            }
            break;
        case PROC_EVENT_UID:
            debug2("proc %u euid %u ruid %u",
                   ev.id.process.tgid,
                   ev.id.e.uid,
                   ev.id.r.uid);
            // We don't currently track credentials.
            break;
        case PROC_EVENT_GID:
            debug2("proc %u egid %u rgid %u",
                   ev.id.process.tgid,
                   ev.id.e.gid,
                   ev.id.r.gid);
            // We don't currently track credentials.
            break;
        case PROC_EVENT_SID:
            // undocumented, but safe to assume sid == tgid
            debug2("proc %u sid %u", ev.sid.process.tgid, ev.sid.process.tgid);
            proc = process_insert(ev.sid.process.tgid, 0, ev.sid.process.tgid);
            switch (callback(PROCWATCH_EVENT_SETSID, proc)) {
                case PROCWATCH_ACTION_DEFAULT:
                    break;
                case PROCWATCH_ACTION_DROP:
                    process_drop(proc->pid);
                    break;
                default:
                    /* error? */
                    break;
            }
            break;
        case PROC_EVENT_COMM:
            debug2("proc %u name %s", ev.comm.process.tgid, ev.comm.comm);
            // We don't currently track process names.
            break;
        case PROC_EVENT_COREDUMP:
            debug2("proc %u core dumped", ev.coredump.process.tgid);
            // Purely informational; an exit event will follow.
            break;
        case PROC_EVENT_EXIT:
            if (ev.exit.signal != SIGCHLD) {
                // thread, not process
                break;
            }
            if (WIFSIGNALED(ev.exit.code)) {
                debug2("proc %u signal %u",
                       ev.exit.process.tgid,
                       WTERMSIG(ev.exit.code));
            } else {
                debug2("proc %u exit %u",
                       ev.exit.process.tgid,
                       WEXITSTATUS(ev.exit.code));
            }
            process_exit(ev.exit.process.tgid, ev.exit.code);
            break;
        default:
            debug("unhandled process event 0x%08x", ev.what);
            break;
    }
    return true;
}

void procwatch_drain(void)
{
    processes_fini();
    processes_init();
}

// Returns a file descriptor that can be used to poll for events.  If not
// connected, returns -1 and sets errno to EBADF.
int procwatch_fd(void)
{
    return cn_proc_fd();
}
