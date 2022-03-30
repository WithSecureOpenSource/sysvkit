#include "strbool.h"

#include <string.h>

// Returns a positive value if str can be interpreted as true (“1”, “yes”,
// “true”, “on”), zero if it can be interpreted as false (“0”, “no”, “false”,
// “off”), and -1 otherwise.
int strbool(const char *str)
{
    if (str == NULL) {
        return -1;
    }
    if (strcmp(str, "1") == 0 || strcasecmp(str, "yes") == 0
        || strcasecmp(str, "true") == 0 || strcasecmp(str, "on") == 0) {
        return 1;
    }
    if (strcmp(str, "0") == 0 || strcmp(str, "no") == 0
        || strcasecmp(str, "false") == 0 || strcasecmp(str, "off") == 0) {
        return 0;
    }
    return -1;
}
