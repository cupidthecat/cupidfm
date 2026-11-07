#define _GNU_SOURCE
#include "file_search.h"

#include <gio/gio.h>
#include <string.h>

struct FileSearch {
    GSubprocess *child;
    GThread *waiter;
    GByteArray *pending;
    GString *diagnostic;
    char *query, *error;
    gint exited;
    int status;
    bool content, output_eof, error_eof, protocol_error, io_error;
};

static gpointer wait_child(gpointer data) {
    FileSearch *search = data;
    GError *error = NULL;
    if (!g_subprocess_wait(search->child, NULL, &error)) {
        search->status = -1;
        search->error = g_strdup(error ? error->message : "Cannot wait for search");
        g_clear_error(&error);
    } else {
        search->status = g_subprocess_get_if_exited(search->child)
                             ? g_subprocess_get_exit_status(search->child)
                             : 128;
    }
    g_atomic_int_set(&search->exited, 1);
    return NULL;
}

FileSearch *file_search_start(const char *directory, const char *query, bool content, bool hidden,
                              GError **error) {
    if (!directory || !query) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "Missing search directory or query");
        return NULL;
    }
    char *program = g_find_program_in_path(content ? "rg" : "fd");
    if (!program && !content)
        program = g_find_program_in_path("fdfind");
    if (!program) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "%s is not installed",
                    content ? "rg" : "fd or fdfind");
        return NULL;
    }
    const char *rg[] = {program,  "--color=never", "--files-with-matches",
                        "--null", "--smart-case",  hidden ? "--hidden" : "--no-hidden",
                        "--",     query,           ".",
                        NULL};
    const char *fd[] = {
        program, "--color=never", "--print0", "--regex", hidden ? "--hidden" : "--no-hidden",
        "--",    query,           ".",        NULL};
    GSubprocessLauncher *launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE);
    g_subprocess_launcher_set_cwd(launcher, directory);
    GSubprocess *child = g_subprocess_launcher_spawnv(launcher, content ? rg : fd, error);
    g_object_unref(launcher);
    g_free(program);
    if (!child)
        return NULL;
    FileSearch *search = g_new0(FileSearch, 1);
    search->child = child;
    search->query = g_strdup(query);
    search->content = content;
    search->pending = g_byte_array_new();
    search->diagnostic = g_string_new(NULL);
    search->waiter = g_thread_try_new("file-search", wait_child, search, error);
    if (!search->waiter) {
        file_search_free(search);
        return NULL;
    }
    return search;
}

static bool read_pipe(GInputStream *stream, GByteArray *pending, GString *diagnostic, bool *eof) {
    if (*eof)
        return false;
    bool failed = false;
    unsigned char buffer[8192];
    size_t budget = 65536;
    while (budget) {
        GError *error = NULL;
        gssize count = g_pollable_input_stream_read_nonblocking(
            G_POLLABLE_INPUT_STREAM(stream), buffer, MIN(sizeof(buffer), budget), NULL, &error);
        if (count < 0) {
            if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
                *eof = true;
                failed = true;
            }
            g_clear_error(&error);
            break;
        }
        if (!count) {
            *eof = true;
            break;
        }
        budget -= (size_t)count;
        if (pending)
            g_byte_array_append(pending, buffer, (guint)count);
        if (diagnostic && diagnostic->len < 4096)
            g_string_append_len(diagnostic, (const char *)buffer,
                                (gssize)MIN((gsize)count, 4096 - diagnostic->len));
    }
    return failed;
}

GPtrArray *file_search_take(FileSearch *search, size_t limit) {
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    if (!search)
        return paths;
    search->io_error |= read_pipe(g_subprocess_get_stderr_pipe(search->child), NULL,
                                  search->diagnostic, &search->error_eof);
    if (search->pending->len < 65536)
        search->io_error |= read_pipe(g_subprocess_get_stdout_pipe(search->child), search->pending,
                                      NULL, &search->output_eof);
    if (search->io_error && !g_atomic_int_get(&search->exited))
        g_subprocess_force_exit(search->child);
    size_t used = 0;
    while (used < search->pending->len && paths->len < limit) {
        const unsigned char *first = search->pending->data + used;
        const unsigned char *end = memchr(first, 0, search->pending->len - used);
        if (!end)
            break;
        g_ptr_array_add(paths, g_strndup((const char *)first, (gsize)(end - first)));
        used += (size_t)(end - first) + 1;
    }
    if (used)
        g_byte_array_remove_range(search->pending, 0, (guint)used);
    if ((search->pending->len >= 65536 &&
         !memchr(search->pending->data, 0, search->pending->len)) ||
        (search->output_eof && search->pending->len &&
         !memchr(search->pending->data, 0, search->pending->len))) {
        g_string_assign(search->diagnostic, "Search returned an invalid or oversized path record");
        g_byte_array_set_size(search->pending, 0);
        if (!g_atomic_int_get(&search->exited))
            g_subprocess_force_exit(search->child);
        search->protocol_error = true;
    }
    return paths;
}

bool file_search_done(FileSearch *search) {
    return search && g_atomic_int_get(&search->exited) && search->output_eof && search->error_eof &&
           !search->pending->len;
}

const char *file_search_error(FileSearch *search) {
    if (!file_search_done(search))
        return NULL;
    if (search->error)
        return search->error;
    if (search->protocol_error)
        return search->diagnostic->str;
    if (search->io_error)
        return "Cannot read search output";
    if (search->status == 0 || (search->content && search->status == 1))
        return NULL;
    return search->diagnostic->len ? search->diagnostic->str : "Search process failed";
}

const char *file_search_query(FileSearch *search) {
    return search->query;
}

bool file_search_content(FileSearch *search) {
    return search->content;
}

void file_search_free(FileSearch *search) {
    if (!search)
        return;
    if (!g_atomic_int_get(&search->exited))
        g_subprocess_force_exit(search->child);
    if (search->waiter)
        g_thread_join(search->waiter);
    else
        g_subprocess_wait(search->child, NULL, NULL);
    g_object_unref(search->child);
    g_byte_array_unref(search->pending);
    g_string_free(search->diagnostic, TRUE);
    g_free(search->query);
    g_free(search->error);
    g_free(search);
}
