#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

bool clipboard_set_from_file(const char *path);
typedef struct {
    char **paths;
    size_t count;
    bool cut;
} ClipboardPaths;

bool clipboard_set_paths(const char *const *paths, size_t count, bool cut);
bool clipboard_read_paths(FILE *stream, ClipboardPaths *out);
void clipboard_paths_free(ClipboardPaths *paths);

#endif // CLIPBOARD_H
