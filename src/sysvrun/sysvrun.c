#include "sysvrun.h"

#include "exitcode.h"
#include "noise.h"
#include "proctitle.h"
#include "service.h"
#include "strlist.h"

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <paths.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Root directory
const char *root = "";

// Own name and location
const char *self;
static char *_self;
const char *self_base;
static char *_self_base;
const char *self_dir;
static char *_self_dir;

// Don't do anything
bool dryrun;

// Run in foreground
bool foreground;

// Output path for convert
const char *output;

// Environment template for commands
//
// Denv: variables that we set to hardcoded defaults + variables copied from our
// own environment + variables passed on the command line using -Dkey=value
//
// Ulist: variable names passed on the command line using -Ukey
//
// When executing a command, we start with Denv, add whatever was defined in the
// unit file, and remove anything listed in Ulist.
//
// TODO: support PassEnvironment and UnsetEnvironment
struct environment *Denv;
list_t *Ulist;

static const char *preserve_env[] = {
    // list of environment variables to pass on to services
    "SYSVKIT_LOG_TO_FILE",
    "SYSVKIT_NOISE",
    NULL
};

static void cleanup_environment(void)
{
    environment_free(Denv);
    destroy_list(Ulist); // contents are all from argv
}

static void setup_environment(void)
{
    const char **key, *value;

    Denv = environment_create();
    // Set PATH.
    environment_set(Denv, "PATH", _PATH_STDPATH, false);
    // Copy preserved environment variables.
    for (key = preserve_env; *key != NULL; key++) {
        if ((value = getenv(*key)) != NULL) {
            environment_set(Denv, *key, value, false);
        }
    }
    Ulist = make_list();
    atexit(cleanup_environment); // for valgrind's sake
}

static void cleanup_self(void)
{
    free(_self);
    free(_self_base);
    free(_self_dir);
}

static void setup_self(const char *arg0)
{
    char exe[PATH_MAX + 1];
    list_t *l;
    ssize_t res;
    size_t len;

    res = readlink("/proc/self/exe", exe, sizeof(exe));
    if (res > 0 && (size_t)res + 1 < sizeof(exe)) {
        exe[res] = '\0';
        self = _self = realpath(exe, NULL);
    }
    if (self == NULL) {
        self = strdup(arg0);
    }
    // XXX this can easily be tripped up by symlinks
    len = strlen(root);
    while (len > 0 && root[len - 1] == '/') {
        len--;
    }
    if (len > 0 && strncmp(self, root, len) == 0 && self[len] == '/') {
        self += len;
    }
    // directory and base name
    l = strlist_from_delim(self, '/', true, false);
    self_base = _self_base = DQ(list_pop_last(l));
    self_dir = _self_dir = strlist_to_delim(l, '/', false);
    strlist_free(l);
    atexit(cleanup_self);
}

static int sysvrun(struct service *svc, const char *verb)
{
    if (strcmp(verb, "convert") == 0) {
        return service_convert(svc, output);
    } else if (strcmp(verb, "show") == 0) {
        return service_show(svc, output);
    } else if (strcmp(verb, "start") == 0) {
        return service_start(svc);
    } else if (strcmp(verb, "stop") == 0) {
        return service_stop(svc);
    } else if (strcmp(verb, "reload") == 0) {
        return service_reload(svc);
    } else if (strcmp(verb, "restart") == 0) {
        return service_restart(svc);
    } else if (strcmp(verb, "status") == 0) {
        return service_status(svc);
    } else if (strcmp(verb, "control") == 0) {
        return service_control(svc);
    }
    fprintf(stderr, "unknown command: %s\n", verb);
    errno = EINVAL;
    return EX_USAGE;
}

static void usage(void)
{
    printf("sysvrun [options] service verb\n");
}

static const struct option options[] = {
    { "debug", no_argument, 0, 'd' },
    { "define", required_argument, 0, 'D' },
    { "dry-run", no_argument, 0, 'n' },
    { "dryrun", no_argument, 0, 'n' },
    { "foreground", no_argument, 0, 'f' },
    { "help", no_argument, 0, 'h' },
    { "output", required_argument, 0, 'o' },
    { "root", required_argument, 0, 'r' },
    { "quiet", no_argument, 0, 'q' },
    { "undefine", required_argument, 0, 'U' },
    { "unit-file", required_argument, 0, 'u' },
    { "verbose", no_argument, 0, 'v' },
    { 0 },
};

int main(int argc, char *argv[])
{
    const char *service, *verb, *unit_file = NULL;
    struct service *svc;
    int opt = -1, res;

    setup_proctitle(argc, argv);
    setup_environment();
    while ((opt = getopt_long(argc, argv, "dD:fhno:qr:U:u:v", options, NULL))
           != -1) {
        switch (opt) {
            case 0:
                // already handled by getopt_long()
                break;
            case 'D':
                environment_put(Denv, optarg, true);
                break;
            case 'f':
                foreground = true;
                break;
            case 'h':
                usage();
                return EXIT_SUCCESS;
            case 'n':
                dryrun = true;
                break;
            case 'o':
                output = optarg;
                break;
            case 'r':
                root = optarg;
                break;
            case 'U':
                list_append(Ulist, optarg);
                break;
            case 'u':
                unit_file = optarg;
                break;
            case 'd':
            case 'q':
            case 'v':
                noise_set_level(opt);
                break;
            default:
                usage();
                return EX_USAGE;
        }
    }

    setup_self(argv[0]);
    argc -= optind;
    argv += optind;
    if (argc != 2) {
        usage();
        return EX_USAGE;
    }
    if (noise_override(NULL) != 0) {
        error("invalid noise level %s=%s", NOISE_ENVVAR, getenv(NOISE_ENVVAR));
        return EX_USAGE;
    }

    service = argv[0];
    verb = argv[1];
    argc -= 2;
    argv += 2;

    // XXX should validate the service name here
    if (output != NULL && strcmp(verb, "convert") != 0
        && strcmp(verb, "show") != 0) {
        usage();
        return EX_USAGE;
    }

    if (unit_file != NULL) {
        svc = service_from_file(service, unit_file);
    } else {
        svc = service_find(service);
    }
    if (svc == NULL) {
        error("service '%s' not found", service);
        return EXIT_FAILURE;
    }

    res = sysvrun(svc, verb);
    service_free(svc);
    exit(res);
}
