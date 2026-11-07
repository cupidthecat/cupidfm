#define _GNU_SOURCE
#include "watch.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/inotify.h>
#endif

bool file_watch_poll(FileWatch *watch, const char *directory) {
#ifdef __linux__
    if (watch->fd < 0)
        watch->fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watch->fd < 0 || !directory)
        return false;
    if (watch->watch < 0 || !watch->directory || strcmp(watch->directory, directory)) {
        char *replacement = strdup(directory);
        if (!replacement)
            return false;
        if (watch->watch >= 0)
            inotify_rm_watch(watch->fd, watch->watch);
        watch->watch =
            inotify_add_watch(watch->fd, directory,
                              IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ATTRIB |
                                  IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF);
        free(watch->directory);
        watch->directory = replacement;
        watch->pending = false;
    }
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now))
        return false;

    union {
        char bytes[8192];
        struct inotify_event alignment;
    } buffer;

    ssize_t length;
    while ((length = read(watch->fd, buffer.bytes, sizeof(buffer.bytes))) > 0) {
        for (size_t offset = 0; offset + sizeof(struct inotify_event) <= (size_t)length;) {
            const struct inotify_event *event = (const void *)(buffer.bytes + offset);
            if (event->wd == watch->watch || (event->mask & IN_Q_OVERFLOW)) {
                watch->pending = true;
                watch->last_change = now;
                if (event->mask & IN_IGNORED)
                    watch->watch = -1;
            }
            offset += sizeof(*event) + event->len;
        }
    }
    long long elapsed = (long long)(now.tv_sec - watch->last_change.tv_sec) * 1000000000LL +
                        now.tv_nsec - watch->last_change.tv_nsec;
    if (watch->pending && elapsed >= 100000000LL) {
        watch->pending = false;
        return true;
    }
#else
    (void)watch;
    (void)directory;
#endif
    return false;
}

void file_watch_close(FileWatch *watch) {
    if (watch->fd >= 0)
        close(watch->fd);
    free(watch->directory);
    *watch = (FileWatch){.fd = -1, .watch = -1};
}
