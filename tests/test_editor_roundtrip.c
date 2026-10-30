#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#define _POSIX_C_SOURCE 200809L
#include "../src/fs/files.c"
#include <assert.h>
volatile sig_atomic_t is_editing = 1;

static void load(TextBuffer *b, const char *bytes) {
    FILE *f = tmpfile();
    assert(f);
    assert(fwrite(bytes, 1, strlen(bytes), f) == strlen(bytes));
    rewind(f);
    assert(editor_load_stream(f, b));
    fclose(f);
}

static void expect(const TextBuffer *b, const char *bytes) {
    FILE *f = tmpfile();
    assert(f);
    assert(editor_write_buffer(f, b));
    assert(fflush(f) == 0);
    rewind(f);
    char result[4096];
    size_t n = fread(result, 1, sizeof(result), f);
    assert(n == strlen(bytes) && memcmp(result, bytes, n) == 0);
    fclose(f);
}

static void release(TextBuffer *b) {
    for (int i = 0; i < b->num_lines; ++i)
        free(b->lines[i]);
    free(b->lines);
    free(b->eols);
    *b = (TextBuffer){0};
}

int main(void) {
    const char *cases[] = {"",          "a",    "a\n",   "a\r\n", "a\r\nb\nc",
                           "\n",        "\r\n", "a\n\n", "a\r",   "\tlong\t\r\nlast",
                           "a\r\nb\r\n"};
    TextBuffer b = {0};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        load(&b, cases[i]);
        expect(&b, cases[i]);
        release(&b);
    }
    load(&b, "ab\r\nc");
    g_editor_buffer = &b;
    g_editor_cursor_line = 0;
    g_editor_cursor_col = 1;
    assert(editor_insert_text("X\nY"));
    expect(&b, "aX\r\nYb\r\nc");
    assert(editor_delete_range(1, 2, 2, 2));
    expect(&b, "ab\r\nc");
    int line = 1, col = 1;
    insert_text_at_cursor(&b, &line, &col, "\nend");
    expect(&b, "ab\r\nc\r\nend");
    assert(line == 2 && col == 3);
    release(&b);
    load(&b, "a\r\nb\nc");
    EditorSnapshot snapshot = editor_snapshot_make(&b, 0, 0, 0);
    assert(snapshot.lines);
    g_sel_active = true;
    g_sel_anchor_line = 0;
    g_sel_anchor_col = 1;
    g_sel_end_line = 1;
    g_sel_end_col = 0;
    delete_selection(&b);
    expect(&b, "ab\nc");
    int start = 0;
    editor_apply_snapshot(&b, &snapshot, &line, &col, &start);
    expect(&b, "a\r\nb\nc");
    g_editor_cursor_line = 2;
    g_editor_cursor_col = 1;
    assert(editor_insert_text("\r\n"));
    expect(&b, "a\r\nb\nc\r\n");
    release(&b);
    g_editor_buffer = NULL;
    puts("Editor byte round-trip checks passed");
}
