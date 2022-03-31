#define _GNU_SOURCE

#include "fork.h"

#include "common.h"
#include "exitcode.h"
#include "noise.h"

#include <fsdyn/integer.h>
#include <fsdyn/list.h>
#include <unixkit/unixkit.h>

#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <sys/wait.h>
#include <unistd.h>

// Within this code, we have three separate processes: the parent, an optional
// intermediate process, and the child.  When daemonizing, the intermediate
// process is necessary to ensure that the child is immediately reparented and
// will not accidentally acquire a controlling tty.
//
// When the caller-provided function is called, there will be four open
// descriptors: STDIN_FILENO, STDOUT_FILENO and STDERR_FILENO will refer to
// /dev/null while REPORT_FILENO refers to a pipe to the intermediate process.
// The function should never write to that file descriptor but should ensure
// that one and exactly one of the following happens:
//
// - The function returns zero to indicate success.
//
// - The function returns non-zero to indicate failure.
//
// - The function never returns, but closes REPORT_FILENO to indicate success.
//
// This third option will automatically happen upon a successful execve() call
// since REPORT_FILENO is close-on-exec.

static void df_fd_setup(fork_pipe *report, fork_io *io)
{
    if (io != NULL && io->in.child != STDIN_FILENO) {
        (void)dup2(io->in.child, STDIN_FILENO);
        (void)close(io->in.child);
    }
    if (io != NULL && io->out.child != STDOUT_FILENO) {
        (void)dup2(io->out.child, STDOUT_FILENO);
        (void)close(io->out.child);
    }
    if (io != NULL && io->err.child != STDERR_FILENO) {
        (void)dup2(io->err.child, STDERR_FILENO);
        (void)close(io->err.child);
    }
    if (report != NULL && report->child != REPORT_FILENO) {
        (void)dup3(report->child, REPORT_FILENO, O_CLOEXEC);
        (void)close(report->child);
        report->child = REPORT_FILENO;
    }
}

static void df_child(child_func func, void *ptr, fork_pipe *report, fork_io *io)
{
    pid_t pid;
    int res;

    df_fd_setup(report, io);
    // First report: just our PID.
    pid = getpid();
    if (write(REPORT_FILENO, &pid, sizeof(pid)) < 0) {
        _exit(EXIT_FAILURE);
    }
    // Call the provided function.
    res = func(ptr);
    if (res != 0) {
        // Second report: something went wrong.
        OK(write(REPORT_FILENO, &res, sizeof(res)));
    }
    if (res >= 0 && res <= 255) {
        _exit(res);
    }
    _exit(EXIT_FAILURE);
}

static void df_inter(child_func func, void *ptr, fork_pipe *report, fork_io *io)
{
    fork_io null_io;
    list_t *keep;
    pid_t pid;

    // If the caller did not provide pipes for stdin / stdout / stderr, point
    // them at /dev/null.
    if (io == NULL) {
        null_io.in.parent = -1;
        if ((null_io.in.child = open(_PATH_DEVNULL, O_RDONLY)) < 0) {
            fatalx(EXIT_STDIN, "failed to set up stdin: %m");
        }
        null_io.out.parent = -1;
        if ((null_io.out.child = open(_PATH_DEVNULL, O_WRONLY | O_APPEND))
            < 0) {
            fatalx(EXIT_STDOUT, "failed to set up stdout: %m");
        }
        null_io.err.parent = -1;
        if ((null_io.err.child = open(_PATH_DEVNULL, O_WRONLY | O_APPEND))
            < 0) {
            fatalx(EXIT_STDERR, "failed to set up stderr: %m");
        }
    }
    // Move to a known safe directory.
    if (chdir("/") < 0) {
        fatalx(EXIT_CHDIR, "failed to switch to root directory: %m");
    }
    // Start a new session.
    if (setsid() < 0) {
        fatalx(EXIT_SETSID, "failed to start new session: %m");
    }
    // Switch stdin / stdout / stderr over.  Assume this cannot fail.
    if (io == NULL) {
        io = &null_io;
        // Log to syslog now that stderr is going away.
        noisef = NULL;
    }
    df_fd_setup(report, io);
    // Fork, closing everything except stdin / stdout / stderr and the
    // reporting pipe.
    keep = make_list();
    list_append(keep, as_integer(STDIN_FILENO));
    list_append(keep, as_integer(STDOUT_FILENO));
    list_append(keep, as_integer(STDERR_FILENO));
    list_append(keep, as_integer(REPORT_FILENO));
    pid = unixkit_fork(keep);
    if (pid < 0) {
        // there is no EXIT_FORK
        fatalx(EXIT_FAILURE, "failed to fork child process: %m");
    }
    if (pid == 0) {
        // Child process.
        // report and io are already set up
        df_child(func, ptr, NULL, NULL);
        abort();
    }
    _exit(EXIT_SUCCESS);
}

static pid_t df_parent(child_func func, void *ptr, fork_io *io, bool daemonize)
{
    fork_pipe report;
    list_t *keep;
    ssize_t res;
    pid_t pid;
    int ex;

    if (!unixkit_pipe(report.pipe)) {
        return -EXIT_FAILURE;
    }
    // Fork, closing everything except stdin / stdout / stderr and the child end
    // of our I/O and reporting pipes.
    keep = make_list();
    list_append(keep, as_integer(STDIN_FILENO));
    list_append(keep, as_integer(STDOUT_FILENO));
    list_append(keep, as_integer(STDERR_FILENO));
    if (io != NULL) {
        list_append(keep, as_integer(io->in.child));
        list_append(keep, as_integer(io->out.child));
        list_append(keep, as_integer(io->err.child));
    }
    list_append(keep, as_integer(report.child));
    pid = unixkit_fork(keep);
    if (pid < 0) {
        error("failed to fork intermediate process: %m");
        close(report.parent);
        close(report.child);
        return -EXIT_FAILURE;
    }
    if (pid == 0) {
        // Child (intermediate) process.
        // Hide the (now-closed) parent ends of the I/O pipes.
        if (io != NULL) {
            io->in.parent = -1;
            io->out.parent = -1;
            io->err.parent = -1;
        }
        if (daemonize) {
            df_inter(func, ptr, &report, io);
        } else {
            df_child(func, ptr, &report, io);
        }
        abort();
    }
    // Parent.
    close(report.child);
    if (daemonize) {
        (void)waitpid(pid, NULL, 0);
    }
    // Wait for first report
    res = read(report.parent, &pid, sizeof(pid));
    if (res != sizeof(pid)) {
        if (res < 0) {
            error("failed to read child pid: %m");
        } else if (res == 0) {
            error("no child pid received");
        }
        close(report.parent);
        return -EXIT_FAILURE;
    }
    // Wait for second report (only on failure)
    res = read(report.parent, &ex, sizeof(ex));
    close(report.parent);
    if (res < 0) {
        error("failed to read child report: %m");
        return -EXIT_FAILURE;
    }
    if (res == 0 || ex == 0) {
        return pid;
    }
    // Try to collect the child, but not too hard.
    for (unsigned int i = 0; i < 10; i++) {
        usleep((1 << i) * 1000);
        if (waitpid(pid, NULL, WNOHANG) != 0) {
            break;
        }
    }
    verbose("child reported exit code %d", ex);
    return ex > 0 ? -ex : ex;
}

// Daemonizes and calls a function.  Returns the daemon's PID if successful,
// a negative value corresponding to a systemd exit code otherwise.
pid_t daemonize_function(child_func func, void *ptr, fork_io *io)
{
    return df_parent(func, ptr, io, true);
}

// Forks and calls a function.  Returns the child's PID if successful, a
// negative value corresponding to a systemd exit code otherwise.  The
// caller is responsible for collecting the child process.
pid_t fork_function(child_func func, void *ptr, fork_io *io)
{
    return df_parent(func, ptr, io, false);
}

// Signal ancestor process that the service is ready by closing the report
// socket.  To avoid the trouble that would ensue if the descriptor was
// reused for some other purpose (e.g. syslog), we close it by replacing it
// with a duplicate of stderr, which at this point should be /dev/null; not
// only is this atomic, idempotent, and guaranteed to succeed (as much as
// anything in POSIX can be) but it also ensures that an unexpected write to
// REPORT_FILENO (e.g. if the child function calls us but then returns to
// df_child() anyway) will succeed.
void report_ready(void)
{
    debug("reporting service ready");
    (void)dup3(STDERR_FILENO, REPORT_FILENO, O_CLOEXEC);
}
