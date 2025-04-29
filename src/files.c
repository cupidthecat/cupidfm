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
#include <ncurses.h>               // for WINDOW, mvwprintw
#include <stdbool.h>               // for bool, true, false
#include <limits.h>                // For PATH_MAX
#include <fcntl.h>                 // For O_RDONLY
#include <magic.h>                 // For libmagic
#include <time.h>

// Local includes
#include "main.h"                  // for FileAttr, Vector, Vector_add, Vector_len, Vector_set_len
#include "utils.h"                 // for path_join, is_directory, get_symlink_target
#include "files.h"                 // for FileAttributes, FileAttr, MAX_PATH_LENGTH
#include "globals.h"
#include "config.h"
#include "virtualfs.h"

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
                char full_path[MAX_PATH_LENGTH];
                path_join(full_path, name, entry->d_name);

                bool is_dir = is_directory(name, entry->d_name);
                
                // Check if it's a symlink
                char symlink_target[MAX_PATH_LENGTH] = {0};
                bool is_symlink = get_symlink_target(full_path, symlink_target, sizeof(symlink_target));
                
                // Create the display name
                char display_name[MAX_PATH_LENGTH * 2] = {0};
                if (is_symlink) {
                    snprintf(display_name, sizeof(display_name), "%s → %s", entry->d_name, symlink_target);
                } else {
                    strncpy(display_name, entry->d_name, sizeof(display_name));
                }

                FileAttr file_attr = mk_attr(display_name, is_dir, entry->d_ino);

                if (file_attr != NULL) {
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
    struct statfs sfs;
    
    // First check filesystem type
    if (statfs(dir_path, &sfs) == 0) {
        if (is_virtual_fstype(sfs.f_type)) {
            return 0;
        }
    }
    
    // Fallback path check for known virtual FS roots
    if (strcmp(dir_path, "/proc") == 0 || 
        strcmp(dir_path, "/sys") == 0 || 
        strcmp(dir_path, "/dev") == 0) {
        return 0;
    }

    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    long total_size = 0;
    const long MAX_SIZE_THRESHOLD = 1000L * 1024 * 1024 * 1024 * 1024; // 1000 TiB

    if (!(dir = opendir(dir_path)))
        return -1;

    while ((entry = readdir(dir)) != NULL) {
        char path[MAX_PATH_LENGTH];
        if (strlen(dir_path) + strlen(entry->d_name) + 1 < sizeof(path)) {
            snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        } else {
            fprintf(stderr, "Path length exceeds buffer size for %s/%s\n", dir_path, entry->d_name);
            continue;
        }
        if (lstat(path, &statbuf) == -1)
            continue;
        if (S_ISDIR(statbuf.st_mode)) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;
            long dir_size = get_directory_size(path);
            if (dir_size == -2) {
                closedir(dir);
                return -2;
            }
            if (dir_size == -1)
                continue;
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

    if (stat(file_path, &file_stat) == -1) {
        mvwprintw(window, 2, 2, "Unable to retrieve file information");
        return;
    }

    int label_width = g_kb.info_label_width;

    if (S_ISDIR(file_stat.st_mode)) {
        struct statfs sfs;
        bool virtual_fs = (statfs(file_path, &sfs) == 0 && is_virtual_fstype(sfs.f_type));

        if (virtual_fs) {
            // Don't try to size a pseudo–FS
            mvwprintw(window, 2, 2, "%-*s %s",
                       label_width, "📁 Directory Size:", "-");
        } else {
            long dir_size = get_directory_size(file_path);
            if (dir_size < 0) {
                mvwprintw(window, 2, 2, "%-*s %s",
                           label_width, "📁 Directory Size:", "Uncalculable");
            } else {
                char fileSizeStr[20];
                format_file_size(fileSizeStr, (size_t)dir_size);
                mvwprintw(window, 2, 2, "%-*s %s",
                           label_width, "📁 Directory Size:", fileSizeStr);
            }
        }
    } else {
        char fileSizeStr[20];
        format_file_size(fileSizeStr, file_stat.st_size);
        mvwprintw(window, 2, 2, "%-*s %s",
                   label_width, "📏 File Size:", fileSizeStr);
    }

    // MIME type display follows unchanged...
    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE | MAGIC_SYMLINK | MAGIC_CHECK);
    if (!magic_cookie) {
        mvwprintw(window, 5, 2, "%-*s %s",
                   label_width, "📂 MIME type:", "Error initializing magic");
        return;
    }
    if (magic_load(magic_cookie, NULL) != 0) {
        mvwprintw(window, 5, 2, "%-*s %s",
                   label_width, "📂 MIME type:", magic_error(magic_cookie));
        magic_close(magic_cookie);
        return;
    }
    const char *mime_type = magic_file(magic_cookie, file_path);
    const char *emoji     = get_file_emoji(mime_type, file_path);

    if (!mime_type) {
        mvwprintw(window, 5, 2, "%-*s %s %s",
                   label_width, "❓", "MIME type:", "Unknown");
    } else {
        size_t value_width = (size_t)(max_x - 2 - label_width - 1);
        if (strlen(mime_type) > value_width) {
            char truncated[value_width+1];
            strncpy(truncated, mime_type, value_width);
            truncated[value_width] = '\0';
            mvwprintw(window, 5, 2, "%-*s %s %s",
                       label_width, emoji, "MIME type:", truncated);
        } else {
            mvwprintw(window, 5, 2, "%-*s %s %s",
                       label_width, emoji, "MIME type:", mime_type);
        }
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

    // Modified section: Ensure we don't scroll past the end of content
    int max_visible_line = buffer->num_lines - content_height;
    if (max_visible_line < 0) max_visible_line = 0;
    if (*start_line > max_visible_line) {
        *start_line = max_visible_line;
    }

    // Draw separator line for line numbers
    for (int i = 1; i < max_y - 1; i++) {
        mvwaddch(window, i, label_width + 1, ACS_VLINE);
    }

    // Calculate the width available for text content
    int content_width = max_x - label_width - 4;  // Subtract borders and line number area

    // Calculate horizontal scroll position to keep cursor visible
    static int h_scroll = 0;
    const int scroll_margin = 5;  // Number of columns to keep visible on either side

    // Adjust horizontal scroll if cursor would be outside visible area
    if (cursor_col >= h_scroll + content_width - scroll_margin) {
        h_scroll = cursor_col - content_width + scroll_margin + 1;
    } else if (cursor_col < h_scroll + scroll_margin) {
        h_scroll = MAX(0, cursor_col - scroll_margin);
    }

    // Display line numbers and content
    for (int i = 0; i < content_height; i++) {  // Changed condition
        int line_index = *start_line + i;
        if (line_index >= buffer->num_lines) break;

        // Print line number (right-aligned in its column)
        mvwprintw(window, i + 1, 2, "%*d", label_width - 1, line_index + 1);

        // Calculate the content start position
        int content_start = label_width + 3;

        // Get the line content
        const char *line = buffer->lines[line_index] ? buffer->lines[line_index] : "";
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
        if (line_index == cursor_line) {
            char cursor_char = ' ';
            if (cursor_col < (int)strlen(line)) {
                cursor_char = line[cursor_col];
            }
            
            // Adjust cursor position for horizontal scroll
            wmove(window, i + 1, content_start + (cursor_col - h_scroll));
            wattron(window, A_REVERSE);
            waddch(window, cursor_char);
            wattroff(window, A_REVERSE);
        }
    }

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

    // Clear the window before editing
    pthread_mutex_lock(&banner_mutex);
    werase(window);
    box(window, 0, 0);
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

    curs_set(1);        // Show cursor
    keypad(window, TRUE);
    wtimeout(window, 10);  // Non-blocking input

    bool exit_edit_mode = false;

    while (!exit_edit_mode) {
        int ch = wgetch(window);
        if (ch == ERR) {
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
        render_text_buffer(window, &text_buffer, &start_line, cursor_line, cursor_col);
    }

    // Cleanup
    fclose(file);
    curs_set(0);
    wtimeout(window, -1);

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
