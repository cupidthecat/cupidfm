#define _GNU_SOURCE
#include "file_search.h"

#include <assert.h>
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static GPtrArray *collect(FileSearch *search) {
    GPtrArray *all = g_ptr_array_new_with_free_func(g_free);
    gint64 deadline = g_get_monotonic_time() + 10000000;
    while (!file_search_done(search) && g_get_monotonic_time() < deadline) {
        GPtrArray *batch = file_search_take(search, 1);
        for (guint i = 0; i < batch->len; i++) {
            const char *path = g_ptr_array_index(batch, i);
            while (g_str_has_prefix(path, "./"))
                path += 2;
            g_ptr_array_add(all, g_strdup(path));
        }
        g_ptr_array_free(batch, TRUE);
        g_usleep(1000);
    }
    assert(file_search_done(search));
    return all;
}

int main(void) {
    char root[] = "/tmp/cupidfm-search-XXXXXX";
    assert(mkdtemp(root));
    const char *names[] = {"top.txt",     "nested/a.txt", "nested/line\nbreak.txt",
                           ".hidden.txt", "ignored.txt",  ".ignore"};
    const char *texts[] = {"Needle", "needle", "Needle", "Needle", "Needle", "ignored.txt\n"};
    char *nested = g_build_filename(root, "nested", NULL);
    assert(!mkdir(nested, 0700));
    for (size_t i = 0; i < G_N_ELEMENTS(names); i++) {
        char *path = g_build_filename(root, names[i], NULL);
        assert(g_file_set_contents(path, texts[i], -1, NULL));
        g_free(path);
    }
    GError *error = NULL;
    FileSearch *search = file_search_start(root, "Needle", true, false, &error);
    assert(search && !error);
    GPtrArray *all = collect(search);
    assert(!file_search_error(search));
    assert(all->len == 2);
    assert(g_ptr_array_find_with_equal_func(all, "nested/line\nbreak.txt", g_str_equal, NULL));
    g_ptr_array_free(all, TRUE);
    file_search_free(search);
    search = file_search_start(root, "needle", true, true, &error);
    assert(search && !error);
    all = collect(search);
    assert(!file_search_error(search) && all->len == 4);
    g_ptr_array_free(all, TRUE);
    file_search_free(search);
    search = file_search_start(root, "\\.txt$", false, false, &error);
    assert(search && !error);
    all = collect(search);
    if (file_search_error(search))
        fprintf(stderr, "%s\n", file_search_error(search));
    assert(!file_search_error(search) && all->len == 3);
    g_ptr_array_free(all, TRUE);
    file_search_free(search);
    search = file_search_start(root, "[", true, false, &error);
    assert(search);
    all = collect(search);
    assert(file_search_error(search));
    g_ptr_array_free(all, TRUE);
    file_search_free(search);
    search = file_search_start(root, "no-such-pattern", true, false, &error);
    assert(search);
    all = collect(search);
    assert(!all->len && !file_search_error(search));
    g_ptr_array_free(all, TRUE);
    file_search_free(search);
    search = file_search_start(root, ".", true, false, &error);
    assert(search);
    gint64 start = g_get_monotonic_time();
    file_search_free(search);
    assert(g_get_monotonic_time() - start < 2000000);
    assert(!file_search_start(NULL, "x", false, false, &error) && error);
    g_clear_error(&error);
    for (size_t i = 0; i < G_N_ELEMENTS(names); i++) {
        char *path = g_build_filename(root, names[i], NULL);
        assert(!unlink(path));
        g_free(path);
    }
    assert(!rmdir(nested) && !rmdir(root));
    g_free(nested);
    puts("Streaming filename/content search, NUL paths, ignores, smart case, errors, and "
         "cancellation passed");
}
