#define _POSIX_C_SOURCE 200809L
#include "arc_zip.h"
#include "arc_reader.h"
#include "arc_stream.h"
#include "arc_filter.h"
#include "arc_base.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <zlib.h>
#include <stdint.h>
#include <limits.h>

// Note: Security/resource limits are provided via ArcLimits (ArcReaderBase.limits).

// ZIP constants
#define ZIP_LOCAL_FILE_HEADER_SIG   0x04034b50  // "PK\03\04"
#define ZIP_CENTRAL_DIR_SIG         0x02014b50  // "PK\01\02"
#define ZIP_END_OF_CENTRAL_DIR_SIG  0x06054b50  // "PK\05\06"
#define ZIP_END_OF_CENTRAL_DIR64_SIG 0x06064b50 // "PK\06\06"
#define ZIP_END_OF_CENTRAL_DIR64_LOCATOR_SIG 0x07064b50 // "PK\07\06"

// ZIP64 Extended Information Extra Field ID
#define ZIP64_EXTRA_FIELD_ID 0x0001

// ZIP compression methods
#define ZIP_METHOD_STORE   0
#define ZIP_METHOD_DEFLATE 8

// ZIP general purpose bit flags
#define ZIP_FLAG_ENCRYPTED 0x0001
#define ZIP_FLAG_DATA_DESCRIPTOR 0x0008

// ZIP Central Directory File Header structure (variable size)
// We'll read it field by field
struct ZipCentralDirEntry {
    uint32_t signature;           // 0x02014b50
    uint16_t version_made_by;
    uint16_t version_needed;
    uint16_t flags;
    uint16_t compression_method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t compressed_size;
    uint32_t uncompressed_size;
    uint16_t filename_length;
    uint16_t extra_field_length;
    uint16_t comment_length;
    uint16_t disk_number;
    uint16_t internal_attrs;
    uint32_t external_attrs;
    uint32_t local_header_offset;
    char *filename;               // Allocated
    uint8_t *extra_field;         // Allocated (optional)
    char *comment;                 // Allocated (optional)
    
    // ZIP64 extended fields (from extra field)
    uint64_t zip64_compressed_size;
    uint64_t zip64_uncompressed_size;
    uint64_t zip64_local_header_offset;
    bool has_zip64_fields;
};

// ZIP End of Central Directory structure
struct ZipEOCD {
    uint32_t signature;           // 0x06054b50
    uint16_t disk_number;
    uint16_t central_dir_disk;
    uint16_t central_dir_records_on_disk;
    uint16_t total_central_dir_records;
    uint32_t central_dir_size;
    uint32_t central_dir_offset;
    uint16_t comment_length;
    char *comment;                 // Allocated (optional)
    
    // ZIP64 indicators (0xFFFF means use ZIP64)
    bool is_zip64;
};

// ZIP64 End of Central Directory Locator
struct Zip64EOCDLocator {
    uint32_t signature;           // 0x07064b50
    uint32_t disk_with_zip64_eocd;
    uint64_t zip64_eocd_offset;
    uint32_t total_disks;
};

// ZIP64 End of Central Directory Record
struct Zip64EOCDRecord {
    uint32_t signature;           // 0x06064b50
    uint64_t zip64_eocd_size;     // Size of this record - 12
    uint16_t version_made_by;
    uint16_t version_needed;
    uint32_t disk_number;
    uint32_t central_dir_disk;
    uint64_t central_dir_records_on_disk;
    uint64_t total_central_dir_records;
    uint64_t central_dir_size;
    uint64_t central_dir_offset;
};

// Format types (must match arc_reader.c and arc_tar.c)
#define ARC_FORMAT_TAR 0
#define ARC_FORMAT_ZIP 1

// ZIP reader structure
typedef struct ZipReader {
    ArcReaderBase base;  // Must be first member for safe dispatch
    ArcEntry current_entry;
    bool entry_valid;
    int64_t entry_data_offset;
    int64_t entry_data_remaining;
    uint64_t entry_uncompressed_size;  // Store separately since current_entry is cleared
    uint16_t entry_compression_method;
    uint16_t entry_flags;
    bool eof;
    
    // Reading mode
    bool streaming_mode;  // true = parse local headers, false = use central directory
    
    // Central directory (used when streaming_mode = false)
    struct ZipCentralDirEntry *entries;
    size_t entry_count;
    size_t current_entry_index;
    int64_t central_dir_offset;
    
    // Streaming mode (used when streaming_mode = true)
    int64_t stream_pos;  // Current position in stream for local header parsing
    struct ZipCentralDirEntry *stream_entries;  // Dynamically built entry list
    size_t stream_entry_count;
    size_t stream_entry_capacity;
} ZipReader;

// Helper: Read little-endian uint16_t
static uint16_t read_le16(const uint8_t *data) {
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

// Helper: Read little-endian uint32_t
static uint32_t read_le32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

// Helper: Read little-endian uint64_t
static uint64_t read_le64(const uint8_t *data) {
    uint64_t low = read_le32(data);
    uint64_t high = read_le32(data + 4);
    return low | (high << 32);
}

// Helper: Parse ZIP64 Extended Information Extra Field
static int parse_zip64_extra_field(const uint8_t *extra_field, size_t extra_field_length,
                                    struct ZipCentralDirEntry *entry) {
    entry->has_zip64_fields = false;
    entry->zip64_compressed_size = 0;
    entry->zip64_uncompressed_size = 0;
    entry->zip64_local_header_offset = 0;
    
    if (!extra_field || extra_field_length < 4) {
        return 0; // No extra field or too small
    }
    
    size_t pos = 0;
    while (pos + 4 <= extra_field_length) {
        uint16_t header_id = read_le16(extra_field + pos);
        uint16_t data_size = read_le16(extra_field + pos + 2);
        pos += 4;
        
        if (pos + data_size > extra_field_length) {
            break; // Invalid size
        }
        
        if (header_id == ZIP64_EXTRA_FIELD_ID) {
            // ZIP64 Extended Information Extra Field
            size_t data_pos = 0;
            
            // Uncompressed size (if standard field is 0xFFFFFFFF)
            if (entry->uncompressed_size == 0xFFFFFFFF && data_pos + 8 <= data_size) {
                entry->zip64_uncompressed_size = read_le64(extra_field + pos + data_pos);
                data_pos += 8;
                entry->has_zip64_fields = true;
            }
            
            // Compressed size (if standard field is 0xFFFFFFFF)
            if (entry->compressed_size == 0xFFFFFFFF && data_pos + 8 <= data_size) {
                entry->zip64_compressed_size = read_le64(extra_field + pos + data_pos);
                data_pos += 8;
            }
            
            // Local header offset (if standard field is 0xFFFFFFFF)
            if (entry->local_header_offset == 0xFFFFFFFF && data_pos + 8 <= data_size) {
                entry->zip64_local_header_offset = read_le64(extra_field + pos + data_pos);
                data_pos += 8;
            }
            
            return 0; // Found ZIP64 field
        }
        
        pos += data_size;
    }
    
    return 0; // ZIP64 field not found (not an error)
}

// Helper: Find ZIP64 End of Central Directory Locator
static int find_zip64_locator(ArcStream *stream, int64_t eocd_pos, struct Zip64EOCDLocator *locator) {
    // Locator is immediately before EOCD (20 bytes before)
    int64_t locator_pos = eocd_pos - 20;
    if (locator_pos < 0) {
        return -1;
    }
    
    if (arc_stream_seek(stream, locator_pos, SEEK_SET) < 0) {
        return -1;
    }
    
    uint8_t buffer[20];
    ssize_t n = arc_stream_read(stream, buffer, sizeof(buffer));
    if (n != sizeof(buffer)) {
        return -1;
    }
    
    uint32_t sig = read_le32(buffer);
    if (sig != ZIP_END_OF_CENTRAL_DIR64_LOCATOR_SIG) {
        return -1; // Not a ZIP64 archive
    }
    
    locator->signature = sig;
    locator->disk_with_zip64_eocd = read_le32(buffer + 4);
    locator->zip64_eocd_offset = read_le64(buffer + 8);
    locator->total_disks = read_le32(buffer + 16);
    
    return 0;
}

// Helper: Read ZIP64 End of Central Directory Record
static int read_zip64_eocd(ArcStream *stream, int64_t offset, struct Zip64EOCDRecord *eocd64) {
    if (arc_stream_seek(stream, offset, SEEK_SET) < 0) {
        return -1;
    }
    
    uint8_t buffer[56]; // Minimum ZIP64 EOCD size
    ssize_t n = arc_stream_read(stream, buffer, sizeof(buffer));
    if (n < 56) {
        return -1;
    }
    
    uint32_t sig = read_le32(buffer);
    if (sig != ZIP_END_OF_CENTRAL_DIR64_SIG) {
        return -1;
    }
    
    eocd64->signature = sig;
    eocd64->zip64_eocd_size = read_le64(buffer + 4);
    eocd64->version_made_by = read_le16(buffer + 12);
    eocd64->version_needed = read_le16(buffer + 14);
    eocd64->disk_number = read_le32(buffer + 16);
    eocd64->central_dir_disk = read_le32(buffer + 20);
    eocd64->central_dir_records_on_disk = read_le64(buffer + 24);
    eocd64->total_central_dir_records = read_le64(buffer + 32);
    eocd64->central_dir_size = read_le64(buffer + 40);
    eocd64->central_dir_offset = read_le64(buffer + 48);
    
    return 0;
}

// Helper: Find End of Central Directory by scanning backwards
static int find_eocd(ArcStream *stream, struct ZipEOCD *eocd, struct Zip64EOCDRecord *eocd64_out, const ArcLimits *limits) {
    // Get stream size (if available)
    int64_t stream_size = -1;
    int64_t current_pos = arc_stream_tell(stream);
    
    // Try to seek to end to get size
    if (arc_stream_seek(stream, 0, SEEK_END) == 0) {
        stream_size = arc_stream_tell(stream);
        arc_stream_seek(stream, current_pos, SEEK_SET);
    }
    
    if (stream_size < 0) {
        // Can't determine size, try reading from current position
        // This is a limitation - we need seekable streams for ZIP
        errno = ESPIPE;
        return -1;
    }
    
    // EOCD is at most 65535 + 22 bytes from end (max comment length + structure)
    // Scan backwards from end
    int64_t max_scan = 65535 + 22;
    int64_t start_pos = stream_size - max_scan;
    if (start_pos < 0) {
        start_pos = 0;
    }
    
    uint8_t buffer[65535 + 22];
    int64_t scan_size = stream_size - start_pos;
    
    if (arc_stream_seek(stream, start_pos, SEEK_SET) < 0) {
        return -1;
    }
    
    ssize_t n = arc_stream_read(stream, buffer, scan_size);
    if (n < 22) {
        return -1; // Too small to contain EOCD
    }
    
    // Search backwards for EOCD signature
    int64_t eocd_file_pos = -1;
    for (ssize_t i = n - 22; i >= 0; i--) {
        uint32_t sig = read_le32(buffer + i);
        if (sig == ZIP_END_OF_CENTRAL_DIR_SIG) {
            // Found EOCD
            eocd_file_pos = start_pos + i;
            const uint8_t *p = buffer + i;
            eocd->signature = read_le32(p);
            eocd->disk_number = read_le16(p + 4);
            eocd->central_dir_disk = read_le16(p + 6);
            eocd->central_dir_records_on_disk = read_le16(p + 8);
            eocd->total_central_dir_records = read_le16(p + 10);
            eocd->central_dir_size = read_le32(p + 12);
            eocd->central_dir_offset = read_le32(p + 16);
            eocd->comment_length = read_le16(p + 20);
            
            // Security: Validate comment length (use max_extra as a generic bound)
            if (limits && limits->max_extra > 0 && (uint64_t)eocd->comment_length > limits->max_extra) {
                errno = EOVERFLOW;
                return -1;
            }
            
            // Read comment if present
            if (eocd->comment_length > 0 && i + 22 + eocd->comment_length <= n) {
                eocd->comment = malloc(eocd->comment_length + 1);
                if (eocd->comment) {
                    memcpy(eocd->comment, p + 22, eocd->comment_length);
                    eocd->comment[eocd->comment_length] = '\0';
                }
            } else {
                eocd->comment = NULL;
            }
            
            // Check if this is a ZIP64 archive
            // ZIP64 is indicated by:
            // - total_central_dir_records == 0xFFFF
            // - central_dir_size == 0xFFFFFFFF
            // - central_dir_offset == 0xFFFFFFFF
            // - disk_number == 0xFFFF
            eocd->is_zip64 = (eocd->total_central_dir_records == 0xFFFF ||
                             eocd->central_dir_size == 0xFFFFFFFF ||
                             eocd->central_dir_offset == 0xFFFFFFFF ||
                             eocd->disk_number == 0xFFFF);
            
            // If ZIP64, read the ZIP64 EOCD
            if (eocd->is_zip64 && eocd64_out) {
                struct Zip64EOCDLocator locator;
                if (find_zip64_locator(stream, eocd_file_pos, &locator) == 0) {
                    if (read_zip64_eocd(stream, locator.zip64_eocd_offset, eocd64_out) == 0) {
                        // Use ZIP64 values
                        eocd->central_dir_offset = (uint32_t)eocd64_out->central_dir_offset;
                        eocd->central_dir_size = (uint32_t)eocd64_out->central_dir_size;
                        eocd->total_central_dir_records = (uint16_t)eocd64_out->total_central_dir_records;
                    }
                }
            }
            
            return 0;
        }
    }
    
    return -1; // EOCD not found
}

// Helper: Free central directory entry
static void free_central_dir_entry(struct ZipCentralDirEntry *entry) {
    if (entry) {
        free(entry->filename);
        free(entry->extra_field);
        free(entry->comment);
    }
}

// Helper: Read central directory entry
static int read_central_dir_entry(ArcStream *stream, struct ZipCentralDirEntry *entry, const ArcLimits *limits) {
    uint8_t header[46]; // Fixed part of central directory header
    
    ssize_t n = arc_stream_read(stream, header, sizeof(header));
    if (n != sizeof(header)) {
        return -1;
    }
    
    entry->signature = read_le32(header);
    if (entry->signature != ZIP_CENTRAL_DIR_SIG) {
        return -1;
    }
    
    entry->version_made_by = read_le16(header + 4);
    entry->version_needed = read_le16(header + 6);
    entry->flags = read_le16(header + 8);
    entry->compression_method = read_le16(header + 10);
    entry->mod_time = read_le16(header + 12);
    entry->mod_date = read_le16(header + 14);
    entry->crc32 = read_le32(header + 16);
    entry->compressed_size = read_le32(header + 20);
    entry->uncompressed_size = read_le32(header + 24);
    entry->filename_length = read_le16(header + 28);
    entry->extra_field_length = read_le16(header + 30);
    entry->comment_length = read_le16(header + 32);
    entry->disk_number = read_le16(header + 34);
    entry->internal_attrs = read_le16(header + 36);
    entry->external_attrs = read_le32(header + 38);
    entry->local_header_offset = read_le32(header + 42);
    
    // Security: Validate field lengths to prevent excessive allocations
    if (limits && limits->max_name > 0 && (uint64_t)entry->filename_length > limits->max_name) {
        errno = EOVERFLOW;
        return -1;
    }
    if (limits && limits->max_extra > 0 && (uint64_t)entry->extra_field_length > limits->max_extra) {
        errno = EOVERFLOW;
        return -1;
    }
    if (limits && limits->max_extra > 0 && (uint64_t)entry->comment_length > limits->max_extra) {
        errno = EOVERFLOW;
        return -1;
    }
    
    // Initialize ZIP64 fields
    entry->has_zip64_fields = false;
    entry->zip64_compressed_size = 0;
    entry->zip64_uncompressed_size = 0;
    entry->zip64_local_header_offset = 0;
    
    // Read variable-length fields
    entry->filename = NULL;
    entry->extra_field = NULL;
    entry->comment = NULL;
    
    if (entry->filename_length > 0) {
        entry->filename = malloc(entry->filename_length + 1);
        if (!entry->filename) {
            return -1;
        }
        n = arc_stream_read(stream, entry->filename, entry->filename_length);
        if (n != entry->filename_length) {
            free(entry->filename);
            entry->filename = NULL;
            return -1;
        }
        entry->filename[entry->filename_length] = '\0';
    }
    
    if (entry->extra_field_length > 0) {
        entry->extra_field = malloc(entry->extra_field_length);
        if (!entry->extra_field) {
            free(entry->filename);
            return -1;
        }
        n = arc_stream_read(stream, entry->extra_field, entry->extra_field_length);
        if (n != entry->extra_field_length) {
            free(entry->filename);
            free(entry->extra_field);
            entry->filename = NULL;
            entry->extra_field = NULL;
            return -1;
        }
        
        // Parse ZIP64 extra field
        parse_zip64_extra_field(entry->extra_field, entry->extra_field_length, entry);
    }
    
    if (entry->comment_length > 0) {
        entry->comment = malloc(entry->comment_length + 1);
        if (!entry->comment) {
            free(entry->filename);
            free(entry->extra_field);
            return -1;
        }
        n = arc_stream_read(stream, entry->comment, entry->comment_length);
        if (n != entry->comment_length) {
            free(entry->filename);
            free(entry->extra_field);
            free(entry->comment);
            entry->filename = NULL;
            entry->extra_field = NULL;
            entry->comment = NULL;
            return -1;
        }
        entry->comment[entry->comment_length] = '\0';
    }
    
    return 0;
}

// Helper: Read all central directory entries
static int read_central_directory(ArcStream *stream, int64_t offset, uint64_t count,
                                  int64_t stream_size, uint64_t central_dir_size,
                                  struct ZipCentralDirEntry **entries_out, size_t *count_out,
                                  const ArcLimits *limits) {
    // Security: Check entry count limit
    if (limits && limits->max_entries > 0 && count > limits->max_entries) {
        errno = EOVERFLOW;
        return -1;
    }
    
    // Security: Check for overflow in allocation size
    if (count > SIZE_MAX / sizeof(struct ZipCentralDirEntry)) {
        errno = EOVERFLOW;
        return -1;
    }
    
    // Security: Validate central directory bounds against file size
    if (stream_size >= 0) {
        // Check that offset is valid
        if (offset < 0 || (uint64_t)offset > (uint64_t)stream_size) {
            errno = EINVAL;
            return -1;
        }
        
        // Check that central directory fits within file bounds
        // Use actual central_dir_size if available, otherwise estimate minimum
        uint64_t cd_size = (central_dir_size > 0) ? central_dir_size : (count * 46);
        if (cd_size > (uint64_t)(stream_size - offset)) {
            errno = EINVAL;
            return -1;
        }
    }
    
    if (arc_stream_seek(stream, offset, SEEK_SET) < 0) {
        return -1;
    }
    
    struct ZipCentralDirEntry *entries = calloc(count, sizeof(struct ZipCentralDirEntry));
    if (!entries) {
        return -1;
    }
    
    for (uint64_t i = 0; i < count; i++) {
        if (read_central_dir_entry(stream, &entries[i], limits) < 0) {
            // Free what we've read so far
            for (uint64_t j = 0; j < i; j++) {
                free_central_dir_entry(&entries[j]);
            }
            free(entries);
            return -1;
        }
    }
    
    *entries_out = entries;
    *count_out = (size_t)count;
    return 0;
}

// Convert DOS date/time to Unix timestamp
static uint64_t dos_datetime_to_unix(uint16_t date, uint16_t time) {
    // DOS date: bits 0-4 = day (1-31), bits 5-8 = month (1-12), bits 9-15 = year (since 1980)
    // DOS time: bits 0-4 = seconds/2 (0-29), bits 5-10 = minute (0-59), bits 11-15 = hour (0-23)
    
    int day = date & 0x1f;
    int month = (date >> 5) & 0x0f;
    int year = ((date >> 9) & 0x7f) + 1980;
    
    int second = (time & 0x1f) * 2;
    int minute = (time >> 5) & 0x3f;
    int hour = (time >> 11) & 0x1f;
    
    // Use mktime() for proper conversion
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = -1; // Let system determine DST
    
    time_t t = mktime(&tm);
    if (t == (time_t)-1) {
        // Fallback to simple approximation if mktime fails
        uint64_t days = (year - 1970) * 365 + (year - 1969) / 4;
        for (int m = 1; m < month; m++) {
            int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            days += days_in_month[m - 1];
            if (m == 2 && (year % 4 == 0)) {
                days++; // Leap year
            }
        }
        days += day - 1;
        return days * 86400 + hour * 3600 + minute * 60 + second;
    }
    
    return (uint64_t)t;
}

// Check if entry is a directory (name ends with /)
static bool is_directory_name(const char *name) {
    if (!name || name[0] == '\0') {
        return false;
    }
    size_t len = strlen(name);
    return (len > 0 && name[len - 1] == '/');
}

// ZIP permission mapping:
// - If "version made by" indicates Unix (host OS = 3), use external_attrs high 16 bits as st_mode.
// - Otherwise synthesize sane defaults (dir 0755, file 0644).
static uint32_t zip_entry_mode(const struct ZipCentralDirEntry *cd_entry) {
    if (!cd_entry) return 0644;
    const bool is_dir = is_directory_name(cd_entry->filename);

    // host OS is high byte of version_made_by
    const uint8_t host_os = (uint8_t)(cd_entry->version_made_by >> 8);
    if (host_os == 3 /* Unix */) {
        uint32_t mode = (cd_entry->external_attrs >> 16) & 0xFFFF;
        // If mode is absent/zero, fall back to defaults.
        if (mode != 0) return mode;
    }

    return is_dir ? 0755 : 0644;
}

// Helper: Read data descriptor (when bit 3 is set)
// Data descriptor format: [optional 4-byte signature 0x08074b50] + CRC32 (4) + compressed_size (4) + uncompressed_size (4)
// Returns 0 on success, -1 on error
static int read_data_descriptor(ArcStream *stream, uint32_t *crc32_out, uint64_t *compressed_size_out, uint64_t *uncompressed_size_out) {
    uint8_t buf[16];
    ssize_t n = arc_stream_read(stream, buf, 4);
    if (n != 4) {
        return -1;
    }

    uint32_t first_word = read_le32(buf);
    int offset = 0;

    // Check for optional signature (0x08074b50)
    if (first_word == 0x08074b50) {
        // Signature present, read the rest
        n = arc_stream_read(stream, buf, 12);
        if (n != 12) {
            return -1;
        }
        offset = 0;
    } else {
        // No signature, first 4 bytes are CRC32, read remaining 8 bytes
        n = arc_stream_read(stream, buf + 4, 8);
        if (n != 8) {
            return -1;
        }
        offset = -4; // We already read CRC32 in first_word
    }

    if (offset == 0) {
        // Signature was present, read CRC32 from buf[0-3]
        *crc32_out = read_le32(buf);
        *compressed_size_out = read_le32(buf + 4);
        *uncompressed_size_out = read_le32(buf + 8);
    } else {
        // No signature, first_word is CRC32
        *crc32_out = first_word;
        *compressed_size_out = read_le32(buf);
        *uncompressed_size_out = read_le32(buf + 4);
    }

    return 0;
}

// Forward declarations
static int zip_read_entry_streaming(ZipReader *reader);
static int read_local_file_header(ArcStream *stream, int64_t *header_pos_out, struct ZipCentralDirEntry *entry, const ArcLimits *limits);
static int add_stream_entry(ZipReader *zip, const struct ZipCentralDirEntry *entry);
static int read_data_descriptor(ArcStream *stream, uint32_t *crc32_out, uint64_t *compressed_size_out, uint64_t *uncompressed_size_out);

// Helper: Read local file header (for streaming mode)
static int read_local_file_header(ArcStream *stream, int64_t *header_pos_out, struct ZipCentralDirEntry *entry, const ArcLimits *limits) {
    int64_t header_pos = arc_stream_tell(stream);
    if (header_pos < 0) {
        return -1;
    }
    
    uint8_t header[30];
    ssize_t n = arc_stream_read(stream, header, sizeof(header));
    if (n != sizeof(header)) {
        return -1;
    }
    
    uint32_t sig = read_le32(header);
    if (sig != ZIP_LOCAL_FILE_HEADER_SIG) {
        return -1;
    }
    
    uint16_t version_needed = read_le16(header + 4);
    uint16_t flags = read_le16(header + 6);
    uint16_t compression_method = read_le16(header + 8);
    uint16_t mod_time = read_le16(header + 10);
    uint16_t mod_date = read_le16(header + 12);
    uint32_t crc32 = read_le32(header + 14);
    uint32_t compressed_size = read_le32(header + 18);
    uint32_t uncompressed_size = read_le32(header + 22);
    uint16_t filename_length = read_le16(header + 26);
    uint16_t extra_field_length = read_le16(header + 28);
    
    // Security: Validate field lengths to prevent excessive allocations
    if (limits && limits->max_name > 0 && (uint64_t)filename_length > limits->max_name) {
        errno = EOVERFLOW;
        return -1;
    }
    if (limits && limits->max_extra > 0 && (uint64_t)extra_field_length > limits->max_extra) {
        errno = EOVERFLOW;
        return -1;
    }
    
    // Initialize entry
    memset(entry, 0, sizeof(*entry));
    entry->signature = ZIP_LOCAL_FILE_HEADER_SIG;
    entry->version_needed = version_needed;
    entry->flags = flags;
    entry->compression_method = compression_method;
    entry->mod_time = mod_time;
    entry->mod_date = mod_date;
    entry->crc32 = crc32;
    
    // When bit 3 (data descriptor) is set, sizes in local header are unreliable (usually zero)
    // They will appear in the data descriptor after the compressed data
    if (flags & ZIP_FLAG_DATA_DESCRIPTOR) {
        // Mark sizes as unreliable - will be read from data descriptor later
        entry->compressed_size = 0;
        entry->uncompressed_size = 0;
    } else {
        entry->compressed_size = compressed_size;
        entry->uncompressed_size = uncompressed_size;
    }
    entry->filename_length = filename_length;
    entry->extra_field_length = extra_field_length;
    entry->local_header_offset = (uint32_t)header_pos;
    
    // Read filename
    if (filename_length > 0) {
        entry->filename = malloc(filename_length + 1);
        if (!entry->filename) {
            return -1;
        }
        n = arc_stream_read(stream, entry->filename, filename_length);
        if (n != filename_length) {
            free(entry->filename);
            entry->filename = NULL;
            return -1;
        }
        entry->filename[filename_length] = '\0';
    }
    
    // Read extra field
    if (extra_field_length > 0) {
        entry->extra_field = malloc(extra_field_length);
        if (!entry->extra_field) {
            free(entry->filename);
            return -1;
        }
        n = arc_stream_read(stream, entry->extra_field, extra_field_length);
        if (n != extra_field_length) {
            free(entry->filename);
            free(entry->extra_field);
            entry->filename = NULL;
            entry->extra_field = NULL;
            return -1;
        }
        
        // Parse ZIP64 extra field
        parse_zip64_extra_field(entry->extra_field, entry->extra_field_length, entry);
    }
    
    // Calculate data start position
    *header_pos_out = header_pos;
    
    return 0;
}

// Helper: Add entry to streaming entries list
static int add_stream_entry(ZipReader *zip, const struct ZipCentralDirEntry *entry) {
    // Grow array if needed
    if (zip->stream_entry_count >= zip->stream_entry_capacity) {
        size_t new_capacity = zip->stream_entry_capacity == 0 ? 16 : zip->stream_entry_capacity * 2;
        struct ZipCentralDirEntry *new_entries = realloc(zip->stream_entries,
                                                         new_capacity * sizeof(struct ZipCentralDirEntry));
        if (!new_entries) {
            return -1;
        }
        zip->stream_entries = new_entries;
        zip->stream_entry_capacity = new_capacity;
    }
    
    // Copy entry
    struct ZipCentralDirEntry *dst = &zip->stream_entries[zip->stream_entry_count];
    memset(dst, 0, sizeof(*dst));
    *dst = *entry;
    
    // Deep copy allocated fields
    if (entry->filename) {
        dst->filename = strdup(entry->filename);
        if (!dst->filename) {
            return -1;
        }
    }
    if (entry->extra_field && entry->extra_field_length > 0) {
        dst->extra_field = malloc(entry->extra_field_length);
        if (!dst->extra_field) {
            free(dst->filename);
            return -1;
        }
        memcpy(dst->extra_field, entry->extra_field, entry->extra_field_length);
    }
    if (entry->comment) {
        dst->comment = strdup(entry->comment);
        if (!dst->comment) {
            free(dst->filename);
            free(dst->extra_field);
            return -1;
        }
    }
    
    zip->stream_entry_count++;
    return 0;
}

// Streaming mode: Read next entry from local headers
static int zip_read_entry_streaming(ZipReader *reader);

// Read next ZIP entry from central directory
static int zip_read_entry(ZipReader *reader) {
    if (reader->eof || reader->current_entry_index >= reader->entry_count) {
        reader->eof = true;
        return 1; // Done
    }
    
    struct ZipCentralDirEntry *cd_entry = &reader->entries[reader->current_entry_index];
    reader->current_entry_index++;
    
    // Free previous entry
    arc_entry_free(&reader->current_entry);
    memset(&reader->current_entry, 0, sizeof(reader->current_entry));
    
    // Set entry fields
    reader->current_entry.path = strdup(cd_entry->filename);
    if (!reader->current_entry.path) {
        return -1;
    }
    
    // Normalize path (remove leading ./ and duplicate slashes)
    char *path = reader->current_entry.path;
    while (path[0] == '.' && path[1] == '/') {
        memmove(path, path + 2, strlen(path) - 1);
    }
    while (path[0] == '/' && path[1] == '/') {
        memmove(path, path + 1, strlen(path));
    }
    
    reader->current_entry.size = cd_entry->uncompressed_size;
    reader->current_entry.mode = zip_entry_mode(cd_entry);
    reader->current_entry.mtime = dos_datetime_to_unix(cd_entry->mod_date, cd_entry->mod_time);
    reader->current_entry.uid = 0; // ZIP doesn't store uid/gid
    reader->current_entry.gid = 0;
    
    // Determine type
    if (is_directory_name(cd_entry->filename)) {
        reader->current_entry.type = ARC_ENTRY_DIR;
    } else {
        reader->current_entry.type = ARC_ENTRY_FILE;
    }
    
    reader->current_entry.link_target = NULL; // ZIP doesn't support symlinks
    
    // Store entry data info (use ZIP64 values if available)
    if (cd_entry->has_zip64_fields) {
        reader->entry_data_offset = (int64_t)cd_entry->zip64_local_header_offset;
        reader->entry_data_remaining = (int64_t)cd_entry->zip64_compressed_size;
        reader->entry_uncompressed_size = cd_entry->zip64_uncompressed_size;
        reader->current_entry.size = cd_entry->zip64_uncompressed_size;
    } else {
        reader->entry_data_offset = cd_entry->local_header_offset;
        reader->entry_data_remaining = cd_entry->compressed_size;
        reader->entry_uncompressed_size = cd_entry->uncompressed_size;
        reader->current_entry.size = cd_entry->uncompressed_size;
    }
    reader->entry_compression_method = cd_entry->compression_method;
    reader->entry_flags = cd_entry->flags;
    reader->entry_valid = true;
    
    return 0;
}

// Streaming mode: Read next entry from local headers
static int zip_read_entry_streaming(ZipReader *reader) {
    if (reader->eof) {
        return 1; // Done
    }
    
    // Seek to current position
    if (arc_stream_seek(reader->base.stream, reader->stream_pos, SEEK_SET) < 0) {
        reader->eof = true;
        return 1;
    }
    
    struct ZipCentralDirEntry entry;
    int64_t header_pos;
    if (read_local_file_header(reader->base.stream, &header_pos, &entry, reader->base.limits) < 0) {
        reader->eof = true;
        return 1; // End of archive or error
    }
    
    // Get data size (use ZIP64 if available)
    int64_t compressed_size = 0;
    int64_t data_start = arc_stream_tell(reader->base.stream);
    int64_t next_header_pos = -1;
    
    if (entry.flags & ZIP_FLAG_DATA_DESCRIPTOR) {
        // Bit 3 is set: sizes are in data descriptor after compressed data
        // Local header sizes are unreliable (usually zero)

        if (entry.compression_method == ZIP_METHOD_STORE) {
            // For uncompressed data with data descriptor, we need to read until
            // we find the data descriptor signature (0x08074b50) or next local header
            int64_t current_pos = data_start;
            const int64_t max_search = 1024 * 1024; // Limit search to 1MB to avoid excessive reading
            int64_t search_limit = current_pos + max_search;

            // Seek to start of data
            if (arc_stream_seek(reader->base.stream, data_start, SEEK_SET) < 0) {
                free_central_dir_entry(&entry);
                return -1;
            }

            // Read in small chunks looking for data descriptor
            uint8_t buf[1024];
            uint32_t descriptor_sig = 0x08074b50;
            bool found_descriptor = false;
            uint32_t crc32 = 0;
            uint64_t compressed_size_found = 0;
            uint64_t uncompressed_size_found = 0;

            while (current_pos < search_limit) {
                ssize_t n = arc_stream_read(reader->base.stream, buf, sizeof(buf));
                if (n <= 0) break;

                // Look for data descriptor signature in the buffer
                for (size_t i = 0; i < (size_t)n - 3; i++) {
                    if (read_le32(buf + i) == descriptor_sig) {
                        // Found data descriptor, read the data
                        int64_t descriptor_pos = current_pos + i;
                        if (arc_stream_seek(reader->base.stream, descriptor_pos + 4, SEEK_SET) == 0) {
                            if (read_data_descriptor(reader->base.stream, &crc32, &compressed_size_found, &uncompressed_size_found) == 0) {
                                found_descriptor = true;
                                compressed_size = compressed_size_found;
                                next_header_pos = descriptor_pos + (compressed_size_found > 0xFFFFFFFF ? 24 : 16); // Size of descriptor
                                break;
                            }
                        }
                    }
                }

                if (found_descriptor) break;
                current_pos += n;

                // If we read less than buffer size, we're at EOF
                if (n < (ssize_t)sizeof(buf)) break;
            }

            if (!found_descriptor) {
                // Couldn't find data descriptor, fall back to local header size
                compressed_size = entry.has_zip64_fields ?
                               (int64_t)entry.zip64_compressed_size :
                               (int64_t)entry.compressed_size;
                next_header_pos = data_start + compressed_size;
            }

        } else if (entry.compression_method == ZIP_METHOD_DEFLATE) {
            // For compressed data, we need to create a filter and read until EOF,
            // then look for the data descriptor
            ArcStream *temp_stream = arc_stream_substream(reader->base.stream, data_start, -1); // Unlimited
            if (temp_stream) {
                ArcStream *decomp = arc_filter_deflate(temp_stream, 0); // No limit for detection
                if (decomp) {
                    // Read all decompressed data to find where compressed data ends
                    uint8_t temp_buf[4096];
                    ssize_t total_decompressed = 0;
                    while (arc_stream_read(decomp, temp_buf, sizeof(temp_buf)) > 0) {
                        total_decompressed += sizeof(temp_buf);
                        // Limit to reasonable size to avoid excessive memory use
                        if (total_decompressed > 100 * 1024 * 1024) break; // 100MB limit
                    }
                    arc_stream_close(decomp);

                    // Now try to read data descriptor from the underlying stream
                    uint32_t crc32;
                    uint64_t compressed_size_found, uncompressed_size_found;
                    if (read_data_descriptor(reader->base.stream, &crc32, &compressed_size_found, &uncompressed_size_found) == 0) {
                        compressed_size = compressed_size_found;
                        next_header_pos = data_start + compressed_size;
                    } else {
                        // Fallback to local header size
                        compressed_size = entry.has_zip64_fields ?
                                       (int64_t)entry.zip64_compressed_size :
                                       (int64_t)entry.compressed_size;
                        next_header_pos = data_start + compressed_size;
                    }
                } else {
                    arc_stream_close(temp_stream);
                    free_central_dir_entry(&entry);
                    return -1;
                }
                arc_stream_close(temp_stream);
            } else {
                free_central_dir_entry(&entry);
                return -1;
            }
        } else {
            // Unknown compression method with data descriptor
            errno = EINVAL;
            free_central_dir_entry(&entry);
            return -1;
        }

        reader->stream_pos = next_header_pos;

    } else {
        // Normal case: use sizes from local header
        compressed_size = entry.has_zip64_fields ?
                           (int64_t)entry.zip64_compressed_size :
                           (int64_t)entry.compressed_size;
        next_header_pos = data_start + compressed_size;
        reader->stream_pos = next_header_pos;
    }
    
    // Add to entries list
    if (add_stream_entry(reader, &entry) < 0) {
        free_central_dir_entry(&entry);
        return -1;
    }
    
    // Free temporary entry (we've copied it)
    free_central_dir_entry(&entry);
    
    // Use the entry we just added
    struct ZipCentralDirEntry *cd_entry = &reader->stream_entries[reader->stream_entry_count - 1];
    
    // Free previous entry
    arc_entry_free(&reader->current_entry);
    memset(&reader->current_entry, 0, sizeof(reader->current_entry));
    
    // Set entry fields
    reader->current_entry.path = strdup(cd_entry->filename);
    if (!reader->current_entry.path) {
        return -1;
    }
    
    // Normalize path
    char *path = reader->current_entry.path;
    while (path[0] == '.' && path[1] == '/') {
        memmove(path, path + 2, strlen(path) - 1);
    }
    while (path[0] == '/' && path[1] == '/') {
        memmove(path, path + 1, strlen(path));
    }
    
    // Use ZIP64 values if available
    if (cd_entry->has_zip64_fields) {
        reader->current_entry.size = cd_entry->zip64_uncompressed_size;
        reader->entry_uncompressed_size = cd_entry->zip64_uncompressed_size;
        reader->entry_data_offset = header_pos + 30 + cd_entry->filename_length + cd_entry->extra_field_length;
        reader->entry_data_remaining = (int64_t)cd_entry->zip64_compressed_size;
    } else {
        reader->current_entry.size = cd_entry->uncompressed_size;
        reader->entry_uncompressed_size = cd_entry->uncompressed_size;
        reader->entry_data_offset = header_pos + 30 + cd_entry->filename_length + cd_entry->extra_field_length;
        reader->entry_data_remaining = (int64_t)cd_entry->compressed_size;
    }
    
    // In streaming mode, we may not have reliable Unix metadata; use best-effort mapping.
    reader->current_entry.mode = zip_entry_mode(cd_entry);
    reader->current_entry.mtime = dos_datetime_to_unix(cd_entry->mod_date, cd_entry->mod_time);
    reader->current_entry.uid = 0;
    reader->current_entry.gid = 0;
    
    // Determine type
    if (is_directory_name(cd_entry->filename)) {
        reader->current_entry.type = ARC_ENTRY_DIR;
    } else {
        reader->current_entry.type = ARC_ENTRY_FILE;
    }
    
    reader->current_entry.link_target = NULL;
    reader->entry_compression_method = cd_entry->compression_method;
    reader->entry_flags = cd_entry->flags;
    reader->entry_valid = true;
    
    return 0;
}

// ArcReader vtable for ZIP
int arc_zip_next(ArcReader *reader, ArcEntry *entry) {
    if (!reader || !entry) {
        return -1;
    }
    ZipReader *zip = (ZipReader *)reader;
    
    int ret;
    if (zip->streaming_mode) {
        ret = zip_read_entry_streaming(zip);
    } else {
        ret = zip_read_entry(zip);
    }
    
    if (ret == 0) {
        *entry = zip->current_entry;
        // Clear entry structure so it's not freed when reader closes
        memset(&zip->current_entry, 0, sizeof(zip->current_entry));
    }
    return ret;
}

ArcStream *arc_zip_open_data(ArcReader *reader) {
    if (!reader) {
        return NULL;
    }
    ZipReader *zip = (ZipReader *)reader;
    if (!zip->entry_valid || zip->entry_data_remaining == 0) {
        return NULL;
    }
    
    // Seek to local file header
    if (arc_stream_seek(zip->base.stream, zip->entry_data_offset, SEEK_SET) < 0) {
        return NULL;
    }
    
    // Read local file header
    uint8_t header[30];
    ssize_t n = arc_stream_read(zip->base.stream, header, sizeof(header));
    if (n != sizeof(header)) {
        return NULL;
    }
    
    uint32_t sig = read_le32(header);
    if (sig != ZIP_LOCAL_FILE_HEADER_SIG) {
        return NULL;
    }
    
    uint16_t filename_length = read_le16(header + 26);
    uint16_t extra_field_length = read_le16(header + 28);
    
    // Skip filename and extra field
    int64_t skip = filename_length + extra_field_length;
    if (arc_stream_seek(zip->base.stream, skip, SEEK_CUR) < 0) {
        return NULL;
    }
    
    // Get current position (start of file data)
    int64_t data_start = arc_stream_tell(zip->base.stream);
    
    // When bit 3 (data descriptor) is set, local header sizes are unreliable
    // Use central directory sizes (which we already have in entry_data_remaining)
    // The substream will use the correct size from central directory
    ArcStream *data_stream = arc_stream_substream(zip->base.stream, data_start, zip->entry_data_remaining);
    if (!data_stream) {
        return NULL;
    }
    
    // Wrap with decompression filter if needed
    if (zip->entry_compression_method == ZIP_METHOD_DEFLATE) {
        // ZIP uses raw deflate (not gzip-wrapped)
        int64_t out_limit = zip->entry_uncompressed_size;
        if (zip->base.limits && zip->base.limits->max_uncompressed_bytes > 0) {
            if (out_limit <= 0 || (uint64_t)out_limit > zip->base.limits->max_uncompressed_bytes) {
                out_limit = (int64_t)zip->base.limits->max_uncompressed_bytes;
            }
        }
        ArcStream *decompressed = arc_filter_deflate(data_stream, out_limit);
        if (decompressed) {
            return decompressed;
        }
        // Fall through to return compressed stream if filter fails
        arc_stream_close(data_stream);
        return NULL;
    } else if (zip->entry_compression_method != ZIP_METHOD_STORE) {
        // Unsupported compression method
        arc_stream_close(data_stream);
        return NULL;
    }
    
    return data_stream;
}

int arc_zip_skip_data(ArcReader *reader) {
    if (!reader) {
        return -1;
    }
    ZipReader *zip = (ZipReader *)reader;
    if (!zip->entry_valid) {
        return -1;
    }
    
    zip->entry_data_remaining = 0;
    zip->entry_valid = false;
    return 0;
}

void arc_zip_close(ArcReader *reader) {
    if (!reader) {
        return;
    }
    ZipReader *zip = (ZipReader *)reader;
    
    arc_entry_free(&zip->current_entry);
    
    // Free central directory entries
    if (zip->entries) {
        for (size_t i = 0; i < zip->entry_count; i++) {
            free_central_dir_entry(&zip->entries[i]);
        }
        free(zip->entries);
    }
    
    // Free streaming entries
    if (zip->stream_entries) {
        for (size_t i = 0; i < zip->stream_entry_count; i++) {
            free_central_dir_entry(&zip->stream_entries[i]);
        }
        free(zip->stream_entries);
    }
    
    if (zip->base.stream) {
        arc_stream_close(zip->base.stream);
        zip->base.stream = NULL;
    }
    if (zip->base.owned_stream) {
        arc_stream_close(zip->base.owned_stream);
        zip->base.owned_stream = NULL;
    }
    free(zip);
}

ArcReader *arc_zip_open(ArcStream *stream) {
    return arc_zip_open_ex(stream, arc_default_limits());
}

ArcReader *arc_zip_open_ex(ArcStream *stream, const ArcLimits *limits) {
    if (!stream) {
        return NULL;
    }
    
    ZipReader *zip = calloc(1, sizeof(ZipReader));
    if (!zip) {
        return NULL;
    }
    
    zip->base.format = ARC_FORMAT_ZIP;
    zip->base.stream = stream;
    zip->base.limits = limits;
    zip->entry_valid = false;
    zip->eof = false;
    zip->current_entry_index = 0;
    zip->streaming_mode = false;
    zip->stream_pos = 0;
    zip->stream_entries = NULL;
    zip->stream_entry_count = 0;
    zip->stream_entry_capacity = 0;
    
    // Try to find End of Central Directory (for fast listing)
    struct ZipEOCD eocd;
    struct Zip64EOCDRecord eocd64;
    memset(&eocd64, 0, sizeof(eocd64));
    
    int eocd_found = find_eocd(stream, &eocd, &eocd64, limits);
    
    if (eocd_found == 0) {
        // Central directory available - use it (faster)
        zip->streaming_mode = false;
        
        // Use ZIP64 values if available
        int64_t cd_offset = eocd.is_zip64 ? (int64_t)eocd64.central_dir_offset : (int64_t)eocd.central_dir_offset;
        uint64_t cd_count = eocd.is_zip64 ? eocd64.total_central_dir_records : (uint64_t)eocd.total_central_dir_records;
        uint64_t cd_size = eocd.is_zip64 ? eocd64.central_dir_size : (uint64_t)eocd.central_dir_size;
        
        // Get stream size for bounds validation
        int64_t stream_size = -1;
        int64_t current_pos = arc_stream_tell(stream);
        if (arc_stream_seek(stream, 0, SEEK_END) == 0) {
            stream_size = arc_stream_tell(stream);
            arc_stream_seek(stream, current_pos, SEEK_SET);
        }
        
        zip->central_dir_offset = cd_offset;
        
        // Read central directory (with security checks)
        if (read_central_directory(stream, cd_offset, cd_count, stream_size, cd_size,
                                   &zip->entries, &zip->entry_count, limits) < 0) {
            free(eocd.comment);
            free(zip);
            return NULL;
        }
    } else {
        // Central directory not found - use streaming mode
        zip->streaming_mode = true;
        zip->stream_pos = 0;
        
        // Reset to beginning
        if (arc_stream_seek(stream, 0, SEEK_SET) < 0) {
            free(eocd.comment);
            free(zip);
            return NULL;
        }
    }
    
    free(eocd.comment);
    
    // Cast to ArcReader (they're the same structure)
    ArcReader *reader = (ArcReader *)zip;
    
    return reader;
}

