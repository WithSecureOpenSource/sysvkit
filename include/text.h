#pragma once

#include <stdio.h>

struct text {
    const struct text *parent;
    const char *beg, *end;
    size_t len;
};

struct text *text_create(const char *, size_t);
struct text *text_from_file(const char *);
struct text *text_line_from_stream(FILE *);
struct text *text_narrow(const struct text *, const char *, size_t);
struct text *text_first_line(const struct text *);
struct text *text_next_line(struct text *);
struct text *text_first_word(const struct text *);
struct text *text_next_word(struct text *);
struct text *text_first_line_prefix(const struct text *, const char *, size_t);
struct text *text_next_line_prefix(struct text *, const char *, size_t);
struct text *text_first_line_equals(const struct text *, const char *, size_t);
struct text *text_next_line_equals(struct text *, const char *, size_t);
void text_free(struct text *);
