// clipboard.c
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "clipboard.h"
#include "process.h"
#include <fcntl.h>
#include <gio/gio.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CLIP_MAX_PATH 4096
#define CLIP_MAX_ITEMS 65536
#define CLIP_MAX_BYTES (16 * 1024 * 1024)
#define CLIP_OSC_MAX_BYTES (1024 * 1024)

void clipboard_paths_free(ClipboardPaths *paths) {
    if (!paths)
        return;
    for (size_t i = 0; i < paths->count; ++i)
        free(paths->paths[i]);
    free(paths->paths);
    *paths = (ClipboardPaths){0};
}

static bool valid_path(const char *path) {
    if (!path || !*path || strlen(path) >= CLIP_MAX_PATH)
        return false;
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    return *base && strcmp(base, ".") != 0 && strcmp(base, "..") != 0;
}

static ClipboardPaths yanked;
static bool owns_yank;

void clipboard_clear_paths(void) {
    clipboard_paths_free(&yanked);
    owns_yank = true;
}

static bool desktop_clipboard(FILE *stream, bool writing, bool wayland) {
    if (fseek(stream, 0, SEEK_SET))
        return false;
    int descriptor = dup(fileno(stream));
    if (descriptor < 0)
        return false;
    const char *wayland_write[] = {"wl-copy", NULL};
    const char *x11_write[] = {"xclip", "-selection", "clipboard", "-i", NULL};
    const char *wayland_read[] = {"wl-paste", "--no-newline", NULL};
    const char *x11_read[] = {"xclip", "-selection", "clipboard", "-o", NULL};
    GSubprocessFlags flags =
        G_SUBPROCESS_FLAGS_STDERR_SILENCE | (writing ? G_SUBPROCESS_FLAGS_STDOUT_SILENCE : 0);
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(flags);
    if (writing)
        g_subprocess_launcher_take_stdin_fd(launcher, descriptor);
    else
        g_subprocess_launcher_take_stdout_fd(launcher, descriptor);
    const char *const *args =
        writing ? (wayland ? wayland_write : x11_write) : (wayland ? wayland_read : x11_read);
    GSubprocess *child = g_subprocess_launcher_spawnv(launcher, args, NULL);
    bool ok = child && g_subprocess_wait_check(child, NULL, NULL);
    if (child)
        g_object_unref(child);
    g_object_unref(launcher);
    return ok;
}

bool clipboard_get_paths(ClipboardPaths *out) {
    if (!out)
        return false;
    *out = (ClipboardPaths){0};
    if (owns_yank) {
        if (!yanked.count)
            return false;
        out->paths = calloc(yanked.count, sizeof(char *));
        if (!out->paths)
            return false;
        out->count = yanked.count;
        out->cut = yanked.cut;
        for (size_t i = 0; i < out->count; i++) {
            out->paths[i] = strdup(yanked.paths[i]);
            if (!out->paths[i]) {
                clipboard_paths_free(out);
                return false;
            }
        }
        return true;
    }
    const char *wayland = getenv("WAYLAND_DISPLAY"), *display = getenv("DISPLAY");
    bool have_wayland = wayland && *wayland, have_x11 = display && *display;
    if (!have_wayland && !have_x11)
        return false;
    FILE *stream = tmpfile();
    if (!stream)
        return false;
    bool copied = have_wayland && desktop_clipboard(stream, false, true);
    if (!copied && have_x11)
        copied = desktop_clipboard(stream, false, false);
    bool ok = copied && !fseek(stream, 0, SEEK_SET) && clipboard_read_paths(stream, out);
    fclose(stream);
    return ok;
}

bool clipboard_write_osc52(FILE *stream, const void *data, size_t length, bool tmux) {
    if (!stream || (!data && length) || length > CLIP_OSC_MAX_BYTES)
        return false;
    char *encoded = g_base64_encode(data ? data : (const unsigned char *)"", length);
    flockfile(stream);
    bool ok = fputs(tmux ? "\033Ptmux;\033\033]52;c;" : "\033]52;c;", stream) != EOF &&
              fputs(encoded, stream) != EOF &&
              fputs(tmux ? "\033\033\\\033\\" : "\033\\", stream) != EOF && fflush(stream) == 0;
    funlockfile(stream);
    g_free(encoded);
    return ok;
}

static bool publish_text(const void *data, size_t length) {
    if (length > CLIP_MAX_BYTES || (!data && length))
        return false;
    if (!data)
        data = "";
    const char *wayland_display = getenv("WAYLAND_DISPLAY"), *display = getenv("DISPLAY");
    bool wayland = wayland_display && *wayland_display, x11 = display && *display;
    bool ok = false;
    if (wayland || x11) {
        FILE *stream = tmpfile();
        if (stream) {
            bool written = fwrite(data, 1, length, stream) == length && fflush(stream) == 0;
            if (written && wayland)
                ok = desktop_clipboard(stream, true, true);
            if (!ok && written && x11)
                ok = desktop_clipboard(stream, true, false);
            fclose(stream);
        }
    }
    const char *term = getenv("TERM"), *tmux = getenv("TMUX");
    if (!ok && isatty(STDOUT_FILENO) && term && strcmp(term, "dumb"))
        ok = clipboard_write_osc52(stdout, data, length, tmux && *tmux);
    return ok;
}

bool clipboard_set_text(const char *text) {
    return text && publish_text(text, strlen(text));
}

bool clipboard_set_from_file(const char *path) {
    if (!path || !*path)
        return false;
    int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
        return false;
    struct stat info;
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size > CLIP_MAX_BYTES) {
        close(fd);
        return false;
    }
    GByteArray *bytes = g_byte_array_new();
    bool ok = true;
    unsigned char buffer[8192];
    while (bytes->len <= CLIP_MAX_BYTES) {
        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            ok = false;
            break;
        }
        if (!count)
            break;
        g_byte_array_append(bytes, buffer, (guint)count);
    }
    close(fd);
    ok = ok && bytes->len <= CLIP_MAX_BYTES && publish_text(bytes->data, bytes->len);
    g_byte_array_unref(bytes);
    return ok;
}

bool clipboard_set_paths(const char *const *paths, size_t count, bool cut) {
    if (!paths || !count || count > CLIP_MAX_ITEMS)
        return false;
    FILE *stream = tmpfile();
    if (!stream)
        return false;
    bool ok = fprintf(stream, "CUPIDFM_CLIP_V3\nOP=%s\nN=%zu\n", cut ? "CUT" : "COPY", count) > 0;
    size_t total = 0;
    for (size_t i = 0; ok && i < count; ++i) {
        struct stat st;
        if (!valid_path(paths[i]) || lstat(paths[i], &st) != 0) {
            ok = false;
            break;
        }
        size_t len = strlen(paths[i]);
        total += len;
        if (total > CLIP_MAX_BYTES) {
            ok = false;
            break;
        }
        ok = fprintf(stream, "P=%zu\n", len) > 0 && fwrite(paths[i], 1, len, stream) == len &&
             fputc('\n', stream) != EOF;
    }
    if (ok)
        ok = fflush(stream) == 0 && fseek(stream, 0, SEEK_SET) == 0;
    if (ok) {
        ClipboardPaths replacement = {0};
        ok = clipboard_read_paths(stream, &replacement);
        if (ok) {
            clipboard_paths_free(&yanked);
            yanked = replacement;
            owns_yank = true;
            rewind(stream);
            if (getenv("WAYLAND_DISPLAY"))
                (void)desktop_clipboard(stream, true, true);
            else if (getenv("DISPLAY"))
                (void)desktop_clipboard(stream, true, false);
        }
    }
    fclose(stream);
    return ok;
}

/* Bounded line reader: reject embedded NULs and lines that would be truncated. */
static bool clip_line(FILE *stream, char *out, size_t capacity) {
    size_t len = 0;
    int c;
    while ((c = fgetc(stream)) != EOF && c != '\n') {
        if (c == 0 || len + 1 >= capacity)
            return false;
        out[len++] = (char)c;
    }
    out[len] = '\0';
    return !ferror(stream) && (c == '\n' || len != 0);
}

static bool clip_number(const char *text, const char *prefix, size_t max, size_t *out) {
    size_t prefix_len = strlen(prefix);
    if (strncmp(text, prefix, prefix_len) != 0)
        return false;
    text += prefix_len;
    if (!*text)
        return false;
    size_t number = 0;
    for (; *text; ++text) {
        if (*text < '0' || *text > '9')
            return false;
        unsigned digit = (unsigned)(*text - '0');
        if (number > (max - digit) / 10)
            return false;
        number = number * 10 + digit;
    }
    if (!number || number > max)
        return false;
    *out = number;
    return true;
}

bool clipboard_read_paths(FILE *stream, ClipboardPaths *out) {
    if (!stream || !out)
        return false;
    *out = (ClipboardPaths){0};
    char line[CLIP_MAX_PATH * 2 + 16];
    if (!clip_line(stream, line, sizeof(line)))
        return false;
    bool v3 = strcmp(line, "CUPIDFM_CLIP_V3") == 0;
    bool v2 = strcmp(line, "CUPIDFM_CLIP_V2") == 0;
    if (!v3 && !v2) {
        /* Old single-file payloads carry only the original path and type. */
        if (!valid_path(line))
            return false;
        out->paths = calloc(1, sizeof(char *));
        if (!out->paths)
            return false;
        out->count = 1;
        out->paths[0] = strdup(line);
        if (!out->paths[0])
            goto invalid;
        if (!clip_line(stream, line, sizeof(line)) ||
            (strcmp(line, "0") != 0 && strcmp(line, "1") != 0))
            goto invalid;
        if (clip_line(stream, line, sizeof(line))) {
            /* Old CUT payloads refer to PID-specific storage, not their stated
             * source. Refuse them rather than moving a different current file. */
            if (strcmp(line, "COPY") != 0)
                goto invalid;
        } else if (!feof(stream))
            goto invalid;
        if (fgetc(stream) != EOF || ferror(stream))
            goto invalid;
        return true;
    }
    if (!clip_line(stream, line, sizeof(line)))
        return false;
    if (strcmp(line, "OP=CUT") == 0)
        out->cut = true;
    else if (strcmp(line, "OP=COPY") != 0)
        return false;
    size_t count;
    if (!clip_line(stream, line, sizeof(line)) || !clip_number(line, "N=", CLIP_MAX_ITEMS, &count))
        return false;
    out->paths = calloc(count, sizeof(char *));
    if (!out->paths)
        return false;
    out->count = count;
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!clip_line(stream, line, sizeof(line)))
            goto invalid;
        if (v3) {
            size_t len;
            if (!clip_number(line, "P=", CLIP_MAX_PATH - 1, &len))
                goto invalid;
            total += len;
            if (total > CLIP_MAX_BYTES)
                goto invalid;
            out->paths[i] = malloc(len + 1);
            if (!out->paths[i])
                goto invalid;
            if (fread(out->paths[i], 1, len, stream) != len || fgetc(stream) != '\n' ||
                memchr(out->paths[i], '\0', len))
                goto invalid;
            out->paths[i][len] = '\0';
            if (!valid_path(out->paths[i]))
                goto invalid;
        } else {
            char *path = strchr(line, '\t');
            if (!path)
                goto invalid;
            *path++ = '\0';
            if (strcmp(line, "0") != 0 && strcmp(line, "1") != 0)
                goto invalid;
            char *name = strchr(path, '\t');
            if (!name)
                goto invalid;
            *name++ = '\0';
            if (!valid_path(path))
                goto invalid;
            const char *base = strrchr(path, '/');
            base = base ? base + 1 : path;
            if (strcmp(name, base) != 0)
                goto invalid;
            total += strlen(path);
            if (total > CLIP_MAX_BYTES)
                goto invalid;
            out->paths[i] = strdup(path);
            if (!out->paths[i])
                goto invalid;
        }
    }
    if (fgetc(stream) != EOF || ferror(stream))
        goto invalid;
    return true;
invalid:
    clipboard_paths_free(out);
    return false;
}
