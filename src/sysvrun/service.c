#include "service.h"

#include "command.h"
#include "common.h"
#include "exitcode.h"
#include "monitor.h"
#include "noise.h"
#include "strlist.h"
#include "systemd.h"
#include "sysvinit.h"
#include "sysvrun.h"
#include "timespan.h"
#include "unit.h"

#include <fsdyn/charstr.h>
#include <fsdyn/fsalloc.h>
#include <fsdyn/hashtable.h>
#include <fsdyn/list.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define DEFAULT_SERVICETYPE ST_SIMPLE
#define DEFAULT_KILL_MODE KM_CGROUP
#define DEFAULT_STOP_TIMEOUT_US 90 * TS_SEC
#define DEFAULT_RESTART_POLICY RP_NO
#define DEFAULT_RESTART_DELAY_US 100 * TS_MSEC
#define DEFAULT_START_LIMIT_BURST 5
#define DEFAULT_START_LIMIT_INTERVAL_US 10 * TS_SEC

const char *service_type_names[] = {
    [ST_SIMPLE] = "simple",
    [ST_EXEC] = "exec",
    [ST_FORKING] = "forking",
    [ST_ONESHOT] = "oneshot",
    [ST_DBUS] = "dbus",
    [ST_NOTIFY] = "notify",
    [ST_IDLE] = "idle",
    //
    NULL,
};

const char *kill_mode_names[] = {
    [KM_CGROUP] = "control-group",
    [KM_MIXED] = "mixed",
    [KM_PROCESS] = "process",
    [KM_NONE] = "none",
    //
    NULL,
};

const char *restart_policy_names[] = {
    [RP_NO] = "no",
    [RP_ALWAYS] = "always",
    [RP_ON_SUCCESS] = "on-success",
    [RP_ON_FAILURE] = "on-failure",
    [RP_ON_ABNORMAL] = "on-abnormal",
    [RP_ON_ABORT] = "on-abort",
    // [RP_ON_WATCHDOG] = "on-watchdog", /* unsupported */
    //
    NULL,
};

static struct service *service_create(const char *name)
{
    struct service *svc;

    svc = fscalloc(1, sizeof(*svc));
    svc->name = charstr_dupstr(name);
    deservicify(svc->name);
    return svc;
}

struct service *service_from_unit_file(const char *name, const struct text *txt)
{
    char buf[64];
    struct service *svc;
    const char *value;
    char *end;
    list_elem_t *e, *next;
    hash_table_t *dedup;
    unsigned long num;
    unsigned int i;

    svc = service_create(name);
    if ((svc->u = systemd_parse_unit_file(name, txt)) == NULL) {
        service_free(svc);
        return NULL;
    }
    verbose("extracting service info from unit");

    // Units required by this one.  Note that this does not imply an ordering.
    value = unit_get_value(svc->u, "Unit", "Requires");
    if (value != NULL) {
        svc->required = systemd_split_quoted(value);
        for (e = list_get_first(svc->required); e != NULL; e = next) {
            next = list_next(e);
            value = list_elem_get_value(e);
            verbose("requires %s", value);
            if (!deservicify(DQ(value))) {
                fsfree(DQ(value));
                list_remove(svc->required, e);
            }
        }
    } else {
        svc->required = make_list();
    }

    // Units that should be started before this one if present.  This list is
    // usually but not necessarily a superset of the Requires list (see above).
    value = unit_get_value(svc->u, "Unit", "After");
    if (value != NULL) {
        svc->should = systemd_split_quoted(value);
        for (e = list_get_first(svc->should); e != NULL; e = next) {
            next = list_next(e);
            value = list_elem_get_value(e);
            verbose("after %s", value);
            if (!deservicify(DQ(value))) {
                fsfree(DQ(value));
                list_remove(svc->should, e);
            }
        }
    } else {
        svc->should = make_list();
    }

    // In the systemd world, dependency and ordering are strictly orthogonal.  A
    // unit can require any number of other units to also be present and
    // enabled, and it can require any number of units _if enabled_ to be
    // started before, or not started until after, itself.  A unit can be in one
    // of the latter groups without being in the former, and vice versa.
    //
    // In the sysvinit world, dependency implies ordering, but not vice versa.
    // There is a list of services which must be started before this one, and a
    // list of services which, if present and enabled, should be started before
    // this one.  Strictly speaking, the former corresponds to the intersection
    // of systemd's After and Requires, and the latter corresponds to the
    // difference of After and Requires.  However, since sysvinit does not have
    // a concept of unordered dependency, we use Required-Start (ordered strong
    // dependency) for systemd's Requires and Should-Start (ordered weak
    // depdency) for the rest.  This means that we need to remove any elements
    // of After that are also present in Requires.
    if (list_size(svc->required) > 0 && list_size(svc->should) > 0) {
        dedup = make_hash_table(list_size(svc->required),
                                (void *)hash_string,
                                (void *)strcmp);
        for (e = list_get_first(svc->required); e != NULL; e = list_next(e)) {
            value = list_elem_get_value(e);
            hash_table_put(dedup, value, value);
        }
        for (e = list_get_first(svc->should); e != NULL; e = next) {
            next = list_next(e);
            value = list_elem_get_value(e);
            if (hash_table_get(dedup, value) != NULL) {
                // duplicate
                fsfree(DQ(value));
                list_remove(svc->should, e);
            }
        }
        destroy_hash_table(dedup);
    }

    // Determine service type (aka start-up type in systemd docs)
    value = unit_get_value(svc->u, "Service", "Type");
    if (value == NULL) {
        svc->type = DEFAULT_SERVICETYPE;
        debug("startup type not specified, defaulting to %s",
              service_type_names[svc->type]);
    } else {
        for (i = 0; i < ST_MAX; i++) {
            if (strcmp(value, service_type_names[i]) == 0) {
                svc->type = i;
                break;
            }
        }
        if (i == ST_MAX) {
            error("invalid or unsupported startup type '%s'", value);
            goto fail;
        }
        verbose("startup type: %s", service_type_names[svc->type]);
    }

    // Determine kill mode
    value = unit_get_value(svc->u, "Service", "KillMode");
    if (value == NULL) {
        svc->kill_mode = DEFAULT_KILL_MODE;
        debug("kill mode not specified, defaulting to %s",
              kill_mode_names[svc->kill_mode]);
    } else {
        for (i = 0; i < KM_MAX; i++) {
            if (strcmp(value, kill_mode_names[i]) == 0) {
                svc->kill_mode = i;
                break;
            }
        }
        if (i == KM_MAX) {
            error("invalid or unsupported kill mode '%s'", value);
            goto fail;
        }
        verbose("kill mode: %s", kill_mode_names[svc->kill_mode]);
    }

    // Determine stop timeout
    value = unit_get_value(svc->u, "Service", "TimeoutStopSec");
    if (value == NULL) {
        svc->stop_timeout = DEFAULT_STOP_TIMEOUT_US;
        timespan_to_str(buf, sizeof(buf), svc->stop_timeout);
        debug("stop timeout not specified, defaulting to %s", buf);
    } else {
        svc->stop_timeout = timespan_from_str(value);
        if (svc->stop_timeout == TS_INVALID) {
            error("invalid stop timeout '%s'", value);
            goto fail;
        }
        timespan_to_str(buf, sizeof(buf), svc->stop_timeout);
        verbose("stop timeout: %s", buf);
    }

    // Determine restart policy
    value = unit_get_value(svc->u, "Service", "Restart");
    if (value == NULL) {
        svc->restart_policy = DEFAULT_RESTART_POLICY;
        debug("restart policy not specified, defaulting to %s",
              restart_policy_names[svc->restart_policy]);
    } else {
        for (i = 0; i < RP_MAX; i++) {
            if (strcmp(value, restart_policy_names[i]) == 0) {
                svc->restart_policy = i;
                break;
            }
        }
        if (i == RP_MAX) {
            error("invalid or unsupported restart policy '%s'", value);
            goto fail;
        }
        verbose("restart policy: %s",
                restart_policy_names[svc->restart_policy]);
    }

    // Determine restart delay
    if (svc->restart_policy != RP_NO) {
        value = unit_get_value(svc->u, "Service", "RestartSec");
        if (value == NULL) {
            svc->delay = DEFAULT_RESTART_DELAY_US;
            timespan_to_str(buf, sizeof(buf), svc->delay);
            debug("restart delay not specified, defaulting to %s", buf);
        } else {
            svc->delay = timespan_from_str(value);
            if (svc->delay == TS_INVALID) {
                error("invalid restart delay '%s'", value);
                goto fail;
            }
            timespan_to_str(buf, sizeof(buf), svc->delay);
            verbose("restart delay: %s", buf);
        }
    }
    if (unit_get_bool(svc->u, "Service", "RemainAfterExit") > 0) {
        svc->remain_after_exit = true;
    }

    // Determine rate limiting parameters
    value = unit_get_value(svc->u, "Service", "StartLimitInterval");
    if (value == NULL) {
        svc->start_limit_interval = DEFAULT_START_LIMIT_INTERVAL_US;
        timespan_to_str(buf, sizeof(buf), svc->start_limit_interval);
        debug("start limit interval not specified, defaulting to %s", buf);
    } else {
        svc->start_limit_interval = timespan_from_str(value);
        if (svc->start_limit_interval == TS_INVALID) {
            error("invalid start limit interval '%s'", value);
            goto fail;
        }
        timespan_to_str(buf, sizeof(buf), svc->start_limit_interval);
        verbose("start limit interval: %s", buf);
    }
    value = unit_get_value(svc->u, "Service", "StartLimitBurst");
    if (value == NULL) {
        svc->start_limit_burst = DEFAULT_START_LIMIT_BURST;
        debug("start limit burst not specified, defaulting to %lu",
              svc->start_limit_burst);
    } else {
        errno = 0;
        num = strtoul(value, &end, 10);
        if (*value == '-' || end == value || *end != '\0' || errno != 0) {
            error("invalid start limit burst '%s'", value);
            goto fail;
        }
        svc->start_limit_burst = num;
        verbose("start limit burst: %lu", svc->start_limit_burst);
    }

    return svc;
fail:
    service_free(svc);
    return NULL;
}

struct service *service_from_init_script(const char *name,
                                         const struct text *txt)
{
    struct service *svc;

    if ((svc = sysvinit_parse_init_script(name, txt)) == NULL) {
        return NULL;
    }
    return svc;
}

struct service *service_from_file(const char *name, const char *path)
{
    struct service *svc = NULL;
    struct text *txt = NULL;

    verbose("loading '%s' service from %s", name, path);
    if ((txt = text_from_file(path)) == NULL) {
        return NULL;
    }
    if (txt->len > 3 && txt->beg[0] == '#' && txt->beg[1] == '!') {
        svc = service_from_init_script(name, txt);
    } else {
        svc = service_from_unit_file(name, txt);
    }
    text_free(txt);
    if (svc == NULL) {
        if (errno == ENOENT) {
            fprintf(stderr, "service '%s' not found in %s\n", name, path);
        }
        fsfree(svc);
        return NULL;
    }
    return svc;
}

struct service *service_find(const char *name)
{
    struct service *svc;

    if ((svc = systemd_find_service(name)) == NULL
        && (svc = sysvinit_find_service(name)) == NULL) {
        return NULL;
    }
    return svc;
}

void service_free(struct service *svc)
{
    if (svc != NULL) {
        unit_free(svc->u);
        fsfree(svc->name);
        strlist_free(svc->required);
        strlist_free(svc->should);
        fsfree(svc);
    }
}

byte_array_t *service_to_byte_array(struct service *svc, byte_array_t *ba)
{
    byte_array_t *nba = NULL;
    list_elem_t *e;
    const char *value;

#define appendf(...)                                \
    do {                                            \
        if (!byte_array_appendf(ba, __VA_ARGS__)) { \
            goto fail;                              \
        }                                           \
    } while (0)

    if (ba == NULL) {
        ba = nba = make_byte_array(SIZE_MAX);
    }
    appendf("#!/bin/sh\n\n%s\n", LSB_BEGIN_INIT_INFO);
    appendf("# %-22s%s\n", "Provides:", svc->name);
    if (list_size(svc->required) > 0) {
        appendf("# %-21s", "Required-Start:");
        for (e = list_get_first(svc->required); e != NULL; e = list_next(e)) {
            value = list_elem_get_value(e);
            appendf(" %s", value);
        }
        appendf("\n");
    }
    if (list_size(svc->should) > 0) {
        appendf("# %-21s", "Should-Start:");
        for (e = list_get_first(svc->should); e != NULL; e = list_next(e)) {
            value = list_elem_get_value(e);
            appendf(" %s", value);
        }
        appendf("\n");
    }
    appendf("# %-22s%s\n", "Default-Start:", "2 3 4 5");
    value = unit_get_value(svc->u, "Unit", "Description");
    if (value != NULL) {
        appendf("# %-22s%s\n", "Short-Description", value);
    }
    appendf("%s\n\n", LSB_END_INIT_INFO);
    appendf("exec %s -u \"$0\" %s \"$@\"\n\n", self, svc->name);
    appendf("%s\n", BEGIN_EMBED);
    if (unit_to_byte_array(svc->u, ba) == NULL) {
        goto fail;
    }
    appendf("%s\n", END_EMBED);
    return ba;
fail:
    if (nba != NULL) {
        destroy_byte_array(nba);
    }
    return NULL;
}

char *service_to_string(struct service *svc)
{
    byte_array_t *ba;
    char *str = NULL;

    ba = make_byte_array(SIZE_MAX);
    if (!service_to_byte_array(svc, ba)) {
        goto end;
    }
    str = charstr_dupstr(byte_array_data(ba));
end:
    destroy_byte_array(ba);
    return str;
}

static int byte_array_to_file(byte_array_t *ba, const char *name, int mode)
{
    char *tmpname = NULL;
    ssize_t res;
    int fd = -1, mask;

    tmpname = charstr_printf("%s.XXXXXX", name);
    fd = mkstemp(tmpname);
    if (fd < 0) {
        error("failed to create %s: %m", tmpname);
        goto fail;
    }
    res = write(fd, byte_array_data(ba), byte_array_size(ba));
    if (res < 0) {
        error("failed to write %s: %m", tmpname);
        goto fail;
    }
    if ((size_t)res != byte_array_size(ba)) {
        error("short write to %s", tmpname);
        goto fail;
    }
    mask = umask(0777);
    mode &= ~mask;
    umask(mask);
    if (fchmod(fd, mode) != 0) {
        error("failed to set mode %04o on %s: %m", mode, tmpname);
        goto fail;
    }
    if (rename(tmpname, name) != 0) {
        error("failed to rename %s to %s: %m", tmpname, name);
        goto fail;
    }
    close(fd);
    fsfree(tmpname);
    return res;
fail:
    if (fd != -1) {
        unlink(tmpname);
        close(fd);
    }
    fsfree(tmpname);
    return -1;
}

int service_convert(struct service *svc, const char *output)
{
    byte_array_t *ba;
    int len, res;

    verbose("generating init script for '%s' service", svc->name);
    ba = service_to_byte_array(svc, NULL);
    if (ba == NULL) {
        return EXIT_FAILURE;
    }
    len = byte_array_size(ba);
    if (output != NULL) {
        res = byte_array_to_file(ba, output, 0755);
    } else {
        res = printf("%s", (const char *)byte_array_data(ba));
    }
    destroy_byte_array(ba);
    if (res != len) {
        error("failed to write init script");
        return EXIT_FAILURE;
    }
    if (output != NULL) {
        info("init script saved to %s", output);
    }
    return EXIT_SUCCESS;
}

int service_show(struct service *svc, const char *output)
{
    byte_array_t *ba;
    int len, res;

    verbose("generating unit file for '%s' service", svc->name);
    ba = unit_to_byte_array(svc->u, NULL);
    if (ba == NULL) {
        return EXIT_FAILURE;
    }
    len = byte_array_size(ba);
    if (output != NULL) {
        res = byte_array_to_file(ba, output, 0644);
    } else {
        res = printf("%s", (const char *)byte_array_data(ba));
    }
    destroy_byte_array(ba);
    if (res != len) {
        error("failed to write unit file");
        return EXIT_FAILURE;
    }
    if (output != NULL) {
        info("unit file saved to %s", output);
    }
    return EXIT_SUCCESS;
}

static int service_start_prerequisites(list_t *req)
{
    struct service *svc;
    const char *name;
    list_elem_t *e;
    int ret = 0;

    for (e = list_get_first(req); e != NULL; e = list_next(e)) {
        name = list_elem_get_value(e);
        svc = service_find(name);
        if (svc == NULL) {
            if (errno == ENOENT) {
                error("service '%s' not found", name);
            }
            return -1;
        } else {
            if (service_start(svc) != EXIT_SUCCESS) {
                error("failed to start %s", svc->name);
                ret = -1;
            } else {
                info("started %s", svc->name);
            }
            service_free(svc);
        }
    }
    return ret;
}

int service_start(struct service *svc)
{
    struct command *cmd;
    monitor_state state;
    pid_t pid;

    state = monitor_control_get_state(svc);
    if (state == MS_STARTING || state == MS_RESTARTING) {
        state = monitor_control_wait(svc,
                                     60000 /* 60 s */,
                                     MS_RUNNING,
                                     MS_REMAINING,
                                     MS_STOPPED,
                                     0);
        if (state == MS_ERROR) {
            if (errno == ETIMEDOUT) {
                error("timed out waiting for service to start");
            } else {
                error("error while waiting for service to start: %m");
            }
            return EXIT_FAILURE;
        }
    }
    if (state == MS_RUNNING || state == MS_REMAINING) {
        info("service is already running");
        return EXIT_SUCCESS;
    }
    if (state != MS_ERROR && state != MS_STOPPED) {
        verbose("waiting for service to stop");
        state = monitor_control_wait(svc, 10000 /* 10 s */, MS_STOPPED, 0);
        if (state == MS_ERROR) {
            error("error while waiting for service to stop: %m");
            return EXIT_FAILURE;
        }
    }
    cmd = command_from_service(svc, "ExecStart");
    if (cmd == NULL) {
        if (errno == ENOENT) {
            error("ExecStart not found in unit");
        }
        return EXIT_FAILURE;
    }
    if (list_size(svc->required) > 0) {
        verbose("checking prerequisites");
        if (service_start_prerequisites(svc->required) != 0) {
            error("failed to start prerequisites");
            return EXIT_FAILURE;
        }
    }
    verbose("starting %s", svc->name);
    pid = command_monitor(cmd);
    command_free(cmd);
    debug("daemon started: %d", pid);
    if (pid < 0) {
        return -pid;
    }
    return EXIT_SUCCESS;
}

// Stops the service.
int service_stop(struct service *svc)
{
    struct command *cmd;
    pid_t pid, pgid;
    int res, version;
    monitor_state state;

    // First, check if it's running.
    state = monitor_control_get_state(svc);
    if (state == MS_STOPPED) {
        return 0;
    }
    verbose("stopping %s", svc->name);
    // Plan A: run the stop command
    cmd = command_from_service(svc, "ExecStop");
    if (cmd == NULL && errno != ENOENT) {
        return 1;
    }
    if (cmd != NULL) {
        res = command_run(cmd);
        if (res < 0) {
            // this is a systemd exit code indicating that an error
            // occurred before the command ran
            error("failed to run stop command: %d", -res);
        } else if (res > 0) {
            // this is the exit status as reported by waitpid()
            warning("stop command completed with exit status 0x%x", res);
        }
        command_free(cmd);
        state = monitor_control_wait(svc, 10000 /* 10 s */, MS_STOPPED, 0);
        if (state == MS_STOPPED) {
            return 0;
        }
        if (errno != ETIMEDOUT) {
            error("error while waiting for service to stop: %m");
            return 1;
        }
        warning("timed out waiting for service to stop");
        // fall through to plan B
    }
    // Plan B: give a stop order to the monitor and wait
    //
    // Versions prior to 20220303 have faulty process tracking and will kill too
    // many processes when given a stop command, so try to kill the monitor's
    // process group, then fall through to killing the PID referenced by the PID
    // file.  The latter usually only works for forking services, but
    // non-forking services should be in the same process group as the monitor
    // and will be caught by the kill().
    //
    // Note that if a non-forking service has KillMode `process` and no PID
    // file, we will kill the monitor but not the service.  There is no good
    // solution for this.
    if (monitor_control_identify(svc, &pid, &version) == 0
        && version < 20220303) {
        verbose("using alternate strategy for monitor %u version %u",
                (unsigned int)pid,
                version);
        switch (svc->kill_mode) {
            case KM_CGROUP:
            case KM_MIXED:
                if ((pgid = getpgid(pid)) > 0) {
                    verbose("killing process group %u", (unsigned int)pgid);
                    kill(-pgid, SIGTERM);
                    kill(-pgid, SIGCONT);
                }
                break;
            case KM_PROCESS:
            case KM_NONE:
                verbose("killing process %u", (unsigned int)pid);
                kill(pid, SIGTERM);
                kill(pid, SIGCONT);
                break;
            default:
                // can't happen but the compiler insists (because of KM_MAX)
                fatal("invalid kill mode %d", svc->kill_mode);
                break;
        }
        // getpgid() can only fail if the monitor died in the interim.
        state = MS_STOPPED; // we tried, at least
    } else {
        state = monitor_control_stop(svc, 10 * TS_SEC);
    }
    // Plan C: kill the process referenced by the PID file
    // We also get here if we already killed the service earlier, so we can
    // remove any stray PID files.
    if (unit_get_value(svc->u, "Service", "PIDFile") != NULL) {
        // Complicated case, kill the existing process.
        verbose("checking for PID file");
        cmd = command_from_service(svc, "ExecStop");
        if (cmd != NULL) {
            command_run(cmd);
        } else {
            // No stop command, but we still need a command because the PID file
            // is stored in the command rather than in the service.
            // XXX This should be rectified at some point.
            cmd = command_from_service(svc, "ExecStart");
        }
        if (cmd == NULL) {
            // This means there is no start or stop command...  how did we even
            // get into this situation?
            return 1;
        }
        switch (svc->kill_mode) {
            case KM_CGROUP:
            case KM_MIXED:
                res = command_killpg(cmd, SIGTERM);
                break;
            case KM_PROCESS:
                res = command_kill(cmd, SIGTERM);
                break;
            case KM_NONE:
                res = 0;
                break;
            default:
                // can't happen but the compiler insists (because of KM_MAX)
                fatal("invalid kill mode %d", svc->kill_mode);
                break;
        }
        // ENOENT: PID file is missing
        // ESRCH: PID not found, i.e. stale PID file
        if (res == 0 || errno == ENOENT || errno == ESRCH) {
            state = MS_STOPPED;
        }
        command_rmpid(cmd);
        command_free(cmd);
    }
    if (state == MS_STOPPED) {
        return 0;
    }
    // Plan D: there is no plan D
    return 1;
}

int service_reload(struct service *svc)
{
    struct command *cmd;
    int res;
    monitor_state state;

    // Figure out if we have a reload command at all.
    cmd = command_from_service(svc, "ExecReload");
    if (cmd == NULL) {
        // The traditional method is SIGHUP, but this will kill a process that
        // does not expect it.
        return 3;
    }
    // Check if the service is running.
    state = monitor_control_get_state(svc);
    verbose("service is %s", monitor_state_name(state));
    switch (state) {
        case MS_ERROR:
            return 1;
        case MS_STARTING:
        case MS_RESTARTING:
            // reloading is pointless
            return 0;
        case MS_RUNNING:
        case MS_REMAINING:
            break;
        default:
            error("service is not running");
            return 7;
    }
    // Execute the reload command.
    res = command_run(cmd);
    command_free(cmd);
    if (res == 0) {
        return 0;
    }
    // XXX do we have any additional detail?
    return 1;
}

// Restarts the service.
int service_restart(struct service *svc)
{
    int res;
    // Disable plan A for now as it won't pick up changes in the unit file.
#if 0
    monitor_state state;

    // Plan A: send a restart command.
    state = monitor_control_restart(svc, 10000 /* 10 s */);
    if (state == MS_RUNNING || state == MS_REMAINING) {
        return 0;
    }
#endif
    // Plan B: stop, then start the service.
    res = service_stop(svc);
    if (res != 0 && errno != ENOENT && errno != ESRCH) {
        return res;
    }
    return service_start(svc);
}

int service_status(struct service *svc)
{
    struct command *cmd;
    monitor_state state;
    int res;

    state = monitor_control_get_state(svc);
    if (state == MS_ERROR) {
        return 4;
    }
    verbose("service state: %s", monitor_state_name(state));
    switch (state) {
        case MS_RESTARTING:
        case MS_STARTING:
        case MS_RUNNING:
        case MS_REMAINING:
        case MS_STOPPING:
            // program is running or service is OK
            return 0;
        default:
            // program is not running
            break;
    }
    // Now check the PID file
    cmd = command_from_service(svc, "ExecStart");
    if (cmd == NULL) {
        return 4;
    }
    res = command_kill(cmd, 0);
    command_free(cmd);
    if (res == 0) {
        warning("service is running but monitor is not");
        return 0;
    }
    if (errno == ESRCH) {
        // not running and PID file exists
        return 1;
    }
    if (errno == ENOENT) {
        // not running and no PID file
        return 3;
    }
    // ¯\_(ツ)_/¯
    return 4;
}

int service_control(struct service *svc)
{
    struct text *request;
    char *response;

    while ((request = text_line_from_stream(stdin)) != NULL) {
        response = monitor_control(svc, request->beg);
        fsfree(request);
        if (response == NULL) {
            error("reqest failed: %m");
            fsfree(response);
            return EXIT_FAILURE;
        }
        printf("%s\n", response);
        fsfree(response);
    }
    return ferror(stdin) ? EXIT_FAILURE : EXIT_SUCCESS;
}
