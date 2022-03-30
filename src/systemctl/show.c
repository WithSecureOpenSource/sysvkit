#include "systemctl.h"

#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void usage(const struct command *cmd)
{
    printf("systemctl [options] %s service [...]\n", cmd->name);
}

// Displays a service's unit file.  Assumes that the service in question is a
// systemd service converted using sysvkit.
int show_main(const struct command *cmd, int argc, char *argv[])
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
        res = service_invoke(svc, "show", false);
        if (res != 0) {
            ret = res;
        }
        service_free(svc);
    }
    return ret;
}

const struct command cmd_show = { "show", show_main };
