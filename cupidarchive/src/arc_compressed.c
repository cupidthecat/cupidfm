#define _POSIX_C_SOURCE 200809L
#include "arc_compressed.h"
#include "arc_reader.h"
#include "arc_stream.h"
#include "arc_filter.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <libgen.h>
#include <zlib.h>

// Format types (must match arc_reader.c)
#define ARC_FORMAT_TAR 0
#define ARC_FORMAT_ZIP 1
#define ARC_FORMAT_COMPRESSED 2

// Compressed file reader structure
typedef struct CompressedReader {
    int format;  // ARC_FORMAT_COMPRESSED
    ArcStream *original_stream;    // Original compressed stream (needs to be closed)
    ArcStream *decompressed;        // Decompressed stream (filter)
    ArcEntry current_entry;        // Current entry data
    bool entry_valid;              // Whether entry data is available
    bool entry_returned;           // Whether we've returned the entry
    char *original_path;           // Original file path (for filename extraction)
    int compression_type;          // ARC_COMPRESSED_GZIP or ARC_COMPRESSED_BZIP2
    uint64_t uncompressed_size;    // Uncompressed size (if known, 0 = unknown)
} CompressedReader;

/**
 * Extract filename without compression extension.
 */
static char *extract_base_filename(const char *path) {
    if (!path) {
        return strdup("file");
    }
    
    // Get basename
    char *path_copy = strdup(path);
    if (!path_copy) {
        return NULL;
    }
    
    char *base = basename(path_copy);
    char *result = strdup(base);
    free(path_copy);
    
    if (!result) {
        return NULL;
    }
    
    // Remove compression extensions
    size_t len = strlen(result);
    if (len >= 3 && strcmp(result + len - 3, ".gz") == 0) {
        result[len - 3] = '\0';
    } else if (len >= 4 && strcmp(result + len - 4, ".bz2") == 0) {
        result[len - 4] = '\0';
    } else if (len >= 3 && strcmp(result + len - 3, ".xz") == 0) {
        result[len - 3] = '\0';
    }
    
    return result;
}

/**
 * Try to extract uncompressed size (ISIZE) from gzip footer.
 * ISIZE is stored in the last 4 bytes of the gzip file (little-endian uint32_t).
 * Returns 0 if size cannot be determined.
 */
static uint64_t extract_gzip_isize(ArcStream *original_stream) {
    if (!original_stream) {
        return 0;
    }
    
    // Save current position
    int64_t current_pos = arc_stream_tell(original_stream);
    if (current_pos < 0) {
        return 0;
    }
    
    // Seek to 4 bytes before end
    if (arc_stream_seek(original_stream, -4, SEEK_END) < 0) {
        // Seek failed, restore position and return unknown
        arc_stream_seek(original_stream, current_pos, SEEK_SET);
        return 0;
    }
    
    // Read ISIZE (4 bytes, little-endian)
    uint8_t isize_bytes[4];
    ssize_t n = arc_stream_read(original_stream, isize_bytes, 4);
    if (n != 4) {
        // Read failed, restore position and return unknown
        arc_stream_seek(original_stream, current_pos, SEEK_SET);
        return 0;
    }
    
    // Parse little-endian uint32_t
    uint32_t isize = (uint32_t)isize_bytes[0] |
                     ((uint32_t)isize_bytes[1] << 8) |
                     ((uint32_t)isize_bytes[2] << 16) |
                     ((uint32_t)isize_bytes[3] << 24);
    
    // Restore position
    arc_stream_seek(original_stream, current_pos, SEEK_SET);
    
    return (uint64_t)isize;
}

ArcReader *arc_compressed_open(ArcStream *decompressed_stream, const char *original_path, int compression_type) {
    if (!decompressed_stream) {
        return NULL;
    }
    
    CompressedReader *reader = calloc(1, sizeof(CompressedReader));
    if (!reader) {
        return NULL;
    }
    
    reader->format = ARC_FORMAT_COMPRESSED;
    // decompressed_stream is the filter stream from detect_format
    reader->decompressed = decompressed_stream;
    // We need to get the original stream from the filter's underlying stream
    // But filters don't expose this easily. For now, we'll rely on arc_open_path
    // to manage the original stream closure.
    reader->original_stream = NULL; // Will be set by caller if needed
    reader->compression_type = compression_type;
    reader->entry_valid = false;
    reader->entry_returned = false;
    reader->uncompressed_size = 0; // Unknown size (will try to extract for gzip)
    
    if (original_path) {
        reader->original_path = strdup(original_path);
    } else {
        reader->original_path = NULL;
    }
    
    // Create virtual entry
    reader->current_entry.path = extract_base_filename(original_path);
    if (!reader->current_entry.path) {
        // Fallback if extraction fails
        reader->current_entry.path = strdup("file");
        if (!reader->current_entry.path) {
            // Memory allocation failed - can't create entry
            free(reader->original_path);
            free(reader);
            return NULL;
        }
    }
    
    // Try to extract uncompressed size for gzip files
    // Note: This will be set later when original_stream is available
    reader->current_entry.size = 0; // Unknown size (will be updated if ISIZE available)
    reader->current_entry.mode = 0644; // Default
    reader->current_entry.mtime = 0; // Unknown
    reader->current_entry.type = ARC_ENTRY_FILE;
    reader->current_entry.link_target = NULL;
    reader->current_entry.uid = 0;
    reader->current_entry.gid = 0;
    reader->entry_valid = true;
    
    return (ArcReader *)reader;
}

int arc_compressed_next(ArcReader *reader, ArcEntry *entry) {
    if (!reader || !entry) {
        return -1;
    }
    CompressedReader *comp = (CompressedReader *)reader;
    
    if (!comp->entry_valid || comp->entry_returned) {
        return 1; // EOF
    }
    
    // Return the single virtual entry
    // Make a copy of the entry (including path, which will be freed by caller)
    memset(entry, 0, sizeof(ArcEntry));
    
    // Copy path (ensure we have a valid path)
    if (comp->current_entry.path) {
        entry->path = strdup(comp->current_entry.path);
        if (!entry->path) {
            // Memory allocation failed
            return -1;
        }
    } else {
        // Fallback: use "file" as default name
        entry->path = strdup("file");
        if (!entry->path) {
            return -1;
        }
    }
    
    entry->size = comp->current_entry.size;
    entry->mode = comp->current_entry.mode;
    entry->mtime = comp->current_entry.mtime;
    entry->type = comp->current_entry.type;
    entry->link_target = comp->current_entry.link_target ? strdup(comp->current_entry.link_target) : NULL;
    entry->uid = comp->current_entry.uid;
    entry->gid = comp->current_entry.gid;
    
    comp->entry_returned = true;
    
    // Don't clear current_entry yet - we might need it for open_data
    // It will be cleared when the reader is closed
    
    return 0;
}

ArcStream *arc_compressed_open_data(ArcReader *reader) {
    if (!reader) {
        return NULL;
    }
    CompressedReader *comp = (CompressedReader *)reader;
    
    if (!comp->entry_valid || !comp->decompressed) {
        return NULL;
    }
    
    // Return the decompressed stream directly
    // Note: We don't create a substream because we don't know the size
    // The stream will naturally end when decompression is complete
    return comp->decompressed;
}

int arc_compressed_skip_data(ArcReader *reader) {
    if (!reader) {
        return -1;
    }
    CompressedReader *comp = (CompressedReader *)reader;
    
    if (!comp->entry_valid) {
        return -1;
    }
    
    // For compressed files, we can't easily skip without decompressing
    // Just mark as invalid
    comp->entry_valid = false;
    return 0;
}

void arc_compressed_set_original_stream(ArcReader *reader, ArcStream *original_stream) {
    if (!reader) {
        return;
    }
    CompressedReader *comp = (CompressedReader *)reader;
    comp->original_stream = original_stream;
    
    // Try to extract ISIZE from gzip footer if available
    if (original_stream && comp->compression_type == ARC_COMPRESSED_GZIP) {
        uint64_t isize = extract_gzip_isize(original_stream);
        if (isize > 0) {
            comp->uncompressed_size = isize;
            comp->current_entry.size = isize;
        }
    }
    // Note: bzip2 doesn't store uncompressed size in the file footer
}

void arc_compressed_close(ArcReader *reader) {
    if (!reader) {
        return;
    }
    CompressedReader *comp = (CompressedReader *)reader;
    
    arc_entry_free(&comp->current_entry);
    
    // Close decompressed stream (filter) first
    if (comp->decompressed) {
        arc_stream_close(comp->decompressed);
    }
    
    // Close original stream if we have it
    // (filters don't close underlying streams, so we need to do it)
    if (comp->original_stream) {
        arc_stream_close(comp->original_stream);
    }
    
    free(comp->original_path);
    free(comp);
}

