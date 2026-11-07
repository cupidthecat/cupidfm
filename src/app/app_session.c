#define _GNU_SOURCE
#include "app_session.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>

#include "app_panes.h"
#include "app_search.h"
#include "git.h"
#include "search.h"
#include "utils.h"

void app_state_dispose(AppState *state) {
    app_search_stop(state, false);
    for (size_t i = 0; i < Vector_len(state->files); i++)
        free_attr(state->files.el[i]);
    Vector_set_len_no_free(&state->files, 0);
    Vector_bye(&state->files);
    Vector_set_len_no_free(&state->search_files, 0);
    Vector_bye(&state->search_files);
    app_pane_dispose(&state->parent);
    app_pane_dispose(&state->child);
    selection_destroy(state->selection);
    undo_state_clear(&state->undo_state);
    free(state->current_directory);
    free(state->lazy_load.directory_path);
    for (size_t i = 0; i < state->history_length; i++) {
        free(state->history[i]);
        free(state->history_hover[i]);
    }
    memset(state, 0, sizeof(*state));
}

void app_selection_restore(AppState *state) {
    if (!state || !state->current_directory)
        return;
    size_t revision = Vector_revision(state->files);
    if (state->selection_revision == revision &&
        !strcmp(state->selection_directory, state->current_directory))
        return;
    for (size_t i = 0; i < Vector_len(state->files); i++) {
        FileAttr file = state->files.el[i];
        char path[MAX_PATH_LENGTH];
        const char *name = FileAttr_get_name(file);
        if (strlen(state->current_directory) + strlen(name) + 2 > sizeof(path)) {
            FileAttr_select(file, false);
            continue;
        }
        path_join(path, state->current_directory, name);
        FileAttr_select(file, selection_has(state->selection, path));
    }
    state->selection_revision = revision;
    snprintf(state->selection_directory, sizeof(state->selection_directory), "%s",
             state->current_directory);
}

bool app_selection_set(AppState *state, FileAttr file, bool selected) {
    app_selection_restore(state);
    if (!file)
        return false;
    char path[MAX_PATH_LENGTH];
    const char *name = FileAttr_get_name(file);
    if (strlen(state->current_directory) + strlen(name) + 2 > sizeof(path))
        return false;
    path_join(path, state->current_directory, name);
    bool ok = selection_set(&state->selection, path, FileAttr_is_dir(file), selected);
    FileAttr_select(file, selection_has(state->selection, path));
    return ok;
}

void app_selection_capture(AppState *state) {
    for (size_t i = 0; i < Vector_len(state->files); i++) {
        FileAttr file = state->files.el[i];
        char path[MAX_PATH_LENGTH];
        const char *name = FileAttr_get_name(file);
        if (strlen(state->current_directory) + strlen(name) + 2 > sizeof(path)) {
            FileAttr_select(file, false);
            continue;
        }
        path_join(path, state->current_directory, name);
        bool marked = FileAttr_selected(file);
        if (!selection_set(&state->selection, path, FileAttr_is_dir(file), marked))
            FileAttr_select(file, selection_has(state->selection, path));
    }
}

size_t app_selection_count(AppState *state) {
    app_selection_restore(state);
    return selection_count(state->selection);
}

bool app_selection_paths(AppState *state, Vector *paths) {
    app_selection_restore(state);
    if (selection_count(state->selection))
        return selection_export(state->selection, paths);
    *paths = Vector_new(1);
    if (!paths->el)
        return false;
    if (!state->selected_entry || !*state->selected_entry)
        return true;
    char path[MAX_PATH_LENGTH];
    if (strlen(state->current_directory) + strlen(state->selected_entry) + 2 > sizeof(path)) {
        Vector_bye(paths);
        *paths = (Vector){0};
        return false;
    }
    path_join(path, state->current_directory, state->selected_entry);
    paths->el[0] = strdup(path);
    if (!paths->el[0]) {
        Vector_bye(paths);
        *paths = (Vector){0};
        return false;
    }
    Vector_set_len_no_free(paths, 1);
    return true;
}

void app_selection_clear(AppState *state) {
    selection_clear(state->selection);
    for (size_t i = 0; i < Vector_len(state->files); i++)
        FileAttr_select(state->files.el[i], false);
    state->select_all_active = false;
    g_select_all_highlight = false;
    state->mouse_drag = false;
    state->visual_active = false;
}

void app_selection_toggle_all(AppState *state, bool invert) {
    Vector *view = active_files(state);
    for (size_t i = 0; i < Vector_len(*view); i++)
        app_selection_set(state, view->el[i], !invert || !FileAttr_selected(view->el[i]));
    state->select_all_active = false;
    g_select_all_highlight = false;
    state->mouse_drag = false;
    state->visual_active = false;
}

void app_move_cursor(AppState *state, SIZE index) {
    app_selection_restore(state);
    CursorAndSlice *cas = &state->dir_window_cas;
    Vector *view = active_files(state);
    SIZE count = (SIZE)Vector_len(*view);
    index = count ? MAX(0, MIN(index, count - 1)) : 0;
    if (state->visual_active && count) {
        SIZE first = MIN(state->visual_anchor, index), last = MAX(state->visual_anchor, index);
        for (SIZE i = 0; i < count; i++) {
            bool in_range;
            if (!state->visual_wraps)
                in_range = i >= first && i <= last;
            else if (state->visual_wraps == 1 && state->visual_anchor > index + 1)
                in_range = i <= index || i >= state->visual_anchor;
            else if (state->visual_wraps == -1 && index > state->visual_anchor + 1)
                in_range = i <= state->visual_anchor || i >= index;
            else
                in_range = true;
            FileAttr_visual_apply(view->el[i], in_range, state->visual_unset);
        }
    }
    if (state->visual_active)
        app_selection_capture(state);
    cas->cursor = index;
    cas->num_files = count;
    fix_cursor(cas);
    sync_selection_from_active(state, cas);
    state->preview_start_line = 0;
}

void app_reload_view(AppState *state) {
    if (state->file_search) {
        app_search_reload(state);
        return;
    }
    char hover[MAX_PATH_LENGTH], query[MAX_PATH_LENGTH];
    snprintf(hover, sizeof(hover), "%s", state->selected_entry ? state->selected_entry : "");
    app_selection_restore(state);
    selection_prune(state->selection);
    state->visual_active = false;
    state->mouse_drag = false;
    search_before_reload(state, query);
    reload_directory(&state->files, state->current_directory);
    state->parent.loaded = state->child.loaded = false;
    state->lazy_load.files_loaded = state->lazy_load.total_files = Vector_len(state->files);
    app_selection_restore(state);
    search_after_reload(state, &state->dir_window_cas, query);
    SIZE index = find_loaded_index_by_name(active_files(state), hover);
    if (index != (SIZE)-1)
        state->dir_window_cas.cursor = index;
    app_move_cursor(state, state->dir_window_cas.cursor);
}

bool app_change_directory(AppState *state, const char *path) {
    if (!state || !path || !*path)
        return false;
    char target[MAX_PATH_LENGTH];
    size_t length = strlen(path);
    if (length >= sizeof(target) ||
        (path[0] != '/' && strlen(state->current_directory) + length + 2 > sizeof(target)))
        return false;
    if (path[0] == '/')
        snprintf(target, sizeof(target), "%s", path);
    else
        path_join(target, state->current_directory, path);
    char *canonical = realpath(target, NULL);
    if (!canonical || strlen(canonical) >= MAX_PATH_LENGTH) {
        free(canonical);
        return false;
    }
    DIR *directory = opendir(canonical);
    char *lazy = directory ? strdup(canonical) : NULL;
    if (directory)
        closedir(directory);
    if (!lazy) {
        free(canonical);
        return false;
    }
    state->visual_active = false;
    state->mouse_drag = false;
    search_clear(state);
    app_search_stop(state, false);
    free(state->current_directory);
    free(state->lazy_load.directory_path);
    state->current_directory = canonical;
    state->lazy_load.directory_path = lazy;
    state->lazy_load.last_load_time = (struct timespec){0};
    git_update_directory(canonical);
    reload_directory_lazy(&state->files, canonical, &state->lazy_load.files_loaded,
                          &state->lazy_load.total_files);
    state->dir_window_cas.cursor = state->dir_window_cas.start = 0;
    app_selection_restore(state);
    app_move_cursor(state, 0);
    return true;
}

void app_history_remember(AppState *state) {
    if (!state || !state->current_directory)
        return;
    size_t at = state->history_position;
    if (state->history_length && !strcmp(state->history[at], state->current_directory)) {
        char *hover = strdup(state->selected_entry ? state->selected_entry : "");
        if (hover) {
            free(state->history_hover[at]);
            state->history_hover[at] = hover;
        }
        return;
    }
    char *path = strdup(state->current_directory);
    char *hover = strdup(state->selected_entry ? state->selected_entry : "");
    if (!path || !hover) {
        free(path);
        free(hover);
        return;
    }
    while (state->history_length && state->history_length > at + 1) {
        size_t last = --state->history_length;
        free(state->history[last]);
        free(state->history_hover[last]);
    }
    if (state->history_length == 128) {
        free(state->history[0]);
        free(state->history_hover[0]);
        memmove(state->history, state->history + 1, 127 * sizeof(*state->history));
        memmove(state->history_hover, state->history_hover + 1,
                127 * sizeof(*state->history_hover));
        state->history_length--;
    }
    at = state->history_length++;
    state->history[at] = path;
    state->history_hover[at] = hover;
    state->history_position = at;
}

bool app_history_move(AppState *state, int direction) {
    if (!state->history_length || (direction < 0 && !state->history_position) ||
        (direction > 0 && state->history_position + 1 >= state->history_length))
        return false;
    size_t next = direction < 0 ? state->history_position - 1 : state->history_position + 1;
    if (!app_change_directory(state, state->history[next]))
        return false;
    state->history_position = next;
    SIZE index = find_loaded_index_by_name(active_files(state), state->history_hover[next]);
    if (index != (SIZE)-1)
        app_move_cursor(state, index);
    return true;
}

bool app_tab_switch(AppTabs *tabs, AppState *state, size_t index) {
    if (index >= tabs->count || index == tabs->active)
        return false;
    AppState *outgoing = malloc(sizeof(*outgoing));
    if (!outgoing)
        return false;
    state->listing = file_listing_options();
    *outgoing = *state;
    outgoing->plugins = NULL;
    PluginManager *plugins = state->plugins;
    *state = *tabs->saved[index];
    state->plugins = plugins;
    free(tabs->saved[index]);
    tabs->saved[index] = NULL;
    tabs->saved[tabs->active] = outgoing;
    tabs->active = index;
    file_listing_configure(state->listing);
    g_select_all_highlight = state->select_all_active;
    git_update_directory(state->current_directory);
    sync_selection_from_active(state, &state->dir_window_cas);
    app_reload_view(state);
    return true;
}

bool app_tab_create(AppTabs *tabs, AppState *state) {
    return app_tab_create_at(tabs, state, state->current_directory);
}

bool app_tab_create_at(AppTabs *tabs, AppState *state, const char *directory) {
    if (tabs->count >= APP_TAB_LIMIT)
        return false;
    AppState *created = calloc(1, sizeof(*created));
    if (!created)
        return false;
    created->current_directory = strdup(directory);
    created->lazy_load.directory_path = strdup(directory);
    created->files = Vector_new(16);
    created->search_files = Vector_new(16);
    if (!created->current_directory || !created->lazy_load.directory_path || !created->files.el ||
        !created->search_files.el) {
        app_state_dispose(created);
        free(created);
        return false;
    }
    created->listing = file_listing_options();
    created->chooser_file = state->chooser_file;
    created->search_mode = state->search_mode;
    created->dir_window_cas.num_lines = state->dir_window_cas.num_lines;
    undo_state_init(&created->undo_state);
    reload_directory_lazy(&created->files, created->current_directory,
                          &created->lazy_load.files_loaded, &created->lazy_load.total_files);
    sync_selection_from_active(created, &created->dir_window_cas);
    app_history_remember(created);
    size_t index = tabs->active + 1;
    for (size_t i = tabs->count; i > index; i--)
        tabs->saved[i] = tabs->saved[i - 1];
    tabs->count++;
    tabs->saved[index] = created;
    if (app_tab_switch(tabs, state, index))
        return true;
    for (size_t i = index; i + 1 < tabs->count; i++)
        tabs->saved[i] = tabs->saved[i + 1];
    tabs->saved[--tabs->count] = NULL;
    app_state_dispose(created);
    free(created);
    return false;
}

bool app_tab_close(AppTabs *tabs, AppState *state) {
    if (tabs->count <= 1)
        return false;
    size_t old = tabs->active;
    size_t next = old ? old - 1 : 1;
    PluginManager *plugins = state->plugins;
    app_state_dispose(state);
    *state = *tabs->saved[next];
    state->plugins = plugins;
    free(tabs->saved[next]);
    tabs->saved[next] = NULL;
    for (size_t i = old; i + 1 < tabs->count; i++)
        tabs->saved[i] = tabs->saved[i + 1];
    tabs->saved[--tabs->count] = NULL;
    tabs->active = next > old ? next - 1 : next;
    file_listing_configure(state->listing);
    g_select_all_highlight = state->select_all_active;
    git_update_directory(state->current_directory);
    sync_selection_from_active(state, &state->dir_window_cas);
    app_reload_view(state);
    return true;
}

void app_tabs_dispose(AppTabs *tabs) {
    for (size_t i = 0; i < tabs->count; i++) {
        if (tabs->saved[i]) {
            app_state_dispose(tabs->saved[i]);
            free(tabs->saved[i]);
        }
    }
    memset(tabs, 0, sizeof(*tabs));
}
