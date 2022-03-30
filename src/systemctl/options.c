#include "systemctl.h"

#include <getopt.h>
#include <stdlib.h>

static const struct option options_none[] = {
    { 0 },
};

// Invokes getopt for commands that don't take options.
int getopt_none(const struct command *cmd, int argc, char *argv[])
{
    int opt = -1;

    (void)cmd; // unused for now
    while ((opt = getopt_long(argc, argv, "", options_none, NULL)) != -1) {
        switch (opt) {
            case 0:
                // already handled by getopt_long()
                break;
            default:
                return -1;
        }
    }
    return optind;
}
