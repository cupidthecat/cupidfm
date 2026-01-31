// plugins.c
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "plugins.h"
#include "plugins_internal.h"
#include "plugins_keys.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../ui/ui.h"
#include "cs_value.h"
#include "cs_vm.h"
#include "globals.h"
#include "search.h"

static bool ends_with(const char *s, const char *suffix) {
  if (!s || !suffix)
    return false;
  size_t sl = strlen(s), su = strlen(suffix);
  if (su > sl)
    return false;
  return memcmp(s + (sl - su), suffix, su) == 0;
}

static bool ensure_dir(const char *path) {
  if (!path || !*path)
    return false;
  if (mkdir(path, 0700) == 0)
    return true;
  return errno == EEXIST;
}

static void pm_notify(const char *msg) { plugin_notify(msg); }

static bool plugin_append(PluginManager *pm, cs_vm *vm, const char *path) {
  if (pm->plugin_count == pm->plugin_cap) {
    size_t new_cap = pm->plugin_cap ? pm->plugin_cap * 2 : 8;
    Plugin *np = (Plugin *)realloc(pm->plugins, new_cap * sizeof(Plugin));
    if (!np)
      return false;
    pm->plugins = np;
    pm->plugin_cap = new_cap;
  }
  pm->plugins[pm->plugin_count].vm = vm;
  pm->plugins[pm->plugin_count].path = strdup(path ? path : "");
  if (!pm->plugins[pm->plugin_count].path)
    return false;
  pm->plugin_count++;
  return true;
}

static void load_plugins_from_dir(PluginManager *pm, const char *dir_path) {
  DIR *d = opendir(dir_path);
  if (!d)
    return;

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.')
      continue;
    if (!ends_with(ent->d_name, ".cs"))
      continue;

    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);

    cs_vm *vm = cs_vm_new();
    if (!vm) {
      pm_notify("Plugin VM alloc failed");
      continue;
    }
    plugins_register_fm_api(pm, vm);

    int rc = cs_vm_run_file(vm, full);
    if (rc != 0) {
      const char *err = cs_vm_last_error(vm);
      char msg[512];
      snprintf(msg, sizeof(msg), "Plugin load failed: %s: %s", ent->d_name,
               err ? err : "");
      pm_notify(msg);
      hold_notification_for_ms(5000);
      cs_vm_free(vm);
      continue;
    }

    (void)plugin_append(pm, vm, full);
    {
      char msg[256];
      snprintf(msg, sizeof(msg), "Loaded plugin: %.200s", ent->d_name);
      pm_notify(msg);
      // Keep visible long enough to actually read.
      hold_notification_for_ms(1500);
    }

    // Optional hook.
    cs_value out = cs_nil();
    if (cs_call(vm, "on_load", 0, NULL, &out) != 0) {
      const char *err = cs_vm_last_error(vm);
      char msg[512];
      snprintf(msg, sizeof(msg), "Plugin on_load failed: %s: %s", ent->d_name,
               err ? err : "");
      pm_notify(msg);
      // Plugin errors are important; keep them visible long enough to read.
      hold_notification_for_ms(5000);
      // Clear so we don't spam the same error for other hooks.
      cs_error(vm, "");
    }
    cs_value_release(out);
  }

  closedir(d);
}

static void plugins_init(PluginManager *pm) {
  memset(pm, 0, sizeof(*pm));
  pm->cwd[0] = '\0';
  pm->selected[0] = '\0';
  pm->search_query[0] = '\0';
  pm->cursor_index = -1;
  pm->list_count = 0;
  pm->select_all_active = false;
  pm->active_pane = 0;
  pm->view = NULL;
  pm->context_initialized = false;
  pm->cd_path[0] = '\0';
  pm->select_name[0] = '\0';
  pm->select_index = -1;
  pm->fileop_requested = false;
  pm->op.kind = PLUGIN_FILEOP_NONE;
  pm->op.paths = NULL;
  pm->op.count = 0;
  pm->op.arg1[0] = '\0';
  pm->ui_pending = false;
  pm->ui_kind = UI_KIND_NONE;
  pm->ui_title[0] = '\0';
  pm->ui_msg[0] = '\0';
  pm->ui_initial[0] = '\0';
  pm->ui_items = NULL;
  pm->ui_item_count = 0;
  pm->ui_vm = NULL;
  pm->ui_cb_is_name = false;
  pm->ui_cb_name[0] = '\0';
  pm->ui_cb = cs_nil();

  pm->open_selected_requested = false;
  pm->open_path_requested = false;
  pm->open_path[0] = '\0';
  pm->selected_paths = NULL;
  pm->selected_path_count = 0;
  pm->preview_path_requested = false;
  pm->preview_path[0] = '\0';
  pm->enter_dir_requested = false;
  pm->parent_dir_requested = false;
  pm->set_search_requested = false;
  pm->requested_search_query[0] = '\0';
  pm->clear_search_requested = false;
  pm->set_search_mode_requested = false;
  pm->requested_search_mode = SEARCH_MODE_FUZZY;

  // Candidate plugin dirs:
  // 1) ~/.cupidfm/plugins
  // 2) ~/.cupidfm/plugin (legacy/singular)
  // 3) ./cupidfm/plugins
  // 4) ./cupidfm/plugin (legacy/singular)
  // 5) ./plugins
  //
  // NOTE: Local relative plugin loading is gated behind an env var to avoid
  // accidentally loading repo example plugins when the user only wants their
  // ~/.cupidfm/plugins set.
  const char *home = getenv("HOME");
  if (home && *home) {
    char base[PATH_MAX];
    int n = snprintf(base, sizeof(base), "%s/.cupidfm", home);
    if (n < 0 || (size_t)n >= sizeof(base)) {
      return;
    }
    (void)ensure_dir(base);
    char dir[PATH_MAX];
    n = snprintf(dir, sizeof(dir), "%s/plugins", base);
    if (n < 0 || (size_t)n >= sizeof(dir)) {
      return;
    }
    (void)ensure_dir(dir);
    load_plugins_from_dir(pm, dir);

    char dir2[PATH_MAX];
    n = snprintf(dir2, sizeof(dir2), "%s/plugin", base);
    if (n < 0 || (size_t)n >= sizeof(dir2)) {
      return;
    }
    load_plugins_from_dir(pm, dir2);
  }

  const char *allow_local = getenv("CUPIDFM_LOAD_LOCAL_PLUGINS");
  if (allow_local && *allow_local && strcmp(allow_local, "0") != 0) {
    load_plugins_from_dir(pm, "./cupidfm/plugins");
    load_plugins_from_dir(pm, "./cupidfm/plugin");
    load_plugins_from_dir(pm, "./plugins");
  }
}

static void plugins_shutdown(PluginManager *pm) {
  if (!pm)
    return;
  for (size_t i = 0; i < pm->bind_count; i++) {
    free(pm->bindings[i].func);
  }
  free(pm->bindings);
  pm->bindings = NULL;
  pm->bind_count = pm->bind_cap = 0;

  for (size_t i = 0; i < pm->event_bind_count; i++) {
    if (!pm->event_bindings)
      break;
    if (!pm->event_bindings[i].cb_is_name &&
        pm->event_bindings[i].cb.type != CS_T_NIL) {
      cs_value_release(pm->event_bindings[i].cb);
    }
  }
  free(pm->event_bindings);
  pm->event_bindings = NULL;
  pm->event_bind_count = pm->event_bind_cap = 0;

  for (size_t i = 0; i < pm->plugin_count; i++) {
    if (pm->plugins[i].vm)
      cs_vm_free(pm->plugins[i].vm);
    free(pm->plugins[i].path);
  }
  free(pm->plugins);
  pm->plugins = NULL;
  pm->plugin_count = pm->plugin_cap = 0;

  for (size_t i = 0; i < pm->mark_count; i++) {
    free(pm->marks[i].name);
    free(pm->marks[i].path);
  }
  free(pm->marks);
  pm->marks = NULL;
  pm->mark_count = pm->mark_cap = 0;

  pm->reload_requested = false;
  pm->quit_requested = false;
  pm->cd_requested = false;
  pm->cd_path[0] = '\0';
  pm->select_requested = false;
  pm->select_name[0] = '\0';
  pm->select_index_requested = false;
  pm->select_index = -1;
  pm->cwd[0] = '\0';
  pm->selected[0] = '\0';
  pm->cursor_index = -1;
  pm->list_count = 0;
  pm->select_all_active = false;
  pm->search_active = false;
  pm->search_query[0] = '\0';
  pm->active_pane = 0;
  pm->view = NULL;
  pm->open_selected_requested = false;
  pm->open_path_requested = false;
  pm->open_path[0] = '\0';
  if (pm->selected_paths) {
    for (size_t i = 0; i < pm->selected_path_count; i++)
      free(pm->selected_paths[i]);
    free(pm->selected_paths);
  }
  pm->selected_paths = NULL;
  pm->selected_path_count = 0;
  pm->preview_path_requested = false;
  pm->preview_path[0] = '\0';
  pm->enter_dir_requested = false;
  pm->parent_dir_requested = false;
  pm->set_search_requested = false;
  pm->requested_search_query[0] = '\0';
  pm->clear_search_requested = false;
  pm->set_search_mode_requested = false;
  pm->requested_search_mode = SEARCH_MODE_FUZZY;
  pm->fileop_requested = false;
  plugins_fileop_free(&pm->op);
  if (pm->ui_pending) {
    pm->ui_pending = false;
  }
  pm->ui_kind = UI_KIND_NONE;
  pm->ui_title[0] = '\0';
  pm->ui_msg[0] = '\0';
  pm->ui_initial[0] = '\0';
  plugin_menu_items_free(pm->ui_items, pm->ui_item_count);
  pm->ui_items = NULL;
  pm->ui_item_count = 0;
  pm->ui_vm = NULL;
  if (pm->ui_cb.type != CS_T_NIL)
    cs_value_release(pm->ui_cb);
  pm->ui_cb = cs_nil();
  pm->ui_cb_is_name = false;
  pm->ui_cb_name[0] = '\0';
  pm->context_initialized = false;
}

PluginManager *plugins_create(void) {
  PluginManager *pm = (PluginManager *)calloc(1, sizeof(PluginManager));
  if (!pm)
    return NULL;
  plugins_init(pm);
  return pm;
}

void plugins_destroy(PluginManager *pm) {
  if (!pm)
    return;
  plugins_shutdown(pm);
  free(pm);
}

void plugins_set_context(PluginManager *pm, const char *cwd,
                         const char *selected_name) {
  // Back-compat shim (internal only): feed the richer setter with minimal info.
  PluginsContext ctx = {
      .cwd = cwd,
      .selected_name = selected_name,
      .cursor_index = -1,
      .list_count = 0,
      .select_all_active = false,
      .search_active = false,
      .search_query = "",
      .active_pane = 0,
      .view = NULL,
  };
  plugins_set_context_ex(pm, &ctx);
}

static void call_void2_str(PluginManager *pm, cs_vm *vm, const char *fn,
                           const char *a, const char *b) {
  if (!pm || !vm || !fn)
    return;
  cs_value args[2];
  args[0] = cs_str(vm, a ? a : "");
  args[1] = cs_str(vm, b ? b : "");
  cs_value out = cs_nil();
  int rc = cs_call(vm, fn, 2, args, &out);
  if (rc != 0) {
    const char *err = cs_vm_last_error(vm);
    if (err && *err) {
      pm_notify(err);
      hold_notification_for_ms(5000);
      cs_error(vm, "");
    }
  }
  cs_value_release(args[0]);
  cs_value_release(args[1]);
  cs_value_release(out);
}

static void call_void1_str(PluginManager *pm, cs_vm *vm, const char *fn,
                           const char *a) {
  if (!pm || !vm || !fn)
    return;
  cs_value args[1];
  args[0] = cs_str(vm, a ? a : "");
  cs_value out = cs_nil();
  int rc = cs_call(vm, fn, 1, args, &out);
  if (rc != 0) {
    const char *err = cs_vm_last_error(vm);
    if (err && *err) {
      pm_notify(err);
      hold_notification_for_ms(5000);
      cs_error(vm, "");
    }
  }
  cs_value_release(args[0]);
  cs_value_release(out);
}

static void dispatch_event2_str(PluginManager *pm, const char *event,
                                const char *a, const char *b) {
  if (!pm || !event || !*event)
    return;
  for (size_t i = 0; i < pm->event_bind_count; i++) {
    EventBinding *eb = &pm->event_bindings[i];
    if (!eb->vm)
      continue;
    if (strcmp(eb->event, event) != 0)
      continue;
    cs_value args[2];
    args[0] = cs_str(eb->vm, a ? a : "");
    args[1] = cs_str(eb->vm, b ? b : "");
    cs_value out = cs_nil();
    int rc = 0;
    if (eb->cb_is_name && eb->cb_name[0]) {
      rc = cs_call(eb->vm, eb->cb_name, 2, args, &out);
    } else if (!eb->cb_is_name &&
               (eb->cb.type == CS_T_FUNC || eb->cb.type == CS_T_NATIVE)) {
      rc = cs_call_value(eb->vm, eb->cb, 2, args, &out);
    }
    if (rc != 0) {
      const char *err = cs_vm_last_error(eb->vm);
      if (err && *err) {
        pm_notify(err);
        hold_notification_for_ms(5000);
        cs_error(eb->vm, "");
      }
    }
    cs_value_release(args[0]);
    cs_value_release(args[1]);
    cs_value_release(out);
  }
}

void plugins_set_context_ex(PluginManager *pm, const PluginsContext *ctx) {
  if (!pm || !ctx)
    return;

  const char *new_cwd = ctx->cwd ? ctx->cwd : "";
  const char *new_sel = ctx->selected_name ? ctx->selected_name : "";

  bool cwd_changed = pm->context_initialized &&
                     (strncmp(pm->cwd, new_cwd, sizeof(pm->cwd)) != 0);
  bool sel_changed =
      pm->context_initialized &&
      (strncmp(pm->selected, new_sel, sizeof(pm->selected)) != 0);
  int old_pane_val = pm->active_pane;
  bool pane_changed =
      pm->context_initialized && (pm->active_pane != ctx->active_pane);

  char old_cwd[MAX_PATH_LENGTH];
  char old_sel[MAX_PATH_LENGTH];
  strncpy(old_cwd, pm->cwd, sizeof(old_cwd) - 1);
  old_cwd[sizeof(old_cwd) - 1] = '\0';
  strncpy(old_sel, pm->selected, sizeof(old_sel) - 1);
  old_sel[sizeof(old_sel) - 1] = '\0';

  strncpy(pm->cwd, new_cwd, sizeof(pm->cwd) - 1);
  pm->cwd[sizeof(pm->cwd) - 1] = '\0';
  strncpy(pm->selected, new_sel, sizeof(pm->selected) - 1);
  pm->selected[sizeof(pm->selected) - 1] = '\0';

  pm->cursor_index = ctx->cursor_index;
  pm->list_count = ctx->list_count;
  pm->select_all_active = ctx->select_all_active;
  pm->search_active = ctx->search_active;
  strncpy(pm->search_query, ctx->search_query ? ctx->search_query : "",
          sizeof(pm->search_query) - 1);
  pm->search_query[sizeof(pm->search_query) - 1] = '\0';
  pm->active_pane = ctx->active_pane;
  pm->view = ctx->view;

  if (!pm->context_initialized) {
    // Don't fire change hooks during the initial startup context population
    // so "Loaded plugin: ..." messages don't get immediately overwritten.
    pm->context_initialized = true;
    return;
  }

  // Change hooks (best-effort). Fired on next input loop after state changes.
  if (cwd_changed) {
    for (size_t i = 0; i < pm->plugin_count; i++) {
      call_void2_str(pm, pm->plugins[i].vm, "on_dir_change", pm->cwd, old_cwd);
    }
    dispatch_event2_str(pm, "dir_change", pm->cwd, old_cwd);
  }
  if (sel_changed) {
    for (size_t i = 0; i < pm->plugin_count; i++) {
      call_void2_str(pm, pm->plugins[i].vm, "on_selection_change", pm->selected,
                     old_sel);
    }
    dispatch_event2_str(pm, "selection_change", pm->selected, old_sel);
  }
  if (pane_changed) {
    const char *new_pane = (pm->active_pane == 1)   ? "directory"
                           : (pm->active_pane == 2) ? "preview"
                                                    : "unknown";
    const char *old_pane = (old_pane_val == 1)   ? "directory"
                           : (old_pane_val == 2) ? "preview"
                                                 : "unknown";
    dispatch_event2_str(pm, "pane_change", new_pane, old_pane);
  }
}

static bool call_bool(PluginManager *pm, cs_vm *vm, const char *fn, int key) {
  if (!vm || !fn)
    return false;
  char keybuf[32];
  const char *keyname = plugins_keycode_to_name_local(key, keybuf);
  cs_value args[1];
  args[0] = cs_str(vm, keyname);
  cs_value out = cs_nil();
  int rc = cs_call(vm, fn, 1, args, &out);
  bool handled = false;
  if (rc == 0 && out.type == CS_T_BOOL)
    handled = (out.as.b != 0);
  if (rc != 0) {
    const char *err = cs_vm_last_error(vm);
    if (err && *err) {
      pm_notify(err);
      // Plugin errors are important; keep them visible long enough to read.
      hold_notification_for_ms(5000);
      // Clear so we don't spam the same error every keypress.
      cs_error(vm, "");
    }
  }
  cs_value_release(args[0]);
  cs_value_release(out);
  return handled;
}

bool plugins_handle_key(PluginManager *pm, int key) {
  if (!pm)
    return false;

  // 1) Explicit key bindings
  for (size_t i = 0; i < pm->bind_count; i++) {
    if (pm->bindings[i].key != key)
      continue;
    bool handled = call_bool(pm, pm->bindings[i].vm, pm->bindings[i].func, key);
    if (pm->quit_requested)
      return true;
    if (!is_editing && pm->reload_requested)
      return true;
    if (handled)
      return true;
  }

  // 2) Conventional per-plugin on_key(key) handler
  for (size_t i = 0; i < pm->plugin_count; i++) {
    bool handled = call_bool(pm, pm->plugins[i].vm, "on_key", key);
    if (pm->quit_requested)
      return true;
    if (!is_editing && pm->reload_requested)
      return true;
    if (handled)
      return true;
  }
  return false;
}

bool plugins_take_reload_request(PluginManager *pm) {
  if (!pm)
    return false;
  bool v = pm->reload_requested;
  pm->reload_requested = false;
  return v;
}

bool plugins_take_quit_request(PluginManager *pm) {
  if (!pm)
    return false;
  bool v = pm->quit_requested;
  pm->quit_requested = false;
  return v;
}

void plugins_request_reload(PluginManager *pm) {
  if (!pm)
    return;
  pm->reload_requested = true;
}

void plugins_request_select(PluginManager *pm, const char *name) {
  if (!pm || !name || !*name)
    return;
  strncpy(pm->select_name, name, sizeof(pm->select_name) - 1);
  pm->select_name[sizeof(pm->select_name) - 1] = '\0';
  pm->select_requested = true;
}

void plugins_poll(PluginManager *pm) {
  if (!pm || !pm->ui_pending || pm->ui_kind == UI_KIND_NONE)
    return;
  if (!pm->ui_vm) {
    pm->ui_pending = false;
    pm->ui_kind = UI_KIND_NONE;
    return;
  }

  cs_vm *vm = pm->ui_vm;
  cs_value arg = cs_nil();
  if (pm->ui_kind == UI_KIND_PROMPT) {
    arg = plugin_modal_prompt_text(vm, pm->ui_title, "", pm->ui_initial);
  } else if (pm->ui_kind == UI_KIND_CONFIRM) {
    arg = cs_bool(plugin_modal_confirm(pm->ui_title, pm->ui_msg) ? 1 : 0);
  } else if (pm->ui_kind == UI_KIND_MENU) {
    int idx = plugin_modal_menu(pm->ui_title, pm->ui_items, pm->ui_item_count);
    arg = cs_int(idx);
  }

  cs_value rv = cs_nil();
  int rc = 0;
  if (pm->ui_cb_is_name) {
    rc = cs_call(vm, pm->ui_cb_name, 1, &arg, &rv);
  } else if (pm->ui_cb.type == CS_T_FUNC || pm->ui_cb.type == CS_T_NATIVE) {
    rc = cs_call_value(vm, pm->ui_cb, 1, &arg, &rv);
  }
  if (rc != 0) {
    const char *err = cs_vm_last_error(vm);
    if (err && *err) {
      pm_notify(err);
      hold_notification_for_ms(5000);
      cs_error(vm, "");
    }
  }
  cs_value_release(arg);
  cs_value_release(rv);

  // Clear request
  pm->ui_pending = false;
  pm->ui_kind = UI_KIND_NONE;
  pm->ui_title[0] = '\0';
  pm->ui_msg[0] = '\0';
  pm->ui_initial[0] = '\0';
  plugin_menu_items_free(pm->ui_items, pm->ui_item_count);
  pm->ui_items = NULL;
  pm->ui_item_count = 0;
  pm->ui_vm = NULL;
  if (pm->ui_cb.type != CS_T_NIL)
    cs_value_release(pm->ui_cb);
  pm->ui_cb = cs_nil();
  pm->ui_cb_is_name = false;
  pm->ui_cb_name[0] = '\0';
}

bool plugins_take_cd_request(PluginManager *pm, char *out_path,
                             size_t out_len) {
  if (!pm || !out_path || out_len == 0)
    return false;
  if (!pm->cd_requested)
    return false;
  strncpy(out_path, pm->cd_path, out_len - 1);
  out_path[out_len - 1] = '\0';
  pm->cd_requested = false;
  pm->cd_path[0] = '\0';
  return true;
}

bool plugins_take_select_request(PluginManager *pm, char *out_name,
                                 size_t out_len) {
  if (!pm || !out_name || out_len == 0)
    return false;
  if (!pm->select_requested)
    return false;
  strncpy(out_name, pm->select_name, out_len - 1);
  out_name[out_len - 1] = '\0';
  pm->select_requested = false;
  pm->select_name[0] = '\0';
  return true;
}

bool plugins_take_select_index_request(PluginManager *pm, int *out_index) {
  if (!pm || !out_index)
    return false;
  if (!pm->select_index_requested)
    return false;
  *out_index = pm->select_index;
  pm->select_index_requested = false;
  pm->select_index = -1;
  return true;
}

bool plugins_take_open_selected_request(PluginManager *pm) {
  if (!pm)
    return false;
  bool v = pm->open_selected_requested;
  pm->open_selected_requested = false;
  return v;
}

bool plugins_take_open_path_request(PluginManager *pm, char *out_path,
                                    size_t out_len) {
  if (!pm || !out_path || out_len == 0)
    return false;
  if (!pm->open_path_requested)
    return false;
  strncpy(out_path, pm->open_path, out_len - 1);
  out_path[out_len - 1] = '\0';
  pm->open_path_requested = false;
  pm->open_path[0] = '\0';
  return true;
}

bool plugins_take_preview_path_request(PluginManager *pm, char *out_path,
                                       size_t out_len) {
  if (!pm || !out_path || out_len == 0)
    return false;
  if (!pm->preview_path_requested)
    return false;
  strncpy(out_path, pm->preview_path, out_len - 1);
  out_path[out_len - 1] = '\0';
  pm->preview_path_requested = false;
  pm->preview_path[0] = '\0';
  return true;
}

bool plugins_take_enter_dir_request(PluginManager *pm) {
  if (!pm)
    return false;
  bool v = pm->enter_dir_requested;
  pm->enter_dir_requested = false;
  return v;
}

bool plugins_take_parent_dir_request(PluginManager *pm) {
  if (!pm)
    return false;
  bool v = pm->parent_dir_requested;
  pm->parent_dir_requested = false;
  return v;
}

bool plugins_take_set_search_request(PluginManager *pm, char *out_query,
                                     size_t out_len) {
  if (!pm || !out_query || out_len == 0)
    return false;
  if (!pm->set_search_requested)
    return false;
  strncpy(out_query, pm->requested_search_query, out_len - 1);
  out_query[out_len - 1] = '\0';
  pm->set_search_requested = false;
  pm->requested_search_query[0] = '\0';
  return true;
}

bool plugins_take_clear_search_request(PluginManager *pm) {
  if (!pm)
    return false;
  bool v = pm->clear_search_requested;
  pm->clear_search_requested = false;
  return v;
}

bool plugins_take_set_search_mode_request(PluginManager *pm, int *out_mode) {
  if (!pm || !out_mode)
    return false;
  if (!pm->set_search_mode_requested)
    return false;
  *out_mode = pm->requested_search_mode;
  pm->set_search_mode_requested = false;
  pm->requested_search_mode = SEARCH_MODE_FUZZY;
  return true;
}

bool plugins_take_fileop_request(PluginManager *pm, PluginFileOp *out) {
  if (!pm || !out)
    return false;
  if (!pm->fileop_requested)
    return false;

  // Transfer ownership.
  *out = pm->op;
  pm->op.kind = PLUGIN_FILEOP_NONE;
  pm->op.paths = NULL;
  pm->op.count = 0;
  pm->op.arg1[0] = '\0';
  pm->fileop_requested = false;
  return true;
}

void plugins_notify_editor_open(PluginManager *pm, const char *path) {
  if (!pm || !path)
    return;

  // Notify all plugins that a file was opened in the editor
  for (size_t i = 0; i < pm->plugin_count; i++) {
    call_void1_str(pm, pm->plugins[i].vm, "on_editor_open", path);
  }
}

void plugins_notify_editor_change(PluginManager *pm, int line, int col,
                                  const char *text) {
  if (!pm)
    return;

  // Notify all plugins that the editor content changed
  for (size_t i = 0; i < pm->plugin_count; i++) {
    cs_vm *vm = pm->plugins[i].vm;
    if (!vm)
      continue;

    // Call on_editor_change(line, col, text) if it exists
    cs_value args[3];
    args[0] = cs_int(line);
    args[1] = cs_int(col);
    args[2] = text ? cs_str(vm, text) : cs_str(vm, "");

    cs_value result;
    cs_call(vm, "on_editor_change", 3, args, &result);
  }
}

void plugins_notify_editor_save(PluginManager *pm, const char *path) {
  if (!pm || !path)
    return;

  // Notify all plugins that a file was saved in the editor
  for (size_t i = 0; i < pm->plugin_count; i++) {
    call_void1_str(pm, pm->plugins[i].vm, "on_editor_save", path);
  }
}

void plugins_notify_editor_cursor_move(PluginManager *pm, int old_line,
                                       int old_col, int new_line, int new_col) {
  if (!pm)
    return;

  // Notify all plugins that the cursor moved in the editor
  for (size_t i = 0; i < pm->plugin_count; i++) {
    cs_vm *vm = pm->plugins[i].vm;
    if (!vm)
      continue;

    // Call on_editor_cursor_move(old_line, old_col, new_line, new_col) if it
    // exists
    cs_value args[4];
    args[0] = cs_int(old_line);
    args[1] = cs_int(old_col);
    args[2] = cs_int(new_line);
    args[3] = cs_int(new_col);

    cs_value result = cs_nil();
    int rc = cs_call(vm, "on_editor_cursor_move", 4, args, &result);
    if (rc != 0) {
      const char *err = cs_vm_last_error(vm);
      if (err && *err) {
        // Silently ignore errors for optional callbacks
        cs_error(vm, "");
      }
    }
    cs_value_release(result);
  }
}
