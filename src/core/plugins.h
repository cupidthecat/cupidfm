// plugins.h
#ifndef PLUGINS_H
#define PLUGINS_H

#include <stdbool.h>
#include <stddef.h> // size_t
#include "globals.h" // MAX_PATH_LENGTH
#include "vector.h"

typedef struct PluginManager PluginManager;

typedef struct {
    const char *cwd;
    const char *selected_name;
    int cursor_index;
    int list_count;
    bool search_active;
    const char *search_query;
    int active_pane; // 1=directory, 2=preview
    const Vector *view; // current visible listing (state->files or state->search_files)
} PluginsContext;

typedef enum {
    PLUGIN_FILEOP_NONE = 0,
    PLUGIN_FILEOP_COPY,
    PLUGIN_FILEOP_MOVE,
    PLUGIN_FILEOP_RENAME,
    PLUGIN_FILEOP_DELETE,
    PLUGIN_FILEOP_MKDIR,
    PLUGIN_FILEOP_TOUCH,
    PLUGIN_FILEOP_UNDO,
    PLUGIN_FILEOP_REDO,
} PluginFileOpKind;

typedef struct {
    PluginFileOpKind kind;
    size_t count;     // number of paths (for copy/move/delete/rename)
    char **paths;     // owned by caller; free with plugins_fileop_free()
    char arg1[MAX_PATH_LENGTH]; // dst_dir (copy/move) | new_name (rename) | name/path (mkdir/touch)
} PluginFileOp;

// Initialize and load all plugins. Safe to call even if no plugin dirs exist.
PluginManager *plugins_create(void);
void plugins_destroy(PluginManager *pm);

// Update context available to plugins (copied internally).
void plugins_set_context_ex(PluginManager *pm, const PluginsContext *ctx);

// Dispatch a key press to plugins. Returns true if a plugin handled it.
bool plugins_handle_key(PluginManager *pm, int key);

// Pump any pending asynchronous plugin UI requests (prompt/confirm/menu).
// Safe to call every loop.
void plugins_poll(PluginManager *pm);

// Retrieve and clear requests raised by plugins.
bool plugins_take_reload_request(PluginManager *pm);
bool plugins_take_quit_request(PluginManager *pm);
bool plugins_take_cd_request(PluginManager *pm, char *out_path, size_t out_len);
bool plugins_take_select_request(PluginManager *pm, char *out_name, size_t out_len);
bool plugins_take_select_index_request(PluginManager *pm, int *out_index);

// Selection + navigation helpers (async: applied by host after script hook completes).
bool plugins_take_open_selected_request(PluginManager *pm);
bool plugins_take_enter_dir_request(PluginManager *pm);
bool plugins_take_parent_dir_request(PluginManager *pm);

// Search control (async: applied by host after script hook completes).
bool plugins_take_set_search_request(PluginManager *pm, char *out_query, size_t out_len);
bool plugins_take_clear_search_request(PluginManager *pm);

// File operation requests issued by plugins (fm.copy/move/rename/delete/mkdir/touch/undo/redo).
// Transfers ownership of op.paths to the caller.
bool plugins_take_fileop_request(PluginManager *pm, PluginFileOp *out);
void plugins_fileop_free(PluginFileOp *op);

#endif // PLUGINS_H
