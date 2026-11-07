#ifndef APP_SESSION_H
#define APP_SESSION_H

#include "app_state.h"

#define APP_TAB_LIMIT 9

typedef struct {
    AppState *saved[APP_TAB_LIMIT];
    size_t count;
    size_t active;
} AppTabs;

void app_state_dispose(AppState *state);
void app_reload_view(AppState *state);
bool app_change_directory(AppState *state, const char *path);
void app_history_remember(AppState *state);
bool app_history_move(AppState *state, int direction);
bool app_tab_create(AppTabs *tabs, AppState *state);
bool app_tab_switch(AppTabs *tabs, AppState *state, size_t index);
bool app_tab_close(AppTabs *tabs, AppState *state);
void app_tabs_dispose(AppTabs *tabs);
void app_move_cursor(AppState *state, SIZE index);
size_t app_selection_count(AppState *state);
void app_selection_clear(AppState *state);
void app_selection_toggle_all(AppState *state, bool invert);

#endif
