#ifndef ARC_BASE_H
#define ARC_BASE_H

#include "arc_stream.h"

typedef struct ArcLimits ArcLimits;

/**
 * Base structure for all archive readers.
 * This must be the first member of every reader struct to ensure
 * safe type casting and format dispatch.
 */
typedef struct ArcReaderBase {
    int format;              // Archive format identifier (ARC_FORMAT_*)
    ArcStream *stream;        // The stream the format reads from
    ArcStream *owned_stream;  // For closing (optional)
    const ArcLimits *limits;  // Safety/resource limits (may be NULL => defaults)
} ArcReaderBase;

/**
 * Safe accessor to get the format from any reader.
 * This function is safe because all reader structs embed ArcReaderBase
 * as their first member.
 * 
 * @param r Pointer to any reader struct
 * @return Format identifier, or -1 if r is NULL
 */
static inline int arc_reader_format(const void *r) {
    if (!r) return -1;
    return ((const ArcReaderBase*)r)->format;
}

#endif // ARC_BASE_H

