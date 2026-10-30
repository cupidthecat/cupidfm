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

static FILE *fixture(void) {
    FILE *f = tmpfile();
    assert(f);
    for (int i = 0; i < 1000; ++i)
        assert(fputc('a', f) != EOF);
    assert(fputs("\n\ttext\t\n", f) >= 0);
    for (int i = 0; i < 20; ++i)
        assert(fputs("tail\n", f) >= 0);
    rewind(f);
    return f;
}

int main(void) {
    int failures = 0, successes = 0;
    for (int point = 0; point < 30; ++point) {
        setup();
        FILE *f = fixture();
        char **original = buffer.lines;
        allocation_failed = 0;
        fail_after = point;
        bool ok = editor_load_stream(f, &buffer);
        fail_after = -1;
        if (allocation_failed) {
            ++failures;
            assert(!ok && buffer.lines == original);
            assert(buffer.capacity == 2 && buffer.num_lines == 2);
            assert(strcmp(buffer.lines[0], "abc") == 0 && strcmp(buffer.lines[1], "def") == 0);
        } else {
            ++successes;
            assert(ok && buffer.num_lines == 22);
            assert(strlen(buffer.lines[0]) == 1000);
            assert(strcmp(buffer.lines[1], "\ttext\t") == 0);
            assert(strcmp(buffer.lines[21], "tail") == 0);
        }
        fclose(f);
        teardown();
    }
    assert(failures && successes);
    for (int point = 0; point < 3; ++point) {
        setup();
        FILE *f = tmpfile();
        assert(f);
        fail_after = point;
        allocation_failed = 0;
        bool ok = editor_load_stream(f, &buffer);
        fail_after = -1;
        if (allocation_failed) {
            assert(!ok && buffer.num_lines == 2 && strcmp(buffer.lines[0], "abc") == 0);
        } else {
            assert(ok && buffer.num_lines == 1 && !buffer.lines[0][0]);
        }
        fclose(f);
        teardown();
    }
    setup();
    FILE *f = tmpfile();
    assert(f);
    const char binary[] = {'a', 0, 'b', '\n'};
    assert(fwrite(binary, 1, sizeof(binary), f) == sizeof(binary));
    rewind(f);
    assert(!editor_load_stream(f, &buffer));
    assert(buffer.num_lines == 2 && strcmp(buffer.lines[0], "abc") == 0);
    fclose(f);
    teardown();
    setup();
    assert(!editor_load_file_into_buffer("/", &buffer));
    assert(buffer.num_lines == 2 && strcmp(buffer.lines[0], "abc") == 0);
    teardown();
    puts("Editor loading checks passed");
}
