#include "environment.h"

#include "common.h"

#include <fsdyn/bytearray.h>
#include <fsdyn/charstr.h>
#include <fsdyn/fsalloc.h>
#include <fsdyn/hashtable.h>
#include <fsdyn/list.h>

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct environment {
    hash_table_t *variables;
};

struct environment *environment_create(void)
{
    struct environment *env;

    env = fscalloc(1, sizeof(*env));
    env->variables = make_hash_table(256, (void *)hash_string, (void *)strcmp);
    return env;
}

struct environment *environment_clone(struct environment *other)
{
    struct environment *env;

    env = environment_create();
    environment_merge(env, other, true);
    return env;
}

void environment_free(struct environment *env)
{
    hash_elem_t *e;

    if (env != NULL) {
        while ((e = hash_table_pop_any(env->variables)) != NULL) {
            pair_free(DQ(hash_elem_get_value(e)));
            destroy_hash_element(e);
        }
        destroy_hash_table(env->variables);
        fsfree(env);
    }
}

// Merges the contents of a second environment into this one.  If overwrite is
// true, variables in the first that also exist in the second will be
// overwritten; otherwise, they will retain their original value.
void environment_merge(struct environment *env,
                       struct environment *other,
                       bool overwrite)
{
    hash_elem_t *e;
    const struct pair *p;

    for (e = hash_table_get_any(other->variables); e != NULL;
         e = hash_table_get_other(e)) {
        p = hash_elem_get_value(e);
        environment_set(env, p->key, p->value, overwrite);
    }
}

// Remove all keys present in a second environment from this one.
void environment_remove_keys(struct environment *env, list_t *list)
{
    hash_elem_t *he;
    list_elem_t *le;

    for (le = list_get_first(list); le != NULL; le = list_next(le)) {
        he = hash_table_pop(env->variables, list_elem_get_value(le));
        if (he != NULL) {
            pair_free(DQ(hash_elem_get_value(he)));
            destroy_hash_element(he);
        }
    }
}

// Adds a variable to the environment, replacing any prior instance if and only
// if overwrite is true.  Returns non-zero if the variable already exists,
// regardless of whether it was replaced.
int environment_set(struct environment *env,
                    const char *name,
                    const char *value,
                    bool overwrite)
{
    hash_elem_t *e;
    struct pair *p;

    e = hash_table_get(env->variables, name);
    if (e == NULL || overwrite) {
        p = pair_create(name, value);
        hash_table_put(env->variables, p->key, p);
    }
    if (e != NULL) {
        pair_free(DQ(hash_elem_get_value(e)));
        destroy_hash_element(e);
        errno = EEXIST;
        return -1;
    }
    return 0;
}

// Adds a variable to the environment, replacing any prior instance if and only
// if overwrite is true.  The string provided by the caller is split at the
// first equal sign; the left-hand side is the name of the variable and the
// right-hand side is its value.  Returns non-zero if the variable already
// exists, regardless of whether it was replaced.
int environment_put(struct environment *env,
                    const char *name_value,
                    bool overwrite)
{
    hash_elem_t *e;
    struct pair *p;
    const char *name, *eq, *value, *end;

    for (eq = name = name_value; *eq != '\0' && *eq != '='; eq++) {
        // XXX should check that chars are valid
        // nothing
    }
    if (*eq == '=') {
        for (end = value = eq + 1; *end != '\0'; end++) {
            // nothing
        }
    } else {
        end = value = eq;
    }
    e = hash_table_get(env->variables, name);
    if (e == NULL || overwrite) {
        p = pair_create_len(name, eq - name, value, end - value);
        e = hash_table_put(env->variables, p->key, p);
    }
    if (e != NULL) {
        pair_free(DQ(hash_elem_get_value(e)));
        destroy_hash_element(e);
        errno = EEXIST;
        return -1;
    }
    return 0;
}

// Returns the value of an environment variable, or NULL if it is not
// defined.
const char *environment_get(struct environment *env, const char *name)
{
    hash_elem_t *e;
    const struct pair *p;

    if ((e = hash_table_get(env->variables, name)) == NULL) {
        errno = ENOENT;
        return NULL;
    }
    p = hash_elem_get_value(e);
    return p->value;
}

// Returns a copy of the environment in the form of a list of key=value
// strings.
list_t *environment_list(struct environment *env)
{
    list_t *list;
    hash_elem_t *e;
    const struct pair *p;

    list = make_list();
    for (e = hash_table_get_any(env->variables); e != NULL;
         e = hash_table_get_other(e)) {
        p = hash_elem_get_value(e);
        list_append(list, charstr_printf("%s=%s", p->key, p->value));
    }
    return list;
}

byte_array_t *environment_to_byte_array(struct environment *env,
                                        byte_array_t *ba)
{
    byte_array_t *nba = NULL;
    hash_elem_t *e;
    const struct pair *p;

    if (ba == NULL) {
        ba = nba = make_byte_array(SIZE_MAX);
    }
    for (e = hash_table_get_any(env->variables); e != NULL;
         e = hash_table_get_other(e)) {
        p = hash_elem_get_value(e);
        if (!byte_array_appendf(ba, "%s=%s\n", p->key, p->value)) {
            goto fail;
        }
    }
    return ba;
fail:
    if (nba != NULL) {
        destroy_byte_array(nba);
    }
    return NULL;
}

char *environment_to_string(struct environment *env)
{
    byte_array_t *ba;
    char *str = NULL;

    ba = make_byte_array(SIZE_MAX);
    if (!environment_to_byte_array(env, ba)) {
        goto end;
    }
    str = charstr_dupstr(byte_array_data(ba));
end:
    destroy_byte_array(ba);
    return str;
}
