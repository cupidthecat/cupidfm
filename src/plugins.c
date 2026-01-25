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
#include <ctype.h>
#include <time.h>

#include "globals.h"
#include "main.h"   // show_notification, draw_scrolling_banner globals
#include "ui.h"     // show_popup
#include "utils.h"  // path_join
#include "console.h"

#include "cupidscript.h"
#include "cs_vm.h"
#include "cs_value.h"

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

    int cursor_index;
    int list_count;
    bool search_active;
    char search_query[MAX_PATH_LENGTH];
    int active_pane; // 1=directory, 2=preview
    bool context_initialized;

    bool reload_requested;
    bool quit_requested;

    bool cd_requested;
    char cd_path[MAX_PATH_LENGTH];

    bool select_requested;
    char select_name[MAX_PATH_LENGTH];

    bool select_index_requested;
    int select_index;

    bool fileop_requested;
    PluginFileOp op;

    // Async prompt UI (modal). One request at a time.
    bool ui_pending;
    enum { UI_KIND_NONE = 0, UI_KIND_PROMPT, UI_KIND_CONFIRM, UI_KIND_MENU } ui_kind;
    char ui_title[128];
    char ui_msg[512];
    char ui_initial[256];
    char **ui_items;      // menu items (owned)
    size_t ui_item_count;
    cs_vm *ui_vm;         // callback target VM
    bool ui_cb_is_name;
    char ui_cb_name[64];
    cs_value ui_cb;       // function/native (refcounted via cs_value_copy)
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

static void banner_tick_for_modal(struct timespec *last_banner_update, int total_scroll_length) {
    if (!last_banner_update) return;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long banner_time_diff = (now.tv_sec - last_banner_update->tv_sec) * 1000000 +
                            (now.tv_nsec - last_banner_update->tv_nsec) / 1000;
    if (banner_time_diff >= BANNER_SCROLL_INTERVAL && BANNER_TEXT && bannerwin) {
        pthread_mutex_lock(&banner_mutex);
        draw_scrolling_banner(bannerwin, BANNER_TEXT, BUILD_INFO, banner_offset);
        pthread_mutex_unlock(&banner_mutex);
        banner_offset = (banner_offset + 1) % total_scroll_length;
        *last_banner_update = now;
    }
}

static cs_value modal_prompt_text(cs_vm *vm, const char *title, const char *initial) {
    // Returns cs_str(...) or cs_nil() if cancelled.
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int popup_height = 7;
    int popup_width = max_x > 10 ? (max_x < 80 ? max_x - 2 : 78) : 10;
    int starty = (max_y - popup_height) / 2;
    int startx = (max_x - popup_width) / 2;
    if (starty < 0) starty = 0;
    if (startx < 0) startx = 0;

    WINDOW *popup = newwin(popup_height, popup_width, starty, startx);
    if (!popup) return cs_nil();
    keypad(popup, TRUE);
    box(popup, 0, 0);

    char buf[256];
    buf[0] = '\0';
    if (initial && *initial) {
        strncpy(buf, initial, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    }
    int len = (int)strlen(buf);

    // Non-blocking so banner keeps animating
    wtimeout(popup, 10);
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = (COLS - 2) + (BANNER_TEXT ? (int)strlen(BANNER_TEXT) : 0) +
                              (BUILD_INFO ? (int)strlen(BUILD_INFO) : 0) +
                              BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

    bool cancelled = false;
    for (;;) {
        werase(popup);
        box(popup, 0, 0);
        mvwprintw(popup, 0, 2, "[ %.*s ]", popup_width - 6, title ? title : "Prompt");

        mvwprintw(popup, 2, 2, "> %.*s", popup_width - 6, buf);
        mvwprintw(popup, 4, 2, "Enter=OK  Esc=Cancel  Backspace=Delete");
        wrefresh(popup);

        int ch = wgetch(popup);
        if (ch == ERR) {
            banner_tick_for_modal(&last_banner_update, total_scroll_length);
            napms(10);
            continue;
        }
        if (ch == 27) { // Esc
            cancelled = true;
            break;
        }
        if (ch == '\n' || ch == KEY_ENTER) {
            break;
        }
        if (ch == KEY_BACKSPACE || ch == 127) {
            if (len > 0) {
                buf[--len] = '\0';
            }
            continue;
        }
        if (ch >= 32 && ch <= 126) {
            if (len < (int)sizeof(buf) - 1) {
                buf[len++] = (char)ch;
                buf[len] = '\0';
            }
            continue;
        }
    }

    wtimeout(popup, -1);
    werase(popup);
    wrefresh(popup);
    delwin(popup);
    touchwin(stdscr);
    refresh();

    if (cancelled) return cs_nil();
    return cs_str(vm, buf);
}

static bool modal_confirm(const char *title, const char *msg) {
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int popup_height = 7;
    int popup_width = max_x > 10 ? (max_x < 90 ? max_x - 2 : 88) : 10;
    int starty = (max_y - popup_height) / 2;
    int startx = (max_x - popup_width) / 2;
    if (starty < 0) starty = 0;
    if (startx < 0) startx = 0;

    WINDOW *popup = newwin(popup_height, popup_width, starty, startx);
    if (!popup) return false;
    keypad(popup, TRUE);
    box(popup, 0, 0);

    wtimeout(popup, 10);
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = (COLS - 2) + (BANNER_TEXT ? (int)strlen(BANNER_TEXT) : 0) +
                              (BUILD_INFO ? (int)strlen(BUILD_INFO) : 0) +
                              BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

    bool result = false;
    for (;;) {
        werase(popup);
        box(popup, 0, 0);
        mvwprintw(popup, 0, 2, "[ %.*s ]", popup_width - 6, title ? title : "Confirm");

        mvwprintw(popup, 2, 2, "%.*s", popup_width - 4, msg ? msg : "");
        mvwprintw(popup, 4, 2, "Y=Yes  N/Esc=No");
        wrefresh(popup);

        int ch = wgetch(popup);
        if (ch == ERR) {
            banner_tick_for_modal(&last_banner_update, total_scroll_length);
            napms(10);
            continue;
        }
        ch = tolower(ch);
        if (ch == 'y') {
            result = true;
            break;
        }
        if (ch == 'n' || ch == 27) {
            result = false;
            break;
        }
    }

    wtimeout(popup, -1);
    werase(popup);
    wrefresh(popup);
    delwin(popup);
    touchwin(stdscr);
    refresh();
    return result;
}

static int modal_menu(const char *title, char **items, size_t count) {
    if (!items || count == 0) return -1;

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int max_rows = max_y - 4;
    if (max_rows < 6) max_rows = 6;

    int visible = (int)count;
    if (visible > max_rows - 4) visible = max_rows - 4;
    if (visible < 1) visible = 1;

    size_t max_item_len = 0;
    for (size_t i = 0; i < count; i++) {
        size_t l = items[i] ? strlen(items[i]) : 0;
        if (l > max_item_len) max_item_len = l;
    }
    int popup_width = (int)max_item_len + 6;
    if (popup_width < 24) popup_width = 24;
    if (popup_width > max_x - 2) popup_width = max_x - 2;
    int popup_height = visible + 4;

    int starty = (max_y - popup_height) / 2;
    int startx = (max_x - popup_width) / 2;
    if (starty < 0) starty = 0;
    if (startx < 0) startx = 0;

    WINDOW *popup = newwin(popup_height, popup_width, starty, startx);
    if (!popup) return -1;
    keypad(popup, TRUE);
    box(popup, 0, 0);

    wtimeout(popup, 10);
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = (COLS - 2) + (BANNER_TEXT ? (int)strlen(BANNER_TEXT) : 0) +
                              (BUILD_INFO ? (int)strlen(BUILD_INFO) : 0) +
                              BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

    int sel = 0;
    int start = 0;
    for (;;) {
        if (sel < 0) sel = 0;
        if (sel >= (int)count) sel = (int)count - 1;
        if (sel < start) start = sel;
        if (sel >= start + visible) start = sel - visible + 1;
        if (start < 0) start = 0;

        werase(popup);
        box(popup, 0, 0);
        mvwprintw(popup, 0, 2, "[ %.*s ]", popup_width - 6, title ? title : "Menu");

        int row = 2;
        for (int i = 0; i < visible; i++) {
            int idx = start + i;
            if (idx >= (int)count) break;
            const char *txt = items[idx] ? items[idx] : "";
            if (idx == sel) wattron(popup, A_REVERSE);
            mvwprintw(popup, row + i, 2, "%.*s", popup_width - 4, txt);
            if (idx == sel) wattroff(popup, A_REVERSE);
        }

        mvwprintw(popup, popup_height - 2, 2, "Enter=Select  Esc=Cancel");
        wrefresh(popup);

        int ch = wgetch(popup);
        if (ch == ERR) {
            banner_tick_for_modal(&last_banner_update, total_scroll_length);
            napms(10);
            continue;
        }
        if (ch == 27) {
            sel = -1;
            break;
        }
        if (ch == '\n' || ch == KEY_ENTER) {
            break;
        }
        if (ch == KEY_UP) sel--;
        else if (ch == KEY_DOWN) sel++;
        else if (ch == KEY_PPAGE) sel -= visible;
        else if (ch == KEY_NPAGE) sel += visible;
    }

    wtimeout(popup, -1);
    werase(popup);
    wrefresh(popup);
    delwin(popup);
    touchwin(stdscr);
    refresh();
    return sel;
}

static void fileop_clear(PluginFileOp *op) {
    if (!op) return;
    for (size_t i = 0; i < op->count; i++) {
        free(op->paths[i]);
    }
    free(op->paths);
    op->paths = NULL;
    op->count = 0;
    op->kind = PLUGIN_FILEOP_NONE;
    op->arg1[0] = '\0';
}

void plugins_fileop_free(PluginFileOp *op) {
    fileop_clear(op);
}

static bool list_ensure_local(cs_list_obj *l, size_t need) {
    if (!l) return false;
    if (need <= l->cap) return true;
    size_t nc = l->cap ? l->cap : 8;
    while (nc < need) nc *= 2;
    cs_value *ni = (cs_value *)realloc(l->items, nc * sizeof(cs_value));
    if (!ni) return false;
    // Ensure new slots are nil.
    for (size_t i = l->cap; i < nc; i++) {
        ni[i] = cs_nil();
    }
    l->items = ni;
    l->cap = nc;
    return true;
}

static bool list_push_local(cs_list_obj *l, cs_value v) {
    if (!l) return false;
    if (!list_ensure_local(l, l->len + 1)) return false;
    l->items[l->len++] = cs_value_copy(v);
    return true;
}

static bool menu_items_from_value(cs_value v, char ***out_items, size_t *out_count) {
    if (!out_items || !out_count) return false;
    *out_items = NULL;
    *out_count = 0;

    if (v.type != CS_T_LIST) return false;
    cs_list_obj *l = (cs_list_obj *)v.as.p;
    if (!l || l->len == 0) return false;

    char **items = (char **)calloc(l->len, sizeof(char *));
    if (!items) return false;
    size_t count = 0;
    for (size_t i = 0; i < l->len; i++) {
        if (l->items[i].type != CS_T_STR) continue;
        const char *s = cs_to_cstr(l->items[i]);
        if (!s) s = "";
        items[count] = strdup(s);
        if (!items[count]) {
            for (size_t j = 0; j < count; j++) free(items[j]);
            free(items);
            return false;
        }
        count++;
    }
    if (count == 0) {
        free(items);
        return false;
    }
    char **shrunk = (char **)realloc(items, count * sizeof(char *));
    if (shrunk) items = shrunk;
    *out_items = items;
    *out_count = count;
    return true;
}

static void menu_items_free(char **items, size_t count) {
    if (!items) return;
    for (size_t i = 0; i < count; i++) free(items[i]);
    free(items);
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

static int nf_fm_prompt(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud;
    const char *title = "Prompt";
    const char *initial = "";
    if (argc >= 1 && argv[0].type == CS_T_STR) title = cs_to_cstr(argv[0]);
    if (argc >= 2 && argv[1].type == CS_T_STR) initial = cs_to_cstr(argv[1]);
    if (out) *out = modal_prompt_text(vm, title, initial);
    return 0;
}

static int nf_fm_confirm(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)ud;
    const char *title = "Confirm";
    const char *msg = "";
    if (argc >= 1 && argv[0].type == CS_T_STR) title = cs_to_cstr(argv[0]);
    if (argc >= 2 && argv[1].type == CS_T_STR) msg = cs_to_cstr(argv[1]);
    if (out) *out = cs_bool(modal_confirm(title, msg) ? 1 : 0);
    return 0;
}

static int nf_fm_menu(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)ud;
    if (!out) return 0;
    const char *title = "Menu";
    if (argc >= 1 && argv[0].type == CS_T_STR) title = cs_to_cstr(argv[0]);
    if (argc < 2) { *out = cs_int(-1); return 0; }

    char **items = NULL;
    size_t count = 0;
    if (!menu_items_from_value(argv[1], &items, &count)) {
        *out = cs_int(-1);
        return 0;
    }
    int idx = modal_menu(title, items, count);
    menu_items_free(items, count);
    *out = cs_int(idx);
    return 0;
}

static int nf_fm_console_print(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    (void)pm;
    if (argc == 1 && argv[0].type == CS_T_STR) {
        console_logf("%s", cs_to_cstr(argv[0]));
    }
    if (out) *out = cs_nil();
    return 0;
}

static int nf_fm_prompt_async(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && !pm->ui_pending && argc == 3 && argv[0].type == CS_T_STR && argv[1].type == CS_T_STR) {
        pm->ui_pending = true;
        pm->ui_kind = UI_KIND_PROMPT;
        strncpy(pm->ui_title, cs_to_cstr(argv[0]), sizeof(pm->ui_title) - 1);
        pm->ui_title[sizeof(pm->ui_title) - 1] = '\0';
        strncpy(pm->ui_initial, cs_to_cstr(argv[1]), sizeof(pm->ui_initial) - 1);
        pm->ui_initial[sizeof(pm->ui_initial) - 1] = '\0';
        pm->ui_msg[0] = '\0';
        pm->ui_items = NULL;
        pm->ui_item_count = 0;
        pm->ui_vm = vm;
        pm->ui_cb_is_name = false;
        pm->ui_cb_name[0] = '\0';
        pm->ui_cb = cs_nil();
        if (argv[2].type == CS_T_STR) {
            pm->ui_cb_is_name = true;
            strncpy(pm->ui_cb_name, cs_to_cstr(argv[2]), sizeof(pm->ui_cb_name) - 1);
            pm->ui_cb_name[sizeof(pm->ui_cb_name) - 1] = '\0';
            ok = 1;
        } else if (argv[2].type == CS_T_FUNC || argv[2].type == CS_T_NATIVE) {
            pm->ui_cb = cs_value_copy(argv[2]);
            ok = 1;
        } else {
            pm->ui_pending = false;
            pm->ui_kind = UI_KIND_NONE;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_confirm_async(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && !pm->ui_pending && argc == 3 && argv[0].type == CS_T_STR && argv[1].type == CS_T_STR) {
        pm->ui_pending = true;
        pm->ui_kind = UI_KIND_CONFIRM;
        strncpy(pm->ui_title, cs_to_cstr(argv[0]), sizeof(pm->ui_title) - 1);
        pm->ui_title[sizeof(pm->ui_title) - 1] = '\0';
        strncpy(pm->ui_msg, cs_to_cstr(argv[1]), sizeof(pm->ui_msg) - 1);
        pm->ui_msg[sizeof(pm->ui_msg) - 1] = '\0';
        pm->ui_initial[0] = '\0';
        pm->ui_items = NULL;
        pm->ui_item_count = 0;
        pm->ui_vm = vm;
        pm->ui_cb_is_name = false;
        pm->ui_cb_name[0] = '\0';
        pm->ui_cb = cs_nil();
        if (argv[2].type == CS_T_STR) {
            pm->ui_cb_is_name = true;
            strncpy(pm->ui_cb_name, cs_to_cstr(argv[2]), sizeof(pm->ui_cb_name) - 1);
            pm->ui_cb_name[sizeof(pm->ui_cb_name) - 1] = '\0';
            ok = 1;
        } else if (argv[2].type == CS_T_FUNC || argv[2].type == CS_T_NATIVE) {
            pm->ui_cb = cs_value_copy(argv[2]);
            ok = 1;
        } else {
            pm->ui_pending = false;
            pm->ui_kind = UI_KIND_NONE;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_menu_async(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && !pm->ui_pending && argc == 3 && argv[0].type == CS_T_STR) {
        char **items = NULL;
        size_t count = 0;
        if (menu_items_from_value(argv[1], &items, &count)) {
            pm->ui_pending = true;
            pm->ui_kind = UI_KIND_MENU;
            strncpy(pm->ui_title, cs_to_cstr(argv[0]), sizeof(pm->ui_title) - 1);
            pm->ui_title[sizeof(pm->ui_title) - 1] = '\0';
            pm->ui_msg[0] = '\0';
            pm->ui_initial[0] = '\0';
            pm->ui_items = items;
            pm->ui_item_count = count;
            pm->ui_vm = vm;
            pm->ui_cb_is_name = false;
            pm->ui_cb_name[0] = '\0';
            pm->ui_cb = cs_nil();
            if (argv[2].type == CS_T_STR) {
                pm->ui_cb_is_name = true;
                strncpy(pm->ui_cb_name, cs_to_cstr(argv[2]), sizeof(pm->ui_cb_name) - 1);
                pm->ui_cb_name[sizeof(pm->ui_cb_name) - 1] = '\0';
                ok = 1;
            } else if (argv[2].type == CS_T_FUNC || argv[2].type == CS_T_NATIVE) {
                pm->ui_cb = cs_value_copy(argv[2]);
                ok = 1;
            } else {
                pm->ui_pending = false;
                pm->ui_kind = UI_KIND_NONE;
                menu_items_free(items, count);
                pm->ui_items = NULL;
                pm->ui_item_count = 0;
            }
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static bool op_set_paths_from_value(PluginManager *pm, cs_value v, char ***out_paths, size_t *out_count) {
    if (!pm || !out_paths || !out_count) return false;
    *out_paths = NULL;
    *out_count = 0;

    if (v.type == CS_T_STR) {
        const char *s = cs_to_cstr(v);
        if (!s || !*s) return false;
        char **paths = (char **)calloc(1, sizeof(char *));
        if (!paths) return false;
        paths[0] = strdup(s);
        if (!paths[0]) {
            free(paths);
            return false;
        }
        *out_paths = paths;
        *out_count = 1;
        return true;
    }

    if (v.type == CS_T_LIST) {
        cs_list_obj *l = (cs_list_obj *)v.as.p;
        if (!l || l->len == 0) return false;
        char **paths = (char **)calloc(l->len, sizeof(char *));
        if (!paths) return false;
        size_t count = 0;
        for (size_t i = 0; i < l->len; i++) {
            cs_value it = l->items[i];
            if (it.type != CS_T_STR) continue;
            const char *s = cs_to_cstr(it);
            if (!s || !*s) continue;
            paths[count] = strdup(s);
            if (!paths[count]) {
                for (size_t j = 0; j < count; j++) free(paths[j]);
                free(paths);
                return false;
            }
            count++;
        }
        if (count == 0) {
            free(paths);
            return false;
        }
        // Shrink to fit.
        char **shrunk = (char **)realloc(paths, count * sizeof(char *));
        if (shrunk) paths = shrunk;
        *out_paths = paths;
        *out_count = count;
        return true;
    }

    return false;
}

static bool enqueue_fileop(PluginManager *pm, PluginFileOpKind kind, char **paths, size_t count, const char *arg1) {
    if (!pm) return false;
    if (pm->fileop_requested) {
        // Only one pending op at a time to keep the host logic simple.
        for (size_t i = 0; i < count; i++) free(paths[i]);
        free(paths);
        return false;
    }
    fileop_clear(&pm->op);
    pm->op.kind = kind;
    pm->op.paths = paths;
    pm->op.count = count;
    pm->op.arg1[0] = '\0';
    if (arg1 && *arg1) {
        strncpy(pm->op.arg1, arg1, sizeof(pm->op.arg1) - 1);
        pm->op.arg1[sizeof(pm->op.arg1) - 1] = '\0';
    }
    pm->fileop_requested = true;
    return true;
}

static int nf_fm_copy(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 2 && argv[1].type == CS_T_STR) {
        char **paths = NULL;
        size_t count = 0;
        if (op_set_paths_from_value(pm, argv[0], &paths, &count)) {
            ok = enqueue_fileop(pm, PLUGIN_FILEOP_COPY, paths, count, cs_to_cstr(argv[1])) ? 1 : 0;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_move(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 2 && argv[1].type == CS_T_STR) {
        char **paths = NULL;
        size_t count = 0;
        if (op_set_paths_from_value(pm, argv[0], &paths, &count)) {
            ok = enqueue_fileop(pm, PLUGIN_FILEOP_MOVE, paths, count, cs_to_cstr(argv[1])) ? 1 : 0;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_rename(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 2 && argv[0].type == CS_T_STR && argv[1].type == CS_T_STR) {
        const char *src = cs_to_cstr(argv[0]);
        const char *new_name = cs_to_cstr(argv[1]);
        if (src && *src && new_name && *new_name) {
            char **paths = (char **)calloc(1, sizeof(char *));
            if (paths) {
                paths[0] = strdup(src);
                if (paths[0]) {
                    ok = enqueue_fileop(pm, PLUGIN_FILEOP_RENAME, paths, 1, new_name) ? 1 : 0;
                } else {
                    free(paths);
                }
            }
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_delete(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1) {
        char **paths = NULL;
        size_t count = 0;
        if (op_set_paths_from_value(pm, argv[0], &paths, &count)) {
            ok = enqueue_fileop(pm, PLUGIN_FILEOP_DELETE, paths, count, NULL) ? 1 : 0;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_mkdir(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1 && argv[0].type == CS_T_STR) {
        const char *name = cs_to_cstr(argv[0]);
        if (name && *name) {
            ok = enqueue_fileop(pm, PLUGIN_FILEOP_MKDIR, NULL, 0, name) ? 1 : 0;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_touch(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1 && argv[0].type == CS_T_STR) {
        const char *name = cs_to_cstr(argv[0]);
        if (name && *name) {
            ok = enqueue_fileop(pm, PLUGIN_FILEOP_TOUCH, NULL, 0, name) ? 1 : 0;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_undo(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    int ok = enqueue_fileop(pm, PLUGIN_FILEOP_UNDO, NULL, 0, NULL) ? 1 : 0;
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_redo(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    int ok = enqueue_fileop(pm, PLUGIN_FILEOP_REDO, NULL, 0, NULL) ? 1 : 0;
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_selected_paths(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    cs_value listv = cs_list(vm);
    cs_list_obj *l = (cs_list_obj *)listv.as.p;
    if (!l) {
        *out = cs_list(vm);
        return 0;
    }

    PluginManager *pm = (PluginManager *)ud;
    if (pm && pm->cwd[0] && pm->selected[0]) {
        char full[MAX_PATH_LENGTH];
        path_join(full, pm->cwd, pm->selected);
        cs_value sv = cs_str(vm, full);
        (void)list_push_local(l, sv);
        cs_value_release(sv);
    }

    *out = listv;
    return 0;
}

static int nf_fm_each_selected(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)out;
    PluginManager *pm = (PluginManager *)ud;
    if (!pm || argc != 1) return 0;

    if (!pm->cwd[0] || !pm->selected[0]) return 0;

    char full[MAX_PATH_LENGTH];
    path_join(full, pm->cwd, pm->selected);

    cs_value arg = cs_str(vm, full);
    cs_value rv = cs_nil();
    if (argv[0].type == CS_T_STR) {
        (void)cs_call(vm, cs_to_cstr(argv[0]), 1, &arg, &rv);
    } else if (argv[0].type == CS_T_FUNC || argv[0].type == CS_T_NATIVE) {
        (void)cs_call_value(vm, argv[0], 1, &arg, &rv);
    }
    cs_value_release(arg);
    cs_value_release(rv);
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

static int nf_fm_cursor(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    if (out) *out = cs_int(pm ? pm->cursor_index : -1);
    return 0;
}

static int nf_fm_count(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    if (out) *out = cs_int(pm ? pm->list_count : 0);
    return 0;
}

static int nf_fm_search_active(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    if (out) *out = cs_bool(pm ? pm->search_active : 0);
    return 0;
}

static int nf_fm_search_query(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    if (out) *out = cs_str(vm, pm ? pm->search_query : "");
    return 0;
}

static int nf_fm_pane(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    const char *name = "unknown";
    if (pm) {
        if (pm->active_pane == 1) name = "directory";
        else if (pm->active_pane == 2) name = "preview";
    }
    if (out) *out = cs_str(vm, name);
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

static int nf_fm_cd(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1 && argv[0].type == CS_T_STR) {
        const char *p = cs_to_cstr(argv[0]);
        if (p && *p) {
            strncpy(pm->cd_path, p, sizeof(pm->cd_path) - 1);
            pm->cd_path[sizeof(pm->cd_path) - 1] = '\0';
            pm->cd_requested = true;
            ok = 1;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_select(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1 && argv[0].type == CS_T_STR) {
        const char *p = cs_to_cstr(argv[0]);
        if (p && *p) {
            strncpy(pm->select_name, p, sizeof(pm->select_name) - 1);
            pm->select_name[sizeof(pm->select_name) - 1] = '\0';
            pm->select_requested = true;
            ok = 1;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_select_index(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1 && argv[0].type == CS_T_INT) {
        pm->select_index = (int)argv[0].as.i;
        pm->select_index_requested = true;
        ok = 1;
    }
    if (out) *out = cs_bool(ok);
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
    cs_register_native(vm, "fm.prompt", nf_fm_prompt, pm);
    cs_register_native(vm, "fm.confirm", nf_fm_confirm, pm);
    cs_register_native(vm, "fm.menu", nf_fm_menu, pm);
    cs_register_native(vm, "fm.console_print", nf_fm_console_print, pm);
    cs_register_native(vm, "fm.console", nf_fm_console_print, pm); // alias
    cs_register_native(vm, "fm.prompt_async", nf_fm_prompt_async, pm);
    cs_register_native(vm, "fm.confirm_async", nf_fm_confirm_async, pm);
    cs_register_native(vm, "fm.menu_async", nf_fm_menu_async, pm);
    cs_register_native(vm, "fm.popup", nf_fm_popup, pm);
    cs_register_native(vm, "fm.cwd", nf_fm_cwd, pm);
    cs_register_native(vm, "fm.selected_name", nf_fm_selected_name, pm);
    cs_register_native(vm, "fm.selected_path", nf_fm_selected_path, pm);
    cs_register_native(vm, "fm.selected_paths", nf_fm_selected_paths, pm);
    cs_register_native(vm, "fm.each_selected", nf_fm_each_selected, pm);
    cs_register_native(vm, "fm.cursor", nf_fm_cursor, pm);
    cs_register_native(vm, "fm.count", nf_fm_count, pm);
    cs_register_native(vm, "fm.search_active", nf_fm_search_active, pm);
    cs_register_native(vm, "fm.search_query", nf_fm_search_query, pm);
    cs_register_native(vm, "fm.pane", nf_fm_pane, pm);
    cs_register_native(vm, "fm.reload", nf_fm_reload, pm);
    cs_register_native(vm, "fm.exit", nf_fm_exit, pm);
    cs_register_native(vm, "fm.cd", nf_fm_cd, pm);
    cs_register_native(vm, "fm.select", nf_fm_select, pm);
    cs_register_native(vm, "fm.select_index", nf_fm_select_index, pm);
    cs_register_native(vm, "fm.copy", nf_fm_copy, pm);
    cs_register_native(vm, "fm.move", nf_fm_move, pm);
    cs_register_native(vm, "fm.rename", nf_fm_rename, pm);
    cs_register_native(vm, "fm.delete", nf_fm_delete, pm);
    cs_register_native(vm, "fm.mkdir", nf_fm_mkdir, pm);
    cs_register_native(vm, "fm.touch", nf_fm_touch, pm);
    cs_register_native(vm, "fm.undo", nf_fm_undo, pm);
    cs_register_native(vm, "fm.redo", nf_fm_redo, pm);
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
        (void)cs_call(vm, "on_load", 0, NULL, &out);
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
    pm->active_pane = 0;
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
    pm->search_active = false;
    pm->search_query[0] = '\0';
    pm->active_pane = 0;
    pm->fileop_requested = false;
    fileop_clear(&pm->op);
    if (pm->ui_pending) {
        pm->ui_pending = false;
    }
    pm->ui_kind = UI_KIND_NONE;
    pm->ui_title[0] = '\0';
    pm->ui_msg[0] = '\0';
    pm->ui_initial[0] = '\0';
    menu_items_free(pm->ui_items, pm->ui_item_count);
    pm->ui_items = NULL;
    pm->ui_item_count = 0;
    pm->ui_vm = NULL;
    if (pm->ui_cb.type != CS_T_NIL) cs_value_release(pm->ui_cb);
    pm->ui_cb = cs_nil();
    pm->ui_cb_is_name = false;
    pm->ui_cb_name[0] = '\0';
    pm->context_initialized = false;
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
    // Back-compat shim (internal only): feed the richer setter with minimal info.
    PluginsContext ctx = {
        .cwd = cwd,
        .selected_name = selected_name,
        .cursor_index = -1,
        .list_count = 0,
        .search_active = false,
        .search_query = "",
        .active_pane = 0,
    };
    plugins_set_context_ex(pm, &ctx);
}

static void call_void2_str(PluginManager *pm, cs_vm *vm, const char *fn, const char *a, const char *b) {
    if (!pm || !vm || !fn) return;
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

void plugins_set_context_ex(PluginManager *pm, const PluginsContext *ctx) {
    if (!pm || !ctx) return;

    const char *new_cwd = ctx->cwd ? ctx->cwd : "";
    const char *new_sel = ctx->selected_name ? ctx->selected_name : "";

    bool cwd_changed = pm->context_initialized &&
                       (strncmp(pm->cwd, new_cwd, sizeof(pm->cwd)) != 0);
    bool sel_changed = pm->context_initialized &&
                       (strncmp(pm->selected, new_sel, sizeof(pm->selected)) != 0);

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
    pm->search_active = ctx->search_active;
    strncpy(pm->search_query, ctx->search_query ? ctx->search_query : "", sizeof(pm->search_query) - 1);
    pm->search_query[sizeof(pm->search_query) - 1] = '\0';
    pm->active_pane = ctx->active_pane;

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
    }
    if (sel_changed) {
        for (size_t i = 0; i < pm->plugin_count; i++) {
            call_void2_str(pm, pm->plugins[i].vm, "on_selection_change", pm->selected, old_sel);
        }
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

void plugins_poll(PluginManager *pm) {
    if (!pm || !pm->ui_pending || pm->ui_kind == UI_KIND_NONE) return;
    if (!pm->ui_vm) {
        pm->ui_pending = false;
        pm->ui_kind = UI_KIND_NONE;
        return;
    }

    cs_vm *vm = pm->ui_vm;
    cs_value arg = cs_nil();
    if (pm->ui_kind == UI_KIND_PROMPT) {
        arg = modal_prompt_text(vm, pm->ui_title, pm->ui_initial);
    } else if (pm->ui_kind == UI_KIND_CONFIRM) {
        arg = cs_bool(modal_confirm(pm->ui_title, pm->ui_msg) ? 1 : 0);
    } else if (pm->ui_kind == UI_KIND_MENU) {
        int idx = modal_menu(pm->ui_title, pm->ui_items, pm->ui_item_count);
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
    menu_items_free(pm->ui_items, pm->ui_item_count);
    pm->ui_items = NULL;
    pm->ui_item_count = 0;
    pm->ui_vm = NULL;
    if (pm->ui_cb.type != CS_T_NIL) cs_value_release(pm->ui_cb);
    pm->ui_cb = cs_nil();
    pm->ui_cb_is_name = false;
    pm->ui_cb_name[0] = '\0';
}

bool plugins_take_cd_request(PluginManager *pm, char *out_path, size_t out_len) {
    if (!pm || !out_path || out_len == 0) return false;
    if (!pm->cd_requested) return false;
    strncpy(out_path, pm->cd_path, out_len - 1);
    out_path[out_len - 1] = '\0';
    pm->cd_requested = false;
    pm->cd_path[0] = '\0';
    return true;
}

bool plugins_take_select_request(PluginManager *pm, char *out_name, size_t out_len) {
    if (!pm || !out_name || out_len == 0) return false;
    if (!pm->select_requested) return false;
    strncpy(out_name, pm->select_name, out_len - 1);
    out_name[out_len - 1] = '\0';
    pm->select_requested = false;
    pm->select_name[0] = '\0';
    return true;
}

bool plugins_take_select_index_request(PluginManager *pm, int *out_index) {
    if (!pm || !out_index) return false;
    if (!pm->select_index_requested) return false;
    *out_index = pm->select_index;
    pm->select_index_requested = false;
    pm->select_index = -1;
    return true;
}

bool plugins_take_fileop_request(PluginManager *pm, PluginFileOp *out) {
    if (!pm || !out) return false;
    if (!pm->fileop_requested) return false;

    // Transfer ownership.
    *out = pm->op;
    pm->op.kind = PLUGIN_FILEOP_NONE;
    pm->op.paths = NULL;
    pm->op.count = 0;
    pm->op.arg1[0] = '\0';
    pm->fileop_requested = false;
    return true;
}
