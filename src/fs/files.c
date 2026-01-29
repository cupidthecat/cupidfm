// File: files.c
// -----------------------
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdlib.h>                // for malloc, free
#include <stddef.h>                // for NULL
#include <sys/types.h>             // for ino_t
#include <string.h>                // for strdup
#include <dirent.h>                // for DIR, struct dirent, opendir, readdir, closedir
#include <unistd.h>                // for lstat
#include <stdio.h>                 // for snprintf
#include <sys/stat.h>              // for struct stat, lstat, S_ISDIR
#include <time.h>                  // for strftime
#include <ncurses.h>               // for WINDOW, mvwprintw
#include <stdbool.h>               // for bool, true, false
#include <string.h>                // for strcmp
#include <ctype.h>                 // for toupper
#include <limits.h>                // For PATH_MAX
#include <fcntl.h>                 // For O_RDONLY
#include <magic.h>                 // For libmagic
#include <time.h>
#include <sys/ioctl.h>             // For ioctl, TIOCGWINSZ, struct winsize
#include <pthread.h>               // For background directory size worker
#include <errno.h>                 // For permission errors
#include "../cupidarchive/cupidarchive.h"  // For archive reading

#ifdef __linux__
#include <sys/vfs.h>               // statfs
// Prefer linux/magic.h if available; otherwise define what we use.
#if defined(__has_include)
  #if __has_include(<linux/magic.h>)
    #include <linux/magic.h>
  #endif
#endif

#ifndef PROC_SUPER_MAGIC
  #define PROC_SUPER_MAGIC 0x9fa0
#endif
#ifndef SYSFS_MAGIC
  #define SYSFS_MAGIC 0x62656572
#endif
#ifndef TMPFS_MAGIC
  #define TMPFS_MAGIC 0x01021994
#endif
#ifndef DEVPTS_SUPER_MAGIC
  #define DEVPTS_SUPER_MAGIC 0x1cd1
#endif
#ifndef CGROUP2_SUPER_MAGIC
  #define CGROUP2_SUPER_MAGIC 0x63677270
#endif
#endif

// Local includes
#include "main.h"                  // for FileAttr, Vector, Vector_add, Vector_len, Vector_set_len
#include "utils.h"                 // for path_join, is_directory
#include "files.h"                 // for FileAttributes, FileAttr, MAX_PATH_LENGTH
#include "globals.h"
#include "config.h"
#include "console.h"
#include "plugins.h"
#define MIN_INT_SIZE_T(x, y) (((size_t)(x) > (y)) ? (y) : (x))
#define FILES_BANNER_UPDATE_INTERVAL 50000 // 50ms in microseconds

#define CTRL_UP_CODE    0x1001
#define CTRL_DOWN_CODE  0x1002
#define CTRL_LEFT_CODE  0x1003
#define CTRL_RIGHT_CODE 0x1004

// Register common Ctrl+Arrow escape sequences so wgetch() returns our CTRL_*_CODE.
// Call once after initscr()/keypad() (safe to call multiple times).
static void register_ctrl_arrow_keys_once(void) {
#ifdef NCURSES_VERSION
    static bool done = false;
    if (done) return;

    // Make ESC-sequence handling snappier.
    set_escdelay(25);

    // Most terminals (xterm, alacritty, kitty, wezterm, iTerm2, GNOME Terminal, etc.)
    define_key("\x1b[1;5A", CTRL_UP_CODE);
    define_key("\x1b[1;5B", CTRL_DOWN_CODE);
    define_key("\x1b[1;5C", CTRL_RIGHT_CODE);
    define_key("\x1b[1;5D", CTRL_LEFT_CODE);

    // Alternate sequences seen in some tmux/rxvt setups
    define_key("\x1b[5A", CTRL_UP_CODE);
    define_key("\x1b[5B", CTRL_DOWN_CODE);
    define_key("\x1b[5C", CTRL_RIGHT_CODE);
    define_key("\x1b[5D", CTRL_LEFT_CODE);

    done = true;
#endif
}

// Supported MIME types
// Key codes for Ctrl+Arrow keys
// These are used for handling keyboard input in the terminal

const char *supported_mime_types[] = {
    "text/plain",               // Plain text files
    "text/x-c",                 // C source files
    "application/json",         // JSON files
    "application/xml",          // XML files
    "text/x-shellscript",       // Shell scripts
    "text/x-python",            // Python source files (common)
    "text/x-script.python",     // Python source files (alternative)
    "text/x-java-source",       // Java source files
    "text/html",                // HTML files
    "text/css",                 // CSS files
    "text/x-c++src",            // C++ source files
    "application/x-yaml",       // YAML files
    "application/x-sh",         // Shell scripts
    "application/x-perl",       // Perl scripts
    "application/x-php",        // PHP scripts
    "text/x-rustsrc",           // Rust source files
    "text/x-go",                // Go source files
    "text/x-swift",             // Swift source files
    "text/x-kotlin",            // Kotlin source files
    "text/x-makefile",          // Makefile files
    "text/x-script.*",          // Generic scripting files
    "text/javascript",          // JavaScript files
    "application/javascript",   // Alternative JavaScript MIME type
    "application/x-javascript", // Another JavaScript MIME type
    "text/x-javascript",        // Legacy JavaScript MIME type
    "text/x-*",                 // Any text-based x- files
};

typedef enum {
    // Handle Ctrl+Arrow key sequences
    // This section processes the input for Ctrl+Arrow key combinations

    DIR_SIZE_STATUS_PENDING = 0,
    DIR_SIZE_STATUS_READY = 1,
} DirSizeCacheStatus;

typedef struct DirSizeEntry {
    char path[MAX_PATH_LENGTH];
    long size;
    long progress;
    DirSizeCacheStatus status;
    bool job_enqueued;
    struct DirSizeEntry *next;
} DirSizeEntry;

typedef struct DirSizeJob {
    char path[MAX_PATH_LENGTH];
    struct DirSizeJob *next;
} DirSizeJob;

static DirSizeEntry *dir_size_cache_head = NULL;
static DirSizeJob *dir_size_job_head = NULL;
static DirSizeJob *dir_size_job_tail = NULL;
static pthread_mutex_t dir_size_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dir_size_cond = PTHREAD_COND_INITIALIZER;
static bool dir_size_thread_running = false;
static bool dir_size_thread_initialized = false;
static pthread_t dir_size_thread;
static struct timespec dir_size_last_activity = {0};
static bool dir_size_last_activity_initialized = false;
static int g_editor_h_scroll = 0;
static bool g_editor_mouse_dragging = false;

// Enable/disable terminal mouse tracking for drag motion (works in most xterm-like terms).
static void editor_enable_mouse_drag_reporting(void) {
    // 1002 = button-event tracking (reports motion while a button is down)
    // 1006 = SGR extended mode (reliable coords)
    fputs("\x1b[?1002h\x1b[?1006h", stdout);
    fflush(stdout);
}

static void editor_disable_mouse_drag_reporting(void) {
    fputs("\x1b[?1002l\x1b[?1006l", stdout);
    fflush(stdout);
}

static DirSizeEntry* dir_size_find_entry(const char *path);
static void dir_size_enqueue_job_locked(const char *path);
static void dir_size_cache_clear_locked(void);
static void *dir_size_worker(void *arg);
static void ensure_dir_size_worker(void);
static long compute_directory_size_full(const char *dir_path);
static long compute_directory_size_full_progress(const char *dir_path, const char *job_path);
static long dir_size_get_result(const char *dir_path, bool allow_enqueue);
static bool dir_size_user_idle(void);

void dir_size_note_user_activity(void) {
    clock_gettime(CLOCK_MONOTONIC, &dir_size_last_activity);
    dir_size_last_activity_initialized = true;
}

static bool dir_size_user_idle(void) {
    if (!dir_size_last_activity_initialized) {
        return true;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_ns = (now.tv_sec - dir_size_last_activity.tv_sec) * 1000000000L +
                      (now.tv_nsec - dir_size_last_activity.tv_nsec);
    return elapsed_ns >= DIR_SIZE_REQUEST_DELAY_NS;
}

bool dir_size_can_enqueue(void) {
    return dir_size_user_idle();
}

static DirSizeEntry* dir_size_find_entry(const char *path) {
    for (DirSizeEntry *entry = dir_size_cache_head; entry != NULL; entry = entry->next) {
        if (strncmp(entry->path, path, MAX_PATH_LENGTH) == 0) {
            return entry;
        }
    }
    return NULL;
}

static void dir_size_enqueue_job_locked(const char *path) {
    DirSizeJob *job = malloc(sizeof(DirSizeJob));
    if (!job) {
        return;
    }
    strncpy(job->path, path, MAX_PATH_LENGTH - 1);
    job->path[MAX_PATH_LENGTH - 1] = '\0';
    job->next = NULL;

    DirSizeEntry *entry = dir_size_find_entry(path);
    if (entry) {
        entry->job_enqueued = true;
    }

    if (dir_size_job_tail) {
        dir_size_job_tail->next = job;
    } else {
        dir_size_job_head = job;
    }
    dir_size_job_tail = job;
    pthread_cond_signal(&dir_size_cond);
}

static void dir_size_cache_clear_locked(void) {
    DirSizeEntry *entry = dir_size_cache_head;
    while (entry) {
        DirSizeEntry *next = entry->next;
        free(entry);
        entry = next;
    }
    dir_size_cache_head = NULL;

    DirSizeJob *job = dir_size_job_head;
    while (job) {
        DirSizeJob *next = job->next;
        free(job);
        job = next;
    }
    dir_size_job_head = NULL;
    dir_size_job_tail = NULL;
}

static long compute_directory_size_full(const char *dir_path) {
    if (strncmp(dir_path, "/proc", 5) == 0 ||
        strncmp(dir_path, "/sys", 4) == 0 ||
        strncmp(dir_path, "/dev", 4) == 0 ||
        strncmp(dir_path, "/run", 4) == 0) {
        return DIR_SIZE_VIRTUAL_FS;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
        if (errno == EACCES) {
            return DIR_SIZE_PERMISSION_DENIED;
        }
        return -1;
    }

    long total_size = 0;
    const long MAX_SIZE_THRESHOLD = 1000L * 1024 * 1024 * 1024 * 1024; // 1000 TiB
    struct dirent *entry;
    struct stat statbuf;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char path[MAX_PATH_LENGTH];
        if (strlen(dir_path) + strlen(entry->d_name) + 1 < sizeof(path)) {
            snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        } else {
            fprintf(stderr, "Path length exceeds buffer size for %s/%s\n", dir_path, entry->d_name);
            continue;
        }

        if (lstat(path, &statbuf) == -1) {
            continue;
        }

        if (S_ISDIR(statbuf.st_mode)) {
            long dir_size = compute_directory_size_full(path);
            if (dir_size == DIR_SIZE_VIRTUAL_FS || dir_size == DIR_SIZE_PERMISSION_DENIED) {
                closedir(dir);
                return dir_size;
            } else if (dir_size == DIR_SIZE_TOO_LARGE) {
                closedir(dir);
                return DIR_SIZE_TOO_LARGE;
            } else if (dir_size >= 0) {
                total_size += dir_size;
            }
            // If dir_size == -1 (unreadable), skip but keep going
        } else {
            total_size += statbuf.st_size;
        }

        if (total_size > MAX_SIZE_THRESHOLD) {
            closedir(dir);
            return DIR_SIZE_TOO_LARGE;
        }
    }

    closedir(dir);
    return total_size;
}

static void *dir_size_worker(void *arg) {
    (void)arg;
    pthread_mutex_lock(&dir_size_mutex);
    while (dir_size_thread_running) {
        while (dir_size_job_head == NULL && dir_size_thread_running) {
            pthread_cond_wait(&dir_size_cond, &dir_size_mutex);
        }
        if (!dir_size_thread_running) {
            break;
        }

        DirSizeJob *job = dir_size_job_head;
        dir_size_job_head = job->next;
        if (!dir_size_job_head) {
            dir_size_job_tail = NULL;
        }
        char path[MAX_PATH_LENGTH];
        strncpy(path, job->path, MAX_PATH_LENGTH - 1);
        path[MAX_PATH_LENGTH - 1] = '\0';
        free(job);

        pthread_mutex_unlock(&dir_size_mutex);
        // Compute size with periodic progress updates for UI.
        long size = compute_directory_size_full_progress(path, path);
        pthread_mutex_lock(&dir_size_mutex);

        DirSizeEntry *entry = dir_size_find_entry(path);
        if (entry) {
            entry->size = size;
            if (size >= 0) entry->progress = size;
            entry->status = DIR_SIZE_STATUS_READY;
            entry->job_enqueued = false;
        }
    }
    pthread_mutex_unlock(&dir_size_mutex);
    return NULL;
}

static void ensure_dir_size_worker(void) {
    if (dir_size_thread_initialized) {
        return;
    }
    dir_size_thread_running = true;
    if (pthread_create(&dir_size_thread, NULL, dir_size_worker, NULL) == 0) {
        dir_size_thread_initialized = true;
    } else {
        dir_size_thread_running = false;
    }
}

size_t num_supported_mime_types = sizeof(supported_mime_types) / sizeof(supported_mime_types[0]);
// FileAttributes structure
struct FileAttributes {
    char *name;  //
    ino_t inode; // Change from int inode;
    bool is_dir;
};
// TextBuffer structure
typedef struct {
    char **lines;      // Dynamic array of strings
    int num_lines;     // Current number of lines
    int capacity;      // Total capacity of the array
} TextBuffer;

// Active editor buffer (only valid while editor is open)
static TextBuffer *g_editor_buffer = NULL;
static char *g_editor_clipboard = NULL;
static bool g_sel_active = false;
static int g_sel_anchor_line = 0;
static int g_sel_anchor_col = 0;
static int g_sel_end_line = 0;
static int g_sel_end_col = 0;
static int g_editor_cursor_line = 0;
static int g_editor_cursor_col = 0;
static bool g_editor_dirty = false;

// -----------------------
// Editor Undo/Redo (snapshot-based)
// -----------------------
#define EDITOR_UNDO_MAX 256

typedef struct EditorSnapshot {
    char **lines;
    int   num_lines;
    int   capacity;

    int   cursor_line;
    int   cursor_col;
    int   start_line;

    bool  sel_active;
    int   sel_anchor_line;
    int   sel_anchor_col;
    int   sel_end_line;
    int   sel_end_col;
} EditorSnapshot;

typedef struct UndoManager {
    EditorSnapshot undo[EDITOR_UNDO_MAX];
    int undo_len;

    EditorSnapshot redo[EDITOR_UNDO_MAX];
    int redo_len;
} UndoManager;

static void editor_snapshot_free(EditorSnapshot *s) {
    if (!s || !s->lines) return;
    for (int i = 0; i < s->num_lines; i++) free(s->lines[i]);
    free(s->lines);
    *s = (EditorSnapshot){0};
}

static void editor_stack_clear(EditorSnapshot *stk, int *len) {
    if (!stk || !len) return;
    for (int i = 0; i < *len; i++) editor_snapshot_free(&stk[i]);
    *len = 0;
}

static EditorSnapshot editor_snapshot_make(const TextBuffer *buf,
                                          int cursor_line, int cursor_col,
                                          int start_line)
{
    EditorSnapshot s = {0};
    if (!buf || !buf->lines || buf->num_lines < 0) return s;

    s.num_lines = buf->num_lines;
    s.capacity  = (buf->num_lines > 0) ? buf->num_lines : 1;

    s.lines = (char**)malloc(sizeof(char*) * (size_t)s.capacity);
    if (!s.lines) return (EditorSnapshot){0};

    for (int i = 0; i < s.num_lines; i++) {
        const char *src = buf->lines[i] ? buf->lines[i] : "";
        s.lines[i] = strdup(src);
        if (!s.lines[i]) {
            for (int j = 0; j < i; j++) free(s.lines[j]);
            free(s.lines);
            return (EditorSnapshot){0};
        }
    }

    s.cursor_line = cursor_line;
    s.cursor_col  = cursor_col;
    s.start_line  = start_line;

    s.sel_active       = g_sel_active;
    s.sel_anchor_line  = g_sel_anchor_line;
    s.sel_anchor_col   = g_sel_anchor_col;
    s.sel_end_line     = g_sel_end_line;
    s.sel_end_col      = g_sel_end_col;

    return s;
}

static void editor_stack_push(EditorSnapshot *stk, int *len, EditorSnapshot *snap) {
    if (!stk || !len || !snap || !snap->lines) return;

    if (*len >= EDITOR_UNDO_MAX) {
        editor_snapshot_free(&stk[0]);
        memmove(&stk[0], &stk[1], sizeof(EditorSnapshot) * (EDITOR_UNDO_MAX - 1));
        *len = EDITOR_UNDO_MAX - 1;
    }

    stk[*len] = *snap;
    (*len)++;
    *snap = (EditorSnapshot){0};
}

static bool editor_stack_pop(EditorSnapshot *stk, int *len, EditorSnapshot *out) {
    if (!stk || !len || !out) return false;
    if (*len <= 0) return false;
    *out = stk[*len - 1];
    stk[*len - 1] = (EditorSnapshot){0};
    (*len)--;
    return true;
}

static void editor_apply_snapshot(TextBuffer *buf, EditorSnapshot *snap,
                                  int *cursor_line, int *cursor_col, int *start_line)
{
    if (!buf || !snap || !snap->lines) return;

    if (buf->lines) {
        for (int i = 0; i < buf->num_lines; i++) free(buf->lines[i]);
        free(buf->lines);
    }

    buf->lines    = snap->lines;
    buf->num_lines = snap->num_lines;
    buf->capacity  = (snap->capacity > 0) ? snap->capacity : ((snap->num_lines > 0) ? snap->num_lines : 1);

    g_sel_active      = snap->sel_active;
    g_sel_anchor_line = snap->sel_anchor_line;
    g_sel_anchor_col  = snap->sel_anchor_col;
    g_sel_end_line    = snap->sel_end_line;
    g_sel_end_col     = snap->sel_end_col;

    if (cursor_line) *cursor_line = snap->cursor_line;
    if (cursor_col)  *cursor_col  = snap->cursor_col;
    if (start_line)  *start_line  = snap->start_line;

    if (buf->num_lines <= 0) {
        buf->num_lines = 1;
        buf->capacity  = 1;
        buf->lines = (char**)malloc(sizeof(char*));
        buf->lines[0] = strdup("");
        if (cursor_line) *cursor_line = 0;
        if (cursor_col)  *cursor_col  = 0;
        if (start_line)  *start_line  = 0;
        g_sel_active = false;
    } else {
        int cl = cursor_line ? *cursor_line : 0;
        if (cl < 0) cl = 0;
        if (cl >= buf->num_lines) cl = buf->num_lines - 1;
        if (cursor_line) *cursor_line = cl;

        const char *ln = buf->lines[cl] ? buf->lines[cl] : "";
        int ll = (int)strlen(ln);
        int cc = cursor_col ? *cursor_col : 0;
        if (cc < 0) cc = 0;
        if (cc > ll) cc = ll;
        if (cursor_col) *cursor_col = cc;

        if (start_line) {
            int sl = *start_line;
            if (sl < 0) sl = 0;
            if (sl >= buf->num_lines) sl = buf->num_lines - 1;
            *start_line = sl;
        }
    }

    snap->lines = NULL;
    *snap = (EditorSnapshot){0};
}

static void editor_undo_record(UndoManager *um, TextBuffer *buf,
                              int cursor_line, int cursor_col, int start_line)
{
    if (!um || !buf) return;

    EditorSnapshot s = editor_snapshot_make(buf, cursor_line, cursor_col, start_line);
    if (!s.lines) return;

    editor_stack_push(um->undo, &um->undo_len, &s);
    editor_stack_clear(um->redo, &um->redo_len);
}

static bool editor_do_undo(UndoManager *um, TextBuffer *buf,
                           int *cursor_line, int *cursor_col, int *start_line)
{
    if (!um || !buf) return false;

    EditorSnapshot prev;
    if (!editor_stack_pop(um->undo, &um->undo_len, &prev)) return false;

    EditorSnapshot cur = editor_snapshot_make(buf,
                                             cursor_line ? *cursor_line : 0,
                                             cursor_col  ? *cursor_col  : 0,
                                             start_line  ? *start_line  : 0);
    if (cur.lines) editor_stack_push(um->redo, &um->redo_len, &cur);

    editor_apply_snapshot(buf, &prev, cursor_line, cursor_col, start_line);
    return true;
}

static bool editor_do_redo(UndoManager *um, TextBuffer *buf,
                           int *cursor_line, int *cursor_col, int *start_line)
{
    if (!um || !buf) return false;

    EditorSnapshot next;
    if (!editor_stack_pop(um->redo, &um->redo_len, &next)) return false;

    EditorSnapshot cur = editor_snapshot_make(buf,
                                             cursor_line ? *cursor_line : 0,
                                             cursor_col  ? *cursor_col  : 0,
                                             start_line  ? *start_line  : 0);
    if (cur.lines) editor_stack_push(um->undo, &um->undo_len, &cur);

    editor_apply_snapshot(buf, &next, cursor_line, cursor_col, start_line);
    return true;
}

static void ensure_text_buffer_capacity(TextBuffer *buffer, int needed) {
    if (!buffer) return;
    if (needed <= buffer->capacity) return;
    int new_cap = buffer->capacity;
    while (new_cap < needed) new_cap *= 2;
    char **tmp = realloc(buffer->lines, sizeof(char *) * new_cap);
    if (!tmp) return;
    buffer->lines = tmp;
    buffer->capacity = new_cap;
}

static void normalize_selection(int *s_line, int *s_col, int *e_line, int *e_col) {
    if (*s_line > *e_line || (*s_line == *e_line && *s_col > *e_col)) {
        int tl = *s_line; int tc = *s_col;
        *s_line = *e_line; *s_col = *e_col;
        *e_line = tl; *e_col = tc;
    }
    if (*s_line < 0) *s_line = 0;
    if (*e_line < 0) *e_line = 0;
}

static bool selection_is_empty(void) {
    if (!g_sel_active) return true;
    int s_line = g_sel_anchor_line;
    int s_col = g_sel_anchor_col;
    int e_line = g_sel_end_line;
    int e_col = g_sel_end_col;
    normalize_selection(&s_line, &s_col, &e_line, &e_col);
    return (s_line == e_line && s_col == e_col);
}

static bool editor_mouse_to_buffer_pos(WINDOW *window, TextBuffer *buffer, int start_line,
                                       int mouse_y, int mouse_x, int *out_line, int *out_col) {
    if (!window || !buffer || !buffer->lines || !out_line || !out_col) return false;

    int win_y = 0, win_x = 0;
    int max_y = 0, max_x = 0;
    getbegyx(window, win_y, win_x);
    getmaxyx(window, max_y, max_x);

    int rel_y = mouse_y - win_y;
    int rel_x = mouse_x - win_x;

    if (rel_y < 1 || rel_y >= max_y - 1) return false;
    if (rel_x < 1 || rel_x >= max_x - 1) return false;

    int label_width = snprintf(NULL, 0, "%d", buffer->num_lines) + 1;
    int content_width = max_x - label_width - 4;
    if (content_width < 1) content_width = 1;
    int content_start = label_width + 3;

    int line_index = start_line + (rel_y - 1);
    if (line_index < 0 || line_index >= buffer->num_lines) return false;

    const char *line = buffer->lines[line_index] ? buffer->lines[line_index] : "";
    int line_length = (int)strlen(line);

    int col = 0;
    if (rel_x <= content_start) {
        col = 0;
    } else {
        int vis_col = rel_x - content_start;
        col = g_editor_h_scroll + vis_col;
        if (col < 0) col = 0;
        if (col > line_length) col = line_length;
    }

    *out_line = line_index;
    *out_col = col;
    return true;
}

static char *copy_selection_or_line(TextBuffer *buffer, int cursor_line, int cursor_col) {
    if (!buffer || !buffer->lines) return NULL;

    int s_line = g_sel_anchor_line;
    int s_col = g_sel_anchor_col;
    int e_line = g_sel_end_line;
    int e_col = g_sel_end_col;

    if (g_sel_active && !selection_is_empty()) {
        normalize_selection(&s_line, &s_col, &e_line, &e_col);
        if (s_line >= buffer->num_lines) return NULL;
        if (e_line >= buffer->num_lines) e_line = buffer->num_lines - 1;

        size_t total = 0;
        for (int i = s_line; i <= e_line; i++) {
            const char *line = buffer->lines[i] ? buffer->lines[i] : "";
            int line_len = (int)strlen(line);
            int start = (i == s_line) ? MIN(s_col, line_len) : 0;
            int end = (i == e_line) ? MIN(e_col, line_len) : line_len;
            if (end < start) end = start;
            total += (size_t)(end - start);
            if (i < e_line) total += 1; // newline
        }

        char *out = (char *)malloc(total + 1);
        if (!out) return NULL;
        size_t pos = 0;
        for (int i = s_line; i <= e_line; i++) {
            const char *line = buffer->lines[i] ? buffer->lines[i] : "";
            int line_len = (int)strlen(line);
            int start = (i == s_line) ? MIN(s_col, line_len) : 0;
            int end = (i == e_line) ? MIN(e_col, line_len) : line_len;
            if (end < start) end = start;
            if (end > start) {
                memcpy(out + pos, line + start, (size_t)(end - start));
                pos += (size_t)(end - start);
            }
            if (i < e_line) out[pos++] = '\n';
        }
        out[pos] = '\0';
        return out;
    }

    if (cursor_line < 0 || cursor_line >= buffer->num_lines) return NULL;
    const char *line = buffer->lines[cursor_line] ? buffer->lines[cursor_line] : "";
    size_t len = strlen(line);
    char *out = (char *)malloc(len + 2);
    if (!out) return NULL;
    memcpy(out, line, len);
    out[len] = '\n';
    out[len + 1] = '\0';
    (void)cursor_col;
    return out;
}

static void delete_selection(TextBuffer *buffer) {
    if (!buffer || !buffer->lines || !g_sel_active || selection_is_empty()) return;

    int s_line = g_sel_anchor_line;
    int s_col = g_sel_anchor_col;
    int e_line = g_sel_end_line;
    int e_col = g_sel_end_col;
    normalize_selection(&s_line, &s_col, &e_line, &e_col);
    if (s_line >= buffer->num_lines) return;
    if (e_line >= buffer->num_lines) e_line = buffer->num_lines - 1;

    if (s_line == e_line) {
        char *line = buffer->lines[s_line];
        if (!line) return;
        int line_len = (int)strlen(line);
        int start = MIN(s_col, line_len);
        int end = MIN(e_col, line_len);
        if (end < start) end = start;
        memmove(line + start, line + end, (size_t)(line_len - end + 1));
        return;
    }

    // Multi-line delete: keep prefix of start line and suffix of end line
    char *start_line = buffer->lines[s_line];
    char *end_line = buffer->lines[e_line];
    if (!start_line || !end_line) return;

    int start_len = (int)strlen(start_line);
    int end_len = (int)strlen(end_line);
    int start_keep = MIN(s_col, start_len);
    int end_keep_from = MIN(e_col, end_len);

    size_t new_len = (size_t)start_keep + (size_t)(end_len - end_keep_from) + 1;
    char *merged = (char *)malloc(new_len);
    if (!merged) return;
    memcpy(merged, start_line, (size_t)start_keep);
    memcpy(merged + start_keep, end_line + end_keep_from, (size_t)(end_len - end_keep_from));
    merged[new_len - 1] = '\0';

    free(buffer->lines[s_line]);
    buffer->lines[s_line] = merged;

    // Free middle lines
    for (int i = s_line + 1; i <= e_line; i++) {
        free(buffer->lines[i]);
    }

    // Shift remaining lines up
    int remove_count = e_line - s_line;
    for (int i = s_line + 1; i + remove_count < buffer->num_lines; i++) {
        buffer->lines[i] = buffer->lines[i + remove_count];
    }
    buffer->num_lines -= remove_count;
}

// -----------------------
// Selection transforms
// -----------------------
static void editor_selection_to_uppercase(TextBuffer *buffer) {
    if (!buffer || !buffer->lines) return;
    if (!g_sel_active || selection_is_empty()) return;

    int s_line = g_sel_anchor_line;
    int s_col  = g_sel_anchor_col;
    int e_line = g_sel_end_line;
    int e_col  = g_sel_end_col;
    normalize_selection(&s_line, &s_col, &e_line, &e_col);

    if (buffer->num_lines <= 0) return;
    if (s_line < 0) s_line = 0;
    if (e_line < 0) e_line = 0;
    if (s_line >= buffer->num_lines) return;
    if (e_line >= buffer->num_lines) e_line = buffer->num_lines - 1;

    for (int i = s_line; i <= e_line; i++) {
        char *line = buffer->lines[i];
        if (!line) continue;

        int len = (int)strlen(line);
        int start = (i == s_line) ? MIN(s_col, len) : 0;
        int end   = (i == e_line) ? MIN(e_col, len) : len; // end is EXCLUSIVE (matches your copy/delete logic)
        if (end < start) end = start;

        for (int j = start; j < end; j++) {
            unsigned char c = (unsigned char)line[j];
            line[j] = (char)toupper((int)c);
        }
    }
}

void editor_apply_uppercase_to_selection(void) {
    if (!g_editor_buffer || selection_is_empty()) {
        return;
    }
    editor_selection_to_uppercase(g_editor_buffer);
    g_editor_dirty = true;
}

static void insert_text_at_cursor(TextBuffer *buffer, int *cursor_line, int *cursor_col, const char *text) {
    if (!buffer || !buffer->lines || !text || !cursor_line || !cursor_col) return;

    const char *line = buffer->lines[*cursor_line] ? buffer->lines[*cursor_line] : "";
    int line_len = (int)strlen(line);
    int col = MIN(*cursor_col, line_len);

    // Count lines in text
    int parts = 1;
    for (const char *p = text; *p; p++) if (*p == '\n') parts++;

    if (parts == 1) {
        int insert_len = (int)strlen(text);
        char *new_line = realloc(buffer->lines[*cursor_line], (size_t)line_len + insert_len + 1);
        if (!new_line) return;
        memmove(new_line + col + insert_len, new_line + col, (size_t)(line_len - col + 1));
        memcpy(new_line + col, text, (size_t)insert_len);
        buffer->lines[*cursor_line] = new_line;
        *cursor_col = col + insert_len;
        return;
    }

    ensure_text_buffer_capacity(buffer, buffer->num_lines + parts);

    // Split text into lines
    const char *p = text;
    int part_index = 0;

    char *prefix = strdup(line);
    if (!prefix) return;
    prefix[col] = '\0';
    char *suffix = strdup(line + col);
    if (!suffix) {
        free(prefix);
        return;
    }

    // Insert new lines (make room)
    for (int i = buffer->num_lines - 1; i > *cursor_line; i--) {
        buffer->lines[i + parts - 1] = buffer->lines[i];
    }

    // First line
    const char *start = p;
    while (*p && *p != '\n') p++;
    size_t first_len = (size_t)(p - start);
    char *first = (char *)malloc(strlen(prefix) + first_len + 1);
    if (!first) {
        free(prefix);
        free(suffix);
        return;
    }
    memcpy(first, prefix, strlen(prefix));
    memcpy(first + strlen(prefix), start, first_len);
    first[strlen(prefix) + first_len] = '\0';
    buffer->lines[*cursor_line] = first;

    if (*p == '\n') p++;
    part_index = 1;

    // Middle lines
    while (part_index < parts - 1) {
        start = p;
        while (*p && *p != '\n') p++;
        size_t len = (size_t)(p - start);
        char *mid = (char *)malloc(len + 1);
        if (!mid) break;
        memcpy(mid, start, len);
        mid[len] = '\0';
        buffer->lines[*cursor_line + part_index] = mid;
        if (*p == '\n') p++;
        part_index++;
    }

    // Last line
    const char *last_start = p;
    size_t last_len = strlen(last_start);
    char *last = (char *)malloc(last_len + strlen(suffix) + 1);
    if (!last) {
        free(prefix);
        free(suffix);
        return;
    }
    memcpy(last, last_start, last_len);
    memcpy(last + last_len, suffix, strlen(suffix));
    last[last_len + strlen(suffix)] = '\0';
    buffer->lines[*cursor_line + parts - 1] = last;

    buffer->num_lines += (parts - 1);
    *cursor_line = *cursor_line + parts - 1;
    *cursor_col = (int)last_len;

    free(prefix);
    free(suffix);
}

static bool confirm_discard_unsaved(WINDOW *parent_win) {
    if (!parent_win) return false;
    int max_y, max_x;
    getmaxyx(parent_win, max_y, max_x);

    int popup_h = 5;
    int popup_w = 50;
    int starty = (max_y - popup_h) / 2;
    int startx = (max_x - popup_w) / 2;

    WINDOW *popup = newwin(popup_h, popup_w, starty, startx);
    if (!popup) return false;
    keypad(popup, TRUE);
    box(popup, 0, 0);
    mvwprintw(popup, 1, 2, "Unsaved changes.");
    mvwprintw(popup, 2, 2, "Discard and exit? (Y/N)");
    wrefresh(popup);

    int ch;
    bool discard = false;
    while (true) {
        ch = wgetch(popup);
        if (ch == 'y' || ch == 'Y') {
            discard = true;
            break;
        }
        if (ch == 'n' || ch == 'N' || ch == 27) {
            discard = false;
            break;
        }
    }

    werase(popup);
    wrefresh(popup);
    delwin(popup);
    touchwin(parent_win);
    wrefresh(parent_win);
    return discard;
}

char *editor_get_content_copy(void) {
    if (!is_editing || !g_editor_buffer || !g_editor_buffer->lines) return NULL;

    size_t total = 0;
    for (int i = 0; i < g_editor_buffer->num_lines; i++) {
        const char *line = g_editor_buffer->lines[i] ? g_editor_buffer->lines[i] : "";
        total += strlen(line);
        if (i < g_editor_buffer->num_lines - 1) total += 1; // newline
    }

    char *out = (char *)malloc(total + 1);
    if (!out) return NULL;
    size_t pos = 0;
    for (int i = 0; i < g_editor_buffer->num_lines; i++) {
        const char *line = g_editor_buffer->lines[i] ? g_editor_buffer->lines[i] : "";
        size_t len = strlen(line);
        if (len) {
            memcpy(out + pos, line, len);
            pos += len;
        }
        if (i < g_editor_buffer->num_lines - 1) {
            out[pos++] = '\n';
        }
    }
    out[pos] = '\0';
    return out;
}

char *editor_get_line_copy(int line_num) {
    if (!is_editing || !g_editor_buffer || !g_editor_buffer->lines) return NULL;
    if (line_num <= 0 || line_num > g_editor_buffer->num_lines) return NULL;
    const char *line = g_editor_buffer->lines[line_num - 1];
    if (!line) line = "";
    size_t len = strlen(line);
    char *out = (char *)malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, line, len);
    out[len] = '\0';
    return out;
}

int editor_get_line_count(void) {
    if (!is_editing || !g_editor_buffer) return 0;
    return g_editor_buffer->num_lines;
}

bool editor_get_cursor(int *line, int *col) {
    if (!is_editing) return false;
    if (line) *line = g_editor_cursor_line + 1;  // Convert to 1-indexed
    if (col) *col = g_editor_cursor_col + 1;     // Convert to 1-indexed
    return true;
}

bool editor_get_selection(int *start_line, int *start_col, int *end_line, int *end_col) {
    if (!is_editing || !g_sel_active) return false;
    
    // Normalize selection so start is always before end
    int sl = g_sel_anchor_line;
    int sc = g_sel_anchor_col;
    int el = g_sel_end_line;
    int ec = g_sel_end_col;
    
    // Swap if needed to ensure start <= end
    if (sl > el || (sl == el && sc > ec)) {
        int tmp_l = sl; sl = el; el = tmp_l;
        int tmp_c = sc; sc = ec; ec = tmp_c;
    }
    
    if (start_line) *start_line = sl + 1;  // Convert to 1-indexed
    if (start_col) *start_col = sc + 1;    // Convert to 1-indexed
    if (end_line) *end_line = el + 1;      // Convert to 1-indexed
    if (end_col) *end_col = ec + 1;        // Convert to 1-indexed
    return true;
}

bool editor_insert_text(const char *text) {
    if (!is_editing || !g_editor_buffer || !text) return false;
    
    int line = g_editor_cursor_line;
    int col = g_editor_cursor_col;
    
    if (line < 0 || line >= g_editor_buffer->num_lines) return false;
    
    char *curr_line = g_editor_buffer->lines[line];
    if (!curr_line) curr_line = "";
    
    // Handle newlines in the text
    const char *p = text;
    const char *line_start = text;
    
    while (*p) {
        if (*p == '\n') {
            // Insert text up to newline
            size_t insert_len = p - line_start;
            if (insert_len > 0 || p == line_start) {
                int curr_len = strlen(curr_line);
                char *new_line = malloc(curr_len + insert_len + 1);
                if (!new_line) return false;
                
                memcpy(new_line, curr_line, col);
                memcpy(new_line + col, line_start, insert_len);
                memcpy(new_line + col + insert_len, curr_line + col, curr_len - col + 1);
                
                free(g_editor_buffer->lines[line]);
                g_editor_buffer->lines[line] = new_line;
                curr_line = new_line;
                col += insert_len;
            }
            
            // Split line at cursor
            char *rest = strdup(curr_line + col);
            if (!rest) return false;
            curr_line[col] = '\0';
            
            // Insert new line
            if (g_editor_buffer->num_lines >= g_editor_buffer->capacity) {
                g_editor_buffer->capacity *= 2;
                char **tmp = realloc(g_editor_buffer->lines, sizeof(char*) * g_editor_buffer->capacity);
                if (!tmp) {
                    free(rest);
                    return false;
                }
                g_editor_buffer->lines = tmp;
            }
            
            memmove(&g_editor_buffer->lines[line + 2], &g_editor_buffer->lines[line + 1],
                   (g_editor_buffer->num_lines - line - 1) * sizeof(char*));
            g_editor_buffer->lines[line + 1] = rest;
            g_editor_buffer->num_lines++;
            
            line++;
            col = 0;
            curr_line = rest;
            line_start = p + 1;
        }
        p++;
    }
    
    // Insert remaining text
    size_t insert_len = p - line_start;
    if (insert_len > 0) {
        int curr_len = strlen(curr_line);
        char *new_line = malloc(curr_len + insert_len + 1);
        if (!new_line) return false;
        
        memcpy(new_line, curr_line, col);
        memcpy(new_line + col, line_start, insert_len);
        memcpy(new_line + col + insert_len, curr_line + col, curr_len - col + 1);
        
        free(g_editor_buffer->lines[line]);
        g_editor_buffer->lines[line] = new_line;
        col += insert_len;
    }
    
    // Update cursor
    g_editor_cursor_line = line;
    g_editor_cursor_col = col;
    g_editor_dirty = true;
    
    return true;
}

bool editor_replace_text(int start_line, int start_col, int end_line, int end_col, const char *text) {
    if (!is_editing || !g_editor_buffer) return false;
    
    // Convert to 0-indexed
    start_line--;
    start_col--;
    end_line--;
    end_col--;
    
    if (start_line < 0 || start_line >= g_editor_buffer->num_lines) return false;
    if (end_line < 0 || end_line >= g_editor_buffer->num_lines) return false;
    if (start_line > end_line || (start_line == end_line && start_col > end_col)) return false;
    
    // Delete the range
    if (start_line == end_line) {
        // Same line - delete portion
        char *line = g_editor_buffer->lines[start_line];
        if (!line) return false;
        int len = strlen(line);
        if (start_col > len || end_col > len) return false;
        
        char *new_line = malloc(len - (end_col - start_col) + 1);
        if (!new_line) return false;
        
        memcpy(new_line, line, start_col);
        strcpy(new_line + start_col, line + end_col);
        
        free(g_editor_buffer->lines[start_line]);
        g_editor_buffer->lines[start_line] = new_line;
    } else {
        // Multiple lines - merge first and last, delete middle
        char *first = g_editor_buffer->lines[start_line];
        char *last = g_editor_buffer->lines[end_line];
        
        char *merged = malloc(start_col + strlen(last + end_col) + 1);
        if (!merged) return false;
        
        memcpy(merged, first, start_col);
        strcpy(merged + start_col, last + end_col);
        
        // Free deleted lines
        for (int i = start_line; i <= end_line; i++) {
            free(g_editor_buffer->lines[i]);
        }
        
        // Shift remaining lines
        g_editor_buffer->lines[start_line] = merged;
        int lines_deleted = end_line - start_line;
        memmove(&g_editor_buffer->lines[start_line + 1], &g_editor_buffer->lines[end_line + 1],
               (g_editor_buffer->num_lines - end_line - 1) * sizeof(char*));
        g_editor_buffer->num_lines -= lines_deleted;
    }
    
    // Set cursor to start of deleted range
    g_editor_cursor_line = start_line;
    g_editor_cursor_col = start_col;
    
    // Insert new text
    if (text && *text) {
        if (!editor_insert_text(text)) return false;
    }
    
    g_editor_dirty = true;
    return true;
}
/**
 * Function to initialize a TextBuffer
 *
 * @param buffer the TextBuffer to initialize
 */
void init_text_buffer(TextBuffer *buffer) {
    buffer->capacity = 100; // Initial capacity
    buffer->num_lines = 0;
    buffer->lines = malloc(sizeof(char*) * buffer->capacity);
}
/**
 * Function to free the memory allocated for a TextBuffer
 *
 * @param buffer the TextBuffer to free
 */
const char *FileAttr_get_name(FileAttr fa) {
    if (fa != NULL) {
        return fa->name;
    } else {
        // Handle the case where fa is NULL
        return "Unknown";
    }
}
/**
 * Function to check if a file type is supported for preview
 *
 * @param filename the name of the file
 * @return true if the file type is supported, false otherwise
 */
bool FileAttr_is_dir(FileAttr fa) {
    return fa != NULL && fa->is_dir;
}
/**
 * Function to format a file size in a human-readable format
 *
 * @param buffer the buffer to store the formatted file size
 * @param size the size of the file
 * @return the formatted file size
 */
char* format_file_size(char *buffer, size_t size) {
    // iB for multiples of 1024, B for multiples of 1000
    // so, KiB = 1024, KB = 1000
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    int i = 0;
    double fileSize = (double)size;
    while (fileSize >= 1024 && i < 4) {
        fileSize /= 1024;
        i++;
    }
    sprintf(buffer, "%.2f %s", fileSize, units[i]);
    return buffer;
}
/**
 * Function to create a new FileAttr
 *
 * @param name the name of the file
 * @param is_dir true if the file is a directory, false otherwise
 * @param inode the inode number of the file
 * @return a new FileAttr
 */
FileAttr mk_attr(const char *name, bool is_dir, ino_t inode) {
    FileAttr fa = malloc(sizeof(struct FileAttributes));

    if (fa != NULL) {
        fa->name = strdup(name);

        if (fa->name == NULL) {
            // Handle memory allocation failure for the name
            free(fa);
            return NULL;
        }

        fa->inode = inode;
        fa->is_dir = is_dir;
        return fa;
    } else {
        // Handle memory allocation failure for the FileAttr
        return NULL;
    }
}
// Function to free the allocated memory for a FileAttr
void free_attr(FileAttr fa) {
    if (fa != NULL) {
        free(fa->name);  // Free the allocated memory for the name
        free(fa);
    }
}
/**
 * Count total files in a directory (excluding . and ..)
 *
 * @param name the name of the directory
 * @return the number of files in the directory
 */
size_t count_directory_files(const char *name) {
    DIR *dir = opendir(name);
    size_t count = 0;
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                count++;
            }
        }
        closedir(dir);
    }
    return count;
}

/**
 * Function to append files in a directory to a Vector (lazy loading version)
 * Loads up to max_files, starting from files_loaded offset
 *
 * @param v the Vector to append the files to
 * @param name the name of the directory
 * @param max_files maximum number of files to load in this batch
 * @param files_loaded pointer to track how many files have been loaded (in/out)
 * @return number of files actually loaded in this batch
 */
void append_files_to_vec_lazy(Vector *v, const char *name, size_t max_files, size_t *files_loaded) {
    DIR *dir = opendir(name);
    size_t loaded_this_batch = 0;
    size_t skipped = 0;
    
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL && loaded_this_batch < max_files) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                // Skip files we've already loaded
                if (skipped < *files_loaded) {
                    skipped++;
                    continue;
                }
                
                // Optimize: Use d_type to avoid expensive stat calls when possible
                bool is_dir = false;
                #ifdef DT_DIR
                if (entry->d_type == DT_DIR) {
                    is_dir = true;
                } else if (entry->d_type == DT_UNKNOWN) {
                    // d_type unavailable, fall back to stat
                    is_dir = is_directory(name, entry->d_name);
                }
                // else: d_type indicates regular file or other non-directory
                #else
                // No d_type support, use stat
                is_dir = is_directory(name, entry->d_name);
                #endif

                FileAttr file_attr = mk_attr(entry->d_name, is_dir, entry->d_ino);

                if (file_attr != NULL) {  // Only add if not NULL
                    Vector_add(v, 1);
                    v->el[Vector_len(*v)] = file_attr;
                    Vector_set_len(v, Vector_len(*v) + 1);
                    loaded_this_batch++;
                }
            }
        }
        closedir(dir);
    }
    *files_loaded += loaded_this_batch;
}

/**
 * Function to append files in a directory to a Vector
 *
 * @param v the Vector to append the files to
 * @param name the name of the directory
 */
void append_files_to_vec(Vector *v, const char *name) {
    DIR *dir = opendir(name);
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                // Optimize: Use d_type to avoid expensive stat calls when possible
                bool is_dir = false;
                #ifdef DT_DIR
                if (entry->d_type == DT_DIR) {
                    is_dir = true;
                } else if (entry->d_type == DT_UNKNOWN) {
                    // d_type unavailable, fall back to stat
                    is_dir = is_directory(name, entry->d_name);
                }
                // else: d_type indicates regular file or other non-directory
                #else
                // No d_type support, use stat
                is_dir = is_directory(name, entry->d_name);
                #endif

                FileAttr file_attr = mk_attr(entry->d_name, is_dir, entry->d_ino);

                if (file_attr != NULL) {  // Only add if not NULL
                    Vector_add(v, 1);
                    v->el[Vector_len(*v)] = file_attr;
                    Vector_set_len(v, Vector_len(*v) + 1);
                }
            }
        }
        closedir(dir);
    }
}

static long dir_size_get_result(const char *dir_path, bool allow_enqueue) {
    dir_size_cache_start();

    pthread_mutex_lock(&dir_size_mutex);
    DirSizeEntry *entry = dir_size_find_entry(dir_path);
    if (entry) {
        long result = entry->size;
        DirSizeCacheStatus status = entry->status;
        pthread_mutex_unlock(&dir_size_mutex);
        return (status == DIR_SIZE_STATUS_READY) ? result : DIR_SIZE_PENDING;
    }

    if (!allow_enqueue || !dir_size_user_idle()) {
        pthread_mutex_unlock(&dir_size_mutex);
        return DIR_SIZE_PENDING;
    }

    DirSizeEntry *new_entry = malloc(sizeof(DirSizeEntry));
    if (!new_entry) {
        pthread_mutex_unlock(&dir_size_mutex);
        return -1;
    }
    strncpy(new_entry->path, dir_path, MAX_PATH_LENGTH - 1);
    new_entry->path[MAX_PATH_LENGTH - 1] = '\0';
    new_entry->size = DIR_SIZE_PENDING;
    new_entry->progress = 0;
    new_entry->status = DIR_SIZE_STATUS_PENDING;
    new_entry->job_enqueued = false;
    new_entry->next = dir_size_cache_head;
    dir_size_cache_head = new_entry;

    dir_size_enqueue_job_locked(dir_path);
    pthread_mutex_unlock(&dir_size_mutex);
    return DIR_SIZE_PENDING;
}

long dir_size_get_progress(const char *dir_path) {
    if (!dir_path || !*dir_path) return 0;
    pthread_mutex_lock(&dir_size_mutex);
    DirSizeEntry *entry = dir_size_find_entry(dir_path);
    long p = 0;
    if (entry && entry->status == DIR_SIZE_STATUS_PENDING) {
        p = entry->progress;
        if (p < 0) p = 0;
    }
    pthread_mutex_unlock(&dir_size_mutex);
    return p;
}

void dir_size_cache_start(void) {
    pthread_mutex_lock(&dir_size_mutex);
    ensure_dir_size_worker();
    pthread_mutex_unlock(&dir_size_mutex);
}

void dir_size_cache_stop(void) {
    pthread_mutex_lock(&dir_size_mutex);
    if (!dir_size_thread_initialized) {
        dir_size_cache_clear_locked();
        pthread_mutex_unlock(&dir_size_mutex);
        return;
    }
    dir_size_thread_running = false;
    pthread_cond_broadcast(&dir_size_cond);
    pthread_mutex_unlock(&dir_size_mutex);
    pthread_join(dir_size_thread, NULL);

    pthread_mutex_lock(&dir_size_mutex);
    dir_size_thread_initialized = false;
    dir_size_cache_clear_locked();
    pthread_mutex_unlock(&dir_size_mutex);
}

void format_dir_size_pending_animation(char *buffer, size_t len, bool reset) {
    if (len == 0 || buffer == NULL) {
        return;
    }
    static size_t estimate = 0;
    const size_t start_estimate = 5 * 1024; // 5 KB
    const size_t max_estimate = 1ULL << 40; // 1 TiB cap for animation
    if (reset || estimate == 0) {
        estimate = start_estimate;
    } else {
        estimate = estimate + estimate / 2 + 1024; // grow by ~1.5x each frame
        if (estimate > max_estimate) {
            estimate = max_estimate;
        }
    }
    char formatted[32];
    format_file_size(formatted, estimate);
    snprintf(buffer, len, "Calculating... %s", formatted);
}

typedef struct {
    const char *job_path;
    long *total;
    long last_update_ns;
    int items_since_update;
} DirSizeProgressCtx;

static void dir_size_progress_maybe_update(DirSizeProgressCtx *ctx) {
    if (!ctx || !ctx->job_path || !ctx->total) return;

    // Throttle updates: at most ~20Hz or every N entries to keep mutex overhead low.
    const long UPDATE_INTERVAL_NS = 50L * 1000 * 1000; // 50ms
    const int UPDATE_ITEM_BATCH = 128;

    ctx->items_since_update++;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long now_ns = now.tv_sec * 1000000000L + now.tv_nsec;

    if (ctx->items_since_update < UPDATE_ITEM_BATCH &&
        (ctx->last_update_ns != 0) &&
        (now_ns - ctx->last_update_ns) < UPDATE_INTERVAL_NS) {
        return;
    }

    ctx->items_since_update = 0;
    ctx->last_update_ns = now_ns;

    pthread_mutex_lock(&dir_size_mutex);
    DirSizeEntry *entry = dir_size_find_entry(ctx->job_path);
    if (entry && entry->status == DIR_SIZE_STATUS_PENDING) {
        entry->progress = *ctx->total;
    }
    pthread_mutex_unlock(&dir_size_mutex);
}

static long compute_directory_size_full_progress_inner(const char *dir_path, DirSizeProgressCtx *ctx) {
    if (strncmp(dir_path, "/proc", 5) == 0 ||
        strncmp(dir_path, "/sys", 4) == 0 ||
        strncmp(dir_path, "/dev", 4) == 0 ||
        strncmp(dir_path, "/run", 4) == 0) {
        return DIR_SIZE_VIRTUAL_FS;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) {
        if (errno == EACCES) {
            return DIR_SIZE_PERMISSION_DENIED;
        }
        return -1;
    }

    const long MAX_SIZE_THRESHOLD = 1000L * 1024 * 1024 * 1024 * 1024; // 1000 TiB
    struct dirent *entry;
    struct stat statbuf;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char path[MAX_PATH_LENGTH];
        if (strlen(dir_path) + strlen(entry->d_name) + 1 < sizeof(path)) {
            snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        } else {
            continue;
        }

        if (lstat(path, &statbuf) == -1) {
            continue;
        }

        if (S_ISDIR(statbuf.st_mode)) {
            long rc = compute_directory_size_full_progress_inner(path, ctx);
            if (rc == DIR_SIZE_VIRTUAL_FS || rc == DIR_SIZE_PERMISSION_DENIED) {
                closedir(dir);
                return rc;
            } else if (rc == DIR_SIZE_TOO_LARGE) {
                closedir(dir);
                return DIR_SIZE_TOO_LARGE;
            } else if (rc < 0) {
                // unreadable dir: skip
            }
        } else {
            *ctx->total += statbuf.st_size;
            if (*ctx->total > MAX_SIZE_THRESHOLD) {
                closedir(dir);
                return DIR_SIZE_TOO_LARGE;
            }
            dir_size_progress_maybe_update(ctx);
        }
    }

    closedir(dir);
    return 0;
}

static long compute_directory_size_full_progress(const char *dir_path, const char *job_path) {
    long total = 0;
    DirSizeProgressCtx ctx = {
        .job_path = job_path,
        .total = &total,
        .last_update_ns = 0,
        .items_since_update = 0,
    };

    // Initialize progress to 0.
    pthread_mutex_lock(&dir_size_mutex);
    DirSizeEntry *entry = dir_size_find_entry(job_path);
    if (entry && entry->status == DIR_SIZE_STATUS_PENDING) {
        entry->progress = 0;
    }
    pthread_mutex_unlock(&dir_size_mutex);

    long rc = compute_directory_size_full_progress_inner(dir_path, &ctx);
    if (rc < 0) return rc;

    // Final progress push.
    pthread_mutex_lock(&dir_size_mutex);
    entry = dir_size_find_entry(job_path);
    if (entry && entry->status == DIR_SIZE_STATUS_PENDING) {
        entry->progress = total;
    }
    pthread_mutex_unlock(&dir_size_mutex);
    return total;
}

// Recursive function to calculate directory size with guard rails to keep UI responsive (legacy fallback)
long get_directory_size(const char *dir_path) {
    return dir_size_get_result(dir_path, true);
}

long get_directory_size_peek(const char *dir_path) {
    return dir_size_get_result(dir_path, false);
}
/**
 * Function to display file information in a window
 *
 * @param window the window to display the file information
 * @param file_path the path to the file
 * @param max_x the maximum width of the window
 */
void display_file_info(WINDOW *window, const char *file_path, int max_x) {
    struct stat file_stat;

    // Attempt to retrieve file statistics (use lstat for POSIX-correct symlink handling)
    if (lstat(file_path, &file_stat) == -1) {
        if (errno == EACCES) {
            mvwprintw(window, 2, 2, "Permission denied");
        } else if (errno == ENOENT) {
            mvwprintw(window, 2, 2, "File not found (it may have been removed)");
        } else {
            mvwprintw(window, 2, 2, "Unable to stat: %s", strerror(errno));
        }
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

    // NEW: get from KeyBindings or from wherever you store it:
    int label_width = g_kb.info_label_width;  

    // Display File Size or Directory Size
   if (S_ISDIR(file_stat.st_mode)) {
        static char last_size_path[MAX_PATH_LENGTH] = "";
        static struct timespec last_size_path_change = {0};
        static bool last_size_path_initialized = false;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        bool path_changed = !last_size_path_initialized ||
                            strncmp(last_size_path, file_path, MAX_PATH_LENGTH) != 0;
        if (path_changed) {
            strncpy(last_size_path, file_path, MAX_PATH_LENGTH - 1);
            last_size_path[MAX_PATH_LENGTH - 1] = '\0';
            last_size_path_change = now;
            last_size_path_initialized = true;
        }

        long elapsed_ns = (now.tv_sec - last_size_path_change.tv_sec) * 1000000000L +
                          (now.tv_nsec - last_size_path_change.tv_nsec);
        bool allow_enqueue = (elapsed_ns >= DIR_SIZE_REQUEST_DELAY_NS) && dir_size_can_enqueue();

        long dir_size = dir_size_get_result(file_path, allow_enqueue);
        
        char fileSizeStr[64] = "-";
        if (dir_size == -1) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Error");
        } else if (dir_size == DIR_SIZE_VIRTUAL_FS) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Virtual FS");
        } else if (dir_size == DIR_SIZE_TOO_LARGE) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Too large");
        } else if (dir_size == DIR_SIZE_PERMISSION_DENIED) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Permission denied");
        } else if (dir_size == DIR_SIZE_PENDING) {
            long p = dir_size_get_progress(file_path);
            if (p > 0) {
                char tmp[32];
                format_file_size(tmp, (size_t)p);
                snprintf(fileSizeStr, sizeof(fileSizeStr), "Calculating... %s", tmp);
            } else if (allow_enqueue) {
                format_dir_size_pending_animation(fileSizeStr, sizeof(fileSizeStr), path_changed);
            } else {
                snprintf(fileSizeStr, sizeof(fileSizeStr), "Waiting...");
            }
        } else {
            format_file_size(fileSizeStr, dir_size);
        }
        mvwprintw(window, 2, 2, "%-*s %s", label_width, "📁 Directory Size:", fileSizeStr);
    } else {
        char fileSizeStr[20];
        format_file_size(fileSizeStr, file_stat.st_size);
        mvwprintw(window, 2, 2, "%-*s %s", label_width, "📏 File Size:", fileSizeStr); // Updated with emoji
    }
    // Display MIME type using libmagic
    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE | MAGIC_SYMLINK | MAGIC_CHECK);
    if (magic_cookie == NULL) {
        mvwprintw(window, 5, 2, "%-*s %s", label_width, "📂 MIME type:", "Error initializing magic library");
        return;
    }
    if (magic_load(magic_cookie, NULL) != 0) {
        mvwprintw(window, 5, 2, "%-*s %s", label_width, "📂 MIME type:", magic_error(magic_cookie));
        magic_close(magic_cookie);
        return;
    }
    const char *mime_type = magic_file(magic_cookie, file_path);
    const char *emoji = get_file_emoji(mime_type, file_path);
    if (mime_type == NULL) {
        mvwprintw(window, 5, 2, "%-*s %s", label_width, "📂 MIME type:", "Unknown (error)");
    } else {
        size_t value_width = (size_t)(max_x - 2 - label_width - 1); // 2 for left margin, 1 for space
        const char *display_mime = mime_type;

        // Truncate MIME type string if it's too long
        char truncated_mime[value_width + 1];
        if (strlen(mime_type) > value_width) {
            strncpy(truncated_mime, mime_type, value_width);
            truncated_mime[value_width] = '\0';
            display_mime = truncated_mime;
        }

        mvwprintw(window, 5, 2, "%-*s %s %s", label_width, emoji, "MIME type:", display_mime);
    }
    magic_close(magic_cookie);
}
/**
 * Function to render and manage scrolling within the text buffer
 *
 * @param window the window to render the text buffer
 * @param buffer the text buffer containing file contents
 * @param start_line the starting line number for rendering
 * @param cursor_line the current cursor line
 * @param cursor_col the current cursor column
 */
void render_text_buffer(WINDOW *window, TextBuffer *buffer, int *start_line, int cursor_line, int cursor_col) {
    if (!buffer || !buffer->lines) {
        return;
    }
    werase(window);
    box(window, 0, 0);

    int max_y, max_x;
    getmaxyx(window, max_y, max_x);
    int content_height = max_y - 2;  // Subtract 2 for borders

    // Calculate the width needed for line numbers
    int label_width = snprintf(NULL, 0, "%d", buffer->num_lines) + 1;

    // Adjust start_line to ensure cursor is visible
    if (cursor_line < *start_line) {
        *start_line = cursor_line;
    } else if (cursor_line >= *start_line + content_height) {
        *start_line = cursor_line - content_height + 1;
    }

    // Ensure start_line doesn't go out of bounds
    if (*start_line < 0) *start_line = 0;
    if (buffer->num_lines > content_height) {
        *start_line = MIN(*start_line, buffer->num_lines - content_height);
    } else {
        *start_line = 0;
    }

    // Draw separator line for line numbers
    for (int i = 1; i < max_y - 1; i++) {
        mvwaddch(window, i, label_width + 1, ACS_VLINE);
    }

    // Calculate the width available for text content
    // Subtract: left border (1) + line numbers + separator (1) + right border (1) + padding (1)
    int content_width = max_x - label_width - 4;
    
    // Ensure content_width is at least 1 to avoid issues
    if (content_width < 1) {
        content_width = 1;
    }
    
    // Calculate the content start position (after line numbers and separator)
    int content_start = label_width + 3;

    // Calculate horizontal scroll position to keep cursor visible
    static int h_scroll = 0;
    static int last_content_width = 0;
    const int scroll_margin = 5;  // Number of columns to keep visible on either side

    // If content_width is too small, reset h_scroll to prevent issues
    if (content_width < scroll_margin * 2) {
        h_scroll = 0;
        last_content_width = content_width;
    }
    // Reset horizontal scroll if window got wider and we can now show more content
    // This ensures we use all available horizontal space
    else if (content_width > last_content_width && h_scroll > 0) {
        // Find the longest line in the visible area
        int max_line_length = 0;
        for (int i = 0; i < content_height && (*start_line + i) < buffer->num_lines; i++) {
            const char *line = buffer->lines[*start_line + i] ? buffer->lines[*start_line + i] : "";
            int line_len = strlen(line);
            if (line_len > max_line_length) {
                max_line_length = line_len;
            }
        }
        // If all visible lines fit in the new width, reset scroll to use full width
        if (max_line_length <= content_width) {
            h_scroll = 0;
        } else {
            // Otherwise, reduce scroll to show as much as possible while keeping cursor visible
            // If cursor is within the new wider view, we can reduce scroll
            if (cursor_col < content_width - scroll_margin) {
                h_scroll = 0;
            } else {
                // Keep cursor visible but reduce scroll to show more content
                int new_h_scroll = cursor_col - content_width + scroll_margin + 1;
                if (new_h_scroll < h_scroll) {
                    h_scroll = new_h_scroll;
                }
            }
        }
    }
    // Only update last_content_width if we didn't reset h_scroll
    if (content_width >= scroll_margin * 2) {
        last_content_width = content_width;
    }

    // Adjust horizontal scroll if cursor would be outside visible area
    // Only do this if content_width is reasonable
    if (content_width >= scroll_margin * 2) {
        if (cursor_col >= h_scroll + content_width - scroll_margin) {
            h_scroll = cursor_col - content_width + scroll_margin + 1;
        } else if (cursor_col < h_scroll + scroll_margin) {
            h_scroll = MAX(0, cursor_col - scroll_margin);
        }
        
        // Ensure h_scroll doesn't exceed reasonable bounds
        if (h_scroll < 0) h_scroll = 0;
    }
    
    // Always try to minimize scroll to use maximum available space
    // If cursor has plenty of room, reduce scroll to show more content to the left
    // Only do this if content_width is reasonable
    if (content_width >= scroll_margin * 2 && h_scroll > 0 && cursor_col < h_scroll + content_width - scroll_margin * 2) {
        // Find the longest line in the visible area to see how much we need to scroll
        int max_line_in_view = 0;
        for (int i = 0; i < content_height && (*start_line + i) < buffer->num_lines; i++) {
            const char *line = buffer->lines[*start_line + i] ? buffer->lines[*start_line + i] : "";
            int line_len = strlen(line);
            if (line_len > max_line_in_view) {
                max_line_in_view = line_len;
            }
        }
        
        // If all lines fit without scrolling, reset scroll
        if (max_line_in_view <= content_width) {
            h_scroll = 0;
        } else {
            // Minimize scroll while keeping cursor visible
            // Try to reduce scroll as much as possible
            int ideal_scroll = MAX(0, cursor_col - content_width + scroll_margin + 1);
            if (ideal_scroll < h_scroll) {
                h_scroll = ideal_scroll;
            }
        }
    }

    g_editor_h_scroll = h_scroll;

    // Display line numbers and content
    for (int i = 0; i < content_height && (*start_line + i) < buffer->num_lines; i++) {
        // Print line number (right-aligned in its column)
        mvwprintw(window, i + 1, 2, "%*d", label_width - 1, *start_line + i + 1);

        // Get the line content
        const char *line = buffer->lines[*start_line + i] ? buffer->lines[*start_line + i] : "";
        int line_length = strlen(line);

        // Print the visible portion of the line
        if (h_scroll < line_length) {
            mvwprintw(window, i + 1, content_start, "%.*s", 
                     content_width,
                     line + h_scroll);  // Offset the line by h_scroll
        } else {
            mvwprintw(window, i + 1, content_start, "%*s", 
                     content_width,
                     "");  // Print empty string if line is shorter than content_width
        }

        // Highlight selection range (if active)
        if (g_sel_active) {
            int s_line = g_sel_anchor_line;
            int s_col = g_sel_anchor_col;
            int e_line = g_sel_end_line;
            int e_col = g_sel_end_col;
            if (s_line > e_line || (s_line == e_line && s_col > e_col)) {
                int tl = s_line; int tc = s_col;
                s_line = e_line; s_col = e_col;
                e_line = tl; e_col = tc;
            }

            int line_index = *start_line + i;
            if (line_index >= s_line && line_index <= e_line) {
                int hl_start = 0;
                int hl_end = line_length; // exclusive
                if (s_line == e_line) {
                    hl_start = s_col;
                    hl_end = e_col;
                } else if (line_index == s_line) {
                    hl_start = s_col;
                    hl_end = line_length;
                } else if (line_index == e_line) {
                    hl_start = 0;
                    hl_end = e_col;
                }

                if (line_length == 0) {
                    hl_start = 0;
                    hl_end = 1;
                }

                if (hl_end < hl_start) {
                    int tmp = hl_start; hl_start = hl_end; hl_end = tmp;
                }

                // Clamp to visible region
                int vis_start = h_scroll;
                int vis_end = h_scroll + content_width;
                int draw_start = MAX(hl_start, vis_start);
                int draw_end = MIN(hl_end, vis_end);
                if (draw_end > draw_start) {
                    int draw_len = draw_end - draw_start;
                    int draw_x = content_start + (draw_start - h_scroll);
                    mvwchgat(window, i + 1, draw_x, draw_len, A_REVERSE, 0, NULL);
                }
            }
        }

        // If this is the cursor line, draw a visible cursor (even on empty/whitespace)
        if ((*start_line + i) == cursor_line) {
            int y = i + 1;

            // Clamp cursor_col to the line length (allow "at EOL")
            int cc = cursor_col;
            if (cc < 0) cc = 0;
            if (cc > line_length) cc = line_length;

            // Compute X and clamp to visible content area
            int cursor_x = content_start + (cc - h_scroll);
            int min_x = content_start;
            int max_x = content_start + content_width - 1;
            if (cursor_x < min_x) cursor_x = min_x;
            if (cursor_x > max_x) cursor_x = max_x;

            // Draw a cursor highlight without using ACS glyphs (some terminals show ACS_CKBOARD as 'a').
            bool at_eol = (cc >= line_length);
            unsigned char ch_under = 0;
            if (!at_eol) ch_under = (unsigned char)line[cc];

            // If we're at EOL or on whitespace, ensure the cell contains a plain space, then reverse it.
            if (at_eol || ch_under == ' ' || ch_under == '\t') {
                mvwaddch(window, y, cursor_x, ' ');
            }
            mvwchgat(window, y, cursor_x, 1, A_REVERSE, 0, NULL);
        }
    }

    // Hide the terminal cursor since we're using visual highlighting (A_REVERSE)
    // This prevents the cursor from appearing in the wrong location
    curs_set(0);
    
    wrefresh(window);
}

static int parse_ctrl_arrow_seq(WINDOW *win) {
    // Read the remaining escape sequence with a small timeout to allow
    // characters to arrive from the terminal.
    wtimeout(win, 100);  // 100ms timeout for escape sequences
    int c = wgetch(win);
    if (c == ERR) {
        wtimeout(win, 10);
        return 27;
    }

    // Handle SS3 sequences (ESC O A/B/C/D)
    if (c == 'O') {
        int next = wgetch(win);
        wtimeout(win, 10);
        if (next == 'A') return KEY_UP;
        if (next == 'B') return KEY_DOWN;
        if (next == 'C') return KEY_RIGHT;
        if (next == 'D') return KEY_LEFT;
        if (next != ERR) ungetch(next);
        return 27;
    }

    // CSI sequences (ESC [ ...)
    if (c != '[') {
        wtimeout(win, 10);
        ungetch(c);
        return 27;
    }

    int p1 = -1;
    int p2 = -1;
    int value = 0;
    bool in_num = false;
    int param_index = 0;
    int final = 0;

    while (true) {
        c = wgetch(win);
        if (c == ERR) break;

        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
            in_num = true;
            continue;
        }

        if (c == ';') {
            if (in_num) {
                if (param_index == 0) p1 = value;
                else if (param_index == 1) p2 = value;
                param_index++;
                value = 0;
                in_num = false;
            } else {
                param_index++;
            }
            continue;
        }

        // Final byte for arrow keys
        if (c == 'A' || c == 'B' || c == 'C' || c == 'D') {
            if (in_num) {
                if (param_index == 0) p1 = value;
                else if (param_index == 1) p2 = value;
            }
            final = c;
            break;
        }
    }

    wtimeout(win, 10);

    if (final == 0) return 27;

    // Interpret modifiers: 2=Shift, 3=Alt, 5=Ctrl, 6=Shift+Ctrl
    int mod = (p2 != -1) ? p2 : p1;

    if (mod == 5 || mod == 6) {
        if (final == 'A') return CTRL_UP_CODE;
        if (final == 'B') return CTRL_DOWN_CODE;
        if (final == 'C') return CTRL_RIGHT_CODE;
        if (final == 'D') return CTRL_LEFT_CODE;
    }

    if (mod == 2) {
        if (final == 'A') return KEY_SR;
        if (final == 'B') return KEY_SF;
        if (final == 'C') return KEY_SRIGHT;
        if (final == 'D') return KEY_SLEFT;
    }

    // Fallback to plain arrows
    if (final == 'A') return KEY_UP;
    if (final == 'B') return KEY_DOWN;
    if (final == 'C') return KEY_RIGHT;
    if (final == 'D') return KEY_LEFT;

    return 27;
}

// -----------------------
// Word-wise cursor movement helpers (used by Ctrl+Arrow)
// -----------------------
// Treat space/tab as word separators (matches Ctrl+Left/Right logic).
static inline bool is_ws_char(char c) {
    return (c == ' ' || c == '\t');
}

static int word_left_col(const char *line, int col) {
    if (!line) return 0;
    int len = (int)strlen(line);
    if (col > len) col = len;
    if (col <= 0) return 0;

    col--;
    while (col > 0 && is_ws_char(line[col])) col--;
    while (col > 0 && !is_ws_char(line[col - 1])) col--;
    return col;
}

static int word_right_col(const char *line, int col) {
    if (!line) return 0;
    int len = (int)strlen(line);
    if (col < 0) col = 0;
    if (col > len) col = len;

    if (col < len) {
        while (col < len && !is_ws_char(line[col])) col++;
        while (col < len && is_ws_char(line[col])) col++;
    }
    return col;
}

/**
 * Function to edit a file in the terminal using a text buffer
 *
 * @param window the window to display the file content
 * @param file_path the path to the file to edit
 * @param notification_window the window to display notifications
 */
void edit_file_in_terminal(WINDOW *window, 
                           const char *file_path, 
                           WINDOW *notification_window, 
                           KeyBindings *kb,
                           struct PluginManager *pm)
{
    (void)window;  // Unused - we create a full-screen editor window instead
    is_editing = 1;
    if (file_path) {
        strncpy(g_editor_path, file_path, sizeof(g_editor_path) - 1);
        g_editor_path[sizeof(g_editor_path) - 1] = '\0';
    } else {
        g_editor_path[0] = '\0';
    }

    // Open the file for reading and writing
    int fd = open(file_path, O_RDWR);
    if (fd == -1) {
        pthread_mutex_lock(&banner_mutex);
        mvwprintw(notification_window, 1, 2, "Unable to open file");
        wrefresh(notification_window);
        pthread_mutex_unlock(&banner_mutex);
        return;
    }

    FILE *file = fdopen(fd, "r+");
    if (!file) {
        pthread_mutex_lock(&banner_mutex);
        mvwprintw(notification_window, 1, 2, "Unable to open file stream");
        wrefresh(notification_window);
        pthread_mutex_unlock(&banner_mutex);
        close(fd);
        return;
    }

    // Create a full-screen editor window (full terminal width, minus banner and notification)
    // This gives us maximum horizontal space for editing
    // The editor window is used for displaying and editing file content

    int banner_height = 3;
    int notif_height = 1;
    int editor_height = LINES - banner_height - notif_height;
    int editor_width = COLS;
    int editor_start_y = banner_height;
    int editor_start_x = 0;
    
    WINDOW *editor_window = newwin(editor_height, editor_width, editor_start_y, editor_start_x);
    if (!editor_window) {
        pthread_mutex_lock(&banner_mutex);
        mvwprintw(notification_window, 1, 2, "Unable to create editor window");
        wrefresh(notification_window);
        pthread_mutex_unlock(&banner_mutex);
        fclose(file);
        return;
    }

    // --- Make Ctrl+C / Ctrl+V come through as bytes (3 / 22) instead of signals/paste handling.
    raw();
    
    // Clear and prepare the editor window
    pthread_mutex_lock(&banner_mutex);
    werase(editor_window);
    box(editor_window, 0, 0);
    pthread_mutex_unlock(&banner_mutex);

    TextBuffer text_buffer;
    text_buffer.capacity = 100; 
    text_buffer.num_lines = 0;
    text_buffer.lines = malloc(sizeof(char*) * text_buffer.capacity);
    g_editor_buffer = &text_buffer;
    if (!text_buffer.lines) {
        pthread_mutex_lock(&banner_mutex);
        mvwprintw(notification_window, 1, 2, "Memory allocation error");
        wrefresh(notification_window);
        pthread_mutex_unlock(&banner_mutex);
        fclose(file);
        close(fd);
        return;
    }

    // Read the file into our text buffer
    bool is_empty = true;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        is_empty = false;
        line[strcspn(line, "\n")] = '\0';
        size_t line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\r') {
            line[line_len - 1] = '\0';
        }
        // Replace tabs with spaces
        for (char *p = line; *p; p++) {
            if (*p == '\t') {
                *p = ' ';
            }
        }
        if (text_buffer.num_lines >= text_buffer.capacity) {
            text_buffer.capacity *= 2;
            char **tmp = realloc(text_buffer.lines, 
                            sizeof(char*) * text_buffer.capacity);
            if (!tmp) {
                mvwprintw(notification_window, 1, 2, "Memory allocation error");
                wrefresh(notification_window);
                fclose(file);
                return;
            }
            text_buffer.lines = tmp;
        }
        text_buffer.lines[text_buffer.num_lines++] = strdup(line);
    }
    if (is_empty) {
        // If file is empty, start with one blank line
        text_buffer.lines[text_buffer.num_lines++] = strdup("");
    }

    // Cursor and scrolling state
    int cursor_line = 0;
    int cursor_col  = 0;
    int start_line  = 0;
    g_sel_active = false;
    g_editor_cursor_line = 0;
    g_editor_cursor_col = 0;
    g_editor_dirty = false;
    bool editor_dirty = false;
    UndoManager um = {0};

    // Hide terminal cursor - we use visual highlighting instead
    curs_set(0);
    keypad(editor_window, TRUE);
    mouseinterval(0);

    // Ask ncurses for mouse events AND motion.
    mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);

    // Also explicitly tell the terminal to send drag-motion events.
    editor_enable_mouse_drag_reporting();
    wtimeout(editor_window, 10);  // Non-blocking input

    register_ctrl_arrow_keys_once();

    // Render the file content immediately when entering edit mode
    render_text_buffer(editor_window, &text_buffer, &start_line, cursor_line, cursor_col);

    bool exit_edit_mode = false;

    // Initialize time-based update tracking for edit mode
    // banner_offset is now a global variable - no need for static
    struct timespec last_banner_update;
    struct timespec last_notif_check;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    clock_gettime(CLOCK_MONOTONIC, &last_notif_check);
    int total_scroll_length = COLS + (BANNER_TEXT ? strlen(BANNER_TEXT) : 0) + (BUILD_INFO ? strlen(BUILD_INFO) : 0) + 4;

    while (!exit_edit_mode) {
        // Check for window resize
        if (resized) {
            resized = 0;
            
            // Update ncurses terminal size
            struct winsize w;
            if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0) {
                resize_term(w.ws_row, w.ws_col);
            }
            
            // Update banner and notification windows first
            pthread_mutex_lock(&banner_mutex);
            
            // Recreate banner window with new dimensions
            if (bannerwin) {
                delwin(bannerwin);
            }
            bannerwin = newwin(banner_height, COLS, 0, 0);
            if (bannerwin) {
                box(bannerwin, 0, 0);
                wrefresh(bannerwin);
            }
            
            // Recreate notification window with new dimensions
            if (notifwin) {
                delwin(notifwin);
            }
            notifwin = newwin(notif_height, COLS, LINES - notif_height, 0);
            if (notifwin) {
                box(notifwin, 0, 0);
                wrefresh(notifwin);
            }
            
            // Recreate editor window with new dimensions
            delwin(editor_window);
            editor_height = LINES - banner_height - notif_height;
            editor_width = COLS;
            editor_window = newwin(editor_height, editor_width, editor_start_y, editor_start_x);
            if (editor_window) {
                // Adjust start_line to ensure cursor is visible in the new window size
                // Get the new content height
                int new_content_height = editor_height - 2; // Subtract 2 for borders
                
                // Ensure cursor is visible - adjust start_line if needed
                if (cursor_line < start_line) {
                    // Cursor is above visible area, move start_line up
                    start_line = cursor_line;
                } else if (cursor_line >= start_line + new_content_height) {
                    // Cursor is below visible area, move start_line down
                    start_line = cursor_line - new_content_height + 1;
                }
                
                // Ensure start_line doesn't go out of bounds
                if (start_line < 0) start_line = 0;
                if (text_buffer.num_lines > new_content_height) {
                    int max_start = text_buffer.num_lines - new_content_height;
                    if (start_line > max_start) start_line = max_start;
                } else {
                    start_line = 0;
                }
                
                werase(editor_window);
                box(editor_window, 0, 0);
                render_text_buffer(editor_window, &text_buffer, &start_line, cursor_line, cursor_col);
                keypad(editor_window, TRUE);
                wtimeout(editor_window, 10);
            }
            
            pthread_mutex_unlock(&banner_mutex);
            
            // Update scroll length for banner
            total_scroll_length = COLS + (BANNER_TEXT ? strlen(BANNER_TEXT) : 0) + (BUILD_INFO ? strlen(BUILD_INFO) : 0) + 4;
        }
        
        int ch = wgetch(editor_window);
        if (ch == ERR) {
            // Handle time-based updates while waiting for input
            struct timespec current_time;
            clock_gettime(CLOCK_MONOTONIC, &current_time);
            
            // Update banner scrolling
            long banner_time_diff = (current_time.tv_sec - last_banner_update.tv_sec) * 1000000 +
                                   (current_time.tv_nsec - last_banner_update.tv_nsec) / 1000;
            if (banner_time_diff >= BANNER_SCROLL_INTERVAL && BANNER_TEXT && bannerwin) {
                pthread_mutex_lock(&banner_mutex);
                draw_scrolling_banner(bannerwin, BANNER_TEXT, BUILD_INFO, banner_offset);
                pthread_mutex_unlock(&banner_mutex);
                banner_offset = (banner_offset + 1) % total_scroll_length;
                last_banner_update = current_time;
            }
            
            // Update notification timeout
            long notif_time_diff = (current_time.tv_sec - last_notif_check.tv_sec) * 1000 +
                                  (current_time.tv_nsec - last_notif_check.tv_nsec) / 1000000;
            // Check notification hold status before clearing
            bool can_clear_notif = !notification_hold_active;
            if (notification_hold_active) {
                // Check if hold period has expired
                if (current_time.tv_sec > notification_hold_until.tv_sec ||
                    (current_time.tv_sec == notification_hold_until.tv_sec && 
                     current_time.tv_nsec >= notification_hold_until.tv_nsec)) {
                    notification_hold_active = false;
                    can_clear_notif = true;
                }
            }
            if (!should_clear_notif && can_clear_notif && notif_time_diff >= NOTIFICATION_TIMEOUT_MS && notifwin) {
                pthread_mutex_lock(&banner_mutex);
                werase(notifwin);
                wrefresh(notifwin);
                pthread_mutex_unlock(&banner_mutex);
                should_clear_notif = true;
            }
            
            napms(10);
            continue;
        }

        if (ch == 27) {
            int mapped = parse_ctrl_arrow_seq(editor_window);
            if (mapped != 27) {
                ch = mapped;
            }
        }

        if (ch == KEY_MOUSE) {
            MEVENT ev;
            if (getmouse(&ev) == OK) {
                int m_line = 0, m_col = 0;

                int win_y = 0, win_x = 0;
                int max_y = 0, max_x = 0;
                getbegyx(editor_window, win_y, win_x);
                getmaxyx(editor_window, max_y, max_x);

                int rel_y = ev.y - win_y;
                int content_height = max_y - 2;
                int max_start = text_buffer.num_lines - content_height;
                if (max_start < 0) max_start = 0;

                bool pressed = (ev.bstate & BUTTON1_PRESSED) != 0;
                bool released = (ev.bstate & BUTTON1_RELEASED) != 0;
                bool moved = (ev.bstate & REPORT_MOUSE_POSITION) != 0;
                bool wheel_up = (ev.bstate & BUTTON4_PRESSED) != 0;
                bool wheel_down = (ev.bstate & BUTTON5_PRESSED) != 0;
                bool scrolled = false;

                if (wheel_up || wheel_down) {
                    int delta = wheel_up ? -3 : 3;
                    int new_start = start_line + delta;
                    if (new_start < 0) new_start = 0;
                    if (new_start > max_start) new_start = max_start;
                    if (new_start != start_line) {
                        start_line = new_start;
                        scrolled = true;
                    }
                }

                // Auto-scroll while dragging near edges.
                if (g_editor_mouse_dragging && (moved || pressed)) {
                    if (rel_y <= 1 && start_line > 0) {
                        start_line--;
                    } else if (rel_y >= max_y - 2) {
                        if (start_line < max_start) start_line++;
                    }
                }

                if (editor_mouse_to_buffer_pos(editor_window, &text_buffer, start_line,
                                               ev.y, ev.x, &m_line, &m_col)) {
                    // Start drag selection on press.
                    if (pressed && !g_editor_mouse_dragging) {
                        g_editor_mouse_dragging = true;

                        g_sel_active = true;
                        g_sel_anchor_line = m_line;
                        g_sel_anchor_col = m_col;
                        g_sel_end_line = m_line;
                        g_sel_end_col = m_col;
                    }

                    // Update selection continuously while dragging.
                    if (g_editor_mouse_dragging && (moved || pressed || scrolled)) {
                        g_sel_end_line = m_line;
                        g_sel_end_col = m_col;
                    }

                    // Finalize on release.
                    if (released) {
                        if (g_editor_mouse_dragging) {
                            g_sel_end_line = m_line;
                            g_sel_end_col = m_col;
                        }
                        g_editor_mouse_dragging = false;
                    }

                    // Keep cursor following the mouse position while selecting.
                    cursor_line = m_line;
                    cursor_col = m_col;

                    render_text_buffer(editor_window, &text_buffer, &start_line, cursor_line, cursor_col);
                }
            }
            continue;
        }

        // Check for console key first (allow opening console in editor)
        if (ch == kb->key_console) {
            console_show();
            should_clear_notif = false;
            render_text_buffer(editor_window, &text_buffer, &start_line, cursor_line, cursor_col);
            // Re-render notification window to ensure it stays visible
            if (notification_window) {
                touchwin(notification_window);
                wrefresh(notification_window);
            }
            continue;
        }

        // Normalize Ctrl+L (ncurses sends KEY_CLEAR)
        int plugin_ch = (ch == KEY_CLEAR) ? 12 : ch;
        if (pm && plugins_handle_key(pm, plugin_ch)) {
            should_clear_notif = false;
            // Reset notification timer so it stays visible for the normal timeout period
            clock_gettime(CLOCK_MONOTONIC, &last_notif_check);
            // Re-render editor buffer, then bring notification back on top
            render_text_buffer(editor_window, &text_buffer, &start_line, cursor_line, cursor_col);
            // Re-render notification window to ensure it stays visible
            if (notification_window) {
                touchwin(notification_window);
                wrefresh(notification_window);
            }
            continue;
        }

        // 1) Quit editing
        if (ch == kb->edit_quit) {
            if (!editor_dirty || confirm_discard_unsaved(editor_window)) {
                break;
            }
            render_text_buffer(editor_window, &text_buffer, &start_line, cursor_line, cursor_col);
            continue;
        }
        // 2) Save file
        else if (ch == kb->edit_save) {
            fclose(file);
            file = fopen(file_path, "w");
            if (!file) {
                mvwprintw(notification_window, 0, 0, "Error opening file for writing");
                wrefresh(notification_window);
                continue;
            }
            // Write lines
            for (int i = 0; i < text_buffer.num_lines; i++) {
                if (fprintf(file, "%s\n", text_buffer.lines[i]) < 0) {
                    mvwprintw(notification_window, 0, 0, "Error writing to file");
                    wrefresh(notification_window);
                    break;
                }
            }
            fflush(file);
            fclose(file);

            // Reopen in read+write (optional, if you want to keep editing)
            file = fopen(file_path, "r+");
            if (!file) {
                mvwprintw(notification_window, 0, 0, "Error reopening file for read+write");
                wrefresh(notification_window);
            }

            werase(notification_window);
            mvwprintw(notification_window, 0, 0, "File saved: %s", file_path);
            wrefresh(notification_window);
            editor_dirty = false;
            g_editor_dirty = false;
        }

        // 3) Move up
        else if (ch == kb->edit_up) {
            g_sel_active = false;
            if (cursor_line > 0) {
                cursor_line--;
                if (cursor_col > (int)strlen(text_buffer.lines[cursor_line])) {
                    cursor_col = strlen(text_buffer.lines[cursor_line]);
                }
            }
        }
        // 3b) Shift+Up (selection)
        else if (ch == KEY_SR) {
            if (!g_sel_active) {
                g_sel_active = true;
                g_sel_anchor_line = cursor_line;
                g_sel_anchor_col = cursor_col;
            }
            if (cursor_line > 0) {
                cursor_line--;
                if (cursor_col > (int)strlen(text_buffer.lines[cursor_line])) {
                    cursor_col = strlen(text_buffer.lines[cursor_line]);
                }
            }
            g_sel_end_line = cursor_line;
            g_sel_end_col = cursor_col;
        }
        // 4) Move down
        else if (ch == kb->edit_down) {
            g_sel_active = false;
            if (cursor_line < text_buffer.num_lines - 1) {
                cursor_line++;
                if (cursor_col > (int)strlen(text_buffer.lines[cursor_line])) {
                    cursor_col = strlen(text_buffer.lines[cursor_line]);
                }
            }
        }
        // 4b) Shift+Down (selection)
        else if (ch == KEY_SF) {
            if (!g_sel_active) {
                g_sel_active = true;
                g_sel_anchor_line = cursor_line;
                g_sel_anchor_col = cursor_col;
            }
            if (cursor_line < text_buffer.num_lines - 1) {
                cursor_line++;
                if (cursor_col > (int)strlen(text_buffer.lines[cursor_line])) {
                    cursor_col = strlen(text_buffer.lines[cursor_line]);
                }
            }
            g_sel_end_line = cursor_line;
            g_sel_end_col = cursor_col;
        }
        // 5) Move left
        else if (ch == kb->edit_left) {
            g_sel_active = false;
            if (cursor_col > 0) {
                cursor_col--;
            } else if (cursor_line > 0) {
                // Move up a line if user is at col=0
                cursor_line--;
                cursor_col = strlen(text_buffer.lines[cursor_line]);
            }
        }
        // 5b) Shift+Left (selection)
        else if (ch == KEY_SLEFT) {
            if (!g_sel_active) {
                g_sel_active = true;
                g_sel_anchor_line = cursor_line;
                g_sel_anchor_col = cursor_col;
            }
            if (cursor_col > 0) {
                cursor_col--;
            } else if (cursor_line > 0) {
                cursor_line--;
                cursor_col = strlen(text_buffer.lines[cursor_line]);
            }
            g_sel_end_line = cursor_line;
            g_sel_end_col = cursor_col;
        }
        // 6) Move right
        else if (ch == kb->edit_right) {
            g_sel_active = false;
            int line_len = (int)strlen(text_buffer.lines[cursor_line]);
            if (cursor_col < line_len) {
                cursor_col++;
            } else if (cursor_line < text_buffer.num_lines - 1) {
                // Move down a line if user is at end
                cursor_line++;
                cursor_col = 0;
            }
        }
        // 6b) Shift+Right (selection)
        else if (ch == KEY_SRIGHT) {
            if (!g_sel_active) {
                g_sel_active = true;
                g_sel_anchor_line = cursor_line;
                g_sel_anchor_col = cursor_col;
            }
            int line_len = (int)strlen(text_buffer.lines[cursor_line]);
            if (cursor_col < line_len) {
                cursor_col++;
            } else if (cursor_line < text_buffer.num_lines - 1) {
                cursor_line++;
                cursor_col = 0;
            }
            g_sel_end_line = cursor_line;
            g_sel_end_col = cursor_col;
        }
        // Ctrl+C (copy)
        else if (ch == kb->edit_copy) {
            char *copied = copy_selection_or_line(&text_buffer, cursor_line, cursor_col);
            if (copied) {
                free(g_editor_clipboard);
                g_editor_clipboard = copied;
                if (notification_window) {
                    werase(notification_window);
                    mvwprintw(notification_window, 0, 0, "Copied %zu bytes", strlen(g_editor_clipboard));
                    wrefresh(notification_window);
                    should_clear_notif = false;
                    clock_gettime(CLOCK_MONOTONIC, &last_notif_check);
                }
            }
        }
        // Ctrl+A (select all)
        else if (ch == kb->edit_select_all) {
            if (text_buffer.num_lines <= 0) {
                g_sel_active = false;
            } else {
                g_sel_active = true;
                g_sel_anchor_line = 0;
                g_sel_anchor_col = 0;

                g_sel_end_line = text_buffer.num_lines - 1;
                const char *last = text_buffer.lines[g_sel_end_line] ? text_buffer.lines[g_sel_end_line] : "";
                g_sel_end_col = (int)strlen(last);

                cursor_line = g_sel_end_line;
                cursor_col = g_sel_end_col;
            }
        }
        // Ctrl+Z (undo)
        else if (ch == kb->edit_undo) {
            if (editor_do_undo(&um, &text_buffer, &cursor_line, &cursor_col, &start_line)) {
                editor_dirty = true;
                if (notification_window) {
                    werase(notification_window);
                    mvwprintw(notification_window, 0, 0, "Undo");
                    wrefresh(notification_window);
                    should_clear_notif = false;
                    clock_gettime(CLOCK_MONOTONIC, &last_notif_check);
                }
            }
        }
        // Ctrl+Y (redo)
        else if (ch == kb->edit_redo) {
            if (editor_do_redo(&um, &text_buffer, &cursor_line, &cursor_col, &start_line)) {
                editor_dirty = true;
                if (notification_window) {
                    werase(notification_window);
                    mvwprintw(notification_window, 0, 0, "Redo");
                    wrefresh(notification_window);
                    should_clear_notif = false;
                    clock_gettime(CLOCK_MONOTONIC, &last_notif_check);
                }
            }
        }
        // Ctrl+U (uppercase selection)
        else if (ch == kb->edit_uppercase) {
            if (!g_sel_active || selection_is_empty()) {
                if (notification_window) {
                    werase(notification_window);
                    mvwprintw(notification_window, 0, 0, "No selection to uppercase");
                    wrefresh(notification_window);
                    should_clear_notif = false;
                    clock_gettime(CLOCK_MONOTONIC, &last_notif_check);
                }
            } else {
                editor_undo_record(&um, &text_buffer, cursor_line, cursor_col, start_line);
                editor_selection_to_uppercase(&text_buffer);
                editor_dirty = true;
                g_editor_dirty = true;

                if (notification_window) {
                    werase(notification_window);
                    mvwprintw(notification_window, 0, 0, "Uppercased selection");
                    wrefresh(notification_window);
                    should_clear_notif = false;
                    clock_gettime(CLOCK_MONOTONIC, &last_notif_check);
                }
            }
            render_text_buffer(editor_window, &text_buffer, &start_line, cursor_line, cursor_col);
        }
        // Ctrl+X (cut)
        else if (ch == kb->edit_cut) {
            char *copied = copy_selection_or_line(&text_buffer, cursor_line, cursor_col);
            if (copied) {
                free(g_editor_clipboard);
                g_editor_clipboard = copied;
                if (notification_window) {
                    werase(notification_window);
                    mvwprintw(notification_window, 0, 0, "Cut %zu bytes", strlen(g_editor_clipboard));
                    wrefresh(notification_window);
                    should_clear_notif = false;
                    clock_gettime(CLOCK_MONOTONIC, &last_notif_check);
                }
            }
            if (g_sel_active && !selection_is_empty()) {
                int s_line = g_sel_anchor_line;
                int s_col = g_sel_anchor_col;
                int e_line = g_sel_end_line;
                int e_col = g_sel_end_col;
                normalize_selection(&s_line, &s_col, &e_line, &e_col);
                editor_undo_record(&um, &text_buffer, cursor_line, cursor_col, start_line);
                delete_selection(&text_buffer);
                cursor_line = s_line;
                cursor_col = s_col;
                g_sel_active = false;
                editor_dirty = true;
            } else {
                // Cut current line
                if (cursor_line >= 0 && cursor_line < text_buffer.num_lines) {
                    editor_undo_record(&um, &text_buffer, cursor_line, cursor_col, start_line);
                    free(text_buffer.lines[cursor_line]);
                    for (int i = cursor_line; i < text_buffer.num_lines - 1; i++) {
                        text_buffer.lines[i] = text_buffer.lines[i + 1];
                    }
                    text_buffer.num_lines--;
                    if (text_buffer.num_lines <= 0) {
                        text_buffer.num_lines = 1;
                        text_buffer.lines[0] = strdup("");
                        cursor_line = 0;
                        cursor_col = 0;
                    } else if (cursor_line >= text_buffer.num_lines) {
                        cursor_line = text_buffer.num_lines - 1;
                        cursor_col = MIN(cursor_col, (int)strlen(text_buffer.lines[cursor_line]));
                    }
                    editor_dirty = true;
                }
            }
        }
        // Ctrl+V or F5 (paste)
        else if (ch == kb->edit_paste || ch == KEY_F(5)) {
            if (g_editor_clipboard && *g_editor_clipboard) {
                editor_undo_record(&um, &text_buffer, cursor_line, cursor_col, start_line);
                if (g_sel_active) {
                    int s_line = g_sel_anchor_line;
                    int s_col = g_sel_anchor_col;
                    int e_line = g_sel_end_line;
                    int e_col = g_sel_end_col;
                    normalize_selection(&s_line, &s_col, &e_line, &e_col);
                    delete_selection(&text_buffer);
                    cursor_line = s_line;
                    cursor_col = s_col;
                    g_sel_active = false;
                    editor_dirty = true;
                }
                insert_text_at_cursor(&text_buffer, &cursor_line, &cursor_col, g_editor_clipboard);
                editor_dirty = true;
            }
        }
        // Ctrl+Up (vertical Ctrl+Left: go to line above, then previous word)
        else if (ch == CTRL_UP_CODE || ch == 566 || ch == 567) {
            g_sel_active = false;

            if (cursor_line > 0) {
                int old_col = cursor_col;
                cursor_line--;
                const char *line = text_buffer.lines[cursor_line] ? text_buffer.lines[cursor_line] : "";
                cursor_col = word_left_col(line, old_col);
            } else {
                const char *line = text_buffer.lines[cursor_line] ? text_buffer.lines[cursor_line] : "";
                cursor_col = word_left_col(line, cursor_col);
            }
        }
        // Ctrl+Down (vertical Ctrl+Right: go to line below, then next word)
        else if (ch == CTRL_DOWN_CODE || ch == 525 || ch == 526) {
            g_sel_active = false;

            if (cursor_line < text_buffer.num_lines - 1) {
                int old_col = cursor_col;
                cursor_line++;
                const char *line = text_buffer.lines[cursor_line] ? text_buffer.lines[cursor_line] : "";
                cursor_col = word_right_col(line, old_col);
            } else {
                const char *line = text_buffer.lines[cursor_line] ? text_buffer.lines[cursor_line] : "";
                cursor_col = word_right_col(line, cursor_col);
            }
        }
        // Ctrl+Left (jump to previous word)
        else if (ch == CTRL_LEFT_CODE || ch == 545 || ch == 546) {
            g_sel_active = false;
            const char *line = text_buffer.lines[cursor_line] ? text_buffer.lines[cursor_line] : "";
            cursor_col = word_left_col(line, cursor_col);
        }
        // Ctrl+Right (jump to next word)
        else if (ch == CTRL_RIGHT_CODE || ch == 560 || ch == 561) {
            g_sel_active = false;
            const char *line = text_buffer.lines[cursor_line] ? text_buffer.lines[cursor_line] : "";
            cursor_col = word_right_col(line, cursor_col);
        }
        // 7) Enter / new line
        else if (ch == '\n') {
            editor_undo_record(&um, &text_buffer, cursor_line, cursor_col, start_line);
            g_sel_active = false;
            char *current_line = text_buffer.lines[cursor_line];
            //int line_len = (int)strlen(current_line);

            // Split the line at cursor_col
            char *new_line = strdup(current_line + cursor_col);
            if (!new_line) {
                mvwprintw(notification_window, 1, 2, "Memory allocation error");
                wrefresh(notification_window);
                continue;
            }
            current_line[cursor_col] = '\0';

            // Realloc if needed
            if (text_buffer.num_lines >= text_buffer.capacity) {
                text_buffer.capacity *= 2;
                char **tmp = realloc(text_buffer.lines, 
                                  sizeof(char*) * text_buffer.capacity);
                if (!tmp) {
                    mvwprintw(notification_window, 1, 2, "Memory allocation error");
                    wrefresh(notification_window);
                    fclose(file);
                    return;
                }
                text_buffer.lines = tmp;
            }

            // Shift lines down
            for (int i = text_buffer.num_lines; i > cursor_line + 1; i--) {
                text_buffer.lines[i] = text_buffer.lines[i - 1];
            }
            text_buffer.lines[cursor_line + 1] = new_line;
            text_buffer.num_lines++;

            // Move cursor to new line
            cursor_line++;
            cursor_col = 0;
            editor_dirty = true;
        }
        // 8) Backspace
        else if (ch == kb->edit_backspace) {
            if (cursor_col > 0) {
                editor_undo_record(&um, &text_buffer, cursor_line, cursor_col, start_line);
                char *current_line = text_buffer.lines[cursor_line];
                memmove(&current_line[cursor_col - 1],
                        &current_line[cursor_col],
                        strlen(current_line) - cursor_col + 1);
                cursor_col--;
                editor_dirty = true;
            }
            else if (cursor_line > 0) {
                editor_undo_record(&um, &text_buffer, cursor_line, cursor_col, start_line);
                // Merge current line with previous line
                int prev_len = strlen(text_buffer.lines[cursor_line - 1]);
                int curr_len = strlen(text_buffer.lines[cursor_line]);

                char *new_line = realloc(
                    text_buffer.lines[cursor_line - 1], prev_len + curr_len + 1
                );
                if (!new_line) {
                    mvwprintw(notification_window, 1, 2, "Memory allocation error");
                    wrefresh(notification_window);
                    continue; // Skip this operation if realloc fails
                }
                text_buffer.lines[cursor_line - 1] = new_line;
                strcat(text_buffer.lines[cursor_line - 1], text_buffer.lines[cursor_line]);
                free(text_buffer.lines[cursor_line]);

                // Shift lines up
                for (int i = cursor_line; i < text_buffer.num_lines - 1; i++) {
                    text_buffer.lines[i] = text_buffer.lines[i + 1];
                }
                text_buffer.num_lines--;

                cursor_line--;
                cursor_col = prev_len;
                editor_dirty = true;
            }
        }
        // 9) Possibly an extra "exit edit" keystroke
        else if (ch == kb->edit_quit) {
            exit_edit_mode = true;
        }
        // 10) Printable characters
        else if (ch >= 32 && ch <= 126) {
            editor_undo_record(&um, &text_buffer, cursor_line, cursor_col, start_line);
            g_sel_active = false;
            char *curr_line = text_buffer.lines[cursor_line];
            int line_len = (int)strlen(curr_line);

            // Expand current line by 1 char
            char *new_line = realloc(curr_line, line_len + 2);
            if (!new_line) {
                mvwprintw(notification_window, 1, 2, "Memory allocation error");
                wrefresh(notification_window);
                continue; // Skip this operation if realloc fails
            }
            curr_line = new_line;
            memmove(&curr_line[cursor_col + 1],
                    &curr_line[cursor_col],
                    line_len - cursor_col + 1);
            curr_line[cursor_col] = (char)ch;
            text_buffer.lines[cursor_line] = curr_line;
            cursor_col++;
            editor_dirty = true;
        }

        // Update global cursor position for API access
        g_editor_cursor_line = cursor_line;
        g_editor_cursor_col = cursor_col;
        if (editor_dirty) g_editor_dirty = true;

        // Re-render after any key
        render_text_buffer(editor_window, &text_buffer, &start_line, cursor_line, cursor_col);
    }

    // Cleanup
    is_editing = 0;  // Reset editing flag when exiting editor
    g_editor_path[0] = '\0';
    g_editor_buffer = NULL;
    g_editor_dirty = false;
    fclose(file);
    curs_set(0);

    // Restore terminal mode after entering editor.
    editor_disable_mouse_drag_reporting();
    noraw();
    cbreak();
    
    // Clear the editor window area before deleting it
    pthread_mutex_lock(&banner_mutex);
    werase(editor_window);
    wrefresh(editor_window);
    delwin(editor_window);
    
    // Clear the area where the editor was to remove any leftover text
    // We need to clear the main content area (banner_height to LINES - notif_height)
    int clear_start_y = banner_height;
    int clear_height = LINES - banner_height - notif_height;
    
    // Create a temporary window to clear the editor area
    WINDOW *clear_win = newwin(clear_height, COLS, clear_start_y, 0);
    if (clear_win) {
        werase(clear_win);
        wrefresh(clear_win);
        delwin(clear_win);
    }
    
    // Force a full screen refresh to ensure everything is redrawn
    // Trigger a resize event so the main loop will redraw all windows properly
    resized = 1;
    
    refresh();
    clear();
    refresh();
    pthread_mutex_unlock(&banner_mutex);

    for (int i = 0; i < text_buffer.num_lines; i++) {
        free(text_buffer.lines[i]);
    }
    free(text_buffer.lines);

    editor_stack_clear(um.undo, &um.undo_len);
    editor_stack_clear(um.redo, &um.redo_len);
}


/**
 * Checks if the given file has a supported MIME type.
 *
 * @param filename The name of the file to check.
 * @return true if the file has a supported MIME type, false otherwise.
 */
bool is_supported_file_type(const char *filename) {
    magic_t magic_cookie;
    bool supported = false;

    // Open magic cookie with additional flags for better MIME detection
    magic_cookie = magic_open(MAGIC_MIME_TYPE | MAGIC_SYMLINK | MAGIC_CHECK);
    if (magic_cookie == NULL) {
        fprintf(stderr, "Unable to initialize magic library\n");
        return false;
    }

    // Load the default magic database
    if (magic_load(magic_cookie, NULL) != 0) {
        fprintf(stderr, "Cannot load magic database: %s\n", magic_error(magic_cookie));
        magic_close(magic_cookie);
        return false;
    }

    // Get the MIME type of the file
    const char *mime_type = magic_file(magic_cookie, filename);
    if (mime_type == NULL) {
        fprintf(stderr, "Could not determine file type: %s\n", magic_error(magic_cookie));
        magic_close(magic_cookie);
        return false;
    }

    // Also check file extension for .js files specifically
    const char *ext = strrchr(filename, '.');
    if (ext && strcmp(ext, ".js") == 0) {
        supported = true;
    } else {
        // Check if MIME type is in supported types
        for (size_t i = 0; i < num_supported_mime_types; i++) {
            if (strncmp(mime_type, supported_mime_types[i], strlen(supported_mime_types[i])) == 0) {
                supported = true;
                break;
            }
        }
    }

    magic_close(magic_cookie);
    return supported;
}

/**
 * Check if a file is an archive that we can preview.
 * 
 * @param filename The filename to check
 * @return true if the file is a supported archive, false otherwise
 */
bool is_archive_file(const char *filename) {
    if (!filename) {
        return false;
    }
    
    size_t len = strlen(filename);
    if (len == 0) {
        return false;
    }
    
    const char *ext = strrchr(filename, '.');
    if (!ext) {
        return false;
    }
    
    // Check for ZIP/7z archives
    if (strcmp(ext, ".zip") == 0 || strcmp(ext, ".7z") == 0) {
        return true;
    }
    
    // Check for TAR archives (with or without compression)
    // Note: strrchr finds the LAST dot, so for .tar.gz it finds .gz
    // We need to check for multi-part extensions
    
    // Check single extensions first
    if (strcmp(ext, ".tar") == 0 ||
        strcmp(ext, ".tgz") == 0 ||
        strcmp(ext, ".tbz2") == 0 ||
        strcmp(ext, ".txz") == 0) {
        return true;
    }
    
    // Check for multi-part extensions like .tar.gz, .tar.bz2, .tar.xz
    // For these, strrchr will find .gz/.bz2/.xz, so check if .tar comes before
    if (strcmp(ext, ".gz") == 0 || strcmp(ext, ".bz2") == 0 || strcmp(ext, ".xz") == 0) {
        // Check if it contains .tar before the compression extension
        const char *tar_pos = strstr(filename, ".tar");
        if (tar_pos && tar_pos < ext) {
            return true; // It's a TAR archive (e.g., .tar.gz, .tar.bz2)
        }
        // If no .tar, it's a single compressed file (also treat as "archive" for preview)
        return true;
    }
    
    return false;
}

/**
 * Display archive contents in the preview window.
 * 
 * @param window The preview window
 * @param file_path Path to the archive file
 * @param start_line Starting line for scrolling
 * @param max_y Maximum Y coordinate
 * @param max_x Maximum X coordinate
 */
void display_archive_preview(WINDOW *window, const char *file_path, int start_line, int max_y, int max_x) {
    // Check if this is a single compressed file (not a TAR archive)
    bool is_single_compressed = false;
    const char *ext = strrchr(file_path, '.');
    if (ext) {
        if (strcmp(ext, ".gz") == 0 || strcmp(ext, ".bz2") == 0 || strcmp(ext, ".xz") == 0) {
            // Check if it's NOT a .tar.gz, .tar.bz2, etc.
            const char *tar_pos = strstr(file_path, ".tar");
            if (!tar_pos || tar_pos >= ext) {
                is_single_compressed = true;
            }
        }
    }
    
    ArcReader *reader = arc_open_path(file_path);
    if (!reader) {
        // Show more detailed error message
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Unable to open archive: %s (errno: %s)", 
                 file_path, strerror(errno));
        mvwprintw(window, 7, 2, "%.*s", max_x - 4, error_msg);
        return;
    }
    
    // Display archive header (start after MIME type which is on line 5)
    // For single compressed files, show a different header
    if (is_single_compressed) {
        mvwprintw(window, 6, 2, "📦 Compressed File Contents:");
    } else {
        mvwprintw(window, 6, 2, "📦 Archive Contents:");
    }
    mvwprintw(window, 7, 2, "─────────────────────────────────────");
    
    ArcEntry entry;
    int line_num = 8;
    int current_line = 0;
    int entry_count = 0;
    const int max_entries = 1000; // Limit entries to prevent UI overload
    
    // Skip entries until start_line (accounting for header lines)
    int header_lines = 2; // "Archive Contents:" and separator line
    int adjusted_start_line = start_line > header_lines ? start_line - header_lines : 0;
    
    while (current_line < adjusted_start_line && entry_count < max_entries) {
        int ret = arc_next(reader, &entry);
        if (ret != 0) {
            break; // Done or error
        }
        arc_entry_free(&entry);
        current_line++;
        entry_count++;
    }
    
    // Display entries
    bool found_any = false;
    while (line_num < max_y - 1 && entry_count < max_entries) {
        int ret = arc_next(reader, &entry);
        if (ret != 0) {
            if (ret < 0) {
                // Error reading - for single compressed files, don't show technical error

                // For compressed files, silently fail (they're not really archives)
            } else if (ret == 1 && !found_any) {
                // EOF but no entries found
                if (!is_single_compressed) {
                    mvwprintw(window, line_num++, 2, "Archive appears empty (may be corrupted or invalid)");
                }
                // For compressed files, silently fail (they're not really archives)
            }
            break; // Done
        }
        
        found_any = true;
        
        // Check if entry has a valid path
        if (!entry.path) {
            // Skip entries without paths (shouldn't happen, but be safe)
            arc_entry_free(&entry);
            entry_count++;
            continue;
        }
        
        // Format entry display
        char entry_line[256];
        const char *type_icon = "📄";
        if (entry.type == ARC_ENTRY_DIR) {
            type_icon = "📁";
        } else if (entry.type == ARC_ENTRY_SYMLINK) {
            type_icon = "🔗";
        } else if (entry.type == ARC_ENTRY_HARDLINK) {
            type_icon = "🔗";
        }
        
        // Format size
        char size_str[20];
        if (entry.type == ARC_ENTRY_DIR) {
            snprintf(size_str, sizeof(size_str), "<DIR>");
        } else if (entry.size == 0) {
            // Unknown size (e.g., compressed single files like .gz, .bz2)
            // For compressed files, size is unknown until decompressed
            // (or if ISIZE is available in gzip metadata)
            snprintf(size_str, sizeof(size_str), "?");
        } else {
            format_file_size(size_str, entry.size);
        }
        
        // Build display line
        if (entry.type == ARC_ENTRY_SYMLINK && entry.link_target) {
            snprintf(entry_line, sizeof(entry_line), "%s %-40s %10s -> %s",
                     type_icon, entry.path ? entry.path : "(null)", size_str, entry.link_target);
        } else {
            snprintf(entry_line, sizeof(entry_line), "%s %-40s %10s",
                     type_icon, entry.path ? entry.path : "(null)", size_str);
        }
        
        // Truncate if too long
        int max_display_width = max_x - 4;
        if ((int)strlen(entry_line) > max_display_width) {
            entry_line[max_display_width] = '\0';
        }
        
        mvwprintw(window, line_num++, 2, "%.*s", max_display_width, entry_line);
        
        arc_entry_free(&entry);
        entry_count++;
    }
    
    if (entry_count >= max_entries) {
        mvwprintw(window, line_num++, 2, "... (showing first %d entries)", max_entries);
    }
    
    arc_close(reader);
}
