// plugin_ui.c - Modal UI functions for plugins
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "plugins_internal.h"

#include <ctype.h>
#include <ncurses.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cs_value.h"
#include "cs_vm.h"
#include "globals.h"
#include "main.h"

void plugin_notify(const char *msg) {
    if (!msg)
        msg = "";
    if (notifwin) {
        show_notification(notifwin, "%s", msg);
        should_clear_notif = false;
    }
}

static void banner_tick_for_modal(struct timespec *last_banner_update,
                                  int total_scroll_length) {
    if (!last_banner_update)
        return;
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
    }
}

cs_value plugin_modal_prompt_text(cs_vm *vm, const char *title, const char *msg,
                                  const char *initial) {
    // Returns cs_str(...) or cs_nil() if cancelled.
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int popup_height = 7;
    int popup_width = max_x > 10 ? (max_x < 80 ? max_x - 2 : 78) : 10;
    int starty = (max_y - popup_height) / 2;
    int startx = (max_x - popup_width) / 2;
    if (starty < 0)
        starty = 0;
    if (startx < 0)
        startx = 0;

    WINDOW *popup = newwin(popup_height, popup_width, starty, startx);
    if (!popup)
        return cs_nil();
    keypad(popup, TRUE);
    box(popup, 0, 0);

    char buf[256];
    buf[0] = '\0';
    if (initial && *initial) {
        strncpy(buf, initial, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    }
    int len = (int)strlen(buf);

    // Non-blocking so banner keeps animating
    wtimeout(popup, 10);
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = (COLS - 2) +
                              (BANNER_TEXT ? (int)strlen(BANNER_TEXT) : 0) +
                              (BUILD_INFO ? (int)strlen(BUILD_INFO) : 0) +
                              BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

    bool cancelled = false;
    for (;;) {
        werase(popup);
        box(popup, 0, 0);
        mvwprintw(popup, 0, 2, "[ %.*s ]", popup_width - 6,
                  title ? title : "Prompt");

        mvwprintw(popup, 2, 2, "> %.*s", popup_width - 6, buf);
        mvwprintw(popup, 4, 2, "Enter=OK  Esc=Cancel  Backspace=Delete");
        wrefresh(popup);

        int ch = wgetch(popup);
        if (ch == ERR) {
            banner_tick_for_modal(&last_banner_update, total_scroll_length);
            napms(10);
            continue;
        }
        if (ch == 27) { // Esc
            cancelled = true;
            break;
        }
        if (ch == '\n' || ch == KEY_ENTER) {
            break;
        }
        if (ch == KEY_BACKSPACE || ch == 127) {
            if (len > 0) {
                buf[--len] = '\0';
            }
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            if (len < (int)sizeof(buf) - 1) {
                buf[len++] = (char)ch;
                buf[len] = '\0';
            }
            continue;
        }
    }

    wtimeout(popup, -1);
    werase(popup);
    wrefresh(popup);
    delwin(popup);
    touchwin(stdscr);
    refresh();

    if (cancelled)
        return cs_nil();
    return cs_str(vm, buf);
}

bool plugin_modal_confirm(const char *title, const char *msg) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int popup_height = 7;
    int popup_width = max_x > 10 ? (max_x < 90 ? max_x - 2 : 88) : 10;
    int starty = (max_y - popup_height) / 2;
    int startx = (max_x - popup_width) / 2;
    if (starty < 0)
        starty = 0;
    if (startx < 0)
        startx = 0;

    WINDOW *popup = newwin(popup_height, popup_width, starty, startx);
    if (!popup)
        return false;
    keypad(popup, TRUE);
    box(popup, 0, 0);

    wtimeout(popup, 10);
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = (COLS - 2) +
                              (BANNER_TEXT ? (int)strlen(BANNER_TEXT) : 0) +
                              (BUILD_INFO ? (int)strlen(BUILD_INFO) : 0) +
                              BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

    bool result = false;
    for (;;) {
        werase(popup);
        box(popup, 0, 0);
        mvwprintw(popup, 0, 2, "[ %.*s ]", popup_width - 6,
                  title ? title : "Confirm");

        mvwprintw(popup, 2, 2, "%.*s", popup_width - 4, msg ? msg : "");
        mvwprintw(popup, 4, 2, "Y=Yes  N/Esc=No");
        wrefresh(popup);

        int ch = wgetch(popup);
        if (ch == ERR) {
            banner_tick_for_modal(&last_banner_update, total_scroll_length);
            napms(10);
            continue;
        }
        ch = tolower(ch);
        if (ch == 'y') {
            result = true;
            break;
        }
        if (ch == 'n' || ch == 27) {
            result = false;
            break;
        }
    }

    wtimeout(popup, -1);
    werase(popup);
    wrefresh(popup);
    delwin(popup);
    touchwin(stdscr);
    refresh();
    return result;
}

int plugin_modal_menu(const char *title, char **items, size_t count) {
    if (!items || count == 0)
        return -1;

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int max_rows = max_y - 4;
    if (max_rows < 6)
        max_rows = 6;

    int visible = (int)count;
    if (visible > max_rows - 4)
        visible = max_rows - 4;
    if (visible < 1)
        visible = 1;

    size_t max_item_len = 0;
    for (size_t i = 0; i < count; i++) {
        size_t l = items[i] ? strlen(items[i]) : 0;
        if (l > max_item_len)
            max_item_len = l;
    }
    int popup_width = (int)max_item_len + 6;
    if (popup_width < 24)
        popup_width = 24;
    if (popup_width > max_x - 2)
        popup_width = max_x - 2;
    int popup_height = visible + 4;

    int starty = (max_y - popup_height) / 2;
    int startx = (max_x - popup_width) / 2;
    if (starty < 0)
        starty = 0;
    if (startx < 0)
        startx = 0;

    WINDOW *popup = newwin(popup_height, popup_width, starty, startx);
    if (!popup)
        return -1;
    keypad(popup, TRUE);
    box(popup, 0, 0);

    wtimeout(popup, 10);
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = (COLS - 2) +
                              (BANNER_TEXT ? (int)strlen(BANNER_TEXT) : 0) +
                              (BUILD_INFO ? (int)strlen(BUILD_INFO) : 0) +
                              BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

    int sel = 0;
    int start = 0;
    for (;;) {
        if (sel < 0)
            sel = 0;
        if (sel >= (int)count)
            sel = (int)count - 1;
        if (sel < start)
            start = sel;
        if (sel >= start + visible)
            start = sel - visible + 1;
        if (start < 0)
            start = 0;

        werase(popup);
        box(popup, 0, 0);
        mvwprintw(popup, 0, 2, "[ %.*s ]", popup_width - 6, title ? title : "Menu");

        int row = 2;
        for (int i = 0; i < visible; i++) {
            int idx = start + i;
            if (idx >= (int)count)
                break;
            const char *txt = items[idx] ? items[idx] : "";
            if (idx == sel)
                wattron(popup, A_REVERSE);
            mvwprintw(popup, row + i, 2, "%.*s", popup_width - 4, txt);
            if (idx == sel)
                wattroff(popup, A_REVERSE);
        }

        mvwprintw(popup, popup_height - 2, 2, "Enter=Select  Esc=Cancel");
        wrefresh(popup);

        int ch = wgetch(popup);
        if (ch == ERR) {
            banner_tick_for_modal(&last_banner_update, total_scroll_length);
            napms(10);
            continue;
        }
        if (ch == 27) {
            sel = -1;
            break;
        }
        if (ch == '\n' || ch == KEY_ENTER) {
            break;
        }
        if (ch == KEY_UP)
            sel--;
        else if (ch == KEY_DOWN)
            sel++;
        else if (ch == KEY_PPAGE)
            sel -= visible;
        else if (ch == KEY_NPAGE)
            sel += visible;
    }

    wtimeout(popup, -1);
    werase(popup);
    wrefresh(popup);
    delwin(popup);
    touchwin(stdscr);
    refresh();
    return sel;
}
