#define _POSIX_C_SOURCE 200809L
#include "arc_stream.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <stdint.h>
#include <limits.h>

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
    
    ssize_t ret;
    do { ret = read(data->fd, buf, n); } while (ret < 0 && errno == EINTR);
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
    stream->bytes_read = 0;
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
    int64_t base;
    if (data->size > INT64_MAX) { errno = EOVERFLOW; return -1; }
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = (int64_t)data->pos; break;
        case SEEK_END: base = (int64_t)data->size; break;
        default: errno = EINVAL; return -1;
    }
    if ((off < 0 && off < -base) || (off >= 0 && (uint64_t)off > data->size - (size_t)base)) {
        errno = EINVAL; return -1;
    }
    size_t new_pos = (size_t)(base + off);
    data->pos = new_pos;
    stream->bytes_read = 0;
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
    if (arc_stream_tell(data->parent) != data->offset + data->pos &&
        arc_stream_seek(data->parent, data->offset + data->pos, SEEK_SET) < 0) {
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
    if ((whence == SEEK_CUR && off > 0 && data->pos > INT64_MAX - off) ||
        (whence == SEEK_END && off > 0 && data->length > INT64_MAX - off)) { errno = EOVERFLOW; return -1; }
    
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
    stream->bytes_read = 0;
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
    if (!stream || (!buf && n) || !stream->vtable || !stream->vtable->read) {
        errno = EINVAL;
        return -1;
    }
    if (!n) return 0;
    if (n > (size_t)SSIZE_MAX) n = (size_t)SSIZE_MAX;
    return stream->vtable->read(stream, buf, n);
}

ssize_t arc_stream_read_fully(ArcStream *stream, void *buf, size_t n) {
    if (n > (size_t)SSIZE_MAX || (!buf && n)) { errno = EINVAL; return -1; }
    size_t total = 0;
    while (total < n) {
        ssize_t count = arc_stream_read(stream, (uint8_t *)buf + total, n - total);
        if (count < 0) { if (errno == EINTR) continue; return -1; }
        if (!count) break;
        total += (size_t)count;
    }
    return (ssize_t)total;
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
    off_t initial = lseek(fd, 0, SEEK_CUR);
    data->pos = initial >= 0 ? (int64_t)initial : 0;
    
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
    if (!parent || offset < 0 || length < 0 || length > INT64_MAX - offset) {
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


static ssize_t view_read(ArcStream *s, void *buf, size_t n) { return arc_stream_read(s->user_data, buf, n); }
static int view_seek(ArcStream *s, int64_t off, int whence) { return arc_stream_seek(s->user_data, off, whence); }
static int64_t view_tell(ArcStream *s) { return arc_stream_tell(s->user_data); }
static void view_close(ArcStream *s) { free(s); }
ArcStream *arc_stream_view(ArcStream *parent) {
    static const struct ArcStreamVtable table = {view_read, view_seek, view_tell, view_close};
    if (!parent) { errno = EINVAL; return NULL; }
    ArcStream *view = calloc(1, sizeof(*view));
    if (view) { view->vtable = &table; view->user_data = parent; }
    return view;
}

struct OwnedStream { ArcStream *stream; ArcStream *parent; };
static ssize_t owned_read(ArcStream *s, void *buf, size_t n) {
    struct OwnedStream *owned = s->user_data;
    return arc_stream_read(owned->stream, buf, n);
}
static int owned_seek(ArcStream *s, int64_t off, int whence) {
    struct OwnedStream *owned = s->user_data;
    return arc_stream_seek(owned->stream, off, whence);
}
static int64_t owned_tell(ArcStream *s) {
    struct OwnedStream *owned = s->user_data;
    return arc_stream_tell(owned->stream);
}
static void owned_close(ArcStream *s) {
    struct OwnedStream *owned = s->user_data;
    arc_stream_close(owned->stream); arc_stream_close(owned->parent);
    free(owned); free(s);
}
ArcStream *arc_stream_with_parent(ArcStream *stream, ArcStream *parent) {
    static const struct ArcStreamVtable table = {owned_read, owned_seek, owned_tell, owned_close};
    struct OwnedStream *owned = malloc(sizeof(*owned));
    ArcStream *wrapper = calloc(1, sizeof(*wrapper));
    if (!owned || !wrapper) { free(owned); free(wrapper); return NULL; }
    owned->stream = stream; owned->parent = parent;
    wrapper->vtable = &table; wrapper->user_data = owned;
    return wrapper;
}
