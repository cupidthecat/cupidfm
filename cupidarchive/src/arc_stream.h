#ifndef ARC_STREAM_H
#define ARC_STREAM_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/**
 * Stream abstraction for reading archive data.
 * 
 * Provides a unified interface that can be backed by:
 * - File descriptors (pread, read)
 * - Memory buffers
 * - Substreams (bounded reads for entries)
 * - Decompression filters
 * 
 * Key safety feature: Hard byte limits per stream to prevent zip bombs.
 */
typedef struct ArcStream ArcStream;

/**
 * Virtual function table for stream operations.
 */
struct ArcStreamVtable {
    /**
     * Read up to n bytes into buf.
     * Returns number of bytes read, 0 on EOF, -1 on error.
     */
    ssize_t (*read)(ArcStream *stream, void *buf, size_t n);
    
    /**
     * Seek to offset (optional, may be NULL).
     * whence: SEEK_SET, SEEK_CUR, SEEK_END
     * Returns 0 on success, -1 on error.
     */
    int (*seek)(ArcStream *stream, int64_t off, int whence);
    
    /**
     * Get current position (optional, may be NULL).
     * Returns current offset, -1 on error.
     */
    int64_t (*tell)(ArcStream *stream);
    
    /**
     * Close and free the stream.
     */
    void (*close)(ArcStream *stream);
};

/**
 * Stream structure.
 */
struct ArcStream {
    const struct ArcStreamVtable *vtable;
    int64_t byte_limit;      // Hard limit on total bytes that can be read
    int64_t bytes_read;      // Total bytes read so far
    void *user_data;         // Implementation-specific data
};

/**
 * Read from a stream.
 * Automatically enforces byte_limit to prevent zip bombs.
 * 
 * @param stream The stream to read from
 * @param buf Buffer to read into
 * @param n Maximum bytes to read
 * @return Number of bytes read, 0 on EOF, -1 on error
 */
ssize_t arc_stream_read(ArcStream *stream, void *buf, size_t n);

/**
 * Seek in a stream (if supported).
 * 
 * @param stream The stream to seek in
 * @param off Offset to seek to
 * @param whence SEEK_SET, SEEK_CUR, or SEEK_END
 * @return 0 on success, -1 on error or not supported
 */
int arc_stream_seek(ArcStream *stream, int64_t off, int whence);

/**
 * Get current position in stream (if supported).
 * 
 * @param stream The stream
 * @return Current offset, -1 on error or not supported
 */
int64_t arc_stream_tell(ArcStream *stream);

/**
 * Close and free a stream.
 * 
 * @param stream The stream to close
 */
void arc_stream_close(ArcStream *stream);

/**
 * Create a file-backed stream.
 * 
 * @param fd File descriptor (must be open)
 * @param byte_limit Maximum bytes that can be read (0 = unlimited, not recommended)
 * @return New stream, or NULL on error
 */
ArcStream *arc_stream_from_fd(int fd, int64_t byte_limit);

/**
 * Create a memory-backed stream.
 * 
 * @param data Memory buffer (must remain valid for stream lifetime)
 * @param size Size of buffer
 * @param byte_limit Maximum bytes that can be read (0 = size)
 * @return New stream, or NULL on error
 */
ArcStream *arc_stream_from_memory(const void *data, size_t size, int64_t byte_limit);

/**
 * Create a substream (bounded view of another stream).
 * 
 * @param parent Parent stream (must remain valid for substream lifetime)
 * @param offset Offset in parent stream to start from
 * @param length Maximum length to read
 * @return New substream, or NULL on error
 */
ArcStream *arc_stream_substream(ArcStream *parent, int64_t offset, int64_t length);

#endif // ARC_STREAM_H

