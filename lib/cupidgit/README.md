# CupidGit

CupidGit is a read-only C library used by CupidFM. It discovers working-tree repositories, reads their indexes, and compares individual files with the index. It has no external library dependencies.

Build with `make`; run the regression suite with `make test` (Python 3 and Git are required for tests). Include `cupidgit.h` and link `libcupidgit.a`.

```c
#include "cupidgit.h"

GitRepository *repo = git_repo_open("/path/to/repo");
if (repo) {
    GitStatus status = git_file_status(repo, "/path/to/repo/file.txt");
    const char *indicator = git_status_emoji(status);
    /* Use status or indicator before releasing the repository. */
    (void)indicator;
    git_repo_free(repo);
}
```

`git_repo_open` accepts a path inside a working tree, including a linked worktree or a repository whose index does not exist yet. Relative status paths are resolved against the process working directory. The repository handle belongs to the caller; release it with `git_repo_free`.

Status values are `GIT_STATUS_UNTRACKED`, `GIT_STATUS_UNMODIFIED`, `GIT_STATUS_MODIFIED`, and `GIT_STATUS_ERROR`. Status compares the working file with the index, including content, executable bits, symlink targets, deletions, and unmerged entries. Regular files are hashed in bounded chunks; matching timestamps alone do not prove that content is unchanged. Index changes are detected between calls on the same handle.

The parser supports SHA-1 index versions 2, 3, and 4, including extended flags, compressed version 4 paths, and long names. It validates checksums, entry boundaries, padding, and extensions. Malformed or unsupported indexes fail instead of producing a partial clean result.

The library does not compare the index with HEAD, apply clean filters or line-ending conversion, interpret `.gitignore`, aggregate directory status, or inspect submodule contents. Bare repositories, SHA-256 repositories, split indexes, and sparse-directory indexes are unsupported. Unknown mandatory index extensions are rejected. Callers must serialize access to each handle.

Tests cover real Git indexes, linked worktrees, empty files, same-timestamp edits, symlinks, executable modes, deleted directories, live index refresh, long paths, and malformed indexes.

Licensed under GNU GPL v3. See the repository license.
