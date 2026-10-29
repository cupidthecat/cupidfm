#include "arc_filter.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h>
#include <zlib.h>
#include <bzlib.h>

// Forward declarations
static ssize_t gzip_read(ArcStream *stream, void *buf, size_t n);
static int gzip_seek(ArcStream *stream, int64_t off, int whence);
static int64_t gzip_tell(ArcStream *stream);
static void gzip_close(ArcStream *stream);

static ssize_t bzip2_read(ArcStream *stream, void *buf, size_t n);
static int bzip2_seek(ArcStream *stream, int64_t off, int whence);
static int64_t bzip2_tell(ArcStream *stream);
static void bzip2_close(ArcStream *stream);

// Vtables
static const struct ArcStreamVtable gzip_vtable = {
    .read = gzip_read,
    .seek = gzip_seek,
    .tell = gzip_tell,
    .close = gzip_close,
};

static const struct ArcStreamVtable bzip2_vtable = {
    .read = bzip2_read,
    .seek = bzip2_seek,
    .tell = bzip2_tell,
    .close = bzip2_close,
};

// Gzip filter implementation
struct GzipFilterData {
    ArcStream *underlying;
    z_stream zs;
    uint8_t *in_buf;
    size_t in_buf_size;
    bool eof;
    bool initialized;
    bool member_end;
    bool input_eof;
    int error;
};

static ssize_t filter_error(int *error, int code) {
    *error = code ? code : EIO;
    errno = *error;
    return -1;
}

static ssize_t gzip_read(ArcStream *stream, void *buf, size_t n) {
    struct GzipFilterData *data = stream->user_data;
    if (!n) return 0;
    if (data->error) { errno = data->error; return -1; }
    if (data->eof) return 0;
    if (!data->initialized) {
        int result = inflateInit2(&data->zs, 16 + MAX_WBITS);
        if (result != Z_OK) return filter_error(&data->error, result == Z_MEM_ERROR ? ENOMEM : EINVAL);
        data->initialized = true;
    }
    if (n > UINT_MAX) n = UINT_MAX;
    if (stream->byte_limit > 0) {
        int64_t remaining = stream->byte_limit - stream->bytes_read;
        if (remaining <= 0) {
            uint8_t extra;
            int64_t limit = stream->byte_limit;
            stream->byte_limit = 0;
            ssize_t result = gzip_read(stream, &extra, 1);
            stream->byte_limit = limit;
            if (result > 0) return filter_error(&data->error, EOVERFLOW);
            return result;
        }
        if (n > (uint64_t)remaining) n = (size_t)remaining;
    }
    data->zs.next_out = buf;
    data->zs.avail_out = (uInt)n;
    while (data->zs.avail_out) {
        if (!data->zs.avail_in && !data->input_eof) {
            ssize_t read = arc_stream_read(data->underlying, data->in_buf, data->in_buf_size);
            if (read < 0) return filter_error(&data->error, errno);
            data->input_eof = read == 0;
            data->zs.next_in = data->in_buf;
            data->zs.avail_in = (uInt)read;
        }
        if (data->member_end) {
            if (!data->zs.avail_in && data->input_eof) { data->eof = true; break; }
            Bytef *input = data->zs.next_in, *output = data->zs.next_out;
            uInt available = data->zs.avail_in, space = data->zs.avail_out;
            int result = inflateReset2(&data->zs, 16 + MAX_WBITS);
            if (result != Z_OK) return filter_error(&data->error, result == Z_MEM_ERROR ? ENOMEM : EINVAL);
            data->zs.next_in = input; data->zs.avail_in = available;
            data->zs.next_out = output; data->zs.avail_out = space;
            data->member_end = false;
        }
        uInt input_before = data->zs.avail_in, output_before = data->zs.avail_out;
        int result = inflate(&data->zs, Z_NO_FLUSH);
        if (result == Z_STREAM_END) { data->member_end = true; continue; }
        if (result != Z_OK && result != Z_BUF_ERROR)
            return filter_error(&data->error, result == Z_MEM_ERROR ? ENOMEM : EINVAL);
        if (input_before == data->zs.avail_in && output_before == data->zs.avail_out &&
            (data->input_eof || data->zs.avail_in)) return filter_error(&data->error, EINVAL);
    }
    size_t size = n - data->zs.avail_out;
    if (size > (uint64_t)(INT64_MAX - stream->bytes_read)) return filter_error(&data->error, EOVERFLOW);
    stream->bytes_read += (int64_t)size;
    return (ssize_t)size;
}

static int gzip_seek(ArcStream *stream, int64_t off, int whence) {
    // Gzip doesn't support seeking (streaming decompression)
    (void)stream;
    (void)off;
    (void)whence;
    errno = ESPIPE;
    return -1;
}

static int64_t gzip_tell(ArcStream *stream) {
    return stream->bytes_read;
}

static void gzip_close(ArcStream *stream) {
    struct GzipFilterData *data = (struct GzipFilterData *)stream->user_data;
    if (data->initialized) {
        inflateEnd(&data->zs);
    }
    free(data->in_buf);
    // Note: We don't close underlying - caller owns it
    free(data);
    free(stream);
}

ArcStream *arc_filter_gzip(ArcStream *underlying, int64_t byte_limit) {
    if (!underlying) {
        return NULL;
    }
    
    ArcStream *stream = calloc(1, sizeof(ArcStream));
    if (!stream) {
        return NULL;
    }
    
    struct GzipFilterData *data = calloc(1, sizeof(struct GzipFilterData));
    if (!data) {
        free(stream);
        return NULL;
    }
    
    data->underlying = underlying;
    data->in_buf_size = 64 * 1024; // 64KB input buffer
    data->in_buf = malloc(data->in_buf_size);
    if (!data->in_buf) {
        free(data);
        free(stream);
        return NULL;
    }
    
    data->eof = false;
    data->initialized = false;
    
    stream->vtable = &gzip_vtable;
    stream->byte_limit = byte_limit;
    stream->bytes_read = 0;
    stream->user_data = data;
    
    return stream;
}

// Bzip2 filter implementation
struct Bzip2FilterData {
    ArcStream *underlying;
    bz_stream bzs;
    uint8_t *in_buf;
    size_t in_buf_size;
    bool eof;
    bool initialized;
    bool member_end;
    bool input_eof;
    int error;
};

static ssize_t bzip2_read(ArcStream *stream, void *buf, size_t n) {
    struct Bzip2FilterData *data = stream->user_data;
    if (!n) return 0;
    if (data->error) { errno = data->error; return -1; }
    if (data->eof) return 0;
    if (!data->initialized) {
        int result = BZ2_bzDecompressInit(&data->bzs, 0, 0);
        if (result != BZ_OK) return filter_error(&data->error, result == BZ_MEM_ERROR ? ENOMEM : EINVAL);
        data->initialized = true;
    }
    if (n > UINT_MAX) n = UINT_MAX;
    if (stream->byte_limit > 0) {
        int64_t remaining = stream->byte_limit - stream->bytes_read;
        if (remaining <= 0) {
            uint8_t extra;
            int64_t limit = stream->byte_limit;
            stream->byte_limit = 0;
            ssize_t result = bzip2_read(stream, &extra, 1);
            stream->byte_limit = limit;
            if (result > 0) return filter_error(&data->error, EOVERFLOW);
            return result;
        }
        if (n > (uint64_t)remaining) n = (size_t)remaining;
    }
    data->bzs.next_out = buf;
    data->bzs.avail_out = (unsigned)n;
    while (data->bzs.avail_out) {
        if (!data->bzs.avail_in && !data->input_eof) {
            ssize_t read = arc_stream_read(data->underlying, data->in_buf, data->in_buf_size);
            if (read < 0) return filter_error(&data->error, errno);
            data->input_eof = read == 0;
            data->bzs.next_in = (char *)data->in_buf;
            data->bzs.avail_in = (unsigned)read;
        }
        if (data->member_end) {
            if (!data->bzs.avail_in && data->input_eof) { data->eof = true; break; }
            char *input = data->bzs.next_in, *output = data->bzs.next_out;
            unsigned available = data->bzs.avail_in, space = data->bzs.avail_out;
            BZ2_bzDecompressEnd(&data->bzs);
            data->initialized = false;
            memset(&data->bzs, 0, sizeof(data->bzs));
            int result = BZ2_bzDecompressInit(&data->bzs, 0, 0);
            if (result != BZ_OK) return filter_error(&data->error, result == BZ_MEM_ERROR ? ENOMEM : EINVAL);
            data->initialized = true;
            data->bzs.next_in = input; data->bzs.avail_in = available;
            data->bzs.next_out = output; data->bzs.avail_out = space;
            data->member_end = false;
        }
        unsigned input_before = data->bzs.avail_in, output_before = data->bzs.avail_out;
        int result = BZ2_bzDecompress(&data->bzs);
        if (result == BZ_STREAM_END) { data->member_end = true; continue; }
        if (result != BZ_OK) return filter_error(&data->error, result == BZ_MEM_ERROR ? ENOMEM : EINVAL);
        if (input_before == data->bzs.avail_in && output_before == data->bzs.avail_out &&
            (data->input_eof || data->bzs.avail_in)) return filter_error(&data->error, EINVAL);
    }
    size_t size = n - data->bzs.avail_out;
    if (size > (uint64_t)(INT64_MAX - stream->bytes_read)) return filter_error(&data->error, EOVERFLOW);
    stream->bytes_read += (int64_t)size;
    return (ssize_t)size;
}

static int bzip2_seek(ArcStream *stream, int64_t off, int whence) {
    // Bzip2 doesn't support seeking (streaming decompression)
    (void)stream;
    (void)off;
    (void)whence;
    errno = ESPIPE;
    return -1;
}

static int64_t bzip2_tell(ArcStream *stream) {
    return stream->bytes_read;
}

static void bzip2_close(ArcStream *stream) {
    struct Bzip2FilterData *data = (struct Bzip2FilterData *)stream->user_data;
    if (data->initialized) {
        BZ2_bzDecompressEnd(&data->bzs);
    }
    free(data->in_buf);
    // Note: We don't close underlying - caller owns it
    free(data);
    free(stream);
}

ArcStream *arc_filter_bzip2(ArcStream *underlying, int64_t byte_limit) {
    if (!underlying) {
        return NULL;
    }
    
    ArcStream *stream = calloc(1, sizeof(ArcStream));
    if (!stream) {
        return NULL;
    }
    
    struct Bzip2FilterData *data = calloc(1, sizeof(struct Bzip2FilterData));
    if (!data) {
        free(stream);
        return NULL;
    }
    
    data->underlying = underlying;
    data->in_buf_size = 64 * 1024; // 64KB input buffer
    data->in_buf = malloc(data->in_buf_size);
    if (!data->in_buf) {
        free(data);
        free(stream);
        return NULL;
    }
    
    data->eof = false;
    data->initialized = false;
    
    stream->vtable = &bzip2_vtable;
    stream->byte_limit = byte_limit;
    stream->bytes_read = 0;
    stream->user_data = data;
    
    return stream;
}

// Raw deflate filter implementation (for ZIP format)
static ssize_t deflate_read(ArcStream *stream, void *buf, size_t n);
static int deflate_seek(ArcStream *stream, int64_t off, int whence);
static int64_t deflate_tell(ArcStream *stream);
static void deflate_close(ArcStream *stream);

static const struct ArcStreamVtable deflate_vtable = {
    .read = deflate_read,
    .seek = deflate_seek,
    .tell = deflate_tell,
    .close = deflate_close,
};

struct DeflateFilterData {
    ArcStream *underlying;
    z_stream zs;
    uint8_t *in_buf;
    size_t in_buf_size;
    bool eof;
    bool initialized;
};

/* ZIP callers bound this input to the declared compressed size. An early
   end marker must not hide bytes counted as compressed payload. */
static int deflate_finish(struct DeflateFilterData *data) {
    if (data->zs.avail_in) { errno = EINVAL; return -1; }
    uint8_t extra;
    ssize_t n = arc_stream_read(data->underlying, &extra, 1);
    if (n < 0) return -1;
    if (n) { errno = EINVAL; return -1; }
    data->eof = true;
    return 0;
}

static ssize_t deflate_read(ArcStream *stream, void *buf, size_t n) {
    struct DeflateFilterData *data = (struct DeflateFilterData *)stream->user_data;
    
    if (!data->initialized) {
        memset(&data->zs, 0, sizeof(data->zs));
        // Use -MAX_WBITS for raw deflate (no gzip wrapper)
        if (inflateInit2(&data->zs, -MAX_WBITS) != Z_OK) {
            errno = ENOMEM; return -1;
        }
        data->initialized = true;
    }
    
    if (data->eof) {
        return 0;
    }
    
    // Enforce byte limit
    if (stream->byte_limit > 0) {
        int64_t remaining = stream->byte_limit - stream->bytes_read;
        if (remaining <= 0) {
            uint8_t extra;
            int64_t limit = stream->byte_limit;
            stream->byte_limit = 0;
            ssize_t result = deflate_read(stream, &extra, 1);
            stream->byte_limit = limit;
            if (result > 0) { errno = EOVERFLOW; return -1; }
            return result;
        }
        if ((int64_t)n > remaining) {
            n = (size_t)remaining;
        }
    }
    
    data->zs.next_out = (Bytef *)buf;
    if (n > UINT_MAX) n = UINT_MAX;
    data->zs.avail_out = (uInt)n;
    
    while (data->zs.avail_out > 0 && !data->eof) {
        // Read more input if needed
        if (data->zs.avail_in == 0) {
            ssize_t in_read = arc_stream_read(data->underlying, data->in_buf, data->in_buf_size);
            if (in_read < 0) {
                return -1;
            }
            if (in_read == 0) {
                // No more input, finish decompression
                // Loop until we get Z_STREAM_END or an error
                size_t output_before = n - data->zs.avail_out;
                while (data->zs.avail_out > 0) {
                    int ret = inflate(&data->zs, Z_FINISH);
                    if (ret == Z_STREAM_END) {
                        if (deflate_finish(data)) return -1;
                        break;
                    }
                    if (ret == Z_BUF_ERROR) {
                        // Z_BUF_ERROR means no progress possible
                        // Check if we made any progress since input was exhausted
                        size_t output_after = n - data->zs.avail_out;
                        if (output_after == output_before) {
                            // No progress made, input exhausted, not at stream end
                            // This indicates a truncated stream
                            errno = EINVAL;
                            return -1;
                        }
                        // Made progress, continue trying
                        output_before = output_after;
                        continue;
                    }
                    if (ret != Z_OK) {
                        // Other error (Z_DATA_ERROR, Z_MEM_ERROR, etc.)
                        errno = ret == Z_MEM_ERROR ? ENOMEM : EINVAL; return -1;
                    }
                    // Z_OK means we made progress, continue
                    size_t output_after = n - data->zs.avail_out;
                    output_before = output_after;
                }
                break;
            }
            data->zs.next_in = data->in_buf;
            data->zs.avail_in = (uInt)in_read;
        }
        
        size_t output_before = n - data->zs.avail_out;
        int ret = inflate(&data->zs, Z_NO_FLUSH);
        if (ret == Z_STREAM_END) {
            if (deflate_finish(data)) return -1;
            break;
        }
        if (ret == Z_BUF_ERROR) {
            // Z_BUF_ERROR: check if we made progress
            size_t output_after = n - data->zs.avail_out;
            if (output_after == output_before && data->zs.avail_in == 0) {
                // No progress and no input available - try reading more
                // (will be handled in next iteration)
                continue;
            }
            // Made progress or have input, continue
            continue;
        }
        if (ret != Z_OK) {
            // Other error
            errno = ret == Z_MEM_ERROR ? ENOMEM : EINVAL; return -1;
        }
    }
    
    size_t decompressed = n - data->zs.avail_out;
    stream->bytes_read += decompressed;
    return (ssize_t)decompressed;
}

static int deflate_seek(ArcStream *stream, int64_t off, int whence) {
    // Deflate doesn't support seeking (streaming decompression)
    (void)stream;
    (void)off;
    (void)whence;
    errno = ESPIPE;
    return -1;
}

static int64_t deflate_tell(ArcStream *stream) {
    return stream->bytes_read;
}

static void deflate_close(ArcStream *stream) {
    struct DeflateFilterData *data = (struct DeflateFilterData *)stream->user_data;
    if (data->initialized) {
        inflateEnd(&data->zs);
    }
    free(data->in_buf);
    // Note: We don't close underlying - caller owns it
    free(data);
    free(stream);
}

ArcStream *arc_filter_deflate(ArcStream *underlying, int64_t byte_limit) {
    if (!underlying) {
        return NULL;
    }
    
    ArcStream *stream = calloc(1, sizeof(ArcStream));
    if (!stream) {
        return NULL;
    }
    
    struct DeflateFilterData *data = calloc(1, sizeof(struct DeflateFilterData));
    if (!data) {
        free(stream);
        return NULL;
    }
    
    data->underlying = underlying;
    data->in_buf_size = 64 * 1024; // 64KB input buffer
    data->in_buf = malloc(data->in_buf_size);
    if (!data->in_buf) {
        free(data);
        free(stream);
        return NULL;
    }
    
    data->eof = false;
    data->initialized = false;
    
    stream->vtable = &deflate_vtable;
    stream->byte_limit = byte_limit;
    stream->bytes_read = 0;
    stream->user_data = data;
    
    return stream;
}

