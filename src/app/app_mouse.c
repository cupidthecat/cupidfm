#include "app_mouse.h"
#include "app_windows.h"

#include "app_session.h"
#include "files.h"
#include "main.h"
#include "search.h"

static bool content_position(WINDOW *window, const MEVENT *event, int *row) {
    if (!window || !wenclose(window, event->y, event->x))
        return false;
    int y = event->y, x = event->x;
    if (!wmouse_trafo(window, &y, &x, FALSE) || y <= 0 || x <= 0 || y >= getmaxy(window) - 1 ||
        x >= getmaxx(window) - 1)
        return false;
    *row = y - 1;
    return true;
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
    if (content_position(previewwin, event, &row) && direction)
        return direction < 0 ? APP_MOUSE_PREVIEW_UP : APP_MOUSE_PREVIEW_DOWN;
    bool release = (event->bstate & BUTTON1_RELEASED) != 0;
    if (!content_position(dirwin, event, &row)) {
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
