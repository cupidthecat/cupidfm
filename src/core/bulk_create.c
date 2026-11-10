#define _GNU_SOURCE
#include "bulk_create.h"

#include <string.h>

#include "globals.h"

GPtrArray *bulk_create_read(FILE *file, const char *directory, char **error) {
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    for (;;) {
        char line[MAX_PATH_LENGTH];
        size_t length = 0;
        int c;
        while ((c = fgetc(file)) != EOF && c != '\n') {
            if (!c || length + 1 >= sizeof(line)) {
                if (error)
                    *error = g_strdup("A create-list entry is too long or contains a NUL byte");
                goto fail;
            }
            line[length++] = (char)c;
        }
        if (ferror(file)) {
            if (error)
                *error = g_strdup("Cannot read the create list");
            goto fail;
        }
        if (length && line[length - 1] == '\r')
            length--;
        line[length] = 0;
        if (length) {
            bool folder = line[length - 1] == '/';
            char *absolute = g_canonicalize_filename(line, directory);
            char *path = folder && !g_str_has_suffix(absolute, "/")
                             ? g_strconcat(absolute, "/", NULL)
                             : g_strdup(absolute);
            g_free(absolute);
            if (strlen(path) >= MAX_PATH_LENGTH || paths->len >= 65536) {
                g_free(path);
                if (error)
                    *error = g_strdup("Create list exceeds the path or 65536-entry limit");
                goto fail;
            }
            g_ptr_array_add(paths, path);
        }
        if (c == EOF)
            return paths;
    }
fail:
    g_ptr_array_free(paths, TRUE);
    return NULL;
}
