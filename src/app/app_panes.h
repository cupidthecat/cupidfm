#ifndef APP_PANES_H
#define APP_PANES_H

#include "app_state.h"

void app_panes_stop(void);
void app_pane_dispose(AppDirectoryPane *pane);
void app_parent_refresh(AppState *state);
void app_parent_draw(AppState *state, WINDOW *window);
bool app_child_refresh(AppState *state);
bool app_child_draw(AppState *state, WINDOW *window);

#endif
