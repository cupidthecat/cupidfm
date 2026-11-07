#define _GNU_SOURCE
#include "app_paths.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char out[MAX_PATH_LENGTH];
    resolve_path_under_cwd(out, "/tmp/base", "file");
    assert(!strcmp(out, "/tmp/base/file"));
    resolve_path_under_cwd(out, "/tmp/base/", "file");
    assert(!strcmp(out, "/tmp/base/file"));
    resolve_path_under_cwd(out, "/tmp/base", "/other");
    assert(!strcmp(out, "/other"));
    strcpy(out, "file");
    resolve_path_under_cwd(out, "/tmp/base", out);
    assert(!strcmp(out, "/tmp/base/file"));
    resolve_path_under_cwd(out, NULL, "relative");
    assert(!strcmp(out, "relative"));
    char too_long[MAX_PATH_LENGTH + 10];
    memset(too_long, 'a', sizeof(too_long));
    too_long[sizeof(too_long) - 1] = 0;
    resolve_path_under_cwd(out, "/tmp/base", too_long);
    assert(!*out && errno == ENAMETOOLONG);
    too_long[0] = '/';
    resolve_path_under_cwd(out, "/tmp/base", too_long);
    assert(!*out && errno == ENAMETOOLONG);
    resolve_path_under_cwd(out, "/tmp/base", "");
    assert(!*out);
    puts("Path resolution, trailing separators, aliasing, and rejection without truncation passed");
}
