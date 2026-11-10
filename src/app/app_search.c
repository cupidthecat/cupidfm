#define _GNU_SOURCE
#include "app_search.h"

#include <glib.h>
#include <string.h>
#include <sys/stat.h>

#include "app_session.h"
#include "file_search.h"
#include "input_prompt.h"
#include "search.h"
#include "ui.h"

static void empty_results(AppState *state) {
    Vector_set_len_no_free(&state->search_files, 0);
    for (size_t i = 0; i < Vector_len(state->files); i++)
        free_attr(state->files.el[i]);
    Vector_set_len_no_free(&state->files, 0);
    state->lazy_load.files_loaded = state->lazy_load.total_files = 0;
    state->lazy_load.is_loading = false;
    state->visual_active = state->mouse_drag = false;
    app_move_cursor(state, 0);
    state->search_revision = Vector_revision(state->files);
}

static bool start(AppState *state, const char *query, bool content) {
    GError *error = NULL;
    FileSearch *search = file_search_start(state->current_directory, query, content,
                                           file_listing_options().show_hidden, &error);
    if (!search) {
        show_notification(notifwin, "Search: %s", error ? error->message : "Cannot start");
        should_clear_notif = false;
        g_clear_error(&error);
        return false;
    }
    file_search_free(state->file_search);
    state->file_search = search;
    state->search_reported = false;
    empty_results(state);
    return true;
}

void app_search_prompt(AppState *state, bool content) {
    char query[MAX_PATH_LENGTH] = {0};
    if (state->file_search)
        snprintf(query, sizeof(query), "%s", file_search_query(state->file_search));
    if (!input_prompt_ex(
            content ? "Search contents (regex)" : "Search names (regex)", query, sizeof(query),
            &(InputPromptOptions){.history = content ? "content search" : "name search"}))
        return;
    char hover[MAX_PATH_LENGTH];
    snprintf(hover, sizeof(hover), "%s", state->selected_entry ? state->selected_entry : "");
    bool was_search = state->file_search != NULL;
    if (start(state, query, content)) {
        if (!was_search)
            snprintf(state->search_origin_hover, sizeof(state->search_origin_hover), "%s", hover);
        search_clear(state);
    }
}

void app_search_reload(AppState *state) {
    char *query = g_strdup(file_search_query(state->file_search));
    bool content = file_search_content(state->file_search);
    (void)start(state, query, content);
    g_free(query);
}

void app_search_stop(AppState *state, bool reload) {
    if (!state->file_search)
        return;
    file_search_free(state->file_search);
    state->file_search = NULL;
    search_clear(state);
    if (reload) {
        app_reload_view(state);
        SIZE index = find_loaded_index_by_name(&state->files, state->search_origin_hover);
        app_move_cursor(state, index == (SIZE)-1 ? 0 : index);
    }
}

static bool relative_path(char *path) {
    if (*path == '/')
        return false;
    while (g_str_has_prefix(path, "./"))
        memmove(path, path + 2, strlen(path + 2) + 1);
    size_t length = strlen(path);
    while (length && path[length - 1] == '/')
        path[--length] = 0;
    if (!length || !strcmp(path, "."))
        return false;
    const char *part = path;
    for (;;) {
        const char *slash = strchr(part, '/');
        size_t size = slash ? (size_t)(slash - part) : strlen(part);
        if (size == 2 && !memcmp(part, "..", 2))
            return false;
        if (!slash)
            break;
        part = slash + 1;
    }
    return true;
}

bool app_search_poll(AppState *state) {
    if (!state->file_search)
        return false;
    if (state->search_revision != Vector_revision(state->files)) {
        app_search_reload(state);
        return true;
    }
    GPtrArray *paths = file_search_take(state->file_search, 512);
    char hover[MAX_PATH_LENGTH];
    snprintf(hover, sizeof(hover), "%s", state->selected_entry ? state->selected_entry : "");
    int preview = state->preview_start_line;
    size_t before = Vector_len(state->files);
    for (guint i = 0; i < paths->len; i++) {
        char *relative = g_ptr_array_index(paths, i);
        if (!relative_path(relative) ||
            strlen(relative) + strlen(state->current_directory) + 2 > MAX_PATH_LENGTH)
            continue;
        char path[MAX_PATH_LENGTH];
        path_join(path, state->current_directory, relative);
        struct stat info, followed;
        if (lstat(path, &info))
            continue;
        bool directory =
            S_ISDIR(info.st_mode) ||
            (S_ISLNK(info.st_mode) && !stat(path, &followed) && S_ISDIR(followed.st_mode));
        FileAttr attr = mk_attr(relative, directory, info.st_ino);
        if (!attr || !Vector_add(&state->files, 1)) {
            free_attr(attr);
            show_notification(notifwin, "Search: cannot retain more results");
            should_clear_notif = false;
            app_search_stop(state, true);
            g_ptr_array_free(paths, TRUE);
            return true;
        }
        size_t at = Vector_len(state->files);
        state->files.el[at] = attr;
        Vector_set_len_no_free(&state->files, at + 1);
    }
    g_ptr_array_free(paths, TRUE);
    bool finished = file_search_done(state->file_search) && !state->search_reported;
    if (finished) {
        state->search_reported = true;
        file_listing_sort(&state->files, state->current_directory);
        const char *error = file_search_error(state->file_search);
        if (error) {
            show_notification(notifwin, "Search: %s", error);
            should_clear_notif = false;
        }
    }
    bool changed = finished || before != Vector_len(state->files);
    if (changed) {
        if (state->search_active)
            search_rebuild(state, state->search_query);
        state->lazy_load.files_loaded = state->lazy_load.total_files = Vector_len(state->files);
        app_selection_restore(state);
        SIZE at = find_loaded_index_by_name(active_files(state), hover);
        app_move_cursor(state, at == (SIZE)-1 ? state->dir_window_cas.cursor : at);
        if (state->selected_entry && !strcmp(hover, state->selected_entry))
            state->preview_start_line = preview;
    }
    state->search_revision = Vector_revision(state->files);
    return changed;
}
