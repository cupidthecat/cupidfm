// File: files.c
// -----------------------
#define _POSIX_C_SOURCE 200112L    // for strdup
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
#include <limits.h>                // For PATH_MAX
#include <fcntl.h>                 // For O_RDONLY
#include <magic.h>                 // For libmagic
#include <time.h>
#include <sys/ioctl.h>             // For ioctl, TIOCGWINSZ, struct winsize

// Local includes
#include "main.h"                  // for FileAttr, Vector, Vector_add, Vector_len, Vector_set_len
#include "utils.h"                 // for path_join, is_directory
#include "files.h"                 // for FileAttributes, FileAttr, MAX_PATH_LENGTH
#include "globals.h"
#include "config.h"
#define MIN_INT_SIZE_T(x, y) (((size_t)(x) > (y)) ? (y) : (x))
#define FILES_BANNER_UPDATE_INTERVAL 50000 // 50ms in microseconds

// Supported MIME types
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

// Recursive function to calculate directory size
// NOTE: this function may take long, it might be better to have the size of
//       the directories displayed as "-" until we have a value. Use of "du" or
//       another already existent tool might be better. The sizes should
//       probably be cached.
//       fork() -> exec() (if using du)
//       fork() -> calculate directory size and return it somehow (maybe print
//       as binary to the stdout)
long get_directory_size(const char *dir_path) {
    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    long total_size = 0;
    const long MAX_SIZE_THRESHOLD = 1000L * 1024 * 1024 * 1024 * 1024; // 1000 TiB

    // Skip virtual/special filesystems that don't have real sizes
    if (strncmp(dir_path, "/proc", 5) == 0 ||
        strncmp(dir_path, "/sys", 4) == 0 ||
        strncmp(dir_path, "/dev", 4) == 0 ||
        strncmp(dir_path, "/run", 4) == 0) {
        return -2;  // Return "too large" indicator for virtual filesystems
    }

    if (!(dir = opendir(dir_path)))
        return -1;

    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".." entries to avoid infinite recursion and incorrect calculations
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char path[MAX_PATH_LENGTH];
        if (strlen(dir_path) + strlen(entry->d_name) + 1 < sizeof(path)) {
            snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        } else {
            // Handle the error, e.g., log a message or skip this entry
            fprintf(stderr, "Path length exceeds buffer size for %s/%s\n", dir_path, entry->d_name);
            continue;
        }
        if (lstat(path, &statbuf) == -1)
            continue;
        if (S_ISDIR(statbuf.st_mode)) {
            long dir_size = get_directory_size(path);
            if (dir_size == -2) {
                closedir(dir);
                return -2;
            }
            total_size += dir_size;
        } else {
            total_size += statbuf.st_size;
        }
        if (total_size > MAX_SIZE_THRESHOLD) {
            closedir(dir);
            return -2;
        }
    }

    closedir(dir);
    return total_size;
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

    // Attempt to retrieve file statistics
    if (stat(file_path, &file_stat) == -1) {
        mvwprintw(window, 2, 2, "Unable to retrieve file information");
        return;
    }

    // NEW: get from KeyBindings or from wherever you store it:
    int label_width = g_kb.info_label_width;  

    // Display File Size or Directory Size
   if (S_ISDIR(file_stat.st_mode)) {
        long dir_size = get_directory_size(file_path);
        
        char fileSizeStr[20] = "Virtual FS";
        if (dir_size == -1) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Error");
        } else if (dir_size != -2) {
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

        // If this is the cursor line, highlight the cursor position
        if ((*start_line + i) == cursor_line) {
            char cursor_char = ' ';
            if (cursor_col < (int)strlen(line)) {
                cursor_char = line[cursor_col];
            }
            
            // Adjust cursor position for horizontal scroll
            int cursor_x = content_start + (cursor_col - h_scroll);
            wattron(window, A_REVERSE);
            mvwaddch(window, i + 1, cursor_x, cursor_char);
            wattroff(window, A_REVERSE);
        }
    }

    // Hide the terminal cursor since we're using visual highlighting (A_REVERSE)
    // This prevents the cursor from appearing in the wrong location
    curs_set(0);
    
    wrefresh(window);
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
                           KeyBindings *kb)
{
    (void)window;  // Unused - we create a full-screen editor window instead
    is_editing = 1;

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
        close(fd);
        return;
    }
    
    // Clear and prepare the editor window
    pthread_mutex_lock(&banner_mutex);
    werase(editor_window);
    box(editor_window, 0, 0);
    pthread_mutex_unlock(&banner_mutex);

    TextBuffer text_buffer;
    text_buffer.capacity = 100; 
    text_buffer.num_lines = 0;
    text_buffer.lines = malloc(sizeof(char*) * text_buffer.capacity);

    // Read the file into our text buffer
    bool is_empty = true;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        is_empty = false;
        line[strcspn(line, "\n")] = '\0';
        // Replace tabs with spaces
        for (char *p = line; *p; p++) {
            if (*p == '\t') {
                *p = ' ';
            }
        }
        if (text_buffer.num_lines >= text_buffer.capacity) {
            text_buffer.capacity *= 2;
            text_buffer.lines = realloc(text_buffer.lines, 
                            sizeof(char*) * text_buffer.capacity);
            if (!text_buffer.lines) {
                mvwprintw(notification_window, 1, 2, "Memory allocation error");
                wrefresh(notification_window);
                fclose(file);
                return;
            }
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

    // Hide terminal cursor - we use visual highlighting instead
    curs_set(0);
    keypad(editor_window, TRUE);
    wtimeout(editor_window, 10);  // Non-blocking input

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
            if (!should_clear_notif && notif_time_diff >= NOTIFICATION_TIMEOUT_MS && notifwin) {
                pthread_mutex_lock(&banner_mutex);
                werase(notifwin);
                wrefresh(notifwin);
                pthread_mutex_unlock(&banner_mutex);
                should_clear_notif = true;
            }
            
            napms(10);
            continue;
        }

        is_editing = 0;

        // 1) Quit editing
        if (ch == kb->edit_quit) {
            break;
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
        }

        // 3) Move up
        else if (ch == kb->edit_up) {
            if (cursor_line > 0) {
                cursor_line--;
                if (cursor_col > (int)strlen(text_buffer.lines[cursor_line])) {
                    cursor_col = strlen(text_buffer.lines[cursor_line]);
                }
            }
        }
        // 4) Move down
        else if (ch == kb->edit_down) {
            if (cursor_line < text_buffer.num_lines - 1) {
                cursor_line++;
                if (cursor_col > (int)strlen(text_buffer.lines[cursor_line])) {
                    cursor_col = strlen(text_buffer.lines[cursor_line]);
                }
            }
        }
        // 5) Move left
        else if (ch == kb->edit_left) {
            if (cursor_col > 0) {
                cursor_col--;
            } else if (cursor_line > 0) {
                // Move up a line if user is at col=0
                cursor_line--;
                cursor_col = strlen(text_buffer.lines[cursor_line]);
            }
        }
        // 6) Move right
        else if (ch == kb->edit_right) {
            int line_len = (int)strlen(text_buffer.lines[cursor_line]);
            if (cursor_col < line_len) {
                cursor_col++;
            } else if (cursor_line < text_buffer.num_lines - 1) {
                // Move down a line if user is at end
                cursor_line++;
                cursor_col = 0;
            }
        }
        // 7) Enter / new line
        else if (ch == '\n') {
            char *current_line = text_buffer.lines[cursor_line];
            //int line_len = (int)strlen(current_line);

            // Split the line at cursor_col
            char *new_line = strdup(current_line + cursor_col);
            current_line[cursor_col] = '\0';

            // Realloc if needed
            if (text_buffer.num_lines >= text_buffer.capacity) {
                text_buffer.capacity *= 2;
                text_buffer.lines = realloc(text_buffer.lines, 
                                  sizeof(char*) * text_buffer.capacity);
                if (!text_buffer.lines) {
                    mvwprintw(notification_window, 1, 2, "Memory allocation error");
                    wrefresh(notification_window);
                    fclose(file);
                    return;
                }
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
        }
        // 8) Backspace
        else if (ch == kb->edit_backspace) {
            if (cursor_col > 0) {
                char *current_line = text_buffer.lines[cursor_line];
                memmove(&current_line[cursor_col - 1],
                        &current_line[cursor_col],
                        strlen(current_line) - cursor_col + 1);
                cursor_col--;
            }
            else if (cursor_line > 0) {
                // Merge current line with previous line
                int prev_len = strlen(text_buffer.lines[cursor_line - 1]);
                int curr_len = strlen(text_buffer.lines[cursor_line]);

                text_buffer.lines[cursor_line - 1] = realloc(
                    text_buffer.lines[cursor_line - 1], prev_len + curr_len + 1
                );
                strcat(text_buffer.lines[cursor_line - 1], text_buffer.lines[cursor_line]);
                free(text_buffer.lines[cursor_line]);

                // Shift lines up
                for (int i = cursor_line; i < text_buffer.num_lines - 1; i++) {
                    text_buffer.lines[i] = text_buffer.lines[i + 1];
                }
                text_buffer.num_lines--;

                cursor_line--;
                cursor_col = prev_len;
            }
        }
        // 9) Possibly an extra "exit edit" keystroke
        else if (ch == kb->edit_quit) {
            exit_edit_mode = true;
        }
        // 10) Printable characters
        else if (ch >= 32 && ch <= 126) {
            char *curr_line = text_buffer.lines[cursor_line];
            int line_len = (int)strlen(curr_line);

            // Expand current line by 1 char
            curr_line = realloc(curr_line, line_len + 2);
            memmove(&curr_line[cursor_col + 1],
                    &curr_line[cursor_col],
                    line_len - cursor_col + 1);
            curr_line[cursor_col] = (char)ch;
            text_buffer.lines[cursor_line] = curr_line;
            cursor_col++;
        }

        // Re-render after any key
        render_text_buffer(editor_window, &text_buffer, &start_line, cursor_line, cursor_col);
    }

    // Cleanup
    fclose(file);
    curs_set(0);
    
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
