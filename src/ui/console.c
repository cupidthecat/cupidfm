// console.c - in-app console log (popup) for CupidFM

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "console.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <ncurses.h>
#include <pthread.h>
#include <stdbool.h>

#include "globals.h"
#include "main.h" // draw_scrolling_banner + banner globals

#ifndef CONSOLE_MAX_LINES
#define CONSOLE_MAX_LINES 500
#endif

static char *g_console_lines[CONSOLE_MAX_LINES];
static size_t g_console_start = 0; // index of oldest
static size_t g_console_count = 0; // number of valid lines

static void console_push_owned(char *line) {
    if (!line) return;
    if (g_console_count == CONSOLE_MAX_LINES) {
        // Drop oldest.
        free(g_console_lines[g_console_start]);
        g_console_lines[g_console_start] = NULL;
        g_console_start = (g_console_start + 1) % CONSOLE_MAX_LINES;
        g_console_count--;
    }
    size_t idx = (g_console_start + g_console_count) % CONSOLE_MAX_LINES;
    g_console_lines[idx] = line;
    g_console_count++;
}

void console_logf(const char *fmt, ...) {
    if (!fmt) return;
    va_list args;
    va_start(args, fmt);
    va_list args2;
    va_copy(args2, args);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) {
        va_end(args2);
        return;
    }
    size_t cap = (size_t)needed + 1;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        va_end(args2);
        return;
    }
    vsnprintf(buf, cap, fmt, args2);
    va_end(args2);
    console_push_owned(buf);
}

void console_clear(void) {
    for (size_t i = 0; i < g_console_count; i++) {
        size_t idx = (g_console_start + i) % CONSOLE_MAX_LINES;
        free(g_console_lines[idx]);
        g_console_lines[idx] = NULL;
    }
    g_console_start = 0;
    g_console_count = 0;
}

// Keep the banner animating while modal UIs are open. Since ncurses windows can
// overlap, we return whether we redrew the banner so the caller can re-touch
// and refresh the popup window to prevent flicker/overdraw.
static bool banner_tick(struct timespec *last_banner_update, int total_scroll_length) {
    if (!last_banner_update) return false;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long banner_time_diff = (now.tv_sec - last_banner_update->tv_sec) * 1000000 +
                            (now.tv_nsec - last_banner_update->tv_nsec) / 1000;
    if (banner_time_diff >= BANNER_SCROLL_INTERVAL && BANNER_TEXT && bannerwin) {
        pthread_mutex_lock(&banner_mutex);
        draw_scrolling_banner(bannerwin, BANNER_TEXT, BUILD_INFO, banner_offset);
        pthread_mutex_unlock(&banner_mutex);
        banner_offset = (banner_offset + 1) % total_scroll_length;
        *last_banner_update = now;
        return true;
    }
    return false;
}

void console_show(void) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    // Avoid overlapping the banner/notification bars; if we overlap, the banner
    // redraw can briefly overdraw the popup border/title on some terminals.
    const int banner_height = 3;
    const int notif_height = 1;
    int usable_y = max_y - banner_height - notif_height;
    if (usable_y < 8) usable_y = max_y; // fallback for tiny terminals

    int popup_height = usable_y - 2;
    int popup_width = max_x - 6;
    if (popup_height < 8) popup_height = 8;
    if (popup_width < 30) popup_width = 30;
    if (popup_height > max_y) popup_height = max_y;
    if (popup_width > max_x) popup_width = max_x;

    int starty;
    if (usable_y == max_y) {
        starty = (max_y - popup_height) / 2;
    } else {
        starty = banner_height + (usable_y - popup_height) / 2;
    }
    int startx = (max_x - popup_width) / 2;
    if (starty < 0) starty = 0;
    if (startx < 0) startx = 0;

    WINDOW *win = newwin(popup_height, popup_width, starty, startx);
    if (!win) return;
    keypad(win, TRUE);
    box(win, 0, 0);

    int content_h = popup_height - 4; // title + footer + borders
    if (content_h < 1) content_h = 1;
    int content_w = popup_width - 4;
    if (content_w < 1) content_w = 1;

    int scroll = 0;
    int total = (int)g_console_count;
    int max_scroll = total - content_h;
    if (max_scroll < 0) max_scroll = 0;

    // Show last lines by default.
    scroll = max_scroll;

    wtimeout(win, 10);
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = (COLS - 2) + (BANNER_TEXT ? (int)strlen(BANNER_TEXT) : 0) +
                              (BUILD_INFO ? (int)strlen(BUILD_INFO) : 0) +
                              BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

    for (;;) {
        total = (int)g_console_count;
        max_scroll = total - content_h;
        if (max_scroll < 0) max_scroll = 0;
        if (scroll < 0) scroll = 0;
        if (scroll > max_scroll) scroll = max_scroll;

        werase(win);
        box(win, 0, 0);
        mvwprintw(win, 0, 2, "[ Console ]");
        mvwprintw(win, popup_height - 2, 2, "Up/Down/PgUp/PgDn scroll | c=clear | Esc/q=close");

        for (int row = 0; row < content_h; row++) {
            int line_idx = scroll + row;
            if (line_idx >= total) break;
            size_t idx = (g_console_start + (size_t)line_idx) % CONSOLE_MAX_LINES;
            const char *line = g_console_lines[idx] ? g_console_lines[idx] : "";
            mvwprintw(win, 2 + row, 2, "%.*s", content_w, line);
        }

        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) {
            if (banner_tick(&last_banner_update, total_scroll_length)) {
                // Banner redraw can overwrite parts of an overlapping popup.
                touchwin(win);
                wrefresh(win);
            }
            napms(10);
            continue;
        }
        if (ch == 27 || ch == 'q' || ch == 'Q') break;
        if (ch == 'c' || ch == 'C') {
            console_clear();
            scroll = 0;
            continue;
        }
        if (ch == KEY_UP) scroll--;
        else if (ch == KEY_DOWN) scroll++;
        else if (ch == KEY_PPAGE) scroll -= content_h;
        else if (ch == KEY_NPAGE) scroll += content_h;
        else if (ch == KEY_HOME) scroll = 0;
        else if (ch == KEY_END) scroll = max_scroll;
    }

    wtimeout(win, -1);
    werase(win);
    wrefresh(win);
    delwin(win);
    touchwin(stdscr);
    refresh();
}
