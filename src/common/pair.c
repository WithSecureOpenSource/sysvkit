#include "pair.h"

#include <fsdyn/fsalloc.h>

#include <stdlib.h>
#include <string.h>

struct pair *pair_create_len(const char *key,
                             size_t klen,
                             const char *value,
                             size_t vlen)
{
    struct pair *p;

    p = fsalloc(sizeof(*p) + klen + 1 + vlen + 1);
    memcpy(p->buf, key, klen);
    p->buf[klen] = '\0';
    p->key = p->buf;
    memcpy(p->buf + klen + 1, value, vlen);
    p->buf[klen + 1 + vlen] = '\0';
    p->value = p->buf + klen + 1;
    return p;
}

struct pair *pair_create(const char *key, const char *value)
{
    return pair_create_len(key, strlen(key), value, strlen(value));
}

struct pair *pair_clone(const struct pair *o)
{

    return pair_create(o->key, o->value);
}

void pair_free(struct pair *p)
{
    fsfree(p);
}
