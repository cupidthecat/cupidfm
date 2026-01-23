#define _POSIX_C_SOURCE 200809L
#include "arc_tar.h"
#include "arc_reader.h"
#include "arc_stream.h"
#include "arc_filter.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <ctype.h>

#define TAR_BLOCK_SIZE 512
#define TAR_NAME_SIZE 100
#define TAR_MODE_SIZE 8
#define TAR_UID_SIZE 8
#define TAR_GID_SIZE 8
#define TAR_SIZE_SIZE 12
#define TAR_MTIME_SIZE 12
#define TAR_CHKSUM_SIZE 8
#define TAR_TYPE_SIZE 1
#define TAR_LINKNAME_SIZE 100
#define TAR_MAGIC_SIZE 6
#define TAR_VERSION_SIZE 2
#define TAR_UNAME_SIZE 32
#define TAR_GNAME_SIZE 32
#define TAR_DEVMAJOR_SIZE 8
#define TAR_DEVMINOR_SIZE 8
#define TAR_PREFIX_SIZE 155

// TAR header structure (ustar)
struct TarHeader {
    char name[TAR_NAME_SIZE];
    char mode[TAR_MODE_SIZE];
    char uid[TAR_UID_SIZE];
    char gid[TAR_GID_SIZE];
    char size[TAR_SIZE_SIZE];
    char mtime[TAR_MTIME_SIZE];
    char chksum[TAR_CHKSUM_SIZE];
    char typeflag;
    char linkname[TAR_LINKNAME_SIZE];
    char magic[TAR_MAGIC_SIZE];
    char version[TAR_VERSION_SIZE];
    char uname[TAR_UNAME_SIZE];
    char gname[TAR_GNAME_SIZE];
    char devmajor[TAR_DEVMAJOR_SIZE];
    char devminor[TAR_DEVMINOR_SIZE];
    char prefix[TAR_PREFIX_SIZE];
    char padding[12];
};

// TAR type flags
#define TAR_REGTYPE   '0'
#define TAR_AREGTYPE  '\0'
#define TAR_LNKTYPE   '1'
#define TAR_SYMTYPE   '2'
#define TAR_CHRTYPE   '3'
#define TAR_BLKTYPE   '4'
#define TAR_DIRTYPE   '5'
#define TAR_FIFOTYPE  '6'
#define TAR_CONTTYPE  '7'
#define TAR_XHDTYPE   'x'  // pax extended header
#define TAR_XGLTYPE   'g'  // pax global header

// Format types (must match arc_reader.c)
#define ARC_FORMAT_TAR 0
#define ARC_FORMAT_ZIP 1

// TAR reader structure
// Note: ArcReader is actually a TarReader (they're the same)
typedef struct TarReader {
    int format;  // ARC_FORMAT_TAR
    ArcStream *stream;
    ArcEntry current_entry;
    bool entry_valid;
    int64_t entry_data_offset;
    int64_t entry_data_remaining;
    bool eof;
} TarReader;

// Helper: Parse octal number from TAR header field
static uint64_t parse_octal(const char *str, size_t len) {
    uint64_t val = 0;
    for (size_t i = 0; i < len && str[i] != '\0' && str[i] != ' '; i++) {
        if (str[i] >= '0' && str[i] <= '7') {
            val = val * 8 + (str[i] - '0');
        }
    }
    return val;
}

// Helper: Check if block is all zeros (end of archive)
static bool is_zero_block(const uint8_t *block) {
    for (size_t i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (block[i] != 0) {
            return false;
        }
    }
    return true;
}

// Helper: Verify TAR header checksum
static bool verify_checksum(const struct TarHeader *hdr) {
    uint32_t sum = 0;
    const uint8_t *bytes = (const uint8_t *)hdr;
    
    // Calculate sum treating chksum field as spaces
    for (size_t i = 0; i < sizeof(struct TarHeader); i++) {
        if (i >= offsetof(struct TarHeader, chksum) &&
            i < offsetof(struct TarHeader, chksum) + TAR_CHKSUM_SIZE) {
            sum += ' ';
        } else {
            sum += bytes[i];
        }
    }
    
    uint32_t stored = (uint32_t)parse_octal(hdr->chksum, TAR_CHKSUM_SIZE);
    return sum == stored;
}

// Helper: Read pax extended header
static int read_pax_header(ArcStream *stream, char **path_out, uint64_t *size_out) {
    // Read size field (first part of pax header)
    char size_str[20];
    ssize_t n = arc_stream_read(stream, size_str, sizeof(size_str) - 1);
    if (n < 0) {
        return -1;
    }
    size_str[n] = '\0';
    
    // Find space/newline
    char *space = strchr(size_str, ' ');
    if (!space) {
        space = strchr(size_str, '\n');
    }
    if (!space) {
        return -1;
    }
    *space = '\0';
    
    uint64_t header_size = parse_octal(size_str, sizeof(size_str));
    if (header_size == 0 || header_size > 1024 * 1024) { // Sanity check
        return -1;
    }
    
    // Allocate buffer for header
    char *header = malloc(header_size + 1);
    if (!header) {
        return -1;
    }
    
    // Read rest of header
    size_t to_read = header_size - (size_t)(space - size_str) - 1;
    n = arc_stream_read(stream, header, to_read);
    if (n < 0 || (size_t)n != to_read) {
        free(header);
        return -1;
    }
    header[header_size] = '\0';
    
    // Parse pax records (key=value\n format)
    char *p = header;
    while (*p) {
        char *eq = strchr(p, '=');
        if (!eq) {
            break;
        }
        *eq = '\0';
        char *key = p;
        char *value = eq + 1;
        char *nl = strchr(value, '\n');
        if (nl) {
            *nl = '\0';
            p = nl + 1;
        } else {
            p = value + strlen(value);
        }
        
        // Handle important pax attributes
        if (strcmp(key, "path") == 0) {
            *path_out = strdup(value);
        } else if (strcmp(key, "size") == 0) {
            *size_out = strtoull(value, NULL, 10);
        }
        // Add more pax attributes as needed
    }
    
    free(header);
    return 0;
}

// Read next TAR entry
static int tar_read_entry(struct TarReader *reader) {
    if (reader->eof) {
        return 1; // Done
    }
    
    // Skip any remaining entry data
    // For filter streams (gzip/bzip2), we can't seek, so read and discard
    if (reader->entry_data_remaining > 0) {
        // Try seeking first (works for regular streams)
        if (arc_stream_seek(reader->stream, reader->entry_data_remaining, SEEK_CUR) < 0) {
            // Seek failed (filter stream) - read and discard instead
            char discard[8192];
            int64_t remaining = reader->entry_data_remaining;
            while (remaining > 0) {
                size_t to_read = (remaining > (int64_t)sizeof(discard)) ? sizeof(discard) : (size_t)remaining;
                ssize_t n = arc_stream_read(reader->stream, discard, to_read);
                if (n <= 0) {
                    break; // EOF or error
                }
                remaining -= n;
            }
        }
        // Round up to block boundary
        int64_t skip = (TAR_BLOCK_SIZE - (reader->entry_data_remaining % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE;
        if (skip > 0) {
            if (arc_stream_seek(reader->stream, skip, SEEK_CUR) < 0) {
                // Seek failed - read and discard padding
                char discard[512];
                int64_t remaining = skip;
                while (remaining > 0) {
                    size_t to_read = (remaining > (int64_t)sizeof(discard)) ? sizeof(discard) : (size_t)remaining;
                    ssize_t n = arc_stream_read(reader->stream, discard, to_read);
                    if (n <= 0) {
                        break;
                    }
                    remaining -= n;
                }
            }
        }
    }
    
    // Read header block
    struct TarHeader hdr;
    memset(&hdr, 0, sizeof(hdr)); // Initialize to zero
    ssize_t n = arc_stream_read(reader->stream, &hdr, sizeof(hdr));
    if (n < 0) {
        return -1; // Read error
    }
    if (n == 0) {
        // EOF reached
        reader->eof = true;
        return 1; // Done
    }
    if (n != sizeof(hdr)) {
        // Partial read - this shouldn't happen for a valid TAR
        // This can happen if the filter stream isn't reading correctly
        return -1;
    }
    
    // Check for zero block (end of archive)
    if (is_zero_block((const uint8_t *)&hdr)) {
        reader->eof = true;
        return 1; // Done
    }
    
    // Verify checksum
    // Note: Checksum verification can fail if:
    // 1. The stream position is wrong (reading from wrong offset)
    // 2. The header is corrupted
    // 3. The filter stream isn't decompressing correctly
    if (!verify_checksum(&hdr)) {
        // Checksum failed - might be corrupted or not a TAR file
        // This can happen if the stream position is wrong or the header is corrupted
        // For debugging: check if we got any valid data
        if (hdr.name[0] == '\0' && is_zero_block((const uint8_t *)&hdr)) {
            // Actually a zero block, should have been caught earlier
            reader->eof = true;
            return 1;
        }
        // Checksum mismatch - likely stream position issue or corrupted data
        return -1;
    }
    
    // Free previous entry
    arc_entry_free(&reader->current_entry);
    memset(&reader->current_entry, 0, sizeof(reader->current_entry));
    
    // Handle pax extended headers
    char *pax_path = NULL;
    uint64_t pax_size = 0;
    
    if (hdr.typeflag == TAR_XHDTYPE || hdr.typeflag == TAR_XGLTYPE) {
        // Read pax header
        uint64_t pax_header_size = parse_octal(hdr.size, TAR_SIZE_SIZE);
        if (read_pax_header(reader->stream, &pax_path, &pax_size) < 0) {
            free(pax_path);
            return -1;
        }
        
        // Skip to next block
        int64_t skip = (TAR_BLOCK_SIZE - (pax_header_size % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE;
        if (skip > 0) {
            if (arc_stream_seek(reader->stream, skip, SEEK_CUR) < 0) {
                // Seek failed (filter stream) - read and discard padding
                char discard[512];
                int64_t remaining = skip;
                while (remaining > 0) {
                    size_t to_read = (remaining > (int64_t)sizeof(discard)) ? sizeof(discard) : (size_t)remaining;
                    ssize_t n = arc_stream_read(reader->stream, discard, to_read);
                    if (n <= 0) {
                        free(pax_path);
                        return -1;
                    }
                    remaining -= n;
                }
            }
        }
        
        // Read next header (actual entry)
        n = arc_stream_read(reader->stream, &hdr, sizeof(hdr));
        if (n < 0 || n != sizeof(hdr)) {
            free(pax_path);
            return -1;
        }
        if (!verify_checksum(&hdr)) {
            free(pax_path);
            return -1;
        }
    }
    
    // Parse entry
    uint64_t size = (pax_size > 0) ? pax_size : parse_octal(hdr.size, TAR_SIZE_SIZE);
    
    // Build path
    char path[TAR_PREFIX_SIZE + TAR_NAME_SIZE + 2];
    if (hdr.prefix[0] != '\0') {
        snprintf(path, sizeof(path), "%.*s/%.*s",
                 (int)TAR_PREFIX_SIZE, hdr.prefix,
                 (int)TAR_NAME_SIZE, hdr.name);
    } else {
        snprintf(path, sizeof(path), "%.*s",
                 (int)TAR_NAME_SIZE, hdr.name);
    }
    
    // Normalize path (remove leading ./ and //)
    char *normalized = path;
    while (normalized[0] == '.' && normalized[1] == '/') {
        normalized += 2;
    }
    while (normalized[0] == '/' && normalized[1] == '/') {
        normalized++;
    }
    
    reader->current_entry.path = pax_path ? pax_path : strdup(normalized);
    reader->current_entry.size = size;
    reader->current_entry.mode = (uint32_t)parse_octal(hdr.mode, TAR_MODE_SIZE);
    reader->current_entry.mtime = parse_octal(hdr.mtime, TAR_MTIME_SIZE);
    reader->current_entry.uid = (uint32_t)parse_octal(hdr.uid, TAR_UID_SIZE);
    reader->current_entry.gid = (uint32_t)parse_octal(hdr.gid, TAR_GID_SIZE);
    
    // Determine type
    if (hdr.typeflag == TAR_DIRTYPE || hdr.typeflag == TAR_REGTYPE || hdr.typeflag == TAR_AREGTYPE) {
        reader->current_entry.type = (hdr.typeflag == TAR_DIRTYPE) ? ARC_ENTRY_DIR : ARC_ENTRY_FILE;
    } else if (hdr.typeflag == TAR_SYMTYPE) {
        reader->current_entry.type = ARC_ENTRY_SYMLINK;
        reader->current_entry.link_target = strndup(hdr.linkname, TAR_LINKNAME_SIZE);
    } else if (hdr.typeflag == TAR_LNKTYPE) {
        reader->current_entry.type = ARC_ENTRY_HARDLINK;
        reader->current_entry.link_target = strndup(hdr.linkname, TAR_LINKNAME_SIZE);
    } else {
        reader->current_entry.type = ARC_ENTRY_OTHER;
    }
    
    reader->entry_valid = true;
    reader->entry_data_offset = arc_stream_tell(reader->stream);
    reader->entry_data_remaining = size;
    
    return 0;
}

// ArcReader vtable for TAR
int arc_tar_next(ArcReader *reader, ArcEntry *entry) {
    if (!reader || !entry) {
        return -1;
    }
    TarReader *tar = (TarReader *)reader;
    
    // If we have a valid entry with data remaining, we need to skip it first
    if (tar->entry_valid && tar->entry_data_remaining > 0) {
        // Skip the previous entry's data before reading next
        // For filter streams (gzip/bzip2), we can't seek, so read and discard
        if (arc_stream_seek(tar->stream, tar->entry_data_remaining, SEEK_CUR) < 0) {
            // Seek failed (filter stream) - read and discard instead
            char discard[8192];
            int64_t remaining = tar->entry_data_remaining;
            while (remaining > 0) {
                size_t to_read = (remaining > (int64_t)sizeof(discard)) ? sizeof(discard) : (size_t)remaining;
                ssize_t n = arc_stream_read(tar->stream, discard, to_read);
                if (n <= 0) {
                    break; // EOF or error
                }
                remaining -= n;
            }
        }
        // Round up to block boundary
        int64_t skip = (TAR_BLOCK_SIZE - (tar->entry_data_remaining % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE;
        if (skip > 0) {
            if (arc_stream_seek(tar->stream, skip, SEEK_CUR) < 0) {
                // Seek failed - read and discard padding
                char discard[512];
                int64_t remaining = skip;
                while (remaining > 0) {
                    size_t to_read = (remaining > (int64_t)sizeof(discard)) ? sizeof(discard) : (size_t)remaining;
                    ssize_t n = arc_stream_read(tar->stream, discard, to_read);
                    if (n <= 0) {
                        break;
                    }
                    remaining -= n;
                }
            }
        }
        tar->entry_data_remaining = 0;
        tar->entry_valid = false; // Mark previous entry as consumed
    }
    
    int ret = tar_read_entry(tar);
    if (ret == 0) {
        *entry = tar->current_entry;
        // Don't clear entry_valid here - keep it valid so arc_open_data() can work
        // The entry will be invalidated when we read the next entry or skip data
        // Clear the entry structure so it's not freed when reader closes
        // But keep entry_valid = true and entry_data_* so arc_open_data() works
        memset(&tar->current_entry, 0, sizeof(tar->current_entry));
        // Note: entry_valid stays true, entry_data_offset and entry_data_remaining are preserved
    }
    return ret;
}

ArcStream *arc_tar_open_data(ArcReader *reader) {
    if (!reader) {
        return NULL;
    }
    TarReader *tar = (TarReader *)reader;
    if (!tar->entry_valid || tar->entry_data_remaining == 0) {
        return NULL;
    }
    
    // Create substream for entry data
    // Note: We don't invalidate entry_valid here - it will be invalidated
    // when arc_next() is called again or arc_skip_data() is called
    return arc_stream_substream(tar->stream, tar->entry_data_offset, tar->entry_data_remaining);
}

int arc_tar_skip_data(ArcReader *reader) {
    if (!reader) {
        return -1;
    }
    TarReader *tar = (TarReader *)reader;
    if (!tar->entry_valid) {
        return -1;
    }
    
    // Seek past entry data (or read and discard for filter streams)
    if (arc_stream_seek(tar->stream, tar->entry_data_remaining, SEEK_CUR) < 0) {
        // Seek failed (filter stream) - read and discard instead
        char discard[8192];
        int64_t remaining = tar->entry_data_remaining;
        while (remaining > 0) {
            size_t to_read = (remaining > (int64_t)sizeof(discard)) ? sizeof(discard) : (size_t)remaining;
            ssize_t n = arc_stream_read(tar->stream, discard, to_read);
            if (n <= 0) {
                break; // EOF or error
            }
            remaining -= n;
        }
    }
    
    // Round up to block boundary
    int64_t skip = (TAR_BLOCK_SIZE - (tar->entry_data_remaining % TAR_BLOCK_SIZE)) % TAR_BLOCK_SIZE;
    if (skip > 0) {
        if (arc_stream_seek(tar->stream, skip, SEEK_CUR) < 0) {
            // Seek failed - read and discard padding
            char discard[512];
            int64_t remaining = skip;
            while (remaining > 0) {
                size_t to_read = (remaining > (int64_t)sizeof(discard)) ? sizeof(discard) : (size_t)remaining;
                ssize_t n = arc_stream_read(tar->stream, discard, to_read);
                if (n <= 0) {
                    break;
                }
                remaining -= n;
            }
        }
    }
    
    tar->entry_data_remaining = 0;
    tar->entry_valid = false; // Mark as invalid after skipping
    return 0;
}

void arc_tar_close(ArcReader *reader) {
    if (!reader) {
        return;
    }
    TarReader *tar = (TarReader *)reader;
    arc_entry_free(&tar->current_entry);
    arc_stream_close(tar->stream);
    free(tar);
}

// Note: Functions are now exported, no vtable needed

ArcReader *arc_tar_open(ArcStream *stream) {
    if (!stream) {
        return NULL;
    }
    TarReader *tar = calloc(1, sizeof(TarReader));
    if (!tar) {
        return NULL;
    }
    
    tar->format = ARC_FORMAT_TAR;
    tar->stream = stream;
    tar->entry_valid = false;
    tar->eof = false;
    
    // Cast to ArcReader (they're the same structure)
    ArcReader *reader = (ArcReader *)tar;
    
    // Format detection already verified this is a TAR file and reset the stream position.
    // For filter streams (gzip/bzip2), we can't seek, so we trust format detection.
    // Just ensure we start reading from the beginning - tar_read_entry will handle the first read.
    // No need to verify again here since format detection already did it.
    
    return reader;
}

