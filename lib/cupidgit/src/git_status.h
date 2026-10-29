#ifndef GIT_STATUS_H
#define GIT_STATUS_H

#include "../cupidgit.h"
#include "git_repo.h"

// Calculate git status for a file
GitStatus git_calculate_status(GitRepository *repo, const char *path);

#endif // GIT_STATUS_H
