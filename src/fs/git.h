#ifndef GIT_H
#define GIT_H

#include "cupidgit.h"

// Initialize git integration
void git_init(void);

// Cleanup git integration
void git_cleanup(void);

// Update repository context when changing directories
void git_update_directory(const char *directory);

// Query git status for a file
GitStatus git_query_file_status(const char *filepath);

// Convert git status to emoji string
const char* git_status_to_emoji(GitStatus status);

#endif // GIT_H
