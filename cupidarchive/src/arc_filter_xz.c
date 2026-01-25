#define _POSIX_C_SOURCE 200809L
#include "arc_filter.h"
#include "arc_stream.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>

#ifndef HAVE_LZMA
#  if defined(__has_include)
#    if __has_include(<lzma.h>)
#      include <lzma.h>
#      define HAVE_LZMA 1
#    else
#      define HAVE_LZMA 0
#    endif
#  else
#    include <lzma.h>
#    define HAVE_LZMA 1
#  endif
#endif

#if HAVE_LZMA

struct XzFilterData {
    ArcStream *underlying;
    lzma_stream zs;
    uint8_t *in_buf;
    size_t in_buf_size;
    bool eof;
    bool initialized;
};

static ssize_t xz_read(ArcStream *stream, void *buf, size_t n) {
    struct XzFilterData *data = (struct XzFilterData *)stream->user_data;

    if (!data->initialized) {
        data->zs = (lzma_stream)LZMA_STREAM_INIT;
        if (lzma_stream_decoder(&data->zs, UINT64_MAX, LZMA_CONCATENATED | LZMA_TELL_NO_CHECK) != LZMA_OK) {
            return -1;
        }
        data->initialized = true;
    }

    if (data->eof) {
        return 0;
    }

    if (stream->byte_limit > 0) {
        int64_t remaining = stream->byte_limit - stream->bytes_read;
        if (remaining <= 0) {
            return 0;
        }
        if ((int64_t)n > remaining) {
            n = (size_t)remaining;
        }
    }

    data->zs.next_out = (uint8_t *)buf;
    data->zs.avail_out = n;

    while (data->zs.avail_out > 0 && !data->eof) {
        if (data->zs.avail_in == 0) {
            ssize_t in_read = arc_stream_read(data->underlying, data->in_buf, data->in_buf_size);
            if (in_read < 0) {
                return -1;
            }
            if (in_read == 0) {
                size_t output_before = n - data->zs.avail_out;
                for (;;) {
                    lzma_ret ret = lzma_code(&data->zs, LZMA_FINISH);
                    if (ret == LZMA_STREAM_END) {
                        data->eof = true;
                        break;
                    }
                    if (ret == LZMA_BUF_ERROR) {
                        size_t output_after = n - data->zs.avail_out;
                        if (output_after == output_before) {
                            errno = EINVAL;
                            return -1;
                        }
                        output_before = output_after;
                        continue;
                    }
                    if (ret != LZMA_OK) {
                        return -1;
                    }
                    size_t output_after = n - data->zs.avail_out;
                    output_before = output_after;
                }
                break;
            }
            data->zs.next_in = data->in_buf;
            data->zs.avail_in = (size_t)in_read;
        }

        size_t output_before = n - data->zs.avail_out;
        lzma_ret ret = lzma_code(&data->zs, LZMA_RUN);
        if (ret == LZMA_STREAM_END) {
            data->eof = true;
            break;
        }
        if (ret == LZMA_BUF_ERROR) {
            size_t output_after = n - data->zs.avail_out;
            if (output_after == output_before && data->zs.avail_in == 0) {
                continue;
            }
            continue;
        }
        if (ret != LZMA_OK) {
            return -1;
        }
    }

    size_t decompressed = n - data->zs.avail_out;
    stream->bytes_read += decompressed;
    return (ssize_t)decompressed;
}

static int xz_seek(ArcStream *stream, int64_t off, int whence) {
    (void)stream;
    (void)off;
    (void)whence;
    errno = ESPIPE;
    return -1;
}

static int64_t xz_tell(ArcStream *stream) {
    (void)stream;
    errno = ESPIPE;
    return -1;
}

static void xz_close(ArcStream *stream) {
    if (!stream) {
        return;
    }
    struct XzFilterData *data = (struct XzFilterData *)stream->user_data;
    if (data) {
        lzma_end(&data->zs);
        free(data->in_buf);
        free(data);
    }
    free(stream);
}

static const struct ArcStreamVtable xz_vtable = {
    .read = xz_read,
    .seek = xz_seek,
    .tell = xz_tell,
    .close = xz_close,
};

ArcStream *arc_filter_xz(ArcStream *underlying, int64_t byte_limit) {
    if (!underlying) {
        errno = EINVAL;
        return NULL;
    }
    struct XzFilterData *data = calloc(1, sizeof(*data));
    if (!data) {
        return NULL;
    }
    data->underlying = underlying;
    data->in_buf_size = 64 * 1024;
    data->in_buf = malloc(data->in_buf_size);
    if (!data->in_buf) {
        free(data);
        return NULL;
    }

    ArcStream *stream = calloc(1, sizeof(*stream));
    if (!stream) {
        free(data->in_buf);
        free(data);
        return NULL;
    }

    stream->vtable = &xz_vtable;
    stream->byte_limit = byte_limit;
    stream->bytes_read = 0;
    stream->user_data = data;
    return stream;
}

#else

ArcStream *arc_filter_xz(ArcStream *underlying, int64_t byte_limit) {
    (void)underlying;
    (void)byte_limit;
    errno = ENOSYS;
    return NULL;
}

#endif // HAVE_LZMA

