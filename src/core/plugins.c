// plugins.c
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "plugins.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <magic.h>
#include <strings.h>
#include <strings.h>

#include "globals.h"
#include "main.h"   // show_notification, draw_scrolling_banner globals
#include "ui.h"     // show_popup
#include "utils.h"  // path_join
#include "console.h"
#include "files.h"
#include "clipboard.h"
#include "search.h"

#include "cupidscript.h"
#include "cs_vm.h"
#include "cs_value.h"
#include "cs_http.h"

typedef struct {
    cs_vm *vm;
    char  *path;
} Plugin;

typedef struct {
    int key;
    cs_vm *vm;
    char *func;
} KeyBinding;

typedef struct {
    char event[32];
    cs_vm *vm;
    bool cb_is_name;
    char cb_name[64];
    cs_value cb;
} EventBinding;

typedef struct {
    char *name;
    char *path;
} MarkEntry;

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
    int active_pane; // 1=directory, 2=preview
    const Vector *view; // current visible listing (not owned)
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

static const char *basename_ptr_local(const char *p) {
    if (!p) return "";
    size_t len = strlen(p);
    while (len > 0 && p[len - 1] == '/') len--;
    if (len == 0) return "";
    for (size_t i = len; i > 0; i--) {
        if (p[i - 1] == '/') return p + i;
    }
    return p;
}

static bool config_key_to_field(const char *key, int **out_field) {
    if (!key || !*key || !out_field) return false;
    if (strcmp(key, "key_up") == 0) { *out_field = &g_kb.key_up; return true; }
    if (strcmp(key, "key_down") == 0) { *out_field = &g_kb.key_down; return true; }
    if (strcmp(key, "key_left") == 0) { *out_field = &g_kb.key_left; return true; }
    if (strcmp(key, "key_right") == 0) { *out_field = &g_kb.key_right; return true; }
    if (strcmp(key, "key_tab") == 0) { *out_field = &g_kb.key_tab; return true; }
    if (strcmp(key, "key_exit") == 0) { *out_field = &g_kb.key_exit; return true; }
    if (strcmp(key, "key_edit") == 0) { *out_field = &g_kb.key_edit; return true; }
    if (strcmp(key, "key_copy") == 0) { *out_field = &g_kb.key_copy; return true; }
    if (strcmp(key, "key_paste") == 0) { *out_field = &g_kb.key_paste; return true; }
    if (strcmp(key, "key_cut") == 0) { *out_field = &g_kb.key_cut; return true; }
    if (strcmp(key, "key_delete") == 0) { *out_field = &g_kb.key_delete; return true; }
    if (strcmp(key, "key_rename") == 0) { *out_field = &g_kb.key_rename; return true; }
    if (strcmp(key, "key_new") == 0) { *out_field = &g_kb.key_new; return true; }
    if (strcmp(key, "key_save") == 0) { *out_field = &g_kb.key_save; return true; }
    if (strcmp(key, "key_new_dir") == 0) { *out_field = &g_kb.key_new_dir; return true; }
    if (strcmp(key, "key_search") == 0) { *out_field = &g_kb.key_search; return true; }
    if (strcmp(key, "key_select_all") == 0) { *out_field = &g_kb.key_select_all; return true; }
    if (strcmp(key, "key_info") == 0) { *out_field = &g_kb.key_info; return true; }
    if (strcmp(key, "key_undo") == 0) { *out_field = &g_kb.key_undo; return true; }
    if (strcmp(key, "key_redo") == 0) { *out_field = &g_kb.key_redo; return true; }
    if (strcmp(key, "key_permissions") == 0) { *out_field = &g_kb.key_permissions; return true; }
    if (strcmp(key, "key_console") == 0) { *out_field = &g_kb.key_console; return true; }
    if (strcmp(key, "edit_up") == 0) { *out_field = &g_kb.edit_up; return true; }
    if (strcmp(key, "edit_down") == 0) { *out_field = &g_kb.edit_down; return true; }
    if (strcmp(key, "edit_left") == 0) { *out_field = &g_kb.edit_left; return true; }
    if (strcmp(key, "edit_right") == 0) { *out_field = &g_kb.edit_right; return true; }
    if (strcmp(key, "edit_save") == 0) { *out_field = &g_kb.edit_save; return true; }
    if (strcmp(key, "edit_quit") == 0) { *out_field = &g_kb.edit_quit; return true; }
    if (strcmp(key, "edit_backspace") == 0) { *out_field = &g_kb.edit_backspace; return true; }
    if (strcmp(key, "info_label_width") == 0) { *out_field = &g_kb.info_label_width; return true; }
    return false;
}

static void keycode_to_config_string_local(int keycode, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    if (keycode >= KEY_F(1) && keycode <= KEY_F(63)) {
        snprintf(buf, buf_size, "F%d", keycode - KEY_F(1) + 1);
        return;
    }
    if (keycode >= 1 && keycode <= 26) {
        snprintf(buf, buf_size, "^%c", 'A' + (keycode - 1));
        return;
    }
    switch (keycode) {
        case KEY_UP: snprintf(buf, buf_size, "KEY_UP"); return;
        case KEY_DOWN: snprintf(buf, buf_size, "KEY_DOWN"); return;
        case KEY_LEFT: snprintf(buf, buf_size, "KEY_LEFT"); return;
        case KEY_RIGHT: snprintf(buf, buf_size, "KEY_RIGHT"); return;
        case KEY_BACKSPACE: snprintf(buf, buf_size, "KEY_BACKSPACE"); return;
        default: break;
    }
    if (keycode == '\t') {
        snprintf(buf, buf_size, "Tab");
        return;
    }
    if (keycode == ' ') {
        snprintf(buf, buf_size, "Space");
        return;
    }
    if (keycode >= 32 && keycode <= 126) {
        snprintf(buf, buf_size, "%c", (char)keycode);
        return;
    }
    snprintf(buf, buf_size, "%d", keycode);
}

static int parse_key_local(const char *val) {
    if (!val || !*val) return -1;
    if (strcasecmp(val, "KEY_UP") == 0) return KEY_UP;
    if (strcasecmp(val, "KEY_DOWN") == 0) return KEY_DOWN;
    if (strcasecmp(val, "KEY_LEFT") == 0) return KEY_LEFT;
    if (strcasecmp(val, "KEY_RIGHT") == 0) return KEY_RIGHT;
    if (strcasecmp(val, "KEY_BACKSPACE") == 0 || strcasecmp(val, "Backspace") == 0) return KEY_BACKSPACE;
    if (strcasecmp(val, "Tab") == 0) return '\t';
    if (strcasecmp(val, "Space") == 0) return ' ';

    if (val[0] == '^' && val[1] && !val[2]) {
        char c = val[1];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c >= 'A' && c <= 'Z') return (c - 'A') + 1;
        return -1;
    }

    if (strncasecmp(val, "KEY_F(", 6) == 0) {
        int fnum = atoi(val + 6);
        if (fnum >= 1 && fnum <= 63) return KEY_F(fnum);
        return -1;
    }

    if ((val[0] == 'F' || val[0] == 'f') && val[1]) {
        char *end = NULL;
        long n = strtol(val + 1, &end, 10);
        if (end && *end == '\0' && n >= 1 && n <= 63) return KEY_F((int)n);
    }

    if (strncasecmp(val, "Shift+", 6) == 0) {
        char shift_key = val[6];
        if (isalpha((unsigned char)shift_key)) return toupper(shift_key);
        static const char shift_symbols[] = ")!@#$%^&*(";
        if (isdigit((unsigned char)shift_key)) {
            int num = shift_key - '0';
            return shift_symbols[num];
        }
        if (strcmp(val + 6, "Minus") == 0) return '_';
        if (strcmp(val + 6, "Equals") == 0) return '+';
        if (strcmp(val + 6, "LeftBracket") == 0) return '{';
        if (strcmp(val + 6, "RightBracket") == 0) return '}';
        if (strcmp(val + 6, "Semicolon") == 0) return ':';
        if (strcmp(val + 6, "Apostrophe") == 0) return '"';
        if (strcmp(val + 6, "Comma") == 0) return '<';
        if (strcmp(val + 6, "Period") == 0) return '>';
        if (strcmp(val + 6, "Slash") == 0) return '?';
        if (strcmp(val + 6, "Backslash") == 0) return '|';
        if (strcmp(val + 6, "Grave") == 0) return '~';
        return -1;
    }

    if (strlen(val) == 1) return (unsigned char)val[0];
    return -1;
}

static bool get_config_path(char *out_path, size_t out_len) {
    if (!out_path || out_len == 0) return false;
    const char *home = getenv("HOME");
    if (!home || !*home) home = ".";
    snprintf(out_path, out_len, "%s/.cupidfmrc", home);
    out_path[out_len - 1] = '\0';
    return true;
}

static bool get_cache_path(char *out_path, size_t out_len) {
    if (!out_path || out_len == 0) return false;
    const char *home = getenv("HOME");
    if (!home || !*home) home = ".";
    snprintf(out_path, out_len, "%s/.cupidfm/cache.kv", home);
    out_path[out_len - 1] = '\0';
    return true;
}

static bool ensure_cache_dir(void) {
    char path[MAX_PATH_LENGTH];
    if (!get_cache_path(path, sizeof(path))) return false;
    char *slash = strrchr(path, '/');
    if (!slash) return false;
    *slash = '\0';
    return ensure_dir(path);
}

static char *escape_kv(const char *s) {
    if (!s) s = "";
    size_t len = strlen(s);
    size_t cap = len * 2 + 1;
    char *out = (char *)malloc(cap);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\\' || c == '\n' || c == '\t') {
            if (j + 2 >= cap) {
                cap *= 2;
                char *nb = (char *)realloc(out, cap);
                if (!nb) { free(out); return NULL; }
                out = nb;
            }
            out[j++] = '\\';
            out[j++] = (c == '\n') ? 'n' : (c == '\t') ? 't' : '\\';
        } else {
            if (j + 1 >= cap) {
                cap *= 2;
                char *nb = (char *)realloc(out, cap);
                if (!nb) { free(out); return NULL; }
                out = nb;
            }
            out[j++] = c;
        }
    }
    out[j] = '\0';
    return out;
}

static char *unescape_kv(const char *s) {
    if (!s) return strdup("");
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < len) {
            char n = s[++i];
            if (n == 'n') out[j++] = '\n';
            else if (n == 't') out[j++] = '\t';
            else out[j++] = n;
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
    return out;
}

static cs_value decode_cache_value(cs_vm *vm, const char *raw) {
    if (!raw) return cs_nil();
    if (strncmp(raw, "s:", 2) == 0) {
        char *v = unescape_kv(raw + 2);
        if (!v) return cs_nil();
        cs_value out = cs_str(vm, v);
        free(v);
        return out;
    }
    if (strncmp(raw, "i:", 2) == 0) {
        long long v = atoll(raw + 2);
        return cs_int(v);
    }
    if (strncmp(raw, "b:", 2) == 0) {
        return cs_bool((raw[2] == '1') ? 1 : 0);
    }
    if (strncmp(raw, "f:", 2) == 0) {
        double v = strtod(raw + 2, NULL);
        return cs_float(v);
    }
    char *v = unescape_kv(raw);
    if (!v) return cs_nil();
    cs_value out = cs_str(vm, v);
    free(v);
    return out;
}

static char *encode_cache_value(const cs_value *v) {
    if (!v) return NULL;
    char buf[128];
    if (v->type == CS_T_STR) {
        const char *s = cs_to_cstr(*v);
        char *esc = escape_kv(s ? s : "");
        if (!esc) return NULL;
        size_t n = strlen(esc) + 3;
        char *out = (char *)malloc(n);
        if (!out) { free(esc); return NULL; }
        snprintf(out, n, "s:%s", esc);
        free(esc);
        return out;
    }
    if (v->type == CS_T_INT) {
        snprintf(buf, sizeof(buf), "i:%lld", (long long)v->as.i);
        return strdup(buf);
    }
    if (v->type == CS_T_BOOL) {
        snprintf(buf, sizeof(buf), "b:%d", v->as.b ? 1 : 0);
        return strdup(buf);
    }
    if (v->type == CS_T_FLOAT) {
        snprintf(buf, sizeof(buf), "f:%.17g", v->as.f);
        return strdup(buf);
    }
    return NULL;
}

static void selected_paths_clear(PluginManager *pm) {
    if (!pm) return;
    if (pm->selected_paths) {
        for (size_t i = 0; i < pm->selected_path_count; i++) free(pm->selected_paths[i]);
        free(pm->selected_paths);
    }
    pm->selected_paths = NULL;
    pm->selected_path_count = 0;
    pm->event_bindings = NULL;
    pm->event_bind_count = pm->event_bind_cap = 0;
    pm->marks = NULL;
    pm->mark_count = pm->mark_cap = 0;
}

static bool selected_paths_set_from_value(PluginManager *pm, cs_value v) {
    if (!pm) return false;
    selected_paths_clear(pm);

    if (v.type != CS_T_LIST) return false;
    cs_list_obj *l = (cs_list_obj *)v.as.p;
    if (!l || l->len == 0) return true;

    char **paths = (char **)calloc(l->len, sizeof(char *));
    if (!paths) return false;
    size_t count = 0;
    for (size_t i = 0; i < l->len; i++) {
        cs_value it = l->items[i];
        if (it.type != CS_T_STR) continue;
        const char *s = cs_to_cstr(it);
        if (!s || !*s) continue;
        if (s[0] != '/' && pm->cwd[0]) {
            char full[MAX_PATH_LENGTH];
            path_join(full, pm->cwd, s);
            paths[count] = strdup(full);
        } else {
            paths[count] = strdup(s);
        }
        if (!paths[count]) {
            for (size_t j = 0; j < count; j++) free(paths[j]);
            free(paths);
            return false;
        }
        count++;
    }

    if (count == 0) {
        free(paths);
        return true;
    }

    char **shrunk = (char **)realloc(paths, count * sizeof(char *));
    if (shrunk) paths = shrunk;
    pm->selected_paths = paths;
    pm->selected_path_count = count;
    return true;
}

static bool event_binding_append(PluginManager *pm, const char *event, cs_vm *vm, const cs_value *cb, bool cb_is_name) {
    if (!pm || !event || !*event || !vm || !cb) return false;
    if (pm->event_bind_count == pm->event_bind_cap) {
        size_t nc = pm->event_bind_cap ? pm->event_bind_cap * 2 : 8;
        EventBinding *nb = (EventBinding *)realloc(pm->event_bindings, nc * sizeof(EventBinding));
        if (!nb) return false;
        pm->event_bindings = nb;
        pm->event_bind_cap = nc;
    }

    EventBinding *eb = &pm->event_bindings[pm->event_bind_count];
    memset(eb, 0, sizeof(*eb));
    strncpy(eb->event, event, sizeof(eb->event) - 1);
    eb->event[sizeof(eb->event) - 1] = '\0';
    eb->vm = vm;
    eb->cb_is_name = cb_is_name;
    eb->cb = cs_nil();
    if (cb_is_name && cb->type == CS_T_STR) {
        const char *name = cs_to_cstr(*cb);
        if (name && *name) {
            strncpy(eb->cb_name, name, sizeof(eb->cb_name) - 1);
            eb->cb_name[sizeof(eb->cb_name) - 1] = '\0';
        }
    } else {
        eb->cb = cs_value_copy(*cb);
    }

    pm->event_bind_count++;
    return true;
}

static bool mark_set(PluginManager *pm, const char *name, const char *path) {
    if (!pm || !name || !*name || !path || !*path) return false;
    for (size_t i = 0; i < pm->mark_count; i++) {
        if (pm->marks[i].name && strcmp(pm->marks[i].name, name) == 0) {
            char *np = strdup(path);
            if (!np) return false;
            free(pm->marks[i].path);
            pm->marks[i].path = np;
            return true;
        }
    }
    if (pm->mark_count == pm->mark_cap) {
        size_t nc = pm->mark_cap ? pm->mark_cap * 2 : 8;
        MarkEntry *nm = (MarkEntry *)realloc(pm->marks, nc * sizeof(MarkEntry));
        if (!nm) return false;
        pm->marks = nm;
        pm->mark_cap = nc;
    }
    pm->marks[pm->mark_count].name = strdup(name);
    pm->marks[pm->mark_count].path = strdup(path);
    if (!pm->marks[pm->mark_count].name || !pm->marks[pm->mark_count].path) {
        free(pm->marks[pm->mark_count].name);
        free(pm->marks[pm->mark_count].path);
        pm->marks[pm->mark_count].name = NULL;
        pm->marks[pm->mark_count].path = NULL;
        return false;
    }
    pm->mark_count++;
    return true;
}

static const char *mark_get(PluginManager *pm, const char *name) {
    if (!pm || !name || !*name) return NULL;
    for (size_t i = 0; i < pm->mark_count; i++) {
        if (pm->marks[i].name && strcmp(pm->marks[i].name, name) == 0) {
            return pm->marks[i].path;
        }
    }
    return NULL;
}

static int nf_fm_on(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 2 && argv[0].type == CS_T_STR) {
        const char *event = cs_to_cstr(argv[0]);
        if (event && *event) {
            bool cb_is_name = (argv[1].type == CS_T_STR);
            if (cb_is_name || argv[1].type == CS_T_FUNC || argv[1].type == CS_T_NATIVE) {
                ok = event_binding_append(pm, event, vm, &argv[1], cb_is_name) ? 1 : 0;
            }
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_mark(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1 && argv[0].type == CS_T_STR) {
        const char *name = cs_to_cstr(argv[0]);
        if (name && *name && pm->cwd[0]) {
            ok = mark_set(pm, name, pm->cwd) ? 1 : 0;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_goto_mark(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1 && argv[0].type == CS_T_STR) {
        const char *name = cs_to_cstr(argv[0]);
        const char *path = mark_get(pm, name ? name : "");
        if (path && *path) {
            strncpy(pm->cd_path, path, sizeof(pm->cd_path) - 1);
            pm->cd_path[sizeof(pm->cd_path) - 1] = '\0';
            pm->cd_requested = true;
            ok = 1;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
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

static int nf_fm_ui_status_set(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)ud;
    if (argc == 1 && argv[0].type == CS_T_STR && notifwin) {
        show_notification(notifwin, "%s", cs_to_cstr(argv[0]));
        should_clear_notif = false;
        hold_notification_for_ms(3600000); // hold for a long time unless cleared
    }
    if (out) *out = cs_nil();
    return 0;
}

static int nf_fm_ui_status_clear(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)ud; (void)argc; (void)argv;
    if (notifwin) {
        notification_hold_active = false;
        should_clear_notif = true;
        werase(notifwin);
        wrefresh(notifwin);
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
    if (pm) {
        if (pm->selected_paths && pm->selected_path_count > 0) {
            for (size_t i = 0; i < pm->selected_path_count; i++) {
                if (pm->selected_paths[i] && pm->selected_paths[i][0]) {
                    (void)list_push_local(l, cs_str(vm, pm->selected_paths[i]));
                }
            }
        } else if (pm->select_all_active && pm->view && pm->view->el) {
            size_t n = Vector_len(*pm->view);
            for (size_t i = 0; i < n; i++) {
                FileAttr fa = (FileAttr)pm->view->el[i];
                const char *name = FileAttr_get_name(fa);
                if (!name || !*name) continue;
                char full[MAX_PATH_LENGTH];
                path_join(full, pm->cwd, name);
                (void)list_push_local(l, cs_str(vm, full));
            }
        } else if (pm->cwd[0] && pm->selected[0]) {
            char full[MAX_PATH_LENGTH];
            path_join(full, pm->cwd, pm->selected);
            (void)list_push_local(l, cs_str(vm, full));
        }
    }

    *out = listv;
    return 0;
}

static int nf_fm_select_paths(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    return nf_fm_selected_paths(vm, ud, argc, argv, out);
}

static int nf_fm_set_selected_paths(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1) {
        if (argv[0].type == CS_T_LIST) {
            ok = selected_paths_set_from_value(pm, argv[0]) ? 1 : 0;
        } else if (argv[0].type == CS_T_NIL) {
            selected_paths_clear(pm);
            ok = 1;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_clear_selected_paths(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    if (pm) selected_paths_clear(pm);
    if (out) *out = cs_nil();
    return 0;
}

static int nf_fm_each_selected(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)out;
    PluginManager *pm = (PluginManager *)ud;
    if (!pm || argc != 1) return 0;

    if (pm->selected_paths && pm->selected_path_count > 0) {
        for (size_t i = 0; i < pm->selected_path_count; i++) {
            const char *p = pm->selected_paths[i];
            if (!p || !*p) continue;
            cs_value arg = cs_str(vm, p);
            cs_value rv = cs_nil();
            if (argv[0].type == CS_T_STR) {
                (void)cs_call(vm, cs_to_cstr(argv[0]), 1, &arg, &rv);
            } else if (argv[0].type == CS_T_FUNC || argv[0].type == CS_T_NATIVE) {
                (void)cs_call_value(vm, argv[0], 1, &arg, &rv);
            }
            cs_value_release(arg);
            cs_value_release(rv);
        }
        return 0;
    }

    if (pm->select_all_active && pm->view && pm->view->el) {
        size_t n = Vector_len(*pm->view);
        for (size_t i = 0; i < n; i++) {
            FileAttr fa = (FileAttr)pm->view->el[i];
            const char *name = FileAttr_get_name(fa);
            if (!name || !*name) continue;
            char full[MAX_PATH_LENGTH];
            path_join(full, pm->cwd, name);
            cs_value arg = cs_str(vm, full);
            cs_value rv = cs_nil();
            if (argv[0].type == CS_T_STR) {
                (void)cs_call(vm, cs_to_cstr(argv[0]), 1, &arg, &rv);
            } else if (argv[0].type == CS_T_FUNC || argv[0].type == CS_T_NATIVE) {
                (void)cs_call_value(vm, argv[0], 1, &arg, &rv);
            }
            cs_value_release(arg);
            cs_value_release(rv);
        }
        return 0;
    }

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

static int nf_fm_editor_active(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)ud; (void)argc; (void)argv;
    if (out) *out = cs_bool(is_editing);
    return 0;
}

static int nf_fm_editor_get_path(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud; (void)argc; (void)argv;
    if (out) {
        if (is_editing && g_editor_path[0]) {
            *out = cs_str(vm, g_editor_path);
        } else {
            *out = cs_nil();
        }
    }
    return 0;
}

static int nf_fm_editor_get_content(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;
    char *content = editor_get_content_copy();
    if (!content) {
        *out = cs_nil();
        return 0;
    }
    *out = cs_str(vm, content);
    free(content);
    return 0;
}

static int nf_fm_editor_get_line(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_INT) {
        *out = cs_nil();
        return 0;
    }
    int line_num = (int)argv[0].as.i;
    char *line = editor_get_line_copy(line_num);
    if (!line) {
        *out = cs_nil();
        return 0;
    }
    *out = cs_str(vm, line);
    free(line);
    return 0;
}

static int nf_fm_clipboard_get(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud; (void)argc; (void)argv;
    if (!out) return 0;

    FILE *pipe = popen("xclip -selection clipboard -o", "r");
    if (!pipe) {
        *out = cs_nil();
        return 0;
    }

    size_t cap = 4096;
    size_t len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        pclose(pipe);
        *out = cs_nil();
        return 0;
    }

    char tmp[1024];
    size_t n = 0;
    while ((n = fread(tmp, 1, sizeof(tmp), pipe)) > 0) {
        if (len + n + 1 > cap) {
            size_t nc = cap * 2;
            while (nc < len + n + 1) nc *= 2;
            char *nb = (char *)realloc(buf, nc);
            if (!nb) {
                free(buf);
                pclose(pipe);
                *out = cs_nil();
                return 0;
            }
            buf = nb;
            cap = nc;
        }
        memcpy(buf + len, tmp, n);
        len += n;
    }
    pclose(pipe);

    buf[len] = '\0';
    *out = cs_str(vm, buf);
    free(buf);
    return 0;
}

static int nf_fm_clipboard_set(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (!pm || argc != 1) {
        if (out) *out = cs_bool(0);
        return 0;
    }

    if (argv[0].type == CS_T_STR) {
        const char *text = cs_to_cstr(argv[0]);
        FILE *pipe = popen("xclip -selection clipboard -i", "w");
        if (pipe) {
            if (text && *text) {
                fwrite(text, 1, strlen(text), pipe);
            }
            int rc = pclose(pipe);
            ok = (rc != -1);
        }
        if (out) *out = cs_bool(ok);
        return 0;
    }

    if (argv[0].type == CS_T_LIST) {
        cs_list_obj *l = (cs_list_obj *)argv[0].as.p;
        if (!l || l->len == 0) {
            if (out) *out = cs_bool(0);
            return 0;
        }

        long count = 0;
        for (size_t i = 0; i < l->len; i++) {
            cs_value it = l->items[i];
            if (it.type != CS_T_STR) continue;
            const char *s = cs_to_cstr(it);
            if (!s || !*s) continue;
            char full[MAX_PATH_LENGTH];
            if (s[0] == '/' || !pm->cwd[0]) {
                strncpy(full, s, sizeof(full) - 1);
                full[sizeof(full) - 1] = '\0';
            } else {
                path_join(full, pm->cwd, s);
            }
            struct stat st;
            if (stat(full, &st) == 0) count++;
        }

        if (count <= 0) {
            if (out) *out = cs_bool(0);
            return 0;
        }

        char tmp_path[] = "/tmp/cupidfm_clip_plugin_XXXXXX";
        int fd = mkstemp(tmp_path);
        if (fd < 0) {
            if (out) *out = cs_bool(0);
            return 0;
        }

        FILE *fp = fdopen(fd, "w");
        if (!fp) {
            close(fd);
            unlink(tmp_path);
            if (out) *out = cs_bool(0);
            return 0;
        }

        fprintf(fp, "CUPIDFM_CLIP_V2\n");
        fprintf(fp, "OP=COPY\n");
        fprintf(fp, "N=%ld\n", count);

        for (size_t i = 0; i < l->len; i++) {
            cs_value it = l->items[i];
            if (it.type != CS_T_STR) continue;
            const char *s = cs_to_cstr(it);
            if (!s || !*s) continue;
            char full[MAX_PATH_LENGTH];
            if (s[0] == '/' || !pm->cwd[0]) {
                strncpy(full, s, sizeof(full) - 1);
                full[sizeof(full) - 1] = '\0';
            } else {
                path_join(full, pm->cwd, s);
            }
            struct stat st;
            if (stat(full, &st) != 0) continue;
            const char *name = basename_ptr_local(full);
            fprintf(fp, "%d\t%s\t%s\n", S_ISDIR(st.st_mode) ? 1 : 0, full, name);
        }

        fclose(fp);
        ok = clipboard_set_from_file(tmp_path) ? 1 : 0;
        unlink(tmp_path);

        if (out) *out = cs_bool(ok);
        return 0;
    }

    if (out) *out = cs_bool(0);
    return 0;
}

static int map_put_move_local(cs_vm *vm, cs_value *map_val, const char *key, cs_value *v);

static bool buffer_append_limited(char **buf, size_t *len, size_t *cap, const char *data, size_t n, size_t max_out, bool *truncated) {
    if (!buf || !len || !cap) return false;
    if (!data || n == 0) return true;
    if (max_out > 0 && *len >= max_out) {
        if (truncated) *truncated = true;
        return true;
    }

    size_t allowed = n;
    if (max_out > 0 && *len + n > max_out) {
        allowed = max_out - *len;
        if (truncated) *truncated = true;
    }

    if (*len + allowed + 1 > *cap) {
        size_t nc = *cap ? *cap : 4096;
        while (nc < *len + allowed + 1) nc *= 2;
        char *nb = (char *)realloc(*buf, nc);
        if (!nb) return false;
        *buf = nb;
        *cap = nc;
    }

    memcpy(*buf + *len, data, allowed);
    *len += allowed;
    (*buf)[*len] = '\0';
    return true;
}

static int nf_fm_info(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    PluginManager *pm = (PluginManager *)ud;
    if (!out) return 0;
    if (!pm || argc != 1 || argv[0].type != CS_T_STR) {
        *out = cs_nil();
        return 0;
    }

    const char *p = cs_to_cstr(argv[0]);
    if (!p || !*p) {
        *out = cs_nil();
        return 0;
    }

    char full[MAX_PATH_LENGTH];
    if (p[0] == '/' || !pm->cwd[0]) {
        strncpy(full, p, sizeof(full) - 1);
        full[sizeof(full) - 1] = '\0';
    } else {
        path_join(full, pm->cwd, p);
    }

    struct stat st;
    struct stat lst;
    if (stat(full, &st) != 0) {
        *out = cs_nil();
        return 0;
    }

    cs_value mapv = cs_map(vm);
    if (!mapv.as.p) {
        *out = cs_nil();
        return 0;
    }

    cs_value v_size = cs_int((long long)st.st_size);
    cs_value v_mtime = cs_int((long long)st.st_mtime);
    cs_value v_mode = cs_int((long long)(st.st_mode & 07777));
    cs_value v_is_dir = cs_bool(S_ISDIR(st.st_mode) ? 1 : 0);

    bool is_link = false;
    if (lstat(full, &lst) == 0 && S_ISLNK(lst.st_mode)) {
        is_link = true;
    }
    cs_value v_is_link = cs_bool(is_link ? 1 : 0);

    cs_value v_target = cs_nil();
    if (is_link) {
        char linkbuf[MAX_PATH_LENGTH];
        ssize_t n = readlink(full, linkbuf, sizeof(linkbuf) - 1);
        if (n > 0) {
            linkbuf[n] = '\0';
            v_target = cs_str(vm, linkbuf);
        }
    }

    cs_value v_mime = cs_str(vm, "");
    magic_t mc = magic_open(MAGIC_MIME_TYPE | MAGIC_SYMLINK | MAGIC_CHECK);
    if (mc && magic_load(mc, NULL) == 0) {
        const char *mt = magic_file(mc, full);
        if (mt && *mt) {
            cs_value_release(v_mime);
            v_mime = cs_str(vm, mt);
        }
    }
    if (mc) magic_close(mc);

    if (!map_put_move_local(vm, &mapv, "size", &v_size)) goto info_oom;
    if (!map_put_move_local(vm, &mapv, "mtime", &v_mtime)) goto info_oom;
    if (!map_put_move_local(vm, &mapv, "mode", &v_mode)) goto info_oom;
    if (!map_put_move_local(vm, &mapv, "mime", &v_mime)) goto info_oom;
    if (!map_put_move_local(vm, &mapv, "is_dir", &v_is_dir)) goto info_oom;
    if (!map_put_move_local(vm, &mapv, "is_link", &v_is_link)) goto info_oom;
    if (!map_put_move_local(vm, &mapv, "target", &v_target)) goto info_oom;

    *out = mapv;
    return 0;

info_oom:
    cs_value_release(v_size);
    cs_value_release(v_mtime);
    cs_value_release(v_mode);
    cs_value_release(v_mime);
    cs_value_release(v_is_dir);
    cs_value_release(v_is_link);
    cs_value_release(v_target);
    cs_value_release(mapv);
    cs_error(vm, "out of memory");
    return 1;
}

static int nf_fm_exec(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    PluginManager *pm = (PluginManager *)ud;
    if (!out) return 0;
    *out = cs_nil();
    if (!pm || argc < 1 || argc > 3 || argv[0].type != CS_T_STR) {
        return 0;
    }

    const char *cmd = cs_to_cstr(argv[0]);
    if (!cmd || !*cmd) return 0;

    cs_list_obj *args = NULL;
    if (argc >= 2 && argv[1].type == CS_T_LIST) {
        args = (cs_list_obj *)argv[1].as.p;
    }

    cs_map_obj *opts = NULL;
    if (argc >= 3 && argv[2].type == CS_T_MAP) {
        opts = (cs_map_obj *)argv[2].as.p;
    }

    int64_t timeout_ms = 5000;
    int64_t max_output = 1024 * 256;
    const char *cwd_opt = NULL;

    if (opts) {
        cs_value v;
        v = cs_map_get(argv[2], "timeout_ms");
        if (v.type == CS_T_INT && v.as.i >= 0) timeout_ms = v.as.i;
        v = cs_map_get(argv[2], "max_output");
        if (v.type == CS_T_INT && v.as.i >= 0) max_output = v.as.i;
        v = cs_map_get(argv[2], "cwd");
        if (v.type == CS_T_STR) cwd_opt = cs_to_cstr(v);
    }

    size_t argc_extra = args ? args->len : 0;
    size_t argc_total = 1 + argc_extra + 1;
    char **cargv = (char **)calloc(argc_total, sizeof(char *));
    if (!cargv) return 0;
    cargv[0] = strdup(cmd);
    if (!cargv[0]) {
        free(cargv);
        return 0;
    }

    size_t ai = 1;
    if (args) {
        for (size_t i = 0; i < args->len; i++) {
            cs_value it = args->items[i];
            if (it.type != CS_T_STR) continue;
            const char *s = cs_to_cstr(it);
            if (!s) continue;
            cargv[ai] = strdup(s);
            if (!cargv[ai]) break;
            ai++;
        }
    }
    cargv[ai] = NULL;

    int out_pipe[2];
    int err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) {
        for (size_t i = 0; i < ai; i++) free(cargv[i]);
        free(cargv);
        return 0;
    }

    pid_t pid = fork();
    if (pid == 0) {
        setpgid(0, 0);
        if (cwd_opt && *cwd_opt) {
            (void)chdir(cwd_opt);
        }
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        execvp(cmd, cargv);
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);
    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);

    char *out_buf = NULL;
    char *err_buf = NULL;
    size_t out_len = 0, out_cap = 0;
    size_t err_len = 0, err_cap = 0;
    bool truncated = false;
    bool timed_out = false;

    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int status = 0;
    bool out_open = true;
    bool err_open = true;
    while (out_open || err_open) {
        int timeout_poll = -1;
        if (timeout_ms > 0) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            int64_t elapsed = (now.tv_sec - start.tv_sec) * 1000 +
                              (now.tv_nsec - start.tv_nsec) / 1000000;
            int64_t remain = timeout_ms - elapsed;
            if (remain <= 0) {
                timed_out = true;
                break;
            }
            timeout_poll = (int)remain;
        }

        struct pollfd fds[2];
        nfds_t nfds = 0;
        if (out_open) {
            fds[nfds].fd = out_pipe[0];
            fds[nfds].events = POLLIN;
            nfds++;
        }
        if (err_open) {
            fds[nfds].fd = err_pipe[0];
            fds[nfds].events = POLLIN;
            nfds++;
        }

        int pr = poll(fds, nfds, timeout_poll);
        if (pr < 0 && errno != EINTR) break;

        char tmp[1024];
        if (out_open) {
            ssize_t r = read(out_pipe[0], tmp, sizeof(tmp));
            if (r > 0) {
                if (!buffer_append_limited(&out_buf, &out_len, &out_cap, tmp, (size_t)r, (size_t)max_output, &truncated)) break;
            } else if (r == 0) {
                out_open = false;
                close(out_pipe[0]);
            }
        }
        if (err_open) {
            ssize_t r = read(err_pipe[0], tmp, sizeof(tmp));
            if (r > 0) {
                if (!buffer_append_limited(&err_buf, &err_len, &err_cap, tmp, (size_t)r, (size_t)max_output, &truncated)) break;
            } else if (r == 0) {
                err_open = false;
                close(err_pipe[0]);
            }
        }

        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid && !out_open && !err_open) break;
    }

    if (timed_out) {
        kill(-pid, SIGKILL);
        waitpid(pid, &status, 0);
    } else {
        waitpid(pid, &status, 0);
    }

    if (out_open) close(out_pipe[0]);
    if (err_open) close(err_pipe[0]);

    for (size_t i = 0; i < ai; i++) free(cargv[i]);
    free(cargv);

    cs_value mapv = cs_map(vm);
    if (!mapv.as.p) {
        free(out_buf);
        free(err_buf);
        return 0;
    }

    cs_value v_stdout = cs_str(vm, out_buf ? out_buf : "");
    cs_value v_stderr = cs_str(vm, err_buf ? err_buf : "");
    cs_value v_code = cs_int(WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    cs_value v_signal = cs_int(WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    cs_value v_timed = cs_bool(timed_out ? 1 : 0);
    cs_value v_trunc = cs_bool(truncated ? 1 : 0);

    if (!map_put_move_local(vm, &mapv, "stdout", &v_stdout)) goto exec_oom;
    if (!map_put_move_local(vm, &mapv, "stderr", &v_stderr)) goto exec_oom;
    if (!map_put_move_local(vm, &mapv, "code", &v_code)) goto exec_oom;
    if (!map_put_move_local(vm, &mapv, "signal", &v_signal)) goto exec_oom;
    if (!map_put_move_local(vm, &mapv, "timed_out", &v_timed)) goto exec_oom;
    if (!map_put_move_local(vm, &mapv, "truncated", &v_trunc)) goto exec_oom;

    free(out_buf);
    free(err_buf);
    *out = mapv;
    return 0;

exec_oom:
    cs_value_release(v_stdout);
    cs_value_release(v_stderr);
    cs_value_release(v_code);
    cs_value_release(v_signal);
    cs_value_release(v_timed);
    cs_value_release(v_trunc);
    cs_value_release(mapv);
    free(out_buf);
    free(err_buf);
    cs_error(vm, "out of memory");
    return 1;
}

static int nf_fm_env(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) {
        *out = cs_nil();
        return 0;
    }
    const char *name = cs_to_cstr(argv[0]);
    if (!name || !*name) {
        *out = cs_nil();
        return 0;
    }
    const char *val = getenv(name);
    if (!val) {
        *out = cs_nil();
        return 0;
    }
    *out = cs_str(vm, val);
    return 0;
}

static int nf_fm_set_env(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)ud;
    int ok = 0;
    if (argc == 2 && argv[0].type == CS_T_STR && argv[1].type == CS_T_STR) {
        const char *name = cs_to_cstr(argv[0]);
        const char *val = cs_to_cstr(argv[1]);
        if (name && *name) {
            ok = (setenv(name, val ? val : "", 1) == 0) ? 1 : 0;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_config_get(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) {
        *out = cs_nil();
        return 0;
    }

    const char *key = cs_to_cstr(argv[0]);
    if (!key || !*key) {
        *out = cs_nil();
        return 0;
    }

    int *field = NULL;
    if (!config_key_to_field(key, &field)) {
        *out = cs_nil();
        return 0;
    }

    if (strcmp(key, "info_label_width") == 0) {
        *out = cs_int(*field);
        return 0;
    }

    char buf[64];
    keycode_to_config_string_local(*field, buf, sizeof(buf));
    *out = cs_str(vm, buf);
    return 0;
}

static int nf_fm_config_set(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)ud;
    int ok = 0;
    if (argc == 2 && argv[0].type == CS_T_STR) {
        const char *key = cs_to_cstr(argv[0]);
        if (key && *key) {
            int *field = NULL;
            if (config_key_to_field(key, &field)) {
                if (strcmp(key, "info_label_width") == 0) {
                    if (argv[1].type == CS_T_INT) {
                        *field = (int)argv[1].as.i;
                        ok = 1;
                    } else if (argv[1].type == CS_T_STR) {
                        const char *s = cs_to_cstr(argv[1]);
                        char *end = NULL;
                        long v = s ? strtol(s, &end, 10) : 0;
                        if (s && end && *end == '\0') {
                            *field = (int)v;
                            ok = 1;
                        }
                    }
                } else {
                    if (argv[1].type == CS_T_INT) {
                        *field = (int)argv[1].as.i;
                        ok = 1;
                    } else if (argv[1].type == CS_T_STR) {
                        const char *s = cs_to_cstr(argv[1]);
                        int parsed = parse_key_local(s);
                        if (parsed != -1) {
                            *field = parsed;
                            ok = 1;
                        }
                    }
                }
            }
        }
    }

    if (ok) {
        char path[MAX_PATH_LENGTH];
        char errbuf[256] = {0};
        if (get_config_path(path, sizeof(path))) {
            if (!write_default_config_file(path, &g_kb, errbuf, sizeof(errbuf))) {
                ok = 0;
            }
        } else {
            ok = 0;
        }
    }

    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_cache_get(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)ud;
    if (!out) return 0;
    if (argc != 1 || argv[0].type != CS_T_STR) {
        *out = cs_nil();
        return 0;
    }
    const char *key = cs_to_cstr(argv[0]);
    if (!key || !*key) {
        *out = cs_nil();
        return 0;
    }

    char path[MAX_PATH_LENGTH];
    if (!get_cache_path(path, sizeof(path))) {
        *out = cs_nil();
        return 0;
    }

    FILE *fp = fopen(path, "r");
    if (!fp) {
        *out = cs_nil();
        return 0;
    }

    char line[2048];
    while (fgets(line, sizeof(line), fp)) {
        line[strcspn(line, "\n")] = '\0';
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        *tab = '\0';
        char *raw_key = line;
        char *raw_val = tab + 1;
        char *k = unescape_kv(raw_key);
        if (!k) continue;
        bool match = (strcmp(k, key) == 0);
        free(k);
        if (!match) continue;
        cs_value v = decode_cache_value(vm, raw_val);
        fclose(fp);
        *out = v;
        return 0;
    }

    fclose(fp);
    *out = cs_nil();
    return 0;
}

static int nf_fm_cache_set(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)ud;
    int ok = 0;
    if (argc != 2 || argv[0].type != CS_T_STR) {
        if (out) *out = cs_bool(0);
        return 0;
    }

    const char *key = cs_to_cstr(argv[0]);
    if (!key || !*key) {
        if (out) *out = cs_bool(0);
        return 0;
    }

    if (!ensure_cache_dir()) {
        if (out) *out = cs_bool(0);
        return 0;
    }

    char path[MAX_PATH_LENGTH];
    if (!get_cache_path(path, sizeof(path))) {
        if (out) *out = cs_bool(0);
        return 0;
    }

    char tmp_path[MAX_PATH_LENGTH];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *in = fopen(path, "r");
    FILE *outf = fopen(tmp_path, "w");
    if (!outf) {
        if (in) fclose(in);
        if (out) *out = cs_bool(0);
        return 0;
    }

    char *encoded_key = escape_kv(key);
    char *encoded_val = (argv[1].type == CS_T_NIL) ? NULL : encode_cache_value(&argv[1]);
    if (!encoded_key || (argv[1].type != CS_T_NIL && !encoded_val)) {
        if (in) fclose(in);
        fclose(outf);
        free(encoded_key);
        free(encoded_val);
        unlink(tmp_path);
        if (out) *out = cs_bool(0);
        return 0;
    }

    bool replaced = false;
    if (in) {
        char line[2048];
        while (fgets(line, sizeof(line), in)) {
            line[strcspn(line, "\n")] = '\0';
            char *tab = strchr(line, '\t');
            if (!tab) continue;
            *tab = '\0';
            char *raw_key = line;
            if (strcmp(raw_key, encoded_key) == 0) {
                if (argv[1].type != CS_T_NIL) {
                    fprintf(outf, "%s\t%s\n", encoded_key, encoded_val);
                }
                replaced = true;
            } else {
                fprintf(outf, "%s\t%s\n", raw_key, tab + 1);
            }
        }
        fclose(in);
    }

    if (!replaced && argv[1].type != CS_T_NIL) {
        fprintf(outf, "%s\t%s\n", encoded_key, encoded_val);
    }

    fclose(outf);
    ok = (rename(tmp_path, path) == 0) ? 1 : 0;
    free(encoded_key);
    free(encoded_val);

    if (out) *out = cs_bool(ok);
    return 0;
}

static int map_put_move_local(cs_vm *vm, cs_value *map_val, const char *key, cs_value *v) {
    if (!map_val || map_val->type != CS_T_MAP || !key || !v) return 0;
    if (cs_map_set(*map_val, key, *v) != 0) return 0;
    cs_value_release(*v);
    *v = cs_nil();
    return 1;
}

static int nf_fm_entries(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)argc;
    (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    if (!out) return 0;

    cs_value list = cs_list(vm);
    if (!list.as.p) {
        cs_error(vm, "out of memory");
        return 1;
    }

    const Vector *view = (pm ? pm->view : NULL);
    if (!pm || !view || !view->el) {
        *out = list;
        return 0;
    }

    magic_t mc = magic_open(MAGIC_MIME_TYPE | MAGIC_SYMLINK | MAGIC_CHECK);
    if (mc && magic_load(mc, NULL) != 0) {
        magic_close(mc);
        mc = NULL;
    }

    size_t n = Vector_len(*view);
    for (size_t i = 0; i < n; i++) {
        FileAttr fa = (FileAttr)view->el[i];
        const char *name = FileAttr_get_name(fa);
        if (!name) name = "";
        bool is_dir = FileAttr_is_dir(fa);

        char full[MAX_PATH_LENGTH];
        full[0] = '\0';
        if (pm->cwd[0] && name[0]) {
            path_join(full, pm->cwd, name);
        }

        struct stat st;
        memset(&st, 0, sizeof(st));
        if (full[0]) {
            (void)lstat(full, &st);
        }

        const char *mime = "unknown";
        if (is_dir) {
            mime = "inode/directory";
        } else if (mc && full[0]) {
            const char *m = magic_file(mc, full);
            if (m && *m) mime = m;
        }

        cs_value m = cs_map(vm);
        cs_value v = cs_nil();
        if (!m.as.p) goto oom;
        v = cs_str(vm, name);
        if (!map_put_move_local(vm, &m, "name", &v)) goto oom;
        v = cs_bool(is_dir ? 1 : 0);
        if (!map_put_move_local(vm, &m, "is_dir", &v)) goto oom;
        v = cs_int((int64_t)st.st_size);
        if (!map_put_move_local(vm, &m, "size", &v)) goto oom;
        v = cs_int((int64_t)st.st_mtime);
        if (!map_put_move_local(vm, &m, "mtime", &v)) goto oom;
        v = cs_int((int64_t)st.st_mode);
        if (!map_put_move_local(vm, &m, "mode", &v)) goto oom;
        v = cs_str(vm, mime);
        if (!map_put_move_local(vm, &m, "mime", &v)) goto oom;

        if (!list_push_local((cs_list_obj *)list.as.p, m)) goto oom;
        cs_value_release(m);
        continue;

    oom:
        cs_value_release(v);
        cs_value_release(m);
        if (mc) magic_close(mc);
        cs_value_release(list);
        cs_error(vm, "out of memory");
        return 1;
    }

    if (mc) magic_close(mc);
    *out = list;
    return 0;
}

static int nf_fm_open_selected(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && pm->selected[0]) {
        pm->open_selected_requested = true;
        ok = 1;
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_open(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1 && argv[0].type == CS_T_STR) {
        const char *p = cs_to_cstr(argv[0]);
        if (p && *p) {
            strncpy(pm->open_path, p, sizeof(pm->open_path) - 1);
            pm->open_path[sizeof(pm->open_path) - 1] = '\0';
            pm->open_path_requested = true;
            ok = 1;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_preview(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1 && argv[0].type == CS_T_STR) {
        const char *p = cs_to_cstr(argv[0]);
        if (p && *p) {
            strncpy(pm->preview_path, p, sizeof(pm->preview_path) - 1);
            pm->preview_path[sizeof(pm->preview_path) - 1] = '\0';
            pm->preview_path_requested = true;
            ok = 1;
        }
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_enter_dir(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && pm->selected[0]) {
        pm->enter_dir_requested = true;
        ok = 1;
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_parent_dir(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && pm->cwd[0]) {
        pm->parent_dir_requested = true;
        ok = 1;
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_set_search(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1 && argv[0].type == CS_T_STR) {
        const char *q = cs_to_cstr(argv[0]);
        if (!q) q = "";
        strncpy(pm->requested_search_query, q, sizeof(pm->requested_search_query) - 1);
        pm->requested_search_query[sizeof(pm->requested_search_query) - 1] = '\0';
        pm->set_search_requested = true;
        ok = 1;
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_clear_search(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm; (void)argc; (void)argv;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm) {
        pm->clear_search_requested = true;
        ok = 1;
    }
    if (out) *out = cs_bool(ok);
    return 0;
}

static int nf_fm_search_set_mode(cs_vm *vm, void *ud, int argc, const cs_value *argv, cs_value *out) {
    (void)vm;
    PluginManager *pm = (PluginManager *)ud;
    int ok = 0;
    if (pm && argc == 1) {
        int mode = -1;
        if (argv[0].type == CS_T_STR) {
            const char *s = cs_to_cstr(argv[0]);
            if (s) {
                if (strcmp(s, "fuzzy") == 0) mode = SEARCH_MODE_FUZZY;
                else if (strcmp(s, "exact") == 0) mode = SEARCH_MODE_EXACT;
                else if (strcmp(s, "regex") == 0) mode = SEARCH_MODE_REGEX;
            }
        } else if (argv[0].type == CS_T_INT) {
            int v = (int)argv[0].as.i;
            if (v >= SEARCH_MODE_FUZZY && v <= SEARCH_MODE_REGEX) mode = v;
        }
        if (mode != -1) {
            pm->requested_search_mode = mode;
            pm->set_search_mode_requested = true;
            ok = 1;
        }
    }
    if (out) *out = cs_bool(ok);
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
    cs_register_http_stdlib(vm);
    cs_register_native(vm, "fm.notify", nf_fm_notify, pm);
    cs_register_native(vm, "fm.status", nf_fm_notify, pm); // alias
    cs_register_native(vm, "fm.on", nf_fm_on, pm);
    cs_register_native(vm, "fm.ui_status_set", nf_fm_ui_status_set, pm);
    cs_register_native(vm, "fm.ui_status_clear", nf_fm_ui_status_clear, pm);
    cs_register_native(vm, "fm.mark", nf_fm_mark, pm);
    cs_register_native(vm, "fm.goto_mark", nf_fm_goto_mark, pm);
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
    cs_register_native(vm, "fm.select_paths", nf_fm_select_paths, pm);
    cs_register_native(vm, "fm.set_selected_paths", nf_fm_set_selected_paths, pm);
    cs_register_native(vm, "fm.clear_selected_paths", nf_fm_clear_selected_paths, pm);
    cs_register_native(vm, "fm.each_selected", nf_fm_each_selected, pm);
    cs_register_native(vm, "fm.cursor", nf_fm_cursor, pm);
    cs_register_native(vm, "fm.count", nf_fm_count, pm);
    cs_register_native(vm, "fm.search_active", nf_fm_search_active, pm);
    cs_register_native(vm, "fm.search_query", nf_fm_search_query, pm);
    cs_register_native(vm, "fm.search_set_mode", nf_fm_search_set_mode, pm);
    cs_register_native(vm, "fm.editor_active", nf_fm_editor_active, pm);
    cs_register_native(vm, "fm.editor_get_path", nf_fm_editor_get_path, pm);
    cs_register_native(vm, "fm.editor_get_content", nf_fm_editor_get_content, pm);
    cs_register_native(vm, "fm.editor_get_line", nf_fm_editor_get_line, pm);
    cs_register_native(vm, "fm.info", nf_fm_info, pm);
    cs_register_native(vm, "fm.exec", nf_fm_exec, pm);
    cs_register_native(vm, "fm.env", nf_fm_env, pm);
    cs_register_native(vm, "fm.set_env", nf_fm_set_env, pm);
    cs_register_native(vm, "fm.config_get", nf_fm_config_get, pm);
    cs_register_native(vm, "fm.config_set", nf_fm_config_set, pm);
    cs_register_native(vm, "fm.cache_get", nf_fm_cache_get, pm);
    cs_register_native(vm, "fm.cache_set", nf_fm_cache_set, pm);
    cs_register_native(vm, "fm.clipboard_get", nf_fm_clipboard_get, pm);
    cs_register_native(vm, "fm.clipboard_set", nf_fm_clipboard_set, pm);
    cs_register_native(vm, "fm.set_search", nf_fm_set_search, pm);
    cs_register_native(vm, "fm.clear_search", nf_fm_clear_search, pm);
    cs_register_native(vm, "fm.pane", nf_fm_pane, pm);
    cs_register_native(vm, "fm.entries", nf_fm_entries, pm);
    cs_register_native(vm, "fm.reload", nf_fm_reload, pm);
    cs_register_native(vm, "fm.exit", nf_fm_exit, pm);
    cs_register_native(vm, "fm.cd", nf_fm_cd, pm);
    cs_register_native(vm, "fm.select", nf_fm_select, pm);
    cs_register_native(vm, "fm.select_index", nf_fm_select_index, pm);
    cs_register_native(vm, "fm.open_selected", nf_fm_open_selected, pm);
    cs_register_native(vm, "fm.open", nf_fm_open, pm);
    cs_register_native(vm, "fm.preview", nf_fm_preview, pm);
    cs_register_native(vm, "fm.enter_dir", nf_fm_enter_dir, pm);
    cs_register_native(vm, "fm.parent_dir", nf_fm_parent_dir, pm);
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
        if (cs_call(vm, "on_load", 0, NULL, &out) != 0) {
            const char *err = cs_vm_last_error(vm);
            char msg[512];
            snprintf(msg, sizeof(msg), "Plugin on_load failed: %s: %s", ent->d_name, err ? err : "");
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
    if (!pm) return;
    for (size_t i = 0; i < pm->bind_count; i++) {
        free(pm->bindings[i].func);
    }
    free(pm->bindings);
    pm->bindings = NULL;
    pm->bind_count = pm->bind_cap = 0;

    for (size_t i = 0; i < pm->event_bind_count; i++) {
        if (!pm->event_bindings) break;
        if (!pm->event_bindings[i].cb_is_name && pm->event_bindings[i].cb.type != CS_T_NIL) {
            cs_value_release(pm->event_bindings[i].cb);
        }
    }
    free(pm->event_bindings);
    pm->event_bindings = NULL;
    pm->event_bind_count = pm->event_bind_cap = 0;

    for (size_t i = 0; i < pm->plugin_count; i++) {
        if (pm->plugins[i].vm) cs_vm_free(pm->plugins[i].vm);
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
        for (size_t i = 0; i < pm->selected_path_count; i++) free(pm->selected_paths[i]);
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
        .select_all_active = false,
        .search_active = false,
        .search_query = "",
        .active_pane = 0,
        .view = NULL,
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

static void dispatch_event2_str(PluginManager *pm, const char *event, const char *a, const char *b) {
    if (!pm || !event || !*event) return;
    for (size_t i = 0; i < pm->event_bind_count; i++) {
        EventBinding *eb = &pm->event_bindings[i];
        if (!eb->vm) continue;
        if (strcmp(eb->event, event) != 0) continue;
        cs_value args[2];
        args[0] = cs_str(eb->vm, a ? a : "");
        args[1] = cs_str(eb->vm, b ? b : "");
        cs_value out = cs_nil();
        int rc = 0;
        if (eb->cb_is_name && eb->cb_name[0]) {
            rc = cs_call(eb->vm, eb->cb_name, 2, args, &out);
        } else if (!eb->cb_is_name && (eb->cb.type == CS_T_FUNC || eb->cb.type == CS_T_NATIVE)) {
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
    if (!pm || !ctx) return;

    const char *new_cwd = ctx->cwd ? ctx->cwd : "";
    const char *new_sel = ctx->selected_name ? ctx->selected_name : "";

    bool cwd_changed = pm->context_initialized &&
                       (strncmp(pm->cwd, new_cwd, sizeof(pm->cwd)) != 0);
    bool sel_changed = pm->context_initialized &&
                       (strncmp(pm->selected, new_sel, sizeof(pm->selected)) != 0);
    int old_pane_val = pm->active_pane;
    bool pane_changed = pm->context_initialized && (pm->active_pane != ctx->active_pane);

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
    strncpy(pm->search_query, ctx->search_query ? ctx->search_query : "", sizeof(pm->search_query) - 1);
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
            call_void2_str(pm, pm->plugins[i].vm, "on_selection_change", pm->selected, old_sel);
        }
        dispatch_event2_str(pm, "selection_change", pm->selected, old_sel);
    }
    if (pane_changed) {
        const char *new_pane = (pm->active_pane == 1) ? "directory" : (pm->active_pane == 2) ? "preview" : "unknown";
        const char *old_pane = (old_pane_val == 1) ? "directory" : (old_pane_val == 2) ? "preview" : "unknown";
        dispatch_event2_str(pm, "pane_change", new_pane, old_pane);
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

bool plugins_take_open_selected_request(PluginManager *pm) {
    if (!pm) return false;
    bool v = pm->open_selected_requested;
    pm->open_selected_requested = false;
    return v;
}

bool plugins_take_open_path_request(PluginManager *pm, char *out_path, size_t out_len) {
    if (!pm || !out_path || out_len == 0) return false;
    if (!pm->open_path_requested) return false;
    strncpy(out_path, pm->open_path, out_len - 1);
    out_path[out_len - 1] = '\0';
    pm->open_path_requested = false;
    pm->open_path[0] = '\0';
    return true;
}

bool plugins_take_preview_path_request(PluginManager *pm, char *out_path, size_t out_len) {
    if (!pm || !out_path || out_len == 0) return false;
    if (!pm->preview_path_requested) return false;
    strncpy(out_path, pm->preview_path, out_len - 1);
    out_path[out_len - 1] = '\0';
    pm->preview_path_requested = false;
    pm->preview_path[0] = '\0';
    return true;
}

bool plugins_take_enter_dir_request(PluginManager *pm) {
    if (!pm) return false;
    bool v = pm->enter_dir_requested;
    pm->enter_dir_requested = false;
    return v;
}

bool plugins_take_parent_dir_request(PluginManager *pm) {
    if (!pm) return false;
    bool v = pm->parent_dir_requested;
    pm->parent_dir_requested = false;
    return v;
}

bool plugins_take_set_search_request(PluginManager *pm, char *out_query, size_t out_len) {
    if (!pm || !out_query || out_len == 0) return false;
    if (!pm->set_search_requested) return false;
    strncpy(out_query, pm->requested_search_query, out_len - 1);
    out_query[out_len - 1] = '\0';
    pm->set_search_requested = false;
    pm->requested_search_query[0] = '\0';
    return true;
}

bool plugins_take_clear_search_request(PluginManager *pm) {
    if (!pm) return false;
    bool v = pm->clear_search_requested;
    pm->clear_search_requested = false;
    return v;
}

bool plugins_take_set_search_mode_request(PluginManager *pm, int *out_mode) {
    if (!pm || !out_mode) return false;
    if (!pm->set_search_mode_requested) return false;
    *out_mode = pm->requested_search_mode;
    pm->set_search_mode_requested = false;
    pm->requested_search_mode = SEARCH_MODE_FUZZY;
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
