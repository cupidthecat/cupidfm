#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "app_plugins.h"

#include "plugins.h"
#include "search.h"

void plugins_update_context(AppState *state, int active_window) {
    if (!state || !state->plugins) return;
    Vector *view = active_files(state);
    PluginsContext ctx = {
        .cwd = state->current_directory,
        .selected_name = state->selected_entry,
        .cursor_index = (int)state->dir_window_cas.cursor,
        .list_count = (int)Vector_len(*view),
        .select_all_active = state->select_all_active,
        .search_active = state->search_active,
        .search_query = state->search_query,
        .active_pane = active_window,
        .view = view,
    };
    plugins_set_context_ex(state->plugins, &ctx);
}
