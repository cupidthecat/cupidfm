#define _POSIX_C_SOURCE 200809L
#include "arc_reader.h"
#include "arc_7z.h"
#include "arc_base.h"
#include "arc_compressed.h"
#include "arc_filter.h"
#include "arc_tar.h"
#include "arc_zip.h"

// Compression type constants (from arc_compressed.h)
#define ARC_COMPRESSED_GZIP 0
#define ARC_COMPRESSED_BZIP2 1
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

// Forward declarations
static int detect_format(ArcStream *stream, ArcStream **decompressed, int *compression_type,
                         const char *path);
static ArcReader *create_reader(ArcStream *stream, int format, const char *path,
                                int compression_type, ArcStream *original_stream,
                                const ArcLimits *limits);

// TAR detection helpers
static bool is_tar_zero_block(const uint8_t *block) {
    for (size_t i = 0; i < 512; i++) {
        if (block[i] != 0) {
            return false;
        }
    }
    return true;
}

static bool path_looks_like_tar(const char *path) {
    if (!path)
        return false;
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    const char *suffixes[] = {".tar.gz", ".tar.bz2", ".tar.xz", ".tgz", ".tbz", ".tbz2", ".txz"};
    size_t length = strlen(name);
    for (size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); i++) {
        size_t suffix = strlen(suffixes[i]);
        if (length >= suffix && !strcasecmp(name + length - suffix, suffixes[i]))
            return true;
    }
    return false;
}

// Default limits (safe baseline)
static const ArcLimits ARC_DEFAULT_LIMITS = {
    .max_entries = 1000000ULL,
    .max_name = 4096ULL,
    .max_extra = 65534ULL,
    .max_uncompressed_bytes = 1024ULL * 1024ULL * 1024ULL, // 1 GiB
    .max_nested_depth = 64ULL,
};

const ArcLimits *arc_default_limits(void) {
    return &ARC_DEFAULT_LIMITS;
}

static const ArcLimits *normalize_limits(const ArcLimits *in) {
    const ArcLimits *d = arc_default_limits();
    if (!in)
        return d;
    // Treat 0 as "use default" per-field
    static _Thread_local ArcLimits merged;
    merged.max_entries = in->max_entries ? in->max_entries : d->max_entries;
    merged.max_name = in->max_name ? in->max_name : d->max_name;
    merged.max_extra = in->max_extra ? in->max_extra : d->max_extra;
    merged.max_uncompressed_bytes =
        in->max_uncompressed_bytes ? in->max_uncompressed_bytes : d->max_uncompressed_bytes;
    if (merged.max_uncompressed_bytes > INT64_MAX)
        merged.max_uncompressed_bytes = INT64_MAX;
    merged.max_nested_depth = in->max_nested_depth ? in->max_nested_depth : d->max_nested_depth;
    return &merged;
}

// Format types (must match arc_tar.c, arc_zip.c, arc_compressed.c, arc_7z.c)
#define ARC_FORMAT_TAR 0
#define ARC_FORMAT_ZIP 1
#define ARC_FORMAT_COMPRESSED 2
#define ARC_FORMAT_7Z 3

int arc_next(ArcReader *reader, ArcEntry *entry) {
    if (!reader || !entry) {
        return -1;
    }
    ArcReaderBase *base = (ArcReaderBase *)reader;
    if (base->terminal_error) {
        errno = base->terminal_error;
        return -1;
    }
    // Check format field using safe accessor
    int format = arc_reader_format(reader);
    int result;
    switch (format) {
        case ARC_FORMAT_TAR:
            result = arc_tar_next(reader, entry);
            break;
        case ARC_FORMAT_ZIP:
            result = arc_zip_next(reader, entry);
            break;
        case ARC_FORMAT_COMPRESSED:
            result = arc_compressed_next(reader, entry);
            break;
        case ARC_FORMAT_7Z:
            result = arc_7z_next(reader, entry);
            break;
        default:
            return -1;
    }
    if (result < 0) {
        base->terminal_error = errno ? errno : EIO;
        errno = base->terminal_error;
        return -1;
    }
    if (result == 1 && base->drain_on_eof) {
        uint8_t buffer[65536];
        ssize_t n;
        while ((n = arc_stream_read(base->stream, buffer, sizeof(buffer))) > 0) {
        }
        if (n < 0) {
            base->terminal_error = errno ? errno : EIO;
            errno = base->terminal_error;
            return -1;
        }
        base->drain_on_eof = false;
    }
    if (result == 0) {
        const ArcLimits *lim = base->limits ? base->limits : arc_default_limits();
        if (++base->entries_seen > lim->max_entries || !entry->path ||
            strlen(entry->path) > lim->max_name || entry->size > lim->max_uncompressed_bytes ||
            base->declared_bytes > lim->max_uncompressed_bytes - entry->size) {
            arc_entry_free(entry);
            base->terminal_error = EOVERFLOW;
            errno = EOVERFLOW;
            return -1;
        }
        base->declared_bytes += entry->size;
    }
    return result;
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
    int format = arc_reader_format(reader);
    switch (format) {
        case ARC_FORMAT_TAR:
            return arc_tar_open_data(reader);
        case ARC_FORMAT_ZIP:
            return arc_zip_open_data(reader);
        case ARC_FORMAT_COMPRESSED:
            return arc_compressed_open_data(reader);
        case ARC_FORMAT_7Z:
            return arc_7z_open_data(reader);
        default:
            return NULL;
    }
}

int arc_skip_data(ArcReader *reader) {
    if (!reader) {
        return -1;
    }
    int format = arc_reader_format(reader);
    switch (format) {
        case ARC_FORMAT_TAR:
            return arc_tar_skip_data(reader);
        case ARC_FORMAT_ZIP:
            return arc_zip_skip_data(reader);
        case ARC_FORMAT_COMPRESSED:
            return arc_compressed_skip_data(reader);
        case ARC_FORMAT_7Z:
            return arc_7z_skip_data(reader);
        default:
            return -1;
    }
}

void arc_close(ArcReader *reader) {
    if (reader) {
        int format = arc_reader_format(reader);
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
            case ARC_FORMAT_7Z:
                arc_7z_close(reader);
                break;
            default:
                /* No public constructor produces an unknown format. */
                break;
        }
    }
}

/* Fill a small detection prefix even when a stream returns short reads. */
static ssize_t read_prefix(ArcStream *stream, void *buffer, size_t size) {
    size_t total = 0;
    while (total < size) {
        ssize_t n = arc_stream_read(stream, (uint8_t *)buffer + total, size - total);
        if (n < 0)
            return -1;
        if (!n)
            break;
        total += (size_t)n;
    }
    return (ssize_t)total;
}

static ArcStream *compression_filter(ArcStream *stream, int compression, int64_t limit) {
    switch (compression) {
        case ARC_COMPRESSED_GZIP:
            return arc_filter_gzip(stream, limit);
        case ARC_COMPRESSED_BZIP2:
            return arc_filter_bzip2(stream, limit);
        case ARC_COMPRESSED_XZ:
            return arc_filter_xz(stream, limit);
        default:
            errno = EINVAL;
            return NULL;
    }
}

/* ZIP and 7z need random access. Spool their decoded contents to an anonymous
 * file; the decompression filter enforces the decoded byte limit. */
static ArcStream *seekable_copy(ArcStream *stream) {
    FILE *temporary = tmpfile();
    if (!temporary)
        return NULL;
    uint8_t buffer[65536];
    ssize_t n;
    while ((n = arc_stream_read(stream, buffer, sizeof(buffer))) > 0) {
        if (fwrite(buffer, 1, (size_t)n, temporary) != (size_t)n) {
            int saved = errno;
            fclose(temporary);
            errno = saved;
            return NULL;
        }
    }
    if (n < 0 || fflush(temporary) || fseek(temporary, 0, SEEK_SET)) {
        int saved = errno;
        fclose(temporary);
        errno = saved;
        return NULL;
    }
    int fd = fcntl(fileno(temporary), F_DUPFD_CLOEXEC, 0);
    int saved = errno;
    if (fclose(temporary) && fd >= 0) {
        saved = errno;
        close(fd);
        fd = -1;
    }
    if (fd < 0) {
        errno = saved;
        return NULL;
    }
    ArcStream *result = arc_stream_from_fd(fd, 0);
    if (!result)
        close(fd);
    return result;
}

/* On failure the caller retains the supplied stream. Successful readers own it. */
static ArcReader *open_reader(ArcStream *stream, const char *path, const ArcLimits *limits) {
    ArcStream *sniff = NULL, *decoded = NULL;
    int compression = -1;
    int format = detect_format(stream, &sniff, &compression, path);
    int saved = errno;
    arc_stream_close(sniff);
    if (format < 0) {
        errno = saved;
        return NULL;
    }
    if (arc_stream_seek(stream, 0, SEEK_SET))
        return NULL;
    ArcStream *input = stream;
    if (compression >= 0) {
        decoded = compression_filter(stream, compression, (int64_t)limits->max_uncompressed_bytes);
        if (!decoded)
            return NULL;
        input = decoded;
        if (format == ARC_FORMAT_ZIP || format == ARC_FORMAT_7Z) {
            input = seekable_copy(decoded);
            saved = errno;
            arc_stream_close(decoded);
            decoded = input;
            if (!input) {
                errno = saved;
                return NULL;
            }
        }
    }
    ArcReader *reader = create_reader(input, format, path, compression,
                                      format == ARC_FORMAT_COMPRESSED ? stream : NULL, limits);
    if (!reader) {
        saved = errno;
        arc_stream_close(decoded);
        errno = saved;
        return NULL;
    }
    ArcReaderBase *base = (ArcReaderBase *)reader;
    base->owned_stream = input != stream ? stream : NULL;
    base->drain_on_eof =
        compression >= 0 && (format == ARC_FORMAT_TAR || format == ARC_FORMAT_COMPRESSED);
    return reader;
}

ArcFormat arc_format(const ArcReader *reader) {
    int format = arc_reader_format(reader);
    if (format < ARC_FORMAT_TAR || format > ARC_FORMAT_7Z)
        return ARC_FORMAT_UNKNOWN;
    return (ArcFormat)format;
}

ArcReader *arc_open_path(const char *path) {
    return arc_open_path_ex(path, NULL);
}

ArcReader *arc_open_path_ex(const char *path, const ArcLimits *limits_in) {
    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return NULL;
    struct stat st;
    if (fstat(fd, &st)) {
        int saved = errno;
        close(fd);
        errno = saved;
        return NULL;
    }
    if (!S_ISREG(st.st_mode)) {
        close(fd);
        errno = EINVAL;
        return NULL;
    }
    ArcStream *stream = arc_stream_from_fd(fd, 0);
    if (!stream) {
        close(fd);
        return NULL;
    }
    ArcReader *reader = open_reader(stream, path, normalize_limits(limits_in));
    if (!reader) {
        int saved = errno;
        arc_stream_close(stream);
        errno = saved;
    }
    return reader;
}

ArcReader *arc_open_stream(ArcStream *stream) {
    return arc_open_stream_ex(stream, NULL);
}

ArcReader *arc_open_stream_ex(ArcStream *stream, const ArcLimits *limits_in) {
    if (!stream) {
        errno = EINVAL;
        return NULL;
    }
    return open_reader(stream, NULL, normalize_limits(limits_in));
}

static int detect_format(ArcStream *source, ArcStream **decoded, int *compression,
                         const char *path) {
    *decoded = NULL;
    *compression = -1;
    if (arc_stream_seek(source, 0, SEEK_SET))
        return -1;
    uint8_t prefix[512];
    ssize_t n = read_prefix(source, prefix, 6);
    if (n < 0)
        return -1;
    if (n >= 2 && prefix[0] == 0x1f && prefix[1] == 0x8b)
        *compression = ARC_COMPRESSED_GZIP;
    else if (n >= 3 && !memcmp(prefix, "BZh", 3))
        *compression = ARC_COMPRESSED_BZIP2;
    else if (n >= 6 && !memcmp(prefix, "\xfd\x37\x7a\x58\x5a\x00", 6))
        *compression = ARC_COMPRESSED_XZ;
    ArcStream *input = source;
    if (*compression >= 0) {
        if (arc_stream_seek(source, 0, SEEK_SET))
            return -1;
        *decoded = compression_filter(source, *compression, 0);
        if (!*decoded)
            return -1;
        input = *decoded;
        n = read_prefix(input, prefix, 6);
        if (n < 0)
            return -1;
    }
    if (n >= 4 && (!memcmp(prefix, "PK\x03\x04", 4) || !memcmp(prefix, "PK\x05\x06", 4) ||
                   !memcmp(prefix, "PK\x01\x02", 4)))
        return ARC_FORMAT_ZIP;
    if (n >= 6 && !memcmp(prefix, "7z\xbc\xaf\x27\x1c", 6))
        return ARC_FORMAT_7Z;
    size_t filled = (size_t)n;
    n = read_prefix(input, prefix + filled, sizeof(prefix) - filled);
    if (n < 0)
        return -1;
    filled += (size_t)n;
    if (filled == sizeof(prefix) && (is_tar_zero_block(prefix) || arc_tar_header_valid(prefix)))
        return ARC_FORMAT_TAR;
    if (*compression >= 0 && !path_looks_like_tar(path))
        return ARC_FORMAT_COMPRESSED;
    errno = EINVAL;
    return -1;
}

static ArcReader *create_reader(ArcStream *stream, int format, const char *path,
                                int compression_type, ArcStream *original_stream,
                                const ArcLimits *limits) {
    switch (format) {
        case ARC_FORMAT_TAR: {
            ArcReader *r = arc_tar_open(stream);
            if (r)
                arc_base_set_limits((ArcReaderBase *)r, limits);
            return r;
        }
        case ARC_FORMAT_ZIP: {
            ArcReader *r = arc_zip_open_ex(stream, limits);
            if (r)
                arc_base_set_limits((ArcReaderBase *)r, limits);
            return r;
        }
        case ARC_FORMAT_COMPRESSED:
            // For compressed, stream is the decompressed filter, original_stream is the underlying
            // stream
            ArcReader *reader = arc_compressed_open(stream, path, compression_type);
            if (reader && original_stream) {
                // Store original stream in reader for cleanup
                arc_compressed_set_original_stream(reader, original_stream);
            }
            if (reader) {
                arc_base_set_limits((ArcReaderBase *)reader, limits);
            }
            return reader;
        case ARC_FORMAT_7Z: {
            ArcReader *r = arc_7z_open_ex(stream, limits);
            if (r)
                arc_base_set_limits((ArcReaderBase *)r, limits);
            return r;
        }
        default:
            return NULL;
    }
}
