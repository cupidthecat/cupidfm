#define _POSIX_C_SOURCE 200809L
#include "arc_reader.h"
#include "arc_stream.h"
#include "arc_base.h"
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
#include <time.h>  // For futimens
#include <limits.h>
#include <libgen.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define EXTRACT_BUFFER_SIZE (64 * 1024) // 64KB buffer

/**
 * Validate archive entry path for security (prevent Zip-Slip attacks).
 * Rejects:
 * - Absolute paths (starting with /)
 * - Any component that is exactly ".."
 * - Components containing null bytes
 * 
 * @param path The path to validate
 * @return 0 if valid, -1 if invalid (sets errno to EINVAL)
 */
static int validate_entry_path(const char *path, const ArcLimits *limits) {
    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    // Enforce max name length (bytes)
    if (limits && limits->max_name > 0) {
        size_t n = strlen(path);
        if ((uint64_t)n > limits->max_name) {
            errno = EOVERFLOW;
            return -1;
        }
    }
    
    // Reject absolute paths
    if (path[0] == '/') {
        errno = EINVAL;
        return -1;
    }
    
    // Check each component
    const char *start = path;
    const char *p = path;
    uint64_t depth = 0;
    
    while (*p) {
        // Check for null bytes (shouldn't happen in normal strings, but be safe)
        if (*p == '\0') {
            break; // End of string
        }
        
        // Find component boundaries
        if (*p == '/') {
            // Check if this component is ".."
            size_t comp_len = p - start;
            if (comp_len == 2 && start[0] == '.' && start[1] == '.') {
                errno = EINVAL;
                return -1;
            }
            if (comp_len > 0) depth++;
            start = p + 1;
        } else {
            // Check if we're at the start of a ".." component
            if (p == start && p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) {
                errno = EINVAL;
                return -1;
            }
        }
        
        p++;
    }
    
    // Check final component (after last slash)
    if (p > start) {
        size_t comp_len = p - start;
        if (comp_len == 2 && start[0] == '.' && start[1] == '.') {
            errno = EINVAL;
            return -1;
        }
        if (comp_len > 0) depth++;
    }

    if (limits && limits->max_nested_depth > 0 && depth > limits->max_nested_depth) {
        errno = EOVERFLOW;
        return -1;
    }
    
    return 0;
}

/**
 * Create a directory and all parent directories using mkdirat (like mkdir -p).
 * Uses openat() with O_NOFOLLOW to avoid symlink races.
 * 
 * @param dirfd Directory file descriptor (or AT_FDCWD)
 * @param path Path relative to dirfd
 * @param mode Directory mode
 * @return 0 on success, -1 on error
 */
static int mkdir_p_at(int dirfd, const char *path, mode_t mode) {
    if (!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    
    // Handle absolute paths (shouldn't happen after validation, but be safe)
    if (path[0] == '/') {
        errno = EINVAL;
        return -1;
    }
    
    // Copy path for manipulation
    char tmp[PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    
    // Remove trailing slash
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
        len--;
    }
    
    if (len == 0) {
        return 0; // Empty path, nothing to create
    }
    
    // Create parent directories first
    char *p = tmp;
    int current_dirfd = dirfd;
    
    // Skip leading ./
    if (p[0] == '.' && p[1] == '/') {
        p += 2;
    }
    
    while (*p) {
        char *slash = strchr(p, '/');
        if (!slash) {
            // Last component
            break;
        }
        
        // Null-terminate at slash
        *slash = '\0';
        
        // Create this directory component
        int new_dirfd = openat(current_dirfd, p, O_DIRECTORY | O_NOFOLLOW | O_RDONLY);
        if (new_dirfd < 0) {
            if (errno == ENOENT) {
                // Directory doesn't exist, create it
                if (mkdirat(current_dirfd, p, mode) < 0 && errno != EEXIST) {
                    *slash = '/'; // Restore for error reporting
                    return -1;
                }
                // Open the directory we just created
                new_dirfd = openat(current_dirfd, p, O_DIRECTORY | O_NOFOLLOW | O_RDONLY);
                if (new_dirfd < 0) {
                    *slash = '/';
                    return -1;
                }
            } else {
                *slash = '/';
                return -1;
            }
        }
        
        // If we opened a new directory and it's not the original dirfd, close previous
        if (current_dirfd != dirfd && current_dirfd != AT_FDCWD) {
            close(current_dirfd);
        }
        current_dirfd = new_dirfd;
        
        // Restore slash and move past it
        *slash = '/';
        p = slash + 1;
    }
    
    // Create final directory
    if (*p) {
        // Check if it already exists
        int fd = openat(current_dirfd, p, O_DIRECTORY | O_NOFOLLOW | O_RDONLY);
        if (fd < 0) {
            if (errno == ENOENT) {
                if (mkdirat(current_dirfd, p, mode) < 0 && errno != EEXIST) {
                    if (current_dirfd != dirfd && current_dirfd != AT_FDCWD) {
                        close(current_dirfd);
                    }
                    return -1;
                }
            } else {
                if (current_dirfd != dirfd && current_dirfd != AT_FDCWD) {
                    close(current_dirfd);
                }
                return -1;
            }
        } else {
            close(fd);
        }
    }
    
    // Clean up if we opened intermediate directories
    if (current_dirfd != dirfd && current_dirfd != AT_FDCWD) {
        close(current_dirfd);
    }
    
    return 0;
}

/**
 * Extract a single file entry using openat() for security.
 * 
 * @param reader Archive reader
 * @param dirfd Destination directory file descriptor
 * @param filename Filename relative to dirfd (must be validated)
 * @param mode File mode
 * @param preserve_permissions Whether to preserve permissions
 * @return 0 on success, -1 on error
 */
static int extract_file_at(ArcReader *reader, int dirfd, const char *filename, mode_t mode, bool preserve_permissions) {
    ArcStream *data = arc_open_data(reader);
    if (!data) {
        errno = EIO;
        return -1;
    }
    
    // Create parent directories if needed
    char *last_slash = strrchr(filename, '/');
    if (last_slash) {
        // Extract parent directory path
        size_t parent_len = last_slash - filename;
        char parent[PATH_MAX];
        if (parent_len >= sizeof(parent)) {
            arc_stream_close(data);
            errno = ENAMETOOLONG;
            return -1;
        }
        strncpy(parent, filename, parent_len);
        parent[parent_len] = '\0';
        
        if (mkdir_p_at(dirfd, parent, 0755) < 0) {
            arc_stream_close(data);
            return -1;
        }
    }
    
    // Open destination file with O_NOFOLLOW to prevent symlink attacks
    int fd = openat(dirfd, filename, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 
                    preserve_permissions ? mode : 0644);
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
    
    if (n < 0) {
        close(fd);
        arc_stream_close(data);
        return -1; // Read error
    }
    
    // Close fd - attributes will be set separately using openat
    close(fd);
    arc_stream_close(data);
    
    return 0;
}

/**
 * Extract a directory entry using mkdirat() for security.
 * 
 * @param dirfd Destination directory file descriptor
 * @param filename Directory name relative to dirfd (must be validated)
 * @param mode Directory mode
 * @return 0 on success, -1 on error
 */
static int extract_directory_at(int dirfd, const char *filename, mode_t mode) {
    return mkdir_p_at(dirfd, filename, mode);
}

/**
 * Extract a symlink entry using symlinkat() for security.
 * 
 * @param dirfd Destination directory file descriptor
 * @param filename Link name relative to dirfd (must be validated)
 * @param link_target Symlink target
 * @return 0 on success, -1 on error
 */
static int extract_symlink_at(int dirfd, const char *filename, const char *link_target) {
    // Create parent directories if needed
    char *last_slash = strrchr(filename, '/');
    if (last_slash) {
        size_t parent_len = last_slash - filename;
        char parent[PATH_MAX];
        if (parent_len >= sizeof(parent)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        strncpy(parent, filename, parent_len);
        parent[parent_len] = '\0';
        
        if (mkdir_p_at(dirfd, parent, 0755) < 0) {
            return -1;
        }
    }
    
    // Remove existing file/symlink if it exists (use unlinkat with O_NOFOLLOW)
    unlinkat(dirfd, filename, 0);
    
    // Create symlink using symlinkat()
    if (symlinkat(link_target, dirfd, filename) < 0) {
        return -1;
    }
    
    return 0;
}

/**
 * Set file permissions and timestamps using file descriptor.
 * 
 * @param fd File descriptor
 * @param entry Archive entry
 * @param preserve_permissions Whether to preserve permissions
 * @param preserve_timestamps Whether to preserve timestamps
 * @return 0 on success, -1 on error
 */
static int set_file_attributes_fd(int fd, const ArcEntry *entry, bool preserve_permissions, bool preserve_timestamps) {
    if (preserve_permissions && entry->mode != 0) {
        if (fchmod(fd, entry->mode & 0777) < 0) {
            return -1;
        }
    }
    
    if (preserve_timestamps && entry->mtime != 0) {
        struct timespec times[2];
        times[0].tv_sec = entry->mtime;
        times[0].tv_nsec = 0;
        times[1].tv_sec = entry->mtime;
        times[1].tv_nsec = 0;
        if (futimens(fd, times) < 0) {
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
    
    const ArcLimits *limits = ((ArcReaderBase *)reader)->limits;

    // Validate entry path for security (prevent Zip-Slip attacks)
    if (validate_entry_path(entry->path, limits) < 0) {
        return -1;
    }
    
    // Open destination directory with O_NOFOLLOW to prevent symlink races
    int dirfd = open(dest_dir, O_DIRECTORY | O_NOFOLLOW | O_RDONLY);
    if (dirfd < 0) {
        return -1;
    }
    
    // Normalize path: remove leading ./
    const char *filename = entry->path;
    if (filename[0] == '.' && filename[1] == '/') {
        filename += 2;
    }
    
    int result = 0;
    int file_fd = -1;
    
    switch (entry->type) {
        case ARC_ENTRY_FILE:
            result = extract_file_at(reader, dirfd, filename, entry->mode, preserve_permissions);
            if (result == 0) {
                // Open file again to set attributes (with O_NOFOLLOW)
                file_fd = openat(dirfd, filename, O_RDWR | O_NOFOLLOW);
            }
            break;
            
        case ARC_ENTRY_DIR:
            result = extract_directory_at(dirfd, filename, entry->mode & 0777);
            if (result == 0) {
                // Open directory to set attributes
                file_fd = openat(dirfd, filename, O_DIRECTORY | O_NOFOLLOW | O_RDONLY);
            }
            break;
            
        case ARC_ENTRY_SYMLINK:
            if (entry->link_target) {
                result = extract_symlink_at(dirfd, filename, entry->link_target);
            } else {
                result = -1;
                errno = EINVAL;
            }
            break;
            
        case ARC_ENTRY_HARDLINK:
            // Hard links are tricky - we'd need to track inode mappings
            // For now, treat as regular file (extract the data)
            result = extract_file_at(reader, dirfd, filename, entry->mode, preserve_permissions);
            if (result == 0) {
                file_fd = openat(dirfd, filename, O_RDWR | O_NOFOLLOW);
            }
            // Note: We don't create the hard link here because the target
            // might not exist yet. A full implementation would need to
            // track inode mappings and create links in a second pass.
            break;
            
        default:
            // Skip unknown types
            close(dirfd);
            arc_skip_data(reader);
            return 0;
    }
    
    // Set attributes if extraction succeeded and we have a file descriptor
    if (result == 0 && file_fd >= 0 && entry->type != ARC_ENTRY_SYMLINK) {
        set_file_attributes_fd(file_fd, entry, preserve_permissions, preserve_timestamps);
        close(file_fd);
    }
    
    close(dirfd);
    return result;
}

int arc_extract_to_path(ArcReader *reader, const char *dest_dir, bool preserve_permissions, bool preserve_timestamps) {
    if (!reader || !dest_dir) {
        errno = EINVAL;
        return -1;
    }
    
    // Verify destination directory exists and open it with O_NOFOLLOW
    int dirfd = open(dest_dir, O_DIRECTORY | O_NOFOLLOW | O_RDONLY);
    if (dirfd < 0) {
        return -1;
    }
    
    // Verify it's actually a directory
    struct stat st;
    if (fstat(dirfd, &st) < 0) {
        close(dirfd);
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        close(dirfd);
        errno = ENOTDIR;
        return -1;
    }
    
    // Extract all entries
    ArcEntry entry;
    int error_count = 0;
    
    while (arc_next(reader, &entry) == 0) {
        // Use the single-entry extraction function (it will open its own dirfd)
        // We could optimize by reusing dirfd, but for simplicity we let each
        // extraction open its own to ensure it's still valid
        int result = arc_extract_entry(reader, &entry, dest_dir, preserve_permissions, preserve_timestamps);
        
        if (result < 0) {
            error_count++;
        }
        
        arc_entry_free(&entry);
    }
    
    close(dirfd);
    return (error_count > 0) ? -1 : 0;
}

