#ifndef APP_SESSION_H
#define APP_SESSION_H

#include "app_options.h"
#include "app_state.h"

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
// Create a tab at a checked absolute directory, with fresh history.
bool app_tab_create_at(AppTabs *tabs, AppState *state, const char *directory);
bool app_tab_switch(AppTabs *tabs, AppState *state, size_t index);
bool app_tab_close(AppTabs *tabs, AppState *state);
void app_tabs_dispose(AppTabs *tabs);
void app_move_cursor(AppState *state, SIZE index);
size_t app_selection_count(AppState *state);
void app_selection_restore(AppState *state);
bool app_selection_set(AppState *state, FileAttr file, bool selected);
void app_selection_capture(AppState *state);
bool app_selection_paths(AppState *state, Vector *paths);
void app_selection_clear(AppState *state);
void app_selection_toggle_all(AppState *state, bool invert);

#endif
