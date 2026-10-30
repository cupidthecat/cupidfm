#ifndef ARC_BASE_H
#define ARC_BASE_H

#include "arc_reader.h"
#include "arc_stream.h"

typedef struct ArcLimits ArcLimits;

/**
 * Base structure for all archive readers.
 * This must be the first member of every reader struct to ensure
 * safe type casting and format dispatch.
 */
typedef struct ArcReaderBase {
    int format;              // Archive format identifier (ARC_FORMAT_*)
    ArcStream *stream;       // The stream the format reads from
    ArcStream *owned_stream; // For closing (optional)
    ArcLimits limits_storage;
    uint64_t entries_seen;
    uint64_t declared_bytes;
    int terminal_error;
    bool drain_on_eof;
    const ArcLimits *limits; // Safety/resource limits (may be NULL => defaults)
} ArcReaderBase;

static inline void arc_base_set_limits(ArcReaderBase *base, const ArcLimits *limits) {
    base->limits_storage = limits ? *limits : *arc_default_limits();
    base->limits = &base->limits_storage;
}

/**
 * Safe accessor to get the format from any reader.
 * This function is safe because all reader structs embed ArcReaderBase
 * as their first member.
 *
 * @param r Pointer to any reader struct
 * @return Format identifier, or -1 if r is NULL
 */
static inline int arc_reader_format(const void *r) {
    if (!r)
        return -1;
    return ((const ArcReaderBase *)r)->format;
}

#endif // ARC_BASE_H
