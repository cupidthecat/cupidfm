#ifndef ARCHIVE_EXTRACT_H
#define ARCHIVE_EXTRACT_H

/* Worker entry point: extract into a new private directory, without starting curses. */
int archive_extract_worker(const char *archive, const char *directory);

#endif
