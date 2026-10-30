#define _POSIX_C_SOURCE 200809L
#include "arc_compressed.h"
#include "arc_base.h"
#include "arc_filter.h"
#include "arc_reader.h"
#include "arc_stream.h"
#include <errno.h>
#include <libgen.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// Format types (must match arc_reader.c)
#define ARC_FORMAT_TAR 0
#define ARC_FORMAT_ZIP 1
#define ARC_FORMAT_COMPRESSED 2

// Compressed file reader structure
typedef struct CompressedReader {
    ArcReaderBase base; // Must be first member for safe dispatch
    // Note: base.stream = decompressed (the stream we read from)
    //       base.owned_stream = original_stream (for closing)
    ArcStream *decompressed; // Decompressed stream (filter) - also in base.stream
    ArcEntry current_entry;  // Current entry data
    bool entry_valid;        // Whether entry data is available
    bool entry_returned;     // Whether we've returned the entry
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

ArcReader *arc_compressed_open(ArcStream *decompressed_stream, const char *original_path,
                               int compression_type) {
    if (!decompressed_stream) {
        return NULL;
    }

    CompressedReader *reader = calloc(1, sizeof(CompressedReader));
    if (!reader) {
        return NULL;
    }

    reader->base.format = ARC_FORMAT_COMPRESSED;
    (void)compression_type;
    // The caller supplies a fresh filter after format detection.
    reader->base.stream = decompressed_stream;
    reader->decompressed = decompressed_stream; // Keep for backward compatibility
    reader->entry_valid = false;
    reader->entry_returned = false;

    // Create virtual entry
    reader->current_entry.path = extract_base_filename(original_path);
    if (!reader->current_entry.path) {
        // Fallback if extraction fails
        reader->current_entry.path = strdup("file");
        if (!reader->current_entry.path) {
            // Memory allocation failed - can't create entry
            free(reader);
            return NULL;
        }
    }

    // Per-member footer sizes do not describe the whole decoded file;
    // gzip ISIZE also wraps at 2^32. Zero denotes an unknown streamed size.
    reader->current_entry.size = 0;
    reader->current_entry.mode = 0644; // Default
    reader->current_entry.mtime = 0;   // Unknown
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
    entry->link_target =
        comp->current_entry.link_target ? strdup(comp->current_entry.link_target) : NULL;
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
    return arc_stream_view(comp->decompressed);
}

int arc_compressed_skip_data(ArcReader *reader) {
    if (!reader) {
        return -1;
    }
    CompressedReader *comp = (CompressedReader *)reader;

    if (!comp->entry_valid) {
        return -1;
    }

    uint8_t buffer[65536];
    ssize_t n;
    while ((n = arc_stream_read(comp->decompressed, buffer, sizeof(buffer))) > 0) {
    }
    if (n < 0)
        return -1;
    comp->entry_valid = false;
    return 0;
}

void arc_compressed_set_original_stream(ArcReader *reader, ArcStream *original_stream) {
    if (!reader) {
        return;
    }
    CompressedReader *comp = (CompressedReader *)reader;
    comp->base.owned_stream = original_stream;
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
        comp->decompressed = NULL;
        comp->base.stream = NULL;
    }

    // Close underlying/original stream if owned
    if (comp->base.owned_stream) {
        arc_stream_close(comp->base.owned_stream);
        comp->base.owned_stream = NULL;
    }

    free(comp);
}
