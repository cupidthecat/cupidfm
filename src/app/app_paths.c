#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "app_paths.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

#include "utils.h" // path_join

bool ensure_parent_dir_local(const char *path) {
    if (!path || !*path) return false;
    char tmp[MAX_PATH_LENGTH];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) return true;
    *slash = '\0';
    if (mkdir(tmp, 0700) == 0) return true;
    return (errno == EEXIST);
}

void resolve_path_under_cwd(char out[MAX_PATH_LENGTH], const char *cwd, const char *path) {
    if (!out) return;
    out[0] = '\0';
    if (!path || !*path) return;
    if (path[0] == '/') {
        strncpy(out, path, MAX_PATH_LENGTH - 1);
        out[MAX_PATH_LENGTH - 1] = '\0';
        return;
    }
    if (!cwd || !*cwd) {
        strncpy(out, path, MAX_PATH_LENGTH - 1);
        out[MAX_PATH_LENGTH - 1] = '\0';
        return;
    }
    path_join(out, cwd, path);
}

const char *basename_ptr(const char *p) {
    if (!p) return "";
    size_t len = strlen(p);
    while (len > 0 && p[len - 1] == '/') len--;
    if (len == 0) return "";
    const char *end = p + len;
    const char *slash = memrchr(p, '/', (size_t)(end - p));
    return slash ? (slash + 1) : p;
}
