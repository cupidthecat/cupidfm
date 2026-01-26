// File: main.c
// -----------------------
#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>     // for snprintf
#include <stdlib.h>    // for free, malloc
#include <unistd.h>    // for getenv
#include <ncurses.h>   // for initscr, noecho, cbreak, keypad, curs_set, timeout, endwin, LINES, COLS, getch, timeout, wtimeout, ERR, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_F1, newwin, subwin, box, wrefresh, werase, mvwprintw, wattron, wattroff, A_REVERSE, A_BOLD, getmaxyx, refresh
#include <dirent.h>    // for opendir, readdir, closedir
#include <sys/types.h> // for types like SIZE
#include <sys/stat.h>  // for struct stat
#include <string.h>    // for strlen, strcpy, strdup, strrchr, strtok, strncmp
#include <strings.h>   // for strcasecmp
#include <signal.h>    // for signal, SIGWINCH
#include <stdbool.h>   // for bool, true, false
#include <ctype.h>     // for isspace, toupper
#include <magic.h>     // For libmagic
#include <time.h>      // For strftime, clock_gettime
#include <sys/ioctl.h> // For ioctl
#include <termios.h>   // For resize_term
#include <pthread.h>   // For threading
#include <locale.h>    // For setlocale
#include <errno.h>     // For errno

// Local includes
#include "utils.h"
#include "vector.h"
#include "files.h"
#include "vecstack.h"
#include "main.h"
#include "globals.h"
#include "config.h"
#include "ui.h"
#include "undo.h"
#include "plugins.h"
#include "console.h"
#include "banner.h"
#include "clipboard.h"
#include "tempfiles.h"
#include "browser_ui.h"
#include "app_state.h"
#include "search.h"
#include "app_input.h"
#include "app_paths.h"
#include "app_navigation.h"
#include "app_windows.h"
#include "app_plugins.h"

// Global resize flag
volatile sig_atomic_t resized = 0;
volatile sig_atomic_t is_editing = 0;

// Other global windows
WINDOW *mainwin = NULL;
WINDOW *dirwin = NULL;
WINDOW *previewwin = NULL;

VecStack directoryStack;
int main() {
    // Initialize ncurses
    setlocale(LC_ALL, "");
    
    // Initialize directory stack
    directoryStack = VecStack_empty();
    // Ignore Ctrl+C at the OS level so we can handle it ourselves
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;      // SIG_IGN means "ignore this signal"
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    // Set up signal handler for window resize
    sa.sa_handler = handle_winch;
    sa.sa_flags = SA_RESTART; // Restart interrupted system calls
    sigaction(SIGWINCH, &sa, NULL);

    // Initialize ncurses
    initscr();
    noecho();
    raw();   // or cbreak() if you prefer
    keypad(stdscr, TRUE);
    curs_set(0);
    timeout(100);

    // Initialize windows and other components
    int notif_height = 1;
    int banner_height = 3;

    // Initialize notif window
    notifwin = newwin(notif_height, COLS, LINES - notif_height, 0);
    werase(notifwin);
    box(notifwin, 0, 0);
    wrefresh(notifwin);

    // Initialize banner window
    bannerwin = newwin(banner_height, COLS, 0, 0);
    // Assuming COLOR_PAIR(1) is defined elsewhere; if not, remove or define it
    // wbkgd(bannerwin, COLOR_PAIR(1)); // Set background color
    box(bannerwin, 0, 0);
    wrefresh(bannerwin);

    // Initialize main window
    mainwin = newwin(LINES - banner_height - notif_height, COLS, banner_height, 0);
    wtimeout(mainwin, 100);

    // Initialize subwindows
    SIZE dir_win_width = MAX(COLS / 2, 20);
    SIZE preview_win_width = MAX(COLS - dir_win_width, 20);

    if (dir_win_width + preview_win_width > COLS) {
        dir_win_width = COLS / 2;
        preview_win_width = COLS - dir_win_width;
    }

    dirwin = subwin(mainwin, LINES - banner_height - notif_height, dir_win_width - 1, banner_height, 0);
    box(dirwin, 0, 0);
    wrefresh(dirwin);

    previewwin = subwin(mainwin, LINES - banner_height - notif_height, preview_win_width, banner_height, dir_win_width);
    box(previewwin, 0, 0);
    wrefresh(previewwin);

    // Initialize keybindings and configs
    KeyBindings kb;
    load_default_keybindings(&kb);

    char config_path[1024];
    const char *home = getenv("HOME");
    if (!home) {
        // Fallback if $HOME is not set
        home = ".";
    }
    snprintf(config_path, sizeof(config_path), "%s/.cupidfmrc", home);

    // Initialize an error buffer to capture error messages
    char error_buffer[ERROR_BUFFER_SIZE] = {0};

    // Load the user configuration, capturing any errors
    int config_errors = load_config_file(&kb, config_path, error_buffer, sizeof(error_buffer));

    if (config_errors == 0) {
        // Configuration loaded successfully
        show_notification(notifwin, "Configuration loaded successfully.");
    } else if (config_errors == 1) {
        // Configuration file not found; create a default config file
        if (write_default_config_file(config_path, &kb, error_buffer, sizeof(error_buffer))) {
            // Notify the user about the creation of the default config file
            show_popup("First Run Setup",
                "No config was found.\n"
                "A default config has been created at:\n\n"
                "  %s\n\n"
                "Press any key to continue...",
                config_path);
        } else {
            show_notification(notifwin, "Failed to create default config: %s", error_buffer);
        }
    } else {
        // Configuration file exists but has errors; display the error messages
        show_popup("Configuration Errors",
            "There were issues loading your configuration:\n\n%s\n\n"
            "Press any key to continue with default settings.",
            error_buffer);
        
        // Optionally, you can decide whether to proceed with defaults or halt execution
        // For now, we'll proceed with whatever was loaded and keep defaults for invalid entries
    }

    g_kb = kb;

    // Now that keybindings are loaded from config, initialize the banner
    char banner_text_buffer[256];
    snprintf(banner_text_buffer, sizeof(banner_text_buffer), "Welcome to CupidFM - Press %s to exit", keycode_to_string(kb.key_exit));

    // Assign to global BANNER_TEXT
    BANNER_TEXT = banner_text_buffer;
    // Start the marquee centered on first render.
    banner_offset = banner_initial_offset_for_center(BANNER_TEXT, BUILD_INFO);

    // Initialize application state
    AppState state;
    undo_state_init(&state.undo_state);
    state.current_directory = malloc(MAX_PATH_LENGTH);
    if (state.current_directory == NULL) {
        die(1, "Memory allocation error");
    }

    if (getcwd(state.current_directory, MAX_PATH_LENGTH) == NULL) {
        die(1, "Unable to get current working directory");
    }

    state.selected_entry = "";
    state.select_all_active = false;
    g_select_all_highlight = false;

    state.files = Vector_new(10);
    state.search_active = false;
    state.search_query[0] = '\0';
    state.search_mode = SEARCH_MODE_FUZZY;
	    state.search_files = Vector_new(10);
	    state.plugins = NULL;
    
    // Initialize lazy loading state
    state.lazy_load.directory_path = strdup(state.current_directory);
    state.lazy_load.files_loaded = 0;
    state.lazy_load.total_files = 0;
    state.lazy_load.is_loading = false;
    state.lazy_load.last_load_time = (struct timespec){0};
    
    // Use lazy loading for initial directory load
    reload_directory_lazy(&state.files, state.current_directory, 
                         &state.lazy_load.files_loaded, &state.lazy_load.total_files);
    dir_size_cache_start();

    state.dir_window_cas = (CursorAndSlice){
            .start = 0,
            .cursor = 0,
            .num_lines = LINES - 6,
            .num_files = Vector_len(state.files),
    };

    state.preview_start_line = 0;
    state.preview_override_active = false;
    state.preview_override_path[0] = '\0';

    enum {
        DIRECTORY_WIN_ACTIVE = 1,
        PREVIEW_WIN_ACTIVE = 2,
    } active_window = DIRECTORY_WIN_ACTIVE;

	    // Initial drawing
	    redraw_all_windows(&state);

	    // Load plugins after the UI is drawn so their notifications aren't immediately overwritten.
	    state.plugins = plugins_create();
	    if (state.plugins) {
	        plugins_update_context(&state, active_window);

            // Allow plugins to navigate on startup (e.g., fm.cd(...) in on_load()).
            char cd_req[MAX_PATH_LENGTH];
            if (plugins_take_cd_request(state.plugins, cd_req, sizeof(cd_req))) {
                char target[MAX_PATH_LENGTH];
                if (cd_req[0] == '/') {
                    strncpy(target, cd_req, sizeof(target) - 1);
                    target[sizeof(target) - 1] = '\0';
                } else {
                    path_join(target, state.current_directory, cd_req);
                }

                struct stat st;
                if (stat(target, &st) == 0 && S_ISDIR(st.st_mode)) {
                    navigation_clear_stack(&directoryStack);
                    search_clear(&state);
                    free(state.current_directory);
                    state.current_directory = strdup(target);
                    if (state.current_directory) {
                        if (state.lazy_load.directory_path) free(state.lazy_load.directory_path);
                        state.lazy_load.directory_path = strdup(state.current_directory);
                        state.lazy_load.last_load_time = (struct timespec){0};
                        reload_directory_lazy(&state.files, state.current_directory,
                                              &state.lazy_load.files_loaded, &state.lazy_load.total_files);
                        state.dir_window_cas.num_files = Vector_len(state.files);
                        state.dir_window_cas.cursor = 0;
                        state.dir_window_cas.start = 0;
                        sync_selection_from_active(&state, &state.dir_window_cas);
                        state.preview_start_line = 0;
                        show_notification(notifwin, "cd: %s", state.current_directory);
                        should_clear_notif = false;
                    }
                } else {
                    show_notification(notifwin, "Plugin cd failed: %s", cd_req);
                    should_clear_notif = false;
                }
            }

	        // If plugins requested a reload during on_load, apply once on startup.
	        if (plugins_take_reload_request(state.plugins)) {
	            reload_directory(&state.files, state.current_directory);
	            state.dir_window_cas.num_files = Vector_len(state.files);
	            state.lazy_load.files_loaded = Vector_len(state.files);
	            state.lazy_load.total_files = state.lazy_load.files_loaded;
	        }

            plugins_update_context(&state, active_window);
	    }

    // Set a separate timeout for mainwin to handle scrolling
    wtimeout(mainwin, INPUT_CHECK_INTERVAL);  // Set shorter timeout for input checking

    // Initialize scrolling variables
    // banner_offset is now a global variable defined in globals.c
    struct timespec last_update_time;
    clock_gettime(CLOCK_MONOTONIC, &last_update_time);

    // Calculate the total scroll length for the banner
    int total_scroll_length = (COLS - 2) + (int)strlen(BANNER_TEXT) + (int)strlen(BUILD_INFO) +
                              BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

    int ch;
    while ((ch = getch()) != kb.key_exit) {
        if (state.plugins) {
            plugins_update_context(&state, active_window);
            bool handled = plugins_handle_key(state.plugins, ch);
            if (plugins_take_quit_request(state.plugins)) {
                break;
            }

            // Async plugin UI (prompt/confirm/menu). If a plugin queued one during key handling,
            // run it now (modal), invoke its callback, and then continue handling any requests
            // the callback may have queued (cd/reload/file ops/etc).
            plugins_poll(state.plugins);
            if (plugins_take_quit_request(state.plugins)) {
                break;
            }

            // Plugin-driven navigation/selection (apply even if plugin consumed the key).
            char cd_req[MAX_PATH_LENGTH];
            if (plugins_take_cd_request(state.plugins, cd_req, sizeof(cd_req))) {
                char target[MAX_PATH_LENGTH];
                if (cd_req[0] == '/') {
                    strncpy(target, cd_req, sizeof(target) - 1);
                    target[sizeof(target) - 1] = '\0';
                } else {
                    path_join(target, state.current_directory, cd_req);
                }

                struct stat st;
                if (stat(target, &st) == 0 && S_ISDIR(st.st_mode)) {
                    navigation_clear_stack(&directoryStack);
                    search_clear(&state);
                    free(state.current_directory);
                    state.current_directory = strdup(target);
                    if (!state.current_directory) {
                        show_notification(notifwin, "Memory allocation error");
                        should_clear_notif = false;
                        plugins_update_context(&state, active_window);
                        goto input_done;
                    }

                    if (state.lazy_load.directory_path) free(state.lazy_load.directory_path);
                    state.lazy_load.directory_path = strdup(state.current_directory);
                    state.lazy_load.last_load_time = (struct timespec){0};
                    reload_directory_lazy(&state.files, state.current_directory,
                                          &state.lazy_load.files_loaded, &state.lazy_load.total_files);
                    state.dir_window_cas.num_files = Vector_len(state.files);
                    state.dir_window_cas.cursor = 0;
                    state.dir_window_cas.start = 0;
                    sync_selection_from_active(&state, &state.dir_window_cas);
                    state.preview_start_line = 0;
                    show_notification(notifwin, "cd: %s", state.current_directory);
                    should_clear_notif = false;
                } else {
                    show_notification(notifwin, "cd failed: %s", cd_req);
                    should_clear_notif = false;
                }
                plugins_update_context(&state, active_window);
                goto input_done;
            }

            int sel_idx = -1;
            if (plugins_take_select_index_request(state.plugins, &sel_idx)) {
                Vector *view = active_files(&state);
                if (sel_idx >= 0 && (size_t)sel_idx < Vector_len(*view)) {
                    state.dir_window_cas.cursor = (SIZE)sel_idx;
                    fix_cursor(&state.dir_window_cas);
                    sync_selection_from_active(&state, &state.dir_window_cas);
                    should_clear_notif = false;
                } else {
                    show_notification(notifwin, "select_index out of range");
                    should_clear_notif = false;
                }
                plugins_update_context(&state, active_window);
                goto input_done;
            }

            char sel_req[MAX_PATH_LENGTH];
	            if (plugins_take_select_request(state.plugins, sel_req, sizeof(sel_req))) {
                SIZE idx = (SIZE)-1;
                if (state.search_active) {
                    Vector *view = active_files(&state);
                    idx = find_loaded_index_by_name(view, sel_req);
                } else {
                    idx = find_index_by_name_lazy(&state.files,
                                                  state.current_directory,
                                                  &state.dir_window_cas,
                                                  &state.lazy_load,
                                                  sel_req);
                }

                if (idx != (SIZE)-1) {
                    state.dir_window_cas.cursor = idx;
                    fix_cursor(&state.dir_window_cas);
                    sync_selection_from_active(&state, &state.dir_window_cas);
                    should_clear_notif = false;
                } else {
                    show_notification(notifwin, "Not found: %s", sel_req);
                    should_clear_notif = false;
                }
	                plugins_update_context(&state, active_window);
	                goto input_done;
	            }

	            if (plugins_take_clear_search_request(state.plugins)) {
	                state.select_all_active = false;
	                g_select_all_highlight = false;
	                search_clear(&state);
	                state.dir_window_cas.cursor = 0;
	                state.dir_window_cas.start = 0;
	                sync_selection_from_active(&state, &state.dir_window_cas);
	                state.preview_start_line = 0;
	                show_notification(notifwin, "Search cleared.");
	                should_clear_notif = false;
	                plugins_update_context(&state, active_window);
	                goto input_done;
	            }

	            char search_req[MAX_PATH_LENGTH];
	            if (plugins_take_set_search_request(state.plugins, search_req, sizeof(search_req))) {
	                state.select_all_active = false;
	                g_select_all_highlight = false;
	                strncpy(state.search_query, search_req, sizeof(state.search_query) - 1);
	                state.search_query[sizeof(state.search_query) - 1] = '\0';
	                if (state.search_query[0] == '\0') {
	                    search_clear(&state);
	                    show_notification(notifwin, "Search cleared.");
	                } else {
	                    state.search_active = true;
	                    search_rebuild(&state, state.search_query);
	                    show_notification(notifwin, "Search: %s", state.search_query);
	                }
	                should_clear_notif = false;
	                state.dir_window_cas.cursor = 0;
	                state.dir_window_cas.start = 0;
	                sync_selection_from_active(&state, &state.dir_window_cas);
	                state.preview_start_line = 0;
	                plugins_update_context(&state, active_window);
	                goto input_done;
	            }

                if (plugins_take_parent_dir_request(state.plugins)) {
                    navigate_left(&state.current_directory, &state.files, &state.dir_window_cas, &state, &directoryStack);
	                state.preview_start_line = 0;
	                plugins_update_context(&state, active_window);
	                goto input_done;
	            }

                if (plugins_take_enter_dir_request(state.plugins)) {
                    navigate_right(&state, &state.current_directory, active_files(&state), &state.dir_window_cas, &directoryStack);
	                state.preview_start_line = 0;
	                plugins_update_context(&state, active_window);
	                goto input_done;
	            }

                char open_req[MAX_PATH_LENGTH];
                if (plugins_take_open_path_request(state.plugins, open_req, sizeof(open_req))) {
                    char target[MAX_PATH_LENGTH];
                    resolve_path_under_cwd(target, state.current_directory, open_req);
                    if (!target[0]) {
                        show_notification(notifwin, "Open failed: empty path");
                        should_clear_notif = false;
                    } else {
                        struct stat st;
                        if (stat(target, &st) == 0) {
                            if (S_ISDIR(st.st_mode)) {
                                navigation_clear_stack(&directoryStack);
                                search_clear(&state);
                                free(state.current_directory);
                                state.current_directory = strdup(target);
                                if (state.current_directory) {
                                    if (state.lazy_load.directory_path) free(state.lazy_load.directory_path);
                                    state.lazy_load.directory_path = strdup(state.current_directory);
                                    state.lazy_load.last_load_time = (struct timespec){0};
                                    reload_directory_lazy(&state.files, state.current_directory,
                                                          &state.lazy_load.files_loaded, &state.lazy_load.total_files);
                                    state.dir_window_cas.num_files = Vector_len(state.files);
                                    state.dir_window_cas.cursor = 0;
                                    state.dir_window_cas.start = 0;
                                    sync_selection_from_active(&state, &state.dir_window_cas);
                                    state.preview_start_line = 0;
                                    show_notification(notifwin, "Opened dir: %s", state.current_directory);
                                    should_clear_notif = false;
                                }
                            } else {
                                edit_file_in_terminal(previewwin, target, notifwin, &kb);
                                state.preview_start_line = 0;
                                redraw_frame_after_edit(&state, dirwin, previewwin, mainwin, notifwin);
                                show_notification(notifwin, "Opened file: %s", target);
                                should_clear_notif = false;
                            }
                        } else {
                            show_notification(notifwin, "Open failed: %s", strerror(errno));
                            should_clear_notif = false;
                        }
                    }
                    plugins_update_context(&state, active_window);
                    goto input_done;
                }

                char preview_req[MAX_PATH_LENGTH];
                if (plugins_take_preview_path_request(state.plugins, preview_req, sizeof(preview_req))) {
                    char target[MAX_PATH_LENGTH];
                    resolve_path_under_cwd(target, state.current_directory, preview_req);
                    if (!target[0]) {
                        show_notification(notifwin, "Preview failed: empty path");
                        should_clear_notif = false;
                    } else {
                        struct stat st;
                        if (stat(target, &st) == 0) {
                            state.preview_override_active = true;
                            strncpy(state.preview_override_path, target, sizeof(state.preview_override_path) - 1);
                            state.preview_override_path[sizeof(state.preview_override_path) - 1] = '\0';
                            state.preview_start_line = 0;
                            show_notification(notifwin, "Previewing: %s", target);
                            should_clear_notif = false;
                        } else {
                            show_notification(notifwin, "Preview failed: %s", strerror(errno));
                            should_clear_notif = false;
                        }
                    }
                    plugins_update_context(&state, active_window);
                    goto input_done;
                }

	            if (plugins_take_open_selected_request(state.plugins)) {
	                Vector *view = active_files(&state);
	                if (view && Vector_len(*view) > 0 && state.dir_window_cas.cursor >= 0 &&
	                    (size_t)state.dir_window_cas.cursor < Vector_len(*view)) {
	                    FileAttr current_file = (FileAttr)view->el[state.dir_window_cas.cursor];
                        if (FileAttr_is_dir(current_file)) {
                            navigate_right(&state, &state.current_directory, view, &state.dir_window_cas, &directoryStack);
	                        state.preview_start_line = 0;
	                    } else {
	                        char file_path[MAX_PATH_LENGTH];
	                        path_join(file_path, state.current_directory, state.selected_entry);
	                        edit_file_in_terminal(previewwin, file_path, notifwin, &kb);
	                        state.preview_start_line = 0;
	                        redraw_frame_after_edit(&state, dirwin, previewwin, mainwin, notifwin);
	                        show_notification(notifwin, "Editing file: %s", state.selected_entry);
	                        should_clear_notif = false;
	                    }
	                } else {
	                    show_notification(notifwin, "No selection");
	                    should_clear_notif = false;
	                }
	                plugins_update_context(&state, active_window);
	                goto input_done;
	            }

	            PluginFileOp op;
	            memset(&op, 0, sizeof(op));
	            if (plugins_take_fileop_request(state.plugins, &op)) {
                bool ok = false;
                char err[256] = {0};

                if (op.kind == PLUGIN_FILEOP_UNDO || op.kind == PLUGIN_FILEOP_REDO) {
                    char ubuf[256] = {0};
                    bool did = (op.kind == PLUGIN_FILEOP_UNDO)
                                   ? undo_state_do_undo(&state.undo_state, ubuf, sizeof(ubuf))
                                   : undo_state_do_redo(&state.undo_state, ubuf, sizeof(ubuf));
                    if (did) {
                        char saved_query[MAX_PATH_LENGTH];
                        search_before_reload(&state, saved_query);
                        reload_directory(&state.files, state.current_directory);
                        state.dir_window_cas.num_files = Vector_len(state.files);
                        state.lazy_load.files_loaded = Vector_len(state.files);
                        state.lazy_load.total_files = state.lazy_load.files_loaded;
                        state.dir_window_cas.cursor = 0;
                        state.dir_window_cas.start = 0;
                        search_after_reload(&state, &state.dir_window_cas, saved_query);
                        sync_selection_from_active(&state, &state.dir_window_cas);
                        show_notification(notifwin, "%s", (op.kind == PLUGIN_FILEOP_UNDO) ? "Undone last operation" : "Redone last operation");
                        should_clear_notif = false;
                        ok = true;
                    } else {
                        show_notification(notifwin, "%s", ubuf[0] ? ubuf : "Undo/redo failed");
                        should_clear_notif = false;
                    }
                } else if (op.kind == PLUGIN_FILEOP_MKDIR) {
                    char full[MAX_PATH_LENGTH];
                    resolve_path_under_cwd(full, state.current_directory, op.arg1);
                    if (full[0]) {
                        (void)ensure_parent_dir_local(full);
                        if (mkdir(full, 0755) == 0) {
                            (void)undo_state_set_single(&state.undo_state, UNDO_OP_CREATE_DIR, NULL, full);
                            show_notification(notifwin, "Created dir: %s", full);
                            should_clear_notif = false;
                            ok = true;
                        } else if (errno == EEXIST) {
                            show_notification(notifwin, "Dir exists: %s", full);
                            should_clear_notif = false;
                            ok = true;
                        } else {
                            snprintf(err, sizeof(err), "mkdir failed: %s", strerror(errno));
                        }
                    } else {
                        snprintf(err, sizeof(err), "mkdir: invalid path");
                    }
                } else if (op.kind == PLUGIN_FILEOP_TOUCH) {
                    char full[MAX_PATH_LENGTH];
                    resolve_path_under_cwd(full, state.current_directory, op.arg1);
                    if (full[0]) {
                        if (access(full, F_OK) == 0) {
                            show_notification(notifwin, "File exists: %s", full);
                            should_clear_notif = false;
                            ok = true;
                        } else {
                            (void)ensure_parent_dir_local(full);
                            FILE *f = fopen(full, "wx");
                            if (f) {
                                fclose(f);
                                (void)undo_state_set_single(&state.undo_state, UNDO_OP_CREATE_FILE, NULL, full);
                                show_notification(notifwin, "Created file: %s", full);
                                should_clear_notif = false;
                                ok = true;
                            } else {
                                snprintf(err, sizeof(err), "touch failed: %s", strerror(errno));
                            }
                        }
                    } else {
                        snprintf(err, sizeof(err), "touch: invalid path");
                    }
                } else if (op.kind == PLUGIN_FILEOP_RENAME) {
                    if (op.count == 1 && op.paths && op.paths[0] && op.arg1[0]) {
                        char src[MAX_PATH_LENGTH];
                        resolve_path_under_cwd(src, state.current_directory, op.paths[0]);
                        char dst[MAX_PATH_LENGTH] = {0};
                        if (op.arg1[0] == '/') {
                            strncpy(dst, op.arg1, sizeof(dst) - 1);
                            dst[sizeof(dst) - 1] = '\0';
                        } else {
                            const char *slash = strrchr(src, '/');
                            if (!slash || slash == src) {
                                path_join(dst, state.current_directory, op.arg1);
                            } else {
                                char dirbuf[MAX_PATH_LENGTH];
                                size_t prefix = (size_t)(slash - src);
                                if (prefix >= sizeof(dirbuf)) prefix = sizeof(dirbuf) - 1;
                                memcpy(dirbuf, src, prefix);
                                dirbuf[prefix] = '\0';
                                path_join(dst, dirbuf, op.arg1);
                            }
                        }
                        if (!dst[0]) snprintf(err, sizeof(err), "rename: invalid destination");
                        else if (access(dst, F_OK) == 0) snprintf(err, sizeof(err), "rename: destination exists");
                        else {
                            (void)ensure_parent_dir_local(dst);
                            if (rename(src, dst) == 0) {
                                (void)undo_state_set_single(&state.undo_state, UNDO_OP_RENAME, src, dst);
                                show_notification(notifwin, "Renamed: %s", op.arg1);
                                should_clear_notif = false;
                                ok = true;
                            } else {
                                snprintf(err, sizeof(err), "rename failed: %s", strerror(errno));
                            }
                        }
                    } else {
                        snprintf(err, sizeof(err), "rename: invalid args");
                    }
                } else if (op.kind == PLUGIN_FILEOP_DELETE) {
                    if (op.count > 0 && op.paths) {
                        UndoItem *items = (UndoItem *)calloc(op.count, sizeof(UndoItem));
                        size_t did = 0;
                        for (size_t i = 0; i < op.count; i++) {
                            char src[MAX_PATH_LENGTH];
                            resolve_path_under_cwd(src, state.current_directory, op.paths[i]);
                            char trashed[MAX_PATH_LENGTH] = {0};
                            if (delete_item(src, trashed, sizeof(trashed))) {
                                if (items) {
                                    items[did].src = strdup(src);
                                    items[did].dst = strdup(trashed);
                                }
                                did++;
                            }
                        }
                        if (items && did > 0) {
                            if (!undo_state_set_owned(&state.undo_state, UNDO_OP_DELETE_TO_TRASH, items, did)) {
                                for (size_t i = 0; i < did; i++) {
                                    free(items[i].src);
                                    free(items[i].dst);
                                }
                                free(items);
                            }
                        } else if (items) {
                            free(items);
                        }
                        show_notification(notifwin, "Deleted %zu item(s)", did);
                        should_clear_notif = false;
                        ok = true;
                    } else {
                        snprintf(err, sizeof(err), "delete: invalid args");
                    }
                } else if (op.kind == PLUGIN_FILEOP_COPY || op.kind == PLUGIN_FILEOP_MOVE) {
                    if (op.count > 0 && op.paths && op.arg1[0]) {
                        char dst_dir[MAX_PATH_LENGTH];
                        resolve_path_under_cwd(dst_dir, state.current_directory, op.arg1);
                        struct stat dstst;
                        if (stat(dst_dir, &dstst) != 0 || !S_ISDIR(dstst.st_mode)) {
                            snprintf(err, sizeof(err), "dst_dir is not a directory");
                        } else {
                            UndoItem *items = (UndoItem *)calloc(op.count, sizeof(UndoItem));
                            size_t did = 0;
                            for (size_t i = 0; i < op.count; i++) {
                                char src[MAX_PATH_LENGTH];
                                resolve_path_under_cwd(src, state.current_directory, op.paths[i]);
                                struct stat st;
                                if (lstat(src, &st) != 0) continue;

                                const char *base = basename_ptr(src);
                                if (!base || !*base) continue;

                                char dst[MAX_PATH_LENGTH];
                                path_join(dst, dst_dir, base);

                                if (access(dst, F_OK) == 0) continue;

                                char cmd[4096];
                                if (op.kind == PLUGIN_FILEOP_COPY) {
                                    snprintf(cmd, sizeof(cmd), "cp %s \"%s\" \"%s\"",
                                             (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) ? "-r" : "",
                                             src, dst);
                                    if (system(cmd) != -1) {
                                        if (items) {
                                            items[did].src = strdup(src);
                                            items[did].dst = strdup(dst);
                                        }
                                        did++;
                                    }
                                } else {
                                    // MOVE
                                    if (rename(src, dst) != 0) {
                                        snprintf(cmd, sizeof(cmd), "mv \"%s\" \"%s\"", src, dst);
                                        if (system(cmd) == -1) continue;
                                    }
                                    if (items) {
                                        items[did].src = strdup(src);
                                        items[did].dst = strdup(dst);
                                    }
                                    did++;
                                }
                            }

                            if (items && did > 0) {
                                UndoOpKind k = (op.kind == PLUGIN_FILEOP_COPY) ? UNDO_OP_COPY : UNDO_OP_MOVE;
                                if (!undo_state_set_owned(&state.undo_state, k, items, did)) {
                                    for (size_t i = 0; i < did; i++) {
                                        free(items[i].src);
                                        free(items[i].dst);
                                    }
                                    free(items);
                                }
                            } else if (items) {
                                free(items);
                            }

                            show_notification(notifwin, "%s %zu item(s)", (op.kind == PLUGIN_FILEOP_COPY) ? "Copied" : "Moved", did);
                            should_clear_notif = false;
                            ok = true;
                        }
                    } else {
                        snprintf(err, sizeof(err), "copy/move: invalid args");
                    }
                }

                plugins_fileop_free(&op);

                if (!ok && err[0]) {
                    show_notification(notifwin, "%s", err);
                    should_clear_notif = false;
                }

                if (ok) {
                    char saved_query[MAX_PATH_LENGTH];
                    search_before_reload(&state, saved_query);
                    reload_directory(&state.files, state.current_directory);
                    state.dir_window_cas.num_files = Vector_len(state.files);
                    state.lazy_load.files_loaded = Vector_len(state.files);
                    state.lazy_load.total_files = state.lazy_load.files_loaded;
                    state.dir_window_cas.cursor = 0;
                    state.dir_window_cas.start = 0;
                    search_after_reload(&state, &state.dir_window_cas, saved_query);
                    sync_selection_from_active(&state, &state.dir_window_cas);
                }

                plugins_update_context(&state, active_window);
                goto input_done;
            }

            int search_mode_req = SEARCH_MODE_FUZZY;
            if (plugins_take_set_search_mode_request(state.plugins, &search_mode_req)) {
                state.search_mode = search_mode_req;
                if (state.search_active && state.search_query[0]) {
                    search_rebuild(&state, state.search_query);
                    state.dir_window_cas.cursor = 0;
                    state.dir_window_cas.start = 0;
                    sync_selection_from_active(&state, &state.dir_window_cas);
                    state.preview_start_line = 0;
                }
                show_notification(notifwin, "Search mode: %s", (search_mode_req == SEARCH_MODE_EXACT) ? "exact" : (search_mode_req == SEARCH_MODE_REGEX) ? "regex" : "fuzzy");
                should_clear_notif = false;
                plugins_update_context(&state, active_window);
                goto input_done;
            }

            if (plugins_take_reload_request(state.plugins)) {
                char saved_query[MAX_PATH_LENGTH];
                search_before_reload(&state, saved_query);
                reload_directory(&state.files, state.current_directory);
                state.dir_window_cas.num_files = Vector_len(state.files);
                state.lazy_load.files_loaded = Vector_len(state.files);
                state.lazy_load.total_files = state.lazy_load.files_loaded;
                state.dir_window_cas.cursor = 0;
                state.dir_window_cas.start = 0;
                search_after_reload(&state, &state.dir_window_cas, saved_query);
                sync_selection_from_active(&state, &state.dir_window_cas);
                should_clear_notif = false;
                plugins_update_context(&state, active_window);
                goto input_done;
            }
            if (handled) {
                plugins_update_context(&state, active_window);
                goto input_done;
            }
        }
        struct timespec loop_start_time;
        clock_gettime(CLOCK_MONOTONIC, &loop_start_time);
        if (resized) {
            resized = 0;
            redraw_all_windows(&state);
            maybe_flush_input(loop_start_time);
            continue;
        }
        // Check if enough time has passed to update the banner
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        long time_diff = (current_time.tv_sec - last_update_time.tv_sec) * 1000000 +
                         (current_time.tv_nsec - last_update_time.tv_nsec) / 1000;

        if (time_diff >= BANNER_SCROLL_INTERVAL) {
            // Update banner with current offset
            pthread_mutex_lock(&banner_mutex);
            draw_scrolling_banner(bannerwin, BANNER_TEXT, BUILD_INFO, banner_offset);
            pthread_mutex_unlock(&banner_mutex);
            banner_offset = (banner_offset + 1) % total_scroll_length;
            last_update_time = current_time;
        }

        clock_gettime(CLOCK_MONOTONIC, &current_time);
        long notification_diff = (current_time.tv_sec - last_notification_time.tv_sec) * 1000 +
                               (current_time.tv_nsec - last_notification_time.tv_nsec) / 1000000;

        if (notification_hold_active) {
            bool expired = (current_time.tv_sec > notification_hold_until.tv_sec) ||
                           (current_time.tv_sec == notification_hold_until.tv_sec &&
                            current_time.tv_nsec >= notification_hold_until.tv_nsec);
            if (expired) {
                notification_hold_active = false;
            }
        }

        if (!notification_hold_active && !should_clear_notif && notification_diff >= NOTIFICATION_TIMEOUT_MS) {
            werase(notifwin);
            wrefresh(notifwin);
            should_clear_notif = true;
        }

        if (ch != ERR) {
            dir_size_note_user_activity();

            // 1) UP
            if (ch == kb.key_up) {
                if (active_window == DIRECTORY_WIN_ACTIVE) {
                    navigate_up(&state.dir_window_cas,
                                active_files(&state),
                                &state.selected_entry,
                                state.current_directory,
                                state.search_active ? NULL : &state.lazy_load);
                    state.preview_start_line = 0;
                    werase(notifwin);
                    show_notification(notifwin, "Moved up");
                    wrefresh(notifwin);
                    should_clear_notif = false;
                } else if (active_window == PREVIEW_WIN_ACTIVE) {
                    if (state.preview_start_line > 0) {
                        state.preview_start_line--;
                        werase(notifwin);
                        show_notification(notifwin, "Scrolled up");
                        wrefresh(notifwin);
                        should_clear_notif = false;
                    }
                }
            }

            // 2) DOWN
            else if (ch == kb.key_down) {
                if (active_window == DIRECTORY_WIN_ACTIVE) {
                    navigate_down(&state.dir_window_cas,
                                  active_files(&state),
                                  &state.selected_entry,
                                  state.current_directory,
                                  state.search_active ? NULL : &state.lazy_load);
                    state.preview_start_line = 0;
                    werase(notifwin);
                    show_notification(notifwin, "Moved down");
                    wrefresh(notifwin);
                    should_clear_notif = false;
                } else if (active_window == PREVIEW_WIN_ACTIVE) {
                    // Determine total lines for scrolling in the preview
                    char file_path[MAX_PATH_LENGTH];
                    if (state.preview_override_active) {
                        strncpy(file_path, state.preview_override_path, sizeof(file_path) - 1);
                        file_path[sizeof(file_path) - 1] = '\0';
                    } else {
                        path_join(file_path, state.current_directory, state.selected_entry);
                    }
                    
                    // Check if it's a directory or a file
                    struct stat file_stat;
                    int total_lines = 0;
                    if (stat(file_path, &file_stat) == 0 && S_ISDIR(file_stat.st_mode)) {
                        // It's a directory, count directory tree lines
                        total_lines = get_directory_tree_total_lines(file_path);
                    } else {
                        // It's a file, count file lines
                        total_lines = get_total_lines(file_path);
                    }

                    int max_x, max_y;
                    getmaxyx(previewwin, max_y, max_x);
                    (void) max_x;
                    int content_height = max_y - 7;
                    int max_start_line = total_lines - content_height;
                    if (max_start_line < 0) max_start_line = 0;

                    if (state.preview_start_line < max_start_line) {
                        state.preview_start_line++;
                        werase(notifwin);
                        show_notification(notifwin, "Scrolled down");
                        wrefresh(notifwin);
                        should_clear_notif = false;
                    }
                }
            }

            // 3) LEFT
            else if (ch == kb.key_left) {
                if (active_window == DIRECTORY_WIN_ACTIVE) {
                    navigate_left(&state.current_directory,
                                &state.files,
                                &state.dir_window_cas,
                                &state,
                                &directoryStack);
                    state.preview_start_line = 0;
                    werase(notifwin);
                    show_notification(notifwin, "Navigated to parent directory");
                    wrefresh(notifwin);
                    should_clear_notif = false;
                }
            }

            // 4) RIGHT
            else if (ch == kb.key_right) {
                if (active_window == DIRECTORY_WIN_ACTIVE) {
                    navigate_right(&state,
                                   &state.current_directory,
                                   active_files(&state),
                                   &state.dir_window_cas,
                                   &directoryStack);
                    state.preview_start_line = 0;
                }
            }

            // 5) TAB (switch active window)
            else if (ch == kb.key_tab) {
                active_window = (active_window == DIRECTORY_WIN_ACTIVE)
                                ? PREVIEW_WIN_ACTIVE
                                : DIRECTORY_WIN_ACTIVE;
                if (active_window == DIRECTORY_WIN_ACTIVE) {
                    state.preview_start_line = 0;
                }
                werase(notifwin);
                show_notification(
                    notifwin,
                    "Switched to %s window",
                    (active_window == DIRECTORY_WIN_ACTIVE) ? "Directory" : "Preview"
                );
                wrefresh(notifwin);
                should_clear_notif = false;
            }

            // 6) EDIT 
	            else if (ch == kb.key_edit) {
	                if (active_window == PREVIEW_WIN_ACTIVE) {
	                    char file_path[MAX_PATH_LENGTH];
	                    path_join(file_path, state.current_directory, state.selected_entry);
	                    edit_file_in_terminal(previewwin, file_path, notifwin, &kb);
	                    state.preview_start_line = 0;
	
	                    redraw_frame_after_edit(&state, dirwin, previewwin, mainwin, notifwin);
	                    
	                    werase(notifwin);
	                    show_notification(notifwin, "Editing file: %s", state.selected_entry);
	                    wrefresh(notifwin);
	                    should_clear_notif = false;
	                }
	            }

            // 7) COPY
            else if (ch == kb.key_copy) {
                if (active_window == DIRECTORY_WIN_ACTIVE && state.selected_entry) {
                    if (state.select_all_active) {
                        Vector *files = active_files(&state);
                        size_t total = Vector_len(*files);
                        if (total == 0) {
                            show_notification(notifwin, "Nothing to copy");
                            should_clear_notif = false;
                            goto input_done;
                        }

                        char tmp_path[256];
                        snprintf(tmp_path, sizeof(tmp_path), "/tmp/cupidfm_clip_%d", getpid());
                        FILE *fp = fopen(tmp_path, "w");
                        if (!fp) {
                            show_notification(notifwin, "Unable to write clipboard temp file");
                            should_clear_notif = false;
                            goto input_done;
                        }

                        fprintf(fp, "CUPIDFM_CLIP_V2\n");
                        fprintf(fp, "OP=COPY\n");
                        fprintf(fp, "N=%zu\n", total);
                        for (size_t i = 0; i < total; i++) {
                            FileAttr fa = (FileAttr)files->el[i];
                            const char *name = FileAttr_get_name(fa);
                            if (!name || !*name) continue;
                            char full_path[MAX_PATH_LENGTH];
                            path_join(full_path, state.current_directory, name);
                            fprintf(fp, "%d\t%s\t%s\n", FileAttr_is_dir(fa) ? 1 : 0, full_path, name);
                        }
                        fclose(fp);

                        if (!clipboard_set_from_file(tmp_path)) {
                            unlink(tmp_path);
                            show_notification(notifwin, "Unable to copy to clipboard");
                            should_clear_notif = false;
                            goto input_done;
                        }
                        unlink(tmp_path);

                        // Enable paste even though legacy paste uses copied_filename.
                        strncpy(copied_filename, "MULTI", MAX_PATH_LENGTH);
                        show_notification(notifwin, "Copied %zu items", total);
                        should_clear_notif = false;
                        state.select_all_active = false;
                        g_select_all_highlight = false;
                        goto input_done;
                    }
                    char full_path[MAX_PATH_LENGTH];
                    path_join(full_path, state.current_directory, state.selected_entry);
                    copy_to_clipboard(full_path);
                    strncpy(copied_filename, state.selected_entry, MAX_PATH_LENGTH);
                    werase(notifwin);
                    show_notification(notifwin, "Copied to clipboard: %s", state.selected_entry);
                    wrefresh(notifwin);
                    should_clear_notif = false;
                }
            }

            // 8) PASTE
            else if (ch == kb.key_paste) {
                if (active_window == DIRECTORY_WIN_ACTIVE) {
                    PasteLog plog;
                    int pasted = paste_from_clipboard(state.current_directory, copied_filename, &plog);
                    if (pasted <= 0) {
                        paste_log_free(&plog);
                        werase(notifwin);
                        show_notification(notifwin, "Nothing to paste");
                        wrefresh(notifwin);
                        should_clear_notif = false;
                        goto input_done;
                    }

                    // Record undo info for paste (best effort).
                    if (plog.count > 0) {
                        UndoItem *items = (UndoItem *)calloc(plog.count, sizeof(UndoItem));
                        if (items) {
                            for (size_t i = 0; i < plog.count; i++) {
                                items[i].src = plog.src && plog.src[i] ? strdup(plog.src[i]) : NULL;
                                items[i].dst = plog.dst && plog.dst[i] ? strdup(plog.dst[i]) : NULL;
                            }
                            UndoOpKind kind = (plog.kind == PASTE_KIND_CUT) ? UNDO_OP_MOVE : UNDO_OP_COPY;
                            if (!undo_state_set_owned(&state.undo_state, kind, items, plog.count)) {
                                for (size_t i = 0; i < plog.count; i++) {
                                    free(items[i].src);
                                    free(items[i].dst);
                                }
                                free(items);
                            }
                        }
                    }
                    paste_log_free(&plog);

                    char saved_query[MAX_PATH_LENGTH];
                    search_before_reload(&state, saved_query);
                    reload_directory(&state.files, state.current_directory);
                    state.dir_window_cas.num_files = Vector_len(state.files);
                    state.lazy_load.files_loaded = Vector_len(state.files);
                    state.lazy_load.total_files = state.lazy_load.files_loaded;
                    search_after_reload(&state, &state.dir_window_cas, saved_query);
                    werase(notifwin);
                    if (pasted == 1) {
                        show_notification(notifwin, "Pasted: %s", copied_filename);
                    } else {
                        show_notification(notifwin, "Pasted %d items", pasted);
                    }
                    wrefresh(notifwin);
                    should_clear_notif = false;
                }
            }

            // 9) CUT 
            else if (ch == kb.key_cut) {
                if (active_window == DIRECTORY_WIN_ACTIVE && state.selected_entry) {
                    if (state.select_all_active) {
                        Vector *files = active_files(&state);
                        size_t total = Vector_len(*files);
                        if (total == 0) {
                            show_notification(notifwin, "Nothing to cut");
                            should_clear_notif = false;
                            goto input_done;
                        }

                        char storage_dir[MAX_PATH_LENGTH];
                        if (!ensure_cut_storage_dir(storage_dir, sizeof(storage_dir))) {
                            show_notification(notifwin, "Unable to create cut storage");
                            should_clear_notif = false;
                            goto input_done;
                        }

                        char tmp_path[256];
                        snprintf(tmp_path, sizeof(tmp_path), "/tmp/cupidfm_clip_%d", getpid());
                        FILE *fp = fopen(tmp_path, "w");
                        if (!fp) {
                            show_notification(notifwin, "Unable to write clipboard temp file");
                            should_clear_notif = false;
                            goto input_done;
                        }

                        fprintf(fp, "CUPIDFM_CLIP_V2\n");
                        fprintf(fp, "OP=CUT\n");
                        fprintf(fp, "N=%zu\n", total);

                        int moved = 0;
                        UndoItem *undo_items = (UndoItem *)calloc(total > 0 ? total : 1, sizeof(UndoItem));
                        for (size_t i = 0; i < total; i++) {
                            FileAttr fa = (FileAttr)files->el[i];
                            const char *name = FileAttr_get_name(fa);
                            if (!name || !*name) continue;

                            char src_path[MAX_PATH_LENGTH];
                            path_join(src_path, state.current_directory, name);

                            // Choose a unique destination inside the cut storage dir.
                            char dst_path[MAX_PATH_LENGTH];
                            int n = snprintf(dst_path, sizeof(dst_path), "%s/%s", storage_dir, name);
                            if (n < 0 || (size_t)n >= sizeof(dst_path)) {
                                continue;
                            }
                            for (int attempt = 1; access(dst_path, F_OK) == 0 && attempt < 1000; attempt++) {
                                n = snprintf(dst_path, sizeof(dst_path), "%s/%s_%d", storage_dir, name, attempt);
                                if (n < 0 || (size_t)n >= sizeof(dst_path)) {
                                    dst_path[0] = '\0';
                                    break;
                                }
                            }
                            if (!dst_path[0]) {
                                continue;
                            }

                            char mv_cmd[2048];
                            n = snprintf(mv_cmd, sizeof(mv_cmd), "mv \"%s\" \"%s\"", src_path, dst_path);
                            if (n < 0 || (size_t)n >= sizeof(mv_cmd)) {
                                continue;
                            }
                            if (system(mv_cmd) == -1) {
                                continue;
                            }

                            fprintf(fp, "%d\t%s\t%s\n", FileAttr_is_dir(fa) ? 1 : 0, dst_path, name);
                            if (undo_items) {
                                undo_items[moved].src = strdup(src_path);
                                undo_items[moved].dst = strdup(dst_path);
                            }
                            moved++;
                        }
                        fclose(fp);

                        if (moved == 0 || !clipboard_set_from_file(tmp_path)) {
                            unlink(tmp_path);
                            show_notification(notifwin, "Unable to cut to clipboard");
                            should_clear_notif = false;
                            if (undo_items) {
                                for (int i = 0; i < moved; i++) {
                                    free(undo_items[i].src);
                                    free(undo_items[i].dst);
                                }
                                free(undo_items);
                            }
                            goto input_done;
                        }
                        unlink(tmp_path);

                        strncpy(copied_filename, "MULTI", MAX_PATH_LENGTH);

                        if (undo_items && moved > 0) {
                            if (!undo_state_set_owned(&state.undo_state, UNDO_OP_MOVE, undo_items, (size_t)moved)) {
                                for (int i = 0; i < moved; i++) {
                                    free(undo_items[i].src);
                                    free(undo_items[i].dst);
                                }
                                free(undo_items);
                            }
                        } else if (undo_items) {
                            free(undo_items);
                        }

                        // Reload directory to reflect the cut items
                        char saved_query[MAX_PATH_LENGTH];
                        search_before_reload(&state, saved_query);
                        reload_directory(&state.files, state.current_directory);
                        state.dir_window_cas.num_files = Vector_len(state.files);
                        state.lazy_load.files_loaded = Vector_len(state.files);
                        state.lazy_load.total_files = state.lazy_load.files_loaded;
                        search_after_reload(&state, &state.dir_window_cas, saved_query);

                        show_notification(notifwin, "Cut %d items", moved);
                        should_clear_notif = false;
                        state.select_all_active = false;
                        g_select_all_highlight = false;
                        goto input_done;
                    }
                    char full_path[MAX_PATH_LENGTH];
                    path_join(full_path, state.current_directory, state.selected_entry);
                    char storage_path[MAX_PATH_LENGTH] = {0};
                    cut_and_paste(full_path, storage_path, sizeof(storage_path));
                    strncpy(copied_filename, state.selected_entry, MAX_PATH_LENGTH);

                    if (storage_path[0] && access(storage_path, F_OK) == 0) {
                        (void)undo_state_set_single(&state.undo_state, UNDO_OP_MOVE, full_path, storage_path);
                    }

                    // Reload directory to reflect the cut file
                    char saved_query[MAX_PATH_LENGTH];
                    search_before_reload(&state, saved_query);
                    reload_directory(&state.files, state.current_directory);
                    state.dir_window_cas.num_files = Vector_len(state.files);
                    state.lazy_load.files_loaded = Vector_len(state.files);
                    state.lazy_load.total_files = state.lazy_load.files_loaded;
                    search_after_reload(&state, &state.dir_window_cas, saved_query);

                    werase(notifwin);
                    show_notification(notifwin, "Cut to clipboard: %s", state.selected_entry);
                    wrefresh(notifwin);
                    should_clear_notif = false;
                }
            }

            //  Select All in Current View (Ctrl+A by default)
            else if (ch == kb.key_select_all) {
                // Toggle select-all mode for bulk operations
                state.select_all_active = !state.select_all_active;
                // Sync UI flag used by renderer
                g_select_all_highlight = state.select_all_active;
                if (state.select_all_active) {
                    show_notification(notifwin, "Selected all items in view");
                } else {
                    show_notification(notifwin, "Selection cleared");
                }
                wrefresh(notifwin);
                should_clear_notif = false;
            }

            // Undo/Redo (Ctrl+Z / Ctrl+Y by default)
            else if (ch == kb.key_undo || ch == kb.key_redo) {
                char err[256] = {0};
                bool ok = false;
                if (ch == kb.key_undo) {
                    ok = undo_state_do_undo(&state.undo_state, err, sizeof(err));
                } else {
                    ok = undo_state_do_redo(&state.undo_state, err, sizeof(err));
                }

                // Refresh directory listing after any filesystem change attempt.
                if (ok) {
                    char saved_query[MAX_PATH_LENGTH];
                    search_before_reload(&state, saved_query);
                    reload_directory(&state.files, state.current_directory);
                    state.dir_window_cas.num_files = Vector_len(state.files);
                    state.lazy_load.files_loaded = Vector_len(state.files);
                    state.lazy_load.total_files = state.lazy_load.files_loaded;
                    state.dir_window_cas.cursor = 0;
                    state.dir_window_cas.start = 0;
                    search_after_reload(&state, &state.dir_window_cas, saved_query);
                    sync_selection_from_active(&state, &state.dir_window_cas);
                    show_notification(notifwin, "%s", (ch == kb.key_undo) ? "Undone last operation" : "Redone last operation");
                } else {
                    show_notification(notifwin, "%s", err[0] ? err : "Undo/redo failed");
                }
                should_clear_notif = false;
                goto input_done;
            }

            // 10) DELETE
            else if (ch == kb.key_delete) {
                if (active_window == DIRECTORY_WIN_ACTIVE && state.selected_entry) {
                    if (state.select_all_active) {
                        // Bulk delete: delete all items currently visible in the active view
                        Vector *files = active_files(&state);
                        size_t total = Vector_len(*files);
                        bool should_delete = false;
                        char label[64];
                        snprintf(label, sizeof(label), "%zu selected items", total);
                        (void)confirm_delete(label, &should_delete);
                        if (!should_delete) {
                            show_notification(notifwin, "Delete cancelled");
                            should_clear_notif = false;
                            state.select_all_active = false;
                            g_select_all_highlight = false;
                            goto input_done;
                        }
                        int deleted_count = 0;
                        UndoItem *undo_items = (UndoItem *)calloc(total > 0 ? total : 1, sizeof(UndoItem));
                        for (size_t i = 0; i < total; i++) {
                            FileAttr fa = (FileAttr)files->el[i];
                            const char *name = FileAttr_get_name(fa);
                            if (!name || !*name) continue;
                            char full_path[MAX_PATH_LENGTH];
                            path_join(full_path, state.current_directory, name);
                            char trashed[MAX_PATH_LENGTH] = {0};
                            if (delete_item(full_path, trashed, sizeof(trashed))) {
                                if (undo_items) {
                                    undo_items[deleted_count].src = strdup(full_path);
                                    undo_items[deleted_count].dst = strdup(trashed);
                                }
                                deleted_count++;
                            }
                        }
                        if (undo_items && deleted_count > 0) {
                            if (!undo_state_set_owned(&state.undo_state, UNDO_OP_DELETE_TO_TRASH, undo_items, (size_t)deleted_count)) {
                                for (int i = 0; i < deleted_count; i++) {
                                    free(undo_items[i].src);
                                    free(undo_items[i].dst);
                                }
                                free(undo_items);
                            }
                        } else if (undo_items) {
                            free(undo_items);
                        }
                        // Reload directory after bulk delete
                        char saved_query[MAX_PATH_LENGTH];
                        search_before_reload(&state, saved_query);
                        reload_directory(&state.files, state.current_directory);
                        state.dir_window_cas.num_files = Vector_len(state.files);
                        state.lazy_load.files_loaded = Vector_len(state.files);
                        state.lazy_load.total_files = state.lazy_load.files_loaded;
                        search_after_reload(&state, &state.dir_window_cas, saved_query);
                        show_notification(notifwin, "Deleted %d items", deleted_count);
                        should_clear_notif = false;
                        state.select_all_active = false;
                        g_select_all_highlight = false;
                    } else {
                        char full_path[MAX_PATH_LENGTH];
                        path_join(full_path, state.current_directory, state.selected_entry);

                        bool should_delete = false;
	                        bool delete_result = confirm_delete(state.selected_entry, &should_delete);

	                        if (delete_result && should_delete) {
	                            char trashed[MAX_PATH_LENGTH] = {0};
	                            bool deleted_ok = delete_item(full_path, trashed, sizeof(trashed));
	                            if (deleted_ok) {
	                                (void)undo_state_set_single(&state.undo_state, UNDO_OP_DELETE_TO_TRASH, full_path, trashed);
	                            }
	                            char saved_query[MAX_PATH_LENGTH];
	                            search_before_reload(&state, saved_query);
	                            reload_directory(&state.files, state.current_directory);
                            state.dir_window_cas.num_files = Vector_len(state.files);
                            state.lazy_load.files_loaded = Vector_len(state.files);
	                            state.lazy_load.total_files = state.lazy_load.files_loaded;
	                            search_after_reload(&state, &state.dir_window_cas, saved_query);

	                            if (deleted_ok) {
	                                show_notification(notifwin, "Deleted: %s", state.selected_entry);
	                            } else {
	                                show_notification(notifwin, "Delete failed");
	                            }
	                            should_clear_notif = false;
	                        } else {
	                            show_notification(notifwin, "Delete cancelled");
	                            should_clear_notif = false;
	                        }
                    }
                }
            }


            // 11) RENAME
            else if (ch == kb.key_rename) {
                if (active_window == DIRECTORY_WIN_ACTIVE && state.selected_entry) {
                    if (state.select_all_active) {
                        show_notification(notifwin, "Rename does not support Select All");
                        should_clear_notif = false;
                        state.select_all_active = false;
                        g_select_all_highlight = false;
                        goto input_done;
                    }
                    char full_path[MAX_PATH_LENGTH];
                    path_join(full_path, state.current_directory, state.selected_entry);

                    char new_path[MAX_PATH_LENGTH * 2] = {0};
                    if (rename_item(notifwin, full_path, new_path, sizeof(new_path))) {
                        (void)undo_state_set_single(&state.undo_state, UNDO_OP_RENAME, full_path, new_path);
                    }

                    // Reload to show changes
                    char saved_query[MAX_PATH_LENGTH];
                    search_before_reload(&state, saved_query);
                    reload_directory(&state.files, state.current_directory);
                    state.dir_window_cas.num_files = Vector_len(state.files);
                    state.lazy_load.files_loaded = Vector_len(state.files);
                    state.lazy_load.total_files = state.lazy_load.files_loaded;
                    state.dir_window_cas.cursor = 0;
                    state.dir_window_cas.start = 0;
                    search_after_reload(&state, &state.dir_window_cas, saved_query);

                    if (!state.search_active) {
                        if (state.dir_window_cas.num_files > 0) {
                            state.selected_entry = FileAttr_get_name(state.files.el[0]);
                        } else {
                            state.selected_entry = "";
                        }
                    }
                }
            }

            else if (ch == kb.key_search) {
                if (active_window == DIRECTORY_WIN_ACTIVE) {
                    prompt_fuzzy_search(&state,
                                        &state.dir_window_cas,
                                        notifwin,
                                        dirwin,
                                        previewwin);
                } else {
                    show_notification(notifwin, "Switch to directory window to search");
                    should_clear_notif = false;
                }
            }

            // 12) CREATE NEW 
            else if (ch == kb.key_new) {
                if (active_window == DIRECTORY_WIN_ACTIVE) {
                    char created_path[MAX_PATH_LENGTH * 2] = {0};
                    if (create_new_file(notifwin, state.current_directory, created_path, sizeof(created_path))) {
                        (void)undo_state_set_single(&state.undo_state, UNDO_OP_CREATE_FILE, NULL, created_path);
                    }
                    char saved_query[MAX_PATH_LENGTH];
                    search_before_reload(&state, saved_query);
                    reload_directory(&state.files, state.current_directory);
                    state.dir_window_cas.num_files = Vector_len(state.files);
                    state.lazy_load.files_loaded = Vector_len(state.files);
                    state.lazy_load.total_files = state.lazy_load.files_loaded;
                    state.dir_window_cas.cursor = 0;
                    state.dir_window_cas.start = 0;
                    search_after_reload(&state, &state.dir_window_cas, saved_query);

                    if (!state.search_active) {
                        if (state.dir_window_cas.num_files > 0) {
                            state.selected_entry = FileAttr_get_name(state.files.el[0]);
                        } else {
                            state.selected_entry = "";
                        }
                    }
                }
            }

            // 13 CREATE NEW DIR
            else if (ch == kb.key_new_dir) {
                char created_path[MAX_PATH_LENGTH * 2] = {0};
                bool created = create_new_directory(notifwin, state.current_directory, created_path, sizeof(created_path));
                if (!created) {
                    // User canceled or creation failed; don't disturb the current view.
                    continue;
                }
                (void)undo_state_set_single(&state.undo_state, UNDO_OP_CREATE_DIR, NULL, created_path);
                char saved_query[MAX_PATH_LENGTH];
                search_before_reload(&state, saved_query);
                reload_directory(&state.files, state.current_directory);
                state.dir_window_cas.num_files = Vector_len(state.files);
                state.lazy_load.files_loaded = Vector_len(state.files);
                state.lazy_load.total_files = state.lazy_load.files_loaded;
                state.dir_window_cas.cursor = 0;
                state.dir_window_cas.start = 0;
                search_after_reload(&state, &state.dir_window_cas, saved_query);

                if (!state.search_active) {
                    if (state.dir_window_cas.num_files > 0) {
                        state.selected_entry = FileAttr_get_name(state.files.el[0]);
                    } else {
                        state.selected_entry = "";
                    }
                }
            }

            // Quick File Info (Ctrl+T by default)
            else if (ch == kb.key_info) {
                if (active_window == DIRECTORY_WIN_ACTIVE && state.selected_entry) {
                    if (state.select_all_active) {
                        show_notification(notifwin, "Quick info does not support Select All");
                        should_clear_notif = false;
                        goto input_done;
                    }

                    char file_path[MAX_PATH_LENGTH];
                    path_join(file_path, state.current_directory, state.selected_entry);

                    struct stat st;
                    if (lstat(file_path, &st) != 0) {
                        show_popup("File Info", "Unable to stat:\n\n%s\n\n%s", file_path, strerror(errno));
                        goto input_done;
                    }

                    char type_buf[64] = "Other";
                    if (S_ISDIR(st.st_mode)) strcpy(type_buf, "Directory");
                    else if (S_ISREG(st.st_mode)) strcpy(type_buf, "File");
                    else if (S_ISLNK(st.st_mode)) strcpy(type_buf, "Symlink");

                    char size_buf[64] = "-";
                    if (S_ISDIR(st.st_mode)) {
                        long dir_size = get_directory_size_peek(file_path);
                        if (dir_size == DIR_SIZE_PENDING) {
                            long p = dir_size_get_progress(file_path);
                            if (p > 0) {
                                char tmp[32];
                                format_file_size(tmp, (size_t)p);
                                snprintf(size_buf, sizeof(size_buf), "Calculating... %s", tmp);
                            } else {
                                snprintf(size_buf, sizeof(size_buf), "Calculating...");
                            }
                        }
                        else if (dir_size == DIR_SIZE_VIRTUAL_FS) snprintf(size_buf, sizeof(size_buf), "Virtual FS");
                        else if (dir_size == DIR_SIZE_TOO_LARGE) snprintf(size_buf, sizeof(size_buf), "Too large");
                        else if (dir_size == DIR_SIZE_PERMISSION_DENIED) snprintf(size_buf, sizeof(size_buf), "Permission denied");
                        else if (dir_size < 0) snprintf(size_buf, sizeof(size_buf), "Error");
                        else {
                            char tmp[32];
                            format_file_size(tmp, (size_t)dir_size);
                            snprintf(size_buf, sizeof(size_buf), "%s", tmp);
                        }
                    } else {
                        char tmp[32];
                        format_file_size(tmp, (size_t)st.st_size);
                        snprintf(size_buf, sizeof(size_buf), "%s", tmp);
                    }

                    char perm_buf[16];
                    snprintf(perm_buf, sizeof(perm_buf), "%04o", (unsigned)(st.st_mode & 07777));

                    char mtime_buf[64];
                    struct tm tm_local;
                    localtime_r(&st.st_mtime, &tm_local);
                    strftime(mtime_buf, sizeof(mtime_buf), "%Y-%m-%d %H:%M:%S", &tm_local);

                    // MIME type (best effort)
                    char mime_buf[128] = "unknown";
                    magic_t mc = magic_open(MAGIC_MIME_TYPE | MAGIC_SYMLINK | MAGIC_CHECK);
                    if (mc && magic_load(mc, NULL) == 0) {
                        const char *m = magic_file(mc, file_path);
                        if (m && *m) {
                            strncpy(mime_buf, m, sizeof(mime_buf) - 1);
                            mime_buf[sizeof(mime_buf) - 1] = '\0';
                        }
                    }

                    char link_target[MAX_PATH_LENGTH] = {0};
                    if (S_ISLNK(st.st_mode)) {
                        ssize_t n = readlink(file_path, link_target, sizeof(link_target) - 1);
                        if (n > 0) link_target[n] = '\0';
                    }

                    if (mc) magic_close(mc);

                    if (S_ISLNK(st.st_mode) && link_target[0]) {
                        show_popup("File Info",
                                   "Name: %s\n"
                                   "Type: %s\n"
                                   "Path: %s\n"
                                   "Link: -> %s\n"
                                   "Size: %s\n"
                                   "Perm: %s\n"
                                   "MIME: %s\n"
                                   "MTime: %s",
                                   state.selected_entry,
                                   type_buf,
                                   file_path,
                                   link_target,
                                   size_buf,
                                   perm_buf,
                                   mime_buf,
                                   mtime_buf);
                    } else {
                        show_popup("File Info",
                                   "Name: %s\n"
                                   "Type: %s\n"
                                   "Path: %s\n"
                                   "Size: %s\n"
                                   "Perm: %s\n"
                                   "MIME: %s\n"
                                   "MTime: %s",
                                   state.selected_entry,
                                   type_buf,
                                   file_path,
                                   size_buf,
                                   perm_buf,
                                   mime_buf,
                                   mtime_buf);
                    }
                }
            }

            // File Permissions (Ctrl+P by default)
            else if (ch == kb.key_permissions) {
                if (active_window == DIRECTORY_WIN_ACTIVE && state.selected_entry) {
                    if (state.select_all_active) {
                        show_notification(notifwin, "Permissions does not support Select All");
                        should_clear_notif = false;
                        goto input_done;
                    }
                    char full_path[MAX_PATH_LENGTH];
                    path_join(full_path, state.current_directory, state.selected_entry);
                    (void)change_permissions(notifwin, full_path);
                    goto input_done;
                }
            }

            // Console (Ctrl+O by default)
            else if (ch == kb.key_console) {
                console_show();
                should_clear_notif = false;
                goto input_done;
            }
        }

input_done:
        // Clear notification window only if no new notification was displayed
        if (should_clear_notif) {
            werase(notifwin);
            wrefresh(notifwin);
        }

        // Keep plugin context up-to-date after CupidFM handled input, and fire change hooks.
        if (state.plugins) {
            plugins_update_context(&state, active_window);
        }

        // Redraw windows
        draw_directory_window(
                dirwin,
                state.current_directory,
                active_files(&state),
                &state.dir_window_cas
        );

        if (state.preview_override_active) {
            draw_preview_window_path(
                previewwin,
                state.preview_override_path,
                NULL,
                state.preview_start_line
            );
        } else {
            draw_preview_window(
                previewwin,
                state.current_directory,
                state.selected_entry,
                state.preview_start_line
            );
        }

        // Removed: duplicate active-window cursor highlighting. Use render-only highlights in draw_directory_window.

        wrefresh(mainwin);
        wrefresh(notifwin);
        // Highlight the active window
        if (active_window == DIRECTORY_WIN_ACTIVE) {
            wattron(dirwin, A_REVERSE);
            Vector *view = active_files(&state);
            if (view && Vector_len(*view) > 0 && state.dir_window_cas.cursor >= 0 &&
                (size_t)state.dir_window_cas.cursor < Vector_len(*view)) {
                mvwprintw(dirwin,
                          state.dir_window_cas.cursor - state.dir_window_cas.start + 1,
                          1,
                          "%s",
                          FileAttr_get_name((FileAttr)view->el[state.dir_window_cas.cursor]));
            }
            wattroff(dirwin, A_REVERSE);
        } else {
            wattron(previewwin, A_REVERSE);
            mvwprintw(previewwin, 1, 1, "Preview Window Active");
            wattroff(previewwin, A_REVERSE);
        }

        wrefresh(mainwin);
        wrefresh(notifwin);

        maybe_flush_input(loop_start_time);
    }

    // Clean up
    // Free all FileAttr objects before destroying the vector
    for (size_t i = 0; i < Vector_len(state.files); i++) {
        free_attr((FileAttr)state.files.el[i]);
    }
    Vector_set_len_no_free(&state.files, 0);
    Vector_bye(&state.files);
    // `search_files` is a shallow view into `files`, so only free its backing array.
    if (state.search_files.el) {
        free(state.search_files.el);
        state.search_files.el = NULL;
    }
    if (state.plugins) {
        plugins_destroy(state.plugins);
        state.plugins = NULL;
    }
    undo_state_clear(&state.undo_state);
    free(state.current_directory);
    if (state.lazy_load.directory_path) {
        free(state.lazy_load.directory_path);
    }
    delwin(dirwin);
    delwin(previewwin);
    delwin(notifwin);
    delwin(mainwin);
    delwin(bannerwin);
    endwin();
    cleanup_temp_files();
    dir_size_cache_stop();

    // Destroy directory stack
    VecStack_bye(&directoryStack);

    return 0;
}
