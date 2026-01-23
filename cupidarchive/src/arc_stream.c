#include "arc_stream.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>

// Forward declarations for implementations
static ssize_t fd_read(ArcStream *stream, void *buf, size_t n);
static int fd_seek(ArcStream *stream, int64_t off, int whence);
static int64_t fd_tell(ArcStream *stream);
static void fd_close(ArcStream *stream);

static ssize_t mem_read(ArcStream *stream, void *buf, size_t n);
static int mem_seek(ArcStream *stream, int64_t off, int whence);
static int64_t mem_tell(ArcStream *stream);
static void mem_close(ArcStream *stream);

static ssize_t substream_read(ArcStream *stream, void *buf, size_t n);
static int substream_seek(ArcStream *stream, int64_t off, int whence);
static int64_t substream_tell(ArcStream *stream);
static void substream_close(ArcStream *stream);

// Vtables
static const struct ArcStreamVtable fd_vtable = {
    .read = fd_read,
    .seek = fd_seek,
    .tell = fd_tell,
    .close = fd_close,
};

static const struct ArcStreamVtable mem_vtable = {
    .read = mem_read,
    .seek = mem_seek,
    .tell = mem_tell,
    .close = mem_close,
};

static const struct ArcStreamVtable substream_vtable = {
    .read = substream_read,
    .seek = substream_seek,
    .tell = substream_tell,
    .close = substream_close,
};

// File descriptor stream implementation
struct FdStreamData {
    int fd;
    int64_t pos;
};

static ssize_t fd_read(ArcStream *stream, void *buf, size_t n) {
    struct FdStreamData *data = (struct FdStreamData *)stream->user_data;
    
    // Enforce byte limit
    if (stream->byte_limit > 0) {
        int64_t remaining = stream->byte_limit - stream->bytes_read;
        if (remaining <= 0) {
            return 0; // EOF (limit reached)
        }
        if ((int64_t)n > remaining) {
            n = (size_t)remaining;
        }
    }
    
    ssize_t ret = read(data->fd, buf, n);
    if (ret > 0) {
        stream->bytes_read += ret;
        // Update position based on actual file position (in case of seeks)
        off_t actual_pos = lseek(data->fd, 0, SEEK_CUR);
        if (actual_pos != (off_t)-1) {
            data->pos = actual_pos;
        } else {
            // Fallback: update by bytes read
            data->pos += ret;
        }
    } else if (ret == 0) {
        // EOF - update position to actual file position
        off_t actual_pos = lseek(data->fd, 0, SEEK_CUR);
        if (actual_pos != (off_t)-1) {
            data->pos = actual_pos;
        }
    }
    return ret;
}

static int fd_seek(ArcStream *stream, int64_t off, int whence) {
    struct FdStreamData *data = (struct FdStreamData *)stream->user_data;
    off_t result = lseek(data->fd, (off_t)off, whence);
    if (result == (off_t)-1) {
        return -1;
    }
    data->pos = result;
    // When seeking to the beginning, reset bytes_read to allow reading from start
    // This is important when recreating filters after format detection
    if (whence == SEEK_SET && off == 0) {
        stream->bytes_read = 0;
    }
    return 0;
}

static int64_t fd_tell(ArcStream *stream) {
    struct FdStreamData *data = (struct FdStreamData *)stream->user_data;
    return data->pos;
}

static void fd_close(ArcStream *stream) {
    struct FdStreamData *data = (struct FdStreamData *)stream->user_data;
    if (data && data->fd >= 0) {
        close(data->fd);
    }
    free(data);
    free(stream);
}

// Memory stream implementation
struct MemStreamData {
    const uint8_t *data;
    size_t size;
    size_t pos;
};

static ssize_t mem_read(ArcStream *stream, void *buf, size_t n) {
    struct MemStreamData *data = (struct MemStreamData *)stream->user_data;
    
    // Enforce byte limit
    if (stream->byte_limit > 0) {
        int64_t remaining = stream->byte_limit - stream->bytes_read;
        if (remaining <= 0) {
            return 0; // EOF (limit reached)
        }
        if ((int64_t)n > remaining) {
            n = (size_t)remaining;
        }
    }
    
    // Check bounds
    if (data->pos >= data->size) {
        return 0; // EOF
    }
    
    size_t available = data->size - data->pos;
    if (n > available) {
        n = available;
    }
    
    memcpy(buf, data->data + data->pos, n);
    data->pos += n;
    stream->bytes_read += n;
    
    return (ssize_t)n;
}

static int mem_seek(ArcStream *stream, int64_t off, int whence) {
    struct MemStreamData *data = (struct MemStreamData *)stream->user_data;
    size_t new_pos;
    
    switch (whence) {
        case SEEK_SET:
            new_pos = (size_t)off;
            break;
        case SEEK_CUR:
            new_pos = data->pos + (size_t)off;
            break;
        case SEEK_END:
            new_pos = data->size + (size_t)off;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    
    if (new_pos > data->size) {
        errno = EINVAL;
        return -1;
    }
    
    data->pos = new_pos;
    return 0;
}

static int64_t mem_tell(ArcStream *stream) {
    struct MemStreamData *data = (struct MemStreamData *)stream->user_data;
    return (int64_t)data->pos;
}

static void mem_close(ArcStream *stream) {
    struct MemStreamData *data = (struct MemStreamData *)stream->user_data;
    // Note: We don't free data->data - caller owns it
    free(data);
    free(stream);
}

// Substream implementation
struct SubstreamData {
    ArcStream *parent;
    int64_t offset;
    int64_t length;
    int64_t pos;
};

static ssize_t substream_read(ArcStream *stream, void *buf, size_t n) {
    struct SubstreamData *data = (struct SubstreamData *)stream->user_data;
    
    // Enforce byte limit
    if (stream->byte_limit > 0) {
        int64_t remaining = stream->byte_limit - stream->bytes_read;
        if (remaining <= 0) {
            return 0; // EOF (limit reached)
        }
        if ((int64_t)n > remaining) {
            n = (size_t)remaining;
        }
    }
    
    // Check substream bounds
    int64_t remaining = data->length - data->pos;
    if (remaining <= 0) {
        return 0; // EOF
    }
    if ((int64_t)n > remaining) {
        n = (size_t)remaining;
    }
    
    // Seek parent to correct position
    if (arc_stream_seek(data->parent, data->offset + data->pos, SEEK_SET) < 0) {
        return -1;
    }
    
    ssize_t ret = arc_stream_read(data->parent, buf, n);
    if (ret > 0) {
        data->pos += ret;
        stream->bytes_read += ret;
    }
    
    return ret;
}

static int substream_seek(ArcStream *stream, int64_t off, int whence) {
    struct SubstreamData *data = (struct SubstreamData *)stream->user_data;
    int64_t new_pos;
    
    switch (whence) {
        case SEEK_SET:
            new_pos = off;
            break;
        case SEEK_CUR:
            new_pos = data->pos + off;
            break;
        case SEEK_END:
            new_pos = data->length + off;
            break;
        default:
            errno = EINVAL;
            return -1;
    }
    
    if (new_pos < 0 || new_pos > data->length) {
        errno = EINVAL;
        return -1;
    }
    
    data->pos = new_pos;
    return 0;
}

static int64_t substream_tell(ArcStream *stream) {
    struct SubstreamData *data = (struct SubstreamData *)stream->user_data;
    return data->pos;
}

static void substream_close(ArcStream *stream) {
    struct SubstreamData *data = (struct SubstreamData *)stream->user_data;
    // Note: We don't close parent - caller owns it
    free(data);
    free(stream);
}

// Public API
ssize_t arc_stream_read(ArcStream *stream, void *buf, size_t n) {
    if (!stream || !stream->vtable || !stream->vtable->read) {
        errno = EINVAL;
        return -1;
    }
    return stream->vtable->read(stream, buf, n);
}

int arc_stream_seek(ArcStream *stream, int64_t off, int whence) {
    if (!stream || !stream->vtable || !stream->vtable->seek) {
        errno = EINVAL;
        return -1;
    }
    return stream->vtable->seek(stream, off, whence);
}

int64_t arc_stream_tell(ArcStream *stream) {
    if (!stream || !stream->vtable || !stream->vtable->tell) {
        errno = EINVAL;
        return -1;
    }
    return stream->vtable->tell(stream);
}

void arc_stream_close(ArcStream *stream) {
    if (stream && stream->vtable && stream->vtable->close) {
        stream->vtable->close(stream);
    }
}

ArcStream *arc_stream_from_fd(int fd, int64_t byte_limit) {
    if (fd < 0) {
        return NULL;
    }
    
    ArcStream *stream = calloc(1, sizeof(ArcStream));
    if (!stream) {
        return NULL;
    }
    
    struct FdStreamData *data = calloc(1, sizeof(struct FdStreamData));
    if (!data) {
        free(stream);
        return NULL;
    }
    
    data->fd = fd;
    data->pos = 0;
    
    stream->vtable = &fd_vtable;
    stream->byte_limit = byte_limit;
    stream->bytes_read = 0;
    stream->user_data = data;
    
    return stream;
}

ArcStream *arc_stream_from_memory(const void *data, size_t size, int64_t byte_limit) {
    if (!data) {
        return NULL;
    }
    
    ArcStream *stream = calloc(1, sizeof(ArcStream));
    if (!stream) {
        return NULL;
    }
    
    struct MemStreamData *mem_data = calloc(1, sizeof(struct MemStreamData));
    if (!mem_data) {
        free(stream);
        return NULL;
    }
    
    mem_data->data = (const uint8_t *)data;
    mem_data->size = size;
    mem_data->pos = 0;
    
    stream->vtable = &mem_vtable;
    stream->byte_limit = (byte_limit > 0) ? byte_limit : (int64_t)size;
    stream->bytes_read = 0;
    stream->user_data = mem_data;
    
    return stream;
}

ArcStream *arc_stream_substream(ArcStream *parent, int64_t offset, int64_t length) {
    if (!parent || offset < 0 || length < 0) {
        return NULL;
    }
    
    ArcStream *stream = calloc(1, sizeof(ArcStream));
    if (!stream) {
        return NULL;
    }
    
    struct SubstreamData *data = calloc(1, sizeof(struct SubstreamData));
    if (!data) {
        free(stream);
        return NULL;
    }
    
    data->parent = parent;
    data->offset = offset;
    data->length = length;
    data->pos = 0;
    
    stream->vtable = &substream_vtable;
    stream->byte_limit = length; // Substream limit is its length
    stream->bytes_read = 0;
    stream->user_data = data;
    
    return stream;
}

