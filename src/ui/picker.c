#define _GNU_SOURCE
#include "picker.h"

#include <ctype.h>
#include <limits.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "browser_ui.h"
#include "fuzzy.h"
#include "globals.h"
#include "utils.h"

typedef struct {
    size_t index;
    int score;
} PickMatch;

static int compare(const void *a, const void *b) {
    const PickMatch *left = a, *right = b;
    if (left->score != right->score)
        return (left->score > right->score) - (left->score < right->score);
    return (left->index > right->index) - (left->index < right->index);
}

int picker_choose(const char *title, const char *const *items, size_t count) {
    if (!items || !count || count > INT_MAX)
        return -1;
    PickMatch *matches = calloc(count, sizeof(*matches));
    if (!matches)
        return -1;
    WINDOW *background = dupwin(curscr);
    if (!background) {
        free(matches);
        return -1;
    }
    char query[256] = {0};
    bool finding = false, dirty = true;
    size_t selected = 0, visible = 0;
    WINDOW *window = NULL;
    int result = -1, rows = 0, columns = 0, delay = wgetdelay(stdscr);
    for (;;) {
        struct winsize terminal = {0};
        if (!ioctl(STDOUT_FILENO, TIOCGWINSZ, &terminal) && terminal.ws_row && terminal.ws_col &&
            (terminal.ws_row != LINES || terminal.ws_col != COLS)) {
            resize_term(terminal.ws_row, terminal.ws_col);
            resized = 1;
        }
        if (!window || rows != LINES || columns != COLS) {
            if (window)
                delwin(window);
            rows = LINES;
            columns = COLS;
            int height = MIN(rows, 22), width = MIN(columns, 100);
            window = newwin(height, width, (rows - height) / 2, (columns - width) / 2);
            if (!window)
                break;
            keypad(window, TRUE);
            wtimeout(window, 100);
        }
        if (dirty) {
            visible = 0;
            for (size_t i = 0; i < count; i++) {
                int score = *query ? fuzzy_score(query, items[i]) : 0;
                if (score >= 0)
                    matches[visible++] = (PickMatch){i, score};
            }
            if (*query)
                qsort(matches, visible, sizeof(*matches), compare);
            selected = 0;
            dirty = false;
        }
        int height = getmaxy(window), width = getmaxx(window);
        werase(window);
        if (height < 5 || width < 12) {
            mvwaddnstr(window, 0, 0, "Resize; Esc cancels", MAX(0, width - 1));
        } else {
            box(window, 0, 0);
            wmove(window, 0, 2);
            browser_draw_name(window, title ? title : "Choose", width - 4);
            size_t page = (size_t)(height - 3);
            size_t start = selected >= page ? selected - page + 1 : 0;
            for (size_t i = start; i < visible && i - start < page; i++) {
                if (i == selected)
                    wattron(window, A_REVERSE);
                wmove(window, (int)(i - start) + 1, 1);
                browser_draw_name(window, items[matches[i].index], width - 2);
                if (i == selected)
                    wattroff(window, A_REVERSE);
            }
            if (!visible)
                mvwaddnstr(window, 1, 2, "No matching choices", width - 4);
            char footer[384];
            if (finding || *query)
                snprintf(footer, sizeof(footer), "%s: %s", finding ? "Find (Enter done)" : "Find",
                         query);
            else
                snprintf(footer, sizeof(footer),
                         "j/k select; / fuzzy find; Enter choose; Esc cancel");
            wmove(window, height - 2, 1);
            browser_draw_name(window, footer, width - 2);
        }
        wrefresh(window);
        int key = wgetch(window);
        if (key == ERR)
            continue;
        if (key == KEY_RESIZE) {
            resized = 1;
            continue;
        }
        if (key == 3)
            break;
        if (key == 27) {
            if (finding || *query) {
                finding = false;
                query[0] = 0;
                dirty = true;
                continue;
            }
            break;
        }
        if (height < 5 || width < 12)
            continue;
        if (finding) {
            size_t length = strlen(query);
            if (key == '\n' || key == '\r' || key == KEY_ENTER)
                finding = false;
            else if (key == 21) {
                query[0] = 0;
                dirty = true;
            } else if (key == KEY_BACKSPACE || key == 127 || key == 8) {
                if (length) {
                    do {
                        length--;
                    } while (length && ((unsigned char)query[length] & 0xc0) == 0x80);
                    query[length] = 0;
                    dirty = true;
                }
            } else if (key >= 32 && key <= 255 && length + 1 < sizeof(query)) {
                query[length] = (char)key;
                query[length + 1] = 0;
                dirty = true;
            }
            continue;
        }
        if (key == '/' || key == 'f') {
            finding = true;
        } else if ((key == 'j' || key == KEY_DOWN) && selected + 1 < visible)
            selected++;
        else if ((key == 'k' || key == KEY_UP) && selected)
            selected--;
        else if (key == KEY_HOME || key == 'g')
            selected = 0;
        else if ((key == KEY_END || key == 'G') && visible)
            selected = visible - 1;
        else if (key == KEY_NPAGE && visible)
            selected = MIN(visible - 1, selected + (size_t)MAX(1, height - 3));
        else if (key == KEY_PPAGE)
            selected -= MIN(selected, (size_t)MAX(1, height - 3));
        else if (key == KEY_MOUSE) {
            MEVENT event;
            if (getmouse(&event) == OK) {
                if ((event.bstate & BUTTON4_PRESSED) && selected)
                    selected--;
                if ((event.bstate & BUTTON5_PRESSED) && selected + 1 < visible)
                    selected++;
            }
        } else if ((key == '\n' || key == '\r' || key == KEY_ENTER) && visible) {
            result = (int)matches[selected].index;
            break;
        }
    }
    if (window)
        delwin(window);
    free(matches);
    wtimeout(stdscr, delay);
    int height = MIN(getmaxy(background), getmaxy(stdscr));
    int width = MIN(getmaxx(background), getmaxx(stdscr));
    if (height > 0 && width > 0)
        copywin(background, stdscr, 0, 0, 0, 0, height - 1, width - 1, false);
    delwin(background);
    touchwin(stdscr);
    refresh();
    return result;
}
