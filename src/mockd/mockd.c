#define _GNU_SOURCE

#include "exitcode.h"
#include "noise.h"
#include "timespan.h"

#include <fsdyn/integer.h>
#include <unixkit/unixkit.h>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <paths.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

#define PATH_PIDFILE _PATH_VARRUN "mockd.pid"

#ifndef INFTIM
#define INFTIM -1
#endif

static const char *pidfile;

static void rmpidfile(void)
{
    verbose("deleting PID file");
    if (pidfile != NULL && unlink(pidfile) != 0) {
        error("failed to remove PID file %s: %m", pidfile);
    }
}

static long argnum(const char *act, const char *arg, long min, long max)
{
    char *end;
    long num;

    errno = 0;
    if (arg == NULL || *arg == '\0') {
        errno = EINVAL;
        return 0;
    }
    num = strtol(arg, &end, 10);
    if (*end != '\0') {
        error("%s: invalid argument", act);
        errno = EINVAL;
        return 0;
    }
    if (errno == ERANGE || num < min || num > max) {
        error("%s: argument out of range", act);
        errno = ERANGE;
        return 0;
    }
    return num;
}

static int mockd_exit(const char *act, const char *arg)
{
    unsigned long status = EXIT_SUCCESS;

    if (arg != NULL) {
        status = argnum(act, arg, 0, 255);
        if (errno != 0) {
            return -1;
        }
    }
    verbose("exiting with status %ld", status);
    exit(status);
    return -1; // not reached
}

static int mockd_sleep(const char *act, const char *arg)
{
    unsigned long ul;
    int timeout;

    if (arg == NULL || strcmp(arg, "forever") == 0) {
        timeout = INFTIM;
        verbose("sleeping forever");
    } else {
        if ((ul = timespan_from_str(arg)) == TS_INVALID) {
            error("%s: invalid timespan", act);
            return -1;
        }
        if (ul / 1000 > (unsigned long)INT_MAX) {
            error("%s: timespan out of range", act);
            return -1;
        }
        timeout = (ul + 999) / 1000;
        verbose("sleeping for %d.%03ds", timeout / 1000, timeout % 1000);
    }
    if (poll(NULL, 0, timeout) < 0) {
        if (errno != EINTR) {
            error("poll(): %m");
            return -1;
        }
        verbose("interrupted");
    }
    return 0;
}

static int mockd_block(const char *act, const char *arg)
{
    sigset_t sigset;
    unsigned long signo = SIGTERM;

    if (arg != NULL) {
        signo = argnum(act, arg, 1, 15);
        if (errno != 0) {
            return -1;
        }
    }
    verbose("blocking signal %ld", signo);
    sigemptyset(&sigset);
    sigaddset(&sigset, signo);
    if (sigprocmask(SIG_BLOCK, &sigset, NULL) != 0) {
        return -1;
    }
    return 0;
}

static int mockd_raise(const char *act, const char *arg)
{
    unsigned long signo = SIGTERM;

    if (arg != NULL) {
        signo = argnum(act, arg, 0, 15);
        if (errno != 0) {
            return -1;
        }
    }
    verbose("raising signal %ld", signo);
    if (raise(signo) != 0) {
        return -1;
    }
    return 0;
}

static int mockd_pidfile(const char *act, const char *arg)
{
    FILE *f;

    (void)act;
    pidfile = arg;
    if (pidfile == NULL) {
        pidfile = getenv("PIDFILE");
    }
    if (pidfile == NULL) {
        pidfile = PATH_PIDFILE;
    }
    verbose("writing PID file %s", pidfile);
    if ((f = fopen(pidfile, "w")) == NULL) {
        error("failed to open PID file %s: %m", pidfile);
        return -1;
    }
    if (fprintf(f, "%u\n", (unsigned int)getpid()) < 0) {
        error("failed to write PID to PID file %s: %m", pidfile);
        fclose(f);
        return -1;
    }
    fclose(f);
    atexit(rmpidfile);
    return 0;
}

static int mockd_syslog(const char *act, const char *arg)
{
    const char *ident = program_invocation_short_name;

    (void)act;
    if (noisef == NULL) {
        return 0;
    }
    verbose("logging to syslog");
    if (arg != NULL) {
        ident = arg;
    }
    noisef = NULL;
    openlog(ident, LOG_PID, LOG_DAEMON);
    return 0;
}

static int mockd_daemon(const char *act, const char *arg)
{
    list_t *keep;
    pid_t pid;
    int status;

    if (arg != NULL) {
        error("%s: no argument expected", act);
        return -1;
    }
    verbose("daemonizing");
    keep = make_list();
    list_append(keep, as_integer(STDIN_FILENO));
    list_append(keep, as_integer(STDOUT_FILENO));
    list_append(keep, as_integer(STDERR_FILENO));
    pid = unixkit_fork(keep);
    if (pid < 0) {
        fatal("fork(): %m");
    }
    if (pid > 0) {
        waitpid(pid, &status, 0);
        _exit(status);
    }
    verbose("mockd intermediate %u", (unsigned int)getpid());
    if (isatty(STDIN_FILENO)) {
        debug("stdin is a tty");
        if (close(STDIN_FILENO) != 0
            || open(_PATH_DEVNULL, O_RDONLY) != STDIN_FILENO) {
            fatal("failed to set up stdin: %m");
        }
    }
    if (isatty(STDOUT_FILENO)) {
        debug("stdout is a tty");
        if (close(STDOUT_FILENO) != 0
            || open(_PATH_DEVNULL, O_WRONLY | O_APPEND) != STDOUT_FILENO) {
            fatal("failed to set up stdout: %m");
        }
    }
    if (isatty(STDERR_FILENO)) {
        debug("stderr is a tty");
        mockd_syslog(NULL, NULL);
        if (close(STDERR_FILENO) != 0
            || open(_PATH_DEVNULL, O_WRONLY | O_APPEND) != STDERR_FILENO) {
            error("failed to set up stderr: %m");
        }
    }
    if (setsid() < 0) {
        fatal("setsid(): %m");
    }
    keep = make_list();
    list_append(keep, as_integer(STDIN_FILENO));
    list_append(keep, as_integer(STDOUT_FILENO));
    list_append(keep, as_integer(STDERR_FILENO));
    pid = unixkit_fork(keep);
    if (pid < 0) {
        fatal("fork(): %m");
    }
    if (pid > 0) {
        _exit(0);
    }
    verbose("mockd daemon pid %u", (unsigned int)getpid());
    return 0;
}

static int mockd_action(const char *act, const char *arg)
{
    if (strcmp(act, "block") == 0) {
        return mockd_block(act, arg);
    } else if (strcmp(act, "daemon") == 0) {
        return mockd_daemon(act, arg);
    } else if (strcmp(act, "exit") == 0) {
        return mockd_exit(act, arg);
    } else if (strcmp(act, "pidfile") == 0) {
        return mockd_pidfile(act, arg);
    } else if (strcmp(act, "raise") == 0) {
        return mockd_raise(act, arg);
    } else if (strcmp(act, "sleep") == 0) {
        return mockd_sleep(act, arg);
    } else if (strcmp(act, "syslog") == 0) {
        return mockd_syslog(act, arg);
    } else {
        error("unrecognized action: %s", act);
        return -1;
    }
}

static int mockd(int argc, char *argv[])
{
    char *act, *arg;
    int res;

    while (argc--) {
        act = *argv++;
        if ((arg = strchr(act, ':')) != NULL) {
            *arg++ = '\0';
        }
        if ((res = mockd_action(act, arg)) != 0) {
            break;
        }
    }
    return 0;
}

static void usage(void) __attribute__((__noreturn__));
static void usage(void)
{
    fprintf(stderr,
            "usage: mockd [-dv] action[:parameter] [...]\n"
            "\n"
            "Available actions:\n"
            "    daemon\n"
            "    exit[:status]\n"
            "    pidfile[:path]\n"
            "    raise[:signal]\n"
            "    sleep[:duration]\n"
            "    syslog[:ident]\n"
            "");
    exit(EX_USAGE);
}

int main(int argc, char *argv[])
{
    int opt, res;

    while ((opt = getopt(argc, argv, "dv")) != -1) {
        switch (opt) {
            case 'd':
                noisy = DEBUG;
                break;
            case 'v':
                noisy = VERBOSE;
                break;
            default:
                usage();
        }
    }
    argc -= optind;
    argv += optind;
    if (argc == 0) {
        usage();
    }
    verbose("mockd pid %u", (unsigned int)getpid());
    if ((res = mockd(argc, argv)) != 0) {
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}
