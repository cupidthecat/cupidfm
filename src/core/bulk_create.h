#ifndef BULK_CREATE_H
#define BULK_CREATE_H

#include <glib.h>
#include <stdio.h>

/* The returned array owns absolute paths, with a trailing slash for directories.
 * Blank lines are ignored; malformed lists return NULL without creating anything. */
GPtrArray *bulk_create_read(FILE *file, const char *directory, char **error);

#endif
