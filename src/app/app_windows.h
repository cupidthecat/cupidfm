#ifndef APP_WINDOWS_H
#define APP_WINDOWS_H

#include <ncurses.h>

#include "app_state.h"

extern WINDOW *mainwin;
extern WINDOW *dirwin;
extern WINDOW *previewwin;

void redraw_all_windows(AppState *state);

void redraw_frame_after_edit(AppState *state,
                             WINDOW *dirwin,
                             WINDOW *previewwin,
                             WINDOW *mainwin,
                             WINDOW *notifwin);

void handle_winch(int sig);

#endif // APP_WINDOWS_H
