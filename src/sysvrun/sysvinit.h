#pragma once

#include "text.h"

#define LSB_BEGIN_INIT_INFO "### BEGIN INIT INFO"
#define LSB_END_INIT_INFO "### END INIT INFO"
#define LSB_PROVIDES "# Provides:"
#define LSB_REQUIRED_START "# Required-Start:"
#define LSB_SHOULD_START "# Should-Start:"

#define BEGIN_EMBED ":<<SYSVKIT"
#define END_EMBED "SYSVKIT"

struct service *sysvinit_parse_init_script(const char *, const struct text *);
struct service *sysvinit_find_service(const char *);
