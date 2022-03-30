#define _GNU_SOURCE

#include "command.h"

#include "environment.h"
#include "exitcode.h"
#include "fork.h"
#include "noise.h"
#include "service.h"
#include "strlist.h"
#include "systemd.h"
#include "sysvrun.h"
#include "text.h"
#include "unit.h"

#include <fsdyn/charstr.h>
#include <fsdyn/fsalloc.h>
#include <unixkit/unixkit.h>

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <paths.h>
#include <pwd.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static enum execflag prefix2flag[256] = {
    ['@'] = EF_AT,   ['-'] = EF_DASH, [':'] = EF_COLON,
    ['+'] = EF_PLUS, ['!'] = EF_BANG,
};

#define DEFAULT_UMASK 0022

static int command_resolve_path_child(struct command *cmd,
                                      const char *name,
                                      bool search,
                                      int pd)
{
    char root[PATH_MAX] = "";
    char path[PATH_MAX] = "";
    const char *p, *q;
    ssize_t ret;

    // Root directory
    if (cmd->rootdir != NULL) {
        debug("resolving root directory: %s", cmd->rootdir);
        if (realpath(cmd->rootdir, root) == NULL) {
            return -1;
        }
        debug("changing root directory to %s", root);
        if (chroot(root) < 0) {
            return -1;
        }
    }
    // Working directory
    if (cmd->workdir != NULL) {
        debug("resolving working directory: %s", cmd->workdir);
        if (realpath(cmd->workdir, path) == NULL) {
            return -1;
        }
        debug("changing working directory to %s", path);
        if (chdir(path) < 0) {
            return -1;
        }
    } else {
        if (chdir("/") < 0) {
            return -1;
        }
    }
    // Simple case: no PATH search
    if (!search || strchr(name, '/') != NULL) {
        debug("resolving name: %s", name);
        memset(path, 0, sizeof(path));
        if (realpath(name, path) == NULL && errno != ENOENT) {
            return -1;
        }
        goto found;
    }
    // We're going to have to search for it; get PATH
    environ = strlist_to_vector(environment_list(cmd->env));
    if ((p = getenv("PATH")) == NULL) {
        p = _PATH_STDPATH;
    }
    // Iterate over PATH
    do {
        for (q = p; *q != '\0' && *q != ':'; q++) {
            // nothing
        }
        if (p == q || *p != '/') {
            goto next;
        }
        // Compose full path
        ret = snprintf(path, sizeof(path), "%.*s/%s", (int)(q - p), p, name);
        if (ret < 0 || (size_t)ret >= sizeof(path)) {
            goto next;
        }
        // Does it exist?
        debug("trying %s", path);
        if (access(path, R_OK | X_OK) == 0) {
            goto found;
        }
    next:
        p = q + 1;
    } while (*q != '\0');
    errno = ENOENT;
    return -1;
found:
    debug("found %s%s", root, path);
    if (write(pd, root, strlen(root)) < 0
        || write(pd, path, strlen(path)) < 0) {
        return -1;
    }
    return 0;
}

// Resolve a path relative to the command's root and / or working directory.
static char *command_resolve_path(struct command *cmd,
                                  const char *name,
                                  bool search)
{
    char buf[PATH_MAX * 2];
    ssize_t len;
    pid_t pid;
    int p[2], status;

    debug("resolving %s", name);
    if (!unixkit_pipe(p)) {
        error("failed to create pipe: %m");
        return NULL;
    }
    if ((pid = fork()) < 0) {
        error("failed to fork path resolver: %m");
        goto fail;
    }
    if (pid == 0) {
        // child
        if (command_resolve_path_child(cmd, name, search, p[1]) < 0) {
            verbose("failed to resolve path '%s': %m", name);
            _exit(errno);
        }
        _exit(0);
    }
    // parent
    if (waitpid(pid, &status, 0) < 0) {
        error("failed to collect path resolver: %m");
        goto fail;
    }
    if (!WIFEXITED(status)) {
        error("path resolver child killed by signal %d", WTERMSIG(status));
        errno = EINTR; // XXX no better
        goto fail;
    }
    errno = WEXITSTATUS(status);
    if (errno != 0) {
        error("failed to resolve path: %m");
        goto fail;
    }
    if ((len = read(p[0], buf, sizeof(buf))) < 0) {
        error("failed to receive result from path resolver: %m");
        goto fail;
    }
    close(p[0]);
    close(p[1]);
    return charstr_dupsubstr(buf, buf + len);
fail:
    close(p[0]);
    close(p[1]);
    return NULL;
}

// Allocates and populates a command struct based on the contents of a unit
// struct.
struct command *command_from_service(struct service *svc, const char *cmdkey)
{
    struct command *cmd;
    struct passwd *pw;
    struct group *gr;
    const char *key, *value;
    list_t *list;
    long num;
    char *str, *end;

    // Retrieve and split the command line
    value = unit_get_value(svc->u, "Service", cmdkey);
    if (value == NULL) {
        errno = ENOENT;
        return NULL;
    }
    cmd = fscalloc(1, sizeof(*cmd));
    cmd->svc = svc;
    if (strchr(value, '$') != NULL) {
        warning("variable substitution not implemented");
    }
    if (strchr(value, '%') != NULL) {
        warning("specifiers not implemented");
    }
    cmd->args = systemd_split_quoted(value);
    if (list_empty(cmd->args)) {
        error("command line empty");
        goto fail;
    }

    // Prepare the environment
    cmd->env = environment_clone(Denv);
    value = unit_get_value(svc->u, "Service", "Environment");
    if (value != NULL) {
        list = systemd_split_quoted(value);
        while ((str = DQ(list_pop_first(list))) != NULL) {
            environment_put(cmd->env, str, true);
            fsfree(str);
        }
        destroy_list(list);
    }
    value = unit_get_value(svc->u, "Service", "PassEnvironment");
    if (value != NULL) {
        list = systemd_split_quoted(value);
        while ((key = list_pop_first(list)) != NULL) {
            if ((value = getenv(key)) != NULL) {
                environment_set(cmd->env, key, value, true);
            }
            fsfree(DQ(key));
        }
        destroy_list(list);
    }
    value = unit_get_value(svc->u, "Service", "UnsetEnvironment");
    if (value != NULL) {
        list = systemd_split_quoted(value);
        environment_remove_keys(cmd->env, list);
        strlist_free(list);
    }
    environment_remove_keys(cmd->env, Ulist);

    // Ensure that we are in the service's PATH
    value = environment_get(cmd->env, "PATH");
    if (value != NULL) {
        list = strlist_from_delim(value, ':', false, false);
        list_prepend(list, self_dir);
        str = strlist_to_delim(list, ':', true);
        list_pop_first(list); // self_dir
        strlist_free(list);
        environment_set(cmd->env, "PATH", str, true);
        fsfree(str);
    }

    // Root directory and working directory
    value = unit_get_value(svc->u, "Service", "RootDirectory");
    if (value != NULL) {
        cmd->rootdir = charstr_printf("%s%s", root, value);
    }
    value = unit_get_value(svc->u, "Service", "WorkingDirectory");
    if (value != NULL) {
        cmd->workdir = charstr_printf("%s%s", root, value);
    }

    // PID file
    value = unit_get_value(svc->u, "Service", "PIDFile");
    if (value != NULL) {
        cmd->pidfile = command_resolve_path(cmd, value, false);
        if (cmd->pidfile == NULL) {
            error("invalid PID file path %s: %m", value);
            goto fail;
        }
        environment_set(cmd->env, "PIDFILE", cmd->pidfile, true);
    }

    // Find the binary
    value = list_elem_get_value(list_get_first(cmd->args));
    while (*value != '\0' && prefix2flag[(unsigned char)*value] != 0) {
        cmd->flags |= prefix2flag[(unsigned char)*value++];
    }
    cmd->path = command_resolve_path(cmd, value, true);
    if (cmd->path == NULL) {
        error("command '%s' not found: %m", value);
        goto fail;
    }
    if (cmd->flags & EF_AT) {
        fsfree(DQ(list_pop_first(cmd->args)));
        if (list_empty(cmd->args)) {
            error("command line empty after @ prefix");
            goto fail;
        }
    }

    // Credentials
    value = unit_get_value(svc->u, "Service", "User");
    if (value != NULL) {
        // XXX should look this up inside the chroot if there is one
        if ((pw = getpwnam(value)) == NULL) {
            error("user '%s' not found", value);
            goto fail;
        }
        cmd->uid = pw->pw_uid;
        if (cmd->workdir != NULL && cmd->workdir[0] == '~'
            && cmd->workdir[1] == '\0') {
            fsfree(cmd->workdir);
            cmd->workdir = charstr_dupstr(pw->pw_dir);
        }
        environment_set(cmd->env, "USER", pw->pw_name, false);
        environment_set(cmd->env, "LOGNAME", pw->pw_name, false);
        environment_set(cmd->env, "HOME", pw->pw_dir, false);
        environment_set(cmd->env, "SHELL", pw->pw_shell, false);
    }
    value = unit_get_value(svc->u, "Service", "Group");
    if (value != NULL) {
        // XXX should look this up inside the chroot if there is one
        if ((gr = getgrnam(value)) == NULL) {
            error("group '%s' not found", value);
            goto fail;
        }
        // XXX only primary group for now
        cmd->gid = gr->gr_gid;
    }

    // File permission mask
    value = unit_get_value(svc->u, "Service", "UMask");
    if (value != NULL) {
        errno = 0;
        num = strtol(value, &end, 8);
        if (end == value || *end != '\0' || errno != 0) {
            error("invalid umask '%s'", value);
            goto fail;
        }
        if (num < 0 || num > 0777) {
            error("umask %lo out of range", num);
            goto fail;
        }
        cmd->umask = num;
    } else {
        cmd->umask = DEFAULT_UMASK;
        debug("umask not specified, defaulting to %04o", cmd->umask);
    }

    // Default working directory if none was set
    if (cmd->workdir == NULL) {
        cmd->workdir = charstr_printf("%s/", root);
    }

    return cmd;
fail:
    command_free(cmd);
    errno = EINVAL;
    return NULL;
}

void command_free(struct command *cmd)
{
    if (cmd != NULL) {
        fsfree(cmd->path);
        fsfree(cmd->rootdir);
        fsfree(cmd->workdir);
        fsfree(cmd->pidfile);
        strlist_free(cmd->args);
        environment_free(cmd->env);
        fsfree(cmd);
    }
}

// Executes a command.  On failure, returns one of the systemd exit codes.
// Suitable for use with fork_function() or daemonize_function().
int command_exec_func(void *ptr)
{
    struct command *cmd = ptr;
    list_t *envl;
    char **argv, **envv;

    // Prepare argument and environment vectors
    argv = strlist_to_vector(cmd->args);
    envl = environment_list(cmd->env);
    envv = strlist_to_vector(envl);
    strlist_free(envl);
    // Change root directory if necessary
    if (cmd->rootdir != NULL) {
        if (chroot(cmd->rootdir) != 0 || chdir("/") != 0) {
            error("failed to chroot to %s: %m", cmd->rootdir);
            return EXIT_CHROOT;
        }
    }
    // Change working directory if necessary
    if (cmd->workdir != NULL) {
        if (chdir(cmd->workdir) != 0) {
            error("failed to chdir to %s: %m", cmd->workdir);
            return EXIT_CHDIR;
        }
    }
    // Switch credentials
    if (cmd->gid != 0 && (cmd->flags & EF_PLUS) == 0) {
        if (setregid(cmd->gid, cmd->gid) != 0) {
            error("failed to set primary group to %d: %m", cmd->gid);
            return EXIT_GROUP;
        }
        // XXX only primary group for now
        if (setgroups(1, &cmd->gid) != 0) {
            error("failed to set supplemental groups: %m");
            return EXIT_GROUP;
        }
    }
    if (cmd->uid != 0 && (cmd->flags & EF_PLUS) == 0) {
        if (setreuid(cmd->uid, cmd->uid) != 0) {
            error("failed to set uid to %d: %m", cmd->uid);
            return EXIT_USER;
        }
    }
    // Set file permission mask
    umask(cmd->umask);
    // And go!
    execve(cmd->path, argv, envv);
    error("failed to execute %s: %m", cmd->path);
    return EXIT_EXEC;
}

pid_t command_getpid(struct command *cmd)
{
    struct text *text, *word;
    char *end;
    long num;

    errno = EINVAL; // strtoul() may change it to ERANGE
    num = -1;
    if (cmd->pidfile != NULL) {
        debug("reading PID file %s", cmd->pidfile);
        text = text_from_file(cmd->pidfile);
        if (text != NULL) {
            word = text_first_word(text);
            if (word != NULL) {
                num = strtol(word->beg, &end, 10);
                if (num < 0 || end == word->beg || end != word->end) {
                    errno = EINVAL;
                    num = -1;
                } else if (errno == ERANGE) {
                    num = -1;
                }
                text_free(word);
            }
            text_free(text);
        }
        if (num < 0) {
            warning("failed to read PID file %s: %m", cmd->pidfile);
        } else {
            debug("PID file %s contains PID %ld", cmd->pidfile, num);
        }
    }
    return num;
}

int command_rmpid(struct command *cmd)
{
    if (cmd->pidfile == NULL) {
        errno = EINVAL;
        return -1;
    }
    debug("removing PID file %s", cmd->pidfile);
    if (unlink(cmd->pidfile) && errno != ENOENT) {
        warning("failed to remove PID file %s: %m", cmd->pidfile);
        return -1;
    }
    return 0;
}

void command_verbose(struct command *cmd)
{
    byte_array_t *ba;

    if (noisy < VERBOSE) {
        return;
    }
    ba = command_to_byte_array(cmd, NULL);
    verbose("%s", (const char *)byte_array_data(ba));
    destroy_byte_array(ba);
}

// Daemonizes and executes a command.  Returns the daemon's PID if successful, a
// negative value corresponding to a systemd exit code otherwise.
pid_t command_daemonize(struct command *cmd)
{
    command_verbose(cmd);
    return daemonize_function(command_exec_func, cmd, NULL);
}

// Forks and executes a command without daemonizing.  Returns the child's PID if
// successful, a negative value corresponding to a systemd exit code otherwise.
pid_t command_fork(struct command *cmd)
{
    command_verbose(cmd);
    return fork_function(command_exec_func, cmd, NULL);
}

// Executes a command and wait for it to terminate.  If successful, returns zero
// or a positive value corresponding to the command's exit status.  Otherwise,
// returns a negative value corresponding to a systemd exit code indicating the
// type of failure.
int command_run(struct command *cmd)
{
    pid_t pid;

    command_verbose(cmd);
    pid = fork_function(command_exec_func, cmd, NULL);
    if (pid < 0) {
        return pid;
    }
    if (waitpid(pid, &cmd->wstatus, 0) < 0) {
        return -EXIT_FAILURE;
    }
    return cmd->wstatus;
}

// Kills the process referenced by the command's PID file, or, if pg is true,
// its process group.  Note that the command itself is irrelevant; only its PID
// file is used.
static int command_kill_impl(struct command *cmd, int signo, bool pg)
{
    pid_t pid, pgid;

    if (cmd->pidfile == NULL) {
        verbose("no PID file specified");
        errno = ENOENT;
        return -1;
    }
    pid = command_getpid(cmd);
    if (pid <= 0) {
        if (pid == 0 || errno == ENOENT) {
            verbose("PID file %s is not in use", cmd->pidfile);
            errno = ENOENT;
            return -1;
        }
        if (errno == EINVAL || errno == ERANGE) {
            error("PID file %s contents invalid: %m", cmd->pidfile);
            errno = EINVAL;
            return -1;
        }
        error("failed to read PID from %s: %m", cmd->pidfile);
        return -1;
    }
    if (pg) {
        pgid = getpgid(pid);
        if (pgid <= 0) {
            error("failed to determine process group for process %u: %m",
                  (unsigned int)pid);
            return -1;
        }
        if (kill(-pgid, signo) != 0) {
            if (signo != 0) {
                // Signal 0 is just a probe, don't yell
                error("failed to signal process group %u: %m",
                      (unsigned int)pgid);
            }
            return -1;
        }
        verbose("sent signal %d to process group %u",
                signo,
                (unsigned int)pgid);
        if (signo != 0 && signo != SIGCONT) {
            (void)kill(-pgid, SIGCONT);
        }
    } else {
        if (kill(pid, signo) != 0) {
            if (signo != 0) {
                // Signal 0 is just a probe, don't yell
                error("failed to signal process %u: %m", (unsigned int)pid);
            }
            return -1;
        }
        verbose("sent signal %d to process %u", signo, (unsigned int)pid);
        if (signo != 0 && signo != SIGCONT) {
            (void)kill(pid, SIGCONT);
        }
    }
    return 0;
}

// Kills the process referenced by the command's PID file.
int command_kill(struct command *cmd, int signo)
{
    return command_kill_impl(cmd, signo, false);
}

// Kills the process group of the process referenced by the command's PID file.
int command_killpg(struct command *cmd, int signo)
{
    return command_kill_impl(cmd, signo, true);
}

byte_array_t *command_to_byte_array(struct command *cmd, byte_array_t *ba)
{
    byte_array_t *nba = NULL;
    list_elem_t *e;
    const char *arg;

    if (ba == NULL) {
        ba = nba = make_byte_array(SIZE_MAX);
    }
    environment_to_byte_array(cmd->env, ba);
    if (!byte_array_appendf(ba, "exec %s", cmd->path)) {
        goto fail;
    }
    e = list_get_first(cmd->args); // skip first
    while ((e = list_next(e)) != NULL) {
        arg = list_elem_get_value(e);
        if (!byte_array_appendf(ba, " %s", arg)) {
            goto fail;
        }
    }
    return ba;
fail:
    if (nba != NULL) {
        destroy_byte_array(nba);
    }
    return NULL;
}

char *command_to_string(struct command *cmd)
{
    byte_array_t *ba;
    char *str = NULL;

    ba = make_byte_array(SIZE_MAX);
    if (!command_to_byte_array(cmd, ba)) {
        goto end;
    }
    str = charstr_dupstr(byte_array_data(ba));
end:
    destroy_byte_array(ba);
    return str;
}
