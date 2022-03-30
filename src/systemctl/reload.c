#include "systemctl.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void usage(const struct command *cmd)
{
    printf("systemctl [options] %s service [...]\n", cmd->name);
}

static int reload(const struct command *cmd, struct service *svc)
{
    int res;

    if (cmd == &cmd_reload_or_try_restart) {
        cmd = &cmd_try_reload_or_restart;
    }
    if (cmd == &cmd_try_reload_or_restart) {
        res = service_invoke(svc, "status", true);
        if (res < 0) {
            fprintf(stderr, "%s: %s: %m\n", cmd->name, svc->name);
            return EXIT_FAILURE;
        }
        // The service is not running, do nothing.
        if (res > 0) {
            return EXIT_SUCCESS;
        }
        // The service is running, perform a reload-or-restart.
        cmd = &cmd_reload_or_restart;
    }
    if (cmd == &cmd_reload || cmd == &cmd_reload_or_restart) {
        res = service_invoke(svc, "reload", true);
        if (res < 0) {
            fprintf(stderr, "%s: %s: %m\n", cmd->name, svc->name);
            return EXIT_FAILURE;
        }
        if (res == 0) {
            return EXIT_SUCCESS;
        }
    }
    if (cmd == &cmd_reload_or_restart) {
        // XXX it would be better to call across to our existing restart command
        cmd = &cmd_restart;
        res = service_invoke(svc, "restart", true);
        if (res < 0) {
            fprintf(stderr, "%s: %s: %m\n", cmd->name, svc->name);
            return EXIT_FAILURE;
        }
    }
    // Pass on the init script's exit status.
    return res;
}

// Reloads a service.
int reload_main(const struct command *cmd, int argc, char *argv[])
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

    ret = 0; // assume success, fail if any service fails
    for (i = 0; i < argc; i++) {
        if ((svc = service_find(argv[i])) == NULL) {
            fprintf(stderr, "service '%s' not found: %m\n", argv[i]);
            return EXIT_FAILURE;
        }
        res = reload(cmd, svc);
        if (res != 0) {
            ret = res;
        }
        service_free(svc);
    }
    return ret;
}

const struct command cmd_reload = { "reload", reload_main };
const struct command cmd_reload_or_restart = { "reload-or-restart",
                                               reload_main };
const struct command cmd_reload_or_try_restart = { "reload-or-try-restart",
                                                   reload_main };
const struct command cmd_try_reload_or_restart = { "try-reload-or-restart",
                                                   reload_main };
