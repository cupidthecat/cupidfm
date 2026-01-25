#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdarg.h>    // For va_list, va_start, va_end, vw_printw
#include <ncurses.h>   // For WINDOW, werase, wmove, vw_printw, wrefresh
#include <stdio.h>     // For vsnprintf, snprintf
#include <stdlib.h>    // For malloc, free
#include <string.h>    // For strlen, memcpy
#include <ctype.h>     // For isprint
#include <time.h>      // For clock_gettime, struct timespec, CLOCK_MONOTONIC
#include <signal.h>    // for signal, SIGWINCH

#include "globals.h"

void hold_notification_for_ms(long ms) {
    if (ms <= 0) {
        notification_hold_active = false;
        return;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long add_ns = ms * 1000000L;
    notification_hold_until = now;
    notification_hold_until.tv_sec += add_ns / 1000000000L;
    notification_hold_until.tv_nsec += add_ns % 1000000000L;
    if (notification_hold_until.tv_nsec >= 1000000000L) {
        notification_hold_until.tv_sec += 1;
        notification_hold_until.tv_nsec -= 1000000000L;
    }
    notification_hold_active = true;
}

void show_notification(WINDOW *win, const char *format, ...) {
    va_list args;
    va_start(args, format);
    werase(win);
    wmove(win, 0, 0);
    vw_printw(win, format, args);
    va_end(args);
    wrefresh(win);
    clock_gettime(CLOCK_MONOTONIC, &last_notification_time);
}

void show_popup(const char *title, const char *fmt, ...) {
    // If not initialized, do it. Usually you have initscr() done already.
    if (!stdscr) initscr();

    // Format message first so we can size the popup and avoid ncurses wrapping
    // into the border (which makes the popup look "corrupted").
    va_list args;
    va_start(args, fmt);
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) {
        va_end(args_copy);
        return;
    }

    size_t cap = (size_t)needed + 1;
    char *msg = (char *)malloc(cap);
    if (!msg) {
        va_end(args_copy);
        return;
    }
    vsnprintf(msg, cap, fmt, args_copy);
    va_end(args_copy);

    // Sanitize control characters (keep '\n' for line breaks).
    for (size_t i = 0; i < cap; i++) {
        unsigned char c = (unsigned char)msg[i];
        if (c == '\0') break;
        if (c == '\n') continue;
        if (!isprint(c)) msg[i] = '?';
    }

    // Split into lines in-place.
    int line_count = 1;
    size_t max_line_len = 0;
    size_t cur_len = 0;
    for (size_t i = 0; msg[i] != '\0'; i++) {
        if (msg[i] == '\n') {
            if (cur_len > max_line_len) max_line_len = cur_len;
            cur_len = 0;
            line_count++;
        } else {
            cur_len++;
        }
    }
    if (cur_len > max_line_len) max_line_len = cur_len;

    // Size the popup within the current terminal.
    int max_rows = LINES - 2;
    int max_cols = COLS - 2;
    if (max_rows < 6) max_rows = 6;
    if (max_cols < 20) max_cols = 20;

    int cols = (int)max_line_len + 4; // borders + padding
    if (cols < 20) cols = 20;
    if (cols > max_cols) cols = max_cols;

    // title line + blank + content + blank + footer + borders
    int max_content_rows = max_rows - 4; // top/title area and footer
    if (max_content_rows < 1) max_content_rows = 1;
    int content_rows = line_count;
    if (content_rows > max_content_rows) content_rows = max_content_rows;
    int rows = content_rows + 4;
    if (rows < 6) rows = 6;
    if (rows > max_rows) rows = max_rows;

    // Center the popup.
    int starty = (LINES - rows) / 2;
    int startx = (COLS - cols) / 2;
    if (starty < 0) starty = 0;
    if (startx < 0) startx = 0;

    WINDOW *popup = newwin(rows, cols, starty, startx);
    if (!popup) {
        free(msg);
        return;
    }
    keypad(popup, TRUE);
    werase(popup);
    box(popup, 0, 0);

    // Title (truncate to fit).
    wattron(popup, A_BOLD);
    int title_space = cols - 6; // "[ " + " ]" plus margin
    if (title_space < 0) title_space = 0;
    mvwprintw(popup, 0, 2, "[ %.*s ]", title_space, title ? title : "");
    wattroff(popup, A_BOLD);

    // Print content line-by-line with truncation to prevent wrapping.
    int y = 2;
    int printable_w = cols - 4; // left/right padding inside border
    if (printable_w < 1) printable_w = 1;

    int printed = 0;
    char *p = msg;
    while (printed < content_rows && p && *p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        mvwaddnstr(popup, y + printed, 2, p, printable_w);
        printed++;
        if (!nl) break;
        p = nl + 1;
    }
    if (line_count > content_rows && content_rows > 0) {
        // Show truncation hint on the last visible line.
        int remaining = line_count - content_rows;
        char tail[64];
        snprintf(tail, sizeof(tail), "... (%d more line%s)", remaining, remaining == 1 ? "" : "s");
        mvwaddnstr(popup, y + content_rows - 1, 2, tail, printable_w);
    }

    // Footer.
    mvwaddnstr(popup, rows - 2, 2, "Press any key to close", printable_w);

    wrefresh(popup);
    wgetch(popup);

    delwin(popup);
    free(msg);

    // Ensure the underlying UI is repainted by the caller; do a minimal refresh
    // so the popup isn't left behind on terminals without full redraw.
    touchwin(stdscr);
    refresh();
}
