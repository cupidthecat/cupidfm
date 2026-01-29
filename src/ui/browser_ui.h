#ifndef BROWSER_UI_H
#define BROWSER_UI_H

#include <ncurses.h>

#include "utils.h"  // SIZE, MIN/MAX, path_join, etc.
#include "vector.h"

typedef struct {
    SIZE start;
    SIZE cursor;
    SIZE num_lines;
    SIZE num_files;
} CursorAndSlice;

void draw_directory_window(WINDOW *window,
                           const char *directory,
                           Vector *files_vector,
                           CursorAndSlice *cas);

void draw_preview_window(WINDOW *window,
                         const char *current_directory,
                         const char *selected_entry,
                         int start_line);

void draw_preview_window_path(WINDOW *window,
                              const char *full_path,
                              const char *display_name,
                              int start_line);

void fix_cursor(CursorAndSlice *cas);

int get_total_lines(const char *file_path);
int get_directory_tree_total_lines(const char *dir_path);

#endif // BROWSER_UI_H
