#ifndef SEARCH_H
#define SEARCH_H

#include <stdbool.h>
#include <stddef.h>

#include <ncurses.h>

#include "app_state.h"

Vector *active_files(AppState *state);

typedef enum {
    SEARCH_MODE_FUZZY = 0,
    SEARCH_MODE_EXACT = 1,
    SEARCH_MODE_REGEX = 2,
} SearchMode;

void search_clear(AppState *state);
size_t search_rebuild(AppState *state, const char *query);
void search_before_reload(AppState *state, char saved_query[MAX_PATH_LENGTH]);
void search_after_reload(AppState *state, CursorAndSlice *cas, const char *saved_query);

void maybe_load_more_for_search(AppState *state, CursorAndSlice *cas);
void sync_selection_from_active(AppState *state, CursorAndSlice *cas);

SIZE find_loaded_index_by_name(Vector *files, const char *name);
SIZE find_index_by_name_lazy(Vector *files,
                            const char *dir,
                            CursorAndSlice *cas,
                            LazyLoadState *lazy_load,
                            const char *name);

bool prompt_fuzzy_search(AppState *state,
                         CursorAndSlice *cas,
                         WINDOW *win,
                         WINDOW *dir_window,
                         WINDOW *preview_window);

#endif // SEARCH_H
