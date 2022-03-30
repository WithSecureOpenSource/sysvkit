#define _GNU_SOURCE

#include "noise.h"
#include "strlist.h"

#include <fsdyn/fsalloc.h>

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static unsigned int ec;

// XXX needs more cases
// XXX needs test cases for blank = true
static struct test_case_delim {
    const char *descr;
    const char **list;
    struct io {
        const char *str;
        char delim;
        bool dedup;
        bool blank;
    } in, out;
} test_cases_delim[] = {
    {
        .descr = "empty",
        .in = { .str = "" },
        .list = (const char *[]) { NULL },
        .out = { .str = "" },
    },
    {
        .descr = "single delimiter",
        .in = { .str = ":" },
        .list = (const char *[]) { NULL },
        .out = { .str = "" },
    },
    {
        .descr = "multiple delimiters",
        .in = { .str = "::::" },
        .list = (const char *[]) { NULL },
        .out = { .str = "" },
    },
    {
        .descr = "single element",
        .in = { .str = "foo" },
        .list =
            (const char *[]) {
                "foo",
                NULL,
            },
        .out = { .str = "foo" },
    },
    {
        .descr = "leading delimiter",
        .in = { .str = ":foo" },
        .list =
            (const char *[]) {
                "foo",
                NULL,
            },
        .out = { .str = "foo" },
    },
    {
        .descr = "trailing delimiter",
        .in = { .str = "foo:" },
        .list =
            (const char *[]) {
                "foo",
                NULL,
            },
        .out = { .str = "foo" },
    },
    {
        .descr = "multiple elements",
        .in = { .str = "foo:bar:baz" },
        .list =
            (const char *[]) {
                "foo",
                "bar",
                "baz",
                NULL,
            },
        .out = { .str = "foo:bar:baz" },
    },
    {
        .descr = "multiple elements, multiple delimiters",
        .in = { .str = "foo::bar:::baz" },
        .list =
            (const char *[]) {
                "foo",
                "bar",
                "baz",
                NULL,
            },
        .out = { .str = "foo:bar:baz" },
    },
    {
        .descr = "duplicate elements, no deduplication",
        .in = { .str = "foo:bar:foo" },
        .list =
            (const char *[]) {
                "foo",
                "bar",
                "foo",
                NULL,
            },
        .out = { .str = "foo:bar:foo" },
    },
    {
        .descr = "duplicate elements, input deduplication",
        .in = { .str = "foo:bar:foo", .dedup = true },
        .list =
            (const char *[]) {
                "foo",
                "bar",
                NULL,
            },
        .out = {},
    },
    {
        .descr = "duplicate elements, output deduplication",
        .in = {},
        .list =
            (const char *[]) {
                "foo",
                "bar",
                "foo",
                NULL,
            },
        .out = { .dedup = true, .str = "foo:bar" },
    },
    // XXX add more cases...
};

static void test_strlist_from_delim(void)
{
    struct test_case_delim *tc;
    list_t *list;
    list_elem_t *e;
    const char **p, *es;
    unsigned int i, j, n;
    bool ok;

    n = sizeof(test_cases_delim) / sizeof(test_cases_delim[0]);
    printf("1..%u\n", n);
    for (i = 1, tc = test_cases_delim; i <= n; i++, tc++) {
        if (tc->in.str == NULL) {
            printf("ok %u # skip No input provided\n", i);
            continue;
        }
        ok = true;
        list = strlist_from_delim(tc->in.str,
                                  tc->in.delim,
                                  tc->in.blank,
                                  tc->in.dedup);
        for (j = 0, p = tc->list, e = list_get_first(list);
             *p != NULL && e != NULL;
             j++, p++, e = list_next(e)) {
            es = list_elem_get_value(e);
            if (strcmp(es, *p) != 0) {
                debug("expected \"%s\" at %u, got \"%s\"", *p, j, es);
                ok = false;
            }
        }
        if (*p != NULL) {
            debug("missing elements in list");
            ok = false;
        }
        if (e != NULL) {
            debug("too many elements in list");
            ok = false;
        }
        strlist_free(list);
        if (!ok) {
            printf("not ");
            ec++;
        }
        printf("ok %u - string to list: %s\n", i, tc->descr);
    }
}

static void test_strlist_to_delim(void)
{
    struct test_case_delim *tc;
    list_t *list;
    const char **p;
    char *str;
    unsigned int i, n;
    bool ok;

    n = sizeof(test_cases_delim) / sizeof(test_cases_delim[0]);
    printf("1..%u\n", n);
    for (i = 1, tc = test_cases_delim; i <= n; i++, tc++) {
        if (tc->out.str == NULL) {
            printf("ok %u # skip No output expected\n", i);
            continue;
        }
        ok = true;
        list = make_list();
        for (p = tc->list; *p != NULL; p++) {
            list_append(list, *p);
        }
        str = strlist_to_delim(list, tc->out.delim, tc->out.dedup);
        if (strcmp(str, tc->out.str) != 0) {
            debug("expected \"%s\", got \"%s\"", tc->out.str, str);
            ok = false;
        }
        fsfree(str);
        destroy_list(list);
        if (!ok) {
            printf("to ");
            ec++;
        }
        printf("ok %u - list to string: %s\n", i, tc->descr);
    }
}

static void usage(void) __attribute__((__noreturn__));
static void usage(void)
{
    fprintf(stderr, "usage: %s [-dhqv]\n", program_invocation_short_name);
    exit(1);
}

int main(int argc, char *argv[])
{
    int opt;

    while ((opt = getopt(argc, argv, "dhqv")) != -1) {
        switch (opt) {
            case 'd':
                if (noisy >= DEBUG) {
                    noisy++;
                } else {
                    noisy = DEBUG;
                }
                break;
            case 'h':
                usage();
                break;
            case 'q':
                noisy = QUIET;
                break;
            case 'v':
                noisy = VERBOSE;
                break;
            default:
                usage();
                break;
        }
    }
    argc -= optind;
    argv += optind;
    if (argc > 0) {
        usage();
    }

    test_strlist_from_delim();
    test_strlist_to_delim();
    exit(ec == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
}
