#define _POSIX_C_SOURCE 200809L
#include "app_input.h"
#include <assert.h>
#include <ncurses.h>
#include <stdio.h>

int main(void) {
    FILE *input = tmpfile(), *output = tmpfile();
    assert(input && output);
    SCREEN *screen = newterm("xterm", output, input);
    assert(screen);
    set_term(screen);
    wtimeout(stdscr, 0);
    struct timespec recent, old;
    clock_gettime(CLOCK_MONOTONIC, &recent);
    old = recent;
    old.tv_sec--;
    assert(ungetch(KEY_DOWN) != ERR);
    assert(app_input_defer_scroll_frame(KEY_DOWN, KEY_UP, KEY_DOWN, &recent));
    assert(app_input_next_key() == KEY_DOWN);
    assert(ungetch('x') != ERR);
    assert(!app_input_defer_scroll_frame(KEY_DOWN, KEY_UP, KEY_DOWN, &recent));
    assert(app_input_next_key() == 'x');
    assert(ungetch(KEY_DOWN) != ERR);
    assert(!app_input_defer_scroll_frame(KEY_DOWN, KEY_UP, KEY_DOWN, &old));
    assert(app_input_next_key() == KEY_DOWN);
    assert(ungetch(KEY_F(1)) != ERR);
    for (int i = 0; i < 100; i++)
        assert(ungetch(KEY_DOWN) != ERR);
    int count = 0, key;
    while ((key = app_input_next_key()) != KEY_F(1)) {
        assert(key == KEY_DOWN);
        count++;
        app_input_defer_scroll_frame(key, KEY_UP, KEY_DOWN, &recent);
    }
    assert(count == 100);
    assert(app_input_next_key() == ERR);
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    puts("Movement bursts preserve all keys, pending commands, and quit requests");
    return 0;
}
