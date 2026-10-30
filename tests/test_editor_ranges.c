#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#define _POSIX_C_SOURCE 200809L
#include "../src/fs/files.c"
#include <assert.h>

volatile sig_atomic_t is_editing = 1;

static TextBuffer buffer;

static void setup(void) {
    buffer = (TextBuffer){.num_lines = 2, .capacity = 2};
    buffer.lines = calloc(2, sizeof(char *));
    assert(buffer.lines);
    buffer.lines[0] = strdup("abc");
    buffer.lines[1] = strdup("def");
    assert(buffer.lines[0] && buffer.lines[1]);
    g_editor_buffer = &buffer;
    g_editor_cursor_line = 0;
    g_editor_cursor_col = 0;
    g_editor_dirty = false;
}

static void teardown(void) {
    for (int i = 0; i < buffer.num_lines; ++i)
        free(buffer.lines[i]);
    free(buffer.lines);
    free(buffer.eols);
    g_editor_buffer = NULL;
}

static void invalid_ranges(void) {
    const int ranges[][4] = {{1, 1, 2, 999},     {1, 999, 2, 1},
                             {1, 0, 1, 2},       {1, -1, 2, 2},
                             {1, 1, 2, 0},       {0, 1, 1, 1},
                             {1, 1, 3, 1},       {2, 1, 1, 1},
                             {1, 3, 1, 2},       {INT_MIN, 1, 1, 1},
                             {1, INT_MIN, 1, 1}, {1, 1, INT_MIN, 1},
                             {1, 1, 1, INT_MIN}, {1, INT_MAX, 2, INT_MAX}};
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); ++i) {
        for (int replace = 0; replace < 2; ++replace) {
            setup();
            const int *r = ranges[i];
            bool ok = replace ? editor_replace_text(r[0], r[1], r[2], r[3], "X")
                              : editor_delete_range(r[0], r[1], r[2], r[3]);
            assert(!ok);
            assert(buffer.num_lines == 2);
            assert(strcmp(buffer.lines[0], "abc") == 0);
            assert(strcmp(buffer.lines[1], "def") == 0);
            assert(!g_editor_dirty);
            assert(g_editor_cursor_line == 0 && g_editor_cursor_col == 0);
            teardown();
        }
    }
}

int main(void) {
    invalid_ranges();
    setup();
    assert(editor_delete_range(1, 2, 2, 3));
    assert(buffer.num_lines == 1 && strcmp(buffer.lines[0], "af") == 0);
    teardown();
    setup();
    assert(editor_replace_text(1, 2, 2, 3, "X"));
    assert(buffer.num_lines == 1 && strcmp(buffer.lines[0], "aXf") == 0);
    teardown();
    setup();
    assert(editor_delete_range(1, 1, 1, 4));
    assert(strcmp(buffer.lines[0], "") == 0);
    teardown();
    setup();
    assert(editor_set_cursor(1, INT_MIN));
    assert(g_editor_cursor_col == 0);
    assert(!editor_set_cursor(INT_MIN, 1));
    teardown();
    assert(!editor_set_cursor(1, 1));
    assert(!editor_delete_range(1, 1, 1, 1));
    assert(!editor_replace_text(1, 1, 1, 1, "X"));
    puts("Editor range validation checks passed");
    return 0;
}
