// plugins_internal.h - Internal shared declarations for plugin system
#ifndef PLUGINS_INTERNAL_H
#define PLUGINS_INTERNAL_H

#include "plugins.h"
#include "cs_value.h"
#include "cs_vm.h"
#include "globals.h"
#include "vector.h"

// Plugin structure
typedef struct {
    cs_vm *vm;
    char *path;
} Plugin;

// Key binding structure
typedef struct {
    int key;
    cs_vm *vm;
    char *func;
} KeyBinding;

// Event binding structure
typedef struct {
    char event[32];
    cs_vm *vm;
    bool cb_is_name;
    char cb_name[64];
    cs_value cb;
} EventBinding;

// Mark entry structure
typedef struct {
    char *name;
    char *path;
} MarkEntry;

// Plugin manager structure (defined in plugins.h as opaque, detailed here)
struct PluginManager {
    Plugin *plugins;
    size_t plugin_count;
    size_t plugin_cap;

    KeyBinding *bindings;
    size_t bind_count;
    size_t bind_cap;

    EventBinding *event_bindings;
    size_t event_bind_count;
    size_t event_bind_cap;

    MarkEntry *marks;
    size_t mark_count;
    size_t mark_cap;

    char cwd[MAX_PATH_LENGTH];
    char selected[MAX_PATH_LENGTH];

    int cursor_index;
    int list_count;
    bool select_all_active;
    bool search_active;
    char search_query[MAX_PATH_LENGTH];
    int active_pane;
    const Vector *view;
    bool context_initialized;

    bool reload_requested;
    bool quit_requested;

    bool cd_requested;
    char cd_path[MAX_PATH_LENGTH];

    bool select_requested;
    char select_name[MAX_PATH_LENGTH];

    bool select_index_requested;
    int select_index;

    bool open_selected_requested;
    bool open_path_requested;
    char open_path[MAX_PATH_LENGTH];
    char **selected_paths;
    size_t selected_path_count;
    bool preview_path_requested;
    char preview_path[MAX_PATH_LENGTH];
    bool enter_dir_requested;
    bool parent_dir_requested;

    bool set_search_requested;
    char requested_search_query[MAX_PATH_LENGTH];
    bool clear_search_requested;
    bool set_search_mode_requested;
    int requested_search_mode;

    bool fileop_requested;
    PluginFileOp op;

    // Async prompt UI (modal)
    bool ui_pending;
    enum {
        UI_KIND_NONE = 0,
        UI_KIND_PROMPT,
        UI_KIND_CONFIRM,
        UI_KIND_MENU
    } ui_kind;
    char ui_title[128];
    char ui_msg[512];
    char ui_initial[256];
    char **ui_items;
    size_t ui_item_count;
    cs_vm *ui_vm;
    bool ui_cb_is_name;
    char ui_cb_name[64];
    cs_value ui_cb;
};

// Modal UI functions (from plugin_ui.c)
cs_value plugin_modal_prompt_text(cs_vm *vm, const char *title, const char *msg, const char *initial);
bool plugin_modal_confirm(const char *title, const char *msg);
int plugin_modal_menu(const char *title, char **items, size_t count);
void plugin_menu_items_free(char **items, size_t count);

// fm.* API registration (from plugins_api.c)
void plugins_register_fm_api(PluginManager *pm, cs_vm *vm);

// Helper functions used across modules
void plugin_notify(const char *msg);
int plugin_parse_key_local(const char *val);
void plugin_keycode_to_config_string_local(int keycode, char *buf, size_t buf_size);

// Config/cache helpers
bool plugin_get_config_path(char *out_path, size_t out_len);
bool plugin_get_cache_path(char *out_path, size_t out_len);
bool plugin_ensure_cache_dir(void);
char *plugin_escape_kv(const char *s);
char *plugin_unescape_kv(const char *s);
cs_value plugin_decode_cache_value(cs_vm *vm, const char *raw);
char *plugin_encode_cache_value(const cs_value *v);

#endif // PLUGINS_INTERNAL_H
