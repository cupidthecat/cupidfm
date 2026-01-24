// File: main.c
// -----------------------
#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200112L
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

// Global resize flag
volatile sig_atomic_t resized = 0;
volatile sig_atomic_t is_editing = 0;

// Other global windows
WINDOW *mainwin = NULL;
WINDOW *dirwin = NULL;
WINDOW *previewwin = NULL;

VecStack directoryStack;

// Input handling tuning
#define INPUT_FLUSH_THRESHOLD_NS 150000000L // 150ms
#define DIRECTORY_TREE_MAX_DEPTH 4
#define DIRECTORY_TREE_MAX_TOTAL 1500

static bool tree_limit_hit = false;

// Typedefs and Structures
typedef struct {
    SIZE start;
    SIZE cursor;
    SIZE num_lines;
    SIZE num_files;
} CursorAndSlice;

// Lazy loading state for large directories
typedef struct {
    char *directory_path;
    size_t files_loaded;
    size_t total_files;  // -1 if unknown
    bool is_loading;
    struct timespec last_load_time;  // Prevent loading too frequently
} LazyLoadState;

typedef struct {
    char *current_directory;
    Vector files;
    CursorAndSlice dir_window_cas;
    const char *selected_entry;
    bool select_all_active; // When true, all visible items are selected for bulk operations
    int preview_start_line;
    LazyLoadState lazy_load;  // Lazy loading state
    // Fuzzy search "view" of the current directory list. This is a shallow list of
    // FileAttr pointers owned by `files` (do not free elements in `search_files`).
    bool search_active;
    char search_query[MAX_PATH_LENGTH];
    Vector search_files;
    UndoState undo_state;
    PluginManager *plugins;
} AppState;

// Global highlight flag for Select All in current view
bool g_select_all_highlight = false;
//! Search helpers
typedef struct {
    int score;      // lower is better
    FileAttr entry; // pointer owned by AppState.files
} FuzzyHit;

static SIZE find_index_by_name_lazy(Vector *files,
                                   const char *dir,
                                   CursorAndSlice *cas,
                                   LazyLoadState *lazy_load,
                                   const char *name);
static void draw_directory_window(WINDOW *window,
                                  const char *directory,
                                  Vector *files_vector,
                                  CursorAndSlice *cas);
static void draw_preview_window(WINDOW *window,
                                const char *current_directory,
                                const char *selected_entry,
                                int start_line);
static void sync_selection_from_active(AppState *state, CursorAndSlice *cas);

// Forward declaration of fix_cursor
void fix_cursor(CursorAndSlice *cas);

static void maybe_flush_input(struct timespec loop_start_time) {
    struct timespec loop_end_time;
    clock_gettime(CLOCK_MONOTONIC, &loop_end_time);
    long loop_duration_ns = (loop_end_time.tv_sec - loop_start_time.tv_sec) * 1000000000L +
                            (loop_end_time.tv_nsec - loop_start_time.tv_nsec);
    if (loop_duration_ns > INPUT_FLUSH_THRESHOLD_NS) {
        flushinp();
    }
}

static bool clipboard_set_from_file(const char *path) {
    if (!path || !*path) return false;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "xclip -selection clipboard -i < \"%s\"", path);
    return system(cmd) != -1;
}

static bool ensure_cut_storage_dir(char *out_dir, size_t out_len) {
    if (!out_dir || out_len == 0) return false;
    snprintf(out_dir, out_len, "/tmp/cupidfm_cut_storage_%d", getpid());
    out_dir[out_len - 1] = '\0';
    if (mkdir(out_dir, 0700) == 0) return true;
    return (errno == EEXIST);
}

static SIZE find_loaded_index_by_name(Vector *files, const char *name) {
    if (!files || !name || !*name) return (SIZE)-1;
    SIZE count = (SIZE)Vector_len(*files);
    for (SIZE i = 0; i < count; i++) {
        FileAttr fa = (FileAttr)files->el[i];
        const char *nm = FileAttr_get_name(fa);
        if (nm && strcmp(nm, name) == 0) {
            return i;
        }
    }
    return (SIZE)-1;
}

static SIZE find_index_by_name_lazy(Vector *files,
                                   const char *dir,
                                   CursorAndSlice *cas,
                                   LazyLoadState *lazy_load,
                                   const char *name)
{
    if (!files || !dir || !cas || !lazy_load || !name || !*name) return (SIZE)-1;

    for (int safety = 0; safety < 512; safety++) {
        SIZE idx = find_loaded_index_by_name(files, name);
        if (idx != (SIZE)-1) return idx;

        size_t before = Vector_len(*files);
        cas->num_files = before;
        if (before == 0) return (SIZE)-1;

        cas->cursor = (SIZE)(before - 1);
        load_more_files_if_needed(files,
                                  dir,
                                  cas,
                                  &lazy_load->files_loaded,
                                  lazy_load->total_files);

        size_t after = Vector_len(*files);
        cas->num_files = after;
        if (after == before) break;
    }

    return (SIZE)-1;
}

static int cupidfuzzy_score(const char *pattern, const char *text) {
    if (!pattern || !*pattern || !text) return -1;
    const char *scan = text;
    const char *segment_start = text;
    int score = 0;

    for (const char *p = pattern; *p; p++) {
        char target = tolower((unsigned char)*p);
        bool matched = false;
        while (*scan) {
            if (tolower((unsigned char)*scan) == target) {
                score += (int)(scan - segment_start);
                segment_start = scan + 1;
                scan = segment_start;
                matched = true;
                break;
            }
            scan++;
        }
        if (!matched) return -1;
    }

    return score;
}

static int fuzzyhit_cmp(const void *a, const void *b) {
    const FuzzyHit *aa = (const FuzzyHit *)a;
    const FuzzyHit *bb = (const FuzzyHit *)b;
    if (aa->score != bb->score) return (aa->score < bb->score) ? -1 : 1;

    const char *an = FileAttr_get_name(aa->entry);
    const char *bn = FileAttr_get_name(bb->entry);
    if (!an && !bn) return 0;
    if (!an) return 1;
    if (!bn) return -1;
    return strcasecmp(an, bn);
}

static Vector *active_files(AppState *state) {
    return (state && state->search_active) ? &state->search_files : &state->files;
}

static void search_clear(AppState *state) {
    if (!state) return;
    state->select_all_active = false;
    g_select_all_highlight = false;
    state->search_active = false;
    state->search_query[0] = '\0';
    if (state->search_files.el) {
        Vector_set_len_no_free(&state->search_files, 0);
    }
}

static size_t search_rebuild(AppState *state, const char *query) {
    if (!state || !state->search_files.el) return 0;
    Vector_set_len_no_free(&state->search_files, 0);

    if (!query || !*query) return 0;

    size_t total = Vector_len(state->files);
    if (total == 0) return 0;

    FuzzyHit *hits = malloc(total * sizeof(*hits));
    if (!hits) return 0;

    size_t count = 0;
    for (size_t i = 0; i < total; i++) {
        FileAttr fa = (FileAttr)state->files.el[i];
        const char *name = FileAttr_get_name(fa);
        if (!name) continue;

        int score = cupidfuzzy_score(query, name);
        if (score < 0) continue;

        hits[count].score = score;
        hits[count].entry = fa;
        count++;
    }

    if (count == 0) {
        free(hits);
        return 0;
    }

    qsort(hits, count, sizeof(*hits), fuzzyhit_cmp);

    Vector_add(&state->search_files, count);
    for (size_t i = 0; i < count; i++) {
        state->search_files.el[i] = hits[i].entry;
    }
    Vector_set_len_no_free(&state->search_files, count);

    free(hits);
    return count;
}

static void search_before_reload(AppState *state, char saved_query[MAX_PATH_LENGTH]) {
    if (!state) return;
    // Clear any select-all UI state when starting a reload
    state->select_all_active = false;
    g_select_all_highlight = false;
    if (saved_query) {
        saved_query[0] = '\0';
        if (state->search_query[0]) {
            strncpy(saved_query, state->search_query, MAX_PATH_LENGTH - 1);
            saved_query[MAX_PATH_LENGTH - 1] = '\0';
        }
    }
    search_clear(state);
}

static void search_after_reload(AppState *state, CursorAndSlice *cas, const char *saved_query) {
    if (!state || !cas) return;
    if (saved_query && *saved_query) {
        strncpy(state->search_query, saved_query, sizeof(state->search_query) - 1);
        state->search_query[sizeof(state->search_query) - 1] = '\0';
        state->search_active = true;
        search_rebuild(state, state->search_query);
        cas->cursor = 0;
        cas->start = 0;
    }
    sync_selection_from_active(state, cas);
}

static void maybe_load_more_for_search(AppState *state, CursorAndSlice *cas) {
    if (!state || !cas) return;
    if (!state->search_active) return;
    if (state->lazy_load.total_files == 0) return;
    if (state->lazy_load.files_loaded >= state->lazy_load.total_files) return;

    // Throttle loading while typing so we don't continuously hammer the filesystem.
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long dt_ns = (now.tv_sec - state->lazy_load.last_load_time.tv_sec) * 1000000000L +
                 (now.tv_nsec - state->lazy_load.last_load_time.tv_nsec);
    if (state->lazy_load.last_load_time.tv_sec != 0 && dt_ns < 150000000L) {
        return;
    }
    state->lazy_load.last_load_time = now;

    // Use a throwaway CursorAndSlice to convince lazy loading we're near the end.
    CursorAndSlice tmp = *cas;
    tmp.start = (SIZE)state->lazy_load.files_loaded;
    tmp.cursor = tmp.start;
    tmp.num_files = (SIZE)Vector_len(state->files);

    load_more_files_if_needed(&state->files,
                              state->current_directory,
                              &tmp,
                              &state->lazy_load.files_loaded,
                              state->lazy_load.total_files);
}

static void sync_selection_from_active(AppState *state, CursorAndSlice *cas) {
    if (!state || !cas) return;
    Vector *files = active_files(state);
    cas->num_files = (SIZE)Vector_len(*files);
    if (cas->num_files <= 0) {
        cas->cursor = 0;
        cas->start = 0;
        state->selected_entry = "";
        return;
    }
    if (cas->cursor >= (SIZE)cas->num_files) cas->cursor = (SIZE)cas->num_files - 1;
    fix_cursor(cas);
    state->selected_entry = FileAttr_get_name((FileAttr)files->el[cas->cursor]);
}

static bool prompt_fuzzy_search(AppState *state,
                                CursorAndSlice *cas,
                                WINDOW *win,
                                WINDOW *dir_window,
                                WINDOW *preview_window)
{
    if (!state || !cas || !win) return false;

    char saved_selection[MAX_PATH_LENGTH] = {0};
    if (state->selected_entry && *state->selected_entry) {
        strncpy(saved_selection, state->selected_entry, sizeof(saved_selection) - 1);
        saved_selection[sizeof(saved_selection) - 1] = '\0';
    }

    // Start with previous query (if any).
    char query[MAX_PATH_LENGTH] = {0};
    strncpy(query, state->search_query, sizeof(query) - 1);
    query[sizeof(query) - 1] = '\0';
    size_t index = strlen(query);

    bool aborted = false;

    // Make input non-blocking to allow banner updates, like rename/new prompts.
    keypad(win, TRUE); // enable KEY_UP/KEY_DOWN on the notif/prompt window
    wtimeout(win, 10);
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = (COLS - 2) +
                              (BANNER_TEXT ? (int)strlen(BANNER_TEXT) : 0) +
                              (BUILD_INFO ? (int)strlen(BUILD_INFO) : 0) +
                              BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

    // Initialize (or clear) the active view from the current query.
    state->search_query[0] = '\0';
    if (query[0]) {
        strncpy(state->search_query, query, sizeof(state->search_query) - 1);
        state->search_query[sizeof(state->search_query) - 1] = '\0';
        state->search_active = true;
        search_rebuild(state, state->search_query);
    } else {
        search_clear(state);
    }
    cas->cursor = 0;
    cas->start = 0;
    sync_selection_from_active(state, cas);

    while (true) {
        // Keep the single-line prompt similar to rename/new file UX.
        size_t shown = Vector_len(*active_files(state));
        werase(win);
        mvwprintw(win, 0, 0, "Search (Esc to cancel): %s [%zu]", query, shown);
        wrefresh(win);

        int ch = wgetch(win);
        if (ch == ERR) {
            struct timespec current_time;
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            long banner_time_diff = (current_time.tv_sec - last_banner_update.tv_sec) * 1000000 +
                                    (current_time.tv_nsec - last_banner_update.tv_nsec) / 1000;
            if (banner_time_diff >= BANNER_SCROLL_INTERVAL && BANNER_TEXT && bannerwin) {
                pthread_mutex_lock(&banner_mutex);
                draw_scrolling_banner(bannerwin, BANNER_TEXT, BUILD_INFO, banner_offset);
                pthread_mutex_unlock(&banner_mutex);
                banner_offset = (banner_offset + 1) % total_scroll_length;
                last_banner_update = current_time;
            }

            maybe_load_more_for_search(state, cas);
            if (state->search_active && state->search_query[0]) {
                search_rebuild(state, state->search_query);
                sync_selection_from_active(state, cas);
                draw_directory_window(dir_window, state->current_directory, active_files(state), cas);
                draw_preview_window(preview_window, state->current_directory, state->selected_entry, state->preview_start_line);
                wrefresh(dir_window);
                wrefresh(preview_window);
            }

            napms(10);
            continue;
        }

        if (ch == 27) { // Esc
            aborted = true;
            break;
        }
        if (ch == '\n') {
            break;
        }

        // Allow browsing matches while the prompt is open.
        if (ch == KEY_UP || ch == KEY_PPAGE) {
            if (state->search_active && Vector_len(state->search_files) > 0) {
                int step = (ch == KEY_PPAGE) ? MAX(1, cas->num_lines - 2) : 1;
                SIZE len = (SIZE)Vector_len(state->search_files);
                if (len <= 0) break;

                SIZE next = cas->cursor - step;
                while (next < 0) next += len;
                cas->cursor = next;
                fix_cursor(cas);
                sync_selection_from_active(state, cas);
                state->preview_start_line = 0;
            }
        } else if (ch == KEY_DOWN || ch == KEY_NPAGE) {
            if (state->search_active && Vector_len(state->search_files) > 0) {
                int step = (ch == KEY_NPAGE) ? MAX(1, cas->num_lines - 2) : 1;
                SIZE len = (SIZE)Vector_len(state->search_files);
                if (len <= 0) break;

                cas->cursor = (cas->cursor + step) % len;
                fix_cursor(cas);
                sync_selection_from_active(state, cas);
                state->preview_start_line = 0;
            }
        } else {
            bool query_changed = false;
            if (ch == KEY_BACKSPACE || ch == 127) {
                if (index > 0) {
                    index--;
                    query[index] = '\0';
                    query_changed = true;
                }
            } else if (isprint(ch) && index < MAX_PATH_LENGTH - 1) {
                query[index++] = (char)ch;
                query[index] = '\0';
                query_changed = true;
            }

            if (query_changed) {
                // Selecting all is scoped to the current visible list; changing the query
                // changes the list, so clear selection mode to avoid surprises.
                state->select_all_active = false;
                g_select_all_highlight = false;

                strncpy(state->search_query, query, sizeof(state->search_query) - 1);
                state->search_query[sizeof(state->search_query) - 1] = '\0';
                if (query[0] == '\0') {
                    search_clear(state);
                } else {
                    state->search_active = true;
                    search_rebuild(state, state->search_query);
                }
                cas->cursor = 0;
                cas->start = 0;
                sync_selection_from_active(state, cas);
                state->preview_start_line = 0;
            }
        }

        draw_directory_window(dir_window, state->current_directory, active_files(state), cas);
        draw_preview_window(preview_window, state->current_directory, state->selected_entry, state->preview_start_line);
        wrefresh(dir_window);
        wrefresh(preview_window);
    }

    wtimeout(win, -1);
    werase(win);
    wrefresh(win);

    if (aborted) {
        // Restore full list and restore selection if possible.
        search_clear(state);
        cas->num_files = (SIZE)Vector_len(state->files);
        cas->cursor = 0;
        cas->start = 0;
        if (saved_selection[0]) {
            SIZE idx = find_index_by_name_lazy(&state->files,
                                               state->current_directory,
                                               cas,
                                               &state->lazy_load,
                                               saved_selection);
            if (idx != (SIZE)-1) {
                cas->cursor = idx;
            }
        }
        sync_selection_from_active(state, cas);
        draw_directory_window(dir_window, state->current_directory, &state->files, cas);
        draw_preview_window(preview_window, state->current_directory, state->selected_entry, state->preview_start_line);
        wrefresh(dir_window);
        wrefresh(preview_window);

        show_notification(win, "Search canceled.");
        should_clear_notif = false;
        return false;
    }

    // If query is empty, treat it as "clear search".
    if (query[0] == '\0') {
        search_clear(state);
        sync_selection_from_active(state, cas);
        show_notification(win, "Search cleared.");
        should_clear_notif = false;
        return true;
    }

    // Keep filtered list active after Enter.
    if (!state->search_active || Vector_len(state->search_files) == 0) {
        show_notification(win, "No matches for \"%s\"", query);
        should_clear_notif = false;
        return false;
    }

    show_notification(win, "Search: %s", query);
    should_clear_notif = false;
    return true;
}

// Function Implementations
static const char* keycode_to_string(int keycode) {
    static char buf[32];

    // Define the base value for function keys
    // Typically, KEY_F(1) is 265, so base = 264
    const int FUNCTION_KEY_BASE = KEY_F(1) - 1;

    // Handle function keys
    if (keycode >= KEY_F(1) && keycode <= KEY_F(63)) {
        int func_num = keycode - FUNCTION_KEY_BASE;
        snprintf(buf, sizeof(buf), "F%d", func_num);
        return buf;
    }

    // Handle control characters (Ctrl+A to Ctrl+Z)
    if (keycode >= 1 && keycode <= 26) { // Ctrl+A (1) to Ctrl+Z (26)
        char c = 'A' + (keycode - 1);
        snprintf(buf, sizeof(buf), "^%c", c);
        return buf;
    }

    // Handle special keys
    switch (keycode) {
        case KEY_UP: return "KEY_UP";
        case KEY_DOWN: return "KEY_DOWN";
        case KEY_LEFT: return "KEY_LEFT";
        case KEY_RIGHT: return "KEY_RIGHT";
        case '\t': return "Tab";
        case KEY_BACKSPACE: return "Backspace";
        // Add more special keys as needed
        default:
            // Handle printable characters
            if (keycode >= 32 && keycode <= 126) { // Printable ASCII
                snprintf(buf, sizeof(buf), "%c", keycode);
                return buf;
            }
            return "UNKNOWN";
    }
}

/** Function to count total lines in a directory tree recursively
 *
 * @param dir_path the path of the directory to count
 * @param level the current level of the directory tree
 * @param line_count pointer to the current line count
 */
void count_directory_tree_lines(const char *dir_path, int level, int *line_count) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    struct stat statbuf;
    char full_path[MAX_PATH_LENGTH];
    size_t dir_path_len = strlen(dir_path);

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        size_t name_len = strlen(entry->d_name);
        if (dir_path_len + name_len + 2 > MAX_PATH_LENGTH) continue;

        strcpy(full_path, dir_path);
        if (full_path[dir_path_len - 1] != '/') {
            strcat(full_path, "/");
        }
        strcat(full_path, entry->d_name);

        if (lstat(full_path, &statbuf) == -1) continue;

        (*line_count)++; // Count this entry
        if (*line_count >= DIRECTORY_TREE_MAX_TOTAL) {
            break;
        }

        if (S_ISDIR(statbuf.st_mode) &&
            level < DIRECTORY_TREE_MAX_DEPTH) {
            count_directory_tree_lines(full_path, level + 1, line_count);
            if (*line_count >= DIRECTORY_TREE_MAX_TOTAL) {
                break;
            }
        }
    }

    closedir(dir);
}

/** Function to get the total number of lines in a directory tree
 *
 * @param dir_path the path to the directory
 * @return the total number of lines in the directory tree
 */
int get_directory_tree_total_lines(const char *dir_path) {
    int line_count = 0;
    count_directory_tree_lines(dir_path, 0, &line_count);
    return line_count;
}

/** Function to show directory tree recursively
 *
 * @param window the window to display the directory tree
 * @param dir_path the path of the directory to display
 * @param level the current level of the directory tree
 * @param line_num the current line number in the window
 * @param max_y the maximum number of lines in the window
 * @param max_x the maximum number of columns in the window
 * @param start_line the starting line offset for scrolling
 * @param current_count pointer to track the current line count (for scrolling)
 */
void show_directory_tree(WINDOW *window, const char *dir_path, int level, int *line_num, int max_y, int max_x, int start_line, int *current_count) {
    if (level == 0) {
        tree_limit_hit = false;
    }
    if (level == 0) {
        // Always display header (it's at a fixed position)
        mvwprintw(window, 6, 2, "Directory Tree Preview:");
        (*line_num)++;
        // Don't count header in scroll offset
    }

    // Early exit if we're already past visible area
    if (*line_num >= max_y - 1) {
        return;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    struct stat statbuf;
    char full_path[MAX_PATH_LENGTH];
    size_t dir_path_len = strlen(dir_path);

    // Define window size for entries
    const int WINDOW_SIZE = 50; // Maximum entries to process at once

    struct {
        char name[MAX_PATH_LENGTH];
        bool is_dir;
        mode_t mode;
    } entries[WINDOW_SIZE];
    int entry_count = 0;

    // Collect all entries first
    while ((entry = readdir(dir)) != NULL && entry_count < WINDOW_SIZE) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        size_t name_len = strlen(entry->d_name);
        if (dir_path_len + name_len + 2 > MAX_PATH_LENGTH) continue;

        strcpy(full_path, dir_path);
        if (full_path[dir_path_len - 1] != '/') {
            strcat(full_path, "/");
        }
        strcat(full_path, entry->d_name);

        if (lstat(full_path, &statbuf) == -1) continue;

        strncpy(entries[entry_count].name, entry->d_name, MAX_PATH_LENGTH - 1);
        entries[entry_count].name[MAX_PATH_LENGTH - 1] = '\0';
        entries[entry_count].is_dir = S_ISDIR(statbuf.st_mode);
        entries[entry_count].mode = statbuf.st_mode;
        entry_count++;
    }
    closedir(dir);

    // Check if no entries were found
    if (entry_count == 0) {
        if (*current_count >= start_line && *line_num < max_y - 1) {
            mvwprintw(window, *line_num, 2 + level * 2, "This directory is empty");
            (*line_num)++;
        }
        (*current_count)++;
        return;
    }

    // Initialize magic only if we have entries to display
    magic_t magic_cookie = NULL;
    if (entry_count > 0) {
        magic_cookie = magic_open(MAGIC_MIME_TYPE);
        if (magic_cookie != NULL) {
            magic_load(magic_cookie, NULL);
        }
    }

    // Display collected entries
    for (int i = 0; i < entry_count && *line_num < max_y - 1; i++) {
        // Skip lines until we reach start_line
        if (*current_count >= DIRECTORY_TREE_MAX_TOTAL) {
            tree_limit_hit = true;
            break;
        }

        if (*current_count < start_line) {
            (*current_count)++;
            if (*current_count >= DIRECTORY_TREE_MAX_TOTAL) {
                tree_limit_hit = true;
                break;
            }
            // Still need to recurse into directories to count their lines
            if (entries[i].is_dir &&
                level < DIRECTORY_TREE_MAX_DEPTH) {
                size_t name_len = strlen(entries[i].name);
                if (dir_path_len + name_len + 2 <= MAX_PATH_LENGTH) {
                    strcpy(full_path, dir_path);
                    if (full_path[dir_path_len - 1] != '/') {
                        strcat(full_path, "/");
                    }
                    strcat(full_path, entries[i].name);
                    show_directory_tree(window, full_path, level + 1, line_num, max_y, max_x, start_line, current_count);
                    if (tree_limit_hit) {
                        break;
                    }
                }
            }
            continue;
        }

        // Reconstruct full path for symlink detection
        size_t name_len = strlen(entries[i].name);
        if (dir_path_len + name_len + 2 <= MAX_PATH_LENGTH) {
            strcpy(full_path, dir_path);
            if (full_path[dir_path_len - 1] != '/') {
                strcat(full_path, "/");
            }
            strcat(full_path, entries[i].name);
        }

        // Check if this is a symlink
        struct stat link_statbuf;
        bool is_symlink = (lstat(full_path, &link_statbuf) == 0 && S_ISLNK(link_statbuf.st_mode));
        char symlink_target[MAX_PATH_LENGTH] = {0};
        
        if (is_symlink) {
            ssize_t target_len = readlink(full_path, symlink_target, sizeof(symlink_target) - 1);
            if (target_len > 0) {
                symlink_target[target_len] = '\0';
            }
        }

        const char *emoji;
        if (entries[i].is_dir) {
            emoji = "📁";
        } else if (magic_cookie) { // if magic fails 
            if (dir_path_len + name_len + 2 <= MAX_PATH_LENGTH) {
                const char *mime_type = magic_file(magic_cookie, full_path);
                emoji = get_file_emoji(mime_type, entries[i].name);
            } else {
                emoji = "📄";
            }
        } else {
            emoji = "📄";
        }

        // Clear the line to prevent ghost characters from emojis
        wmove(window, *line_num, 2 + level * 2);
        for (int clear_x = 2 + level * 2; clear_x < max_x - 10; clear_x++) {
            waddch(window, ' ');
        }

        // Calculate available width for display
        int available_width = max_x - 4 - level * 2 - 10; // Account for permissions column
        int display_len = name_len + (is_symlink ? (4 + strlen(symlink_target)) : 0);
        
        if (display_len > available_width) {
            // Truncate if needed
            if (is_symlink && strlen(symlink_target) > 0) {
                int name_part = available_width / 2;
                int target_part = available_width - name_part - 4;
                mvwprintw(window, *line_num, 2 + level * 2, "%s %.*s -> %.*s...", 
                         emoji, name_part, entries[i].name, target_part, symlink_target);
            } else {
                mvwprintw(window, *line_num, 2 + level * 2, "%s %.*s", 
                         emoji, available_width, entries[i].name);
            }
        } else {
            if (is_symlink && strlen(symlink_target) > 0) {
                mvwprintw(window, *line_num, 2 + level * 2, "%s %s -> %s", 
                         emoji, entries[i].name, symlink_target);
            } else {
                mvwprintw(window, *line_num, 2 + level * 2, "%s %.*s", 
                         emoji, available_width, entries[i].name);
            }
        }

        char perm[10];
        snprintf(perm, sizeof(perm), "%o", entries[i].mode & 0777);
        mvwprintw(window, *line_num, max_x - 10, "%s", perm);
        (*line_num)++;
        (*current_count)++;
        if (*current_count >= DIRECTORY_TREE_MAX_TOTAL) {
            tree_limit_hit = true;
            break;
        }

        // Only recurse into directories if we have space
        if (entries[i].is_dir &&
            *line_num < max_y - 1 &&
            level < DIRECTORY_TREE_MAX_DEPTH) {
            if (dir_path_len + name_len + 2 <= MAX_PATH_LENGTH) {
                show_directory_tree(window, full_path, level + 1, line_num, max_y, max_x, start_line, current_count);
                if (tree_limit_hit) {
                    break;
                }
            }
        }
    }

    if (magic_cookie) {
        magic_close(magic_cookie);
    }

    if (level == 0 && tree_limit_hit && *line_num < max_y - 1) {
        mvwprintw(window, *line_num, 2, "[Preview truncated]");
        (*line_num)++;
    }
}

bool is_hidden(const char *filename) {
    return filename[0] == '.' && (strlen(filename) == 1 || (filename[1] != '.' && filename[1] != '\0'));
}

/** Function to get the total number of lines in a file
 *
 * @param file_path the path to the file
 * @return the total number of lines in the file
 */
int get_total_lines(const char *file_path) {
    FILE *file = fopen(file_path, "r");
    if (!file) return 0;

    int total_lines = 0;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        total_lines++;
    }

    fclose(file);
    return total_lines;
}

// Function to draw the directory window
void draw_directory_window(
        WINDOW *window,
        const char *directory,
        Vector *files_vector,
        CursorAndSlice *cas
) {
    int cols;
    int rows;
    getmaxyx(window, rows, cols);  // Get window dimensions
    
    // Clear the window and draw border
    werase(window);
    box(window, 0, 0);
    
    // Check if the directory is empty
    if (cas->num_files == 0) {
        mvwprintw(window, 1, 1, "This directory is empty");
        wrefresh(window);
        return;
    }
    
    // Calculate maximum number of items that can fit (accounting for borders)
    // Line 0 is top border, lines 1 to rows-2 are usable, line rows-1 is bottom border
    int max_visible_items = rows - 2;
    
    // Initialize magic for MIME type detection
    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE);
    if (magic_cookie == NULL || magic_load(magic_cookie, NULL) != 0) {
        // Fallback to basic directory/file emojis if magic fails
        for (int i = 0; i < max_visible_items && (cas->start + i) < cas->num_files; i++) {
            FileAttr fa = (FileAttr)files_vector->el[cas->start + i];
            const char *name = FileAttr_get_name(fa);
            
            // Construct full path for symlink detection
            char full_path[MAX_PATH_LENGTH];
            path_join(full_path, directory, name);
            
            // Check if this is a symlink
            struct stat statbuf;
            bool is_symlink = (lstat(full_path, &statbuf) == 0 && S_ISLNK(statbuf.st_mode));
            char symlink_target[MAX_PATH_LENGTH] = {0};
            
            if (is_symlink) {
                ssize_t target_len = readlink(full_path, symlink_target, sizeof(symlink_target) - 1);
                if (target_len > 0) {
                    symlink_target[target_len] = '\0';
                }
            }
            
            const char *emoji = FileAttr_is_dir(fa) ? "📁" : "📄";

            // Clear the line completely before drawing to prevent ghost characters
            wmove(window, i + 1, 1);
            for (int j = 1; j < cols - 1; j++) {
                waddch(window, ' ');
            }

            // Highlight logic: either cursor or select-all highlight
            bool is_cursor = ((cas->start + i) == cas->cursor);
            bool is_selected = g_select_all_highlight || is_cursor;
            if (is_selected) wattron(window, A_REVERSE);
            if (g_select_all_highlight && is_cursor) wattron(window, A_BOLD);

            int name_len = strlen(name);
            int target_len = is_symlink ? strlen(symlink_target) : 0;
            int display_len = name_len + (is_symlink ? (4 + target_len) : 0); // " -> " + target
            // Account for: border(1) + emoji(~2) + space(1) + ellipsis(3) + border(1) = ~8
            int max_name_len = cols - 8;
            
            if (display_len > max_name_len) {
                // Truncate if needed
                int available = max_name_len - (is_symlink ? 4 : 0); // 4 for " -> "
                if (is_symlink && target_len > 0) {
                    // Show name and truncated target
                    int name_part = available / 2;
                    int target_part = available - name_part - 7; // 7 for " -> " + "..."
                    if (target_part < 0) target_part = 0;
                    mvwprintw(window, i + 1, 1, "%s %.*s -> %.*s...", emoji, 
                             name_part, name, target_part, symlink_target);
                } else {
                    // Account for emoji + space + "..."
                    int max_chars = max_name_len - 3; // 3 for "..."
                    if (max_chars < 1) max_chars = 1;
                    mvwprintw(window, i + 1, 1, "%s %.*s...", emoji, max_chars, name);
                }
            } else {
                if (is_symlink && target_len > 0) {
                    mvwprintw(window, i + 1, 1, "%s %s -> %s", emoji, name, symlink_target);
                } else {
                    mvwprintw(window, i + 1, 1, "%s %s", emoji, name);
                }
            }

            if (is_selected) wattroff(window, A_REVERSE);
            if (g_select_all_highlight && is_cursor) wattroff(window, A_BOLD);
        }
    } else {
        // Use magic to get proper file type emojis
        for (int i = 0; i < max_visible_items && (cas->start + i) < cas->num_files; i++) {
            FileAttr fa = (FileAttr)files_vector->el[cas->start + i];
            const char *name = FileAttr_get_name(fa);
            
            // Construct full path for MIME type detection
            char full_path[MAX_PATH_LENGTH];
            path_join(full_path, directory, name);
            
            // Check if this is a symlink
            struct stat statbuf;
            bool is_symlink = (lstat(full_path, &statbuf) == 0 && S_ISLNK(statbuf.st_mode));
            char symlink_target[MAX_PATH_LENGTH] = {0};
            
            if (is_symlink) {
                ssize_t target_len = readlink(full_path, symlink_target, sizeof(symlink_target) - 1);
                if (target_len > 0) {
                    symlink_target[target_len] = '\0';
                }
            }
            
            const char *emoji;
            if (FileAttr_is_dir(fa)) {
                emoji = "📁";
            } else {
                const char *mime_type = magic_file(magic_cookie, full_path);
                emoji = get_file_emoji(mime_type, name);
            }

            // Clear the line completely before drawing to prevent ghost characters
            wmove(window, i + 1, 1);
            for (int j = 1; j < cols - 1; j++) {
                waddch(window, ' ');
            }

            // Highlight logic for the second loop as well (first set)
            bool is_cursor1 = ((cas->start + i) == cas->cursor);
            bool is_selected1 = g_select_all_highlight || is_cursor1;
            if (is_selected1) wattron(window, A_REVERSE);
            if (g_select_all_highlight && is_cursor1) wattron(window, A_BOLD);

            int name_len = strlen(name);
            int target_len = is_symlink ? strlen(symlink_target) : 0;
            int display_len = name_len + (is_symlink ? (4 + target_len) : 0); // " -> " + target
            // Account for: border(1) + emoji(~2) + space(1) + ellipsis(3) + border(1) = ~8
            int max_name_len = cols - 8;
            
            if (display_len > max_name_len) {
                // Truncate if needed
                int available = max_name_len - (is_symlink ? 4 : 0); // 4 for " -> "
                if (is_symlink && target_len > 0) {
                    // Show name and truncated target
                    int name_part = available / 2;
                    int target_part = available - name_part - 7; // 7 for " -> " + "..."
                    if (target_part < 0) target_part = 0;
                    mvwprintw(window, i + 1, 1, "%s %.*s -> %.*s...", emoji, 
                             name_part, name, target_part, symlink_target);
                } else {
                    // Account for emoji + space + "..."
                    int max_chars = max_name_len - 3; // 3 for "..."
                    if (max_chars < 1) max_chars = 1;
                    mvwprintw(window, i + 1, 1, "%s %.*s...", emoji, max_chars, name);
                }
            } else {
                if (is_symlink && target_len > 0) {
                    mvwprintw(window, i + 1, 1, "%s %s -> %s", emoji, name, symlink_target);
                } else {
                    mvwprintw(window, i + 1, 1, "%s %s", emoji, name);
                }
            }

            // Reset attributes for a clean start and compute per-line highlight anew
            wattroff(window, A_REVERSE);
            wattroff(window, A_BOLD);
            bool is_cursor = ((cas->start + i) == cas->cursor);
            bool is_selected = g_select_all_highlight || is_cursor;
            if (is_selected) wattron(window, A_REVERSE);
            if (g_select_all_highlight && is_cursor) wattron(window, A_BOLD);
            // Note: content for this line is printed above in this loop
            // Cleanup after printing to avoid leakage
            if (g_select_all_highlight && is_cursor) wattroff(window, A_BOLD);
            if (is_selected) wattroff(window, A_REVERSE);
        }
        magic_close(magic_cookie);
    }

    mvwprintw(window, 0, 2, "Directory: %.*s", cols - 13, directory);
    wrefresh(window);
}

/** Function to draw the preview window
 *
 * @param window the window to draw the preview in
 * @param current_directory the current directory
 * @param selected_entry the selected entry
 * @param start_line the starting line of the preview
 */
void draw_preview_window(WINDOW *window, const char *current_directory, const char *selected_entry, int start_line) {
    // Clear the window and draw a border
    werase(window);
    box(window, 0, 0);

    // Get window dimensions
    int max_x, max_y;
    getmaxyx(window, max_y, max_x);

    // Display the selected entry path
    char file_path[MAX_PATH_LENGTH];
    path_join(file_path, current_directory, selected_entry);
    mvwprintw(window, 0, 2, "Selected Entry: %.*s", max_x - 4, file_path);

    // Attempt to retrieve file information
    struct stat file_stat;
    if (stat(file_path, &file_stat) == -1) {
        mvwprintw(window, 2, 2, "Unable to retrieve file information");
        wrefresh(window);
        return;
    }
    
    // Display file size or directory size with emoji
    char fileSizeStr[20];
    if (S_ISDIR(file_stat.st_mode)) {
        static char last_preview_size_path[MAX_PATH_LENGTH] = "";
        static struct timespec last_preview_size_change = {0};
        static bool last_preview_size_initialized = false;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        bool path_changed = !last_preview_size_initialized ||
                            strncmp(last_preview_size_path, file_path, MAX_PATH_LENGTH) != 0;
        if (path_changed) {
            strncpy(last_preview_size_path, file_path, MAX_PATH_LENGTH - 1);
            last_preview_size_path[MAX_PATH_LENGTH - 1] = '\0';
            last_preview_size_change = now;
            last_preview_size_initialized = true;
        }

        long elapsed_ns = (now.tv_sec - last_preview_size_change.tv_sec) * 1000000000L +
                          (now.tv_nsec - last_preview_size_change.tv_nsec);
        bool allow_enqueue = (elapsed_ns >= DIR_SIZE_REQUEST_DELAY_NS) && dir_size_can_enqueue();

        long dir_size = allow_enqueue ? get_directory_size(file_path)
                                      : get_directory_size_peek(file_path);
        if (dir_size == -1) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Error");
        } else if (dir_size == DIR_SIZE_VIRTUAL_FS) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Virtual FS");
        } else if (dir_size == DIR_SIZE_TOO_LARGE) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Too large");
        } else if (dir_size == DIR_SIZE_PERMISSION_DENIED) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Permission denied");
        } else if (dir_size == DIR_SIZE_PENDING) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), allow_enqueue ? "Calculating..." : "Waiting...");
        } else {
            format_file_size(fileSizeStr, dir_size);
        }
        mvwprintw(window, 2, 2, "📁 Directory Size: %s", fileSizeStr);
    } else {
        format_file_size(fileSizeStr, file_stat.st_size);
        mvwprintw(window, 2, 2, "📏 File Size: %s", fileSizeStr);
    }

    // Display file permissions with emoji
    char permissions[10];
    snprintf(permissions, sizeof(permissions), "%o", file_stat.st_mode & 0777);
    mvwprintw(window, 3, 2, "🔒 Permissions: %s", permissions);

    // Display last modification time with emoji
    char modTime[50];
    strftime(modTime, sizeof(modTime), "%c", localtime(&file_stat.st_mtime));
    mvwprintw(window, 4, 2, "🕒 Last Modified: %s", modTime);
    
    // Display MIME type using libmagic
    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE);
    if (magic_cookie != NULL && magic_load(magic_cookie, NULL) == 0) {
        const char *mime_type = magic_file(magic_cookie, file_path);
        mvwprintw(window, 5, 2, "MIME Type: %s", mime_type ? mime_type : "Unknown");
        magic_close(magic_cookie);
    } else {
        mvwprintw(window, 5, 2, "MIME Type: Unable to detect");
    }

    // If the file is a directory, display the directory contents
    if (S_ISDIR(file_stat.st_mode)) {
        int line_num = 7;
        int current_count = 0;
        show_directory_tree(window, file_path, 0, &line_num, max_y, max_x, start_line, &current_count);

        // If the directory is empty, show a message
      
    } else if (is_supported_file_type(file_path)) {
        // Display file preview for supported types
        FILE *file = fopen(file_path, "r");
        if (file) {
            char line[256];
            int line_num = 7;
            int current_line = 0;

            // Skip lines until start_line
            while (current_line < start_line && fgets(line, sizeof(line), file)) {
                current_line++;
            }

            // Display file content from start_line onward
            while (fgets(line, sizeof(line), file) && line_num < max_y - 1) {
                line[strcspn(line, "\n")] = '\0'; // Remove newline character

                // Replace tabs with spaces
                for (char *p = line; *p; p++) {
                    if (*p == '\t') {
                        *p = ' ';
                    }
                }

                mvwprintw(window, line_num++, 2, "%.*s", max_x - 4, line);
            }

            fclose(file);

            if (line_num < max_y - 1) {
                mvwprintw(window, line_num++, 2, "--------------------------------");
                mvwprintw(window, line_num++, 2, "[End of file]");
            }
        } else {
            mvwprintw(window, 7, 2, "Unable to open file for preview");
        }
    }

    // Refresh to show changes
    wrefresh(window);
}

/** Function to handle cursor movement in the directory window
 * @param cas the cursor and slice state
 */
void fix_cursor(CursorAndSlice *cas) {
    // Ensure cursor stays within valid range
    cas->cursor = MIN(cas->cursor, cas->num_files - 1);
    cas->cursor = MAX(0, cas->cursor);

    // Calculate visible window size (subtract 2 for borders)
    int visible_lines = cas->num_lines - 2;
    
    // If there are fewer files than visible lines, start should be 0
    if (cas->num_files <= visible_lines) {
        cas->start = 0;
        return;
    }

    // Adjust start position to keep cursor visible
    if (cas->cursor < cas->start) {
        cas->start = cas->cursor;
    } else if (cas->cursor >= cas->start + visible_lines) {
        cas->start = cas->cursor - visible_lines + 1;
    }

    // Ensure start position is valid - don't scroll past the end
    int max_start = cas->num_files - visible_lines;
    if (max_start < 0) max_start = 0;
    cas->start = MIN(cas->start, max_start);
    cas->start = MAX(0, cas->start);
    
    // Final check: ensure cursor is within the visible range
    // The cursor should be at position (cursor - start) in the visible area
    // Valid range is 0 to visible_lines - 1
    int cursor_relative_pos = cas->cursor - cas->start;
    if (cursor_relative_pos < 0 || cursor_relative_pos >= visible_lines) {
        // If cursor is out of visible range, adjust start to show it
        if (cursor_relative_pos < 0) {
            // Cursor is above visible area, move start up
            cas->start = cas->cursor;
        } else {
            // Cursor is below visible area, move start down
            cas->start = cas->cursor - visible_lines + 1;
            // Clamp to valid range (reuse max_start calculated above)
            if (cas->start < 0) cas->start = 0;
            if (cas->start > max_start) cas->start = max_start;
        }
    }
}

/** Function to redraw all windows
 *
 * @param state the application state
 */
void redraw_all_windows(AppState *state) {
    // Get new terminal dimensions
    struct winsize w;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);
    resize_term(w.ws_row, w.ws_col);

    // Update ncurses internal structures
    endwin();
    refresh();
    clear();

    // Recalculate window dimensions with minimum sizes
    int new_cols = MAX(COLS, 40);  // Minimum width of 40 columns
    int new_lines = MAX(LINES, 10); // Minimum height of 10 lines
    int banner_height = 3;
    int notif_height = 1;
    int main_height = new_lines - banner_height - notif_height;

    // Calculate subwindow dimensions with minimum sizes
    SIZE dir_win_width = MAX(new_cols / 3, 20);  // Minimum directory window width
    SIZE preview_win_width = new_cols - dir_win_width - 2; // Account for borders

    // Delete all windows first
    if (dirwin) delwin(dirwin);
    if (previewwin) delwin(previewwin);
    if (mainwin) delwin(mainwin);
    if (bannerwin) delwin(bannerwin);
    if (notifwin) delwin(notifwin);

    // Recreate all windows in order
    bannerwin = newwin(banner_height, new_cols, 0, 0);
    box(bannerwin, 0, 0);
    if (BANNER_TEXT && BUILD_INFO) {
        pthread_mutex_lock(&banner_mutex);
        draw_scrolling_banner(bannerwin, BANNER_TEXT, BUILD_INFO, banner_offset);
        pthread_mutex_unlock(&banner_mutex);
    }

    mainwin = newwin(main_height, new_cols, banner_height, 0);
    box(mainwin, 0, 0);

    // Create subwindows with proper border accounting
    int inner_height = main_height - 2;  // Account for main window borders
    int inner_start_y = 1;               // Start after main window's top border
    int dir_start_x = 1;                 // Start after main window's left border
    int preview_start_x = dir_win_width + 1; // Start after directory window

    // Ensure windows are created with correct positions
    dirwin = derwin(mainwin, inner_height, dir_win_width - 1, inner_start_y, dir_start_x);
    previewwin = derwin(mainwin, inner_height, preview_win_width, inner_start_y, preview_start_x);

    notifwin = newwin(notif_height, new_cols, new_lines - notif_height, 0);
    box(notifwin, 0, 0);

    // Update cursor and slice state with correct dimensions
    state->dir_window_cas.num_lines = inner_height;
    sync_selection_from_active(state, &state->dir_window_cas);

    // Draw borders for subwindows
    box(dirwin, 0, 0);
    box(previewwin, 0, 0);

    // Redraw content
    draw_directory_window(
        dirwin,
        state->current_directory,
        active_files(state),
        &state->dir_window_cas
    );

    draw_preview_window(
        previewwin,
        state->current_directory,
        state->selected_entry,
        state->preview_start_line
    );

    // Refresh all windows in correct order
    refresh();
    wrefresh(bannerwin);
    wrefresh(mainwin);
    wrefresh(dirwin);
    wrefresh(previewwin);
    wrefresh(notifwin);
}

/** Function to navigate up in the directory window
 *
 * @param cas the cursor and slice state
 * @param files the list of files
 * @param selected_entry the selected entry
 */
void navigate_up(CursorAndSlice *cas, Vector *files, const char **selected_entry,
                const char *current_directory, LazyLoadState *lazy_load) {
    cas->num_files = Vector_len(*files);
    if (cas->num_files > 0) {
        if (cas->cursor == 0) {
            // Wrap to bottom - need to ensure all files are loaded first
            if (lazy_load && current_directory) {
                load_more_files_if_needed(files, current_directory, cas,
                                          &lazy_load->files_loaded, lazy_load->total_files);
                cas->num_files = Vector_len(*files);
            }
            
            cas->cursor = cas->num_files - 1;
            // Calculate visible window size (subtract 2 for borders)
            int visible_lines = cas->num_lines - 2;
            // Adjust start to show the last page of entries
            // Ensure the cursor (at num_files - 1) is visible
            int max_start = cas->num_files - visible_lines;
            if (max_start < 0) {
                cas->start = 0;
            } else {
                cas->start = max_start;
            }
        } else {
            cas->cursor -= 1;
            // Adjust start if cursor would go off screen
            if (cas->cursor < cas->start) {
                cas->start = cas->cursor;
            }
        }
        fix_cursor(cas);
        if (cas->num_files > 0) {
            *selected_entry = FileAttr_get_name(files->el[cas->cursor]);
        }
    }
}

/** Function to navigate down in the directory window
 *
 * @param cas the cursor and slice state
 * @param files the list of files
 * @param selected_entry the selected entry
 * @param current_directory the current directory path
 * @param lazy_load the lazy loading state
 */
void navigate_down(CursorAndSlice *cas, Vector *files, const char **selected_entry, 
                   const char *current_directory, LazyLoadState *lazy_load) {
    cas->num_files = Vector_len(*files);
    if (cas->num_files > 0) {
        if (cas->cursor >= cas->num_files - 1) {
            // Wrap to top
            cas->cursor = 0;
            cas->start = 0;
        } else {
            cas->cursor += 1;
            // Calculate visible window size (subtract 2 for borders)
            int visible_lines = cas->num_lines - 2;
            
            // Adjust start if cursor would go off screen
            // The cursor should be visible, so if it's at or beyond the visible area, scroll
            if (cas->cursor >= cas->start + visible_lines) {
                cas->start = cas->cursor - visible_lines + 1;
            }
            
            // Ensure we don't scroll past the end
            int max_start = cas->num_files - visible_lines;
            if (max_start < 0) max_start = 0;
            if (cas->start > max_start) {
                cas->start = max_start;
            }
        }
        fix_cursor(cas);
        
        // Check if we need to load more files
        if (lazy_load && current_directory) {
            load_more_files_if_needed(files, current_directory, cas,
                                      &lazy_load->files_loaded, lazy_load->total_files);
            cas->num_files = Vector_len(*files);
        }
        
        if (cas->num_files > 0) {
            *selected_entry = FileAttr_get_name(files->el[cas->cursor]);
        }
    }
}

/** Function to navigate left in the directory window
 *
 * @param current_directory the current directory
 * @param files the list of files
 * @param cas the cursor and slice state
 * @param state the application state
 */
void navigate_left(char **current_directory, Vector *files, CursorAndSlice *dir_window_cas, AppState *state) {
    // Leaving the directory clears any active search view (search results point into the old directory list).
    search_clear(state);

    // Remember the directory name we’re leaving so we can re-select it in the parent.
    char *popped_dir = VecStack_pop(&directoryStack);

    // Check if the current directory is the root directory
    if (strcmp(*current_directory, "/") != 0) {
        // If not the root directory, move up one level
        char *last_slash = strrchr(*current_directory, '/');
        if (last_slash != NULL) {
            *last_slash = '\0'; // Remove the last directory from the path
            // Update lazy loading state
            if (state->lazy_load.directory_path) {
                free(state->lazy_load.directory_path);
            }
            state->lazy_load.directory_path = strdup(*current_directory);
            reload_directory_lazy(files, *current_directory, 
                                &state->lazy_load.files_loaded, &state->lazy_load.total_files);
        }
    }

    // Check if the current directory is now an empty string
    if ((*current_directory)[0] == '\0') {
        // If empty, set it back to the root directory
        strcpy(*current_directory, "/");
        // Update lazy loading state
        if (state->lazy_load.directory_path) {
            free(state->lazy_load.directory_path);
        }
        state->lazy_load.directory_path = strdup(*current_directory);
        reload_directory_lazy(files, *current_directory, 
                            &state->lazy_load.files_loaded, &state->lazy_load.total_files);
    }

    // Re-select the directory we left in the parent (if possible)
    if (popped_dir) {
        // Try to restore cursor to the popped directory within the parent listing
        SIZE idx = find_index_by_name_lazy(files, *current_directory, dir_window_cas, &state->lazy_load, popped_dir);
        if (idx != (SIZE)-1) {
            dir_window_cas->cursor = idx;
        } else {
            dir_window_cas->cursor = 0;
        }
        free(popped_dir);
    }

    // Reset cursor and start position
    dir_window_cas->cursor = 0;
    dir_window_cas->start = 0;
    dir_window_cas->num_lines = LINES - 6;
    dir_window_cas->num_files = Vector_len(*files);

    // Set selected_entry to the first file in the parent directory
    if (dir_window_cas->num_files > 0) {
        state->selected_entry = FileAttr_get_name(files->el[0]);
    } else {
        state->selected_entry = "";
    }

    werase(notifwin);
    show_notification(notifwin, "Navigated to parent directory: %s", *current_directory);
    should_clear_notif = false;
    
    wrefresh(notifwin);
}

/** Function to navigate right in the directory window
 *
 * @param state the application state
 * @param current_directory the current directory
 * @param selected_entry the selected entry
 * @param files the list of files
 * @param dir_window_cas the cursor and slice state
 */
void navigate_right(AppState *state, char **current_directory, Vector *files_view, CursorAndSlice *dir_window_cas) {
    if (!state || !current_directory || !*current_directory || !dir_window_cas) return;

    Vector *view = files_view ? files_view : &state->files;
    if (Vector_len(*view) == 0) return;
    if (dir_window_cas->cursor < 0 || (size_t)dir_window_cas->cursor >= Vector_len(*view)) return;

    // Verify if the selected entry is a directory (use the current visible list).
    FileAttr current_file = (FileAttr)view->el[dir_window_cas->cursor];
    const char *selected_entry = FileAttr_get_name(current_file);
    if (!FileAttr_is_dir(current_file) || !selected_entry) {
        werase(notifwin);
        show_notification(notifwin, "Selected entry is not a directory");
        should_clear_notif = false;

        wrefresh(notifwin);
        return;
    }

    // Construct the new path carefully
    char new_path[MAX_PATH_LENGTH];
    path_join(new_path, *current_directory, selected_entry);

    // Check if we're not re-entering the same directory path
    if (strcmp(new_path, *current_directory) == 0) {
        werase(notifwin);
        show_notification(notifwin, "Already in this directory");
        should_clear_notif = false;
        wrefresh(notifwin);
        return;
    }

    // Push the selected directory name onto the stack
    char *new_entry = strdup(selected_entry);
    if (new_entry == NULL) {
        mvwprintw(notifwin, LINES - 1, 1, "Memory allocation error");
        wrefresh(notifwin);
        return;
    }
    
    VecStack_push(&directoryStack, new_entry);

    // Free the old directory and set to the new path
    free(*current_directory);
    *current_directory = strdup(new_path);
    if (*current_directory == NULL) {
        mvwprintw(notifwin, LINES - 1, 1, "Memory allocation error");
        wrefresh(notifwin);
        free(VecStack_pop(&directoryStack));  // Roll back the stack operation
        return;
    }

    // Leaving this directory clears any active search view (search results point into the old directory list).
    search_clear(state);

    // Update lazy loading state for new directory
    if (state->lazy_load.directory_path) {
        free(state->lazy_load.directory_path);
    }
    state->lazy_load.directory_path = strdup(*current_directory);
    state->lazy_load.last_load_time = (struct timespec){0};
    
    // Reload directory contents in the new path (lazy loading)
    reload_directory_lazy(&state->files, *current_directory,
                          &state->lazy_load.files_loaded, &state->lazy_load.total_files);

    // Reset cursor and start position for the new directory
    dir_window_cas->cursor = 0;
    dir_window_cas->start = 0;
    dir_window_cas->num_lines = LINES - 6;
    dir_window_cas->num_files = Vector_len(state->files);

    // Set selected_entry to the first file in the new directory
    if (dir_window_cas->num_files > 0) {
        state->selected_entry = FileAttr_get_name(state->files.el[0]);
    } else {
        state->selected_entry = "";
    }

    // If there's only one entry, automatically select it
    if (dir_window_cas->num_files == 1) {
        state->selected_entry = FileAttr_get_name(state->files.el[0]);
    }

    werase(notifwin);
    show_notification(notifwin, "Entered directory: %s", state->selected_entry);
    should_clear_notif = false;    
    
    wrefresh(notifwin);
}

/** Function to handle terminal window resize
 *
 * @param sig the signal number
 */
void handle_winch(int sig) {
    (void)sig;  // Suppress unused parameter warning
    // Always set resize flag, even during edit mode
    resized = 1;
}

/**
 * Function to draw and scroll the banner text
 *
 * @param window the banner window
 * @param text the text to scroll
 * @param build_info the build information to display
 * @param offset the current offset for scrolling
 */
void draw_scrolling_banner(WINDOW *window, const char *text, const char *build_info, int offset) {
    // Include local date/time in the banner (fixed width so the scroll math stays stable).
    time_t t = time(NULL);
    struct tm tm_local;
    localtime_r(&t, &tm_local);
    char time_buf[32] = {0};
    strftime(time_buf, sizeof(time_buf), BANNER_TIME_FORMAT, &tm_local);

    char build_with_time[256];
    snprintf(build_with_time, sizeof(build_with_time), "%s%s%s",
             build_info ? build_info : "",
             BANNER_TIME_PREFIX,
             time_buf);

    int width = COLS - 2;
    int text_len = strlen(text);
    int build_len = (int)strlen(build_with_time);
    
    // Calculate total length including padding
    int total_len = width + text_len + build_len + 4;
    
    // Create the scroll text buffer
    char *scroll_text = malloc(2 * total_len + 1);
    if (!scroll_text) return;
    
    memset(scroll_text, ' ', 2 * total_len);
    scroll_text[2 * total_len] = '\0';
    
    // Copy the text pattern twice for smooth wrapping
    for (int i = 0; i < 2; i++) {
        int pos = i * total_len;
        memcpy(scroll_text + pos, text, text_len);
        memcpy(scroll_text + pos + text_len + 2, build_with_time, build_len);
    }
    
    // Draw the banner
    werase(window);
    box(window, 0, 0);
    mvwprintw(window, 1, 1, "%.*s", width, scroll_text + offset);
    wrefresh(window);
    
    free(scroll_text);
}

// Function to handle banner scrolling in a separate thread
void *banner_scrolling_thread(void *arg) {
    WINDOW *window = (WINDOW *)arg;
    // banner_offset is now a global variable - use the shared one
    struct timespec last_update_time;
    clock_gettime(CLOCK_MONOTONIC, &last_update_time);

    int total_scroll_length = (COLS - 2) + (int)strlen(BANNER_TEXT) + (int)strlen(BUILD_INFO) +
                              BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

    while (1) {
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        long time_diff = (current_time.tv_sec - last_update_time.tv_sec) * 1000000 +
                         (current_time.tv_nsec - last_update_time.tv_nsec) / 1000;

        if (time_diff >= BANNER_SCROLL_INTERVAL) {
            draw_scrolling_banner(window, BANNER_TEXT, BUILD_INFO, banner_offset);
            banner_offset = (banner_offset + 1) % total_scroll_length;
            last_update_time = current_time;
        }

        // Sleep for a short duration to prevent busy-waiting
        usleep(10000); // 10ms
    }

    return NULL;
}

void cleanup_temp_files() {
    char command[1024];
    snprintf(command, sizeof(command), "rm -rf /tmp/cupidfm_*_%d", getpid());
    system(command);
}

/** Function to handle cleanup and exit
 *
 * @param r the exit code
 * @param format the error message format
 */
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

    // Now that keybindings are loaded from config, initialize the banner
    char banner_text_buffer[256];
    snprintf(banner_text_buffer, sizeof(banner_text_buffer), "Welcome to CupidFM - Press %s to exit", keycode_to_string(kb.key_exit));

    // Assign to global BANNER_TEXT
    BANNER_TEXT = banner_text_buffer;

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

    enum {
        DIRECTORY_WIN_ACTIVE = 1,
        PREVIEW_WIN_ACTIVE = 2,
    } active_window = DIRECTORY_WIN_ACTIVE;

	    // Initial drawing
	    redraw_all_windows(&state);

	    // Load plugins after the UI is drawn so their notifications aren't immediately overwritten.
	    state.plugins = plugins_create();
	    if (state.plugins) {
	        plugins_set_context(state.plugins, state.current_directory, state.selected_entry);
	        // If plugins requested a reload during on_load, apply once on startup.
	        if (plugins_take_reload_request(state.plugins)) {
	            reload_directory(&state.files, state.current_directory);
	            state.dir_window_cas.num_files = Vector_len(state.files);
	            state.lazy_load.files_loaded = Vector_len(state.files);
	            state.lazy_load.total_files = state.lazy_load.files_loaded;
	        }
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
            plugins_set_context(state.plugins, state.current_directory, state.selected_entry);
            bool handled = plugins_handle_key(state.plugins, ch);
            if (plugins_take_quit_request(state.plugins)) {
                break;
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
                goto input_done;
            }
            if (handled) {
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
                    path_join(file_path, state.current_directory, state.selected_entry);
                    
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
                                &state);
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
                                   &state.dir_window_cas);
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
                    
                    // After exiting edit mode, redraw all windows with borders
                    // Redraw banner
                    if (bannerwin) {
                        box(bannerwin, 0, 0);
                        wrefresh(bannerwin);
                    }
                    
                    // Redraw main window with border
                    if (mainwin) {
                        box(mainwin, 0, 0);
                        wrefresh(mainwin);
                    }
                    
                    // Redraw directory and preview windows (they draw their own borders)
                    draw_directory_window(
                        dirwin,
                        state.current_directory,
                        active_files(&state),
                        &state.dir_window_cas
                    );
                    
                    draw_preview_window(
                        previewwin,
                        state.current_directory,
                        state.selected_entry,
                        state.preview_start_line
                    );
                    
                    // Redraw notification window
                    if (notifwin) {
                        box(notifwin, 0, 0);
                        wrefresh(notifwin);
                    }
                    
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
                            snprintf(dst_path, sizeof(dst_path), "%s/%s", storage_dir, name);
                            dst_path[sizeof(dst_path) - 1] = '\0';
                            for (int attempt = 1; access(dst_path, F_OK) == 0 && attempt < 1000; attempt++) {
                                snprintf(dst_path, sizeof(dst_path), "%s/%s_%d", storage_dir, name, attempt);
                                dst_path[sizeof(dst_path) - 1] = '\0';
                            }

                            char mv_cmd[2048];
                            snprintf(mv_cmd, sizeof(mv_cmd), "mv \"%s\" \"%s\"", src_path, dst_path);
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
                        if (dir_size == DIR_SIZE_PENDING) snprintf(size_buf, sizeof(size_buf), "Calculating...");
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
        }

input_done:
        // Clear notification window only if no new notification was displayed
        if (should_clear_notif) {
            werase(notifwin);
            wrefresh(notifwin);
        }

        // Redraw windows
        draw_directory_window(
                dirwin,
                state.current_directory,
                active_files(&state),
                &state.dir_window_cas
        );

        draw_preview_window(
                previewwin,
                state.current_directory,
                state.selected_entry,
                state.preview_start_line
        );

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
