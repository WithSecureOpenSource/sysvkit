#include "systemctl.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void usage(const struct command *cmd)
{
    printf("systemctl [options] %s service [...]\n", cmd->name);
}

static int status(const struct command *cmd, struct service *svc)
{
    bool enabled = false, running = false;
    int res;

    if (cmd == &cmd_status || cmd == &cmd_is_enabled) {
        res = service_is_enabled(svc);
        if (res < 0) {
            fprintf(stderr, "%s: %s: %m\n", cmd->name, svc->name);
            return EXIT_FAILURE;
        }
        if (res > 0) {
            enabled = true;
        }
    }
    if (cmd == &cmd_status || cmd == &cmd_is_active) {
        // This assumes that the init script returns a non-zero exit status
        // when the service is not running.  Unfortunately, this is not
        // universally true.  We'll just have to trust that the ones we care
        // about (i.e. the ones we wrote and installed ourselves) do.
        res = service_invoke(svc, "status", true);
        if (res < 0) {
            fprintf(stderr, "%s: %s: %m\n", "status", svc->name);
            return EXIT_FAILURE;
        }
        if (res == 0) {
            running = true;
        }
    }
    if (cmd == &cmd_status) {
        if (noisy > QUIET) {
            printf("%s is %s and %s\n",
                   svc->name,
                   enabled ? "enabled" : "disabled",
                   running ? "active" : "inactive");
        }
        // Try to conform to the Linux Standard Base.  Since it doesn't really
        // have a concept of enabling or disabling services, use 4 (status
        // unknown) when the service is neither enabled nor running.
        if (running) {
            return 0; // program is running or service is OK
        }
        if (enabled) {
            return 3; // program is not running
        }
        return 4; // program or service status is unkown
    }
    if (cmd == &cmd_is_enabled) {
        if (noisy > QUIET) {
            printf("%s\n", enabled ? "enabled" : "disabled");
        }
        return enabled ? 0 : 1;
    }
    if (cmd == &cmd_is_active) {
        if (noisy > QUIET) {
            printf("%s\n", running ? "active" : "inactive");
        }
        return running ? 0 : 3;
    }
    // can't happen [tm]
    return EXIT_FAILURE;
}

// Reports whether a service is enabled and running.
int status_main(const struct command *cmd, int argc, char *argv[])
{
    struct service *svc;
    int i, res, ret;

    res = getopt_none(cmd, argc, argv);
    if (res < 0 || res == argc) {
        usage(cmd);
        return EXIT_FAILURE;
    }
    argc -= res;
    argv += res;

    if (cmd == &cmd_status) {
        ret = 0; // assume success, fail if any service fails
    } else {     // is-enabled, is-active
        ret = 3; // assume failure, succeed if any service succeeds
    }
    for (i = 0; i < argc; i++) {
        if ((svc = service_find(argv[i])) == NULL) {
            fprintf(stderr, "service '%s' not found: %m\n", argv[i]);
            return EXIT_FAILURE;
        }
        res = status(cmd, svc);
        if (cmd == &cmd_status) {
            if (res != 0) {
                ret = res;
            }
        } else {
            if (res == 0) {
                ret = 0;
            }
        }
        service_free(svc);
    }
    return ret;
}

const struct command cmd_status = { "status", status_main };
const struct command cmd_is_enabled = { "is-enabled", status_main };
const struct command cmd_is_active = { "is-active", status_main };
