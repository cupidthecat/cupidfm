// plugins.c
#define _POSIX_C_SOURCE 200112L

#include "plugins.h"

#include <dirent.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdio.h>

#include "globals.h"
#include "main.h"   // show_notification, draw_scrolling_banner globals
#include "ui.h"     // show_popup
#include "utils.h"  // path_join

#include "cupidscript.h"
#include "cs_vm.h"

typedef struct {
    cs_vm *vm;
    char  *path;
} Plugin;

typedef struct {
    int key;
    cs_vm *vm;
    char *func;
} KeyBinding;

struct PluginManager {
    Plugin *plugins;
    size_t plugin_count;
    size_t plugin_cap;

    KeyBinding *bindings;
    size_t bind_count;
    size_t bind_cap;

    char cwd[MAX_PATH_LENGTH];
    char selected[MAX_PATH_LENGTH];

    bool reload_requested;
    bool quit_requested;
};

static bool ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) return false;
    size_t sl = strlen(s), su = strlen(suffix);
    if (su > sl) return false;
    return memcmp(s + (sl - su), suffix, su) == 0;
}

static bool ensure_dir(const char *path) {
    if (!path || !*path) return false;
    if (mkdir(path, 0700) == 0) return true;
    return errno == EEXIST;
}

static const char *keycode_to_name_local(int keycode, char buf[32]) {
    if (!buf) return "UNKNOWN";

    // Function keys
    if (keycode >= KEY_F(1) && keycode <= KEY_F(63)) {
        int func_num = keycode - (KEY_F(1) - 1);
        snprintf(buf, 32, "F%d", func_num);
        return buf;
    }

    // Control characters: Ctrl+A..Ctrl+Z
    if (keycode >= 1 && keycode <= 26) {
        snprintf(buf, 32, "^%c", 'A' + (keycode - 1));
        return buf;
    }

    switch (keycode) {
        case KEY_UP: return "KEY_UP";
        case KEY_DOWN: return "KEY_DOWN";
        case KEY_LEFT: return "KEY_LEFT";
        case KEY_RIGHT: return "KEY_RIGHT";
        case KEY_BACKSPACE: return "KEY_BACKSPACE";
        case '\t': return "Tab";
        default: break;
    }

    // Printable ASCII
    if (keycode >= 32 && keycode <= 126) {
        buf[0] = (char)keycode;
        buf[1] = '\0';
        return buf;
    }

    return "UNKNOWN";
}

static int parse_key_name_local(const char *s) {
    if (!s || !*s) return -1;

    // Ctrl sequences: ^A..^Z
    if (s[0] == '^' && s[1] && !s[2]) {
        char c = s[1];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c >= 'A' && c <= 'Z') return (c - 'A') + 1;
        return -1;
    }

    if (strncmp(s, "F", 1) == 0 && s[1]) {
        char *end = NULL;
        long n = strtol(s + 1, &end, 10);
        if (end && *end == '\0' && n >= 1 && n <= 63) {
            return KEY_F((int)n);
        }
    }

    if (strcmp(s, "KEY_UP") == 0) return KEY_UP;
    if (strcmp(s, "KEY_DOWN") == 0) return KEY_DOWN;
    if (strcmp(s, "KEY_LEFT") == 0) return KEY_LEFT;
    if (strcmp(s, "KEY_RIGHT") == 0) return KEY_RIGHT;
    if (strcmp(s, "KEY_BACKSPACE") == 0) return KEY_BACKSPACE;
    if (strcmp(s, "Tab") == 0) return '\t';

    // Single printable char
    if (s[0] && !s[1]) return (unsigned char)s[0];

    return -1;
}

static void pm_notify(const char *msg) {
    if (!msg) msg = "";
    if (notifwin) {
        show_notification(notifwin, "%s", msg);
        should_clear_notif = false;
    }
}

static int nf_fm_notify(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    (void)pm;
    if (argc == 1 && argv[0].type == CS_T_STR) {
        pm_notify(cs_to_cstr(argv[0]));
    }
    if (out) *out = cs_nil();
    return 0;
}

static int nf_fm_popup(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    (void)ud;
    const char *title = "Plugin";
    const char *msg = "";
    if (argc >= 1 && argv[0].type == CS_T_STR) title = cs_to_cstr(argv[0]);
    if (argc >= 2 && argv[1].type == CS_T_STR) msg = cs_to_cstr(argv[1]);
    show_popup(title, "%s", msg);
    if (out) *out = cs_nil();
    return 0;
}

static int nf_fm_cwd(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    if (out) *out = cs_str(vm, pm ? pm->cwd : "");
    return 0;
}

static int nf_fm_selected_name(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    if (out) *out = cs_str(vm, pm ? pm->selected : "");
    return 0;
}

static int nf_fm_selected_path(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    char full[MAX_PATH_LENGTH];
    full[0] = '\0';
    if (pm && pm->cwd[0] && pm->selected[0]) {
        path_join(full, pm->cwd, pm->selected);
    }
    if (out) *out = cs_str(vm, full);
    return 0;
}

static int nf_fm_reload(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    if (pm) pm->reload_requested = true;
    if (out) *out = cs_nil();
    return 0;
}

static int nf_fm_exit(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    if (pm) pm->quit_requested = true;
    if (out) *out = cs_nil();
    return 0;
}

static int nf_fm_key_name(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud;
    if (!out) return 0;
    if (argc == 1 && argv[0].type == CS_T_INT) {
        char buf[32];
        const char *name = keycode_to_name_local((int)argv[0].as.i, buf);
        *out = cs_str(vm, name);
        return 0;
    }
    *out = cs_str(vm, "UNKNOWN");
    return 0;
}

static int nf_fm_key_code(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    if (argc == 1 && argv[0].type == CS_T_STR) {
        int key = parse_key_name_local(cs_to_cstr(argv[0]));
        if (key == -1) {
            *out = cs_int(-1);
        } else {
            *out = cs_int(key);
        }
        return 0;
    }
    *out = cs_int(-1);
    return 0;
}

static bool binding_append(PluginManager *pm, int key, cs_vm *vm, const char *func) {
    if (!pm || !vm || !func || !*func) return false;
    if (pm->bind_count == pm->bind_cap) {
        size_t new_cap = pm->bind_cap ? pm->bind_cap * 2 : 8;
        KeyBinding *nb = (KeyBinding *)realloc(pm->bindings, new_cap * sizeof(KeyBinding));
        if (!nb) return false;
        pm->bindings = nb;
        pm->bind_cap = new_cap;
    }
    pm->bindings[pm->bind_count].key = key;
    pm->bindings[pm->bind_count].vm = vm;
    pm->bindings[pm->bind_count].func = strdup(func);
    if (!pm->bindings[pm->bind_count].func) return false;
    pm->bind_count++;
    return true;
}

static int nf_fm_bind(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 2 && argv[1].type == CS_T_STR) {
        int key = -1;
        if (argv[0].type == CS_T_INT) {
            key = (int)argv[0].as.i;
        } else if (argv[0].type == CS_T_STR) {
            key = parse_key_name_local(cs_to_cstr(argv[0]));
        }
        if (key != -1) {
            ok = binding_append(pm, key, vm, cs_to_cstr(argv[1])) ? 1 : 0;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static void register_fm_api(PluginManager *pm, cs_vm *vm) {
    cs_register_stdlib(vm);
    cs_register_native(vm, "fm.notify", nf_fm_notify, pm);
    cs_register_native(vm, "fm.status", nf_fm_notify, pm); // alias
    cs_register_native(vm, "fm.popup", nf_fm_popup, pm);
    cs_register_native(vm, "fm.cwd", nf_fm_cwd, pm);
    cs_register_native(vm, "fm.selected_name", nf_fm_selected_name, pm);
    cs_register_native(vm, "fm.selected_path", nf_fm_selected_path, pm);
    cs_register_native(vm, "fm.reload", nf_fm_reload, pm);
    cs_register_native(vm, "fm.exit", nf_fm_exit, pm);
    cs_register_native(vm, "fm.bind", nf_fm_bind, pm);
    cs_register_native(vm, "fm.key_name", nf_fm_key_name, pm);
    cs_register_native(vm, "fm.key_code", nf_fm_key_code, pm);
}

static bool plugin_append(PluginManager *pm, cs_vm *vm, const char *path) {
    if (pm->plugin_count == pm->plugin_cap) {
        size_t new_cap = pm->plugin_cap ? pm->plugin_cap * 2 : 8;
        Plugin *np = (Plugin *)realloc(pm->plugins, new_cap * sizeof(Plugin));
        if (!np) return false;
        pm->plugins = np;
        pm->plugin_cap = new_cap;
    }
    pm->plugins[pm->plugin_count].vm = vm;
    pm->plugins[pm->plugin_count].path = strdup(path ? path : "");
    if (!pm->plugins[pm->plugin_count].path) return false;
    pm->plugin_count++;
    return true;
}

static void load_plugins_from_dir(PluginManager *pm, const char *dir_path) {
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (!ends_with(ent->d_name, ".cs")) continue;

        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);

        cs_vm *vm = cs_vm_new();
        if (!vm) {
            pm_notify("Plugin VM alloc failed");
            continue;
        }
        register_fm_api(pm, vm);

        int rc = cs_vm_run_file(vm, full);
        if (rc != 0) {
            const char *err = cs_vm_last_error(vm);
            char msg[512];
            snprintf(msg, sizeof(msg), "Plugin load failed: %s: %s", ent->d_name, err ? err : "");
            pm_notify(msg);
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
        (void)cs_call(vm, "on_load", 0, NULL, &out);
        cs_value_release(out);
    }

    closedir(d);
}

static void plugins_init(PluginManager *pm) {
    memset(pm, 0, sizeof(*pm));
    pm->cwd[0] = '\0';
    pm->selected[0] = '\0';

    // Candidate plugin dirs:
    // 1) ~/.cupidfm/plugins
    // 2) ~/.cupidfm/plugin (legacy/singular)
    // 3) ./cupidfm/plugins
    // 4) ./cupidfm/plugin (legacy/singular)
    // 5) ./plugins
    const char *home = getenv("HOME");
    if (home && *home) {
        char base[PATH_MAX];
        snprintf(base, sizeof(base), "%s/.cupidfm", home);
        (void)ensure_dir(base);
        char dir[PATH_MAX];
        snprintf(dir, sizeof(dir), "%s/plugins", base);
        (void)ensure_dir(dir);
        load_plugins_from_dir(pm, dir);

        char dir2[PATH_MAX];
        snprintf(dir2, sizeof(dir2), "%s/plugin", base);
        load_plugins_from_dir(pm, dir2);
    }

    load_plugins_from_dir(pm, "./cupidfm/plugins");
    load_plugins_from_dir(pm, "./cupidfm/plugin");
    load_plugins_from_dir(pm, "./plugins");
}

static void plugins_shutdown(PluginManager *pm) {
    if (!pm) return;
    for (size_t i = 0; i < pm->bind_count; i++) {
        free(pm->bindings[i].func);
    }
    free(pm->bindings);
    pm->bindings = NULL;
    pm->bind_count = pm->bind_cap = 0;

    for (size_t i = 0; i < pm->plugin_count; i++) {
        if (pm->plugins[i].vm) cs_vm_free(pm->plugins[i].vm);
        free(pm->plugins[i].path);
    }
    free(pm->plugins);
    pm->plugins = NULL;
    pm->plugin_count = pm->plugin_cap = 0;

    pm->reload_requested = false;
    pm->quit_requested = false;
    pm->cwd[0] = '\0';
    pm->selected[0] = '\0';
}

PluginManager *plugins_create(void) {
    PluginManager *pm = (PluginManager *)calloc(1, sizeof(PluginManager));
    if (!pm) return NULL;
    plugins_init(pm);
    return pm;
}

void plugins_destroy(PluginManager *pm) {
    if (!pm) return;
    plugins_shutdown(pm);
    free(pm);
}

void plugins_set_context(PluginManager *pm, const char *cwd, const char *selected_name) {
    if (!pm) return;
    if (cwd) {
        strncpy(pm->cwd, cwd, sizeof(pm->cwd) - 1);
        pm->cwd[sizeof(pm->cwd) - 1] = '\0';
    } else {
        pm->cwd[0] = '\0';
    }
    if (selected_name) {
        strncpy(pm->selected, selected_name, sizeof(pm->selected) - 1);
        pm->selected[sizeof(pm->selected) - 1] = '\0';
    } else {
        pm->selected[0] = '\0';
    }
}

static bool call_bool(PluginManager *pm, cs_vm *vm, const char *fn, int key) {
    if (!vm || !fn) return false;
    char keybuf[32];
    const char *keyname = keycode_to_name_local(key, keybuf);
    cs_value args[1];
    args[0] = cs_str(vm, keyname);
    cs_value out = cs_nil();
    int rc = cs_call(vm, fn, 1, args, &out);
    bool handled = false;
    if (rc == 0 && out.type == CS_T_BOOL) handled = (out.as.b != 0);
    if (rc != 0) {
        const char *err = cs_vm_last_error(vm);
        if (err && *err) {
            pm_notify(err);
            // Clear so we don't spam the same error every keypress.
            cs_error(vm, "");
        }
    }
    cs_value_release(args[0]);
    cs_value_release(out);
    return handled;
}

bool plugins_handle_key(PluginManager *pm, int key) {
    if (!pm) return false;

    // 1) Explicit key bindings
    for (size_t i = 0; i < pm->bind_count; i++) {
        if (pm->bindings[i].key != key) continue;
        bool handled = call_bool(pm, pm->bindings[i].vm, pm->bindings[i].func, key);
        if (pm->quit_requested) return true;
        if (pm->reload_requested) return true;
        if (handled) return true;
    }

    // 2) Conventional per-plugin on_key(key) handler
    for (size_t i = 0; i < pm->plugin_count; i++) {
        bool handled = call_bool(pm, pm->plugins[i].vm, "on_key", key);
        if (pm->quit_requested) return true;
        if (pm->reload_requested) return true;
        if (handled) return true;
    }
    return false;
}

bool plugins_take_reload_request(PluginManager *pm) {
    if (!pm) return false;
    bool v = pm->reload_requested;
    pm->reload_requested = false;
    return v;
}

bool plugins_take_quit_request(PluginManager *pm) {
    if (!pm) return false;
    bool v = pm->quit_requested;
    pm->quit_requested = false;
    return v;
}
