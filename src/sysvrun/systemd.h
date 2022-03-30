#pragma once

#include "text.h"

#include <fsdyn/list.h>

#include <stdbool.h>

#define DOT_SERVICE ".service"

bool deservicify(char *);
list_t *systemd_split_quoted(const char *);
struct unit *systemd_parse_unit_file(const char *name, const struct text *);
struct service *systemd_find_service(const char *);
