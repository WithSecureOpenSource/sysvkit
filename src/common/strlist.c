#include "strlist.h"

#include <fsdyn/bytearray.h>
#include <fsdyn/charstr.h>
#include <fsdyn/fsalloc.h>
#include <fsdyn/hashtable.h>
#include <fsdyn/list.h>

#include <stdbool.h>
#include <string.h>

// Returns a copy of a list of strings in a form suitable for passing to
// execve(2) etc.  The vector and strings are allocated as a single unit and
// should be freed in a single fsfree() call.
char **strlist_to_vector(list_t *list)
{
    list_elem_t *e;
    void *buf;
    char **ptr;
    char *str;
    size_t psz, ssz;

    for (psz = ssz = 0, e = list_get_first(list); e != NULL; e = list_next(e)) {
        psz += sizeof(char *);
        ssz += strlen(list_elem_get_value(e)) + 1;
    }
    psz += sizeof(char *);
    ptr = buf = fscalloc(1, psz + ssz);
    str = (char *)buf + psz;
    for (e = list_get_first(list); e != NULL; e = list_next(e)) {
        *ptr++ = str;
        for (const char *p = list_elem_get_value(e); *p != '\0'; p++) {
            *str++ = *p;
        }
        *str++ = '\0';
    }
    *ptr++ = NULL;
    // assert((char *)ptr == (char *)buf + psz);
    // assert(str == (char *)ptr + ssz);
    return buf;
}

void strlist_free(list_t *list)
{
    if (list != NULL) {
        list_foreach(list, (void *)fsfree, NULL);
        destroy_list(list);
    }
}

// Convert a delimited string into a list of its elements.  The delimiter is ':'
// unless otherwise specified.  If blank is false, empty elements (whether at
// the beginning or the end or between consecutive delimiters) are ignored.  If
// dedup is true, only the first occurrence of repeated elements is kept.
list_t *strlist_from_delim(const char *str, char delim, bool blank, bool dedup)
{
    hash_table_t *h = NULL;
    list_t *l;
    const char *p, *q;
    char *s;

    delim = delim ? delim : ':';
    l = make_list();
    if (dedup) {
        h = make_hash_table(1024, (void *)hash_string, (void *)strcmp);
    }
    p = q = str;
    do {
        if (*q == '\0' || *q == delim) {
            if (blank || q > p) {
                s = charstr_dupsubstr(p, q);
                if (h != NULL) {
                    if (hash_table_get(h, s) != NULL) {
                        fsfree(s);
                        continue;
                    }
                    hash_table_put(h, s, s);
                }
                list_append(l, s);
            }
            p = q + 1;
        }
    } while (*q++ != '\0');
    if (h != NULL) {
        destroy_hash_table(h);
    }
    return l;
}

// Converts a list of strings into a delimited string.  The delimiter is ':'
// unless otherwise specified.  If dedup is true, only the first occurrence of
// repeated elements is kept.
char *strlist_to_delim(list_t *l, char delim, bool dedup)
{
    byte_array_t *ba;
    hash_table_t *h = NULL;
    list_elem_t *e;
    const char *s;
    char *str;

    delim = delim ? delim : ':';
    ba = make_byte_array(SIZE_MAX);
    if (dedup) {
        h = make_hash_table(1024, (void *)hash_string, (void *)strcmp);
    }
    for (e = list_get_first(l); e != NULL; e = list_next(e)) {
        s = list_elem_get_value(e);
        if (h != NULL) {
            if (hash_table_get(h, s) != NULL) {
                continue;
            }
            hash_table_put(h, s, s);
        }
        byte_array_appendf(ba, "%s%c", s, delim);
    }
    if (byte_array_size(ba) > 0) {
        byte_array_resize(ba, byte_array_size(ba) - 1, 0);
    }
    if (h != NULL) {
        destroy_hash_table(h);
    }
    str = charstr_dupstr(byte_array_data(ba));
    destroy_byte_array(ba);
    return str;
}
