#ifndef ARC_7Z_H
#define ARC_7Z_H

#include "arc_reader.h"
#include "arc_stream.h"

/**
 * 7z format implementation (limited support).
 *
 * Supports:
 * - 7z container with a single file entry
 * - LZMA or LZMA2 compressed data streams
 * - Uncompressed (copy) streams
 *
 * Limitations:
 * - No encryption, multi-volume, or multi-file solid archives
 * - Only single-folder, single-coder archives are supported
 */

ArcReader *arc_7z_open(ArcStream *stream);
ArcReader *arc_7z_open_ex(ArcStream *stream, const ArcLimits *limits);
int arc_7z_next(ArcReader *reader, ArcEntry *entry);
ArcStream *arc_7z_open_data(ArcReader *reader);
int arc_7z_skip_data(ArcReader *reader);
void arc_7z_close(ArcReader *reader);

#endif // ARC_7Z_H

