#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "app_navigation.h"
#include "app_session.h"

#include <dirent.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "git.h"
#include "globals.h"
#include "main.h"
#include "search.h"
#include "ui.h"
#include "utils.h"

void navigation_clear_stack(VecStack *stack) {
    if (!stack)
        return;
    char *p;
    while ((p = (char *)VecStack_pop(stack)) != NULL) {
        free(p);
    }
}

void navigate_up(CursorAndSlice *cas, Vector *files, const char **selected_entry,
                 const char *current_directory, LazyLoadState *lazy_load) {
    if (!cas || !files || !selected_entry)
        return;
    cas->num_files = Vector_len(*files);
    if (cas->cursor > 0)
        cas->cursor--;
    else if (cas->num_files) {
        if (lazy_load && current_directory) {
            size_t remaining = lazy_load->total_files > lazy_load->files_loaded
                                   ? lazy_load->total_files - lazy_load->files_loaded
                                   : 0;
            if (remaining)
                append_files_to_vec_lazy(files, current_directory, remaining,
                                         &lazy_load->files_loaded);
            cas->num_files = Vector_len(*files);
        }
        cas->cursor = cas->num_files - 1;
    }
    fix_cursor(cas);
    *selected_entry = cas->num_files ? FileAttr_get_name(files->el[cas->cursor]) : "";
}

void navigate_down(CursorAndSlice *cas, Vector *files, const char **selected_entry,
                   const char *current_directory, LazyLoadState *lazy_load) {
    if (!cas || !files || !selected_entry)
        return;
    if (lazy_load && current_directory)
        load_more_files_if_needed(files, current_directory, cas, &lazy_load->files_loaded,
                                  lazy_load->total_files);
    cas->num_files = Vector_len(*files);
    if (cas->cursor + 1 < cas->num_files)
        cas->cursor++;
    else if (cas->num_files)
        cas->cursor = 0;
    fix_cursor(cas);
    *selected_entry = cas->num_files ? FileAttr_get_name(files->el[cas->cursor]) : "";
}

void navigate_left(char **current_directory, Vector *files, CursorAndSlice *dir_window_cas,
                   AppState *state, VecStack *dir_stack) {
    if (!current_directory || !*current_directory || !files || !dir_window_cas || !state ||
        !strcmp(*current_directory, "/"))
        return;
    char *parent = strdup(*current_directory);
    if (!parent)
        return;
    size_t length = strlen(parent);
    while (length > 1 && parent[length - 1] == '/')
        parent[--length] = '\0';
    char *slash = strrchr(parent, '/');
    if (!slash) {
        free(parent);
        return;
    }
    char child[MAX_PATH_LENGTH];
    snprintf(child, sizeof(child), "%s", slash + 1);
    slash[slash == parent ? 1 : 0] = '\0';
    DIR *directory = opendir(parent);
    char *lazy_path = directory ? strdup(parent) : NULL;
    if (directory)
        closedir(directory);
    if (!lazy_path) {
        free(parent);
        return;
    }
    search_clear(state);
    free(*current_directory);
    *current_directory = parent;
    free(state->lazy_load.directory_path);
    state->lazy_load.directory_path = lazy_path;
    if (dir_stack)
        free(VecStack_pop(dir_stack));
    git_update_directory(parent);
    reload_directory_lazy(files, parent, &state->lazy_load.files_loaded,
                          &state->lazy_load.total_files);
    dir_window_cas->cursor = 0;
    dir_window_cas->start = 0;
    dir_window_cas->num_files = Vector_len(*files);
    SIZE index = find_index_by_name_lazy(files, parent, dir_window_cas, &state->lazy_load, child);
    dir_window_cas->cursor = index == (SIZE)-1 ? 0 : index;
    fix_cursor(dir_window_cas);
    app_selection_restore(state);
    sync_selection_from_active(state, dir_window_cas);
    show_notification(notifwin, "Parent: %s", parent);
    should_clear_notif = false;
}

void navigate_right(AppState *state, char **current_directory, Vector *files_view,
                    CursorAndSlice *dir_window_cas, VecStack *dir_stack) {
    if (!state || !current_directory || !*current_directory || !dir_window_cas)
        return;

    Vector *view = files_view ? files_view : &state->files;
    if (Vector_len(*view) == 0)
        return;
    if (dir_window_cas->cursor < 0 || (size_t)dir_window_cas->cursor >= Vector_len(*view))
        return;

    FileAttr current_file = (FileAttr)view->el[dir_window_cas->cursor];
    const char *selected_entry = FileAttr_get_name(current_file);
    if (!FileAttr_is_dir(current_file) || !selected_entry) {
        werase(notifwin);
        show_notification(notifwin, "Selected entry is not a directory");
        should_clear_notif = false;

        wrefresh(notifwin);
        return;
    }

    char new_path[MAX_PATH_LENGTH];
    if (strlen(*current_directory) + strlen(selected_entry) + 2 > sizeof(new_path)) {
        show_notification(notifwin, "Directory path is too long");
        should_clear_notif = false;
        return;
    }
    path_join(new_path, *current_directory, selected_entry);
    DIR *directory = opendir(new_path);
    if (!directory) {
        show_notification(notifwin, "Cannot enter directory: %s", selected_entry);
        should_clear_notif = false;
        return;
    }
    closedir(directory);

    if (strcmp(new_path, *current_directory) == 0) {
        werase(notifwin);
        show_notification(notifwin, "Already in this directory");
        should_clear_notif = false;
        wrefresh(notifwin);
        return;
    }

    if (dir_stack) {
        char *new_entry = strdup(selected_entry);
        if (new_entry == NULL) {
            mvwprintw(notifwin, LINES - 1, 1, "Memory allocation error");
            wrefresh(notifwin);
            return;
        }
        if (!VecStack_push(dir_stack, new_entry)) {
            free(new_entry);
            show_notification(notifwin, "Memory allocation error");
            wrefresh(notifwin);
            return;
        }
    }

    char *replacement = strdup(new_path);
    if (replacement == NULL) {
        mvwprintw(notifwin, LINES - 1, 1, "Memory allocation error");
        wrefresh(notifwin);
        if (dir_stack) {
            free(VecStack_pop(dir_stack));
        }
        return;
    }

    free(*current_directory);
    *current_directory = replacement;
    search_clear(state);

    if (state->lazy_load.directory_path) {
        free(state->lazy_load.directory_path);
    }
    state->lazy_load.directory_path = strdup(*current_directory);
    state->lazy_load.last_load_time = (struct timespec){0};

    git_update_directory(*current_directory);
    reload_directory_lazy(&state->files, *current_directory, &state->lazy_load.files_loaded,
                          &state->lazy_load.total_files);

    dir_window_cas->cursor = 0;
    dir_window_cas->start = 0;
    dir_window_cas->num_lines = LINES - 6;
    dir_window_cas->num_files = Vector_len(state->files);

    if (dir_window_cas->num_files > 0) {
        state->selected_entry = FileAttr_get_name(state->files.el[0]);
    } else {
        state->selected_entry = "";
    }

    if (dir_window_cas->num_files == 1) {
        state->selected_entry = FileAttr_get_name(state->files.el[0]);
    }

    app_selection_restore(state);
    werase(notifwin);
    show_notification(notifwin, "Entered directory: %s", state->selected_entry);
    should_clear_notif = false;

    wrefresh(notifwin);
}
