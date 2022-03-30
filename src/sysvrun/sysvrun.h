#pragma once

#include "common.h"
#include "environment.h"
#include "noise.h"
#include "pair.h"
#include "text.h"

#include <fsdyn/bytearray.h>
#include <fsdyn/list.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

extern const char *root;
extern const char *self;
extern const char *self_base;
extern const char *self_dir;

extern bool dryrun;
extern bool foreground;

extern struct environment *Denv;
extern list_t *Ulist;

void set_argv(int, const char **);
