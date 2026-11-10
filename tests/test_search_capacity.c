#define _POSIX_C_SOURCE 200809L
#include "files.h"
#include "search.h"
#include "vecstack.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int allocations_until_failure = -1;
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);

void *__wrap_malloc(size_t size) {
    if (allocations_until_failure == 0)
        return NULL;
    if (allocations_until_failure > 0)
        allocations_until_failure--;
    return __real_malloc(size);
}

void *__wrap_realloc(void *pointer, size_t size) {
    if (allocations_until_failure == 0)
        return NULL;
    if (allocations_until_failure > 0)
        allocations_until_failure--;
    return __real_realloc(pointer, size);
}

struct FileAttributes {
    char *name;
};

const char *FileAttr_get_name(FileAttr file) {
    return file->name;
}

bool g_select_all_highlight, should_clear_notif;
WINDOW *bannerwin;
const char *BANNER_TEXT, *BUILD_INFO;
int banner_offset;
pthread_mutex_t banner_mutex = PTHREAD_MUTEX_INITIALIZER;
static int idle_reads, directory_draws;

int __wrap_wgetch(WINDOW *window) {
    (void)window;
    return idle_reads++ < 5 ? ERR : 27;
}

void fix_cursor(CursorAndSlice *cas) {
    (void)cas;
}

void load_more_files_if_needed(Vector *v, const char *p, void *c, size_t *n, size_t t) {
    (void)v;
    (void)p;
    (void)c;
    (void)n;
    (void)t;
}

void draw_directory_window(WINDOW *w, const char *p, Vector *v, CursorAndSlice *c) {
    (void)w;
    (void)p;
    (void)v;
    (void)c;
    directory_draws++;
}

void draw_preview_window(WINDOW *w, const char *p, const char *s, int n) {
    (void)w;
    (void)p;
    (void)s;
    (void)n;
}

void draw_preview_window_path(WINDOW *w, const char *p, const char *s, int n) {
    draw_preview_window(w, p, s, n);
}

void draw_scrolling_banner(WINDOW *w, const char *t, const char *b, int o) {
    (void)w;
    (void)t;
    (void)b;
    (void)o;
}

void show_notification(WINDOW *w, const char *format, ...) {
    (void)w;
    (void)format;
}

int main(void) {
    AppState state = {0};
    state.files = Vector_new(1);
    state.search_files = Vector_new(1);
    for (size_t i = 0; i < 257; i++) {
        FileAttr file = malloc(sizeof(*file));
        assert(file);
        char name[32];
        snprintf(name, sizeof(name), "file-%03zu", i);
        file->name = strdup(name);
        assert(file->name);
        assert(Vector_add(&state.files, 1));
        size_t length = Vector_len(state.files);
        state.files.el[length] = file;
        Vector_set_len_no_free(&state.files, length + 1);
    }
    for (int mode = SEARCH_MODE_FUZZY; mode <= SEARCH_MODE_REGEX; mode++) {
        state.search_mode = mode;
        assert(search_rebuild(&state, mode == SEARCH_MODE_REGEX ? "^file-" : "file") == 257);
        assert(Vector_len(state.search_files) == 257);
        for (size_t i = 0; i < 257; i++)
            assert(state.search_files.el[i] == state.files.el[i]);
        assert(state.search_files.el[257] == NULL);
        assert(search_rebuild(&state, "no-such-entry") == 0);
    }
    /* Fail both the result-buffer allocation and the fuzzy hit allocation. */
    Vector_set_len_no_free(&state.search_files, 0);
    Vector_bye(&state.search_files);
    for (int mode = SEARCH_MODE_FUZZY; mode <= SEARCH_MODE_REGEX; mode++) {
        for (int fail = 0; fail <= (mode == SEARCH_MODE_FUZZY ? 1 : 0); fail++) {
            state.search_files = Vector_new(1);
            state.search_mode = mode;
            allocations_until_failure = fail;
            assert(search_rebuild(&state, "file") == 0);
            allocations_until_failure = -1;
            assert(Vector_len(state.search_files) == 0);
            assert(state.search_files.el[0] == NULL);
            Vector_bye(&state.search_files);
        }
    }
    FILE *input = tmpfile(), *output = tmpfile();
    SCREEN *screen = newterm("xterm-256color", output, input);
    assert(screen && set_term(screen));
    WINDOW *notice = newwin(1, 80, 0, 0);
    wtimeout(notice, 37);
    state.search_files = Vector_new(1);
    state.current_directory = "/tmp";
    state.lazy_load.files_loaded = state.lazy_load.total_files = Vector_len(state.files);
    strcpy(state.search_query, "file");
    state.dir_window_cas.num_lines = 10;
    assert(!prompt_fuzzy_search(&state, &state.dir_window_cas, notice, notice, notice));
    assert(idle_reads == 6 && !state.search_active);
    assert(directory_draws <= 2); // Opening and cancelling; idle input must not repaint.
    assert(wgetdelay(notice) == 37);
    Vector_bye(&state.search_files);
    delwin(notice);
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    for (size_t i = 0; i < Vector_len(state.files); i++) {
        FileAttr file = state.files.el[i];
        free(file->name);
        free(file);
    }
    Vector_set_len_no_free(&state.files, 0);
    Vector_bye(&state.files);

    Vector vector = Vector_new(1);
    void **original = vector.el;
    allocations_until_failure = 0;
    assert(!Vector_add(&vector, 100));
    allocations_until_failure = -1;
    assert(vector.el == original && Vector_len(vector) == 0 && vector.el[0] == NULL);
    errno = 0;
    assert(!Vector_add(&vector, SIZE_MAX));
    assert(errno == EOVERFLOW);
    assert(vector.el == original);
    Vector_bye(&vector);
    errno = 0;
    vector = Vector_new(SIZE_MAX);
    assert(!vector.el && errno == EOVERFLOW);
    allocations_until_failure = 0;
    vector = Vector_new(1);
    allocations_until_failure = -1;
    assert(!vector.el);
    assert(Vector_add(&vector, 1));
    assert(vector.el[0] == NULL);
    Vector_bye(&vector);

    VecStack stack = VecStack_empty();
    for (int i = 0; i < 10; i++) {
        void *item = malloc(1);
        assert(item && VecStack_push(&stack, item));
    }
    void *item = malloc(1);
    assert(item);
    allocations_until_failure = 0;
    assert(!VecStack_push(&stack, item));
    allocations_until_failure = -1;
    assert(Vector_len(stack.v) == 10);
    free(item);
    VecStack_bye(&stack);
    puts("Search capacity, borrowed results, overflow, and allocation failures passed");
    return 0;
}
