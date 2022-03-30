#include "systemd.h"

#include "noise.h"
#include "service.h"
#include "sysvrun.h"
#include "unit.h"

#include <fsdyn/bytearray.h>
#include <fsdyn/charstr.h>
#include <fsdyn/fsalloc.h>
#include <fsdyn/hashtable.h>

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

// Strips the ".service" suffix from a service name.  Returns false if the
// suffix was not present.
bool deservicify(char *str)
{
    size_t len = strlen(str);
    if (charstr_ends_with(str, DOT_SERVICE)) {
        str[len - strlen(DOT_SERVICE)] = '\0';
        return true;
    }
    return false;
}

// Read and interpret systemd unit files in general, and service files in
// particular.
//
// The general syntax is described in systemd.syntax(7) and is, to quote Douglas
// Adams,“almost, but not quite, entirely unlike tea”.  The documentation is
// scattered and vague and frequently contradicts the external sources that it
// references.  Here is a summary:
//
// * If a line begins with '#' or ';', it is considered a comment and discarded.
//   A '#' or ';' encountered anywhere else is preserved as-is.
// * If a line ends with '\', the backslash is replaced by a single space and
//   the next non-comment line is appended to it.  A '\' encounted anywhere else
//   is preserved as-is.
// * One presumes that trailing whitespace is discarded, but the documentation
//   does not say, so it is entirely possible that it is not.
// * Blank lines are discarded.
// * A line is either a section header or a key-value pair.
// * A section header consists of a section name surrounded by '[' and ']'.
// * The systemd documentation does not specify the syntax of a section name.
//   We fall back on the XDG DES whch specifies “all ASCII characters except for
//   [ and ] and control characters”.
// * A key-value pair consists of a key and a value separated by a '='
//   character.  Any amount of whitespace before and after the '=' character is
//   ignored; all other whitespace is preserved.
// * The systemd documentation does not specify the syntax of a key name.  We
//   fall back on the XDG DES which specifies “the characters A-Za-z0-9-”.
// * As a general rule, each section can only contain a single value for a
//   particular key, but there are exceptions.
// * Multiple values for the same key are permitted.
// * Empty values are permitted.  An empty value for an existing key erases the
//   previous value.
// * Quoting and substitution are not handled by the parser.  Thus, quotes, '$',
//   and '%' have no special meaning to the parser.
// * Whitespace is preserved exactly as-is, except before and after the '=' in a
//   key-value pair.
//
// References:
// * systemd man pages: https://www.freedesktop.org/software/systemd/man/
//   - systemd.syntax(7): general configuration file syntax
//   - systemd.unit(7): unit files
//   - systemd.service(7): service files
//   - systemd.exec(7): execution environment
// * XDG Desktop Entry Specification:
//   https://specifications.freedesktop.org/desktop-entry-spec/latest/

// Note: we have no way of verifying the service name, as it is intentionally
// not included in the unit file.  This allows the same unit file to be used for
// multiple services, relying on symlinks and specifiers to differentiate them
// (e.g. `ExecStart=/usr/sbin/%p --config /etc/%p/%i.conf`).

// Define to 1 to normalize whitespace in values: tabs are replaced with spaces,
// multiple consecutive spaces are collapsed into one, and trailing space is
// removed.
#define NORMALIZE_WHITESPACE 1

// Standard C escapes
static const char escape[256] = {
    ['a'] = '\a',  ['b'] = '\b', ['f'] = '\f',  ['n'] = '\n',
    ['r'] = '\r',  ['s'] = ' ',  ['t'] = '\t',  ['v'] = '\v',
    ['\\'] = '\\', ['"'] = '"',  ['\''] = '\'',
};

// Splits the given string in accordance with systemd's quoting rules and
// returns the result as a list of strings.  Words longer than a certain
// stupidly big limit may be silently truncated.
//
// Known deviations from the spec:
// * Numeric character escapes (\xxx, \nnn, \unnnn, \Unnnnnnnn) are currently
//   not supported.
// * Quotes are allowed within words, not just at their boundaries.
// * No substitutions are performed.
// * Specifiers are not supported.
list_t *systemd_split_quoted(const char *string)
{
    list_t *list;
    byte_array_t *word;
    const char *p, *str;
    size_t len;
    char ch, q;

    list = make_list();
    word = NULL;
    p = string;
    q = '\0';
    word = make_byte_array(SIZE_MAX);
    for (;;) {
        while (isblank(*p)) {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        byte_array_clear(word);
        len = 0;
        while ((ch = *p) != '\0') {
            p++;
            if (ch == '"' || ch == '\'') {
                if (q && ch != q) {
                    // this is fine
                } else if (q == '\0') {
                    if (len != 0) {
                        // The spec does not allow this, but I do not know for
                        // sure how systemd itself reacts.  We will treat the
                        // same way the shell would, i.e. a"b"c == abc.
                        verbose("opening quote not at start of word");
                    }
                    q = ch;
                    continue;
                } else {
                    if (*p != '\0' && !isblank(*p)) {
                        // See above.
                        verbose("closing quote not at end of word");
                    }
                    q = '\0';
                    continue;
                }
            }
            if (ch == '\\') {
                // Escape sequence
                if (*p == '\0') {
                    // shouldn't happen (but I suppose it can)
                    break;
                }
                if ((ch = escape[(unsigned char)*p]) == 0) {
                    warning("invalid escape: '\\%c'", *p);
                    ch = *p;
                }
            }
            if (isblank(ch) && !q) {
                // End of word
                break;
            }
            byte_array_append(word, &ch, 1);
            len++;
        }
        str = byte_array_data(word);
        list_append(list, charstr_dupsubstr(str, str + len));
    }
    destroy_byte_array(word);
    return list;
}

static inline int issectionname(int ch)
{
    return isprint(ch) && ch != '[' && ch != ']';
}

static inline int iskey(int ch)
{
    return isdigit(ch) || isalpha(ch) || ch == '-';
}

static inline int isvalue(int ch)
{
    return isprint(ch) || ch == '\t';
}

// Parse a systemd unit file.
struct unit *systemd_parse_unit_file(const char *name, const struct text *txt)
{
    char section[256] = "", key[256] = "", value[1024] = "";
    const char *cur = txt->beg, *end = txt->beg + txt->len;
    const char *p, *q, *r;
    struct unit *u;
    unsigned int s, k, v;
    int lno;
    char ch;

    verbose("parsing unit file for '%s' service", name);

    u = unit_create(name);
    for (lno = 1; cur < end; lno++) {
        if (*cur == '\0') {
            // Premature end of input
            goto eof;
        } else if (*cur == '\n') {
            // Blank line
            cur++;
        } else if (*cur == '#' || *cur == ';') {
            // Comment line
            for (p = cur; p < end && *p != '\0' && *p != '\n'; p++) {
                // nothing
            }
            if (p >= end || *p == '\0') {
                goto eof;
            }
            cur = p + 1;
        } else if (*cur == '[') {
            // Section header
            p = cur + 1;
            for (s = 0, q = p; q < end && issectionname(*q); q++) {
                if (s < sizeof(section)) {
                    section[s++] = *q;
                }
            }
            if (q >= end || *q == '\0') {
                goto eof;
            }
            if (s == 0) {
                error("expected section name");
                goto error;
            }
            if (*q != ']') {
                error("expected ']'");
                goto error;
            }
            r = q + 1;
            if (r >= end || *r == '\0') {
                goto eof;
            }
            if (*r != '\n') {
                error("expected end of line");
                goto error;
            }
            r++;
            // At this point:
            // - cur points to the start of the line
            // - p points to the start of the section name
            // - q points to the closing bracket
            // - r points to the start of the next line
            // - section contains as much of the section name as will fit
            // - s is the length of the section name in the input
            if (s >= sizeof(section) - 1) {
                error("section name too long");
                goto error;
            }
            section[s] = '\0';
            cur = r;
            continue;
        } else {
            // Key-value pair
            for (k = 0, p = cur; p < end && iskey(*p); p++) {
                if (k < sizeof(key)) {
                    key[k] = *p;
                }
                k++;
            }
            if (p >= end || *p == '\0') {
                goto eof;
            }
            if (k == 0) {
                error("expected key");
                goto error;
            }
            for (q = p; q < end && isblank(*q); q++) {
                // nothing
            }
            if (q >= end || *q == '\0') {
                goto eof;
            }
            if (*q != '=') {
                error("expected '='");
                goto error;
            }
            for (q++; q < end && isblank(*q); q++) {
                // nothing
            }
            if (q >= end || *q == '\0') {
                goto eof;
            }
            for (v = 0, r = q; r < end && isvalue(*r); r++) {
                ch = *r;
                if (ch == '\\' && r + 1 < end && r[1] == '\n') {
                    // Line continuation
                    ch = ' ';
                    r++; // now at end of line
                    lno++;
                    // Skip comment lines
                    while (r + 1 < end && (r[1] == '#' || r[1] == ';')) {
                        do {
                            r++;
                        } while (r < end && *r != '\0' && *r != '\n');
                        // now at end of line
                        lno++;
                    }
                }
#if NORMALIZE_WHITESPACE
                // Replace tabs with spaces
                if (ch == '\t') {
                    ch = ' ';
                }
                // Collapse consecutive spaces into one
                if (ch == ' ' && v > 0 && v < sizeof(value)
                    && value[v - 1] == ' ') {
                    continue;
                }
#endif
                if (v < sizeof(value)) {
                    value[v] = ch;
                }
                v++;
            }
#if NORMALIZE_WHITESPACE
            // Remove trailing space
            if (v > 0 && v < sizeof(value) && value[v - 1] == ' ') {
                v--;
            }
#endif
            if (r >= end || *r == '\0') {
                goto eof;
            }
            // At this point:
            // - cur points to the start of the line and the key
            // - p points to the first character after the key
            // - key contains as much of the key as will fit
            // - k is the length of the key in the input
            // - q points to the first character of the value
            // - r points to the end of the line
            // - value contains as much of the value as will fit
            // - v is the length of the value in the input
            //   (adjusted for line continuations)
            if (k >= sizeof(key) - 1) {
                error("key too long");
                goto error;
            }
            key[k] = '\0';
            if (v >= sizeof(value) - 1) {
                error("value too long");
                goto error;
            }
            value[v] = '\0';
            if (section[0] == '\0') {
                error("key-value pair before first section");
                goto error;
            }
            // append unless the value is empty
            unit_update_value(u, section, key, value, v > 0);
            cur = r + 1;
        }
    }
    return u;
eof:
    error("unexpected end of unit file");
error:
    error("error in unit file line %d", lno);
    if (cur < end) {
        for (p = cur; p < end && *p != '\0' && *p != '\n'; p++) {
            // nothing
        }
        if (p - cur > 64) {
            verbose("\t%.*s...", 64, cur);
        } else {
            verbose("\t%.*s", (int)(p - cur), cur);
        }
    }
    unit_free(u);
    return NULL;
}

// Locate a service by its name.  There are many, many places it could be,
// so we will only check the most likely.
static const char *systemd_unit_path[] = {
    "/etc/systemd/system",
    "/run/systemd/system",
    "/usr/lib/systemd/system",
    ".",
    NULL,
};
struct service *systemd_find_service(const char *name)
{
    char path[1024];
    struct stat sb;
    const char **dir;
    const char *suffix;
    int res;

    if (charstr_ends_with(name, DOT_SERVICE)) {
        suffix = "";
    } else {
        suffix = DOT_SERVICE;
    }
    for (dir = systemd_unit_path; *dir != NULL; dir++) {
        res =
            snprintf(path, sizeof(path), "%s%s/%s%s", root, *dir, name, suffix);
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
