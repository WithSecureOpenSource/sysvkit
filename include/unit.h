#pragma once

#include <fsdyn/bytearray.h>

struct unit *unit_create(const char *);
int unit_update_value(struct unit *,
                      const char *,
                      const char *,
                      const char *,
                      bool);
int unit_set_value(struct unit *, const char *, const char *, const char *);
int unit_append_value(struct unit *, const char *, const char *, const char *);
const char *unit_get_value(struct unit *, const char *, const char *);
int unit_get_bool(struct unit *, const char *, const char *);
int unit_delete_value(struct unit *u, const char *, const char *);
void unit_free(struct unit *);
byte_array_t *unit_to_byte_array(struct unit *, byte_array_t *);
char *unit_to_string(struct unit *);
