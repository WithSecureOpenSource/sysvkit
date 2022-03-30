#include "systemctl.h"

#include "exitcode.h"

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Root directory
const char *root = "";

static void version(void)
{
    printf("systemctl 1812 (f-secure)\n");
}

static void usage(void)
{
    printf("systemctl [options] command [...]\n");
}

// A subset of the real systemctl's global options
static const struct option options[] = {
    { "debug", no_argument, 0, 'd' },
    { "help", no_argument, 0, 'h' },
    { "root", required_argument, 0, 'r' },
    { "quiet", no_argument, &noisy, QUIET },
    { "verbose", no_argument, &noisy, VERBOSE },
    { "version", no_argument, 0, 'V' },
    { 0 },
};

// A subset of the real systemctl's commands
static const struct command *commands[] = {
    &cmd_enable,
    &cmd_disable,
    &cmd_status,
    &cmd_is_enabled,
    &cmd_is_active,
    &cmd_start,
    &cmd_stop,
    &cmd_restart,
    &cmd_try_restart,
    &cmd_reload,
    &cmd_reload_or_restart,
    &cmd_reload_or_try_restart,
    &cmd_try_reload_or_restart,
    &cmd_daemon_reload,
    &cmd_show,
    NULL,
};

int main(int argc, char *argv[])
{
    int opt = -1;

    while ((opt = getopt_long(argc, argv, "dhqv", options, NULL)) != -1) {
        switch (opt) {
            case 0:
                // already handled by getopt_long()
                break;
            case 'h':
                usage();
                return EXIT_SUCCESS;
            case 'r':
                root = optarg;
                break;
            case 'V':
                version();
                return EXIT_SUCCESS;
            case 'd':
            case 'q':
            case 'v':
                noise_set_level(opt);
                break;
            default:
                usage();
                return EX_USAGE;
        }
    }
    argc -= optind;
    argv += optind;
    if (argc == 0) {
        usage();
        return EX_USAGE;
    }
    optind = 1; // reset getopt() / getopt_long()
    if (noise_override(NULL)) {
        error("invalid noise level %s=%s", NOISE_ENVVAR, getenv(NOISE_ENVVAR));
        return EX_USAGE;
    }
    for (const struct command **cmd = commands; *cmd != NULL; cmd++) {
        if (strcmp((*cmd)->name, argv[0]) == 0) {
            return (*cmd)->main(*cmd, argc, argv);
        }
    }
    fprintf(stderr, "unrecognized command '%s'\n", argv[0]);
    return EXIT_FAILURE;
}
