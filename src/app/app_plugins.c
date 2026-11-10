#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "app_plugins.h"

#include "app_session.h"
#include "plugins.h"
#include "search.h"

static PluginsContext browser_context(AppState *state, int active_window) {
    // A plugin may have requested a reload that replaced the file attributes.
    app_selection_restore(state);
    sync_selection_from_active(state, &state->dir_window_cas);
    Vector *view = active_files(state);
    return (PluginsContext){
        .cwd = state->current_directory,
        .selected_name = state->selected_entry,
        .cursor_index = (int)state->dir_window_cas.cursor,
        .list_count = (int)Vector_len(*view),
        .select_all_active = state->select_all_active,
        .search_active = state->search_active,
        .search_query = state->search_query,
        .active_pane = active_window,
        .view = view,
        .selection_model = state->selection,
    };
}

void plugins_start(AppState *state, int active_window) {
    if (!state || state->plugins)
        return;
    PluginsContext context = browser_context(state, active_window);
    state->plugins = plugins_create(&context);
}

void plugins_update_context(AppState *state, int active_window) {
    if (!state || !state->plugins)
        return;
    PluginsContext context = browser_context(state, active_window);
    plugins_set_context_ex(state->plugins, &context);
}
