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
    uint32_t entry_crc32;
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
    int64_t stream_size;
    
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
static int parse_zip64_extra_field(const uint8_t *extra, size_t length,
                                    struct ZipCentralDirEntry *entry) {
    entry->zip64_uncompressed_size = entry->uncompressed_size;
    entry->zip64_compressed_size = entry->compressed_size;
    entry->zip64_local_header_offset = entry->local_header_offset;
    entry->has_zip64_fields = entry->uncompressed_size == UINT32_MAX ||
        entry->compressed_size == UINT32_MAX || entry->local_header_offset == UINT32_MAX;
    bool found = false;
    size_t pos = 0;
    while (pos < length) {
        if (length - pos < 4) { errno = EINVAL; return -1; }
        uint16_t id = read_le16(extra + pos), size = read_le16(extra + pos + 2);
        pos += 4;
        if (size > length - pos) { errno = EINVAL; return -1; }
        if (id == ZIP64_EXTRA_FIELD_ID) {
            if (found) { errno = EINVAL; return -1; }
            found = true;
            size_t cursor = 0;
            uint64_t *wide[] = {&entry->zip64_uncompressed_size, &entry->zip64_compressed_size,
                                &entry->zip64_local_header_offset};
            uint32_t narrow[] = {entry->uncompressed_size, entry->compressed_size, entry->local_header_offset};
            for (size_t i = 0; i < 3; i++) {
                if (narrow[i] != UINT32_MAX) continue;
                if (size - cursor < 8) { errno = EINVAL; return -1; }
                *wide[i] = read_le64(extra + pos + cursor); cursor += 8;
            }
            if (entry->disk_number == UINT16_MAX) {
                if (size - cursor < 4 || read_le32(extra + pos + cursor) != 0) { errno = EINVAL; return -1; }
                entry->disk_number = 0;
            }
        }
        pos += size;
    }
    if ((entry->has_zip64_fields || entry->disk_number == UINT16_MAX) && !found) { errno = EINVAL; return -1; }
    if (entry->disk_number || entry->zip64_compressed_size > INT64_MAX ||
        entry->zip64_local_header_offset > INT64_MAX) { errno = EINVAL; return -1; }
    return 0;
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
    ssize_t n = arc_stream_read_fully(stream, buffer, sizeof(buffer));
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
    ssize_t n = arc_stream_read_fully(stream, buffer, sizeof(buffer));
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
/* Return 1 only when no end record exists, allowing local-header recovery.
   A present but invalid end record is a malformed archive, not an empty one. */
static int find_eocd(ArcStream *stream, struct ZipEOCD *eocd,
                     struct Zip64EOCDRecord *eocd64_out, const ArcLimits *limits) {
    memset(eocd, 0, sizeof(*eocd));
    memset(eocd64_out, 0, sizeof(*eocd64_out));
    if (arc_stream_seek(stream, 0, SEEK_END)) return -1;
    int64_t size = arc_stream_tell(stream);
    if (size < 0) return -1;
    if (size < 22) return 1;
    int64_t start = size > 65557 ? size - 65557 : 0;
    uint8_t buffer[65557];
    if (arc_stream_seek(stream, start, SEEK_SET)) return -1;
    ssize_t n = arc_stream_read_fully(stream, buffer, (size_t)(size - start));
    if (n != size - start) { if (n >= 0) errno = EIO; return -1; }
    bool seen = false;
    for (ssize_t i = n - 22; i >= 0; i--) {
        const uint8_t *record = buffer + i;
        if (read_le32(record) != ZIP_END_OF_CENTRAL_DIR_SIG) continue;
        seen = true;
        uint16_t comment = read_le16(record + 20);
        if ((size_t)i + 22 + comment != (size_t)n) continue;
        if (limits && comment > limits->max_extra) { errno = EOVERFLOW; continue; }
        struct ZipEOCD candidate = {0};
        struct Zip64EOCDRecord wide = {0};
        candidate.disk_number = read_le16(record + 4);
        candidate.central_dir_disk = read_le16(record + 6);
        candidate.central_dir_records_on_disk = read_le16(record + 8);
        candidate.total_central_dir_records = read_le16(record + 10);
        candidate.central_dir_size = read_le32(record + 12);
        candidate.central_dir_offset = read_le32(record + 16);
        candidate.comment_length = comment;
        candidate.is_zip64 = candidate.disk_number == UINT16_MAX || candidate.central_dir_disk == UINT16_MAX ||
            candidate.central_dir_records_on_disk == UINT16_MAX || candidate.total_central_dir_records == UINT16_MAX ||
            candidate.central_dir_size == UINT32_MAX || candidate.central_dir_offset == UINT32_MAX;
        uint64_t count = candidate.total_central_dir_records;
        uint64_t bytes = candidate.central_dir_size, offset = candidate.central_dir_offset;
        int64_t boundary = start + i;
        struct Zip64EOCDLocator locator;
        bool has_locator = find_zip64_locator(stream, boundary, &locator) == 0;
        if (candidate.is_zip64 || has_locator) {
            candidate.is_zip64 = true;
            if (!has_locator || locator.disk_with_zip64_eocd || locator.total_disks != 1 ||
                locator.zip64_eocd_offset > (uint64_t)(boundary >= 76 ? boundary - 76 : 0) || boundary < 76 ||
                read_zip64_eocd(stream, (int64_t)locator.zip64_eocd_offset, &wide)) continue;
            if (wide.zip64_eocd_size < 44 || wide.zip64_eocd_size > (uint64_t)(boundary - 20) - locator.zip64_eocd_offset - 12 ||
                locator.zip64_eocd_offset + 12 + wide.zip64_eocd_size != (uint64_t)boundary - 20 ||
                (limits && wide.zip64_eocd_size > limits->max_extra) || wide.disk_number || wide.central_dir_disk ||
                wide.central_dir_records_on_disk != wide.total_central_dir_records) continue;
            if ((candidate.disk_number != UINT16_MAX && candidate.disk_number != wide.disk_number) ||
                (candidate.central_dir_disk != UINT16_MAX && candidate.central_dir_disk != wide.central_dir_disk) ||
                (candidate.central_dir_records_on_disk != UINT16_MAX && candidate.central_dir_records_on_disk != wide.central_dir_records_on_disk) ||
                (candidate.total_central_dir_records != UINT16_MAX && candidate.total_central_dir_records != wide.total_central_dir_records) ||
                (candidate.central_dir_size != UINT32_MAX && candidate.central_dir_size != wide.central_dir_size) ||
                (candidate.central_dir_offset != UINT32_MAX && candidate.central_dir_offset != wide.central_dir_offset)) continue;
            count = wide.total_central_dir_records; bytes = wide.central_dir_size; offset = wide.central_dir_offset;
            boundary = (int64_t)locator.zip64_eocd_offset;
        } else if (candidate.disk_number || candidate.central_dir_disk ||
                   candidate.central_dir_records_on_disk != candidate.total_central_dir_records) continue;
        if (offset > (uint64_t)boundary || bytes != (uint64_t)boundary - offset ||
            count > bytes / 46 || (!count && bytes)) continue;
        candidate.signature = ZIP_END_OF_CENTRAL_DIR_SIG;
        *eocd = candidate; *eocd64_out = wide;
        return 0;
    }
    if (seen) { errno = EINVAL; return -1; }
    return 1;
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
    uint8_t header[46];
    memset(entry, 0, sizeof(*entry));
    if (arc_stream_read_fully(stream, header, sizeof(header)) != sizeof(header) ||
        read_le32(header) != ZIP_CENTRAL_DIR_SIG) { errno = EINVAL; return -1; }
    entry->signature = ZIP_CENTRAL_DIR_SIG;
    entry->version_made_by = read_le16(header + 4); entry->version_needed = read_le16(header + 6);
    entry->flags = read_le16(header + 8); entry->compression_method = read_le16(header + 10);
    entry->mod_time = read_le16(header + 12); entry->mod_date = read_le16(header + 14);
    entry->crc32 = read_le32(header + 16); entry->compressed_size = read_le32(header + 20);
    entry->uncompressed_size = read_le32(header + 24); entry->filename_length = read_le16(header + 28);
    entry->extra_field_length = read_le16(header + 30); entry->comment_length = read_le16(header + 32);
    entry->disk_number = read_le16(header + 34); entry->internal_attrs = read_le16(header + 36);
    entry->external_attrs = read_le32(header + 38); entry->local_header_offset = read_le32(header + 42);
    if (!entry->filename_length) { errno = EINVAL; return -1; }
    if (limits && (entry->filename_length > limits->max_name || entry->extra_field_length > limits->max_extra ||
                   entry->comment_length > limits->max_extra)) { errno = EOVERFLOW; return -1; }
    entry->filename = malloc((size_t)entry->filename_length + 1);
    if (!entry->filename) return -1;
    if (arc_stream_read_fully(stream, entry->filename, entry->filename_length) != entry->filename_length ||
        memchr(entry->filename, 0, entry->filename_length)) { errno = EINVAL; goto fail; }
    entry->filename[entry->filename_length] = 0;
    if (entry->extra_field_length) {
        entry->extra_field = malloc(entry->extra_field_length);
        if (!entry->extra_field) goto fail;
        if (arc_stream_read_fully(stream, entry->extra_field, entry->extra_field_length) != entry->extra_field_length) {
            errno = EINVAL; goto fail;
        }
    }
    if (parse_zip64_extra_field(entry->extra_field, entry->extra_field_length, entry)) goto fail;
    if (entry->comment_length && arc_stream_seek(stream, entry->comment_length, SEEK_CUR)) goto fail;
    return 0;
fail:
    free_central_dir_entry(entry);
    return -1;
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
    
    if (!count) { *entries_out = NULL; *count_out = 0; return central_dir_size ? -1 : 0; }
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
    
    int64_t end = arc_stream_tell(stream);
    if (end < offset || (uint64_t)(end - offset) != central_dir_size) {
        for (size_t i = 0; i < count; i++) free_central_dir_entry(&entries[i]);
        free(entries); errno = EINVAL; return -1;
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
    
    static const unsigned month_days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (month < 1 || month > 12 || day < 1 ||
        day > (int)(month_days[month - 1] + (month == 2 && leap)) || hour > 23 || minute > 59 || second > 59) return 0;
    time_t t = mktime(&tm);
    if (t == (time_t)-1) {
        // Fallback to simple approximation if mktime fails
        uint64_t previous = (uint64_t)year - 1;
        uint64_t days = (uint64_t)(year - 1970) * 365 + previous / 4 - 1969 / 4
            - (previous / 100 - 1969 / 100) + (previous / 400 - 1969 / 400);
        for (int m = 1; m < month; m++) {
            int days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            days += days_in_month[m - 1];
            if (m == 2 && leap) {
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

// Forward declarations for local-header recovery.
static int zip_read_entry_streaming(ZipReader *reader);
static int read_local_file_header(ArcStream *stream, int64_t *header_pos_out,
                                  struct ZipCentralDirEntry *entry, const ArcLimits *limits);
static int add_stream_entry(ZipReader *zip, const struct ZipCentralDirEntry *entry);

// Helper: Read local file header (for streaming mode)
static int read_local_file_header(ArcStream *stream, int64_t *header_pos_out,
                                  struct ZipCentralDirEntry *entry, const ArcLimits *limits) {
    memset(entry, 0, sizeof(*entry));
    int64_t pos = arc_stream_tell(stream);
    if (pos < 0) return -1;
    uint8_t header[30];
    ssize_t n = arc_stream_read_fully(stream, header, 4);
    if (!n) return 1;
    if (n != 4) { if (n >= 0) errno = EINVAL; return -1; }
    uint32_t signature = read_le32(header);
    if (signature == ZIP_CENTRAL_DIR_SIG || signature == ZIP_END_OF_CENTRAL_DIR_SIG ||
        signature == ZIP_END_OF_CENTRAL_DIR64_SIG) return 1;
    if (signature != ZIP_LOCAL_FILE_HEADER_SIG || arc_stream_read_fully(stream, header + 4, 26) != 26) {
        errno = EINVAL; return -1;
    }
    entry->signature = signature; entry->version_needed = read_le16(header + 4);
    entry->flags = read_le16(header + 6); entry->compression_method = read_le16(header + 8);
    entry->mod_time = read_le16(header + 10); entry->mod_date = read_le16(header + 12);
    entry->crc32 = read_le32(header + 14); entry->compressed_size = read_le32(header + 18);
    entry->uncompressed_size = read_le32(header + 22); entry->filename_length = read_le16(header + 26);
    entry->extra_field_length = read_le16(header + 28);
    entry->local_header_offset = pos > UINT32_MAX ? UINT32_MAX : (uint32_t)pos;
    if (!entry->filename_length) { errno = EINVAL; return -1; }
    if (limits && (entry->filename_length > limits->max_name || entry->extra_field_length > limits->max_extra)) {
        errno = EOVERFLOW; return -1;
    }
    entry->filename = malloc((size_t)entry->filename_length + 1);
    if (!entry->filename) return -1;
    if (arc_stream_read_fully(stream, entry->filename, entry->filename_length) != entry->filename_length ||
        memchr(entry->filename, 0, entry->filename_length)) { errno = EINVAL; goto fail; }
    entry->filename[entry->filename_length] = 0;
    if (entry->extra_field_length) {
        entry->extra_field = malloc(entry->extra_field_length);
        if (!entry->extra_field) goto fail;
        if (arc_stream_read_fully(stream, entry->extra_field, entry->extra_field_length) != entry->extra_field_length) {
            errno = EINVAL; goto fail;
        }
    }
    // A local header has no offset member in its ZIP64 extra field.
    uint32_t local_offset = entry->local_header_offset;
    entry->local_header_offset = 0;
    if (parse_zip64_extra_field(entry->extra_field, entry->extra_field_length, entry)) goto fail;
    entry->local_header_offset = local_offset;
    entry->zip64_local_header_offset = (uint64_t)pos;
    *header_pos_out = pos;
    return 0;
fail:
    free_central_dir_entry(entry);
    return -1;
}

// Helper: Add entry to streaming entries list
static int add_stream_entry(ZipReader *zip, const struct ZipCentralDirEntry *entry) {
    // Grow array if needed
    if (zip->stream_entry_count >= zip->stream_entry_capacity) {
        if (zip->stream_entry_capacity > SIZE_MAX / (2 * sizeof(struct ZipCentralDirEntry))) { errno = EOVERFLOW; return -1; }
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

static int select_zip_entry(ZipReader *reader, const struct ZipCentralDirEntry *entry) {
    arc_entry_free(&reader->current_entry);
    reader->entry_valid = false;
    reader->current_entry.path = strdup(entry->filename);
    if (!reader->current_entry.path) return -1;
    reader->current_entry.size = entry->zip64_uncompressed_size;
    reader->current_entry.mode = zip_entry_mode(entry);
    reader->current_entry.mtime = dos_datetime_to_unix(entry->mod_date, entry->mod_time);
    reader->current_entry.type = is_directory_name(entry->filename) ? ARC_ENTRY_DIR : ARC_ENTRY_FILE;
    reader->entry_crc32 = entry->crc32;
    reader->entry_uncompressed_size = entry->zip64_uncompressed_size;
    reader->entry_data_remaining = (int64_t)entry->zip64_compressed_size;
    reader->entry_data_offset = (int64_t)entry->zip64_local_header_offset;
    reader->entry_compression_method = entry->compression_method;
    reader->entry_flags = entry->flags;
    reader->entry_valid = true;
    return 0;
}

static int zip_read_entry(ZipReader *reader) {
    if (reader->current_entry_index >= reader->entry_count) { reader->eof = true; return 1; }
    return select_zip_entry(reader, &reader->entries[reader->current_entry_index++]);
}

static bool zip_boundary(uint32_t signature) {
    return signature == ZIP_LOCAL_FILE_HEADER_SIG || signature == ZIP_CENTRAL_DIR_SIG ||
        signature == ZIP_END_OF_CENTRAL_DIR_SIG || signature == ZIP_END_OF_CENTRAL_DIR64_SIG ||
        signature == ZIP_END_OF_CENTRAL_DIR64_LOCATOR_SIG || signature == 0x08064b50 || signature == 0x05054b50;
}

/* Descriptors may omit their signature and use either 32- or 64-bit sizes.
   A recovery candidate must also end at a record boundary or actual EOF. */
static size_t match_descriptor(const uint8_t *buffer, size_t size, bool eof,
                               uint32_t crc, uint64_t packed, uint64_t unpacked, bool wide_first) {
    for (size_t pass = 0; pass < 2; pass++) {
        size_t width = (pass == 0) == wide_first ? 8 : 4;
        for (size_t prefix = 0; prefix <= 4; prefix += 4) {
            size_t length = prefix + 4 + width * 2;
            if (size < length || (prefix && read_le32(buffer) != 0x08074b50) ||
                read_le32(buffer + prefix) != crc) continue;
            uint64_t compressed = width == 8 ? read_le64(buffer + prefix + 4) : read_le32(buffer + prefix + 4);
            uint64_t uncompressed = width == 8 ? read_le64(buffer + prefix + 12) : read_le32(buffer + prefix + 8);
            if (compressed != packed || uncompressed != unpacked) continue;
            if ((size == length && eof) || (size >= length + 4 && zip_boundary(read_le32(buffer + length)))) return length;
        }
    }
    return 0;
}

static int descriptor_at(ZipReader *zip, int64_t offset, uint32_t crc,
                         uint64_t packed, uint64_t unpacked, bool wide, int64_t *next) {
    if (offset < 0 || offset > zip->stream_size) { errno = EINVAL; return -1; }
    size_t size = zip->stream_size - offset > 28 ? 28 : (size_t)(zip->stream_size - offset);
    uint8_t buffer[28];
    if (arc_stream_seek(zip->base.stream, offset, SEEK_SET) || arc_stream_read_fully(zip->base.stream, buffer, size) != (ssize_t)size) return -1;
    size_t length = match_descriptor(buffer, size, size == (uint64_t)(zip->stream_size - offset), crc, packed, unpacked, wide);
    if (!length) { errno = EINVAL; return -1; }
    *next = offset + (int64_t)length;
    return 0;
}

static bool plausible_stored_descriptor(const uint8_t *p, size_t size, uint64_t count) {
    for (size_t prefix = 0; prefix <= 4; prefix += 4) {
        if (prefix && (size < 4 || read_le32(p) != 0x08074b50)) continue;
        if (size >= prefix + 12 && count <= UINT32_MAX && read_le32(p + prefix + 4) == count &&
            read_le32(p + prefix + 8) == count) return true;
        if (size >= prefix + 20 && read_le64(p + prefix + 4) == count && read_le64(p + prefix + 12) == count) return true;
    }
    return false;
}

static int recover_stored_descriptor(ZipReader *zip, struct ZipCentralDirEntry *entry,
                                     int64_t start, uint64_t limit, uint64_t *packed,
                                     uint64_t *unpacked, int64_t *next) {
    uint8_t buffer[65536 + 28];
    size_t used = 0;
    uint64_t count = 0;
    uint32_t checksum = crc32(0, NULL, 0);
    bool eof = false;
    while (true) {
        if (!eof) {
            ssize_t n = arc_stream_read(zip->base.stream, buffer + used, sizeof(buffer) - used);
            if (n < 0) return -1;
            used += (size_t)n; eof = !n;
        }
        size_t scan = eof ? used : (used > 28 ? used - 28 : 0);
        for (size_t i = 0; i < scan; i++) {
            if (count + i > limit) { errno = EOVERFLOW; return -1; }
            if (!plausible_stored_descriptor(buffer + i, used - i, count + i)) continue;
            uint32_t candidate_crc = crc32(checksum, buffer, (uInt)i);
            size_t length = match_descriptor(buffer + i, used - i, eof, candidate_crc, count + i, count + i,
                                               entry->has_zip64_fields || entry->version_needed >= 45);
            if (length) {
                *packed = *unpacked = count + i;
                entry->crc32 = candidate_crc;
                *next = start + (int64_t)(count + i + length);
                return 0;
            }
        }
        if (eof) { errno = EINVAL; return -1; }
        if (scan > limit - count) { errno = EOVERFLOW; return -1; }
        checksum = crc32(checksum, buffer, (uInt)scan);
        count += scan; used -= scan;
        memmove(buffer, buffer + scan, used);
    }
}

static int recover_deflated_descriptor(ZipReader *zip, struct ZipCentralDirEntry *entry,
                                       int64_t start, uint64_t limit, uint64_t *packed,
                                       uint64_t *unpacked, int64_t *next) {
    z_stream decoder = {0};
    if (inflateInit2(&decoder, -MAX_WBITS) != Z_OK) { errno = ENOMEM; return -1; }
    uint8_t input[65536], output[65536];
    uint64_t compressed = 0, uncompressed = 0;
    uint32_t checksum = crc32(0, NULL, 0);
    bool eof = false;
    int status = -1;
    while (true) {
        if (!decoder.avail_in && !eof) {
            ssize_t n = arc_stream_read(zip->base.stream, input, sizeof(input));
            if (n < 0) goto done;
            decoder.next_in = input; decoder.avail_in = (uInt)n; eof = !n;
        }
        decoder.next_out = output; decoder.avail_out = sizeof(output);
        uInt before = decoder.avail_in;
        int result = inflate(&decoder, Z_NO_FLUSH);
        compressed += before - decoder.avail_in;
        size_t size = sizeof(output) - decoder.avail_out;
        if (size > limit - uncompressed) { errno = EOVERFLOW; goto done; }
        uncompressed += size; checksum = crc32(checksum, output, (uInt)size);
        if (result == Z_STREAM_END) break;
        if ((result != Z_OK && result != Z_BUF_ERROR) ||
            (before == decoder.avail_in && !size && (eof || decoder.avail_in))) { errno = EINVAL; goto done; }
    }
    if (compressed > (uint64_t)(zip->stream_size - start)) { errno = EINVAL; goto done; }
    if (descriptor_at(zip, start + (int64_t)compressed, checksum, compressed, uncompressed,
                      entry->has_zip64_fields || entry->version_needed >= 45, next)) goto done;
    *packed = compressed; *unpacked = uncompressed; entry->crc32 = checksum;
    status = 0;
done:
    inflateEnd(&decoder);
    return status;
}

static int zip_read_entry_streaming(ZipReader *reader) {
    if (reader->eof) return 1;
    if (arc_stream_seek(reader->base.stream, reader->stream_pos, SEEK_SET)) return -1;
    struct ZipCentralDirEntry entry;
    int64_t header;
    int result = read_local_file_header(reader->base.stream, &header, &entry, reader->base.limits);
    if (result < 0) return -1;
    if (result == 1) {
        if (!reader->stream_entry_count) { errno = EINVAL; return -1; }
        reader->eof = true; return 1;
    }
    const ArcLimits *limits = reader->base.limits;
    if (reader->stream_entry_count >= limits->max_entries) { errno = EOVERFLOW; goto fail; }
    int64_t data_start = arc_stream_tell(reader->base.stream);
    uint64_t packed = entry.zip64_compressed_size, unpacked = entry.zip64_uncompressed_size;
    int64_t next;
    if (data_start < 0 || data_start > reader->stream_size || packed > (uint64_t)(reader->stream_size - data_start)) {
        errno = EINVAL; goto fail;
    }
    if (entry.flags & ZIP_FLAG_DATA_DESCRIPTOR) {
        if (entry.flags & ZIP_FLAG_ENCRYPTED) { errno = ENOTSUP; goto fail; }
        uint64_t limit = limits->max_uncompressed_bytes - reader->base.declared_bytes;
        if (entry.compression_method == ZIP_METHOD_STORE)
            result = recover_stored_descriptor(reader, &entry, data_start, limit, &packed, &unpacked, &next);
        else if (entry.compression_method == ZIP_METHOD_DEFLATE)
            result = recover_deflated_descriptor(reader, &entry, data_start, limit, &packed, &unpacked, &next);
        else { errno = ENOTSUP; result = -1; }
        if (result) goto fail;
        entry.zip64_compressed_size = packed; entry.zip64_uncompressed_size = unpacked;
        entry.has_zip64_fields = true;
    } else next = data_start + (int64_t)packed;
    if (add_stream_entry(reader, &entry)) goto fail;
    free_central_dir_entry(&entry);
    reader->stream_pos = next;
    return select_zip_entry(reader, &reader->stream_entries[reader->stream_entry_count - 1]);
fail:
    free_central_dir_entry(&entry);
    return -1;
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

typedef struct ZipCheckedData {
    ArcStream *data;
    uint64_t size, expected_size;
    uint32_t checksum, expected_checksum;
    bool failed;
} ZipCheckedData;
static ssize_t checked_zip_read(ArcStream *stream, void *buffer, size_t size) {
    if (!size) return 0;
    ZipCheckedData *data = stream->user_data;
    if (data->failed) { errno = EIO; return -1; }
    if (size > UINT_MAX) size = UINT_MAX;
    ssize_t n = arc_stream_read_fully(data->data, buffer, size);
    if (n < 0) { data->failed = true; return -1; }
    if (n > 0) {
        if ((uint64_t)n > data->expected_size - data->size) { data->failed = true; errno = EIO; return -1; }
        data->size += (uint64_t)n;
        data->checksum = crc32(data->checksum, buffer, (uInt)n);
    } else if (n == 0 && (data->size != data->expected_size || data->checksum != data->expected_checksum)) {
        data->failed = true; errno = EIO; return -1;
    }
    return n;
}
static int64_t checked_zip_tell(ArcStream *stream) {
    return (int64_t)((ZipCheckedData *)stream->user_data)->size;
}
static void checked_zip_close(ArcStream *stream) {
    ZipCheckedData *data = stream->user_data;
    arc_stream_close(data->data); free(data); free(stream);
}
static ArcStream *checked_zip(ArcStream *source, uint64_t expected_size, uint32_t expected_crc) {
    static const struct ArcStreamVtable table = {checked_zip_read, NULL, checked_zip_tell, checked_zip_close};
    ZipCheckedData *data = calloc(1, sizeof(*data));
    ArcStream *stream = calloc(1, sizeof(*stream));
    if (!data || !stream) { free(data); free(stream); arc_stream_close(source); return NULL; }
    data->data = source; data->expected_size = expected_size; data->expected_checksum = expected_crc;
    stream->vtable = &table; stream->user_data = data;
    return stream;
}

ArcStream *arc_zip_open_data(ArcReader *reader) {
    if (!reader) {
        return NULL;
    }
    ZipReader *zip = (ZipReader *)reader;
    if (!zip->entry_valid) {
        return NULL;
    }
    
    if (zip->entry_flags & ZIP_FLAG_ENCRYPTED) { errno = ENOTSUP; return NULL; }
    if (arc_stream_seek(zip->base.stream, zip->entry_data_offset, SEEK_SET)) return NULL;
    struct ZipCentralDirEntry local;
    int64_t header;
    int result = read_local_file_header(zip->base.stream, &header, &local, zip->base.limits);
    if (result) { if (result > 0) errno = EINVAL; return NULL; }
    const struct ZipCentralDirEntry *expected = zip->streaming_mode
        ? &zip->stream_entries[zip->stream_entry_count - 1] : &zip->entries[zip->current_entry_index - 1];
    bool consistent = local.flags == expected->flags && local.compression_method == expected->compression_method &&
        !strcmp(local.filename, expected->filename);
    if (!(local.flags & ZIP_FLAG_DATA_DESCRIPTOR)) consistent = consistent && local.crc32 == zip->entry_crc32 &&
        local.zip64_compressed_size == (uint64_t)zip->entry_data_remaining && local.zip64_uncompressed_size == zip->entry_uncompressed_size;
    bool wide = local.has_zip64_fields || local.version_needed >= 45;
    free_central_dir_entry(&local);
    int64_t data_start = arc_stream_tell(zip->base.stream);
    int64_t boundary = zip->streaming_mode ? zip->stream_size : zip->central_dir_offset;
    if (!consistent || data_start < 0 || data_start > boundary || zip->entry_data_remaining < 0 ||
        zip->entry_data_remaining > boundary - data_start) { errno = EINVAL; return NULL; }
    if (zip->entry_flags & ZIP_FLAG_DATA_DESCRIPTOR) {
        int64_t next;
        if (descriptor_at(zip, data_start + zip->entry_data_remaining, zip->entry_crc32,
                          (uint64_t)zip->entry_data_remaining, zip->entry_uncompressed_size, wide, &next)) return NULL;
    }

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
        int64_t out_limit = zip->entry_uncompressed_size < INT64_MAX ? (int64_t)zip->entry_uncompressed_size + 1 : INT64_MAX;
        if (zip->base.limits && zip->base.limits->max_uncompressed_bytes > 0) {
            if (out_limit <= 0 || (uint64_t)out_limit > zip->base.limits->max_uncompressed_bytes) {
                out_limit = (int64_t)zip->base.limits->max_uncompressed_bytes;
            }
        }
        ArcStream *decompressed = arc_filter_deflate(data_stream, out_limit);
        if (decompressed) {
            ArcStream *owned = arc_stream_with_parent(decompressed, data_stream);
            if (!owned) { arc_stream_close(decompressed); arc_stream_close(data_stream); return NULL; }
            return checked_zip(owned, zip->entry_uncompressed_size, zip->entry_crc32);
        }
        // Fall through to return compressed stream if filter fails
        arc_stream_close(data_stream);
        return NULL;
    } else if (zip->entry_compression_method != ZIP_METHOD_STORE) {
        // Unsupported compression method
        arc_stream_close(data_stream);
        return NULL;
    }
    
    return checked_zip(data_stream, zip->entry_uncompressed_size, zip->entry_crc32);
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
    arc_base_set_limits(&zip->base, limits);
    limits = zip->base.limits;
    zip->entry_valid = false;
    zip->eof = false;
    zip->current_entry_index = 0;
    zip->streaming_mode = false;
    zip->stream_pos = 0;
    zip->stream_entries = NULL;
    zip->stream_entry_count = 0;
    zip->stream_entry_capacity = 0;
    
    // Try to find End of Central Directory (for fast listing)
    struct ZipEOCD eocd = {0};
    struct Zip64EOCDRecord eocd64;
    memset(&eocd64, 0, sizeof(eocd64));
    
    int eocd_found = find_eocd(stream, &eocd, &eocd64, limits);
    if (eocd_found < 0) { free(zip); return NULL; }
    if (arc_stream_seek(stream, 0, SEEK_END)) { free(zip); return NULL; }
    zip->stream_size = arc_stream_tell(stream);
    if (zip->stream_size < 0) { free(zip); return NULL; }
    
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

