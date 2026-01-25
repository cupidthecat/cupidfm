#ifndef APP_NAVIGATION_H
#define APP_NAVIGATION_H

#include "app_state.h"
#include "browser_ui.h"
#include "vecstack.h"
#include "vector.h"

// Stack helpers for directory navigation history.
void navigation_clear_stack(VecStack *stack);

void navigate_up(CursorAndSlice *cas,
                 Vector *files,
                 const char **selected_entry,
                 const char *current_directory,
                 LazyLoadState *lazy_load);

void navigate_down(CursorAndSlice *cas,
                   Vector *files,
                   const char **selected_entry,
                   const char *current_directory,
                   LazyLoadState *lazy_load);

void navigate_left(char **current_directory,
                   Vector *files,
                   CursorAndSlice *dir_window_cas,
                   AppState *state,
                   VecStack *dir_stack);

void navigate_right(AppState *state,
                    char **current_directory,
                    Vector *files_view,
                    CursorAndSlice *dir_window_cas,
                    VecStack *dir_stack);

#endif // APP_NAVIGATION_H
