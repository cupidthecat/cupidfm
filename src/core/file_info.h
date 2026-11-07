#ifndef FILE_INFO_H
#define FILE_INFO_H

#include <magic.h>
#include <stddef.h>

typedef struct {
    const char *label;
    char *value;
} FileInfoField;

typedef struct {
    FileInfoField fields[16];
    size_t count;
} FileInfo;

/* Initialize with zero. Reading replaces owned field values; clearing releases them. */
void file_info_read(FileInfo *info, const char *path, magic_t cookie);
void file_info_clear(FileInfo *info);

#endif
