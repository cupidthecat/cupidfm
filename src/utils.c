// File: utils.c
// -----------------------
#define _POSIX_C_SOURCE 200112L
#include <errno.h>     // for errno
#include <stdarg.h>    // for va_list, va_start, va_end
#include <stdio.h>     // for fprintf, stderr, vfprintf
#include <stdlib.h>    // for exit
#include <string.h>    // for strerror
#include <sys/wait.h>  // for WEXITSTATUS, WIFEXITED
#include <dirent.h>    // for DIR, struct dirent, opendir, readdir, closedir
#include <curses.h>    // for initscr, noecho, keypad, stdscr, clear, printw, refresh, mvaddch, getch, endwin
#include <unistd.h>    // for system
#include <sys/types.h> // for stat
#include <sys/stat.h>  // for stat, S_ISDIR
#include <ctype.h>     // for isprint
#include <libgen.h>    // for dirname() and basename()
#include <limits.h>    // for PATH_MAX
#include <time.h>      // for clock_gettime, CLOCK_MONOTONIC, struct timespec

// Local includes
#include "utils.h"
#include "files.h"  // Include the header for FileAttr and related functions
#include "globals.h"
#include "main.h"
#define MAX_DISPLAY_LENGTH 32

// Declare copied_filename as a global variable at the top of the file
char copied_filename[MAX_PATH_LENGTH] = "";
extern bool should_clear_notif; 

/**
 * Confirm deletion of a file or directory by prompting the user.
 *
 * @param path          The name of the file or directory to delete.
 * @param should_delete Pointer to a boolean that will be set to true if deletion is confirmed.
 * @return              true if deletion was confirmed, false otherwise.
 */
bool confirm_delete(const char *path, bool *should_delete) {
    *should_delete = false;
    
    // Get terminal size
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    
    // Define popup window size
    int popup_height = 5;
    int popup_width = 60;
    int starty = (max_y - popup_height) / 2;
    int startx = (max_x - popup_width) / 2;
    
    // Create the popup window
    WINDOW *popup = newwin(popup_height, popup_width, starty, startx);
    box(popup, 0, 0);
    
    // Display the confirmation message
    mvwprintw(popup, 1, 2, "Confirm Delete:");
    mvwprintw(popup, 2, 2, "'%s' (Y to confirm, N or ESC to cancel)", path);
    wrefresh(popup);
    
    // Make input non-blocking to allow banner updates
    wtimeout(popup, 10);
    
    // Initialize time-based update tracking
    // banner_offset is now a global variable - no need for static
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = COLS + (BANNER_TEXT ? strlen(BANNER_TEXT) : 0) + (BUILD_INFO ? strlen(BUILD_INFO) : 0) + 4;
    
    // Capture user input
    int ch;
    while (1) {
        ch = wgetch(popup);
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
            
            napms(10);
            continue;
        }
        ch = tolower(ch);
        if (ch == 'y') {
            *should_delete = true;
            break;
        } else if (ch == 'n' || ch == 27) { // 27 = ESC key
            break;
        }
    }
    
    wtimeout(popup, -1); // Restore blocking input
    
    // Clear and delete the popup window
    werase(popup);
    wrefresh(popup);
    delwin(popup);
    
    return *should_delete;
}

void die(int r, const char *format, ...) {
	fprintf(stderr, "The program used die()\n");
	fprintf(stderr, "The last errno was %d/%s\n", errno, strerror(errno));
	fprintf(stderr, "The user of die() decided to leave this message for "
			"you:\n");

	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	fprintf(stderr, "\nGood Luck.\n");

	exit(r);
}

void create_file(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (f == NULL)
        die(1, "Couldn't create file %s", filename);
    fclose(f);
}

void browse_files(const char *directory) {
    char command[256];
    snprintf(command, sizeof(command), "xdg-open %s", directory);

    int result = system(command);

    if (result == -1) {
        // Error launching the file manager
        printf("Error: Unable to open the file manager.\n");
    } else if (WIFEXITED(result) && WEXITSTATUS(result) != 0) {
        // The file manager exited with a non-zero status, indicating an issue
        printf("Error: The file manager returned a non-zero status.\n");
    }
}
/**
 * Function to display the contents of a directory in the terminal
 *
 * @param directory the directory to display
 */
void display_files(const char *directory) {
    DIR *dir;
    struct dirent *entry;

    if ((dir = opendir(directory)) == NULL) {
        die(1, "Couldn't open directory %s", directory);
        return ;
    }

    while ((entry = readdir(dir)) != NULL) {
        printf("%s\n", entry->d_name);
    }

    closedir(dir);
}
/**
 * Function to preview the contents of a file in the terminal
 *
 * @param filename the name of the file to preview
 */
void preview_file(const char *filename) {
    printf("Attempting to preview file: %s\n", filename);
    
    // First check if file exists and is readable
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("Error: Couldn't open file %s for preview\n", filename);
        printf("Errno: %d - %s\n", errno, strerror(errno));
        return;
    }

    // Initialize ncurses
    initscr();
    start_color();
    noecho();
    keypad(stdscr, TRUE);
    raw();
    
    int ch;
    int row = 0;
    int col = 0;
    int max_rows, max_cols;
    
    getmaxyx(stdscr, max_rows, max_cols);
    
    // Clear screen and show header
    clear();
    attron(A_BOLD | A_REVERSE);
    printw("File Preview: %s", filename);
    attroff(A_BOLD | A_REVERSE);
    printw("\nPress 'q' to exit, arrow keys to scroll\n\n");
    refresh();
    
    row = 3;
    
    // Read and display file contents
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        col = 0;
        for (int i = 0; buffer[i] != '\0' && col < max_cols - 1; i++) {
            if (buffer[i] == '\t') {
                // Handle tabs
                for (int j = 0; j < 4 && col < max_cols - 1; j++) {
                    mvaddch(row, col++, ' ');
                }
            } else if (isprint(buffer[i]) || buffer[i] == '\n') {
                // Handle printable characters
                mvaddch(row, col++, buffer[i]);
            } else {
                // Handle non-printable characters
                mvaddch(row, col++, '?');
            }
        }
        row++;
        
        if (row >= max_rows - 1) {
            break;
        }
    }
    
    refresh();
    
    // Wait for 'q' to exit
    while ((ch = getch()) != 'q');
    
    endwin();
    fclose(file);
}
/**
 * Function to change the current directory and update the list of files
 *
 * @param new_directory the new directory to change to
 * @param files the list of files in the directory
 * @param num_files the number of files in the directory
 * @param selected_entry the index of the selected entry
 * @param start_entry the index of the first entry displayed
 * @param end_entry the index of the last entry displayed
 */
bool is_directory(const char *path, const char *filename) {
    struct stat path_stat;
    char full_path[MAX_PATH_LENGTH];
    snprintf(full_path, sizeof(full_path), "%s/%s", path, filename);

    if (stat(full_path, &path_stat) == 0)
        return S_ISDIR(path_stat.st_mode);

    return false; // Correct: Do not assume it's a directory if stat fails
}

/** Function to join two paths
 *
 * @param result the resulting path
 * @param base the base path
 * @param extra the extra path
 */
void path_join(char *result, const char *base, const char *extra) {
    size_t base_len = strlen(base);
    size_t extra_len = strlen(extra);

    if (base_len == 0) {
        strncpy(result, extra, MAX_PATH_LENGTH);
    } else if (extra_len == 0) {
        strncpy(result, base, MAX_PATH_LENGTH);
    } else {
        if (base[base_len - 1] == '/') {
            snprintf(result, MAX_PATH_LENGTH, "%s%s", base, extra);
        } else {
            snprintf(result, MAX_PATH_LENGTH, "%s/%s", base, extra);
        }
    }

    result[MAX_PATH_LENGTH - 1] = '\0';
}

/**
 * Function to get an emoji based on the file's MIME type.
 *
 * @param mime_type The MIME type of the file.
 * @return A string representing the emoji.
 */
const char* get_file_emoji(const char *mime_type, const char *filename) {
    if (mime_type == NULL) {
        return "📄";
    }

    // Check for specific MIME types first
    if (strncmp(mime_type, "text/", 5) == 0) {
        if (strstr(mime_type, "python")) return "🐍";
        if (strstr(mime_type, "javascript")) return "📜";
        if (strstr(mime_type, "html")) return "🌐";
        if (strstr(mime_type, "css")) return "🎨";
        if (strstr(mime_type, "x-c")) return "📝";
        if (strstr(mime_type, "x-java")) return "☕";
        if (strstr(mime_type, "x-shellscript")) return "💻";
        if (strstr(mime_type, "x-rust")) return "🦀";
        if (strstr(mime_type, "markdown")) return "📘";
        if (strstr(mime_type, "csv")) return "📊";
        if (strstr(mime_type, "x-perl")) return "🐪";
        if (strstr(mime_type, "x-ruby")) return "💎";
        if (strstr(mime_type, "x-php")) return "🐘";
        if (strstr(mime_type, "x-go")) return "🐹";
        if (strstr(mime_type, "x-swift")) return "🦅";
        if (strstr(mime_type, "x-kotlin")) return "🎯";
        if (strstr(mime_type, "x-scala")) return "⚡";
        if (strstr(mime_type, "x-haskell")) return "λ";
        if (strstr(mime_type, "x-lua")) return "🌙";
        if (strstr(mime_type, "x-r")) return "📊";
        
        // Data formats
        if (strstr(mime_type, "json")) return "🔣";
        if (strstr(mime_type, "xml")) return "📑";
        if (strstr(mime_type, "yaml")) return "📋";
        if (strstr(mime_type, "toml")) return "⚙️";
        if (strstr(mime_type, "ini")) return "🔧";
    }

    // Fallback to extension-based detection if MIME type is "text/plain"
    if (strcmp(mime_type, "text/plain") == 0) {
        const char *ext = strrchr(filename, '.');
        if (ext) {
            if (strcmp(ext, ".js") == 0) return "📜";
            if (strcmp(ext, ".py") == 0) return "🐍";
            if (strcmp(ext, ".html") == 0) return "🌐";
            if (strcmp(ext, ".css") == 0) return "🎨";
            if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) return "📝";
            if (strcmp(ext, ".java") == 0) return "☕";
            if (strcmp(ext, ".sh") == 0) return "💻";
            if (strcmp(ext, ".rs") == 0) return "🦀";
            if (strcmp(ext, ".md") == 0) return "📘";
            if (strcmp(ext, ".csv") == 0) return "📊";
            if (strcmp(ext, ".pl") == 0) return "🐪";
            if (strcmp(ext, ".rb") == 0) return "💎";
            if (strcmp(ext, ".php") == 0) return "🐘";
            if (strcmp(ext, ".go") == 0) return "🐹";
            if (strcmp(ext, ".swift") == 0) return "🦅";
            if (strcmp(ext, ".kt") == 0) return "🎯";
            if (strcmp(ext, ".scala") == 0) return "⚡";
            if (strcmp(ext, ".hs") == 0) return "λ";
            if (strcmp(ext, ".lua") == 0) return "🌙";
            if (strcmp(ext, ".r") == 0) return "📊";
            if (strcmp(ext, ".json") == 0) return "🔣";
            if (strcmp(ext, ".xml") == 0) return "📑";
            if (strcmp(ext, ".yaml") == 0 || strcmp(ext, ".yml") == 0) return "📋";
            if (strcmp(ext, ".toml") == 0) return "⚙️";
            if (strcmp(ext, ".ini") == 0) return "🔧";
        }
    }

    // Images
    if (strncmp(mime_type, "image/", 6) == 0) {
        if (strstr(mime_type, "gif")) return "🎭";
        if (strstr(mime_type, "svg")) return "✨";
        if (strstr(mime_type, "png")) return "🖼️ ";
        if (strstr(mime_type, "jpeg") || strstr(mime_type, "jpg")) return "📸";
        if (strstr(mime_type, "webp")) return "🌅";
        if (strstr(mime_type, "tiff")) return "📷";
        if (strstr(mime_type, "bmp")) return "🎨";
        if (strstr(mime_type, "ico")) return "🎯";
        return "🖼️";
    }

    // Audio
    if (strncmp(mime_type, "audio/", 6) == 0) {
        if (strstr(mime_type, "midi")) return "🎹";
        if (strstr(mime_type, "mp3")) return "🎵";
        if (strstr(mime_type, "wav")) return "🔊";
        if (strstr(mime_type, "ogg")) return "🎼";
        if (strstr(mime_type, "flac")) return "🎶";
        if (strstr(mime_type, "aac")) return "🔉";
        return "🎵";
    }

    // Video
    if (strncmp(mime_type, "video/", 6) == 0) {
        if (strstr(mime_type, "mp4")) return "🎥";
        if (strstr(mime_type, "avi")) return "📽️";
        if (strstr(mime_type, "mkv")) return "🎬";
        if (strstr(mime_type, "webm")) return "▶️";
        if (strstr(mime_type, "mov")) return "🎦";
        if (strstr(mime_type, "wmv")) return "📹";
        return "🎞️";
    }

    // Applications
    if (strncmp(mime_type, "application/", 12) == 0) {
        // Archives
        if (strstr(mime_type, "zip")) return "📦";
        if (strstr(mime_type, "x-tar")) return "📦";
        if (strstr(mime_type, "x-rar")) return "📦";
        if (strstr(mime_type, "x-7z")) return "📦";
        if (strstr(mime_type, "gzip")) return "📦";
        if (strstr(mime_type, "x-bzip")) return "📦";
        if (strstr(mime_type, "x-xz")) return "📦";
        if (strstr(mime_type, "x-compress")) return "📦";

        // Documents
        if (strstr(mime_type, "pdf")) return "📕";
        if (strstr(mime_type, "msword")) return "📝";
        if (strstr(mime_type, "vnd.ms-excel")) return "📊";
        if (strstr(mime_type, "vnd.ms-powerpoint")) return "📊";
        if (strstr(mime_type, "vnd.oasis.opendocument.text")) return "📃";
        if (strstr(mime_type, "rtf")) return "📄";
        if (strstr(mime_type, "epub")) return "📚";
        if (strstr(mime_type, "js")) return "📜";

        // Data formats
        if (strstr(mime_type, "json")) return "🔣";
        if (strstr(mime_type, "xml")) return "📑";
        if (strstr(mime_type, "yaml")) return "📋";
        if (strstr(mime_type, "sql")) return "🗄️";
        
        // Executables and binaries
        if (strstr(mime_type, "x-executable")) return "⚙️";
        if (strstr(mime_type, "x-sharedlib")) return "🔧";
        if (strstr(mime_type, "x-object")) return "🔨";
        if (strstr(mime_type, "x-pie-executable")) return "🎯";
        if (strstr(mime_type, "x-dex")) return "🤖";
        if (strstr(mime_type, "java-archive")) return "☕";
        if (strstr(mime_type, "x-msdownload")) return "🪟";
    }

    // Font files
    if (strncmp(mime_type, "font/", 5) == 0) {
        if (strstr(mime_type, "ttf")) return "🔤";
        if (strstr(mime_type, "otf")) return "🔠";
        if (strstr(mime_type, "woff")) return "🔡";
        if (strstr(mime_type, "woff2")) return "🔣";
        return "🔤";
    }

    // Database files
    if (strstr(mime_type, "database") || strstr(mime_type, "sql")) {
        return "🗄️";
    }

    // Version control
    if (strstr(mime_type, "x-git")) {
        return "📥";
    }

    // Certificate files
    if (strstr(mime_type, "x-x509-ca-cert")) {
        return "🔐";
    }

    // Fallback to extension-based detection
    const char *ext = strrchr(filename, '.');
    if (ext) {
        if (strcmp(ext, ".py") == 0) return "🐍";
        if (strcmp(ext, ".js") == 0) return "📜";
        if (strcmp(ext, ".html") == 0) return "🌐";
        if (strcmp(ext, ".css") == 0) return "🎨";
        if (strcmp(ext, ".c") == 0 || strcmp(ext, ".h") == 0) return "📝";
        if (strcmp(ext, ".java") == 0) return "☕";
        if (strcmp(ext, ".sh") == 0) return "💻";
        if (strcmp(ext, ".rs") == 0) return "🦀";
        if (strcmp(ext, ".md") == 0) return "📘";
        if (strcmp(ext, ".csv") == 0) return "📊";
        if (strcmp(ext, ".pl") == 0) return "🐪";
        if (strcmp(ext, ".rb") == 0) return "💎";
        if (strcmp(ext, ".php") == 0) return "🐘";
        if (strcmp(ext, ".go") == 0) return "🐹";
        if (strcmp(ext, ".swift") == 0) return "🦅";
        if (strcmp(ext, ".kt") == 0) return "🎯";
        if (strcmp(ext, ".scala") == 0) return "⚡";
        if (strcmp(ext, ".hs") == 0) return "λ";
        if (strcmp(ext, ".lua") == 0) return "🌙";
        if (strcmp(ext, ".r") == 0) return "📊";
        if (strcmp(ext, ".json") == 0) return "🔣";
        if (strcmp(ext, ".xml") == 0) return "📑";
        if (strcmp(ext, ".yaml") == 0 || strcmp(ext, ".yml") == 0) return "📋";
        if (strcmp(ext, ".toml") == 0) return "⚙️";
        if (strcmp(ext, ".ini") == 0) return "🔧";
    }

    return "📄";
}

// copy file to users clipboard
void copy_to_clipboard(const char *path) {
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        fprintf(stderr, "Error: Unable to get file/directory stats.\n");
        return;
    }

    // Create a temporary file to store path
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "/tmp/cupidfm_copy_%d", getpid());
    
    FILE *temp = fopen(temp_path, "w");
    if (!temp) {
        fprintf(stderr, "Error: Unable to create temporary file.\n");
        return;
    }

    // Write the path and whether it's a directory
    fprintf(temp, "%s\n%d", path, S_ISDIR(path_stat.st_mode));
    fclose(temp);

    // Copy the temp file content to clipboard
    char command[1024];
    snprintf(command, sizeof(command), "xclip -selection clipboard -i < %s", temp_path);
    
    int result = system(command);
    if (result == -1) {
        fprintf(stderr, "Error: Unable to copy to clipboard.\n");
    }

    // Clean up
    unlink(temp_path);
}

// Helper function to generate a unique filename in target_directory.
// If target_directory/filename exists, the function returns a new filename
// such as "filename (1).ext", "filename (2).ext", etc.
static void generate_unique_filename(const char *target_directory, const char *filename, char *unique_name, size_t unique_size) {
    char target_path[PATH_MAX];
    // Create the initial target path.
    snprintf(target_path, sizeof(target_path), "%s/%s", target_directory, filename);
    
    // If no file exists with this name, use it.
    if (access(target_path, F_OK) != 0) {
        strncpy(unique_name, filename, unique_size);
        unique_name[unique_size - 1] = '\0';
        return;
    }
    
    // Otherwise, split the filename into base and extension.
    char base[PATH_MAX];
    char ext[PATH_MAX];
    const char *dot = strrchr(filename, '.');
    if (dot != NULL) {
        size_t base_len = dot - filename;
        if (base_len >= sizeof(base))
            base_len = sizeof(base) - 1;
        strncpy(base, filename, base_len);
        base[base_len] = '\0';
        strncpy(ext, dot, sizeof(ext));
        ext[sizeof(ext) - 1] = '\0';
    } else {
        strncpy(base, filename, sizeof(base));
        base[sizeof(base) - 1] = '\0';
        ext[0] = '\0';
    }
    
    // Append a counter until we get a name that does not exist.
    int counter = 1;
    while (1) {
        snprintf(unique_name, unique_size, "%s (%d)%s", base, counter, ext);
        snprintf(target_path, sizeof(target_path), "%s/%s", target_directory, unique_name);
        if (access(target_path, F_OK) != 0) {
            break;
        }
        counter++;
    }
}

// paste files to directory the user in
void paste_from_clipboard(const char *target_directory, const char *filename) {
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "/tmp/cupidfm_paste_%d", getpid());
    
    char command[1024];
    snprintf(command, sizeof(command), "xclip -selection clipboard -o > %s", temp_path);
    
    if (system(command) == -1) {
        fprintf(stderr, "Error: Unable to read from clipboard.\n");
        return;
    }
    
    FILE *temp = fopen(temp_path, "r");
    if (!temp) {
        unlink(temp_path);
        return;
    }
    
    char source_path[512];
    int is_directory;
    char operation[10] = {0};
    
    if (fscanf(temp, "%511[^\n]\n%d\n%9s", source_path, &is_directory, operation) < 2) {
        fclose(temp);
        unlink(temp_path);
        return;
    }
    fclose(temp);
    unlink(temp_path);
    
    // Check if this is a cut operation.
    bool is_cut = (operation[0] == 'C' && operation[1] == 'U' && operation[2] == 'T');
    
    // Generate a unique file name if one already exists in target_directory.
    char unique_filename[512];
    generate_unique_filename(target_directory, filename, unique_filename, sizeof(unique_filename));
    
    if (is_cut) {
        // Get the temporary storage path (for cut operations).
        char temp_storage[MAX_PATH_LENGTH];
        snprintf(temp_storage, sizeof(temp_storage), "/tmp/cupidfm_cut_storage_%d", getpid());
        
        // Move from temporary storage to target directory with the unique name.
        char mv_command[2048];
        snprintf(mv_command, sizeof(mv_command), "mv \"%s\" \"%s/%s\"", 
                 temp_storage, target_directory, unique_filename);
        if (system(mv_command) == -1) {
            fprintf(stderr, "Error: Unable to move file from temporary storage.\n");
            return;
        }
    } else {
        // Handle regular copy operation.
        char cp_command[2048];
        snprintf(cp_command, sizeof(cp_command), "cp %s \"%s\" \"%s/%s\"",
                 is_directory ? "-r" : "", source_path, target_directory, unique_filename);
        if (system(cp_command) == -1) {
            fprintf(stderr, "Error: Unable to copy file.\n");
        }
    }
}

// cut and put into memory 
void cut_and_paste(const char *path) {
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        fprintf(stderr, "Error: Unable to get file/directory stats.\n");
        return;
    }

    // Create a temporary file to store path with CUT flag
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "/tmp/cupidfm_cut_%d", getpid());
    
    FILE *temp = fopen(temp_path, "w");
    if (!temp) {
        fprintf(stderr, "Error: Unable to create temporary file.\n");
        return;
    }

    // Write the path, whether it's a directory, and mark it as CUT operation
    fprintf(temp, "%s\n%d\nCUT", path, S_ISDIR(path_stat.st_mode));
    fclose(temp);

    // Copy the temp file content to clipboard
    char command[1024];
    snprintf(command, sizeof(command), "xclip -selection clipboard -i < %s", temp_path);
    
    int result = system(command);
    if (result == -1) {
        fprintf(stderr, "Error: Unable to copy to clipboard.\n");
    }

    // Clean up
    unlink(temp_path);

    // Hide the file from the current view by moving it to a temporary location
    char temp_storage[MAX_PATH_LENGTH];
    snprintf(temp_storage, sizeof(temp_storage), "/tmp/cupidfm_cut_storage_%d", getpid());
    
    // Move the file to temporary storage
    char mv_command[2048];
    snprintf(mv_command, sizeof(mv_command), "mv \"%s\" \"%s\"", path, temp_storage);
    
    if (system(mv_command) == -1) {
        fprintf(stderr, "Error: Unable to move file to temporary storage.\n");
        return;
    }
}

void delete_item(const char *path) {
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        fprintf(stderr, "Error: Unable to get file/directory stats.\n");
        return;
    }

    if (S_ISDIR(path_stat.st_mode)) {
        // For directories, use rm -rf command
        char command[1024];
        snprintf(command, sizeof(command), "rm -rf \"%s\"", path);
        
        int result = system(command);
        if (result == -1) {
            fprintf(stderr, "Error: Unable to delete directory: %s\n", path);
        }
    } else {
        // For regular files, use unlink
        if (unlink(path) != 0) {
            fprintf(stderr, "Error: Unable to delete file: %s\n", path);
        }
    }
}

/**
 * Create a new directory by prompting the user for the directory name.
 *
 * @param win      The ncurses window to display prompts and messages.
 * @param dir_path The directory where the new folder will be created.
 * @return         true if the directory was created successfully, false if canceled or failed.
 */
bool create_new_directory(WINDOW *win, const char *dir_path) {
    char dir_name[MAX_PATH_LENGTH] = {0};
    int ch, index = 0;

    // Make input non-blocking to allow banner updates
    wtimeout(win, 10);

    // Initialize time-based update tracking
    // banner_offset is now a global variable - no need for static
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = COLS + (BANNER_TEXT ? strlen(BANNER_TEXT) : 0) + (BUILD_INFO ? strlen(BUILD_INFO) : 0) + 4;

    // Prompt for the new directory name
    werase(win);
    mvwprintw(win, 0, 0, "New directory name (Esc to cancel): ");
    wrefresh(win);

    while ((ch = wgetch(win)) != '\n') {
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
            
            napms(10);
            continue;
        }
        if (ch == 27) { // Escape key pressed
            wtimeout(win, -1); // Restore blocking input
            show_notification(win, "❌ Directory creation canceled.");
            should_clear_notif = false;
            return false;
        }
        if (ch == KEY_BACKSPACE || ch == 127) {
            if (index > 0) {
                index--;
                dir_name[index] = '\0';
            }
        } else if (isprint(ch) && index < MAX_PATH_LENGTH - 1) {
            dir_name[index++] = ch;
            dir_name[index] = '\0';
        }
        werase(win);
        mvwprintw(win, 0, 0, "New directory name (Esc to cancel): %s", dir_name);
        wrefresh(win);
    }
    
    wtimeout(win, -1); // Restore blocking input

    if (index == 0) {
        show_notification(win, "❌ Invalid name, directory creation canceled.");
        should_clear_notif = false;
        return false;
    }

    // Construct the full path
    char full_path[MAX_PATH_LENGTH * 2];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, dir_name);

    // Attempt to create the directory
    if (mkdir(full_path, 0755) == 0) {
        show_notification(win, "✅ Directory created: %s", dir_name);
        should_clear_notif = false;
        return true;
    } else {
        show_notification(win, "❌ Directory creation failed: %s", strerror(errno));
        should_clear_notif = false;
        return false;
    }
}

/**
 * Rename a file or directory by prompting the user for a new name.
 *
 * @param win      The ncurses window to display prompts and messages.
 * @param old_path The full path to the existing file or directory.
 * @return         true if rename was successful, false if canceled or failed.
 */
bool rename_item(WINDOW *win, const char *old_path) {
    char new_name[MAX_PATH_LENGTH] = {0};
    int ch, index = 0;

    // Make input non-blocking to allow banner updates
    wtimeout(win, 10);

    // Initialize time-based update tracking
    // banner_offset is now a global variable - no need for static
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = COLS + (BANNER_TEXT ? strlen(BANNER_TEXT) : 0) + (BUILD_INFO ? strlen(BUILD_INFO) : 0) + 4;

    // Prompt for new name
    werase(win);
    mvwprintw(win, 0, 0, "Rename (Esc to cancel): ");
    wrefresh(win);

    while ((ch = wgetch(win)) != '\n') {
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
            
            napms(10);
            continue;
        }
        if (ch == 27) { // Escape key pressed
            wtimeout(win, -1); // Restore blocking input
            show_notification(win, "❌ Rename canceled.");
            should_clear_notif = false; // Prevent immediate clearing
            return false;
        }
        if (ch == KEY_BACKSPACE || ch == 127) {
            if (index > 0) {
                index--;
                new_name[index] = '\0';
            }
        } else if (isprint(ch) && index < MAX_PATH_LENGTH - 1) {
            new_name[index++] = ch;
            new_name[index] = '\0';
        }
        werase(win);
        mvwprintw(win, 0, 0, "Rename (Esc to cancel): %s", new_name);
        wrefresh(win);
    }
    
    wtimeout(win, -1); // Restore blocking input

    if (index == 0) {
        show_notification(win, "❌ Invalid name, rename canceled.");
        should_clear_notif = false;
        return false;
    }

    // Construct the new path
    char temp_path[MAX_PATH_LENGTH];
    strncpy(temp_path, old_path, MAX_PATH_LENGTH - 1);
    temp_path[MAX_PATH_LENGTH - 1] = '\0'; // Ensure null-termination
    char *dir = dirname(temp_path);

    char new_path[MAX_PATH_LENGTH * 2];
    snprintf(new_path, sizeof(new_path), "%s/%s", dir, new_name);

    // Attempt to rename
    if (rename(old_path, new_path) == 0) {
        show_notification(win, "✅ Renamed to: %s", new_name);
        should_clear_notif = false;
        return true;
    } else {
        show_notification(win, "❌ Rename failed: %s", strerror(errno));
        should_clear_notif = false;
        return false;
    }
}

/**
 * Create a new file by prompting the user for the file name.
 *
 * @param win      The ncurses window to display prompts and messages.
 * @param dir_path The directory where the new file will be created.
 * @return         true if the file was created successfully, false if canceled or failed.
 */
bool create_new_file(WINDOW *win, const char *dir_path) {
    char file_name[MAX_PATH_LENGTH] = {0};
    int ch, index = 0;

    // Make input non-blocking to allow banner updates
    wtimeout(win, 10);

    // Initialize time-based update tracking
    // banner_offset is now a global variable - no need for static
    struct timespec last_banner_update;
    clock_gettime(CLOCK_MONOTONIC, &last_banner_update);
    int total_scroll_length = COLS + (BANNER_TEXT ? strlen(BANNER_TEXT) : 0) + (BUILD_INFO ? strlen(BUILD_INFO) : 0) + 4;

    // Prompt for the new file name
    werase(win);
    mvwprintw(win, 0, 0, "New file name (Esc to cancel): ");
    wrefresh(win);

    while ((ch = wgetch(win)) != '\n') {
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
            
            napms(10);
            continue;
        }
        if (ch == 27) { // Escape key pressed
            wtimeout(win, -1); // Restore blocking input
            show_notification(win, "❌ File creation canceled.");
            should_clear_notif = false;
            return false;
        }
        if (ch == KEY_BACKSPACE || ch == 127) {
            if (index > 0) {
                index--;
                file_name[index] = '\0';
            }
        } else if (isprint(ch) && index < MAX_PATH_LENGTH - 1) {
            file_name[index++] = ch;
            file_name[index] = '\0';
        }
        werase(win);
        mvwprintw(win, 0, 0, "New file name (Esc to cancel): %s", file_name);
        wrefresh(win);
    }
    
    wtimeout(win, -1); // Restore blocking input

    if (index == 0) {
        show_notification(win, "❌ Invalid name, file creation canceled.");
        should_clear_notif = false;
        return false;
    }

    // Construct the full path
    char full_path[MAX_PATH_LENGTH * 2];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, file_name);

    // Attempt to create the file
    FILE *file = fopen(full_path, "w");
    if (file) {
        fclose(file);
        show_notification(win, "✅ File created: %s", file_name);
        should_clear_notif = false;
        return true;
    } else {
        show_notification(win, "❌ File creation failed: %s", strerror(errno));
        should_clear_notif = false;
        return false;
    }
}

/** Function to reload the directory contents
 *
 * @param files the list of files
 * @param current_directory the current directory
 */
void reload_directory(Vector *files, const char *current_directory) {
    // Empties the vector
    Vector_set_len(files, 0);
    // Reads the filenames
    append_files_to_vec(files, current_directory);
    // Makes the vector shorter
    Vector_sane_cap(files);
}