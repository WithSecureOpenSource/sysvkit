#pragma once

#include "pair.h"

#include <fsdyn/bytearray.h>
#include <fsdyn/list.h>

#include <stdbool.h>

struct environment *environment_create(void);
struct environment *environment_clone(struct environment *);
void environment_free(struct environment *);
void environment_merge(struct environment *, struct environment *, bool);
int environment_set(struct environment *, const char *, const char *, bool);
int environment_put(struct environment *, const char *, bool);
const char *environment_get(struct environment *, const char *);
void environment_remove_keys(struct environment *, list_t *);
list_t *environment_list(struct environment *);
byte_array_t *environment_to_byte_array(struct environment *, byte_array_t *);
char *environment_to_string(struct environment *);
