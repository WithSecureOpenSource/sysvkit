#include "systemctl.h"

#include <fsdyn/charstr.h>
#include <fsdyn/integer.h>
#include <unixkit/unixkit.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <paths.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Returns an allocated buffer containing the path of the init script for the
// specified service.
static char *init_path(const char *name)
{
    return charstr_printf("%s/etc/init.d/%s", root, name);
}

// Returns an allocated buffer containing the path of the rc directory for the
// specified runlevel.
static char *rcdir_path(int rl)
{
    return charstr_printf("%s/etc/rc%d.d", root, rl);
}

// Returns an allocated buffer containing the name of a startup symlink for the
// specified service.
static char *start_link(const char *name)
{
    return charstr_printf("S%02d%s", 20, name);
}

// Returns an allocated buffer containing the name of a stop symlink for the
// specified service.
static char *stop_link(const char *name)
{
    return charstr_printf("K%02d%s", 80, name);
}

#define DOT_SERVICE ".service"

// Locates the service with the specified name and returns either a pointer to a
// struct describing it, or NULL if it was not found or some other error
// occurred.
struct service *service_find(const char *name)
{
    struct service *svc = NULL;
    int fd = -1, len, serrno;

    len = strlen(name);
    // Subtract .service suffix from length of name if present
    if (charstr_ends_with(name, DOT_SERVICE)) {
        len -= strlen(DOT_SERVICE);
    }
    if (len == 0) {
        errno = EINVAL;
        return NULL;
    }
    svc = fscalloc(1, sizeof(*svc));
    svc->name = charstr_dupsubstr(name, name + len);
    svc->path = init_path(svc->name);
    if ((fd = open(svc->path, O_RDONLY)) < 0) {
        goto fail;
    }
    if (fstat(fd, &svc->sb) != 0) {
        goto fail;
    }
    close(fd);
    return svc;
fail:
    serrno = errno;
    fsfree(svc->path);
    fsfree(svc->name);
    fsfree(svc);
    if (fd != -1) {
        close(fd);
    }
    errno = serrno;
    return NULL;
}

// Frees a struct previously returned by service_find().
void service_free(struct service *svc)
{
    fsfree(svc);
}

static void service_child(const char *, const char *, int, bool)
    __attribute__((__noreturn__));

static void service_child(const char *path,
                          const char *command,
                          int chan,
                          bool silent)
{
    char *argv[] = { DQ(path), DQ(command), NULL };

    if (close(0) != 0 || open(_PATH_DEVNULL, O_RDONLY) != 0) {
        goto fail;
    }
    if (silent && noisy < VERBOSE) {
        if (close(1) != 0 || open(_PATH_DEVNULL, O_WRONLY) != 1) {
            goto fail;
        }
        if (close(2) != 0 || open(_PATH_DEVNULL, O_WRONLY) != 2) {
            goto fail;
        }
    }
    execv(path, argv);
fail:
    OK(write(chan, &errno, sizeof(errno)));
    _exit(1);
}

// Invokes the service's init script.  Returns the script's exit status, or a
// negative number if an error occurred.  If the script was killed by a signal,
// the return value will be the negation of the signal number; otherwise, it
// will be -1 and errno will be set accordingly.  If silent is true, the
// script's output will be suppressed, unless noisy is VERBOSE or higher.
int service_invoke(struct service *svc, const char *command, bool silent)
{
    list_t *keep;
    pid_t pid;
    int chan[2], res, status;

    if (!unixkit_pipe(chan)) {
        return -1;
    }
    keep = make_list();
    list_append(keep, as_integer(STDIN_FILENO));
    list_append(keep, as_integer(STDOUT_FILENO));
    list_append(keep, as_integer(STDERR_FILENO));
    list_append(keep, as_integer(chan[1]));
    pid = unixkit_fork(keep);
    if (pid < 0) {
        close(chan[1]);
        close(chan[0]);
        return -1;
    }
    if (pid == 0) {
        // child
        service_child(svc->path, command, chan[1], silent);
        // never returns
    }
    // parent
    close(chan[1]);
    while ((res = waitpid(pid, &status, 0)) == 0) {
        // wait...
    }
    if (res < 0) {
        close(chan[0]);
        return -1;
    }
    res = read(chan[0], &errno, sizeof(errno));
    close(chan[0]);
    if (res > 0) {
        return -1;
    }
    if (WIFSIGNALED(status)) {
        errno = 0;
        return -WTERMSIG(status);
    }
    // assert(WIFEXITED(status));
    return WEXITSTATUS(status);
}

// Scans the rc directory for the specified runlevel looking for links to the
// specific service.  If del is true, removes any existing links.  If add is
// true, adds a link if none was found or existing ones were removed.  In all
// cases, returns a positive number if at least one link was found, zero if no
// links were found, and a negative number if an error occurred.
//
// Note that POSIX guarantees that unlinking an entry that has already been
// returned by readdir is safe, but if an entry is unlinked before readdir(2)
// has returned it, readdir(2) may or may not return it anyway.
static int service_manip_rl(struct service *svc, int rl, bool del, bool add)
{
    struct stat sb;
    DIR *dirp;
    struct dirent *de;
    char *name, *rcdir;
    int ret = 0;
    bool found = false;

    if (rl >= 2 && rl <= 5) {
        name = start_link(svc->name);
    } else {
        name = stop_link(svc->name);
    }
    rcdir = rcdir_path(rl);
    dirp = opendir(rcdir);
    if (dirp == NULL) {
        return -1;
    }
    while ((de = readdir(dirp)) != NULL) {
        if (de->d_type != DT_LNK) {
            continue;
        }
        if (fstatat(dirfd(dirp), de->d_name, &sb, 0) != 0 && errno != ENOENT) {
            ret = -errno;
            break;
        }
        if (sb.st_dev == svc->sb.st_dev && sb.st_ino == svc->sb.st_ino) {
            // This links to our service.
            if (add && strcmp(de->d_name, name) != 0) {
                // Wrong name.  We will create the right one later.
                verbose("deleting %s/%s", rcdir, de->d_name);
                if (unlinkat(dirfd(dirp), de->d_name, 0) != 0
                    && errno != ENOENT) {
                    ret = -errno;
                }
                continue;
            }
            found = true;
            if (del && !add) {
                verbose("deleting %s/%s", rcdir, de->d_name);
                if (unlinkat(dirfd(dirp), de->d_name, 0) != 0
                    && errno != ENOENT) {
                    ret = -errno;
                }
            }
        } else if (add && strcmp(de->d_name, name) == 0) {
            // Links somewhere else.  Someone stole our name!
            verbose("deleting %s/%s", rcdir, de->d_name);
            if (unlinkat(dirfd(dirp), de->d_name, 0) != 0 && errno != ENOENT) {
                ret = -errno;
            }
        }
    }
    if (add && !found && ret == 0) {
        verbose("creating %s/%s -> %s", rcdir, name, svc->path);
        if (symlinkat(svc->path, dirfd(dirp), name) != 0 && errno != EEXIST) {
            ret = -errno;
        }
    }
    closedir(dirp);
    fsfree(rcdir);
    fsfree(name);
    if (ret < 0) {
        errno = -ret; // closedir() may have modified it
        return -1;
    }
    return found;
}

// Scans the rc directories for runlevels 0 through 6 looking for links to the
// specific service.  If del is true, removes any unwanted links.  If add is
// true, adds a link if none was found.  In all cases, returns either the number
// of links that were found or a negative number if an error occurred.
static int service_manip(struct service *svc, bool del, bool add)
{
    int res, ret = 0;

    for (int rl = 0; rl <= 6; rl++) {
        if ((res = service_manip_rl(svc, rl, del, add)) < 0) {
            ret = -errno;
        } else if (ret >= 0) {
            ret += res;
        }
    }
    if (ret < 0) {
        errno = -ret;
    }
    return ret;
}

// Returns a positive non-zero number if the service is enabled at the given
// runlevel, zero if it is not, and a negative number if an error occurs.
int service_is_enabled_rl(struct service *svc, int rl)
{
    return service_manip_rl(svc, rl, false, false);
}

// Returns a positive non-zero number if the service is enabled at one or more
// of runlevels 2 through 5, zero if it is not, and a negative number if an
// error occurs.
int service_is_enabled(struct service *svc)
{
    return service_manip(svc, false, false);
}

// Disables a service at the specified runlevel.  Returns zero if it was not
// enabled, a positive non-zero number if it was successfully disabled, and a
// negative number if an error occurred.
int service_disable_rl(struct service *svc, int rl)
{
    return service_manip_rl(svc, rl, true, false);
}

// Disables a service at runlevels 2 through 5.  Returns zero if it was not
// enabled at any runlevel, a positive non-zero number if it was successfully
// disabled at every runlevel at which it was enabled, and a negative number if
// an error occurred.
int service_disable(struct service *svc)
{
    return service_manip(svc, true, false);
}

// Enables a service at the specified runlevel.  Returns zero if it was
// successfully enabled, a positive non-zero number if it was already enabled,
// and a negative number if an error occurred.
int service_enable_rl(struct service *svc, int rl)
{
    return service_manip_rl(svc, rl, false, true);
}

// Enables a service at runlevels 2 through 5.  Returns zero if it was
// successfully enabled at all runlevels, a positive non-zero number if it was
// already enabled at one or more runlevels, and a negative number if an error
// occurred.
int service_enable(struct service *svc)
{
    return service_manip(svc, false, true);
}
