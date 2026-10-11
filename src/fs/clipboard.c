// clipboard.c
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "clipboard.h"
#include "process.h"
#include <fcntl.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool clipboard_set_from_file(const char *path) {
    if (!path || !*path) return false;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    char *args[] = {"xclip", "-selection", "clipboard", "-i", NULL};
    bool ok = run_process(args, fd, -1);
    close(fd);
    return ok;
}


#define CLIP_MAX_PATH 4096
#define CLIP_MAX_ITEMS 65536
#define CLIP_MAX_BYTES (16 * 1024 * 1024)

void clipboard_paths_free(ClipboardPaths *paths) {
    if (!paths) return;
    for (size_t i = 0; i < paths->count; ++i) free(paths->paths[i]);
    free(paths->paths);
    *paths = (ClipboardPaths){0};
}

static bool valid_path(const char *path) {
    if (!path || !*path || strlen(path) >= CLIP_MAX_PATH) return false;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return *base && strcmp(base, ".") != 0 && strcmp(base, "..") != 0;
}

bool clipboard_set_paths(const char *const *paths, size_t count, bool cut) {
    if (!paths || !count || count > CLIP_MAX_ITEMS) return false;
    FILE *stream = tmpfile();
    if (!stream) return false;
    bool ok = fprintf(stream, "CUPIDFM_CLIP_V3\nOP=%s\nN=%zu\n", cut ? "CUT" : "COPY", count) > 0;
    size_t total = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        struct stat st;
        if (!valid_path(paths[i]) || lstat(paths[i], &st) != 0) { ok = false; break; }
        size_t len = strlen(paths[i]);
        total += len;
        if (total > CLIP_MAX_BYTES) { ok = false; break; }
        ok = fprintf(stream, "P=%zu\n", len) > 0 &&
             fwrite(paths[i], 1, len, stream) == len && fputc('\n', stream) != EOF;
    }
    if (ok) ok = fflush(stream) == 0 && fseek(stream, 0, SEEK_SET) == 0;
    if (ok) {
        char *args[] = {"xclip", "-selection", "clipboard", "-i", NULL};
        ok = run_process(args, fileno(stream), -1);
    }
    fclose(stream);
    return ok;
}

/* Bounded line reader: reject embedded NULs and lines that would be truncated. */
static bool clip_line(FILE *stream, char *out, size_t capacity) {
    size_t len = 0;
    int c;
    while ((c = fgetc(stream)) != EOF && c != '\n') {
        if (c == 0 || len + 1 >= capacity) return false;
        out[len++] = (char)c;
    }
    out[len] = '\0';
    return !ferror(stream) && (c == '\n' || len != 0);
}

static bool clip_number(const char *text, const char *prefix, size_t max, size_t *out) {
    size_t prefix_len = strlen(prefix);
    if (strncmp(text, prefix, prefix_len) != 0) return false;
    text += prefix_len;
    if (!*text) return false;
    size_t number = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') return false;
        unsigned digit = (unsigned)(*text - '0');
        if (number > (max - digit) / 10) return false;
        number = number * 10 + digit;
    }
    if (!number || number > max) return false;
    *out = number;
    return true;
}

bool clipboard_read_paths(FILE *stream, ClipboardPaths *out) {
    if (!stream || !out) return false;
    *out = (ClipboardPaths){0};
    char line[CLIP_MAX_PATH * 2 + 16];
    if (!clip_line(stream, line, sizeof(line))) return false;
    bool v3 = strcmp(line, "CUPIDFM_CLIP_V3") == 0;
    bool v2 = strcmp(line, "CUPIDFM_CLIP_V2") == 0;
    if (!v3 && !v2) {
        /* Old single-file payloads carry only the original path and type. */
        if (!valid_path(line)) return false;
        out->paths = calloc(1, sizeof(char *));
        if (!out->paths) return false;
        out->count = 1;
        out->paths[0] = strdup(line);
        if (!out->paths[0]) goto invalid;
        if (!clip_line(stream, line, sizeof(line)) ||
            (strcmp(line, "0") != 0 && strcmp(line, "1") != 0)) goto invalid;
        if (clip_line(stream, line, sizeof(line))) {
            /* Old CUT payloads refer to PID-specific storage, not their stated
             * source. Refuse them rather than moving a different current file. */
            if (strcmp(line, "COPY") != 0) goto invalid;
        } else if (!feof(stream)) goto invalid;
        if (fgetc(stream) != EOF || ferror(stream)) goto invalid;
        return true;
    }
    if (!clip_line(stream, line, sizeof(line))) return false;
    if (strcmp(line, "OP=CUT") == 0) out->cut = true;
    else if (strcmp(line, "OP=COPY") != 0) return false;
    size_t count;
    if (!clip_line(stream, line, sizeof(line)) || !clip_number(line, "N=", CLIP_MAX_ITEMS, &count)) return false;
    out->paths = calloc(count, sizeof(char *));
    if (!out->paths) return false;
    out->count = count;
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!clip_line(stream, line, sizeof(line))) goto invalid;
        if (v3) {
            size_t len;
            if (!clip_number(line, "P=", CLIP_MAX_PATH - 1, &len)) goto invalid;
            total += len;
            if (total > CLIP_MAX_BYTES) goto invalid;
            out->paths[i] = malloc(len + 1);
            if (!out->paths[i]) goto invalid;
            if (fread(out->paths[i], 1, len, stream) != len || fgetc(stream) != '\n' ||
                memchr(out->paths[i], '\0', len)) goto invalid;
            out->paths[i][len] = '\0';
            if (!valid_path(out->paths[i])) goto invalid;
        } else {
            char *path = strchr(line, '\t');
            if (!path) goto invalid;
            *path++ = '\0';
            if (strcmp(line, "0") != 0 && strcmp(line, "1") != 0) goto invalid;
            char *name = strchr(path, '\t');
            if (!name) goto invalid;
            *name++ = '\0';
            if (!valid_path(path)) goto invalid;
            const char *base = strrchr(path, '/');
            base = base ? base + 1 : path;
            if (strcmp(name, base) != 0) goto invalid;
            total += strlen(path);
            if (total > CLIP_MAX_BYTES) goto invalid;
            out->paths[i] = strdup(path);
            if (!out->paths[i]) goto invalid;
        }
    }
    if (fgetc(stream) != EOF || ferror(stream)) goto invalid;
    return true;
invalid:
    clipboard_paths_free(out);
    return false;
}
