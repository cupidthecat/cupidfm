#define _GNU_SOURCE
#include "picker.h"

#include <assert.h>
#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

volatile sig_atomic_t resized;

static void keys(const char *text) {
    size_t length = strlen(text);
    while (length)
        assert(ungetch((unsigned char)text[--length]) != ERR);
}

int main(void) {
    assert(setlocale(LC_ALL, "C.UTF-8"));
    FILE *input = tmpfile(), *output = tmpfile();
    assert(input && output);
    SCREEN *screen = newterm("xterm-256color", output, input);
    assert(screen);
    set_term(screen);
    assert(resizeterm(30, 100) != ERR);
    const char *items[] = {"Edit selected files", "View image", "Reveal directory"};
    keys("\n");
    assert(picker_choose("Open with", items, 3) == 0);
    keys("j\n");
    assert(picker_choose("Open with", items, 3) == 1);
    keys("/rvdir\n\n");
    assert(picker_choose("Open with", items, 3) == 2);
    keys("/zz\025view\n\n");
    assert(picker_choose("Open with", items, 3) == 1);
    keys("\033");
    assert(picker_choose("Open with", items, 3) == -1);
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    puts("Picker selection, live fuzzy ranking, query editing, and cancellation passed");
}
