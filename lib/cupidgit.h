#ifndef CUPIDGIT_H
#define CUPIDGIT_H

#include <stdbool.h>
#include <stddef.h>

// Opaque repository handle
typedef struct GitRepository GitRepository;

// Git file status enumeration
typedef enum {
    GIT_STATUS_UNTRACKED = 0,   // 🟢 Not in index
    GIT_STATUS_UNMODIFIED,      // ⚪ Tracked, no changes
    GIT_STATUS_MODIFIED,        // 🔴 Working tree differs from index
    GIT_STATUS_ERROR            // Error checking status
} GitStatus;

// Repository management
GitRepository* git_repo_open(const char *path);
void git_repo_free(GitRepository *repo);
bool git_repo_is_valid(GitRepository *repo);
const char* git_repo_root(GitRepository *repo);

// Status queries
GitStatus git_file_status(GitRepository *repo, const char *path);

// Utility
const char* git_status_emoji(GitStatus status);

#endif // CUPIDGIT_H
