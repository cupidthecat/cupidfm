#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <stdbool.h>

#include "globals.h" // MAX_PATH_LENGTH

// Ensures the parent directory of path exists (mkdir -p one level). Returns true on success.
bool ensure_parent_dir_local(const char *path);

// Resolve a path relative to cwd (absolute paths are retained). Aliasing is safe.
// On overflow, out is empty and errno is ENAMETOOLONG; paths are never truncated.
void resolve_path_under_cwd(char out[MAX_PATH_LENGTH], const char *cwd, const char *path);

typedef struct {
    char directory[MAX_PATH_LENGTH];
    char name[MAX_PATH_LENGTH]; // Empty for directory entries; otherwise the literal filename.
} AppStartupEntry;

// Resolve a directory, file, or broken link before terminal initialization.
// Checks the canonical parent, readability, and path limit without following a file's symlink.
bool app_startup_entry(AppStartupEntry *entry, const char *cwd, const char *path);

// Return pointer to basename portion of path (no allocations).
const char *basename_ptr(const char *p);

#endif // APP_PATHS_H
