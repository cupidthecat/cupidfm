#include "git.h"
#include <stdio.h>
#include <stdlib.h>

static GitRepository *g_repo = NULL;

void git_init(void) {
    g_repo = NULL;
}

void git_cleanup(void) {
    if (g_repo) {
        git_repo_free(g_repo);
        g_repo = NULL;
    }
}

void git_update_directory(const char *directory) {
    // Free existing repository
    if (g_repo) {
        git_repo_free(g_repo);
        g_repo = NULL;
    }

    // Try to open repository at new directory
    if (directory) {
        g_repo = git_repo_open(directory);
        // Silently fail if not a git repository
    }
}

GitStatus git_query_file_status(const char *filepath) {
    if (!g_repo || !git_repo_is_valid(g_repo)) {
        return GIT_STATUS_ERROR;
    }

    if (!filepath) {
        return GIT_STATUS_ERROR;
    }

    return git_file_status(g_repo, filepath);
}

const char* git_status_to_emoji(GitStatus status) {
    return git_status_emoji(status);
}
