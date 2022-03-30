#include "systemctl.h"

#include <stdio.h>
#include <stdlib.h>

static void usage(const struct command *cmd)
{
    printf("systemctl [options] %s\n", cmd->name);
}

int noop_main(const struct command *cmd, int argc, char *argv[])
{
    int res;

    res = getopt_none(cmd, argc, argv);
    if (res < 0) {
        usage(cmd);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

const struct command cmd_daemon_reload = { "daemon-reload", noop_main };
