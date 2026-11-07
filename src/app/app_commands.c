#define _GNU_SOURCE
#include "app_commands.h"
#include "app_help.h"
#include "app_mouse.h"
#include "app_panes.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_paths.h"
#include "app_windows.h"
#include "clipboard.h"
#include "input_prompt.h"
#include "main.h"
#include "mime.h"
#include "process.h"
#include "search.h"
#include "task_ui.h"
#include "tasks.h"
#include "ui.h"

static void notify_result(bool ok, const char *action) {
    show_notification(notifwin, "%s%s", action, ok ? "" : ": failed");
    should_clear_notif = false;
}

static void find_next(AppState *state, bool previous) {
    if (!state->find_query[0])
        return;
    bool sensitive = false;
    for (const unsigned char *p = (const unsigned char *)state->find_query; *p; p++)
        sensitive |= isupper(*p) != 0;
    regex_t regex;
    int status =
        regcomp(&regex, state->find_query, REG_EXTENDED | REG_NOSUB | (sensitive ? 0 : REG_ICASE));
    if (status) {
        char error[256];
        regerror(status, &regex, error, sizeof(error));
        show_notification(notifwin, "Find: %s", error);
        should_clear_notif = false;
        return;
    }
    Vector *view = active_files(state);
    size_t count = Vector_len(*view), cursor = (size_t)state->dir_window_cas.cursor;
    bool found = false;
    for (size_t step = 1; step <= count; step++) {
        size_t at = previous ? (cursor + count - step) % count : (cursor + step) % count;
        if (regexec(&regex, FileAttr_get_name(view->el[at]), 0, NULL, 0) == 0) {
            app_move_cursor(state, (SIZE)at);
            found = true;
            break;
        }
    }
    regfree(&regex);
    if (!found)
        notify_result(false, "No matching file");
}

static bool browser_paths(AppState *state, ClipboardPaths *out) {
    *out = (ClipboardPaths){0};
    Vector paths;
    if (!app_selection_paths(state, &paths))
        return false;
    if (!Vector_len(paths)) {
        Vector_bye(&paths);
        return false;
    }
    out->count = Vector_len(paths);
    out->paths = (char **)paths.el;
    return out->count != 0;
}

static void submit_operation(AppState *state, TaskKind kind, bool overwrite) {
    ClipboardPaths paths;
    bool paste = kind == TASK_COPY || kind == TASK_MOVE || kind == TASK_LINK ||
                 kind == TASK_RELATIVE_LINK || kind == TASK_HARDLINK;
    bool ok = paste ? clipboard_get_paths(&paths) : browser_paths(state, &paths);
    if (!ok) {
        notify_result(false, paste ? "Nothing yanked" : "No selection");
        return;
    }
    if (kind == TASK_TRASH || kind == TASK_DELETE) {
        char answer[8] = {0}, title[120];
        snprintf(title, sizeof(title), "%s %zu selected item(s)? (y/N)",
                 kind == TASK_TRASH ? "Trash" : "Permanently delete", paths.count);
        if (!input_prompt(title, answer, sizeof(answer)) ||
            (strcmp(answer, "y") && strcmp(answer, "Y"))) {
            clipboard_paths_free(&paths);
            return;
        }
    }
    if (paste && kind == TASK_COPY && !overwrite) {
        bool conflict = false;
        for (size_t i = 0; i < paths.count; i++) {
            char destination[MAX_PATH_LENGTH];
            path_join(destination, state->current_directory, basename_ptr(paths.paths[i]));
            struct stat existing;
            conflict |= lstat(destination, &existing) == 0;
        }
        if (conflict) {
            char answer[8] = {0};
            overwrite =
                input_prompt("Overwrite existing destinations? (y/N)", answer, sizeof(answer)) &&
                (!strcmp(answer, "y") || !strcmp(answer, "Y"));
            if (!overwrite) {
                clipboard_paths_free(&paths);
                return;
            }
        }
    }
    if (kind == TASK_COPY && paths.cut)
        kind = TASK_MOVE;
    ok = tasks_submit(kind, (const char *const *)paths.paths, paths.count,
                      paste ? state->current_directory : NULL, overwrite);
    if (ok)
        app_selection_clear(state);
    clipboard_paths_free(&paths);
    notify_result(ok, "Task queued (w to inspect)");
}

static void run_shell(AppState *state, const char *command) {
    Vector selected;
    if (!app_selection_paths(state, &selected)) {
        notify_result(false, "Read selected paths");
        return;
    }
    ClipboardPaths paths = {.paths = (char **)selected.el, .count = Vector_len(selected)};
    size_t count = paths.count;
    char **argv = calloc(count + 5, sizeof(*argv));
    if (!argv) {
        clipboard_paths_free(&paths);
        return;
    }
    argv[0] = "sh";
    argv[1] = "-c";
    argv[2] = (char *)command;
    argv[3] = "cupidfm";
    for (size_t i = 0; i < count; i++)
        argv[i + 4] = paths.paths[i];
    def_prog_mode();
    endwin();
    bool ok = run_process_in_directory(argv, state->current_directory, -1, -1);
    reset_prog_mode();
    refresh();
    clearok(stdscr, TRUE);
    resized = 1;
    app_reload_view(state);
    notify_result(ok, "Command finished");
    clipboard_paths_free(&paths);
    free(argv);
}

static void open_selected(AppState *state, bool interactive) {
    if (state->chooser_file && !interactive) {
        ClipboardPaths paths;
        if (!browser_paths(state, &paths))
            return;
        FILE *output = fopen(state->chooser_file, "w");
        bool ok = output != NULL;
        if (output) {
            for (size_t i = 0; i < paths.count; i++)
                if (fprintf(output, "%s\n", paths.paths[i]) < 0)
                    ok = false;
            if (fclose(output))
                ok = false;
        }
        clipboard_paths_free(&paths);
        state->quit_requested = ok;
        notify_result(ok, "Choose files");
        return;
    }
    Vector *view = active_files(state);
    SIZE cursor = state->dir_window_cas.cursor;
    FileAttr file = cursor >= 0 && (size_t)cursor < Vector_len(*view) ? view->el[cursor] : NULL;
    if (file && FileAttr_is_dir(file) && !app_selection_count(state)) {
        notify_result(app_change_directory(state, FileAttr_get_name(file)), "Open directory");
        return;
    }
    ClipboardPaths paths;
    if (!browser_paths(state, &paths))
        return;
    char path[MAX_PATH_LENGTH], type[512];
    snprintf(path, sizeof(path), "%s", paths.paths[0]);
    clipboard_paths_free(&paths);
    magic_t cookie = magic_open(MAGIC_MIME_TYPE | MAGIC_SYMLINK);
    if (cookie && magic_load(cookie, NULL)) {
        magic_close(cookie);
        cookie = NULL;
    }
    const char *mime =
        mime_type_cached(cookie, path, MAGIC_MIME_TYPE | MAGIC_SYMLINK, type, sizeof(type));
    const char *opener = "xdg-open";
    if (mime && (!strncmp(mime, "text/", 5) || is_supported_mime_type(mime) ||
                 !strcmp(mime, "inode/empty")))
        opener = getenv("EDITOR") && *getenv("EDITOR") ? getenv("EDITOR") : "vi";
    if (cookie)
        magic_close(cookie);
    char command[1024];
    snprintf(command, sizeof(command), "%s \"$@\"", opener);
    if (!interactive || input_prompt("Open with (files are in \"$@\")", command, sizeof(command)))
        run_shell(state, command);
}

static void create_entry(AppState *state) {
    char name[MAX_PATH_LENGTH] = {0}, path[MAX_PATH_LENGTH];
    if (!input_prompt("Create (end with / for a directory)", name, sizeof(name)) || !name[0])
        return;
    bool directory = name[strlen(name) - 1] == '/';
    while (name[0] && name[strlen(name) - 1] == '/')
        name[strlen(name) - 1] = '\0';
    if (!name[0])
        return;
    resolve_path_under_cwd(path, state->current_directory, name);
    bool ok = path[0] && ensure_parent_dir_local(path);
    if (ok && directory)
        ok = mkdir(path, 0755) == 0;
    else if (ok) {
        int fd = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0666);
        ok = fd >= 0;
        if (fd >= 0)
            close(fd);
    }
    if (ok) {
        (void)undo_state_set_single(
            &state->undo_state, directory ? UNDO_OP_CREATE_DIR : UNDO_OP_CREATE_FILE, NULL, path);
        app_reload_view(state);
        SIZE index = find_loaded_index_by_name(active_files(state), basename_ptr(path));
        if (index != (SIZE)-1)
            app_move_cursor(state, index);
    }
    notify_result(ok, "Create");
}

static void copy_field(AppState *state, int key) {
    char path[MAX_PATH_LENGTH], value[MAX_PATH_LENGTH * 3 + 16];
    const char *name = state->selected_entry ? state->selected_entry : "";
    path_join(path, state->current_directory, name);
    const char *source = key == 'd' || key == 'D' ? state->current_directory : path;
    if (key == 'f' || key == 'n') {
        snprintf(value, sizeof(value), "%s", name);
        if (key == 'n') {
            char *extension = strrchr(value + (value[0] == '.'), '.');
            if (extension)
                *extension = '\0';
        }
    } else if (key == 'C' || key == 'D') {
        size_t used = 7;
        strcpy(value, "file://");
        for (const unsigned char *p = (const unsigned char *)source; *p; p++) {
            if (isalnum(*p) || strchr("/-._~", *p))
                value[used++] = (char)*p;
            else {
                snprintf(value + used, sizeof(value) - used, "%%%02X", *p);
                used += 3;
            }
        }
        value[used] = '\0';
    } else {
        snprintf(value, sizeof(value), "%s", source);
    }
    notify_result(clipboard_set_text(value), "Copy");
}

static void follow_link(AppState *state) {
    if (!state->selected_entry || !*state->selected_entry)
        return;
    char path[MAX_PATH_LENGTH];
    path_join(path, state->current_directory, state->selected_entry);
    char *resolved = realpath(path, NULL);
    if (!resolved)
        return;
    struct stat info;
    if (!stat(resolved, &info) && S_ISDIR(info.st_mode)) {
        (void)app_change_directory(state, resolved);
    } else {
        char *slash = strrchr(resolved, '/');
        if (slash) {
            char name[MAX_PATH_LENGTH];
            snprintf(name, sizeof(name), "%s", slash + 1);
            slash[slash == resolved ? 1 : 0] = '\0';
            if (app_change_directory(state, resolved)) {
                SIZE at = find_loaded_index_by_name(active_files(state), name);
                if (at != (SIZE)-1)
                    app_move_cursor(state, at);
            }
        }
    }
    free(resolved);
}

static AppKeyResult prefix_key(AppState *state, AppTabs *tabs, int prefix, int key) {
    FileListingOptions options = file_listing_options();
    if (prefix == ',') {
        int lower = tolower((unsigned char)key);
        options.reverse = key >= 'A' && key <= 'Z';
        if (lower == 'a')
            options.sort = FILE_SORT_ALPHABETICAL;
        else if (lower == 'n')
            options.sort = FILE_SORT_NATURAL;
        else if (lower == 'e')
            options.sort = FILE_SORT_EXTENSION;
        else if (lower == 's') {
            options.sort = FILE_SORT_SIZE;
            options.linemode = FILE_LINE_SIZE;
        } else if (lower == 'm') {
            options.sort = FILE_SORT_MTIME;
            options.linemode = FILE_LINE_MTIME;
        } else if (lower == 'b') {
            options.sort = FILE_SORT_BTIME;
            options.linemode = FILE_LINE_BTIME;
        } else if (lower == 'r') {
            options.sort = FILE_SORT_RANDOM;
            options.random_seed++;
        } else
            return APP_KEY_HANDLED;
        file_listing_configure(options);
        app_reload_view(state);
    } else if (prefix == 'm') {
        const char *keys = "nspmbo";
        const char *at = key > 0 && key < 128 ? strchr(keys, key) : NULL;
        if (at) {
            static const FileLineMode modes[] = {FILE_LINE_NONE,        FILE_LINE_SIZE,
                                                 FILE_LINE_PERMISSIONS, FILE_LINE_MTIME,
                                                 FILE_LINE_BTIME,       FILE_LINE_OWNER};
            options.linemode = modes[at - keys];
            file_listing_configure(options);
        }
    } else if (prefix == 't') {
        if (key == 't')
            notify_result(app_tab_create(tabs, state), "New tab");
        else if (key == 'r')
            (void)input_prompt("Tab name", state->tab_name, sizeof(state->tab_name));
    } else if (prefix == 'g') {
        if (key == 'g')
            app_move_cursor(state, 0);
        else if (key == 'f')
            follow_link(state);
        else {
            char path[MAX_PATH_LENGTH] = {0};
            const char *home = getenv("HOME");
            if (key == ' ' && !input_prompt("Change directory", path, sizeof(path)))
                return APP_KEY_HANDLED;
            if (home && (key == 'h' || key == 'c' || key == 'd'))
                snprintf(path, sizeof(path), "%s%s", home,
                         key == 'h'   ? ""
                         : key == 'c' ? "/.config"
                                      : "/Downloads");
            if (path[0] == '~' && (!path[1] || path[1] == '/') && home) {
                char expanded[MAX_PATH_LENGTH];
                snprintf(expanded, sizeof(expanded), "%s%s", home, path + 1);
                snprintf(path, sizeof(path), "%s", expanded);
            }
            if (path[0])
                notify_result(app_change_directory(state, path), "Change directory");
        }
    } else if (prefix == 'c' && key > 0 && key < 128 && strchr("cCdDfn", key)) {
        copy_field(state, key);
    }
    return APP_KEY_HANDLED;
}

static void seek_preview(AppState *state, int amount) {
    if (g_kb.vim_keymap && previewwin && app_child_refresh(state)) {
        SIZE bound = MAX(0, (SIZE)Vector_len(state->child.files) - getmaxy(previewwin));
        state->preview_start_line = MAX(0, MIN((SIZE)state->preview_start_line + amount, bound));
        return;
    }
    char path[MAX_PATH_LENGTH];
    if (state->preview_override_active)
        snprintf(path, sizeof(path), "%s", state->preview_override_path);
    else
        path_join(path, state->current_directory,
                  state->selected_entry ? state->selected_entry : "");
    for (int i = 0; i < abs(amount); i++) {
        if (amount < 0)
            state->preview_start_line = MAX(0, state->preview_start_line - 1);
        else if (previewwin &&
                 preview_can_scroll_down(path, state->preview_start_line, getmaxx(previewwin) - 4,
                                         getmaxy(previewwin) - (g_kb.vim_keymap ? 2 : 8)))
            state->preview_start_line++;
    }
}

AppKeyResult app_browser_key(AppState *state, AppTabs *tabs, int *key, const KeyBindings *kb) {
    int ch = *key;
    bool vim = kb->vim_keymap;
    if (ch == ERR)
        return APP_KEY_PASS;
    if (ch == KEY_MOUSE && vim) {
        MEVENT event;
        if (getmouse(&event) == OK) {
            AppMouseResult result = app_mouse_event(state, &event);
            if (result == APP_MOUSE_PREVIEW_UP || result == APP_MOUSE_PREVIEW_DOWN)
                seek_preview(state, result == APP_MOUSE_PREVIEW_UP ? -3 : 3);
            if (result == APP_MOUSE_ENTER) {
                Vector *view = active_files(state);
                SIZE at = state->dir_window_cas.cursor;
                if (at >= 0 && (size_t)at < Vector_len(*view) && FileAttr_is_dir(view->el[at]))
                    (void)app_change_directory(state, FileAttr_get_name(view->el[at]));
                else
                    open_selected(state, false);
            }
        }
        return state->quit_requested ? APP_KEY_QUIT : APP_KEY_HANDLED;
    }
    if (state->mouse_drag) {
        state->mouse_drag = false;
        state->visual_active = false;
    }
    if (state->prefix_key) {
        int prefix = state->prefix_key;
        state->prefix_key = 0;
        werase(notifwin);
        wrefresh(notifwin);
        should_clear_notif = true;
        if (ch == 27)
            return APP_KEY_HANDLED;
        return prefix_key(state, tabs, prefix, ch);
    }
    if (!vim) {
        const int assigned[] = {
            kb->key_up,          kb->key_down,       kb->key_left, kb->key_right, kb->key_tab,
            kb->key_exit,        kb->key_edit,       kb->key_copy, kb->key_paste, kb->key_cut,
            kb->key_delete,      kb->key_rename,     kb->key_new,  kb->key_save,  kb->key_new_dir,
            kb->key_search,      kb->key_select_all, kb->key_info, kb->key_undo,  kb->key_redo,
            kb->key_permissions, kb->key_console,    kb->key_help};
        for (size_t i = 0; i < sizeof(assigned) / sizeof(*assigned); i++)
            if (ch == assigned[i])
                return APP_KEY_PASS;
    }
    if (ch == ',' || ch == 't' || ch == 'g' || (vim && (ch == 'm' || ch == 'c'))) {
        state->prefix_key = ch;
        show_notification(notifwin, "%s", app_help_prefix(ch));
        should_clear_notif = false;
        return APP_KEY_HANDLED;
    }
    if (ch == 27) {
        if (state->find_query[0])
            state->find_query[0] = '\0';
        else if (state->visual_active)
            state->visual_active = false;
        else if (state->search_active) {
            search_clear(state);
            app_move_cursor(state, state->dir_window_cas.cursor);
        } else if (state->select_all_active || app_selection_count(state))
            app_selection_clear(state);
        return APP_KEY_HANDLED;
    }
    if (ch == ' ') {
        Vector *view = active_files(state);
        SIZE at = state->dir_window_cas.cursor;
        if (at >= 0 && (size_t)at < Vector_len(*view)) {
            FileAttr file = view->el[at];
            app_selection_set(state, file, !FileAttr_selected(file));
            app_move_cursor(state, at + 1);
        }
        return APP_KEY_HANDLED;
    }
    if (ch == '.') {
        FileListingOptions options = file_listing_options();
        options.show_hidden = !options.show_hidden;
        file_listing_configure(options);
        app_reload_view(state);
        return APP_KEY_HANDLED;
    }
    if (ch == KEY_HOME || ch == KEY_END || ch == KEY_PPAGE || ch == KEY_NPAGE ||
        (vim && (ch == 'G' || ch == 21 || ch == 4 || ch == 2 || ch == 6 || ch == 'j' || ch == 'k' ||
                 ch == KEY_UP || ch == KEY_DOWN))) {
        SIZE count = (SIZE)Vector_len(*active_files(state));
        SIZE page = MAX(1, state->dir_window_cas.num_lines - 2);
        SIZE next = state->dir_window_cas.cursor;
        if (ch == KEY_HOME)
            next = 0;
        else if (ch == KEY_END || ch == 'G')
            next = count - 1;
        else if (ch == 21 || ch == 4)
            next += ch == 21 ? -MAX(1, page / 2) : MAX(1, page / 2);
        else if (ch == 'k' || ch == KEY_UP) {
            if (count && next == 0) {
                next = count - 1;
                if (state->visual_active)
                    state->visual_wraps--;
            } else
                next--;
        } else if (ch == 'j' || ch == KEY_DOWN) {
            if (count && next == count - 1) {
                next = 0;
                if (state->visual_active)
                    state->visual_wraps++;
            } else
                next++;
        } else
            next += ch == KEY_PPAGE || ch == 2 ? -page : page;
        app_move_cursor(state, next);
        return APP_KEY_HANDLED;
    }
    if (ch == '[' || ch == ']') {
        size_t next = (tabs->active + tabs->count + (ch == '[' ? -1 : 1)) % tabs->count;
        (void)app_tab_switch(tabs, state, next);
        return APP_KEY_HANDLED;
    }
    if (ch == '{' || ch == '}') {
        size_t next = ch == '{' ? (tabs->active ? tabs->active - 1 : 0)
                                : MIN(tabs->active + 1, tabs->count - 1);
        AppState *other = tabs->saved[next];
        tabs->saved[next] = NULL;
        tabs->saved[tabs->active] = other;
        tabs->active = next;
        return APP_KEY_HANDLED;
    }
    if (ch >= '1' && ch <= '9') {
        (void)app_tab_switch(tabs, state, (size_t)(ch - '1'));
        return APP_KEY_HANDLED;
    }
    if (!vim)
        return APP_KEY_PASS;
    if (ch == 'q' || ch == 'Q') {
        if (!task_ui_can_quit())
            return APP_KEY_HANDLED;
        state->no_cwd_file = ch == 'Q';
        return APP_KEY_QUIT;
    }
    if (ch == 3)
        return app_tab_close(tabs, state) || !task_ui_can_quit() ? APP_KEY_HANDLED : APP_KEY_QUIT;
    if (ch == KEY_F(1) || ch == '~') {
        show_help_menu(kb);
    } else if (ch == 'h' || ch == 'l') {
        *key = ch == 'h' ? kb->key_left : kb->key_right;
        return APP_KEY_PASS;
    } else if (ch == 'H' || ch == 'L') {
        (void)app_history_move(state, ch == 'H' ? -1 : 1);
    } else if (ch == 1 || ch == 18) {
        app_selection_toggle_all(state, ch == 18);
    } else if (ch == 'v' || ch == 'V') {
        Vector *view = active_files(state);
        for (size_t i = 0; i < Vector_len(*view); i++)
            FileAttr_visual_begin(view->el[i]);
        state->visual_active = true;
        state->visual_unset = ch == 'V';
        state->visual_wraps = 0;
        state->visual_anchor = state->dir_window_cas.cursor;
        app_move_cursor(state, state->visual_anchor);
    } else if (ch == '/' || ch == '?') {
        if (input_prompt(ch == '/' ? "Find next" : "Find previous", state->find_query,
                         sizeof(state->find_query)))
            find_next(state, ch == '?');
    } else if (ch == 'n' || ch == 'N') {
        find_next(state, ch == 'N');
    } else if (ch == 'f') {
        char query[MAX_PATH_LENGTH];
        snprintf(query, sizeof(query), "%s", state->search_query);
        if (input_prompt("Filter", query, sizeof(query))) {
            state->search_mode = SEARCH_MODE_SMART;
            snprintf(state->search_query, sizeof(state->search_query), "%s", query);
            state->search_active = query[0] != '\0';
            search_rebuild(state, query);
            app_move_cursor(state, 0);
        }
    } else if (ch == 'J' || ch == 'K') {
        seek_preview(state, ch == 'J' ? 5 : -5);
    } else if (ch == 'Y' || ch == 'X') {
        clipboard_clear_paths();
        copied_filename[0] = '\0';
        app_selection_clear(state);
    } else if (ch == 'o' || ch == 'O' || ch == '\n' || ch == KEY_ENTER) {
        open_selected(state, ch == 'O');
        if (state->quit_requested && task_ui_can_quit())
            return APP_KEY_QUIT;
    } else if (ch == 'a') {
        create_entry(state);
    } else if (ch == 'w') {
        task_ui_show();
    } else if (ch == 'd' || ch == 'D' || ch == 'p' || ch == 'P' || ch == '-' || ch == '_' ||
               ch == 31) {
        TaskKind kind = ch == 'd'   ? TASK_TRASH
                        : ch == 'D' ? TASK_DELETE
                        : ch == '-' ? TASK_LINK
                        : ch == '_' ? TASK_RELATIVE_LINK
                        : ch == 31  ? TASK_HARDLINK
                                    : TASK_COPY;
        submit_operation(state, kind, ch == 'P');
    } else if (ch == ':' || ch == ';') {
        char command[4096] = {0};
        if (input_prompt("Shell (selected paths in \"$@\")", command, sizeof(command)) &&
            command[0]) {
            if (ch == ':')
                run_shell(state, command);
            else {
                ClipboardPaths paths = {0};
                (void)browser_paths(state, &paths);
                bool ok = tasks_submit_shell(command, state->current_directory,
                                             (const char *const *)paths.paths, paths.count);
                clipboard_paths_free(&paths);
                notify_result(ok, "Shell task queued");
            }
        }
    } else if (ch == 26) {
        def_prog_mode();
        endwin();
        raise(SIGTSTP);
        reset_prog_mode();
        refresh();
        resized = 1;
    } else {
        if (ch == 'y')
            *key = kb->key_copy;
        else if (ch == 'x')
            *key = kb->key_cut;
        else if (ch == 'p')
            *key = kb->key_paste;
        else if (ch == 'd')
            *key = kb->key_delete;
        else if (ch == 'r')
            *key = kb->key_rename;
        else if (ch == '\t')
            *key = kb->key_info;
        else if (ch == 19) {
            search_clear(state);
            app_move_cursor(state, state->dir_window_cas.cursor);
            return APP_KEY_HANDLED;
        } else
            return APP_KEY_PASS;
        return APP_KEY_PASS;
    }
    return APP_KEY_HANDLED;
}
