#ifndef FILE_SEARCH_H
#define FILE_SEARCH_H

#include <glib.h>
#include <stdbool.h>

typedef struct FileSearch FileSearch;

FileSearch *file_search_start(const char *directory, const char *query, bool content, bool hidden,
                              GError **error);
/* Nonblocking, bounded output delivery. The returned array owns its strings. */
GPtrArray *file_search_take(FileSearch *search, size_t limit);
bool file_search_done(FileSearch *search);
const char *file_search_error(FileSearch *search);
const char *file_search_query(FileSearch *search);
bool file_search_content(FileSearch *search);
/* Stops and reaps the owned child, including an unfinished search. */
void file_search_free(FileSearch *search);

#endif
