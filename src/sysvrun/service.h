#pragma once

#include "clock.h"
#include "text.h"

#include <fsdyn/bytearray.h>
#include <fsdyn/list.h>

enum servicetype {
    ST_SIMPLE,
    ST_EXEC,
    ST_FORKING,
    ST_ONESHOT,
    ST_DBUS,
    ST_NOTIFY,
    ST_IDLE,
    //
    ST_MAX
};

extern const char *service_type_names[];

enum killmode {
    KM_CGROUP,
    KM_MIXED,
    KM_PROCESS,
    KM_NONE,
    //
    KM_MAX
};

extern const char *kill_mode_names[];

enum restartpolicy {
    RP_NO,
    RP_ALWAYS,
    RP_ON_SUCCESS,
    RP_ON_FAILURE,
    RP_ON_ABNORMAL,
    RP_ON_ABORT,
    // RP_ON_WATCHDOG, /* unsupported */
    //
    RP_MAX
};

extern const char *restart_policy_names[];

struct service {
    char *name;
    // pointer to systemd unit if applicable
    struct unit *u;
    // type and policy
    enum servicetype type;
    enum killmode kill_mode;
    usec_t stop_timeout;
    enum restartpolicy restart_policy;
    bool remain_after_exit;
    usec_t delay;
    usec_t start_limit_interval;
    unsigned long start_limit_burst;
    // lists of dependencies
    list_t *required;
    list_t *should;
};

struct service *service_from_init_script(const char *, const struct text *);
struct service *service_from_unit_file(const char *, const struct text *);
struct service *service_from_file(const char *, const char *);
struct service *service_find(const char *);
void service_free(struct service *);
byte_array_t *service_to_byte_array(struct service *, byte_array_t *);
char *service_to_string(struct service *);
int service_convert(struct service *, const char *);
int service_show(struct service *, const char *);
int service_start(struct service *);
int service_stop(struct service *);
int service_reload(struct service *);
int service_restart(struct service *);
int service_status(struct service *);
int service_control(struct service *);
