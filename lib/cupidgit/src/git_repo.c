#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include "git_repo.h"
#include "git_index.h"
#include "git_status.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Find .git directory by walking up from path
char *find_git_directory(const char *path) {
    char current[PATH_MAX];
    char git_path[PATH_MAX];
    struct stat st;

    // Get absolute path
    if (!realpath(path, current)) {
        return NULL;
    }

    // Walk up directory tree
    while (1) {
        // Check for .git in current directory
        if (snprintf(git_path, sizeof(git_path), "%s/.git", current) >= (int)sizeof(git_path))
            return NULL;

        if (stat(git_path, &st) == 0 && (S_ISDIR(st.st_mode) || S_ISREG(st.st_mode))) {
            return strdup(current); // Found repo root
        }

        // Check if we've reached root
        if (strcmp(current, "/") == 0) {
            break;
        }

        // Move to parent directory
        char *last_slash = strrchr(current, '/');
        if (!last_slash) {
            break;
        }

        if (last_slash == current) {
            strcpy(current, "/");
        } else {
            *last_slash = '\0';
        }
    }

    return NULL; // No .git found
}

// Check if directory is a bare repository
bool is_bare_repository(const char *git_dir) {
    char path[PATH_MAX];
    struct stat st;

    // Bare repos have HEAD, objects, refs in the directory itself
    snprintf(path, sizeof(path), "%s/HEAD", git_dir);
    if (stat(path, &st) != 0)
        return false;

    snprintf(path, sizeof(path), "%s/objects", git_dir);
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
        return false;

    snprintf(path, sizeof(path), "%s/refs", git_dir);
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode))
        return false;

    return true;
}

GitRepository *git_repo_open(const char *path) {
    if (!path)
        return NULL;

    char *repo_root = find_git_directory(path);
    if (!repo_root) {
        return NULL; // Not a git repository
    }

    // If we found .git directory, it's a normal repository (not bare)
    // Bare repos don't have .git subdirectory, so no need to check

    // Allocate repository structure
    GitRepository *repo = calloc(1, sizeof(GitRepository));
    if (!repo) {
        free(repo_root);
        return NULL;
    }

    repo->repo_root = repo_root;
    size_t git_len = strlen(repo_root) + 6;
    repo->git_dir = malloc(git_len);
    if (!repo->git_dir) {
        git_repo_free(repo);
        return NULL;
    }
    snprintf(repo->git_dir, git_len, "%s/.git", repo_root);
    struct stat st;
    if (stat(repo->git_dir, &st) != 0) {
        git_repo_free(repo);
        return NULL;
    }
    if (S_ISREG(st.st_mode)) {
        FILE *fp = fopen(repo->git_dir, "r");
        char *line = NULL;
        size_t cap = 0;
        ssize_t len = fp ? getline(&line, &cap, fp) : -1;
        if (fp)
            fclose(fp);
        if (len < 9 || strncmp(line, "gitdir: ", 8) != 0) {
            free(line);
            git_repo_free(repo);
            return NULL;
        }
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = 0;
        char *target = NULL;
        if (line[8] == '/')
            target = strdup(line + 8);
        else {
            size_t n = strlen(repo_root) + strlen(line + 8) + 2;
            target = malloc(n);
            if (target)
                snprintf(target, n, "%s/%s", repo_root, line + 8);
        }
        free(line);
        free(repo->git_dir);
        repo->git_dir = target ? realpath(target, NULL) : NULL;
        free(target);
        if (!repo->git_dir) {
            git_repo_free(repo);
            return NULL;
        }
    }
    size_t head_len = strlen(repo->git_dir) + 6;
    char *head = malloc(head_len);
    if (!head) {
        git_repo_free(repo);
        return NULL;
    }
    snprintf(head, head_len, "%s/HEAD", repo->git_dir);
    bool has_head = stat(head, &st) == 0 && S_ISREG(st.st_mode);
    free(head);
    if (!has_head) {
        git_repo_free(repo);
        return NULL;
    }
    repo->valid = true;
    repo->index_entries = NULL;

    // Parse git index
    if (!git_index_parse(repo)) {
        git_repo_free(repo);
        return NULL;
    }

    return repo;
}

void git_repo_free(GitRepository *repo) {
    if (!repo)
        return;

    free(repo->repo_root);
    free(repo->git_dir);
    if (repo->index_entries) {
        git_index_free(repo->index_entries);
    }
    free(repo);
}

bool git_repo_is_valid(GitRepository *repo) {
    return repo && repo->valid;
}

const char *git_repo_root(GitRepository *repo) {
    if (!repo || !repo->valid)
        return NULL;
    return repo->repo_root;
}

GitStatus git_file_status(GitRepository *repo, const char *path) {
    return git_calculate_status(repo, path);
}

const char *git_status_emoji(GitStatus status) {
    switch (status) {
        case GIT_STATUS_MODIFIED:
            return "🔴";
        case GIT_STATUS_UNTRACKED:
            return "🟢";
        case GIT_STATUS_UNMODIFIED:
            return "⚪";
        case GIT_STATUS_ERROR:
            return "";
        default:
            return "";
    }
}
