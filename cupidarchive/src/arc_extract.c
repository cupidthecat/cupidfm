#define _POSIX_C_SOURCE 200809L
#include "arc_reader.h"
#include "arc_stream.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include <utime.h>
#include <limits.h>
#include <libgen.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define EXTRACT_BUFFER_SIZE (64 * 1024) // 64KB buffer

/**
 * Create a directory and all parent directories (like mkdir -p).
 */
static int mkdir_p(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    
    // Remove trailing slash
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    
    // Create parent directories
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    
    // Create final directory
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    
    return 0;
}

/**
 * Get the parent directory of a path.
 */
static char *get_parent_dir(const char *path) {
    static char parent[PATH_MAX];
    strncpy(parent, path, sizeof(parent) - 1);
    parent[sizeof(parent) - 1] = '\0';
    
    char *last_slash = strrchr(parent, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (parent[0] == '\0') {
            strcpy(parent, "/");
        }
    } else {
        strcpy(parent, ".");
    }
    
    return parent;
}

/**
 * Extract a single file entry.
 */
static int extract_file(ArcReader *reader, const char *dest_path, mode_t mode, bool preserve_permissions) {
    ArcStream *data = arc_open_data(reader);
    if (!data) {
        errno = EIO; // Set errno if arc_open_data fails
        return -1;
    }
    
    // Create parent directories
    char *parent = get_parent_dir(dest_path);
    if (mkdir_p(parent, 0755) < 0 && errno != EEXIST) {
        arc_stream_close(data);
        return -1;
    }
    
    // Open destination file
    int fd = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, preserve_permissions ? mode : 0644);
    if (fd < 0) {
        arc_stream_close(data);
        return -1;
    }
    
    // Copy data
    char buffer[EXTRACT_BUFFER_SIZE];
    ssize_t n;
    while ((n = arc_stream_read(data, buffer, sizeof(buffer))) > 0) {
        ssize_t written = write(fd, buffer, n);
        if (written != n) {
            close(fd);
            arc_stream_close(data);
            return -1;
        }
    }
    
    close(fd);
    arc_stream_close(data);
    
    if (n < 0) {
        return -1; // Read error
    }
    
    return 0;
}

/**
 * Extract a directory entry.
 */
static int extract_directory(const char *dest_path, mode_t mode) {
    return mkdir_p(dest_path, mode);
}

/**
 * Extract a symlink entry.
 */
static int extract_symlink(const char *dest_path, const char *link_target) {
    // Create parent directories
    const char *parent = get_parent_dir(dest_path);
    if (mkdir_p(parent, 0755) < 0 && errno != EEXIST) {
        return -1;
    }
    
    // Remove existing file/symlink if it exists
    unlink(dest_path);
    
    // Create symlink
    if (symlink(link_target, dest_path) < 0) {
        return -1;
    }
    
    return 0;
}

/**
 * Set file permissions and timestamps.
 */
static int set_file_attributes(const char *path, const ArcEntry *entry, bool preserve_permissions, bool preserve_timestamps) {
    if (preserve_permissions && entry->mode != 0) {
        if (chmod(path, entry->mode & 0777) < 0) {
            return -1;
        }
    }
    
    if (preserve_timestamps && entry->mtime != 0) {
        struct utimbuf times;
        times.actime = entry->mtime;
        times.modtime = entry->mtime;
        if (utime(path, &times) < 0) {
            return -1;
        }
    }
    
    return 0;
}

int arc_extract_entry(ArcReader *reader, const ArcEntry *entry, const char *dest_dir, bool preserve_permissions, bool preserve_timestamps) {
    if (!reader || !entry || !dest_dir) {
        errno = EINVAL;
        return -1;
    }
    
    // Build destination path
    char dest_path[PATH_MAX];
    snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, entry->path);
    
    // Remove leading ./ if present
    if (dest_path[0] == '.' && dest_path[1] == '/') {
        memmove(dest_path, dest_path + 2, strlen(dest_path) - 1);
    }
    
    int result = 0;
    
    switch (entry->type) {
        case ARC_ENTRY_FILE:
            result = extract_file(reader, dest_path, entry->mode, preserve_permissions);
            break;
            
        case ARC_ENTRY_DIR:
            result = extract_directory(dest_path, entry->mode & 0777);
            break;
            
        case ARC_ENTRY_SYMLINK:
            if (entry->link_target) {
                result = extract_symlink(dest_path, entry->link_target);
            } else {
                result = -1;
                errno = EINVAL;
            }
            break;
            
        case ARC_ENTRY_HARDLINK:
            // Hard links are tricky - we'd need to track inode mappings
            // For now, treat as regular file (extract the data)
            result = extract_file(reader, dest_path, entry->mode, preserve_permissions);
            // Note: We don't create the hard link here because the target
            // might not exist yet. A full implementation would need to
            // track inode mappings and create links in a second pass.
            break;
            
        default:
            // Skip unknown types
            arc_skip_data(reader);
            return 0;
    }
    
    if (result == 0 && entry->type != ARC_ENTRY_SYMLINK) {
        // Set attributes (symlinks handled separately)
        set_file_attributes(dest_path, entry, preserve_permissions, preserve_timestamps);
    }
    
    return result;
}

int arc_extract_to_path(ArcReader *reader, const char *dest_dir, bool preserve_permissions, bool preserve_timestamps) {
    if (!reader || !dest_dir) {
        errno = EINVAL;
        return -1;
    }
    
    // Verify destination directory exists and is writable
    struct stat st;
    if (stat(dest_dir, &st) < 0) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    
    // Reset reader to beginning (we'll need to add this function)
    // For now, we'll need to close and reopen, or add arc_rewind()
    // Actually, we can't easily reset - let's document that reader should be fresh
    
    // Build full destination paths and extract
    ArcEntry entry;
    int error_count = 0;
    
    while (arc_next(reader, &entry) == 0) {
        // Build destination path
        char dest_path[PATH_MAX];
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, entry.path);
        
        // Remove leading ./ if present
        if (dest_path[0] == '.' && dest_path[1] == '/') {
            memmove(dest_path, dest_path + 2, strlen(dest_path) - 1);
        }
        
        // Use the single-entry extraction function
        int result = arc_extract_entry(reader, &entry, dest_dir, preserve_permissions, preserve_timestamps);
        
        if (result < 0) {
            error_count++;
        }
        
        arc_entry_free(&entry);
    }
    
    return (error_count > 0) ? -1 : 0;
}

