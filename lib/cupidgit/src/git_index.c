#define _POSIX_C_SOURCE 200809L
#include "git_index.h"
#include "sha1.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t read_uint32(const unsigned char *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

void git_index_free(IndexMap *map) {
    if (!map)
        return;
    for (size_t i = 0; i < map->count; i++)
        free(map->entries[i].path);
    free(map->entries);
    free(map);
}

GitIndexEntry *git_index_lookup(IndexMap *map, const char *path) {
    if (!map || !path)
        return NULL;
    size_t lo = 0, hi = map->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(map->entries[mid].path, path);
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < map->count && !strcmp(map->entries[lo].path, path) ? &map->entries[lo] : NULL;
}

bool git_index_parse(GitRepository *repo) {
    if (!repo || !repo->git_dir)
        return false;
    size_t n = strlen(repo->git_dir) + 7;
    char *path = malloc(n);
    if (!path)
        return false;
    snprintf(path, n, "%s/index", repo->git_dir);
    FILE *fp = fopen(path, "rb");
    int open_error = errno;
    free(path);
    IndexMap *map = calloc(1, sizeof(*map));
    if (!map) {
        if (fp)
            fclose(fp);
        return false;
    }
    if (!fp) {
        if (open_error != ENOENT) {
            free(map);
            return false;
        }
        git_index_free(repo->index_entries);
        repo->index_entries = map;
        repo->index_present = false;
        return true;
    }
    struct stat st;
    unsigned char *data = NULL;
    bool ok = false;
    if (fstat(fileno(fp), &st) || st.st_size < 32 || (uintmax_t)st.st_size > SIZE_MAX)
        goto done;
    size_t size = (size_t)st.st_size;
    data = malloc(size);
    if (!data || fread(data, 1, size, fp) != size)
        goto done;
    if (memcmp(data, "DIRC", 4))
        goto done;
    unsigned version = read_uint32(data + 4), count = read_uint32(data + 8);
    if (version < 2 || version > 4 || count > 1000000 || count > (size - 32) / 64)
        goto done;
    SHA1_CTX ctx;
    unsigned char digest[20];
    SHA1_Init(&ctx);
    SHA1_Update(&ctx, data, size - 20);
    SHA1_Final(digest, &ctx);
    if (memcmp(digest, data + size - 20, 20))
        goto done;
    map->entries = calloc(count ? count : 1, sizeof(*map->entries));
    if (!map->entries)
        goto done;
    map->capacity = count;
    size_t pos = 12, end = size - 20;
    for (unsigned i = 0; i < count; i++) {
        size_t start = pos;
        if (end - pos < 62)
            goto done;
        GitIndexEntry *entry = &map->entries[i];
        entry->mtime_sec = read_uint32(data + pos + 8);
        entry->mtime_nsec = read_uint32(data + pos + 12);
        entry->mode = read_uint32(data + pos + 24);
        memcpy(entry->sha1, data + pos + 40, 20);
        unsigned flags = (unsigned)data[pos + 60] << 8 | data[pos + 61];
        entry->flags = (uint16_t)flags;
        pos += 62;
        if (flags & 0x4000) {
            if (version == 2 || end - pos < 2)
                goto done;
            entry->extended_flags = (uint16_t)((unsigned)data[pos] << 8 | data[pos + 1]);
            if (entry->extended_flags & ~0x6000)
                goto done;
            pos += 2;
        }
        size_t prefix = 0;
        if (version == 4) {
            size_t remove = 0;
            unsigned byte;
            do {
                if (pos == end || remove > (SIZE_MAX >> 7) - 1)
                    goto done;
                byte = data[pos++];
                remove = (remove << 7) + (byte & 127);
                if (byte & 128)
                    remove++;
            } while (byte & 128);
            size_t previous = i ? strlen(map->entries[i - 1].path) : 0;
            if (remove > previous)
                goto done;
            prefix = previous - remove;
        }
        const unsigned char *nul = memchr(data + pos, 0, end - pos);
        if (!nul)
            goto done;
        size_t suffix = (size_t)(nul - (data + pos));
        if (prefix > SIZE_MAX - suffix - 1)
            goto done;
        size_t len = prefix + suffix;
        if (!len || ((flags & 4095) != 4095 && (flags & 4095) != len))
            goto done;
        entry->path = malloc(len + 1);
        if (!entry->path)
            goto done;
        map->count++;
        if (prefix)
            memcpy(entry->path, map->entries[i - 1].path, prefix);
        memcpy(entry->path + prefix, data + pos, suffix + 1);
        if (entry->path[0] == '/' || (i && strcmp(map->entries[i - 1].path, entry->path) > 0))
            goto done;
        pos += (suffix + 1);
        if (version != 4) {
            size_t pad = (8 - (pos - start) % 8) % 8;
            if (end - pos < pad)
                goto done;
            for (size_t j = 0; j < pad; j++)
                if (data[pos + j])
                    goto done;
            pos += pad;
        }
    }
    while (pos < end) {
        if (end - pos < 8 || data[pos] < 'A' || data[pos] > 'Z')
            goto done;
        size_t ext = read_uint32(data + pos + 4);
        pos += 8;
        if (ext > end - pos)
            goto done;
        pos += ext;
    }
    git_index_free(repo->index_entries);
    repo->index_entries = map;
    repo->index_stat = st;
    repo->index_present = true;
    map = NULL;
    ok = true;
done:
    free(data);
    fclose(fp);
    git_index_free(map);
    return ok;
}

bool git_index_refresh(GitRepository *repo) {
    size_t n = strlen(repo->git_dir) + 7;
    char *path = malloc(n);
    if (!path)
        return false;
    snprintf(path, n, "%s/index", repo->git_dir);
    struct stat st;
    int rc = stat(path, &st), saved = errno;
    free(path);
    if (rc < 0)
        return saved == ENOENT ? (!repo->index_present || git_index_parse(repo)) : false;
    const struct stat *old = &repo->index_stat;
    if (repo->index_present && st.st_ino == old->st_ino && st.st_size == old->st_size &&
        st.st_mtim.tv_sec == old->st_mtim.tv_sec && st.st_mtim.tv_nsec == old->st_mtim.tv_nsec &&
        st.st_ctim.tv_sec == old->st_ctim.tv_sec && st.st_ctim.tv_nsec == old->st_ctim.tv_nsec)
        return true;
    return git_index_parse(repo);
}
