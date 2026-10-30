#ifndef MIME_H
#define MIME_H

#include <magic.h>
#include <stdbool.h>
#include <stddef.h>

/* Results are copied into caller-owned storage. Cache entries include flags and
 * file identity/size/mode/mtime/ctime; explicit reloads can clear the cache. */
const char *mime_file_cached(magic_t cookie, const char *path, int flags, char *result,
                             size_t capacity);
void mime_cache_clear(void);

// MIME type support
extern const char *supported_mime_types[];
extern const size_t num_supported_mime_types;

bool is_supported_mime_type(const char *mime_type);
bool is_archive_file(const char *filename);
const char *get_file_emoji(const char *mime_type, const char *filename);

#endif // MIME_H
