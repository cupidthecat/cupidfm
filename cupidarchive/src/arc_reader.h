#ifndef ARC_READER_H
#define ARC_READER_H

#include "arc_stream.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Archive entry types.
 */
#define ARC_ENTRY_FILE     0
#define ARC_ENTRY_DIR     1
#define ARC_ENTRY_SYMLINK 2
#define ARC_ENTRY_HARDLINK 3
#define ARC_ENTRY_OTHER   4

/**
 * Archive entry structure.
 */
typedef struct ArcEntry {
    char     *path;        // Normalized path (allocated, caller must free)
    uint64_t  size;        // File size in bytes
    uint32_t  mode;        // File mode/permissions
    uint64_t  mtime;       // Modification time (Unix timestamp)
    uint8_t   type;        // Entry type (ARC_ENTRY_*)
    char     *link_target; // Symlink target (if applicable, allocated, caller must free)
    uint32_t  uid;         // User ID
    uint32_t  gid;         // Group ID
} ArcEntry;

/**
 * Archive reader structure (opaque).
 */
typedef struct ArcReader ArcReader;

/**
 * Open an archive from a file path.
 * Automatically detects format and compression.
 * 
 * @param path Path to archive file
 * @return New reader, or NULL on error
 */
ArcReader *arc_open_path(const char *path);

/**
 * Open an archive from a stream.
 * The stream must remain valid for the reader's lifetime.
 * 
 * @param stream Stream to read from
 * @return New reader, or NULL on error
 */
ArcReader *arc_open_stream(ArcStream *stream);

/**
 * Get the next entry in the archive.
 * 
 * @param reader The archive reader
 * @param entry Output structure (will be filled in)
 * @return 0 on success, 1 when done (no more entries), <0 on error
 * 
 * Note: The entry's path and link_target are allocated and must be freed
 *       by the caller (use arc_entry_free()).
 */
int arc_next(ArcReader *reader, ArcEntry *entry);

/**
 * Free an entry's allocated fields.
 * 
 * @param entry The entry to free
 */
void arc_entry_free(ArcEntry *entry);

/**
 * Open a stream for reading the current entry's data.
 * Only valid after a successful arc_next() call.
 * 
 * @param reader The archive reader
 * @return Stream for reading entry data, or NULL on error
 * 
 * Note: The stream is bounded to the entry's size.
 *       Caller must close the stream when done.
 */
ArcStream *arc_open_data(ArcReader *reader);

/**
 * Skip the current entry's data (fast path).
 * Only valid after a successful arc_next() call.
 * 
 * @param reader The archive reader
 * @return 0 on success, <0 on error
 */
int arc_skip_data(ArcReader *reader);

/**
 * Close and free an archive reader.
 * 
 * @param reader The reader to close
 */
void arc_close(ArcReader *reader);

/**
 * Extract all entries from an archive to a destination directory.
 * 
 * @param reader The archive reader (will be reset to beginning)
 * @param dest_dir Destination directory path (must exist)
 * @param preserve_permissions If true, preserve file permissions and ownership
 * @param preserve_timestamps If true, preserve modification times
 * @return 0 on success, <0 on error
 * 
 * Note: Creates subdirectories as needed. Overwrites existing files.
 *       Returns error if dest_dir doesn't exist or isn't writable.
 */
int arc_extract_to_path(ArcReader *reader, const char *dest_dir, bool preserve_permissions, bool preserve_timestamps);

/**
 * Extract a single entry from an archive.
 * 
 * @param reader The archive reader (must have current entry from arc_next())
 * @param entry The entry to extract (from arc_next())
 * @param dest_dir Destination directory path (must exist)
 * @param preserve_permissions If true, preserve file permissions and ownership
 * @param preserve_timestamps If true, preserve modification times
 * @return 0 on success, <0 on error
 * 
 * Note: Creates parent directories as needed. Overwrites existing files.
 *       Must be called immediately after arc_next() while the entry data is available.
 */
int arc_extract_entry(ArcReader *reader, const ArcEntry *entry, const char *dest_dir, bool preserve_permissions, bool preserve_timestamps);

#endif // ARC_READER_H

