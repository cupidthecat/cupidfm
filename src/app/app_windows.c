#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "app_windows.h"

#include <pthread.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "browser_ui.h"
#include "globals.h"
#include "main.h"
#include "search.h"

void redraw_all_windows(AppState *state) {
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    resize_term(w.ws_row, w.ws_col);

    endwin();
    refresh();
    clear();

    int new_cols = MAX(COLS, 40);
    int new_lines = MAX(LINES, 10);
    int banner_height = 3;
    int notif_height = 1;
    int main_height = new_lines - banner_height - notif_height;

    SIZE dir_win_width = MAX(new_cols / 3, 20);
    SIZE preview_win_width = new_cols - dir_win_width - 2;

    if (dirwin) delwin(dirwin);
    if (previewwin) delwin(previewwin);
    if (mainwin) delwin(mainwin);
    if (bannerwin) delwin(bannerwin);
    if (notifwin) delwin(notifwin);

    bannerwin = newwin(banner_height, new_cols, 0, 0);
    box(bannerwin, 0, 0);
    if (BANNER_TEXT && BUILD_INFO) {
        pthread_mutex_lock(&banner_mutex);
        draw_scrolling_banner(bannerwin, BANNER_TEXT, BUILD_INFO, banner_offset);
        pthread_mutex_unlock(&banner_mutex);
    }

    mainwin = newwin(main_height, new_cols, banner_height, 0);
    box(mainwin, 0, 0);

    int inner_height = main_height - 2;
    int inner_start_y = 1;
    int dir_start_x = 1;
    int preview_start_x = dir_win_width + 1;

    dirwin = derwin(mainwin, inner_height, dir_win_width - 1, inner_start_y, dir_start_x);
    previewwin = derwin(mainwin, inner_height, preview_win_width, inner_start_y, preview_start_x);

    notifwin = newwin(notif_height, new_cols, new_lines - notif_height, 0);
    box(notifwin, 0, 0);

    state->dir_window_cas.num_lines = inner_height;
    sync_selection_from_active(state, &state->dir_window_cas);

    box(dirwin, 0, 0);
    box(previewwin, 0, 0);

    draw_directory_window(
        dirwin,
        state->current_directory,
        active_files(state),
        &state->dir_window_cas
    );

    draw_preview_window(
        previewwin,
        state->current_directory,
        state->selected_entry,
        state->preview_start_line
    );

    refresh();
    wrefresh(bannerwin);
    wrefresh(mainwin);
    wrefresh(dirwin);
    wrefresh(previewwin);
    wrefresh(notifwin);
}

void redraw_frame_after_edit(AppState *state,
                             WINDOW *dirwin,
                             WINDOW *previewwin,
                             WINDOW *mainwin,
                             WINDOW *notifwin) {
    if (!state) return;
    if (bannerwin) {
        box(bannerwin, 0, 0);
        wrefresh(bannerwin);
    }
    if (mainwin) {
        box(mainwin, 0, 0);
        wrefresh(mainwin);
    }
    if (dirwin) {
        draw_directory_window(dirwin, state->current_directory, active_files(state), &state->dir_window_cas);
    }
    if (previewwin) {
        draw_preview_window(previewwin, state->current_directory, state->selected_entry, state->preview_start_line);
    }
    if (notifwin) {
        box(notifwin, 0, 0);
        wrefresh(notifwin);
    }
}

void handle_winch(int sig) {
    (void)sig;
    resized = 1;
}
