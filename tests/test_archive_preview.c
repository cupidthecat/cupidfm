#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#include "../src/fs/files.c"
#include <assert.h>
#include <locale.h>

int main(int argc, char **argv) {
    assert(argc >= 4 && argc <= 6);
    assert(setlocale(LC_ALL, "C.UTF-8"));
    FILE *input = tmpfile(), *output = tmpfile();
    assert(input && output);
    SCREEN *screen = newterm("xterm", output, input);
    assert(screen);
    set_term(screen);
    assert(resizeterm(30, 120) != ERR);
    WINDOW *window = newwin(30, 120, 0, 0);
    assert(window);
    display_archive_preview(window, argv[1], argc >= 5 ? atoi(argv[4]) : 0, 30, 120);
    char text[16384] = {0};
    size_t used = 0;
    for (int row = 0; row < 30; row++) {
        int count = mvwinnstr(window, row, 0, text + used, 500);
        assert(count >= 0);
        used += (size_t)count;
        text[used++] = '\n';
        text[used] = 0;
    }
    int ok = strstr(text, argv[2]) && strstr(text, argv[3]) && (argc < 6 || !strstr(text, argv[5]));
    if (!ok)
        fprintf(stderr, "Preview missing [%s] or [%s]:\n%s", argv[2], argv[3], text);
    delwin(window);
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    return ok ? 0 : 1;
}
