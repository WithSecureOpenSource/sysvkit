#include "systemctl.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void usage(const struct command *cmd)
{
    printf("systemctl [options] %s service [...]\n", cmd->name);
}

static int start_stop(const struct command *cmd, struct service *svc)
{
    bool running = false;
    int res = EXIT_FAILURE;

    if (cmd == &cmd_start || cmd == &cmd_stop || cmd == &cmd_try_restart) {
        res = service_invoke(svc, "status", true);
        if (res < 0) {
            fprintf(stderr, "%s: %s: %m\n", cmd->name, svc->name);
            return EXIT_FAILURE;
        }
        if (res == 0) {
            running = true;
        }
    }
    if (cmd == &cmd_start) {
        if (running) {
            // The service is already running, do nothing.
            return EXIT_SUCCESS;
        }
    }
    if (cmd == &cmd_stop) {
        if (!running) {
            // The service is not running, do nothing.
            return EXIT_SUCCESS;
        }
    }
    if (cmd == &cmd_try_restart) {
        if (!running) {
            // The service is not running, do nothing.
            return EXIT_SUCCESS;
        }
        // The service is running, perform a restart.
        cmd = &cmd_restart;
    }
    res = service_invoke(svc, cmd->name, true);
    if (res < 0) {
        fprintf(stderr, "%s: %s: %m\n", cmd->name, svc->name);
        return EXIT_FAILURE;
    }
    // Pass on the init script's exit status.
    return res;
}

// Starts or stops a service.
int start_stop_main(const struct command *cmd, int argc, char *argv[])
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
        res = start_stop(cmd, svc);
        if (res != 0) {
            ret = res;
        }
        service_free(svc);
    }
    return ret;
}

const struct command cmd_start = { "start", start_stop_main };
const struct command cmd_stop = { "stop", start_stop_main };
const struct command cmd_restart = { "restart", start_stop_main };
const struct command cmd_try_restart = { "try-restart", start_stop_main };
