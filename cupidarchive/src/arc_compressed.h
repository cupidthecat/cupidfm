#ifndef ARC_COMPRESSED_H
#define ARC_COMPRESSED_H

#include "arc_reader.h"
#include "arc_stream.h"

/**
 * Compressed single-file format implementation.
 * 
 * Supports:
 * - Gzip (.gz) - single compressed files
 * - Bzip2 (.bz2) - single compressed files
 * 
 * These are not archives, but compressed single files.
 * The reader presents them as a single "virtual" entry with the
 * filename without the compression extension.
 */

/**
 * Internal function to create a compressed file reader.
 * Called by arc_open_stream() after format detection.
 * 
 * @param decompressed_stream The already-decompressed stream (filter)
 * @param original_path Original file path (for filename extraction)
 * @param compression_type ARC_COMPRESSED_GZIP or ARC_COMPRESSED_BZIP2
 */
ArcReader *arc_compressed_open(ArcStream *decompressed_stream, const char *original_path, int compression_type);

/**
 * Internal compressed file functions (exposed for arc_reader.c).
 */
int arc_compressed_next(ArcReader *reader, ArcEntry *entry);
ArcStream *arc_compressed_open_data(ArcReader *reader);
int arc_compressed_skip_data(ArcReader *reader);
void arc_compressed_close(ArcReader *reader);

/**
 * Set the original stream for cleanup (called by arc_reader.c).
 */
void arc_compressed_set_original_stream(ArcReader *reader, ArcStream *original_stream);

// Compression types
#define ARC_COMPRESSED_GZIP  0
#define ARC_COMPRESSED_BZIP2 1
#define ARC_COMPRESSED_XZ    2

#endif // ARC_COMPRESSED_H

