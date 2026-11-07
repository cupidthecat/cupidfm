#ifndef FILE_WATCH_H
#define FILE_WATCH_H

#include <stdbool.h>
#include <time.h>

typedef struct {
    int fd;
    int watch;
    char *directory;
    bool pending;
    struct timespec last_change;
} FileWatch;

bool file_watch_poll(FileWatch *watch, const char *directory);
void file_watch_close(FileWatch *watch);

#endif
