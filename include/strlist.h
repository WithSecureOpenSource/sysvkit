#pragma once

#include <fsdyn/list.h>

#include <stdbool.h>

char **strlist_to_vector(list_t *);
void strlist_free(list_t *);
list_t *strlist_from_delim(const char *, char, bool, bool);
char *strlist_to_delim(list_t *, char, bool);
