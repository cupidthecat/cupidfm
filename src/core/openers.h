#ifndef OPENERS_H
#define OPENERS_H

#include <glib.h>
#include <stdbool.h>

typedef struct Openers Openers;

typedef struct {
    char *name;
    char *description;
    char *command;
    char *platform;
    bool block;
    bool orphan;
} Opener;

/* A missing optional file keeps the defaults. Invalid settings fail with a GError.
 * Each match is borrowed; free the returned array before destroying the set. */
Openers *openers_load(const char *filename, GError **error);
GPtrArray *openers_match(Openers *set, const char *path, const char *mime);
void openers_free(Openers *set);

#endif
