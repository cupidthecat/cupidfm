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
#include <pwd.h>       // For getpwuid, getpwnam
#include <wordexp.h>   // For wordexp (tilde expansion)

// Local includes
#include "utils.h"
#include "vector.h"
#include "files.h"
#include "vecstack.h"
#include "main.h"
#include "globals.h"
#include "config.h"
#include "ui.h"

// Global resize flag
volatile sig_atomic_t resized = 0;
volatile sig_atomic_t is_editing = 0;

// Other global windows
WINDOW *mainwin = NULL;
WINDOW *dirwin = NULL;
WINDOW *previewwin = NULL;

VecStack directoryStack;

// Directory scroll position tracking
typedef struct DirScrollPos {
    char *path;
    SIZE cursor;
    SIZE start;
    struct DirScrollPos *next;
} DirScrollPos;

static DirScrollPos *dir_scroll_positions = NULL;

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
    int preview_start_line;
    LazyLoadState lazy_load;  // Lazy loading state
} AppState;

// Forward declaration of fix_cursor
void fix_cursor(CursorAndSlice *cas);

// --- Path / selection helpers ---------------------------------------------

static void strip_trailing_slashes_inplace(char *p) {
    if (!p) return;
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/') {
        p[n - 1] = '\0';
        n--;
    }
}

static const char *path_last_component(const char *p) {
    if (!p || !*p) return "";
    const char *end = p + strlen(p);
    while (end > p + 1 && end[-1] == '/') end--;
    const char *s = end;
    while (s > p && s[-1] != '/') s--;
    return s; // points into p, NUL-terminated
}

static SIZE find_loaded_index_by_name(Vector *files, const char *name) {
    if (!files || !name || !*name) return (SIZE)-1;
    SIZE n = (SIZE)Vector_len(*files);
    for (SIZE i = 0; i < n; i++) {
        FileAttr fa = (FileAttr)files->el[i];
        const char *nm = FileAttr_get_name(fa);
        if (nm && strcmp(nm, name) == 0) return i;
    }
    return (SIZE)-1;
}

// Drives your lazy loader until the entry shows up (or no more entries load).
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

        // Force "near end" so load_more_files_if_needed actually loads
        cas->cursor = (SIZE)(before - 1);

        load_more_files_if_needed(files, dir, cas,
                                  &lazy_load->files_loaded,
                                  lazy_load->total_files);

        size_t after = Vector_len(*files);
        cas->num_files = after;

        if (after == before) break; // nothing more loaded
    }

    return (SIZE)-1;
}

// Ensure lazy-loaded directory has at least (target_index+1) entries loaded.
static void load_until_index(Vector *files,
                             const char *current_directory,
                             CursorAndSlice *cas,
                             LazyLoadState *lazy_load,
                             SIZE target_index)
{
    // If directory is empty or target is already loaded, nothing to do.
    cas->num_files = Vector_len(*files);
    if (cas->num_files == 0 || target_index < cas->num_files) return;

    // Safety cap to avoid infinite loops if something goes wrong.
    for (int safety = 0; safety < 512; safety++) {
        size_t before = Vector_len(*files);

        // Drive lazy loader by pretending we're at the end of what's currently loaded.
        cas->num_files = before;
        cas->cursor = (before > 0) ? (before - 1) : 0;

        load_more_files_if_needed(files,
                                  current_directory,
                                  cas,
                                  &lazy_load->files_loaded,
                                  lazy_load->total_files);

        size_t after = Vector_len(*files);
        cas->num_files = after;

        if (after == before) break;                 // no more data loaded
        if (target_index < (SIZE)after) break;       // target now available
    }
}

// Helper to resync selection after directory reload
static void resync_selection(AppState *s) {
    s->dir_window_cas.num_files = Vector_len(s->files);

    if (s->dir_window_cas.num_files == 0) {
        s->dir_window_cas.cursor = 0;
        s->dir_window_cas.start = 0;
        s->selected_entry = "";
        return;
    }

    if (s->dir_window_cas.cursor >= s->dir_window_cas.num_files) {
        s->dir_window_cas.cursor = s->dir_window_cas.num_files - 1;
    }

    fix_cursor(&s->dir_window_cas);
    s->selected_entry = FileAttr_get_name(s->files.el[s->dir_window_cas.cursor]);
}

static void maybe_flush_input(struct timespec loop_start_time) {
    struct timespec loop_end_time;
    clock_gettime(CLOCK_MONOTONIC, &loop_end_time);
    long loop_duration_ns = (loop_end_time.tv_sec - loop_start_time.tv_sec) * 1000000000L +
                            (loop_end_time.tv_nsec - loop_start_time.tv_nsec);
    if (loop_duration_ns > INPUT_FLUSH_THRESHOLD_NS) {
        flushinp();
    }
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

        if (dir_path_len == 0 || dir_path[dir_path_len - 1] == '/') {
            snprintf(full_path, sizeof(full_path), "%s%s", dir_path, entry->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        }
        full_path[MAX_PATH_LENGTH - 1] = '\0';

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

        if (dir_path_len == 0 || dir_path[dir_path_len - 1] == '/') {
            snprintf(full_path, sizeof(full_path), "%s%s", dir_path, entry->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        }
        full_path[MAX_PATH_LENGTH - 1] = '\0';

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
                    if (dir_path_len == 0 || dir_path[dir_path_len - 1] == '/') {
                        snprintf(full_path, sizeof(full_path), "%s%s", dir_path, entries[i].name);
                    } else {
                        int ret = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entries[i].name);
                        if (ret >= (int)sizeof(full_path)) {
                            full_path[sizeof(full_path) - 1] = '\0'; // Ensure null termination
                        }
                    }
                    full_path[MAX_PATH_LENGTH - 1] = '\0';
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
            if (dir_path_len == 0 || dir_path[dir_path_len - 1] == '/') {
                snprintf(full_path, sizeof(full_path), "%s%s", dir_path, entries[i].name);
            } else {
                int ret = snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entries[i].name);
                if (ret >= (int)sizeof(full_path)) {
                    full_path[sizeof(full_path) - 1] = '\0'; // Ensure null termination
                }
            }
            full_path[MAX_PATH_LENGTH - 1] = '\0';
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
                // full_path already constructed safely above
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

            if ((cas->start + i) == cas->cursor) {
                wattron(window, A_REVERSE);
            }

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

            if ((cas->start + i) == cas->cursor) {
                wattroff(window, A_REVERSE);
            }
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

            if ((cas->start + i) == cas->cursor) {
                wattron(window, A_REVERSE);
            }

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

            if ((cas->start + i) == cas->cursor) {
                wattroff(window, A_REVERSE);
            }
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

    // Attempt to retrieve file information (use lstat for POSIX-correct symlink handling)
    struct stat file_stat;
    if (lstat(file_path, &file_stat) == -1) {
        if (errno == EACCES) {
            mvwprintw(window, 2, 2, "Permission denied");
        } else if (errno == ENOENT) {
            mvwprintw(window, 2, 2, "File not found (it may have been removed)");
        } else {
            mvwprintw(window, 2, 2, "Unable to stat: %s", strerror(errno));
        }
        wrefresh(window);
        return;
    }
    
    // Show symlink target info if it's a symlink
    if (S_ISLNK(file_stat.st_mode)) {
        char link_target[MAX_PATH_LENGTH];
        ssize_t target_len = readlink(file_path, link_target, sizeof(link_target) - 1);
        if (target_len > 0) {
            link_target[target_len] = '\0';
            // Try to stat the target to show target info
            struct stat target_stat;
            if (stat(file_path, &target_stat) == 0) {
                if (S_ISDIR(target_stat.st_mode)) {
                    mvwprintw(window, 1, 2, "Symlink -> %s (directory)", link_target);
                } else {
                    mvwprintw(window, 1, 2, "Symlink -> %s (file, %ld bytes)", link_target, (long)target_stat.st_size);
                }
            } else {
                mvwprintw(window, 1, 2, "Symlink -> %s (broken)", link_target);
            }
        }
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
      
    } else if (is_archive_file(file_path)) {
        // Display archive contents preview
        display_archive_preview(window, file_path, start_line, max_y, max_x);
      
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
    fix_cursor(&state->dir_window_cas);

    // Draw borders for subwindows
    box(dirwin, 0, 0);
    box(previewwin, 0, 0);

    // Redraw content
    draw_directory_window(
        dirwin,
        state->current_directory,
        &state->files,
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
    if (cas->num_files > 0) {
        if (cas->cursor == 0) {
            // Wrap to bottom - need to ensure all files are loaded first
            load_more_files_if_needed(files, current_directory, cas, 
                                     &lazy_load->files_loaded, lazy_load->total_files);
            cas->num_files = Vector_len(*files);
            
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
        load_more_files_if_needed(files, current_directory, cas, 
                                  &lazy_load->files_loaded, lazy_load->total_files);
        cas->num_files = Vector_len(*files);
        
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
    // Strip trailing slashes for consistent path handling
    strip_trailing_slashes_inplace(*current_directory);
    
    // Save current directory's scroll position before navigating away
    char *current_path = *current_directory;
    DirScrollPos *pos = dir_scroll_positions;
    DirScrollPos *prev = NULL;
    while (pos && strcmp(pos->path, current_path) != 0) {
        prev = pos;
        pos = pos->next;
    }
    
    if (pos) {
        // Update existing position
        pos->cursor = dir_window_cas->cursor;
        pos->start = dir_window_cas->start;
    } else {
        // Create new position entry
        pos = malloc(sizeof(DirScrollPos));
        if (pos) {
            pos->path = strdup(current_path);
            pos->cursor = dir_window_cas->cursor;
            pos->start = dir_window_cas->start;
            pos->next = dir_scroll_positions;
            dir_scroll_positions = pos;
        }
    }
    
    // Determine parent directory path
    char parent_path[MAX_PATH_LENGTH];
    if (strcmp(*current_directory, "/") != 0) {
        // If not the root directory, move up one level
        char *last_slash = strrchr(*current_directory, '/');
        if (last_slash != NULL) {
            size_t parent_len = last_slash - *current_directory;
            if (parent_len == 0) {
                // Parent is root
                strncpy(parent_path, "/", sizeof(parent_path));
            } else {
                strncpy(parent_path, *current_directory, parent_len);
                parent_path[parent_len] = '\0';
            }
        } else {
            strncpy(parent_path, "/", sizeof(parent_path));
        }
    } else {
        strncpy(parent_path, "/", sizeof(parent_path));
    }
    
    // Check if the parent directory is now an empty string
    if (parent_path[0] == '\0') {
        strncpy(parent_path, "/", sizeof(parent_path));
    }
    
    // Update current_directory to parent
    free(*current_directory);
    *current_directory = strdup(parent_path);
    
    // Update lazy loading state
    if (state->lazy_load.directory_path) {
        free(state->lazy_load.directory_path);
    }
    state->lazy_load.directory_path = strdup(*current_directory);
    reload_directory_lazy(files, *current_directory, 
                        &state->lazy_load.files_loaded, &state->lazy_load.total_files);

    // Pop the last directory name from the stack (we'll reselect it in the parent)
    char *child_name = VecStack_pop(&directoryStack);

    // Use the actual dir window height (more accurate than LINES - 6)
    {
        int rows, cols;
        getmaxyx(dirwin, rows, cols);
        (void)cols;
        dir_window_cas->num_lines = rows;
    }

    dir_window_cas->num_files = Vector_len(*files);

    // Restore scroll position for the parent directory (and load enough entries first)
    DirScrollPos *saved_pos = dir_scroll_positions;
    while (saved_pos && strcmp(saved_pos->path, *current_directory) != 0) {
        saved_pos = saved_pos->next;
    }

    if (saved_pos) {
        int visible_lines = (int)dir_window_cas->num_lines - 2;
        SIZE target = saved_pos->cursor;
        if (visible_lines > 0) {
            SIZE need = saved_pos->start + (SIZE)visible_lines;
            if (need > 0) target = MAX(target, need - 1);
        }

        load_until_index(files, *current_directory, dir_window_cas, &state->lazy_load, target);
        dir_window_cas->num_files = Vector_len(*files);
    }

    if (dir_window_cas->num_files == 0) {
        dir_window_cas->cursor = 0;
        dir_window_cas->start = 0;
        state->selected_entry = "";
    } else if (saved_pos) {
        dir_window_cas->cursor = (saved_pos->cursor < dir_window_cas->num_files)
                                  ? saved_pos->cursor
                                  : (dir_window_cas->num_files - 1);

        dir_window_cas->start = saved_pos->start;
        if (dir_window_cas->start >= dir_window_cas->num_files) {
            dir_window_cas->start = 0;
        }

        fix_cursor(dir_window_cas);
        state->selected_entry = FileAttr_get_name(files->el[dir_window_cas->cursor]);
    } else {
        dir_window_cas->cursor = 0;
        dir_window_cas->start = 0;
        state->selected_entry = FileAttr_get_name(files->el[0]);
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
void navigate_right(AppState *state, char **current_directory, const char *selected_entry, Vector *files, CursorAndSlice *dir_window_cas) {
    // Verify if the selected entry is a directory
    FileAttr current_file = files->el[dir_window_cas->cursor];
    if (!FileAttr_is_dir(current_file)) {
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

    // Save CURRENT directory scroll position BEFORE leaving it (so parent restores on back)
    {
        const char *cur_path = *current_directory;
        DirScrollPos *pos = dir_scroll_positions;
        while (pos && strcmp(pos->path, cur_path) != 0) {
            pos = pos->next;
        }

        if (pos) {
            pos->cursor = dir_window_cas->cursor;
            pos->start  = dir_window_cas->start;
        } else {
            pos = malloc(sizeof(DirScrollPos));
            if (pos) {
                pos->path = strdup(cur_path);
                pos->cursor = dir_window_cas->cursor;
                pos->start  = dir_window_cas->start;
                pos->next   = dir_scroll_positions;
                dir_scroll_positions = pos;
            }
        }
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

    // Update lazy loading state for new directory
    if (state->lazy_load.directory_path) {
        free(state->lazy_load.directory_path);
    }
    state->lazy_load.directory_path = strdup(*current_directory);
    
    // Reload directory contents in the new path (lazy loading)
    reload_directory_lazy(files, *current_directory, 
                        &state->lazy_load.files_loaded, &state->lazy_load.total_files);

    // Use the actual dir window height for correct visible_lines math
    {
        int rows, cols;
        getmaxyx(dirwin, rows, cols);
        (void)cols;
        dir_window_cas->num_lines = rows;
    }

    dir_window_cas->num_files = Vector_len(*files);

    // Restore scroll position if we have one for this directory (and load enough entries first)
    DirScrollPos *saved_pos = dir_scroll_positions;
    while (saved_pos && strcmp(saved_pos->path, *current_directory) != 0) {
        saved_pos = saved_pos->next;
    }

    if (saved_pos) {
        int visible_lines = (int)dir_window_cas->num_lines - 2;
        SIZE target = saved_pos->cursor;
        if (visible_lines > 0) {
            SIZE need = saved_pos->start + (SIZE)visible_lines;
            if (need > 0) target = MAX(target, need - 1);
        }

        load_until_index(files, *current_directory, dir_window_cas, &state->lazy_load, target);
        dir_window_cas->num_files = Vector_len(*files);
    }

    if (dir_window_cas->num_files == 0) {
        dir_window_cas->cursor = 0;
        dir_window_cas->start = 0;
        state->selected_entry = "";
    } else if (saved_pos) {
        dir_window_cas->cursor = (saved_pos->cursor < dir_window_cas->num_files)
                                  ? saved_pos->cursor
                                  : (dir_window_cas->num_files - 1);

        dir_window_cas->start = saved_pos->start;
        if (dir_window_cas->start >= dir_window_cas->num_files) {
            dir_window_cas->start = 0;
        }

        fix_cursor(dir_window_cas);
        state->selected_entry = FileAttr_get_name(files->el[dir_window_cas->cursor]);
    } else {
        dir_window_cas->cursor = 0;
        dir_window_cas->start = 0;
        state->selected_entry = FileAttr_get_name(files->el[0]);
    }

    // If there's only one entry, automatically select it
    if (dir_window_cas->num_files == 1) {
        state->selected_entry = FileAttr_get_name(files->el[0]);
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
    struct timespec current_time;
    clock_gettime(CLOCK_MONOTONIC, &current_time);
    
    // Only update banner if enough time has passed
    long time_diff = (current_time.tv_sec - last_scroll_time.tv_sec) * 1000000 +
                    (current_time.tv_nsec - last_scroll_time.tv_nsec) / 1000;
        
    if (time_diff < BANNER_SCROLL_INTERVAL) {
        return;  // Skip update if not enough time has passed
    }
    
    int width = COLS - 2;
    int text_len = strlen(text);
    int build_len = strlen(build_info);
    
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
        memcpy(scroll_text + pos + text_len + 2, build_info, build_len);
    }
    
    // Draw the banner
    werase(window);
    box(window, 0, 0);
    mvwprintw(window, 1, 1, "%.*s", width, scroll_text + offset);
    wrefresh(window);
    
    free(scroll_text);
    
    // Update last scroll time
    last_scroll_time = current_time;
}

// Function to handle banner scrolling in a separate thread
void *banner_scrolling_thread(void *arg) {
    WINDOW *window = (WINDOW *)arg;
    // banner_offset is now a global variable - use the shared one
    struct timespec last_update_time;
    clock_gettime(CLOCK_MONOTONIC, &last_update_time);

    int total_scroll_length = COLS + strlen(BANNER_TEXT) + strlen(BUILD_INFO) + 4;

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
int main(int argc, char *argv[]) {
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
    } else if (config_errors == 1 && strstr(error_buffer, "Configuration file not found")) {
        // Configuration file not found; create a default config file
        FILE *fp = fopen(config_path, "w");
        if (fp) {
            fprintf(fp, "# CupidFM Configuration File\n");
            fprintf(fp, "# Automatically generated on first run.\n\n");

            // Navigation Keys
            fprintf(fp, "key_up=KEY_UP\n");
            fprintf(fp, "key_down=KEY_DOWN\n");
            fprintf(fp, "key_left=KEY_LEFT\n");
            fprintf(fp, "key_right=KEY_RIGHT\n");
            fprintf(fp, "key_tab=Tab\n");
            fprintf(fp, "key_exit=F1\n");

            // File Management
            fprintf(fp, "key_edit=^E  # Enter edit mode\n");
            fprintf(fp, "key_copy=^C  # Copy selected file\n");
            fprintf(fp, "key_paste=^V  # Paste copied file\n");
            fprintf(fp, "key_cut=^X  # Cut (move) file\n");
            fprintf(fp, "key_delete=^D  # Delete selected file\n");
            fprintf(fp, "key_rename=^R  # Rename file\n");
            fprintf(fp, "key_new=^N  # Create new file\n");
            fprintf(fp, "key_save=^S  # Save changes\n\n");
            fprintf(fp, "key_new_dir=Shift+N  # Create new directory\n");
            // Editing Mode Keys
            fprintf(fp, "edit_up=KEY_UP\n");
            fprintf(fp, "edit_down=KEY_DOWN\n");
            fprintf(fp, "edit_left=KEY_LEFT\n");
            fprintf(fp, "edit_right=KEY_RIGHT\n");
            fprintf(fp, "edit_save=^S # Save in editor\n");
            fprintf(fp, "edit_quit=^Q # Quit editor\n");
            fprintf(fp, "edit_backspace=KEY_BACKSPACE\n");

            fprintf(fp, "info_label_width=15");

            fclose(fp);

            // Notify the user about the creation of the default config file
            show_popup("First Run Setup",
                "No config was found.\n"
                "A default config has been created at:\n\n"
                "  %s\n\n"
                "Press any key to continue...",
                config_path);
        } else {
            // Failed to create the config file
            show_notification(notifwin, "Failed to create default configuration file.");
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
    state.current_directory = malloc(MAX_PATH_LENGTH);
    if (state.current_directory == NULL) {
        die(1, "Memory allocation error");
    }

    // Check if a directory path was provided as an argument
    if (argc > 1) {
        char expanded_path[MAX_PATH_LENGTH];
        wordexp_t p;
        
        // Expand tilde and other shell expansions
        if (wordexp(argv[1], &p, 0) == 0) {
            if (p.we_wordc > 0 && strlen(p.we_wordv[0]) < MAX_PATH_LENGTH) {
                strncpy(expanded_path, p.we_wordv[0], MAX_PATH_LENGTH - 1);
                expanded_path[MAX_PATH_LENGTH - 1] = '\0';
            } else {
                strncpy(expanded_path, argv[1], MAX_PATH_LENGTH - 1);
                expanded_path[MAX_PATH_LENGTH - 1] = '\0';
            }
            wordfree(&p);
        } else {
            // Expansion failed, use argument as-is
            strncpy(expanded_path, argv[1], MAX_PATH_LENGTH - 1);
            expanded_path[MAX_PATH_LENGTH - 1] = '\0';
        }
        
        // Try to resolve the path (handles relative paths, etc.)
        char resolved_path[MAX_PATH_LENGTH];
        char *final_path = NULL;
        
        if (realpath(expanded_path, resolved_path) != NULL) {
            // realpath succeeded - use resolved path
            final_path = resolved_path;
        } else {
            // realpath failed - try using expanded_path directly if it's absolute
            // or construct absolute path from current directory
            if (expanded_path[0] == '/') {
                // Already absolute, use as-is
                final_path = expanded_path;
            } else {
                // Relative path - try to make it absolute
                char cwd[MAX_PATH_LENGTH];
                if (getcwd(cwd, MAX_PATH_LENGTH) != NULL) {
                    snprintf(resolved_path, MAX_PATH_LENGTH, "%s/%s", cwd, expanded_path);
                    final_path = resolved_path;
                } else {
                    // Can't get cwd, use expanded_path as-is
                    final_path = expanded_path;
                }
            }
        }
        
        // Check if it's a directory
        struct stat path_stat;
        if (final_path && stat(final_path, &path_stat) == 0 && S_ISDIR(path_stat.st_mode)) {
            strncpy(state.current_directory, final_path, MAX_PATH_LENGTH - 1);
            state.current_directory[MAX_PATH_LENGTH - 1] = '\0';
        } else {
            // Not a directory or doesn't exist - fall back to current directory
            if (getcwd(state.current_directory, MAX_PATH_LENGTH) == NULL) {
                die(1, "Unable to get current working directory");
            }
            // Show error notification (but continue with current directory)
            show_notification(notifwin, "Invalid directory: %s (using current directory)", argv[1]);
            should_clear_notif = false;
        }
    } else {
        // No argument provided - use current working directory
        if (getcwd(state.current_directory, MAX_PATH_LENGTH) == NULL) {
            die(1, "Unable to get current working directory");
        }
    }

    // Strip trailing slashes for consistent path handling
    strip_trailing_slashes_inplace(state.current_directory);

    // Seed the stack so the first LEFT (after starting inside a dir) can reselect us in parent
    if (strcmp(state.current_directory, "/") != 0) {
        const char *leaf = path_last_component(state.current_directory);
        if (leaf && *leaf) {
            char *seed = strdup(leaf);
            if (seed) VecStack_push(&directoryStack, seed);
        }
    }

    state.selected_entry = "";

    state.files = Vector_new(10);
    
    // Initialize lazy loading state
    state.lazy_load.directory_path = strdup(state.current_directory);
    state.lazy_load.files_loaded = 0;
    state.lazy_load.total_files = 0;
    state.lazy_load.is_loading = false;
    
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

    // Set a separate timeout for mainwin to handle scrolling
    wtimeout(mainwin, INPUT_CHECK_INTERVAL);  // Set shorter timeout for input checking

    // Initialize scrolling variables
    // banner_offset is now a global variable defined in globals.c
    struct timespec last_update_time;
    clock_gettime(CLOCK_MONOTONIC, &last_update_time);

    // Calculate the total scroll length for the banner
    int total_scroll_length = COLS + strlen(BANNER_TEXT) + strlen(BUILD_INFO) + 4;

    int ch;
    while ((ch = getch()) != kb.key_exit) {
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
            draw_scrolling_banner(bannerwin, BANNER_TEXT, BUILD_INFO, banner_offset);
            banner_offset = (banner_offset + 1) % total_scroll_length;
            last_update_time = current_time;
        }

        clock_gettime(CLOCK_MONOTONIC, &current_time);
        long notification_diff = (current_time.tv_sec - last_notification_time.tv_sec) * 1000 +
                               (current_time.tv_nsec - last_notification_time.tv_nsec) / 1000000;

        if (!should_clear_notif && notification_diff >= NOTIFICATION_TIMEOUT_MS) {
            werase(notifwin);
            wrefresh(notifwin);
            should_clear_notif = true;
        }

        if (ch != ERR) {
            dir_size_note_user_activity();

            // 1) UP
            if (ch == kb.key_up) {
                if (active_window == DIRECTORY_WIN_ACTIVE) {
                    navigate_up(&state.dir_window_cas, &state.files, &state.selected_entry,
                               state.current_directory, &state.lazy_load);
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
                    navigate_down(&state.dir_window_cas, &state.files, &state.selected_entry,
                                 state.current_directory, &state.lazy_load);
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
                    if (FileAttr_is_dir(state.files.el[state.dir_window_cas.cursor])) {
                        navigate_right(&state,
                                    &state.current_directory,
                                    state.selected_entry,
                                    &state.files,
                                    &state.dir_window_cas);
                        state.preview_start_line = 0;

                        // If there's only one file in the directory, auto-select it
                        if (state.dir_window_cas.num_files == 1) {
                            state.selected_entry = FileAttr_get_name(state.files.el[0]);
                        }
                        werase(notifwin);
                        show_notification(notifwin, "Entered directory: %s", state.selected_entry);
                        wrefresh(notifwin);
                        should_clear_notif = false;
                    } else {
                        werase(notifwin);
                        show_notification(notifwin, "Selected entry is not a directory");
                        wrefresh(notifwin);
                        should_clear_notif = false;
                    }
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
                        &state.files,
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
                    char full_path[MAX_PATH_LENGTH];
                    path_join(full_path, state.current_directory, state.selected_entry);
                    copy_to_clipboard(full_path);
                    strncpy(copied_filename, state.selected_entry, MAX_PATH_LENGTH - 1);
                    copied_filename[MAX_PATH_LENGTH - 1] = '\0';
                    werase(notifwin);
                    show_notification(notifwin, "Copied to clipboard: %s", state.selected_entry);
                    wrefresh(notifwin);
                    should_clear_notif = false;
                }
            }

            // 8) PASTE
            else if (ch == kb.key_paste) {
                if (active_window == DIRECTORY_WIN_ACTIVE && copied_filename[0] != '\0') {
                    paste_from_clipboard(state.current_directory, copied_filename);
                    reload_directory(&state.files, state.current_directory);
                    resync_selection(&state);
                    werase(notifwin);
                    show_notification(notifwin, "Pasted file: %s", copied_filename);
                    wrefresh(notifwin);
                    should_clear_notif = false;
                }
            }

            // 9) CUT 
            else if (ch == kb.key_cut) {
                if (active_window == DIRECTORY_WIN_ACTIVE && state.selected_entry) {
                    char full_path[MAX_PATH_LENGTH];
                    path_join(full_path, state.current_directory, state.selected_entry);
                    char name_copy[MAX_PATH_LENGTH];
                    strncpy(name_copy, state.selected_entry, MAX_PATH_LENGTH - 1);
                    name_copy[MAX_PATH_LENGTH - 1] = '\0';
                    cut_and_paste(full_path);
                    strncpy(copied_filename, name_copy, MAX_PATH_LENGTH - 1);
                    copied_filename[MAX_PATH_LENGTH - 1] = '\0';

                    // Reload directory to reflect the cut file
                    reload_directory(&state.files, state.current_directory);
                    resync_selection(&state);

                    werase(notifwin);
                    show_notification(notifwin, "Cut to clipboard: %s", name_copy);
                    wrefresh(notifwin);
                    should_clear_notif = false;
                }
            }

            // 10) DELETE
            else if (ch == kb.key_delete) {
                if (active_window == DIRECTORY_WIN_ACTIVE && state.selected_entry) {
                    char full_path[MAX_PATH_LENGTH];
                    path_join(full_path, state.current_directory, state.selected_entry);
                    char name_copy[MAX_PATH_LENGTH];
                    strncpy(name_copy, state.selected_entry, MAX_PATH_LENGTH - 1);
                    name_copy[MAX_PATH_LENGTH - 1] = '\0';

                    bool should_delete = false;
                    bool delete_result = confirm_delete(name_copy, &should_delete);

                    if (delete_result && should_delete) {
                        delete_item(full_path);
                        reload_directory(&state.files, state.current_directory);
                        resync_selection(&state);

                        show_notification(notifwin, "Deleted: %s", name_copy);
                        should_clear_notif = false;
                    } else {
                        show_notification(notifwin, "Delete cancelled");
                        should_clear_notif = false;
                    }
                }
            }


            // 11) RENAME
            else if (ch == kb.key_rename) {
                if (active_window == DIRECTORY_WIN_ACTIVE && state.selected_entry) {
                    char full_path[MAX_PATH_LENGTH];
                    path_join(full_path, state.current_directory, state.selected_entry);

                    rename_item(notifwin, full_path);

                    // Reload to show changes
                    reload_directory(&state.files, state.current_directory);
                    resync_selection(&state);
                }
            }

            // 12) CREATE NEW 
            else if (ch == kb.key_new) {
                if (active_window == DIRECTORY_WIN_ACTIVE) {
                    create_new_file(notifwin, state.current_directory);
                    reload_directory(&state.files, state.current_directory);
                    resync_selection(&state);
                }
            }

            // 13 CREATE NEW DIR
            else if (ch == kb.key_new_dir) {
                create_new_directory(notifwin, state.current_directory);
                reload_directory(&state.files, state.current_directory);
                resync_selection(&state);
            }
        }

        // Clear notification window only if no new notification was displayed
        if (should_clear_notif) {
            werase(notifwin);
            wrefresh(notifwin);
        }

        // Redraw windows
        draw_directory_window(
                dirwin,
                state.current_directory,
                &state.files,
                &state.dir_window_cas
        );

        draw_preview_window(
                previewwin,
                state.current_directory,
                state.selected_entry,
                state.preview_start_line
        );

        // Highlight the active window
        if (active_window == DIRECTORY_WIN_ACTIVE) {
            wattron(dirwin, A_REVERSE);
            mvwprintw(dirwin, state.dir_window_cas.cursor - state.dir_window_cas.start + 1, 1, "%s", FileAttr_get_name(state.files.el[state.dir_window_cas.cursor]));
            wattroff(dirwin, A_REVERSE);
        } else {
            wattron(previewwin, A_REVERSE);
            mvwprintw(previewwin, 1, 1, "Preview Window Active");
            wattroff(previewwin, A_REVERSE);
        }

        wrefresh(mainwin);
        wrefresh(notifwin);
        // Highlight the active window
        if (active_window == DIRECTORY_WIN_ACTIVE) {
            wattron(dirwin, A_REVERSE);
            mvwprintw(dirwin, state.dir_window_cas.cursor - state.dir_window_cas.start + 1, 1, "%s", FileAttr_get_name(state.files.el[state.dir_window_cas.cursor]));
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
    
    // Free scroll position tracking
    DirScrollPos *pos = dir_scroll_positions;
    while (pos) {
        DirScrollPos *next = pos->next;
        free(pos->path);
        free(pos);
        pos = next;
    }
    dir_scroll_positions = NULL;

    return 0;
}
