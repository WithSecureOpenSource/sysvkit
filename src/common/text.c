#include "text.h"

#include <fsdyn/fsalloc.h>

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static inline int isspace(int ch)
{
    return ch == ' ' || ch == '\f' || ch == '\n' || ch == '\r' || ch == '\t'
        || ch == '\v';
}

struct text *text_create(const char *buf, size_t len)
{
    struct text *txt;

    txt = fsalloc(sizeof(*txt));
    txt->beg = buf;
    txt->end = buf + len;
    txt->len = len;
    return txt;
}

struct text *text_from_file(const char *path)
{
    struct stat sb;
    struct text *txt;
    char *buf;
    ssize_t rsize;
    int fd;

    if ((fd = open(path, O_RDONLY)) < 1 || fstat(fd, &sb) != 0) {
        return NULL;
    }
    txt = fsalloc(sizeof(*txt) + sb.st_size + 1);
    buf = (char *)(txt + 1);
    rsize = read(fd, buf, sb.st_size);
    close(fd);
    if (rsize < 0) {
        fsfree(txt);
        return NULL;
    }
    buf[rsize] = '\0';
    txt->beg = buf;
    txt->end = txt->beg + rsize;
    txt->len = rsize;
    return txt;
}

struct text *text_line_from_stream(FILE *s)
{
    struct text *txt;
    char *buf;
    size_t sz;
    int ch;

    sz = 256;
    txt = fsalloc(sz);
    buf = (char *)txt + sizeof(*txt);
    txt->len = 0;
    while ((ch = fgetc(s)) != EOF) {
        if (ch == '\n') {
            break;
        }
        if (txt->len >= sz - sizeof(*txt) - 1) {
            sz *= 2;
            txt = fsrealloc(txt, sz);
            buf = (char *)txt + sizeof(*txt);
        }
        buf[txt->len++] = ch;
    }
    buf[txt->len] = '\0';
    if (ch == EOF && txt->len == 0) {
        fsfree(txt);
        return NULL;
    }
    txt->beg = buf;
    txt->end = txt->beg + txt->len;
    return txt;
}

// Returns a pointer to an allocated struct text which refers to a subsection of
// an existing one.
struct text *text_narrow(const struct text *parent, const char *beg, size_t len)
{
    struct text *txt;

    if (beg < parent->beg || beg + len > parent->end) {
        errno = EINVAL;
        return NULL;
    }
    txt = fsalloc(sizeof(*txt));
    txt->parent = parent;
    txt->beg = beg;
    txt->end = beg + len;
    txt->len = len;
    return txt;
}

// Returns a struct text that refers to the first line of a larger text.  This
// struct must be either freed with text_free() or passed repeatedly to
// text_next_line() until it returns NULL.  Note that a NUL is treated as the
// end of the text.
struct text *text_first_line(const struct text *txt)
{
    struct text *line;

    line = fsalloc(sizeof(*line));
    line->parent = txt;
    for (line->beg = line->end = txt->beg;
         line->end < txt->end && *line->end != '\0' && *line->end != '\n';
         line->end++) {
        // nothing
    }
    line->len = line->end - line->beg;
    return line;
}

// Updates a struct text to refer to the next line in its parent text.  Frees
// the struct and returns NULL if it already refers to the last line.  Note that
// a NUL is treated as the end of the text, and by extension the end of the last
// line.
struct text *text_next_line(struct text *line)
{
    const struct text *txt = line->parent;

    // line->end points to either a) the newline character at the end of a line
    // or b) the terminating '\0' in a text where the last line does not end in
    // a newline (which is, strictly speaking, invalid, but we accept it).
    line->beg = line->end + 1;
    if (line->beg >= txt->end || *line->end == '\0') {
        text_free(line);
        return NULL;
    }
    for (line->end = line->beg;
         line->end < txt->end && *line->end != '\0' && *line->end != '\n';
         line->end++) {
        // nothing
    }
    line->len = line->end - line->beg;
    return line;
}

// Returns a struct text that refers to the first word of a larger text.  This
// struct must be either freed with text_free() or passed repeatedly to
// text_next_word() until it returns NULL.  Note that a NUL is treated as the
// end of the text.
struct text *text_first_word(const struct text *txt)
{
    struct text *word;

    word = fsalloc(sizeof(*word));
    word->parent = txt;
    for (word->beg = txt->beg;
         word->beg < txt->end && *word->beg != '\0' && isspace(*word->beg);
         word->beg++) {
        // nothing
    }
    for (word->end = word->beg;
         word->end < txt->end && *word->end != '\0' && !isspace(*word->end);
         word->end++) {
        // nothing
    }
    word->len = word->end - word->beg;
    return word;
}

// Updates a struct text to refer to the next line in its parent text.  Frees
// the struct and returns NULL if it already refers to the last line.  Note that
// a NUL is treated as the end of the text, and by extension the end of the last
// line.
struct text *text_next_word(struct text *word)
{
    const struct text *txt = word->parent;

    // word->end points to either a) the first space following the word or b)
    // the terminating '\0' in a text where the last line does not end in a
    // newline (which is, strictly speaking, invalid, but we accept it).
    for (word->beg = word->end + 1;
         word->beg < txt->end && *word->beg != '\0' && isspace(*word->beg);
         word->beg++) {
        // nothing
    }
    if (word->beg >= txt->end || *word->beg == '\0') {
        text_free(word);
        return NULL;
    }
    for (word->end = word->beg;
         word->end < txt->end && *word->end != '\0' && !isspace(*word->end);
         word->end++) {
        // nothing
    }
    word->len = word->end - word->beg;
    return word;
}

// Returns a pointer to the first occurrence of a given string in a text.  If no
// length is provided, searches for the entire string.
//
// This is a plain linear search with no preprocessing and thus has a worst-case
// performance of O(mn).
const char *text_find(const struct text *txt, const char *str, size_t len)
{
    const char *p, *q, *s, *end;

    end = len ? str + len : (char *)INTPTR_MAX;
    for (p = txt->beg; p < txt->end && *p != '\0'; p++) {
        if (*p == *str) {
            for (q = p, s = str; q < txt->end && *q != '\0' && s < end
                 && *s != '\0' && *q == *s;
                 q++, s++) {
                // nothing
            }
            if (s == end || *s == '\0') {
                return p;
            }
        }
    }
    return NULL;
}

// Finds the first line in the text that starts with the given prefix.
struct text *text_first_line_prefix(const struct text *txt,
                                    const char *str,
                                    size_t len)
{
    struct text *line;
    const char *p, *q, *s, *end;

    end = len ? str + len : (char *)INTPTR_MAX;
    for (p = txt->beg; p < txt->end && *p != '\0'; p++) {
        for (q = p, s = str;
             q < txt->end && *q != '\0' && s < end && *s != '\0' && *q == *s;
             q++, s++) {
            // nothing
        }
        while (q < txt->end && *q != '\0' && *q != '\n') {
            q++;
        }
        if (s == end || *s == '\0') {
            line = text_narrow(txt, p, q - p);

            return line;
        }
        p = q;
    }
    return NULL;
}

// Finds the next line in the text that starts with the given prefix.
struct text *text_next_line_prefix(struct text *line,
                                   const char *str,
                                   size_t len)
{
    const struct text *txt = line->parent;
    const char *p, *q, *s, *end;

    end = len ? str + len : (char *)INTPTR_MAX;
    line->beg = line->end + 1;
    if (line->beg >= txt->end || *line->end == '\0') {
        text_free(line);
        return NULL;
    }
    for (p = line->beg; p < txt->end && *p != '\0'; p++) {
        for (q = p, s = str;
             q < txt->end && *q != '\0' && s < end && *s != '\0' && *q == *s;
             q++, s++) {
            // nothing
        }
        while (q < txt->end && *q != '\0' && *q != '\n') {
            q++;
        }
        if (s == end || *s == '\0') {
            line->beg = p;
            line->end = q;
            line->len = q - p;

            return line;
        }
        p = q;
    }
    text_free(line);
    return NULL;
}

// Finds the first line in the text which is equal to the given string.
struct text *text_first_line_equals(const struct text *txt,
                                    const char *str,
                                    size_t len)
{
    struct text *line;
    const char *p, *q, *s, *end;

    end = len ? str + len : (char *)INTPTR_MAX;
    for (p = txt->beg; p < txt->end && *p != '\0'; p++) {
        for (q = p, s = str;
             q < txt->end && *q != '\0' && s < end && *s != '\0' && *q == *s;
             q++, s++) {
            // nothing
        }
        if ((s == end || *s == '\0')
            && (q == txt->end || *q == '\0' || *q == '\n')) {
            line = text_narrow(txt, p, q - p);
            return line;
        }
        while (q < txt->end && *q != '\0' && *q != '\n') {
            q++;
        }
        p = q;
    }
    return NULL;
}

// Finds the next line in the text which is equal to the given string.
struct text *text_next_line_equals(struct text *line,
                                   const char *str,
                                   size_t len)
{
    const struct text *txt = line->parent;
    const char *p, *q, *s, *end;

    end = len ? str + len : (char *)INTPTR_MAX;
    line->beg = line->end + 1;
    if (line->beg >= txt->end || *line->end == '\0') {
        text_free(line);
        return NULL;
    }
    for (p = line->beg; p < txt->end && *p != '\0'; p++) {
        for (q = p, s = str;
             q < txt->end && *q != '\0' && s < end && *s != '\0' && *q == *s;
             q++, s++) {
            // nothing
        }
        if ((s == end || *s == '\0')
            && (q == txt->end || *q == '\0' || *q == '\n')) {
            line->beg = p;
            line->end = q;
            line->len = q - p;
            return line;
        }
        while (q < txt->end && *q != '\0' && *q != '\n') {
            q++;
        }
        p = q;
    }
    text_free(line);
    return NULL;
}

void text_free(struct text *txt)
{
    fsfree(txt);
}
