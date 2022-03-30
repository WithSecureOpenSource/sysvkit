#include "unit.h"

#include "common.h"
#include "pair.h"
#include "strbool.h"

#include <fsdyn/bytearray.h>
#include <fsdyn/charstr.h>
#include <fsdyn/hashtable.h>

#include <errno.h>
#include <string.h>

struct section {
    char *name;
    struct unit *parent;
    hash_table_t *pairs;
};

struct section *section_create(struct unit *u, const char *name)
{
    struct section *s;

    s = fsalloc(sizeof(*s));
    s->name = charstr_dupstr(name);
    s->parent = u;
    s->pairs = make_hash_table(256, (void *)hash_string, (void *)strcmp);
    return s;
}

void section_free(struct section *s)
{
    hash_elem_t *e;

    if (s != NULL) {
        while ((e = hash_table_pop_any(s->pairs)) != NULL) {
            pair_free(DQ(hash_elem_get_value(e)));
            destroy_hash_element(e);
        }
        destroy_hash_table(s->pairs);
        fsfree(s->name);
        fsfree(s);
    }
}

struct unit {
    char *name;
    hash_table_t *sections;
};

struct unit *unit_create(const char *name)
{
    struct unit *u;

    u = fsalloc(sizeof(*u));
    u->name = charstr_dupstr(name);
    u->sections = make_hash_table(16, (void *)hash_string, (void *)strcmp);
    return u;
}

void unit_free(struct unit *u)
{
    hash_elem_t *e;

    if (u != NULL) {
        while ((e = hash_table_pop_any(u->sections)) != NULL) {
            section_free(DQ(hash_elem_get_value(e)));
            destroy_hash_element(e);
        }
        destroy_hash_table(u->sections);
        fsfree(u->name);
        fsfree(u);
    }
}

// Sets or updates the given key in the given section to the given value.  If
// append is true and the key already exists, the new value will be appended to
// the existing one, with an intervening space, instead of replacing it.  As a
// special case, if the value is NULL, the key is deleted.  Returns non-zero if
// the key already existed.
int unit_update_value(struct unit *u,
                      const char *section,
                      const char *key,
                      const char *value,
                      bool append)
{
    hash_elem_t *e;
    struct section *s;
    struct pair *o = NULL, *p = NULL;
    char *buf;

    if ((e = hash_table_get(u->sections, section)) == NULL) {
        // Section does not yet exist
        s = section_create(u, section);
        // always successful since the section does not yet exist
        (void)hash_table_put(u->sections, s->name, s);
    } else {
        s = DQ(hash_elem_get_value(e));
    }
    if ((e = hash_table_get(s->pairs, key)) != NULL) {
        o = DQ(hash_elem_get_value(e));
    }
    if (value == NULL) {
        // nothing
    } else if (append && e != NULL) {
        buf = charstr_printf("%s %s", o->value, value);
        p = pair_create(key, buf);
        fsfree(buf);
        hash_table_put(s->pairs, p->key, p);
    } else {
        p = pair_create(key, value);
        hash_table_put(s->pairs, p->key, p);
    }
    if (e != NULL) {
        pair_free(o);
        destroy_hash_element(e);
        return 1;
    }
    return 0;
}

// Sets or replaces the given key in the given section.
int unit_set_value(struct unit *u,
                   const char *section,
                   const char *key,
                   const char *value)
{
    return unit_update_value(u, section, key, value, false);
}

// Sets or appends to the given key in the given section.
int unit_append_value(struct unit *u,
                      const char *section,
                      const char *key,
                      const char *value)
{
    return unit_update_value(u, section, key, value, true);
}

// Deletes the given key from the given section.
int unit_delete_key(struct unit *u, const char *section, const char *key)
{
    return unit_update_value(u, section, key, NULL, false);
}

// Returns the value of the given key in the given section.
const char *unit_get_value(struct unit *u, const char *section, const char *key)
{
    hash_elem_t *e;
    const struct section *s;
    const struct pair *p;

    if ((e = hash_table_get(u->sections, section)) == NULL) {
        errno = ENOENT;
        return NULL;
    }
    s = hash_elem_get_value(e);
    if ((e = hash_table_get(s->pairs, key)) == NULL) {
        errno = ENOENT;
        return NULL;
    }
    p = hash_elem_get_value(e);
    return p->value;
}

int unit_get_bool(struct unit *u, const char *section, const char *key)
{
    return strbool(unit_get_value(u, section, key));
}

byte_array_t *unit_to_byte_array(struct unit *u, byte_array_t *ba)
{
    byte_array_t *nba = NULL;
    hash_elem_t *se, *pe;
    const struct section *s;
    const struct pair *p;

#define appendf(...)                                \
    do {                                            \
        if (!byte_array_appendf(ba, __VA_ARGS__)) { \
            goto fail;                              \
        }                                           \
    } while (0)

    if (ba == NULL) {
        ba = nba = make_byte_array(SIZE_MAX);
    }
    for (se = hash_table_get_any(u->sections); se != NULL;
         se = hash_table_get_other(se)) {
        s = hash_elem_get_value(se);
        appendf("[%s]\n", s->name);
        for (pe = hash_table_get_any(s->pairs); pe != NULL;
             pe = hash_table_get_other(pe)) {
            p = hash_elem_get_value(pe);
            appendf("%s=%s\n", p->key, p->value);
        }
    }
    return ba;
fail:
    if (nba != NULL) {
        destroy_byte_array(nba);
    }
    return NULL;
}

char *unit_to_string(struct unit *u)
{
    byte_array_t *ba;
    char *str = NULL;

    ba = make_byte_array(SIZE_MAX);
    if (!unit_to_byte_array(u, ba)) {
        goto end;
    }
    str = charstr_dupstr(byte_array_data(ba));
end:
    destroy_byte_array(ba);
    return str;
}
