#ifndef APP_STATE_H
#define APP_STATE_H

#include <stdbool.h>
#include <time.h>

#include "browser_ui.h" // CursorAndSlice
#include "globals.h"    // MAX_PATH_LENGTH
#include "undo.h"       // UndoState
#include "vector.h"

typedef struct PluginManager PluginManager;

typedef struct {
    char *directory_path;
    size_t files_loaded;
    size_t total_files; // (size_t)-1 if unknown
    bool is_loading;
    struct timespec last_load_time;
} LazyLoadState;

typedef struct {
    char *current_directory;
    Vector files;
    CursorAndSlice dir_window_cas;
    const char *selected_entry;
    bool select_all_active;
    int preview_start_line;
    bool preview_override_active;
    char preview_override_path[MAX_PATH_LENGTH];
    LazyLoadState lazy_load;
    bool search_active;
    char search_query[MAX_PATH_LENGTH];
    int search_mode;
    Vector search_files;
    UndoState undo_state;
    PluginManager *plugins;
} AppState;

#endif // APP_STATE_H

