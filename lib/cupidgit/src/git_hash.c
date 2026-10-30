#define _XOPEN_SOURCE 700
#define _DEFAULT_SOURCE
#include "git_hash.h"
#include "sha1.h"
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

bool git_hash_file(const char *filepath, unsigned char sha1_out[GIT_SHA1_SIZE]) {
    if (!filepath || !sha1_out)
        return false;
    int fd = open(filepath, O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
    if (fd < 0)
        return false;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
        close(fd);
        return false;
    }
    FILE *f = fdopen(fd, "rb");
    if (!f) {
        close(fd);
        return false;
    }
    SHA1_CTX ctx;
    SHA1_Init(&ctx);
    char header[64];
    int header_len = snprintf(header, sizeof(header), "blob %" PRIuMAX, (uintmax_t)st.st_size);
    SHA1_Update(&ctx, header, (size_t)header_len + 1);
    unsigned char buffer[65536];
    uintmax_t remaining = (uintmax_t)st.st_size;
    while (remaining) {
        size_t want = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        size_t n = fread(buffer, 1, want, f);
        if (!n) {
            fclose(f);
            return false;
        }
        SHA1_Update(&ctx, buffer, n);
        remaining -= n;
    }
    struct stat after;
    bool ok =
        !ferror(f) && !fstat(fd, &after) && st.st_size == after.st_size &&
        st.st_mtim.tv_sec == after.st_mtim.tv_sec && st.st_mtim.tv_nsec == after.st_mtim.tv_nsec &&
        st.st_ctim.tv_sec == after.st_ctim.tv_sec && st.st_ctim.tv_nsec == after.st_ctim.tv_nsec;
    if (fclose(f))
        ok = false;
    if (!ok)
        return false;
    SHA1_Final(sha1_out, &ctx);
    return true;
}

bool git_hash_symlink(const char *filepath, unsigned char sha1_out[GIT_SHA1_SIZE]) {
    if (!filepath || !sha1_out)
        return false;

    char target[PATH_MAX];
    ssize_t len = readlink(filepath, target, sizeof(target) - 1);
    if (len < 0 || len == (ssize_t)sizeof(target) - 1)
        return false;

    target[len] = '\0';

    // Compute git object hash for symlink target
    SHA1_CTX ctx;
    SHA1_Init(&ctx);

    char header[64];
    int header_len = snprintf(header, sizeof(header), "blob %ld", (long)len);

    SHA1_Update(&ctx, header, header_len);
    SHA1_Update(&ctx, "\0", 1);
    SHA1_Update(&ctx, target, len);

    SHA1_Final(sha1_out, &ctx);

    return true;
}
