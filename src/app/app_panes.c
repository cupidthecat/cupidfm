#define _GNU_SOURCE
#include "app_panes.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#include "search.h"
#include "utils.h"
#include "watch.h"

static FileWatch parent_watch = {.fd = -1, .watch = -1};
static FileWatch child_watch = {.fd = -1, .watch = -1};

void app_panes_stop(void) {
    file_watch_close(&parent_watch);
    file_watch_close(&child_watch);
}

void app_pane_dispose(AppDirectoryPane *pane) {
    for (size_t i = 0; i < Vector_len(pane->files); i++)
        free_attr(pane->files.el[i]);
    Vector_set_len_no_free(&pane->files, 0);
    Vector_bye(&pane->files);
    *pane = (AppDirectoryPane){0};
}

static bool pane_refresh(AppDirectoryPane *pane, const char *directory, FileWatch *watch) {
    struct stat metadata;
    if (stat(directory, &metadata) || !S_ISDIR(metadata.st_mode)) {
        pane->loaded = false;
        return false;
    }
    bool changed = file_watch_poll(watch, directory);
    FileListingOptions options = file_listing_options(), old = pane->listing;
    bool preferences = options.sort != old.sort || options.sensitive != old.sensitive ||
                       options.reverse != old.reverse || options.dir_first != old.dir_first ||
                       options.show_hidden != old.show_hidden ||
                       options.random_seed != old.random_seed;
    if (pane->loaded && !strcmp(directory, pane->directory) && !preferences && !changed &&
        metadata.st_mtim.tv_sec == pane->mtime.tv_sec &&
        metadata.st_mtim.tv_nsec == pane->mtime.tv_nsec)
        return true;
    DIR *check = opendir(directory);
    if (!check) {
        pane->loaded = false;
        return false;
    }
    closedir(check);
    if (strcmp(directory, pane->directory))
        pane->cas = (CursorAndSlice){0};
    for (size_t i = 0; i < Vector_len(pane->files); i++)
        free_attr(pane->files.el[i]);
    Vector_set_len_no_free(&pane->files, 0);
    append_files_to_vec(&pane->files, directory);
    snprintf(pane->directory, sizeof(pane->directory), "%s", directory);
    pane->mtime = metadata.st_mtim;
    pane->listing = options;
    pane->loaded = true;
    pane->cas.num_files = Vector_len(pane->files);
    return true;
}

static void pane_selection(AppState *state, AppDirectoryPane *pane, int rows) {
    SIZE start = MAX(0, MIN(pane->cas.start, (SIZE)Vector_len(pane->files) - rows));
    SIZE end = MIN((SIZE)Vector_len(pane->files), start + rows);
    for (SIZE i = start; i < end; i++) {
        FileAttr file = pane->files.el[i];
        char path[MAX_PATH_LENGTH];
        const char *name = FileAttr_get_name(file);
        bool fits = strlen(pane->directory) + strlen(name) + 2 <= sizeof(path);
        if (fits)
            path_join(path, pane->directory, name);
        FileAttr_select(file, fits && selection_has(state->selection, path));
    }
}

void app_parent_refresh(AppState *state) {
    char parent[MAX_PATH_LENGTH], child[MAX_PATH_LENGTH];
    snprintf(parent, sizeof(parent), "%s", state->current_directory);
    size_t length = strlen(parent);
    while (length > 1 && parent[length - 1] == '/')
        parent[--length] = '\0';
    char *slash = strrchr(parent, '/');
    if (!slash || !strcmp(parent, "/")) {
        state->parent.loaded = false;
        return;
    }
    snprintf(child, sizeof(child), "%s", slash + 1);
    slash[slash == parent ? 1 : 0] = '\0';
    if (!pane_refresh(&state->parent, parent, &parent_watch))
        return;
    SIZE index = find_loaded_index_by_name(&state->parent.files, child);
    state->parent.cas.cursor = index == (SIZE)-1 ? 0 : index;
}

void app_parent_draw(AppState *state, WINDOW *window) {
    if (!window)
        return;
    app_parent_refresh(state);
    if (!state->parent.loaded) {
        werase(window);
        wrefresh(window);
        return;
    }
    state->parent.cas.num_lines = getmaxy(window) + (g_kb.vim_keymap ? 2 : 0);
    fix_cursor(&state->parent.cas);
    pane_selection(state, &state->parent, getmaxy(window));
    bool selected = g_select_all_highlight;
    g_select_all_highlight = false;
    draw_directory_window(window, state->parent.directory, &state->parent.files,
                          &state->parent.cas);
    g_select_all_highlight = selected;
}

bool app_child_refresh(AppState *state) {
    char directory[MAX_PATH_LENGTH];
    if (state->preview_override_active)
        snprintf(directory, sizeof(directory), "%s", state->preview_override_path);
    else {
        const char *name = state->selected_entry;
        if (!name || !*name ||
            strlen(state->current_directory) + strlen(name) + 2 > sizeof(directory)) {
            state->child.loaded = false;
            return false;
        }
        path_join(directory, state->current_directory, name);
    }
    return pane_refresh(&state->child, directory, &child_watch);
}

bool app_child_draw(AppState *state, WINDOW *window) {
    if (!app_child_refresh(state))
        return false;
    browser_clear_preview(window);
    state->child.cas.start = state->preview_start_line;
    pane_selection(state, &state->child, getmaxy(window));
    bool selected = g_select_all_highlight;
    g_select_all_highlight = false;
    draw_directory_preview(window, state->child.directory, &state->child.files, &state->child.cas);
    g_select_all_highlight = selected;
    state->preview_start_line = state->child.cas.start;
    return true;
}
