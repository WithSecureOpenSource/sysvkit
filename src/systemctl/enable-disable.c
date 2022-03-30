#include "systemctl.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void usage(const struct command *cmd)
{
    printf("systemctl [options] %s service [...]\n", cmd->name);
}

static int enable_disable(const struct command *cmd, struct service *svc)
{
    int res;

    if (cmd == &cmd_enable) {
        res = service_enable(svc);
        if (res < 0) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    if (cmd == &cmd_disable) {
        res = service_disable(svc);
        if (res < 0) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }
    // can't happen [tm]
    return EXIT_FAILURE;
}

// Enables or disables a service, or reports whether it is enabled.
int enable_disable_main(const struct command *cmd, int argc, char *argv[])
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
        res = enable_disable(cmd, svc);
        if (res != 0) {
            ret = res;
        }
        service_free(svc);
    }
    return ret;
}

const struct command cmd_enable = { "enable", enable_disable_main };
const struct command cmd_disable = { "disable", enable_disable_main };
