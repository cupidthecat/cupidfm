#define _GNU_SOURCE
#include "app_inspector.h"

#include <glib.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "app_paths.h"
#include "app_session.h"
#include "app_windows.h"
#include "browser_ui.h"
#include "clipboard.h"
#include "file_info.h"
#include "main.h"
#include "search.h"
#include "ui.h"

static GPtrArray *inspection_paths(AppState *state, size_t *current) {
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    if (state->select_all_active)
        app_selection_capture(state);
    if (app_selection_count(state)) {
        Vector selected;
        if (app_selection_paths(state, &selected)) {
            for (size_t i = 0; i < Vector_len(selected); i++)
                g_ptr_array_add(paths, g_strdup(selected.el[i]));
            Vector_bye(&selected);
        }
    } else {
        Vector *view = active_files(state);
        for (size_t i = 0; i < Vector_len(*view); i++) {
            char path[MAX_PATH_LENGTH];
            resolve_path_under_cwd(path, state->current_directory, FileAttr_get_name(view->el[i]));
            if (*path)
                g_ptr_array_add(paths, g_strdup(path));
        }
    }
    char hovered[MAX_PATH_LENGTH];
    resolve_path_under_cwd(hovered, state->current_directory, state->selected_entry);
    for (size_t i = 0; i < paths->len; i++)
        if (!strcmp(hovered, paths->pdata[i]))
            *current = i;
    return paths;
}

static void directory_size(FileInfo *info, size_t field, const char *path) {
    long size = get_directory_size(path);
    char *value;
    if (size >= 0) {
        char *human = g_format_size_full((guint64)size, G_FORMAT_SIZE_IEC_UNITS);
        value = g_strdup_printf("%ld bytes (%s)", size, human);
        g_free(human);
    } else if (size == DIR_SIZE_PENDING)
        value = g_strdup("Calculating");
    else if (size == DIR_SIZE_VIRTUAL_FS)
        value = g_strdup("Virtual filesystem");
    else if (size == DIR_SIZE_TOO_LARGE)
        value = g_strdup("Size limit reached");
    else if (size == DIR_SIZE_PERMISSION_DENIED)
        value = g_strdup("Permission denied");
    else
        value = g_strdup("Unavailable");
    g_free(info->fields[field].value);
    info->fields[field].value = value;
}

void app_inspector_show(AppState *state) {
    size_t current = 0, selected = 0, start = 0;
    GPtrArray *paths = inspection_paths(state, &current);
    if (!paths->len) {
        show_notification(notifwin, "No files to inspect");
        should_clear_notif = false;
        g_ptr_array_free(paths, TRUE);
        return;
    }
    WINDOW *background = dupwin(curscr), *window = NULL;
    if (!background) {
        g_ptr_array_free(paths, TRUE);
        return;
    }
    if (previewwin)
        browser_clear_preview(previewwin);
    magic_t cookie = magic_open(MAGIC_MIME_TYPE);
    if (cookie && magic_load(cookie, NULL)) {
        magic_close(cookie);
        cookie = NULL;
    }
    FileInfo info = {0};
    size_t size_field = SIZE_MAX;
    struct timespec size_updated = {0};
    bool reload = true, copy_prefix = false;
    char notice[128] = {0};
    int rows = 0, columns = 0;
    for (;;) {
        struct winsize terminal = {0};
        if (resized && !ioctl(STDOUT_FILENO, TIOCGWINSZ, &terminal) && terminal.ws_row &&
            terminal.ws_col && (terminal.ws_row != LINES || terminal.ws_col != COLS))
            resize_term(terminal.ws_row, terminal.ws_col);
        if (!window || rows != LINES || columns != COLS) {
            if (window)
                delwin(window);
            rows = LINES;
            columns = COLS;
            int height = MAX(1, MIN(rows - 2, 22)), width = MAX(1, MIN(columns - 2, 110));
            window =
                newwin(height, width, MAX(0, (rows - height) / 2), MAX(0, (columns - width) / 2));
            if (!window)
                break;
            keypad(window, TRUE);
            wtimeout(window, 100);
        }
        if (reload) {
            file_info_read(&info, paths->pdata[current], cookie);
            size_field = SIZE_MAX;
            for (size_t i = 0; i < info.count; i++)
                if (!strcmp(info.fields[i].label, "Type") &&
                    !strcmp(info.fields[i].value, "Directory") &&
                    info.count < G_N_ELEMENTS(info.fields)) {
                    size_field = info.count++;
                    info.fields[size_field] = (FileInfoField){"Directory size", NULL};
                    directory_size(&info, size_field, paths->pdata[current]);
                    clock_gettime(CLOCK_MONOTONIC, &size_updated);
                    break;
                }
            selected = MIN(selected, info.count ? info.count - 1 : 0);
            reload = false;
        }
        if (size_field != SIZE_MAX) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            long long age = (now.tv_sec - size_updated.tv_sec) * 1000000000LL + now.tv_nsec -
                            size_updated.tv_nsec;
            if (age >= 500000000LL) {
                directory_size(&info, size_field, paths->pdata[current]);
                size_updated = now;
            }
        }
        int height = getmaxy(window), width = getmaxx(window);
        int visible = MAX(1, height - 3);
        if (selected < start)
            start = selected;
        if (selected >= start + (size_t)visible)
            start = selected - (size_t)visible + 1;
        werase(window);
        if (height >= 4 && width >= 20) {
            box(window, 0, 0);
            char title[64];
            snprintf(title, sizeof(title), " File info %zu/%u ", current + 1, paths->len);
            mvwaddnstr(window, 0, 2, title, MAX(0, width - 4));
            int label_width = MIN(14, MAX(1, (width - 4) / 3));
            for (size_t i = start; i < info.count && i - start < (size_t)visible; i++) {
                int row = (int)(i - start) + 1;
                if (i == selected)
                    wattron(window, A_REVERSE);
                mvwhline(window, row, 1, ' ', width - 2);
                wmove(window, row, 1);
                wattron(window, A_BOLD);
                browser_draw_name(window, info.fields[i].label, label_width);
                wattroff(window, A_BOLD);
                wmove(window, row, label_width + 3);
                browser_draw_name(window, info.fields[i].value, MAX(0, width - label_width - 4));
                if (i == selected)
                    wattroff(window, A_REVERSE);
            }
            mvwaddnstr(window, height - 2, 1,
                       copy_prefix ? "c: copy field; Esc closes"
                       : *notice   ? notice
                                   : "j/k fields; h/l files; cc copy; r refresh; Tab/Esc close",
                       width - 2);
        } else
            mvwaddnstr(window, 0, 0, "Resize; Esc closes", MAX(0, width - 1));
        wrefresh(window);
        int key = wgetch(window);
        if (key == ERR)
            continue;
        if (key == 27 || key == 3 || key == '\t' || key == 'q' || key == 'Q')
            break;
        if (key == KEY_RESIZE) {
            resized = 1;
            continue;
        }
        if (copy_prefix) {
            if (key == 'c' && info.count) {
                bool copied = clipboard_set_text(info.fields[selected].value);
                snprintf(notice, sizeof(notice), "%s %s", copied ? "Copied" : "Cannot copy",
                         info.fields[selected].label);
            }
            copy_prefix = false;
            continue;
        }
        notice[0] = 0;
        if (key == 'c')
            copy_prefix = true;
        else if ((key == 'j' || key == KEY_DOWN) && selected + 1 < info.count)
            selected++;
        else if ((key == 'k' || key == KEY_UP) && selected)
            selected--;
        else if (key == 'g' || key == KEY_HOME)
            selected = 0;
        else if ((key == 'G' || key == KEY_END) && info.count)
            selected = info.count - 1;
        else if (key == KEY_NPAGE && info.count)
            selected = MIN(info.count - 1, selected + (size_t)visible);
        else if (key == KEY_PPAGE)
            selected -= MIN(selected, (size_t)visible);
        else if ((key == 'l' || key == KEY_RIGHT) && current + 1 < paths->len) {
            current++;
            reload = true;
        } else if ((key == 'h' || key == KEY_LEFT) && current) {
            current--;
            reload = true;
        } else if (key == 'r')
            reload = true;
        else if (key == KEY_F(1) || key == '~')
            show_help_menu(&g_kb);
    }
    if (window)
        delwin(window);
    if (cookie)
        magic_close(cookie);
    file_info_clear(&info);
    Vector *view = active_files(state);
    for (size_t i = 0; i < Vector_len(*view); i++) {
        char path[MAX_PATH_LENGTH];
        resolve_path_under_cwd(path, state->current_directory, FileAttr_get_name(view->el[i]));
        if (!strcmp(path, paths->pdata[current])) {
            bool visual = state->visual_active;
            state->visual_active = false;
            app_move_cursor(state, (SIZE)i);
            state->visual_active = visual;
            break;
        }
    }
    g_ptr_array_free(paths, TRUE);
    int height = MIN(getmaxy(background), getmaxy(stdscr));
    int width = MIN(getmaxx(background), getmaxx(stdscr));
    if (height > 0 && width > 0)
        copywin(background, stdscr, 0, 0, 0, 0, height - 1, width - 1, false);
    delwin(background);
    touchwin(stdscr);
    refresh();
    invalidate_image_preview_overlay();
}
