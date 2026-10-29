#ifndef GIT_HASH_H
#define GIT_HASH_H

#include <stddef.h>
#include <stdbool.h>

#define GIT_SHA1_SIZE 20

// Compute git object hash for a file
// Format: sha1("blob " + filesize + "\0" + contents)
// Returns true on success, false on error
bool git_hash_file(const char *filepath, unsigned char sha1_out[GIT_SHA1_SIZE]);

// Compute git object hash for symlink
// Format: sha1("blob " + target_len + "\0" + target_path)
bool git_hash_symlink(const char *filepath, unsigned char sha1_out[GIT_SHA1_SIZE]);

#endif // GIT_HASH_H
