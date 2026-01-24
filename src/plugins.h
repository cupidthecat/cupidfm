// plugins.h
#ifndef PLUGINS_H
#define PLUGINS_H

#include <stdbool.h>

typedef struct PluginManager PluginManager;

// Initialize and load all plugins. Safe to call even if no plugin dirs exist.
PluginManager *plugins_create(void);
void plugins_destroy(PluginManager *pm);

// Update context available to plugins (copied internally).
void plugins_set_context(PluginManager *pm, const char *cwd, const char *selected_name);

// Dispatch a key press to plugins. Returns true if a plugin handled it.
bool plugins_handle_key(PluginManager *pm, int key);

// Retrieve and clear requests raised by plugins.
bool plugins_take_reload_request(PluginManager *pm);
bool plugins_take_quit_request(PluginManager *pm);

#endif // PLUGINS_H
