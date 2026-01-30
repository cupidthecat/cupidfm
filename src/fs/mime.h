#ifndef MIME_H
#define MIME_H

#include <stdbool.h>
#include <stddef.h>

// MIME type support
extern const char *supported_mime_types[];
extern const size_t num_supported_mime_types;

bool is_supported_mime_type(const char *mime_type);
bool is_archive_file(const char *filename);
const char *get_file_emoji(const char *mime_type, const char *filename);

#endif // MIME_H
