#ifndef ARC_TAR_H
#define ARC_TAR_H

#include "arc_reader.h"
#include "arc_stream.h"

/**
 * TAR format implementation.
 * 
 * Supports:
 * - ustar format (basic)
 * - pax extended headers (long paths, large sizes)
 * - Regular files, directories, symlinks, hardlinks
 */

/**
 * Internal function to create a TAR reader.
 * Called by arc_open_stream() after format detection.
 */
ArcReader *arc_tar_open(ArcStream *stream);

/**
 * Internal TAR functions (exposed for arc_reader.c).
 */
int arc_tar_next(ArcReader *reader, ArcEntry *entry);
ArcStream *arc_tar_open_data(ArcReader *reader);
int arc_tar_skip_data(ArcReader *reader);
void arc_tar_close(ArcReader *reader);

#endif // ARC_TAR_H

