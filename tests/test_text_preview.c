#define _GNU_SOURCE
#include "text_preview.h"
#include <assert.h>
#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static SyntaxDef syntax = {
    .line_comment = "//", .block_comment_start = "/*", .block_comment_end = "*/",
    .string_delim = '"', .char_delim = '\''
};
static void expect(WINDOW *win, int row, const char *expected, int comment) {
    char text[256] = {0};
    assert(mvwinnstr(win, row, 2, text, 120) != ERR);
    if (strncmp(text, expected, strlen(expected))) {
        fprintf(stderr, "row %d: expected [%s], got [%s]\n", row, expected, text);
        abort();
    }
    if (comment >= 0)
        assert(((mvwinch(win, row, 2) & A_COLOR) == COLOR_PAIR(COLOR_SYNTAX_COMMENT)) == comment);
}
static void display(WINDOW *win, const char *text, int first) {
    FILE *stream = fmemopen((void *)text, strlen(text), "r");
    assert(stream); werase(win);
    draw_text_preview(win, stream, &syntax, first, 18, 80);
    fclose(stream);
}
typedef struct { size_t position, size; } Input;
static ssize_t read_input(void *data, char *out, size_t count) {
    Input *input = data;
    static const char text[] = "preview benchmark line\n";
    if (count > input->size - input->position) count = input->size - input->position;
    for (size_t i = 0; i < count; i++) out[i] = text[(input->position + i) % (sizeof(text) - 1)];
    input->position += count; return (ssize_t)count;
}
static ssize_t fail_input(void *data, char *out, size_t count) {
    (void)data; (void)out; (void)count; errno = EIO; return -1;
}
int main(int argc, char **argv) {
    assert(setlocale(LC_ALL, "C.UTF-8"));
    FILE *input = tmpfile(), *output = tmpfile(); assert(input && output);
    SCREEN *screen = newterm("xterm-256color", output, input); assert(screen);
    set_term(screen); assert(start_color() != ERR);
    for (int i = 1; i <= 20; i++) init_pair(i, COLOR_WHITE, COLOR_BLACK);
    WINDOW *window = newwin(24, 140, 0, 0); assert(window);
    if (argc > 1) {
        assert(argc == 2 && !strcmp(argv[1], "--benchmark"));
        Input data = {0, 32U * 1024U * 1024U};
        FILE *stream = fopencookie(&data, "r", (cookie_io_functions_t){.read = read_input}); assert(stream);
        struct timespec before, after; clock_gettime(CLOCK_MONOTONIC, &before);
        draw_text_preview(window, stream, &syntax, 0, 24, 100);
        clock_gettime(CLOCK_MONOTONIC, &after); fclose(stream);
        printf("{\"input_bytes\":%zu,\"bytes_read\":%zu,\"seconds\":%.9f}\n", data.size, data.position,
               after.tv_sec - before.tv_sec + (after.tv_nsec - before.tv_nsec) / 1e9);
    } else {
        assert(syntax_scan_line_state("\"/*\" // /*", &syntax, 0) == 0);
        assert(syntax_scan_line_state("\"\\\"/*\"", &syntax, 0) == 0);
        assert(syntax_scan_line_state("'/' /* open", &syntax, 0) == 1);
        assert(syntax_scan_line_state("close */ \"/*\"", &syntax, 1) == 0);
        char *prefix[] = {"\"/*\"", "// /*", "plain"};
        assert(get_initial_block_comment_state(prefix, 3, 3, &syntax) == 0);
        char long_line[1024]; memset(long_line, 'a', 500);
        strcpy(long_line + 500, " /*\ncommented\n*/\nplain\n");
        display(window, long_line, 0);
        expect(window, 8, "commented", 1); expect(window, 10, "plain", 0);
        display(window, long_line, 1); expect(window, 7, "commented", 1);
        display(window, "/* open\n*/\nplain\n", 2); expect(window, 7, "plain", 0);
        display(window, "\"/*\"\nplain\n", 1); expect(window, 7, "plain", 0);
        display(window, "first\r\nsecond\n", 0);
        expect(window, 7, "first", 0); expect(window, 8, "second", 0);
        display(window, "last line", 0); expect(window, 7, "last line", 0); expect(window, 9, "[End of file]", -1);
        display(window, "", 0); expect(window, 8, "[End of file]", -1);
        display(window, "first\n", 1000); expect(window, 8, "[End of file]", -1);
        display(window, "first\n", -1); expect(window, 7, "first", 0);
        Input data = {0, 32U * 1024U * 1024U};
        FILE *stream = fopencookie(&data, "r", (cookie_io_functions_t){.read = read_input}); assert(stream);
        werase(window); draw_text_preview(window, stream, &syntax, 0, 24, 100); fclose(stream);
        expect(window, 7, "preview benchmark line", 0);
        assert(data.position < 65536); /* A viewport must not consume the whole 32 MiB input. */
        stream = fopencookie(NULL, "r", (cookie_io_functions_t){.read = fail_input}); assert(stream);
        werase(window); draw_text_preview(window, stream, &syntax, 0, 24, 100); fclose(stream);
        expect(window, 7, "Unable to read file for preview", -1);
        puts("Text preview: physical lines, syntax state, EOF, errors, and bounded reads passed");
    }
    delwin(window); endwin(); delscreen(screen); fclose(input); fclose(output);
    return 0;
}
