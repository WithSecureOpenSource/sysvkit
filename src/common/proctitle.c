#include "proctitle.h"

#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *argv_start, *argv_end;
static bool argv_bug;

void setup_proctitle(int argc, char *argv[])
{
    ssize_t res;
    size_t len;
    char *save;
    int fd;

    // Determine location and length of argv space
    argv_start = argv[0];
    argv_end = strchr(argv[argc - 1], '\0') + 1;
    len = argv_end - argv_start;

    // Attempt to detect argv bug in Linux 4.18 - 5.2: after setting the argv
    // space to all-zeroes except for the final character, /proc/self/cmdline
    // should be either empty or contain a single NUL.  If not, the bug is
    // present and we need to work around it.
    save = malloc(len);
    memcpy(save, argv_start, len);
    memset(argv_start, 0, len - 1);
    argv_start[len - 1] = '#';
    fd = open("/proc/self/cmdline", O_RDONLY);
    if (fd > 0) {
        res = read(fd, argv_start, len);
        if (res > 1) {
            argv_bug = true;
        }
        close(fd);
    }
    memcpy(argv_start, save, len);
    free(save);
}

// Set the process title (reflected in /proc/*/cmdline or ps) to what it would
// be if it had been invoked with the given argv.
void set_argv(int argc, const char *argv[])
{
    const char *src;
    char *dst;
    int i;

    for (i = 0, dst = argv_start; i < argc && dst < argv_end - 1; i++) {
        if (i > 0) {
            *dst++ = ' ';
        }
        for (src = argv[i]; *src != '\0' && dst < argv_end - 2; src++) {
            *dst++ = *src;
        }
    }
    if (!argv_bug && dst < argv_end - 1) {
        argv_end[-1] = '#';
    } else {
        argv_end[-1] = '\0';
    }
    while (dst < argv_end - 1) {
        *dst++ = '\0';
    }
}
