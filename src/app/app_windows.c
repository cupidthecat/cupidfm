#define _GNU_SOURCE
#include "app_windows.h"

#include <pthread.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_layout.h"
#include "app_panes.h"
#include "app_session.h"
#include "globals.h"
#include "main.h"
#include "search.h"
#include "tasks.h"

WINDOW *parentwin;
WINDOW *statuswin;
static AppLayout layout;

static WINDOW *make_window(AppRect rect) {
    if (rect.rows <= 0 || rect.columns <= 0)
        return NULL;
    return newwin(rect.rows, rect.columns, rect.y, rect.x);
}

static void delete_window(WINDOW **window) {
    if (*window)
        delwin(*window);
    *window = NULL;
}

void app_windows_destroy(void) {
    delete_window(&dirwin);
    delete_window(&previewwin);
    delete_window(&parentwin);
    delete_window(&statuswin);
    delete_window(&mainwin);
    delete_window(&bannerwin);
    delete_window(&notifwin);
}

void app_windows_create(void) {
    app_windows_destroy();
    layout = app_layout_compute(LINES, COLS, &g_kb);
    bannerwin = make_window(layout.header);
    mainwin = make_window(layout.body);
    dirwin = make_window(layout.current);
    parentwin = make_window(layout.parent);
    previewwin = make_window(layout.preview);
    statuswin = make_window(layout.status);
    notifwin = make_window(layout.notice);
    /* Hidden previews retain a valid handle for older editor/plugin entrypoints.
     * The frame renderer never draws the placeholder. */
    if (!previewwin)
        previewwin = newwin(1, 1, 0, 0);
    if (!bannerwin || !mainwin || !dirwin || !previewwin || !notifwin) {
        endwin();
        fputs("CupidFM: unable to allocate terminal windows\n", stderr);
        exit(1);
    }
    wtimeout(mainwin, INPUT_CHECK_INTERVAL);
    werase(mainwin);
    if (!g_kb.vim_keymap && !layout.compact)
        box(mainwin, 0, 0);
    wrefresh(mainwin);
}

static void draw_status(AppState *state) {
    if (!statuswin)
        return;
    TaskInfo jobs[128];
    size_t total = tasks_snapshot(jobs, 128), running = 0;
    for (size_t i = 0; i < total; i++)
        running += jobs[i].status == TASK_QUEUED || jobs[i].status == TASK_RUNNING;
    Vector *view = active_files(state);
    size_t count = Vector_len(*view), index = count ? (size_t)state->dir_window_cas.cursor + 1 : 0;
    size_t selected = app_selection_count(state);
    char text[256];
    snprintf(text, sizeof(text), " %s  %zu selected  %zu/%zu  %zu%%  tasks %zu",
             state->visual_active ? (state->visual_unset ? "UNSET" : "SELECT") : "NORMAL", selected,
             index, count, count ? index * 100 / count : 0, running);
    werase(statuswin);
    wattron(statuswin, A_REVERSE);
    whline(statuswin, ' ', getmaxx(statuswin));
    mvwaddnstr(statuswin, 0, 0, text, getmaxx(statuswin));
    wattroff(statuswin, A_REVERSE);
    wrefresh(statuswin);
}

void app_draw_browser(AppState *state) {
    if (layout.compact) {
        werase(mainwin);
        mvwaddnstr(mainwin, 0, 0, "Resize terminal", getmaxx(mainwin));
        wrefresh(mainwin);
        return;
    }
    if (g_kb.vim_keymap) {
        werase(bannerwin);
        wmove(bannerwin, 0, 0);
        browser_draw_name(bannerwin, state->current_directory, getmaxx(bannerwin));
        wrefresh(bannerwin);
    }
    if (parentwin)
        app_parent_draw(state, parentwin);
    draw_directory_window(dirwin, state->current_directory, active_files(state),
                          &state->dir_window_cas);
    if (layout.preview.columns > 0) {
        if (g_kb.vim_keymap && app_child_draw(state, previewwin)) {
            /* Directory previews share sorting and selection with the browser. */
        } else if (state->preview_override_active)
            draw_preview_window_path(previewwin, state->preview_override_path, NULL,
                                     state->preview_start_line);
        else
            draw_preview_window(previewwin, state->current_directory, state->selected_entry,
                                state->preview_start_line);
    }
    draw_status(state);
}

void redraw_all_windows(AppState *state) {
    invalidate_image_preview_overlay();
    struct winsize size = {0};
    if (!ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) && size.ws_row && size.ws_col)
        resize_term(size.ws_row, size.ws_col);
    endwin();
    refresh();
    clear();
    app_windows_create();
    state->mouse_drag = false;
    state->dir_window_cas.num_lines = getmaxy(dirwin) + (g_kb.vim_keymap ? 2 : 0);
    sync_selection_from_active(state, &state->dir_window_cas);
    if (!g_kb.vim_keymap && BANNER_TEXT && BUILD_INFO && !layout.compact) {
        pthread_mutex_lock(&banner_mutex);
        draw_scrolling_banner(bannerwin, BANNER_TEXT, BUILD_INFO, banner_offset);
        pthread_mutex_unlock(&banner_mutex);
    }
    app_draw_browser(state);
    wrefresh(notifwin);
}

void redraw_frame_after_edit(AppState *state, WINDOW *directory, WINDOW *preview,
                             WINDOW *main_window, WINDOW *notice) {
    (void)directory;
    (void)preview;
    (void)main_window;
    (void)notice;
    invalidate_image_preview_overlay();
    if (state) {
        touchwin(mainwin);
        wrefresh(mainwin);
        app_draw_browser(state);
        wrefresh(notifwin);
    }
}

void handle_winch(int signal_number) {
    (void)signal_number;
    resized = 1;
}
