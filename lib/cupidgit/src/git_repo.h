#ifndef GIT_REPO_INTERNAL_H
#define GIT_REPO_INTERNAL_H
#include "../cupidgit.h"
#include <sys/stat.h>

struct GitRepository {
    char *repo_root;
    char *git_dir;
    void *index_entries;
    struct stat index_stat;
    bool index_present;
    bool valid;
};

char *find_git_directory(const char *path);
bool is_bare_repository(const char *git_dir);
#endif
