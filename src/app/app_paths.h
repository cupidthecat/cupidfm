#ifndef APP_PATHS_H
#define APP_PATHS_H

#include <stdbool.h>

#include "globals.h" // MAX_PATH_LENGTH

// Ensures the parent directory of path exists (mkdir -p one level). Returns true on success.
bool ensure_parent_dir_local(const char *path);

// Resolve a path relative to cwd into out (absolute if path already absolute).
void resolve_path_under_cwd(char out[MAX_PATH_LENGTH], const char *cwd, const char *path);

// Return pointer to basename portion of path (no allocations).
const char *basename_ptr(const char *p);

#endif // APP_PATHS_H
