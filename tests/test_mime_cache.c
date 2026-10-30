#define _POSIX_C_SOURCE 200809L
#include "mime.h"
#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static _Atomic int calls;
static bool fail_next;
const char *__real_magic_file(magic_t, const char *);

const char *__wrap_magic_file(magic_t cookie, const char *path) {
    atomic_fetch_add(&calls, 1);
    if (fail_next) {
        fail_next = false;
        return NULL;
    }
    return __real_magic_file(cookie, path);
}

static void write_file(const char *path, const void *data, size_t size) {
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(data, 1, size, file) == size);
    assert(fclose(file) == 0);
}

typedef struct {
    magic_t cookie;
    const char *path;
} Worker;

static void *worker(void *argument) {
    Worker *input = argument;
    for (int i = 0; i < 100; i++) {
        char type[512];
        assert(mime_file_cached(input->cookie, input->path, MAGIC_MIME_TYPE, type, sizeof(type)));
        assert(!strcmp(type, "image/gif"));
    }
    return NULL;
}

int main(void) {
    char directory[] = "/tmp/cupidfm-mime-XXXXXX";
    assert(mkdtemp(directory));
    char path[512], link[512], type[512], other[512];
    snprintf(path, sizeof(path), "%s/file", directory);
    snprintf(link, sizeof(link), "%s/link", directory);
    static const unsigned char gif[13] = {'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0, 0, 0};
    write_file(path, "plain text\nab", 13);
    magic_t plain = magic_open(MAGIC_MIME_TYPE),
            follow = magic_open(MAGIC_MIME_TYPE | MAGIC_SYMLINK);
    assert(plain && follow && magic_load(plain, NULL) == 0 && magic_load(follow, NULL) == 0);
    assert(mime_file_cached(plain, path, MAGIC_MIME_TYPE, type, sizeof(type)));
    assert(!strcmp(type, "text/plain"));
    int first = atomic_load(&calls);
    memset(type, 0, sizeof(type));
    assert(mime_file_cached(plain, path, MAGIC_MIME_TYPE, type, sizeof(type)) &&
           !strcmp(type, "text/plain"));
    assert(atomic_load(&calls) == first + 1); /* Recent files bypass timestamp-only hits. */
    struct timespec settle = {2, 100000000};
    assert(nanosleep(&settle, NULL) == 0);
    assert(mime_file_cached(plain, path, MAGIC_MIME_TYPE, type, sizeof(type)));
    first = atomic_load(&calls);
    assert(mime_file_cached(plain, path, MAGIC_MIME_TYPE, type, sizeof(type)));
    assert(atomic_load(&calls) == first);
    assert(mime_file_cached(follow, path, MAGIC_MIME_TYPE | MAGIC_SYMLINK, type, sizeof(type)));
    assert(atomic_load(&calls) == first + 1);
    struct stat before;
    assert(stat(path, &before) == 0);
    assert(symlink(path, link) == 0);
    assert(mime_file_cached(follow, link, MAGIC_MIME_TYPE | MAGIC_SYMLINK, type, sizeof(type)));
    write_file(path, gif, sizeof(gif));
    struct timespec times[2] = {before.st_atim, before.st_mtim};
    assert(utimensat(AT_FDCWD, path, times, 0) == 0);
    const char *changed = mime_file_cached(plain, path, MAGIC_MIME_TYPE, type, sizeof(type));
    if (!changed || strcmp(type, "image/gif")) {
        struct stat after;
        assert(stat(path, &after) == 0);
        fprintf(stderr, "MIME after edit: [%s], calls=%d, ctime before=%lld.%ld after=%lld.%ld\n",
                changed ? changed : "NULL", atomic_load(&calls), (long long)before.st_ctim.tv_sec,
                before.st_ctim.tv_nsec, (long long)after.st_ctim.tv_sec, after.st_ctim.tv_nsec);
    }
    assert(changed && !strcmp(type, "image/gif"));
    assert(mime_file_cached(follow, link, MAGIC_MIME_TYPE | MAGIC_SYMLINK, type, sizeof(type)) &&
           !strcmp(type, "image/gif"));
    assert(nanosleep(&settle, NULL) == 0);
    mime_cache_clear();
    first = atomic_load(&calls);
    Worker input = {plain, path};
    pthread_t threads[4];
    for (int i = 0; i < 4; i++)
        assert(pthread_create(&threads[i], NULL, worker, &input) == 0);
    for (int i = 0; i < 4; i++)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(atomic_load(&calls) == first + 1);
    char tiny[2];
    assert(!mime_file_cached(plain, path, MAGIC_MIME_TYPE, tiny, sizeof(tiny)));
    mime_cache_clear();
    fail_next = true;
    assert(!mime_file_cached(plain, path, MAGIC_MIME_TYPE, type, sizeof(type)));
    assert(mime_file_cached(plain, path, MAGIC_MIME_TYPE, type, sizeof(type)));
    /* More distinct files than the cache holds must evict an older result. */
    for (int i = 0; i < 140; i++) {
        snprintf(other, sizeof(other), "%s/entry-%d", directory, i);
        write_file(other, gif, sizeof(gif));
        assert(mime_file_cached(plain, other, MAGIC_MIME_TYPE, type, sizeof(type)));
        unlink(other);
    }
    first = atomic_load(&calls);
    assert(mime_file_cached(plain, path, MAGIC_MIME_TYPE, type, sizeof(type)));
    assert(atomic_load(&calls) == first + 1);
    unlink(path);
    assert(!mime_file_cached(plain, path, MAGIC_MIME_TYPE, type, sizeof(type)));
    unlink(link);
    rmdir(directory);
    mime_cache_clear();
    magic_close(follow);
    magic_close(plain);
    puts("MIME cache hits, metadata/flags/symlinks, concurrency, failures, and eviction passed");
    return 0;
}
