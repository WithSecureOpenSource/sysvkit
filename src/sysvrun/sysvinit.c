#include "sysvinit.h"

#include "service.h"
#include "sysvrun.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

// Parse an init script which is expected to contain an embedded unit file.
// Includes a quick check of the LSB comment block to confirm that we have the
// correct script.  This will not work if the script provides multiple
// facilities, but we'll assume it doesn't.
struct service *sysvinit_parse_init_script(const char *name,
                                           const struct text *txt)
{
    struct service *svc;
    struct text *line, *embed;
    const char *p, *q, *s;

    verbose("parsing init script for '%s' service", name);

    // Find Provides line within LSB comment block
    if ((line = text_first_line_equals(txt, LSB_BEGIN_INIT_INFO, 0)) == NULL) {
        verbose("failed to find start of LSB info block");
        errno = EINVAL;
        return NULL;
    }
    if ((line = text_next_line_prefix(line, LSB_PROVIDES, 0)) == NULL) {
        verbose("failed to find Provides line");
        errno = EINVAL;
        return NULL;
    }

    // Skip to the first word in the facility list.
    for (p = line->beg + strlen(LSB_PROVIDES); p < line->end && isblank(*p);
         p++) {
        // nothing
    }
    // Compare that with our name
    for (q = p, s = name; q < line->end && *q == *s; q++, s++) {
        // nothing
    }
    // Did we match the entire name and reach a blank or EOL?
    if (*s != '\0' || !(q == line->end || isblank(*q))) {
        verbose("service name mismatch");
        errno = ENOENT;
        return NULL;
    }

    // Look for the end of the LSB comment block.
    if ((line = text_next_line_equals(line, LSB_END_INIT_INFO, 0)) == NULL) {
        verbose("failed to find end of LSB info block");
        errno = EINVAL;
        return NULL;
    }

    // Now look for the embedded unit file.
    if ((line = text_next_line_equals(line, BEGIN_EMBED, 0)) == NULL) {
        verbose("failed to find start of embedded unit file");
        errno = ENOENT;
        return NULL;
    }
    p = line->end + 1; // p now points to start of unit file
    if ((line = text_next_line_equals(line, END_EMBED, 0)) == NULL) {
        verbose("failed to find end of embedded unit file");
        errno = EINVAL;
        return NULL;
    }
    q = line->beg; // q now points to end of unit file
    text_free(line);

    // Narrow to just the embedded unit file and parse it
    embed = text_narrow(txt, p, q - p);
    svc = service_from_unit_file(name, embed);
    text_free(embed);
    return svc;
}

// Locate a service by its name.
static const char *sysvinit_script_path[] = {
    "/etc/init.d",
    ".",
    NULL,
};
struct service *sysvinit_find_service(const char *name)
{
    char path[1024];
    struct stat sb;
    const char **dir;
    int res;

    for (dir = sysvinit_script_path; *dir != NULL; dir++) {
        res = snprintf(path, sizeof(path), "%s%s/%s", root, *dir, name);
        if (res < 0) {
            return NULL;
        }
        if ((size_t)res >= sizeof(path)) {
            errno = EOVERFLOW;
            return NULL;
        }
        debug("looking for %s in %s", name, path);
        if (stat(path, &sb) == 0) {
            return service_from_file(name, path);
        }
        if (errno != ENOENT && errno != ENOTDIR) {
            return NULL;
        }
    }
    errno = ENOENT;
    return NULL;
}

char *sysvinit_create_init_script(struct service *svc)
{
    (void)svc;
    return NULL;
}
