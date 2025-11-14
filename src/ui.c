#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L
#include <stdarg.h>    // For va_list, va_start, va_end, vw_printw
#include <ncurses.h>   // For WINDOW, werase, wmove, vw_printw, wrefresh
#include <time.h>      // For clock_gettime, struct timespec, CLOCK_MONOTONIC
#include <signal.h>    // for signal, SIGWINCH

#include "globals.h"

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
    // We use a small window in the center of the screen
    int rows = 10;
    int cols = 60;

    // If not initialized, do it. Usually you have initscr() done already.
    if (!stdscr) initscr();

    // Center the popup
    int starty = (LINES - rows) / 2;
    int startx = (COLS - cols) / 2;

    // Create the window
    WINDOW *popup = newwin(rows, cols, starty, startx);
    box(popup, 0, 0);

    // Print a title in bold near the top
    wattron(popup, A_BOLD);
    mvwprintw(popup, 0, 2, "[ %s ]", title);
    wattroff(popup, A_BOLD);

    // Use varargs to display your custom message
    va_list args;
    va_start(args, fmt);
    // Start printing text a few rows down so it’s readable
    wmove(popup, 2, 2);
    vw_printw(popup, fmt, args);
    va_end(args);

    // Refresh so the user sees it
    wrefresh(popup);

    // Wait for one key press
    wgetch(popup);

    // Cleanup
    delwin(popup);
}