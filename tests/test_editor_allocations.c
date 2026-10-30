#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Inject failures only while the production bookkeeping calls are running. */
static long fail_after = -1;
static int allocation_failed;

static int should_fail(void) {
    if (fail_after < 0)
        return 0;
    if (fail_after-- == 0) {
        allocation_failed = 1;
        fail_after = -1;
        return 1;
    }
    return 0;
}

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
char *__real_strdup(const char *);

void *__wrap_malloc(size_t n) {
    return should_fail() ? NULL : __real_malloc(n);
}

void *__wrap_calloc(size_t n, size_t size) {
    return should_fail() ? NULL : __real_calloc(n, size);
}

void *__wrap_realloc(void *ptr, size_t n) {
    return should_fail() ? NULL : __real_realloc(ptr, n);
}

char *__wrap_strdup(const char *s) {
    return should_fail() ? NULL : __real_strdup(s);
}

#include "../src/fs/files.c"
volatile sig_atomic_t is_editing = 1;
static TextBuffer buffer;

static void setup(void) {
    buffer = (TextBuffer){.num_lines = 2, .capacity = 2};
    buffer.lines = calloc(2, sizeof(char *));
    assert(buffer.lines);
    buffer.lines[0] = strdup("abc");
    buffer.lines[1] = strdup("def");
    assert(buffer.lines[0] && buffer.lines[1]);
    buffer.eols = malloc(2 * sizeof(*buffer.eols));
    assert(buffer.eols);
    buffer.eols[0] = EDITOR_CRLF;
    buffer.eols[1] = EDITOR_NO_EOL;
    buffer.default_eol = EDITOR_CRLF;
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

int main(void) {
    const char *texts[] = {"X", "X\nY\nZ", "\n", ""};
    for (int replace = 0; replace < 2; ++replace) {
        for (size_t t = 0; t < sizeof(texts) / sizeof(texts[0]); ++t) {
            int saw_failure = 0, saw_success = 0;
            for (long point = 0; point < 8; ++point) {
                setup();
                g_editor_cursor_col = 1;
                char **original = buffer.lines;
                EditorEol *original_eols = buffer.eols;
                fail_after = point;
                allocation_failed = 0;
                bool ok = replace ? editor_replace_text(1, 2, 2, 3, texts[t])
                                  : editor_insert_text(texts[t]);
                fail_after = -1;
                if (allocation_failed) {
                    saw_failure = 1;
                    assert(!ok && buffer.lines == original);
                    assert(buffer.eols == original_eols && buffer.eols[0] == EDITOR_CRLF &&
                           buffer.eols[1] == EDITOR_NO_EOL);
                    assert(buffer.capacity == 2 && buffer.num_lines == 2);
                    assert(strcmp(buffer.lines[0], "abc") == 0);
                    assert(strcmp(buffer.lines[1], "def") == 0);
                    assert(!g_editor_dirty);
                    assert(g_editor_cursor_line == 0 && g_editor_cursor_col == 1);
                    assert(editor_insert_text("!"));
                    assert(strcmp(buffer.lines[0], "a!bc") == 0);
                } else {
                    saw_success = 1;
                    assert(ok && g_editor_dirty);
                    const char *first[] = {replace ? "aXf" : "aXbc", "aX", "a",
                                           replace ? "af" : "abc"};
                    assert(strcmp(buffer.lines[0], first[t]) == 0);
                    if (t == 1) {
                        assert(buffer.num_lines == (replace ? 3 : 4));
                        assert(strcmp(buffer.lines[1], "Y") == 0);
                        assert(strcmp(buffer.lines[2], replace ? "Zf" : "Zbc") == 0);
                        assert(g_editor_cursor_line == 2 && g_editor_cursor_col == 1);
                    }
                    if (t == 2) {
                        assert(buffer.num_lines == (replace ? 2 : 3));
                        assert(strcmp(buffer.lines[1], replace ? "f" : "bc") == 0);
                        assert(g_editor_cursor_line == 1 && g_editor_cursor_col == 0);
                    }
                }
                teardown();
            }
            assert(saw_failure && saw_success);
        }
    }
    setup();
    g_editor_dirty = true;
    g_editor_cursor_line = 1;
    g_editor_cursor_col = 2;
    fail_after = 0;
    assert(!editor_insert_text("X"));
    fail_after = -1;
    assert(g_editor_dirty && g_editor_cursor_line == 1 && g_editor_cursor_col == 2);
    assert(editor_insert_text("X\nY"));
    assert(strcmp(buffer.lines[0], "abc") == 0);
    assert(strcmp(buffer.lines[1], "deX") == 0);
    assert(strcmp(buffer.lines[2], "Yf") == 0);
    teardown();
    setup();
    assert(editor_replace_text(1, 1, 1, 4, buffer.lines[1]));
    assert(strcmp(buffer.lines[0], "def") == 0 && strcmp(buffer.lines[1], "def") == 0);
    teardown();
    for (int point = 0; point < 6; ++point) {
        setup();
        fail_after = point;
        allocation_failed = 0;
        EditorSnapshot snapshot = editor_snapshot_make(&buffer, 0, 0, 0);
        fail_after = -1;
        assert((snapshot.lines != NULL) == !allocation_failed);
        assert(buffer.eols[0] == EDITOR_CRLF && buffer.eols[1] == EDITOR_NO_EOL);
        if (snapshot.lines)
            assert(snapshot.eols[0] == EDITOR_CRLF && snapshot.eols[1] == EDITOR_NO_EOL);
        editor_snapshot_free(&snapshot);
        teardown();
    }
    puts("Editor allocation failure checks passed");
}
