#ifndef ARC_ZIP_H
#define ARC_ZIP_H

#include "arc_reader.h"
#include "arc_stream.h"

/**
 * ZIP format implementation.
 * 
 * Supports:
 * - Central Directory parsing (fast listing)
 * - Streaming local header parsing (for archives without central directory)
 * - ZIP64 support (files >4GB, archives >4GB, >65535 entries)
 * - Store (0) and Deflate (8) compression
 * - Directory detection (name ending with /)
 * - Encryption flag detection
 * 
 * ZIP64 Features:
 * - Automatically detects ZIP64 archives via EOCD64 locator
 * - Parses ZIP64 Extended Information Extra Field (0x0001)
 * - Uses 64-bit sizes and offsets when standard fields are 0xFFFFFFFF
 * 
 * Streaming Mode:
 * - Falls back to local header parsing when central directory is missing
 * - Builds entry list dynamically as archive is read
 * - Useful for reading archives being created/streamed
 */

/**
 * Internal function to create a ZIP reader.
 * Called by arc_open_stream() after format detection.
 */
ArcReader *arc_zip_open(ArcStream *stream);

/**
 * Internal ZIP functions (exposed for arc_reader.c).
 */
int arc_zip_next(ArcReader *reader, ArcEntry *entry);
ArcStream *arc_zip_open_data(ArcReader *reader);
int arc_zip_skip_data(ArcReader *reader);
void arc_zip_close(ArcReader *reader);

#endif // ARC_ZIP_H

