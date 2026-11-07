#include "app_mouse.h"
#include "app_windows.h"

#include "app_session.h"
#include "files.h"
#include "main.h"
#include "search.h"
#include <stdio.h>

static bool content_position(WINDOW *window, const MEVENT *event, int *row, int padding) {
    if (!window || !wenclose(window, event->y, event->x))
        return false;
    int y = event->y, x = event->x;
    if (!wmouse_trafo(window, &y, &x, FALSE) || y < padding || x < padding ||
        y >= getmaxy(window) - padding || x >= getmaxx(window) - padding)
        return false;
    *row = y - padding;
    return true;
}

static AppMouseResult pane_click(AppState *state, AppDirectoryPane *pane, const MEVENT *event,
                                 int row) {
    bool click = event->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED | BUTTON1_DOUBLE_CLICKED |
                                  BUTTON3_CLICKED | BUTTON3_PRESSED);
    if (!click)
        return APP_MOUSE_HANDLED;
    SIZE index = pane->cas.start + row;
    char name[MAX_PATH_LENGTH] = {0}, directory[MAX_PATH_LENGTH];
    if (index >= 0 && (size_t)index < Vector_len(pane->files))
        snprintf(name, sizeof(name), "%s", FileAttr_get_name(pane->files.el[index]));
    else if (!(event->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)))
        return APP_MOUSE_HANDLED;
    snprintf(directory, sizeof(directory), "%s", pane->directory);
    if (!app_change_directory(state, directory))
        return APP_MOUSE_HANDLED;
    SIZE at = find_loaded_index_by_name(active_files(state), name);
    if (at == (SIZE)-1)
        return APP_MOUSE_HANDLED;
    app_move_cursor(state, at);
    return event->bstate & (BUTTON1_DOUBLE_CLICKED | BUTTON3_CLICKED | BUTTON3_PRESSED)
               ? APP_MOUSE_ENTER
               : APP_MOUSE_HANDLED;
}

AppMouseResult app_mouse_event(AppState *state, const MEVENT *event) {
    if (!state || !event)
        return APP_MOUSE_NONE;
    int direction = 0;
    if (event->bstate & BUTTON4_PRESSED)
        direction = -1;
#ifdef BUTTON5_PRESSED
    else if (event->bstate & BUTTON5_PRESSED)
        direction = 1;
#endif
    int row;
    int padding = g_kb.vim_keymap ? 0 : 1;
    int preview_padding = g_kb.vim_keymap && state->child.loaded ? 0 : 1;
    if (content_position(previewwin, event, &row, preview_padding)) {
        if (direction)
            return direction < 0 ? APP_MOUSE_PREVIEW_UP : APP_MOUSE_PREVIEW_DOWN;
        if (g_kb.vim_keymap && state->child.loaded)
            return pane_click(state, &state->child, event, row);
    }
    if (content_position(parentwin, event, &row, padding) && state->parent.loaded) {
        if (direction)
            return APP_MOUSE_HANDLED;
        return pane_click(state, &state->parent, event, row);
    }
    bool release = (event->bstate & BUTTON1_RELEASED) != 0;
    if (!content_position(dirwin, event, &row, padding)) {
        if (release) {
            state->mouse_drag = false;
            state->visual_active = false;
        }
        return APP_MOUSE_NONE;
    }
    if (direction) {
        app_move_cursor(state, state->dir_window_cas.cursor + direction * 3);
        return APP_MOUSE_HANDLED;
    }
    Vector *view = active_files(state);
    SIZE index = state->dir_window_cas.start + row;
    if (index < 0 || (size_t)index >= Vector_len(*view)) {
        if (release) {
            state->mouse_drag = false;
            state->visual_active = false;
        }
        return APP_MOUSE_HANDLED;
    }
    if (event->bstate & (BUTTON1_DOUBLE_CLICKED | BUTTON3_CLICKED | BUTTON3_PRESSED)) {
        state->mouse_drag = false;
        state->visual_active = false;
        app_move_cursor(state, index);
        return APP_MOUSE_ENTER;
    }
    if (state->mouse_drag && (event->bstate & REPORT_MOUSE_POSITION)) {
        state->visual_active = true;
        app_move_cursor(state, index);
        return APP_MOUSE_HANDLED;
    }
    if (event->bstate & (BUTTON1_PRESSED | BUTTON1_CLICKED)) {
        state->visual_active = false;
        app_move_cursor(state, index);
        for (size_t i = 0; i < Vector_len(*view); i++)
            FileAttr_visual_begin(view->el[i]);
        state->visual_anchor = index;
        state->visual_unset = false;
        state->visual_wraps = 0;
        state->mouse_drag = (event->bstate & BUTTON1_PRESSED) != 0;
        return APP_MOUSE_HANDLED;
    }
    if (release && !state->mouse_drag) {
        app_move_cursor(state, index);
        return APP_MOUSE_HANDLED;
    }
    if (release && state->mouse_drag) {
        if (index != state->visual_anchor) {
            state->visual_active = true;
            app_move_cursor(state, index);
        }
        state->mouse_drag = false;
        state->visual_active = false;
        return APP_MOUSE_HANDLED;
    }
    return APP_MOUSE_NONE;
}
