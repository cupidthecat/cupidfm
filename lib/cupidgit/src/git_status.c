#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include "git_status.h"
#include "git_hash.h"
#include "git_index.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Missing directories are allowed for deleted tracked paths. Resolve the
 * nearest existing ancestor so an intermediate symlink still stays meaningful. */
static char *resolve_directory(const char *path) {
    char *resolved = realpath(path, NULL);
    if (resolved || errno != ENOENT)
        return resolved;
    char *copy = strdup(path);
    if (!copy)
        return NULL;
    size_t len = strlen(copy);
    while (len > 1 && copy[len - 1] == '/')
        copy[--len] = 0;
    char *slash = strrchr(copy, '/');
    const char *name = slash ? slash + 1 : copy;
    if (slash)
        *slash = 0;
    char *parent = resolve_directory(slash ? (*copy ? copy : "/") : ".");
    if (parent && !strcmp(name, "..")) {
        char *last = strrchr(parent, '/');
        if (last == parent)
            last[1] = 0;
        else if (last)
            *last = 0;
        resolved = parent;
    } else if (parent && !strcmp(name, ".")) {
        resolved = parent;
    } else if (parent) {
        size_t n = strlen(parent) + strlen(name) + 2;
        resolved = malloc(n);
        if (resolved)
            snprintf(resolved, n, "%s%s%s", parent, strcmp(parent, "/") ? "/" : "", name);
        free(parent);
    }
    free(copy);
    return resolved;
}

/* Resolve the parent only: symlinks and deleted tracked files keep their names. */
static char *relative_path(const char *root, const char *path, char **absolute) {
    char *copy = strdup(path);
    if (!copy)
        return NULL;
    char *slash = strrchr(copy, '/');
    const char *name = slash ? slash + 1 : copy;
    if (slash)
        *slash = 0;
    char *parent = resolve_directory(slash ? (*copy ? copy : "/") : ".");
    if (!parent || !*name || !strcmp(name, ".") || !strcmp(name, "..")) {
        free(parent);
        free(copy);
        return NULL;
    }
    size_t n = strlen(parent) + strlen(name) + 2;
    *absolute = malloc(n);
    if (*absolute)
        snprintf(*absolute, n, "%s%s%s", parent, strcmp(parent, "/") ? "/" : "", name);
    free(parent);
    free(copy);
    if (!*absolute)
        return NULL;
    size_t len = strlen(root);
    if (strncmp(*absolute, root, len) || (len > 1 && (*absolute)[len] != '/')) {
        free(*absolute);
        *absolute = NULL;
        return NULL;
    }
    return strdup(*absolute + len + (len > 1));
}

GitStatus git_calculate_status(GitRepository *repo, const char *path) {
    if (!repo || !repo->valid || !path || !*path || !git_index_refresh(repo))
        return GIT_STATUS_ERROR;
    char *absolute = NULL;
    char *rel = relative_path(repo->repo_root, path, &absolute);
    if (!rel) {
        free(absolute);
        return GIT_STATUS_ERROR;
    }
    GitIndexEntry *entry = git_index_lookup(repo->index_entries, rel);
    free(rel);
    if (!entry) {
        free(absolute);
        return GIT_STATUS_UNTRACKED;
    }
    GitStatus result = GIT_STATUS_MODIFIED;
    if (entry->flags & 0x3000 || entry->extended_flags & 0x2000)
        goto done;
    struct stat st;
    if (lstat(absolute, &st)) {
        result = errno == ENOENT ? GIT_STATUS_MODIFIED : GIT_STATUS_ERROR;
        goto done;
    }
    unsigned kind = entry->mode & 0170000;
    if ((kind == 0100000 && !S_ISREG(st.st_mode)) || (kind == 0120000 && !S_ISLNK(st.st_mode)))
        goto done;
    if (kind != 0100000 && kind != 0120000) {
        result = GIT_STATUS_ERROR;
        goto done;
    }
    if (kind == 0100000 && !!(entry->mode & 0111) != !!(st.st_mode & 0111))
        goto done;
    unsigned char digest[GIT_SHA1_SIZE];
    bool ok =
        S_ISLNK(st.st_mode) ? git_hash_symlink(absolute, digest) : git_hash_file(absolute, digest);
    result = !ok                                           ? GIT_STATUS_ERROR
             : memcmp(digest, entry->sha1, sizeof(digest)) ? GIT_STATUS_MODIFIED
                                                           : GIT_STATUS_UNMODIFIED;
done:
    free(absolute);
    return result;
}
