#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "app_navigation.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "globals.h"
#include "main.h"
#include "search.h"
#include "ui.h"
#include "utils.h"

void navigation_clear_stack(VecStack *stack) {
    if (!stack) return;
    char *p;
    while ((p = (char *)VecStack_pop(stack)) != NULL) {
        free(p);
    }
}

void navigate_up(CursorAndSlice *cas,
                 Vector *files,
                 const char **selected_entry,
                 const char *current_directory,
                 LazyLoadState *lazy_load) {
    if (!cas || !files || !selected_entry) return;
    cas->num_files = Vector_len(*files);
    if (cas->num_files > 0) {
        if (cas->cursor == 0) {
            if (lazy_load && current_directory) {
                load_more_files_if_needed(files, current_directory, cas,
                                          &lazy_load->files_loaded, lazy_load->total_files);
                cas->num_files = Vector_len(*files);
            }

            cas->cursor = cas->num_files - 1;
            int visible_lines = cas->num_lines - 2;
            int max_start = cas->num_files - visible_lines;
            if (max_start < 0) {
                cas->start = 0;
            } else {
                cas->start = max_start;
            }
        } else {
            cas->cursor -= 1;
            if (cas->cursor < cas->start) {
                cas->start = cas->cursor;
            }
        }
        fix_cursor(cas);
        if (cas->num_files > 0) {
            *selected_entry = FileAttr_get_name(files->el[cas->cursor]);
        }
    }
}

void navigate_down(CursorAndSlice *cas,
                   Vector *files,
                   const char **selected_entry,
                   const char *current_directory,
                   LazyLoadState *lazy_load) {
    if (!cas || !files || !selected_entry) return;
    cas->num_files = Vector_len(*files);
    if (cas->num_files > 0) {
        if (cas->cursor >= cas->num_files - 1) {
            cas->cursor = 0;
            cas->start = 0;
        } else {
            cas->cursor += 1;
            int visible_lines = cas->num_lines - 2;

            if (cas->cursor >= cas->start + visible_lines) {
                cas->start = cas->cursor - visible_lines + 1;
            }

            int max_start = cas->num_files - visible_lines;
            if (max_start < 0) max_start = 0;
            if (cas->start > max_start) {
                cas->start = max_start;
            }
        }
        fix_cursor(cas);

        if (lazy_load && current_directory) {
            load_more_files_if_needed(files, current_directory, cas,
                                      &lazy_load->files_loaded, lazy_load->total_files);
            cas->num_files = Vector_len(*files);
        }

        if (cas->num_files > 0) {
            *selected_entry = FileAttr_get_name(files->el[cas->cursor]);
        }
    }
}

void navigate_left(char **current_directory,
                   Vector *files,
                   CursorAndSlice *dir_window_cas,
                   AppState *state,
                   VecStack *dir_stack) {
    if (!current_directory || !*current_directory || !files || !dir_window_cas || !state) return;

    search_clear(state);

    char *popped_dir = dir_stack ? VecStack_pop(dir_stack) : NULL;

    if (strcmp(*current_directory, "/") != 0) {
        char *last_slash = strrchr(*current_directory, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            if (state->lazy_load.directory_path) {
                free(state->lazy_load.directory_path);
            }
            state->lazy_load.directory_path = strdup(*current_directory);
            reload_directory_lazy(files, *current_directory,
                                  &state->lazy_load.files_loaded, &state->lazy_load.total_files);
        }
    }

    if ((*current_directory)[0] == '\0') {
        strcpy(*current_directory, "/");
        if (state->lazy_load.directory_path) {
            free(state->lazy_load.directory_path);
        }
        state->lazy_load.directory_path = strdup(*current_directory);
        reload_directory_lazy(files, *current_directory,
                              &state->lazy_load.files_loaded, &state->lazy_load.total_files);
    }

    if (popped_dir) {
        SIZE idx = find_index_by_name_lazy(files, *current_directory, dir_window_cas,
                                           &state->lazy_load, popped_dir);
        if (idx != (SIZE)-1) {
            dir_window_cas->cursor = idx;
        } else {
            dir_window_cas->cursor = 0;
        }
        free(popped_dir);
    }

    dir_window_cas->cursor = 0;
    dir_window_cas->start = 0;
    dir_window_cas->num_lines = LINES - 6;
    dir_window_cas->num_files = Vector_len(*files);

    if (dir_window_cas->num_files > 0) {
        state->selected_entry = FileAttr_get_name(files->el[0]);
    } else {
        state->selected_entry = "";
    }

    werase(notifwin);
    show_notification(notifwin, "Navigated to parent directory: %s", *current_directory);
    should_clear_notif = false;

    wrefresh(notifwin);
}

void navigate_right(AppState *state,
                    char **current_directory,
                    Vector *files_view,
                    CursorAndSlice *dir_window_cas,
                    VecStack *dir_stack) {
    if (!state || !current_directory || !*current_directory || !dir_window_cas) return;

    Vector *view = files_view ? files_view : &state->files;
    if (Vector_len(*view) == 0) return;
    if (dir_window_cas->cursor < 0 || (size_t)dir_window_cas->cursor >= Vector_len(*view)) return;

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
    path_join(new_path, *current_directory, selected_entry);

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
        VecStack_push(dir_stack, new_entry);
    }

    free(*current_directory);
    *current_directory = strdup(new_path);
    if (*current_directory == NULL) {
        mvwprintw(notifwin, LINES - 1, 1, "Memory allocation error");
        wrefresh(notifwin);
        if (dir_stack) {
            free(VecStack_pop(dir_stack));
        }
        return;
    }

    search_clear(state);

    if (state->lazy_load.directory_path) {
        free(state->lazy_load.directory_path);
    }
    state->lazy_load.directory_path = strdup(*current_directory);
    state->lazy_load.last_load_time = (struct timespec){0};

    reload_directory_lazy(&state->files, *current_directory,
                          &state->lazy_load.files_loaded, &state->lazy_load.total_files);

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

    werase(notifwin);
    show_notification(notifwin, "Entered directory: %s", state->selected_entry);
    should_clear_notif = false;

    wrefresh(notifwin);
}
