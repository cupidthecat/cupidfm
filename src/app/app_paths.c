#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "app_paths.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "utils.h" // path_join

bool ensure_parent_dir_local(const char *path) {
    if (!path || !*path)
        return false;
    if (strlen(path) >= MAX_PATH_LENGTH) {
        errno = ENAMETOOLONG;
        return false;
    }
    char tmp[MAX_PATH_LENGTH];
    strcpy(tmp, path);
    char *last = strrchr(tmp, '/');
    if (!last || last == tmp)
        return true;
    *last = '\0';
    for (char *at = tmp + 1;; at++) {
        if (*at && *at != '/')
            continue;
        char saved = *at;
        *at = '\0';
        struct stat info;
        if (mkdir(tmp, 0755) && (errno != EEXIST || stat(tmp, &info) || !S_ISDIR(info.st_mode))) {
            *at = saved;
            return false;
        }
        *at = saved;
        if (!saved)
            break;
    }
    return true;
}

void resolve_path_under_cwd(char out[MAX_PATH_LENGTH], const char *cwd, const char *path) {
    if (!out)
        return;
    if (!path || !*path) {
        out[0] = 0;
        return;
    }
    char resolved[MAX_PATH_LENGTH];
    int length;
    if (path[0] == '/' || !cwd || !*cwd)
        length = snprintf(resolved, sizeof(resolved), "%s", path);
    else
        length = snprintf(resolved, sizeof(resolved), "%s%s%s", cwd,
                          cwd[strlen(cwd) - 1] == '/' ? "" : "/", path);
    if (length < 0 || (size_t)length >= sizeof(resolved)) {
        out[0] = 0;
        errno = ENAMETOOLONG;
        return;
    }
    memcpy(out, resolved, (size_t)length + 1);
}

bool app_startup_entry(AppStartupEntry *entry, const char *cwd, const char *path) {
    if (!entry) {
        errno = EINVAL;
        return false;
    }
    *entry = (AppStartupEntry){0};
    char resolved[MAX_PATH_LENGTH];
    resolve_path_under_cwd(resolved, cwd, path);
    if (!*resolved) {
        if (!path || !*path)
            errno = EINVAL;
        return false;
    }
    struct stat info;
    bool directory = !stat(resolved, &info) && S_ISDIR(info.st_mode);
    if (!directory) {
        if (lstat(resolved, &info))
            return false;
        char *slash = strrchr(resolved, '/');
        if (!slash || !slash[1]) {
            errno = EINVAL;
            return false;
        }
        strcpy(entry->name, slash + 1);
        slash[slash == resolved ? 1 : 0] = 0;
    }
    char *canonical = realpath(resolved, NULL);
    if (!canonical)
        return false;
    size_t length = strlen(canonical), name_length = strlen(entry->name);
    if (length >= MAX_PATH_LENGTH ||
        (name_length && length + name_length + (length > 1 ? 1 : 0) >= MAX_PATH_LENGTH)) {
        free(canonical);
        errno = ENAMETOOLONG;
        return false;
    }
    DIR *handle = opendir(canonical);
    if (!handle) {
        int saved = errno;
        free(canonical);
        errno = saved;
        return false;
    }
    closedir(handle);
    strcpy(entry->directory, canonical);
    free(canonical);
    return true;
}

const char *basename_ptr(const char *p) {
    if (!p)
        return "";
    size_t len = strlen(p);
    while (len > 0 && p[len - 1] == '/')
        len--;
    if (len == 0)
        return "";
    const char *end = p + len;
    const char *slash = memrchr(p, '/', (size_t)(end - p));
    return slash ? (slash + 1) : p;
}
