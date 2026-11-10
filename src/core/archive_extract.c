#define _POSIX_C_SOURCE 200809L
#include "archive_extract.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "cupidarchive.h"

int archive_extract_worker(const char *archive, const char *directory) {
    ArcReader *reader = arc_open_path(archive);
    if (!reader) {
        fprintf(stderr, "Cannot read archive %s: %s\n", archive, strerror(errno ? errno : EINVAL));
        return 1;
    }
    if (mkdir(directory, 0700)) {
        fprintf(stderr, "Cannot create extraction directory: %s\n", strerror(errno));
        arc_close(reader);
        return 1;
    }
    int result = arc_extract_to_path(reader, directory, true, true);
    int saved = errno;
    arc_close(reader);
    if (result) {
        fprintf(stderr, "Archive extraction failed: %s\n", strerror(saved ? saved : EIO));
        return 1;
    }
    return 0;
}
