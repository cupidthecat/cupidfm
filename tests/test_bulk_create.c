#define _GNU_SOURCE
#include "bulk_create.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static GPtrArray *parse(const char *data, size_t size, char **error) {
    FILE *file = fmemopen((char *)data, size, "r");
    assert(file);
    GPtrArray *paths = bulk_create_read(file, "/tmp/base", error);
    fclose(file);
    return paths;
}

int main(void) {
    char *error = NULL;
    const char *text = "nested/file.txt\n\nempty/\n../next.txt\r\n/absolute\n   \n";
    GPtrArray *paths = parse(text, strlen(text), &error);
    assert(paths && !error && paths->len == 5);
    assert(!strcmp(g_ptr_array_index(paths, 0), "/tmp/base/nested/file.txt"));
    assert(!strcmp(g_ptr_array_index(paths, 1), "/tmp/base/empty/"));
    assert(!strcmp(g_ptr_array_index(paths, 2), "/tmp/next.txt"));
    assert(!strcmp(g_ptr_array_index(paths, 3), "/absolute"));
    assert(!strcmp(g_ptr_array_index(paths, 4), "/tmp/base/   "));
    g_ptr_array_free(paths, TRUE);
    const char nul[] = {'o', 'k', '\n', 'b', 0, 'd'};
    assert(!parse(nul, sizeof(nul), &error) && error);
    g_clear_pointer(&error, g_free);
    char long_name[2048];
    memset(long_name, 'a', sizeof(long_name));
    assert(!parse(long_name, sizeof(long_name), &error) && error);
    g_clear_pointer(&error, g_free);
    paths = parse("", 0, &error);
    assert(paths && !error && !paths->len);
    g_ptr_array_free(paths, TRUE);
    puts("Bulk create paths, directory markers, blank lines, CRLF, limits, and NUL rejection "
         "passed");
}
