#define _GNU_SOURCE

#include "monitor.h"

#include "clock.h"
#include "command.h"
#include "fork.h"
#include "noise.h"
#include "proctitle.h"
#include "procwatch.h"
#include "service.h"
#include "strbool.h"
#include "systemd.h"
#include "sysvrun.h"

#include <fsdyn/charstr.h>
#include <fsdyn/fsalloc.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdarg.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define POLLFD(_fd, _events) \
    (struct pollfd) { .fd = (_fd), .events = (_events) }

#define MONITOR_CONTROL_VERSION 20220303
#define MONITOR_CONTROL_BANNER_FORMAT "{\"version\": \"%u\"}"
#define MONITOR_CONTROL_MAX_SESSION_DURATION (100 * 1000 /* 100 ms */)

#define MONITOR_POLL_INTERVAL ms2us(500)
#define MONITOR_KILL_INTERVAL s2us(3)

struct monitor {
    struct service *svc;
    struct command *cmd;
    usec_t *start_times;
    usec_t start_limit_interval;
    unsigned long start_limit_burst;
    unsigned int start_time_cursor;
    fork_io io;
    pid_t child;
    pid_t pid, sid;
    int wstatus;
    monitor_state state;
    // control socket
    struct sockaddr_un sockaddr;
    socklen_t socklen;
    int sock;
};

static const char *monitor_state_names[MS_NUM_STATES] = {
    [MS_IDLE] = "idle",           [MS_RESTARTING] = "restarting",
    [MS_STARTING] = "starting",   [MS_RUNNING] = "running",
    [MS_REMAINING] = "remaining", [MS_STOPPING] = "stopping",
    [MS_STOPPED] = "stopped",     [MS_FAILED] = "failed",
    [MS_DEAD] = "dead",
};

monitor_state monitor_state_from_name(const char *name)
{
    for (int i = 0; i < MS_NUM_STATES; i++) {
        if (strcmp(monitor_state_names[i], name) == 0) {
            return i;
        }
    }
    return MS_ERROR;
}

const char *monitor_state_name(monitor_state state)
{
    if (state < MS_ERROR || state >= MS_NUM_STATES) {
        return "invalid";
    }
    if (state == MS_ERROR) {
        return "error";
    }
    return monitor_state_names[state];
}

static inline bool monitor_is_waiting(struct monitor *mon)
{
    return mon->state == MS_RESTARTING || mon->state == MS_REMAINING;
}

static inline bool monitor_is_stopping(struct monitor *mon)
{
    return mon->state == MS_RESTARTING || mon->state == MS_STOPPING;
}

socklen_t monitor_socket_addr(struct service *svc, struct sockaddr_un *sun)
{
    int res;

    memset(sun, 0, sizeof(*sun));
    sun->sun_family = AF_UNIX;
    // By prepending a null byte to the socket name, we create an abstract
    // socket that has no representation in the filesystem.  This means we don't
    // have to worry about file ownership or permissions (we rely exclusively on
    // SO_PEERCRED) or about unlinking the socket on exit (it will evaporate on
    // last close).
    res = snprintf(sun->sun_path,
                   sizeof(sun->sun_path),
                   "%c%s/%s%s",
                   '\0',
                   self_base,
                   svc->name,
                   DOT_SERVICE);
    if (res < 0) {
        return 0;
    }
    if ((size_t)res >= sizeof(sun->sun_path)) {
        errno = ENAMETOOLONG;
        return 0;
    }
    return offsetof(struct sockaddr_un, sun_path) + res;
}

static void monitor_set_state(struct monitor *mon, monitor_state state)
{
    const char *argv[3];

    // assert(state >= 0 && state < MS_NUM_STATES);
    if (state != mon->state) {
        verbose("monitor state %s -> %s",
                monitor_state_name(mon->state),
                monitor_state_name(state));
        mon->state = state;
    }
    argv[0] = self_base;
    argv[1] = mon->svc->name;
    argv[2] = monitor_state_name(mon->state);
    set_argv(3, argv);
}

static int monitor_control_listen(struct monitor *mon)
{
    int serrno;

    mon->socklen = monitor_socket_addr(mon->svc, &mon->sockaddr);
    if (mon->socklen == 0) {
        return -1;
    }
    debug("creating control socket %s", mon->sockaddr.sun_path + 1);
    if ((mon->sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0
        || bind(mon->sock, (struct sockaddr *)&mon->sockaddr, mon->socklen) != 0
        || listen(mon->sock, 8) != 0) {
        goto fail;
    }
    return 0;
fail:
    serrno = errno;
    if (mon->sock >= 0) {
        close(mon->sock);
        mon->sock = -1;
    }
    errno = serrno;
    return -1;
}

static void monitor_control_close(struct monitor *mon)
{
    close(mon->sock);
    mon->sock = -1;
}

static int monitor_control_socket_ingest(struct monitor *mon)
{
    char buf[4096];
    struct ucred ccred;
    struct sockaddr_un sun;
    struct pollfd pfds[1];
    const char *str;
    socklen_t len;
    ssize_t res;
    usec_t deadline, now;
    int csock;
    bool privileged;

    debug("control socket %d ready", mon->sock);
    len = sizeof(sun);
    if ((csock = accept(mon->sock, (struct sockaddr *)&sun, &len)) < 0) {
        error("failed to accept control client connection: %m");
        return -1;
    }
    debug("control(%d): accepted", csock);
    len = sizeof(ccred);
    if (getsockopt(csock, SOL_SOCKET, SO_PEERCRED, &ccred, &len) != 0) {
        error("control(%d): failed to get credentials: %m", csock);
        close(csock);
        return -1;
    }
    debug("control(%d) pid %u uid %u gid %u",
          csock,
          (unsigned int)ccred.pid,
          (unsigned int)ccred.uid,
          (unsigned int)ccred.gid);
    if (ccred.uid == 0 || ccred.uid == mon->cmd->uid) {
        debug("control client is privileged");
        privileged = true;
    } else {
        privileged = false;
    }
    res = snprintf(buf,
                   sizeof(buf) - 2,
                   MONITOR_CONTROL_BANNER_FORMAT,
                   MONITOR_CONTROL_VERSION);
    debug("control(%d): >\"%s\"", csock, buf);
    buf[res++] = '\r';
    buf[res++] = '\n';
    buf[res] = '\0';
    if (write(csock, buf, res) < 0) {
        goto csockerr;
    }
    pfds[0] = POLLFD(csock, POLLIN);
    now = clock_usec();
    deadline = now + MONITOR_CONTROL_MAX_SESSION_DURATION;
    while (now < deadline) {
        debug("control(%d): %llu us until deadline", csock, deadline - now);
        res = poll(pfds, sizeof(pfds) / sizeof(pfds[0]), us2ms(deadline - now));
        if (res < 0) {
            goto csockerr;
        }
        if (res == 0) {
            break;
        }
        res = read(csock, buf, sizeof(buf));
        if (res < 0) {
            goto csockerr;
        }
        if (res == 0) {
            // closed
            break;
        }
        while (res > 0 && isspace((unsigned char)buf[res - 1])) {
            buf[--res] = '\0';
        }
        debug("control(%d): <\"%s\"", csock, buf);
        str = "denied";
        if (strcmp(buf, "status") == 0) {
            verbose("control(%d): status requested", csock);
            if (mon->state < MS_NUM_STATES) {
                str = monitor_state_name(mon->state);
            } else {
                str = "unknown";
            }
        } else if (strcmp(buf, "stop") == 0) {
            if (privileged) {
                verbose("control(%d): stop requested", csock);
                if (mon->state < MS_STOPPING) {
                    monitor_set_state(mon, MS_STOPPING);
                }
                str = "ok";
            }
        } else if (strcmp(buf, "restart") == 0) {
            if (privileged) {
                verbose("control(%d): restart requested", csock);
                monitor_set_state(mon, MS_RESTARTING);
                str = "ok";
            }
        } else if (strcmp(buf, "noise=debug") == 0) {
            if (privileged) {
                noisy = DEBUG;
                str = "ok";
            }
        } else if (strcmp(buf, "noise=verbose") == 0) {
            if (privileged) {
                noisy = VERBOSE;
                str = "ok";
            }
        } else if (strcmp(buf, "noise=normal") == 0) {
            if (privileged) {
                noisy = NORMAL;
                str = "ok";
            }
        } else {
            str = "error";
        }
        res = snprintf(buf, sizeof(buf) - 2, "%s", str);
        debug("control(%d): >\"%s\"", csock, buf);
        buf[res++] = '\r';
        buf[res++] = '\n';
        buf[res] = '\0';
        res = write(csock, buf, res);
        if (res < 0) {
            goto csockerr;
        }
        now = clock_usec();
    }
    debug("control(%d): closing", csock);
    close(csock);
    return 0;
csockerr:
    error("control(%d): error: %m", csock);
    close(csock);
    return -1;
}

// Redirect logs to the specified file, or syslog if we fail to open
// it.  If the path is a directory, create or append to
// sysvrun.<service>.log in that directory.
void monitor_log_to_file(struct monitor *mon, const char *path)
{
    struct stat sb;
    char *dynpath = NULL;
    FILE *f;

    if (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        dynpath = charstr_printf("%s/sysvrun.%s.log", path, mon->svc->name);
        path = dynpath;
    }
    f = fopen(path, "a");
    if (f == NULL) {
        error("unable to log to %s: %m", path);
    } else {
        info("logging to %s", path);
    }
    fsfree(dynpath);
    noisef = f;
}

// Set up logging
void monitor_log_setup(struct monitor *mon)
{
    const char *log_to_file;

    if (foreground) {
        return;
    }
    openlog(mon->svc->name, LOG_PID, LOG_DAEMON);
    if ((log_to_file = getenv("SYSVKIT_LOG_TO_FILE")) != NULL) {
        if (*log_to_file == '/') {
            monitor_log_to_file(mon, log_to_file);
        } else if (strbool(log_to_file) > 0) {
            monitor_log_to_file(mon, "/var/log");
        }
    } else {
        noisef = NULL;
    }
}

// Read from a file or socket descriptor and write to log.
// XXX if the source uses multiple write operations to write a single line, it
// is possible that we will only catch part of it on each call, thus splitting
// the output over multiple log lines.  This can be fixed with buffering,
// perhaps by moving this code into common/fork and keeping the state in
// fork_io.  It is not safe to loop the read() (or to call fd_to_log() in a
// loop) as we might end up blocking other operations if the source is producing
// output at a very high rate.
static int fd_to_log(int priority, int fd)
{
    char iobuf[4096];
    char *e, *p, *q;
    usec_t now;
    ssize_t res;
    int len = 0;

    res = read(fd, iobuf, sizeof(iobuf));
    if (res < 0) {
        if (errno == EAGAIN) {
            return 0;
        }
        return -1;
    }
    e = iobuf + res;
    for (p = iobuf; p < e; p = q + 1) {
        // find end of line
        for (q = p; q < e && *q != '\0' && *q != '\n'; q++) {
            // suppress non-printable characters
            if (*q < ' ') {
                *q = ' ';
            }
        }
        if (q > p) {
            if (noisef == NULL) {
                syslog(priority, "%.*s", (int)(q - p), p);
            } else {
                now = clock_realtime_usec();
                fprintf(noisef,
                        "%llu.%06llu [%u] %.*s\n",
                        now / 1000000,
                        now % 1000000,
                        (unsigned int)getpid(),
                        (int)(q - p),
                        p);
            }
            len += q - p;
        }
    }
    if (noisef != NULL && len > 0) {
        fflush(noisef);
    }
    return len;
}

struct kill_order {
    struct monitor *mon;
    int signal;
    bool all;
    unsigned long sent;
    const char *signame;
};

static void monitor_kill(struct process *proc, void *ptr)
{
    struct kill_order *ko = ptr;

    if (proc->pid != getpid() && proc->pid != 1
        && (ko->all || proc->pid == ko->mon->pid)) {
        debug("ko: sending %s to %u", ko->signame, (unsigned int)proc->pid);
        kill(proc->pid, ko->signal);
        kill(proc->pid, SIGCONT);
    } else {
        debug("ko: skipping %u", proc->pid);
    }
}

static void report_proc_execve(pid_t pid)
{
    char linkbuf[1024];
    struct text *comm;
    char *path;
    ssize_t res;

    if (noisy < DEBUG) {
        return;
    }
    path = charstr_printf("/proc/%u/exe", (unsigned int)pid);
    res = readlink(path, linkbuf, sizeof(linkbuf));
    fsfree(path);
    if (res > 0) {
        debug("PID %u executed %.*s", (unsigned int)pid, (int)res, linkbuf);
        return;
    }
    path = charstr_printf("/proc/%u/comm", (unsigned int)pid);
    comm = text_from_file(path);
    fsfree(path);
    if (comm != NULL) {
        debug("PID %u executed %.*s",
              (unsigned int)pid,
              (int)(comm->end - comm->beg),
              comm->beg);
        text_free(comm);
        return;
    }
    debug("PID %u executed unknown command", (unsigned int)pid);
}

static procwatch_action monitor_proc_event(procwatch_event event,
                                           const struct process *proc,
                                           void *data)
{
    struct monitor *mon = data;
    struct service *svc = mon->svc;

    if (event == PROCWATCH_EVENT_SETSID) {
        // assert(proc->sid != mon->sid);
        debug("descendant %u changed sid from %u to %u",
              proc->pid,
              mon->sid,
              proc->sid);
        if (svc->type != ST_FORKING) {
            // A non-forking service should never setsid, so this is a
            // descendant service which we should not track.
            debug("non-forking service changed sid: dropping %u", proc->pid);
            return PROCWATCH_ACTION_DROP;
        } else if (mon->sid != getsid(0)) {
            // A forking service is expected to setsid exactly once, when
            // initially daemonizing.  Any subsequent setsid indicates a
            // descendant service which we should not track.
            debug("forking service changed sid again: dropping %u", proc->pid);
            return PROCWATCH_ACTION_DROP;
        } else {
            verbose("setting service sid to %u", proc->sid);
            mon->sid = proc->sid;
        }
    } else if (event == PROCWATCH_EVENT_EXEC) {
        report_proc_execve(proc->pid);
    }
    return PROCWATCH_ACTION_DEFAULT;
}

static bool monitor_find_main_pid(struct monitor *mon)
{
    struct command *cmd = mon->cmd;

    if (cmd->pidfile != NULL) {
        mon->pid = command_getpid(cmd);
        if (mon->pid > 0 && process_get(mon->pid) == NULL) {
            warning("main service process %u not found",
                    (unsigned int)mon->pid);
            mon->pid = 0;
        }
    } else {
        // XXX implement GuessMainPID?
        // No warning here as it could flood the logs.  We already
        // issued a warning when we started the service.
    }
    if (mon->pid > 0) {
        verbose("main process identified as %u", (unsigned int)mon->pid);
        return true;
    }
    return false;
}

// Inner loop of service monitor.  Monitor the service and its descendants until
// they have all terminated.  Returns the PID of the final descendant, or a
// negative value on error.
// XXX need to review the return value
static int monitor_watch(struct monitor *mon)
{
    struct pollfd pfds[4] = {};
    struct kill_order ko = { .mon = mon };
    struct service *svc = mon->svc;
    struct command *cmd = mon->cmd;
    struct process *proc;
    usec_t now;
    pid_t pid;
    int res, ret, wstatus;
    int stopping;

    pid = 0;
    pfds[0] = POLLFD(procwatch_fd(), POLLIN);
    pfds[1] = POLLFD(mon->io.out.parent, POLLIN);
    pfds[2] = POLLFD(mon->io.err.parent, POLLIN);
    pfds[3] = POLLFD(mon->sock, POLLIN);
    procwatch_set_callback(monitor_proc_event, mon);
    stopping = 0;
    ret = 0;
    for (;;) {
        res = poll(pfds, sizeof(pfds) / sizeof(pfds[0]), -1);
        if (res < 0 && errno != EINTR) {
            error("unrecoverable poll error: %m");
            ret = -1;
            break;
        }
        now = clock_usec();
        // control socket connection
        if (pfds[3].revents) {
            if (monitor_control_socket_ingest(mon) < 0) {
                error("unrecoverable control socket error: %m");
                ret = -1;
                break;
            }
        }
        // Did we get a stop or restart order?
        if (monitor_is_stopping(mon) && now - ko.sent > svc->stop_timeout) {
            if (mon->pid <= 0) {
                // Forking services only: we still don't have a main process.
                // We can get here if we receive a stop order very shortly after
                // starting (or restarting) the service.  Therefore, we will
                // give it one chance (one TimeoutStopSec interval) to make
                // itself known before we give up.
                warning("stop order received with no main process");
                if (ko.sent > 0) {
                    process_drop(mon->child);
                    break;
                }
                ko.sent = now;
            } else if (svc->kill_mode == KM_NONE) {
                process_drop(mon->pid >= 0 ? mon->pid : mon->child);
                break;
            } else {
                // If KillMode is `control-group`, kill all processes on the
                // first pass.
                // If KillMode is `mixed`, kill only the main process on the
                // first pass, then any remaining processes on the second.
                // If it is `process` (the only remaining option since we
                // handled `none` above), only the main process will be killed.
                stopping++;
                if (stopping == 1) {
                    // First pass
                    if (svc->kill_mode == KM_CGROUP) {
                        ko.all = true;
                    }
                    ko.signal = SIGTERM;
                    ko.signame = "SIGTERM";
                } else if (stopping == 2) {
                    // Second pass
                    if (svc->kill_mode == KM_MIXED) {
                        ko.all = true;
                    }
                    ko.signal = SIGKILL;
                    ko.signame = "SIGKILL";
                } else {
                    // Still running after second pass, give up
                    error("%zu processes still running, giving up",
                          process_count());
                    break;
                }
                verbose("sending %s to %s",
                        ko.signame,
                        ko.all ? "all processes" : "main process");
                ko.sent = now;
                process_foreach(monitor_kill, &ko);
            }
        }
        // data on stderr
        if (pfds[2].revents) {
            if (fd_to_log(LOG_ERR, mon->io.err.parent) < 0) {
                error("error reading from service stderr: %m");
                dup2(STDIN_FILENO, mon->io.err.parent);
            }
        }
        // data on stdout
        if (pfds[1].revents) {
            if (fd_to_log(LOG_NOTICE, mon->io.out.parent) < 0) {
                error("error reading from service stdout: %m");
                dup2(STDIN_FILENO, mon->io.out.parent);
            }
        }
        // process event
        if (!pfds[0].revents) {
            continue;
        }
        // Ingest all outstanding events.
        while (procwatch_ingest(0)) {
            // nothing
        }
        if (errno != ETIMEDOUT) {
            error("unrecoverable process event connector error: %m");
            if (!procwatch_reconnect()) {
                ret = -1;
                break;
            }
        }
        // Look for main PID if we don't have it yet.  To reduce log spam, only
        // check after the service child has terminated.
        if (mon->pid <= 0 && mon->child == 0) {
            monitor_find_main_pid(mon);
        }
        // Collect terminated processes.
        while ((proc = process_collect()) != NULL) {
            if (WIFEXITED(proc->wstatus)) {
                debug("process %u (ppid %u) exited with status %d",
                      proc->pid,
                      proc->ppid,
                      WEXITSTATUS(proc->wstatus));
            } else if (WIFSIGNALED(proc->wstatus)) {
                debug("process %u (ppid %u) terminated by signal %d",
                      proc->pid,
                      proc->ppid,
                      WTERMSIG(proc->wstatus));
            } else {
                debug("process %u (ppid %u) terminated!?",
                      proc->pid,
                      proc->ppid);
            }
            pid = proc->pid;
            wstatus = proc->wstatus;
            process_destroy(proc);
            if (pid == mon->child) {
                // Direct child, collect it.
                verbose("service child %u terminated", (unsigned int)pid);
                (void)waitpid(pid, NULL, 0);
                // Report readiness for Type=forking.
                if (svc->type == ST_FORKING) {
                    // XXX should we report a negative result if the exit status
                    // is non-zero?
                    monitor_set_state(mon, MS_RUNNING);
                    report_ready();
                }
                mon->child = 0;
            }
            if (pid == mon->pid) {
                // Main process exited
                mon->wstatus = wstatus;
                if (cmd->pidfile != NULL) {
                    command_rmpid(cmd);
                }
            }
        }
        // Once the main process of a one-shot service has terminated, the
        // service is ready and we return to the main loop, which will
        // transition to MS_REMAINING.
        // XXX should we report a negative result if the exit status is
        // non-zero?
        if (svc->type == ST_ONESHOT && mon->wstatus >= 0) {
            monitor_set_state(mon, MS_RUNNING);
            report_ready();
            break;
        }
        if (mon->wstatus >= 0) {
            // The main process has terminated or been killed.
            verbose("main process %u terminated", (unsigned int)mon->pid);
            if (!monitor_is_stopping(mon) || svc->kill_mode == KM_PROCESS) {
                // If we are not in a stopping state, the main process
                // self-terminated.  If we are in a stopping state and KillMode
                // is `process`, we have successfully stopped the service.  In
                // either case, we are done.
                break;
            }
        }
        // If there are events queued up before the one signaling the creation
        // of our child, we will reach this point prematurely, so make sure that
        // we have collected at least one process (pid != 0) before we return.
        if (errno == ECHILD && pid != 0) {
            // All descendants have terminated.
            debug("no descendants left");
            break;
        }
    }
    debug("monitor watch loop terminated in state %s",
          monitor_state_name(mon->state));
    procwatch_set_callback(NULL, NULL);
    procwatch_drain();
    return ret;
}

// Do nothing except serve control connections until the given deadline is
// reached or the state changes.
// Note that while we don't have descendants at this point, we still need to
// ingest procwatch events, otherwise they will stack up and the socket will
// close.
static int monitor_wait(struct monitor *mon, usec_t deadline)
{
    struct pollfd pfds[2];
    struct process *proc;
    usec_t t;
    int res, timeout;
    monitor_state state;

    pfds[0] = POLLFD(procwatch_fd(), POLLIN);
    pfds[1] = POLLFD(mon->sock, POLLIN);
    state = mon->state;
    if (deadline == 0) {
        debug("waiting forever");
    } else {
        debug("waiting until %llu.%03llu",
              deadline / 1000000,
              (deadline / 1000) % 1000);
    }
    procwatch_set_callback(NULL, NULL);
    while (mon->state == state) {
        t = clock_usec();
        if (deadline == 0) {
            timeout = -1;
        } else if (t < deadline) {
            timeout = us2ms(deadline - t);
        } else {
            debug("wait over: timer expired");
            break;
        }
        res = poll(pfds, sizeof(pfds) / sizeof(pfds[0]), timeout);
        if (res < 0 && errno != EINTR) {
            error("unrecoverable poll error: %m");
            return -1;
        }
        if (pfds[1].revents) {
            if (monitor_control_socket_ingest(mon) < 0) {
                error("unrecoverable control socket error: %m");
                return -1;
            }
        }
        if (pfds[0].revents) {
            // Ingest all outstanding events.
            while (procwatch_ingest(0)) {
                // nothing
            }
            if (errno != ETIMEDOUT) {
                error("unrecoverable process event connector error: %m");
                if (!procwatch_reconnect()) {
                    return -1;
                }
            }
            while ((proc = process_collect()) != NULL) {
                // This shouldn't happen, in theory...
                fsfree(proc);
            }
        }
    }
    if (mon->state != state) {
        debug("wait over: state changed from %s to %s",
              monitor_state_name(state),
              monitor_state_name(mon->state));
    }
    return 0;
}

#define MAX_START_LIMIT_BURST 100

// Outer loop of the service monitor.  Run and monitor a command, restarting it
// as needed.
static int monitor_func(void *ptr)
{
    struct monitor mon = {};
    usec_t next_start_time, start_time_delta;
    int pid = 0;
    bool ucexit, ucsig;

    mon.cmd = ptr;
    mon.svc = mon.cmd->svc;
    monitor_set_state(&mon, MS_IDLE);
    // Point stdin at /dev/null and set up pipes for stdout and stderr. Note
    // that we do not use pipe2() because we only want O_NONBLOCK on the
    // parent end of each pipe, while pipe2() would set it on both.
    mon.io.in.parent = -1;
    if ((mon.io.in.child = open(_PATH_DEVNULL, O_RDONLY)) < 0
        || pipe(mon.io.out.pipe) != 0
        || fcntl(mon.io.out.parent, F_SETFL, O_NONBLOCK) != 0
        || pipe(mon.io.err.pipe) != 0
        || fcntl(mon.io.err.parent, F_SETFL, O_NONBLOCK) != 0) {
        error("failed to set up I/O pipes");
        return EXIT_FAILURE;
    }
    monitor_log_setup(&mon);
    if (monitor_control_listen(&mon) != 0) {
        error("failed to open control socket: %m");
        return EXIT_FAILURE;
    }
    if (!procwatch_start()) {
        error("failed to start process event monitor");
        return EXIT_FAILURE;
    }
    next_start_time = clock_usec();
    mon.start_limit_interval = mon.svc->start_limit_interval;
    mon.start_limit_burst = mon.svc->start_limit_burst;
    if (mon.start_limit_interval > 0 && mon.start_limit_burst > 1) {
        if (mon.start_limit_burst > MAX_START_LIMIT_BURST) {
            mon.start_limit_burst = MAX_START_LIMIT_BURST;
            warning("capping StartLimitBurst at %lu", mon.start_limit_burst);
        }
        mon.start_times =
            fscalloc(mon.start_limit_burst, sizeof(*mon.start_times));
        mon.start_time_cursor = 0;
        mon.start_times[mon.start_time_cursor] = clock_usec();
        mon.start_time_cursor =
            (mon.start_time_cursor + 1) % mon.start_limit_burst;
    }
    debug("monitor started");
    monitor_set_state(&mon, MS_STARTING);
    while (mon.state < MS_STOPPED) {
        switch (mon.state) {
            case MS_RESTARTING:
                // This is the approximate time we will restart.
                next_start_time = clock_usec() + mon.svc->delay;
                // If applicable, check if restarting after the mandated delay
                // would bust the start limit.
                if (mon.start_times != NULL) {
                    // The value under the cursor is the time we started
                    // start_limit_burst starts ago, or zero if we haven't
                    // gotten that far yet.  If it is less than
                    // start_limit_interval ago then we're cycling too fast and
                    // shouldn't restart.
                    start_time_delta = next_start_time
                        - mon.start_times[mon.start_time_cursor];
                    if (start_time_delta < mon.start_limit_interval) {
                        error("start limit exceeded (%lu in %llu.%06llu s)",
                              mon.start_limit_burst,
                              start_time_delta / 1000000,
                              start_time_delta % 1000000);
                        monitor_set_state(&mon, MS_FAILED);
                        break;
                    }
                    mon.start_times[mon.start_time_cursor] = next_start_time;
                    mon.start_time_cursor =
                        (mon.start_time_cursor + 1) % mon.start_limit_burst;
                }
                verbose("restarting (policy: %s) after %llu.%06llu s delay",
                        restart_policy_names[mon.svc->restart_policy],
                        mon.svc->delay / 1000000,
                        mon.svc->delay % 1000000);
                if (monitor_wait(&mon, next_start_time) < 0) {
                    // XXX wrong?
                    monitor_set_state(&mon, MS_DEAD);
                }
                if (mon.state != MS_RESTARTING) {
                    break;
                }
                /* fall through */
            case MS_STARTING:
                command_verbose(mon.cmd);
                mon.wstatus = -1;
                mon.child = fork_function(command_exec_func, mon.cmd, &mon.io);
                mon.sid = getsid(0); // Will be updated later
                if (mon.child < 0) {
                    error("failed to start service: %m");
                    monitor_set_state(&mon, MS_DEAD);
                    break;
                }
                verbose("started service child %u", (unsigned int)mon.child);
                // Report readiness for Type=simple and Type=exec.  The
                // fork_function() call above does not return until the child
                // process has either called execve() or terminated, which is
                // late for Type=simple, but all that matters is that we're not
                // early.
                if (mon.svc->type == ST_SIMPLE || mon.svc->type == ST_EXEC) {
                    monitor_set_state(&mon, MS_RUNNING);
                    report_ready();
                }
                // For anything other than ST_FORKING, the child is also the
                // main process.
                if (mon.svc->type != ST_FORKING) {
                    mon.pid = mon.child;
                } else if (mon.cmd->pidfile == NULL) {
                    // We don't implement GuessMainPID, so this is bad,
                    // especially if KillMode is `process` or `mixed`.
                    warning("forking service without PID file");
                }
                /* fall through */
            case MS_RUNNING:
                if (monitor_watch(&mon) < 0) {
                    // XXX wrong?
                    monitor_set_state(&mon, MS_DEAD);
                    break;
                }
                ucexit = ucsig = false;
                debug("pid %u status 0x%04x",
                      (unsigned int)mon.pid,
                      (unsigned int)mon.wstatus);
                if (WIFEXITED(mon.wstatus)) {
                    verbose("%s exited with status %d",
                            mon.cmd->path,
                            WEXITSTATUS(mon.wstatus));
                    ucexit = WEXITSTATUS(mon.wstatus) != 0;
                } else if (WIFSIGNALED(mon.wstatus)) {
                    verbose("%s terminated by signal %d",
                            mon.cmd->path,
                            WTERMSIG(mon.wstatus));
                    ucsig = WTERMSIG(mon.wstatus) != SIGHUP
                        && WTERMSIG(mon.wstatus) != SIGINT
                        && WTERMSIG(mon.wstatus) != SIGTERM
                        && WTERMSIG(mon.wstatus) != SIGPIPE;
                }
                if (ucexit) {
                    debug("unclean exit");
                } else if (ucsig) {
                    debug("unclean signal");
                } else {
                    debug("clean exit");
                }
                if (mon.state != MS_RUNNING) {
                    // already stopping or restarting
                    break;
                }
                // Remain after successful exit?
                if (mon.svc->remain_after_exit && !ucexit && !ucsig) {
                    verbose("start command successful, remain after exit");
                    monitor_set_state(&mon, MS_REMAINING);
                    break;
                }
                // Decide whether to restart.
                if (mon.svc->restart_policy == RP_ALWAYS
                    || (mon.svc->restart_policy == RP_ON_SUCCESS && !ucexit
                        && !ucsig)
                    || (mon.svc->restart_policy == RP_ON_FAILURE
                        && (ucexit || ucsig))
                    || (mon.svc->restart_policy == RP_ON_ABNORMAL && ucsig)
                    || (mon.svc->restart_policy == RP_ON_ABORT && ucsig)) {
                    monitor_set_state(&mon, MS_RESTARTING);
                    break;
                }
                verbose("restarting (policy: %s) not indicated",
                        restart_policy_names[mon.svc->restart_policy]);
                if (ucexit || ucsig) {
                    monitor_set_state(&mon, MS_FAILED);
                } else {
                    monitor_set_state(&mon, MS_STOPPED);
                }
                break;
            case MS_STOPPING:
                // If we're here, we're already stopped.
                monitor_set_state(&mon, MS_STOPPED);
                break;
            case MS_REMAINING:
                // Continue to serve control requests until  we get a stop
                // or restart command.
                if (monitor_wait(&mon, 0) < 0) {
                    // XXX wrong?
                    monitor_set_state(&mon, MS_DEAD);
                }
                break;
            default:
                error("invalid monitor state %d", mon.state);
                monitor_set_state(&mon, MS_DEAD);
        }
    }
    procwatch_stop();
    monitor_control_close(&mon);
    if (mon.start_times != NULL) {
        fsfree(mon.start_times);
    }
    debug("monitor stopped");
    if (pid < 0) {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

// Daemonizes and executes a command, monitoring it and restarting it as
// needed.
pid_t command_monitor(struct command *cmd)
{
    pid_t pid;

    if (foreground) {
        pid = fork_function(monitor_func, cmd, NULL);
        waitpid(pid, &cmd->wstatus, 0);
        return -WEXITSTATUS(cmd->wstatus); // XXX what if signal?
    }
    // XXX cmd->wstatus will not be set
    return daemonize_function(monitor_func, cmd, NULL);
}

struct monitor_client {
    struct sockaddr_un addr;
    socklen_t addrlen;
    int sock;
    struct ucred cred;
    unsigned int version;
};

static struct monitor_client *monitor_client_connect(struct service *svc)
{
    char buf[4096];
    struct monitor_client *mc;
    socklen_t len;
    ssize_t res;

    mc = fscalloc(1, sizeof(*mc));
    mc->sock = -1;
    debug("opening control socket");
    mc->addrlen = monitor_socket_addr(svc, &mc->addr);
    if ((mc->sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        goto fail;
    }
    debug("connecting to monitor");
    if (connect(mc->sock, (struct sockaddr *)&mc->addr, mc->addrlen) != 0) {
        goto fail;
    }
    debug("control socket connected");
    len = sizeof(mc->cred);
    if (getsockopt(mc->sock, SOL_SOCKET, SO_PEERCRED, &mc->cred, &len) != 0) {
        goto fail;
    }
    debug("monitor pid %u uid %u gid %u",
          (unsigned int)mc->cred.pid,
          (unsigned int)mc->cred.uid,
          (unsigned int)mc->cred.gid);
    if ((res = read(mc->sock, buf, sizeof(buf))) < 0) {
        goto fail;
    }
    while (res > 0 && isspace((unsigned char)buf[res - 1])) {
        buf[--res] = '\0';
    }
    debug("banner received: %s", buf);
    if (sscanf(buf, MONITOR_CONTROL_BANNER_FORMAT, &mc->version) != 1) {
        errno = EPROTO;
        goto fail;
    }
    debug("monitor version: %d", mc->version);
    return mc;
fail:
    if (errno == ECONNRESET) {
        // This happens if we connect just as the monitor is shutting down.
        // Translate it to ECONNREFUSED which will be interpreted by the
        // caller as “monitor not running”.
        errno = ECONNREFUSED;
    }
    if (mc->sock >= 0) {
        close(mc->sock);
    }
    fsfree(mc);
    return NULL;
}

static void monitor_client_close(struct monitor_client *mc)
{
    int serrno = errno;
    debug("closing control socket");
    close(mc->sock);
    fsfree(mc);
    errno = serrno;
}

// Connects to a running monitor and returns its PID and version.
unsigned int monitor_control_identify(struct service *svc,
                                      pid_t *pidp,
                                      int *versionp)
{
    struct monitor_client *mc;

    if ((mc = monitor_client_connect(svc)) == NULL) {
        return -1;
    }
    if (pidp != NULL) {
        *pidp = mc->cred.pid;
    }
    if (versionp != NULL) {
        *versionp = mc->version;
    }
    monitor_client_close(mc);
    return 0;
}

// Sends a single command to a running monitor and returns the response.
char *monitor_control(struct service *svc, const char *command)
{
    char buf[4096];
    struct monitor_client *mc;
    ssize_t res;

    if ((mc = monitor_client_connect(svc)) == NULL) {
        return NULL;
    }
    if (mc->version > MONITOR_CONTROL_VERSION) {
        error("control protocol version mismatch: %u > %u",
              mc->version,
              MONITOR_CONTROL_VERSION);
        monitor_client_close(mc);
        errno = EPROTO;
        return NULL;
    }
    res = snprintf(buf, sizeof(buf) - 2, "%s", command);
    if ((size_t)res >= sizeof(buf) - 2) {
        debug("requested command is too long");
        monitor_client_close(mc);
        errno = EINVAL;
        return NULL;
    }
    debug("control >%s", buf);
    buf[res++] = '\r';
    buf[res++] = '\n';
    buf[res] = '\0';
    res = write(mc->sock, buf, res);
    if (res < 0) {
        goto fail;
    }
    res = read(mc->sock, buf, sizeof(buf));
    if (res < 0) {
        goto fail;
    }
    while (res > 0 && isspace((unsigned char)buf[res - 1])) {
        buf[--res] = '\0';
    }
    debug("control <%s", buf);
    monitor_client_close(mc);
    return charstr_dupstr(buf);
fail:
    if (errno != ENOENT && errno != ECONNREFUSED) {
        error("control socket error: %m");
    } else {
        debug("control socket error: %m");
    }
    monitor_client_close(mc);
    return NULL;
}

// Interrogates a running monitor and returns the current state of the service.
// Returns MS_STOPPED if the monitor is not running.  Returns MS_ERROR and sets
// errno if the monitor is running but we were unable to communicate with it.
monitor_state monitor_control_get_state(struct service *svc)
{
    char *response;
    monitor_state state;

    response = monitor_control(svc, "status");
    if (response == NULL) {
        if (errno == ENOENT || errno == ECONNREFUSED) {
            // ENOENT = concrete socket does not exist
            // ECONNREFUSED = abstract socket does not exist or concrete socket
            // exists but is not bound
            return MS_STOPPED;
        }
        return MS_ERROR;
    }
    if (strcmp(response, "denied") == 0) {
        state = MS_ERROR;
        errno = EPERM;
    } else {
        state = monitor_state_from_name(response);
        if (state == MS_ERROR) {
            errno = EINVAL;
        }
    }
    fsfree(response);
    return state;
}

// Waits for a running monitor to reach one of the states in the zero-terminated
// list.  The timeout is in milliseconds and will be rounded up to the nearest
// multiple of MONITOR_POLL_INTERVAL; a negative timeout means infinity. Returns
// the expected state if it is reached before the timeout expires.  Returns
// MS_ERROR and sets errno to ETIMEDOUT if it does not.  Returns MS_ERROR and
// sets errno to an appropriate value if an error occurs.
monitor_state monitor_control_wait(struct service *svc, int timeout, ...)
{
    va_list ap;
    unsigned long deadline, now;
    monitor_state state;
    unsigned int mask = 0;

    va_start(ap, timeout);
    do {
        state = va_arg(ap, monitor_state);
        if (state > MS_IDLE && state < MS_NUM_STATES) {
            mask |= 1U << state;
        }
    } while (state > MS_IDLE);
    va_end(ap);
    state = monitor_control_get_state(svc);
    if (state == MS_ERROR || mask & (1U << state)) {
        return state;
    }
    verbose("waiting for service to change state");
    now = clock_usec();
    if (timeout < 0) {
        deadline = ~0UL;
    } else {
        deadline = now + ms2us(timeout);
    }
    while (now < deadline) {
        usleep(MONITOR_POLL_INTERVAL);
        state = monitor_control_get_state(svc);
        if (state == MS_ERROR || mask & (1U << state)) {
            verbose("service reached state %s", monitor_state_name(state));
            return state;
        }
        now = clock_usec();
    }
    errno = ETIMEDOUT;
    return MS_ERROR;
}

// Sends a stop command to a running monitor, then waits for it to terminate.
// The timeout is in milliseconds and will be rounded up to the nearest multiple
// of MONITOR_POLL_INTERVAL; a negative timeout means infinity.  Returns
// MS_STOPPED if the service stops before the timeout.  Returns MS_ERROR and
// sets errno to ETIMEDOUT if it does not.  Returns MS_ERROR and sets errno to
// an appropriate value if an error occurs.
monitor_state monitor_control_stop(struct service *svc, int timeout)
{
    char *response;
    monitor_state state;

    // Check the state first, in case it's already stopped or stopping.
    state = monitor_control_get_state(svc);
    switch (state) {
        case MS_ERROR:
            break;
        case MS_STOPPED:
            verbose("service is already stopped");
            break;
        case MS_STOPPING:
            verbose("service is already stopping");
            break;
        default:
            verbose("sending stop command");
            response = monitor_control(svc, "stop");
            if (response == NULL) {
                break;
            }
            if (strcmp(response, "ok") == 0) {
                state = MS_STOPPING;
            } else if (strcmp(response, "denied") == 0) {
                state = MS_ERROR;
                errno = EPERM;
            } else {
                state = MS_ERROR;
                errno = EPROTO;
            }
            fsfree(response);
            break;
    }
    if (state != MS_STOPPING) {
        return state;
    }
    // Wait for the service to stop.
    return monitor_control_wait(svc, timeout, MS_STOPPED, 0);
}

// Sends a restart command to a running monitor, then waits for it to come back
// up.  The timeout is in milliseconds and will be rounded up to the nearest
// multiple of MONITOR_POLL_INTERVAL; a negative timeout means infinity.
// Returns MS_RUNNING if the service successfully restarts before the timeout.
// Returns MS_ERROR and sets errno to ETIMEDOUT if it does not.  Returns
// MS_ERROR and sets errno to an appropriate value if an error occurs.
monitor_state monitor_control_restart(struct service *svc, int timeout)
{
    char *response;
    monitor_state state;

    // Check the state first, in case it's already stopped, stopping, or
    // restarting.
    state = monitor_control_get_state(svc);
    switch (state) {
        case MS_ERROR:
            break;
        case MS_STOPPED:
            verbose("service is stopped");
            break;
        case MS_STOPPING:
            verbose("service is stopping");
            break;
        case MS_RESTARTING:
            verbose("service is already restarting");
            break;
        default:
            verbose("sending restart command");
            response = monitor_control(svc, "restart");
            if (response == NULL) {
                break;
            }
            if (strcmp(response, "ok") == 0) {
                state = MS_RESTARTING;
            } else if (strcmp(response, "denied") == 0) {
                state = MS_ERROR;
                errno = EPERM;
            } else {
                state = MS_ERROR;
                errno = EPROTO;
            }
            fsfree(response);
            break;
    }
    if (state != MS_RESTARTING) {
        return state;
    }
    // Wait for the service to finish restarting.
    return monitor_control_wait(svc, timeout, MS_RUNNING, MS_REMAINING, 0);
}
