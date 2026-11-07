#ifndef APP_WINDOWS_H
#define APP_WINDOWS_H

#include <ncurses.h>

#include "app_state.h"

extern WINDOW *mainwin;
extern WINDOW *dirwin;
extern WINDOW *previewwin;
extern WINDOW *parentwin;
extern WINDOW *statuswin;

void app_windows_create(void);
void app_windows_destroy(void);
void app_draw_browser(AppState *state);

void redraw_all_windows(AppState *state);

void redraw_frame_after_edit(AppState *state, WINDOW *dirwin, WINDOW *previewwin, WINDOW *mainwin,
                             WINDOW *notifwin);

void handle_winch(int sig);

#endif // APP_WINDOWS_H
