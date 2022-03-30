#pragma once

#include <stddef.h>

struct pair {
    const char *key;
    const char *value;
    char buf[];
};

struct pair *pair_create_len(const char *, size_t, const char *, size_t);
struct pair *pair_create(const char *, const char *);
struct pair *pair_clone(const struct pair *);
void pair_free(struct pair *);
