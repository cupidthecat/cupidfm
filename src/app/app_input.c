#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "app_input.h"

#include <ncurses.h>
#include <stdio.h>

static int pending_key = ERR;

int app_input_next_key(void) {
    if (pending_key != ERR) {
        int key = pending_key;
        pending_key = ERR;
        return key;
    }
    return getch();
}

bool app_input_defer_scroll_frame(int key, int up, int down,
                                  const struct timespec *last_frame) {
    if (!last_frame || (key != up && key != down)) return false;
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return false;
    long long elapsed = (long long)(now.tv_sec - last_frame->tv_sec) * 1000000000LL +
                        now.tv_nsec - last_frame->tv_nsec;
    if (elapsed < 0 || elapsed >= 16000000LL) return false;
    if (pending_key == ERR) {
        int delay = wgetdelay(stdscr);
        wtimeout(stdscr, 0);
        pending_key = getch();
        wtimeout(stdscr, delay);
    }
    return pending_key != ERR && (pending_key == up || pending_key == down);
}

const char *keycode_to_string(int keycode) {
    static char buf[32];

    const int FUNCTION_KEY_BASE = KEY_F(1) - 1;

    if (keycode >= KEY_F(1) && keycode <= KEY_F(63)) {
        int func_num = keycode - FUNCTION_KEY_BASE;
        snprintf(buf, sizeof(buf), "F%d", func_num);
        return buf;
    }

    if (keycode >= 1 && keycode <= 26) {
        char c = 'A' + (keycode - 1);
        snprintf(buf, sizeof(buf), "^%c", c);
        return buf;
    }

    switch (keycode) {
        case KEY_UP: return "Up";
        case KEY_DOWN: return "Down";
        case KEY_LEFT: return "Left";
        case KEY_RIGHT: return "Right";
        case '\t': return "Tab";
        case KEY_BACKSPACE: return "Backspace";
        case 127: return "Backspace";
        case KEY_DC: return "Delete";
        case KEY_HOME: return "Home";
        case KEY_END: return "End";
        case KEY_PPAGE: return "PgUp";
        case KEY_NPAGE: return "PgDn";
        case KEY_ENTER: return "Enter";
        case '\n': return "Enter";
        case '\r': return "Enter";
        case 27: return "Esc";
        default:
            // Handle uppercase letters (A-Z) to show as Shift+<letter>
            if (keycode >= 'A' && keycode <= 'Z') {
                snprintf(buf, sizeof(buf), "Shift+%c", (char)keycode);
                return buf;
            }
            if (keycode >= 32 && keycode <= 126) {
                snprintf(buf, sizeof(buf), "%c", keycode);
                return buf;
            }
            snprintf(buf, sizeof(buf), "Key_%d", keycode);
            return buf;
    }
}
