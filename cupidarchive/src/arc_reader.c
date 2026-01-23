#include "arc_reader.h"
#include "arc_tar.h"
#include "arc_zip.h"
#include "arc_compressed.h"
#include "arc_filter.h"

// Compression type constants (from arc_compressed.h)
#define ARC_COMPRESSED_GZIP  0
#define ARC_COMPRESSED_BZIP2 1
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>

// Forward declarations
static int detect_format(ArcStream *stream, ArcStream **decompressed, int *compression_type, const char *path);
static ArcReader *create_reader(ArcStream *stream, int format, const char *path, int compression_type, ArcStream *original_stream);


// Format types (must match arc_tar.c, arc_zip.c, and arc_compressed.c)
#define ARC_FORMAT_TAR 0
#define ARC_FORMAT_ZIP 1
#define ARC_FORMAT_COMPRESSED 2

int arc_next(ArcReader *reader, ArcEntry *entry) {
    if (!reader || !entry) {
        return -1;
    }
    // Check format field (first field in all readers)
    int format = ((int *)reader)[0];
    switch (format) {
        case ARC_FORMAT_TAR:
            return arc_tar_next(reader, entry);
        case ARC_FORMAT_ZIP:
            return arc_zip_next(reader, entry);
        case ARC_FORMAT_COMPRESSED:
            return arc_compressed_next(reader, entry);
        default:
            return -1;
    }
}

void arc_entry_free(ArcEntry *entry) {
    if (entry) {
        free(entry->path);
        free(entry->link_target);
        memset(entry, 0, sizeof(*entry));
    }
}

ArcStream *arc_open_data(ArcReader *reader) {
    if (!reader) {
        return NULL;
    }
    int format = ((int *)reader)[0];
    switch (format) {
        case ARC_FORMAT_TAR:
            return arc_tar_open_data(reader);
        case ARC_FORMAT_ZIP:
            return arc_zip_open_data(reader);
        case ARC_FORMAT_COMPRESSED:
            return arc_compressed_open_data(reader);
        default:
            return NULL;
    }
}

int arc_skip_data(ArcReader *reader) {
    if (!reader) {
        return -1;
    }
    int format = ((int *)reader)[0];
    switch (format) {
        case ARC_FORMAT_TAR:
            return arc_tar_skip_data(reader);
        case ARC_FORMAT_ZIP:
            return arc_zip_skip_data(reader);
        case ARC_FORMAT_COMPRESSED:
            return arc_compressed_skip_data(reader);
        default:
            return -1;
    }
}

void arc_close(ArcReader *reader) {
    if (reader) {
        int format = ((int *)reader)[0];
        switch (format) {
            case ARC_FORMAT_TAR:
                arc_tar_close(reader);
                break;
            case ARC_FORMAT_ZIP:
                arc_zip_close(reader);
                break;
            case ARC_FORMAT_COMPRESSED:
                arc_compressed_close(reader);
                break;
            default:
                // Unknown format, try all (one will fail gracefully)
                arc_tar_close(reader);
                arc_zip_close(reader);
                arc_compressed_close(reader);
                break;
        }
    }
}

ArcReader *arc_open_path(const char *path) {
    if (!path) {
        return NULL;
    }
    
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return NULL;
    }
    
    // Get file size for byte limit
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return NULL;
    }
    
    // Create stream with reasonable limit (10x file size for compressed archives)
    ArcStream *stream = arc_stream_from_fd(fd, st.st_size * 10);
    if (!stream) {
        close(fd);
        return NULL;
    }
    
    // Detect format and decompression
    ArcStream *decompressed = NULL;
    int compression_type = -1;
    int format = detect_format(stream, &decompressed, &compression_type, path);
    if (format < 0) {
        arc_stream_close(stream);
        return NULL;
    }
    
    // For compressed formats (TAR or single compressed files), we need to recreate the filter fresh
    // (the detection filter has already read ahead and can't seek back)
    // Note: detect_format may have closed decompressed already, so check compression_type instead
    if (compression_type >= 0 && (format == ARC_FORMAT_TAR || format == ARC_FORMAT_COMPRESSED)) {
        // If detect_format closed the filter, we need to recreate it
        if (decompressed) {
            arc_stream_close(decompressed);
            decompressed = NULL;
        }
        // Reset underlying stream to beginning (stream is the original file stream)
        // This is important because detect_format may have advanced the stream position
        // when reading the 512-byte TAR header for format detection
        if (arc_stream_seek(stream, 0, SEEK_SET) < 0) {
            arc_stream_close(stream);
            return NULL;
        }
        // Recreate filter starting from position 0
        // Use a large byte limit (10x file size) to allow for decompression expansion
        // The underlying stream already has this limit set, so we pass 0 to use it
        if (compression_type == ARC_COMPRESSED_GZIP) {
            decompressed = arc_filter_gzip(stream, 0); // 0 = use underlying stream's limit
        } else if (compression_type == ARC_COMPRESSED_BZIP2) {
            decompressed = arc_filter_bzip2(stream, 0); // 0 = use underlying stream's limit
        }
        if (!decompressed) {
            arc_stream_close(stream);
            return NULL;
        }
    }
    
    // Use decompressed stream if available
    ArcStream *final_stream = decompressed ? decompressed : stream;
    
    // For compressed format, we need to pass the original stream too
    // Store it temporarily (we'll handle it in create_reader)
    ArcStream *original_stream_for_compressed = (format == ARC_FORMAT_COMPRESSED) ? stream : NULL;
    
    // Create reader
    ArcReader *reader = create_reader(final_stream, format, path, compression_type, original_stream_for_compressed);
    if (!reader) {
        if (decompressed) {
            arc_stream_close(decompressed);
        }
        arc_stream_close(stream);
        return NULL;
    }
    
    return reader;
}

ArcReader *arc_open_stream(ArcStream *stream) {
    if (!stream) {
        return NULL;
    }
    
    // Detect format
    ArcStream *decompressed = NULL;
    int compression_type = -1;
    int format = detect_format(stream, &decompressed, &compression_type, NULL);
    if (format < 0) {
        return NULL;
    }
    
    ArcStream *final_stream = decompressed ? decompressed : stream;
    ArcStream *original_stream_for_compressed = (format == ARC_FORMAT_COMPRESSED) ? stream : NULL;
    return create_reader(final_stream, format, NULL, compression_type, original_stream_for_compressed);
}

// Detect archive format and compression
static int detect_format(ArcStream *stream, ArcStream **decompressed, int *compression_type, const char *path) {
    *decompressed = NULL;
    *compression_type = -1;
    
    // Keep track of the original underlying stream (before any filters)
    ArcStream *original_stream = stream;
    
    // Read first few bytes to detect compression
    uint8_t magic[4];
    int64_t pos = arc_stream_tell(stream);
    ssize_t n = arc_stream_read(stream, magic, sizeof(magic));
    if (n < 2) {
        return -1;
    }
    
    int detected_compression = -1;
    
    // Check for gzip (0x1f 0x8b)
    if (magic[0] == 0x1f && magic[1] == 0x8b) {
        detected_compression = ARC_COMPRESSED_GZIP;
        // Reset underlying stream to beginning before creating filter
        // (filters can't seek, so they start from current position)
        arc_stream_seek(original_stream, 0, SEEK_SET);
        *decompressed = arc_filter_gzip(original_stream, 0); // 0 = use stream's limit
        if (!*decompressed) {
            return -1;
        }
        stream = *decompressed;
        n = arc_stream_read(stream, magic, sizeof(magic));
        if (n < 2) {
            return -1;
        }
    }
    // Check for bzip2 (BZ)
    else if (magic[0] == 'B' && magic[1] == 'Z' && n >= 3 && magic[2] == 'h') {
        detected_compression = ARC_COMPRESSED_BZIP2;
        // Reset underlying stream to beginning before creating filter
        arc_stream_seek(original_stream, 0, SEEK_SET);
        *decompressed = arc_filter_bzip2(original_stream, 0);
        if (!*decompressed) {
            return -1;
        }
        stream = *decompressed;
        n = arc_stream_read(stream, magic, sizeof(magic));
        if (n < 2) {
            return -1;
        }
    }
    // Check for xz (FD 37 7A 58 5A 00)
    else if (magic[0] == 0xFD && magic[1] == 0x37 && n >= 4 &&
             magic[2] == 0x7A && magic[3] == 0x58) {
        // XZ not supported yet, but we could add it
        arc_stream_seek(stream, pos, SEEK_SET);
        return -1;
    } else {
        // No compression, reset position
        arc_stream_seek(stream, pos, SEEK_SET);
    }
    
    // Now detect format (after decompression if any)
    // Check for ZIP first (more specific signature)
    if (n >= 2 && magic[0] == 'P' && magic[1] == 'K') {
        // Could be ZIP - check for ZIP signatures
        if (n >= 4) {
            uint32_t sig = (uint32_t)magic[0] | ((uint32_t)magic[1] << 8) |
                          ((uint32_t)magic[2] << 16) | ((uint32_t)magic[3] << 24);
            if (sig == 0x04034b50 || sig == 0x06054b50 || sig == 0x02014b50) {
                // ZIP signature found
                arc_stream_seek(stream, pos, SEEK_SET);
                return ARC_FORMAT_ZIP;
            }
        }
    }
    
    // TAR: Check for ustar magic or old TAR format
    // Read first 512 bytes to check TAR header
    uint8_t header[512];
    pos = arc_stream_tell(stream);
    n = arc_stream_read(stream, header, sizeof(header));
    if (n == sizeof(header)) {
        // Check for ustar magic (at offset 257)
        if ((memcmp(header + 257, "ustar", 5) == 0) ||
            (memcmp(header + 257, "USTAR", 5) == 0) ||
            (header[0] != '\0' && isprint((unsigned char)header[0]))) {
            // For filter streams, we can't seek back, but that's OK
            // We'll create a fresh filter for the reader starting from position 0
            // For regular streams, reset to beginning
            if (detected_compression >= 0) {
                // Filter stream - can't seek, but we'll recreate it fresh
                // Close the detection filter since we'll create a new one
                // The underlying stream position may have advanced during detection,
                // so we need to reset it to 0 before creating the new filter
                arc_stream_close(*decompressed);
                *decompressed = NULL;
                // Reset original underlying stream to beginning
                arc_stream_seek(original_stream, 0, SEEK_SET);
            } else {
                // Regular stream - reset to beginning
                arc_stream_seek(stream, 0, SEEK_SET);
            }
            return ARC_FORMAT_TAR;
        }
    }
    
    // If we detected compression but it's not a known archive format,
    // treat it as a single compressed file
    if (detected_compression >= 0) {
        *compression_type = detected_compression;
        // Check if path suggests it's a TAR archive (e.g., .tar.gz, .tar.bz2)
        // If so, don't treat as single compressed file
        if (path) {
            const char *ext = strrchr(path, '.');
            if (ext) {
                // Check for .tar.gz, .tar.bz2, etc.
                if (strstr(path, ".tar.") != NULL || 
                    strcmp(ext, ".tgz") == 0 || 
                    strcmp(ext, ".tbz2") == 0 ||
                    strcmp(ext, ".txz") == 0) {
                    // Looks like a TAR archive, but we didn't detect TAR format
                    // This is an error - corrupted or unsupported
                    arc_stream_seek(stream, pos, SEEK_SET);
                    return -1;
                }
            }
        }
        arc_stream_seek(stream, pos, SEEK_SET);
        return ARC_FORMAT_COMPRESSED;
    }
    
    arc_stream_seek(stream, pos, SEEK_SET);
    return -1; // Unknown format
}

static ArcReader *create_reader(ArcStream *stream, int format, const char *path, int compression_type, ArcStream *original_stream) {
    switch (format) {
        case ARC_FORMAT_TAR:
            return arc_tar_open(stream);
        case ARC_FORMAT_ZIP:
            return arc_zip_open(stream);
        case ARC_FORMAT_COMPRESSED:
            // For compressed, stream is the decompressed filter, original_stream is the underlying stream
            ArcReader *reader = arc_compressed_open(stream, path, compression_type);
            if (reader && original_stream) {
                // Store original stream in reader for cleanup
                arc_compressed_set_original_stream(reader, original_stream);
            }
            return reader;
        default:
            return NULL;
    }
}

