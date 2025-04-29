// File: utils.c
// -----------------------
#define _POSIX_C_SOURCE 200809L
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
#include <unistd.h>    // for readlink
// Local includes
#include "utils.h"
#include "files.h"  // Include the header for FileAttr and related functions
#include "globals.h"
#include "main.h"
#define MAX_DISPLAY_LENGTH 32

// Declare copied_filename as a global variable at the top of the file
char copied_filename[MAX_PATH_LENGTH] = "";
extern bool should_clear_notif; 

#define FOLDER_EMOJI "📁 "
#define TEXT_EMOJI "📄 "
#define IMAGE_EMOJI "🖼️ "
#define CODE_EMOJI "📝 "
#define ARCHIVE_EMOJI "📦 "
#define PDF_EMOJI "📑 "
#define AUDIO_EMOJI "🎵 "
#define VIDEO_EMOJI "🎬 "
#define SPREADSHEET_EMOJI "📊 "
#define PRESENTATION_EMOJI "📽️ "
#define BINARY_EMOJI "🔢 "

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
    
    // Capture user input
    int ch;
    while ((ch = wgetch(popup)) != ERR) {
        ch = tolower(ch);
        if (ch == 'y') {
            *should_delete = true;
            break;
        } else if (ch == 'n' || ch == 27) { // 27 = ESC key
            break;
        }
    }
    
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
    // First check for directories
    if (strcmp(mime_type, "inode/directory") == 0) {
        return FOLDER_EMOJI;
    }

    // Then check specific MIME types
    if (strstr(mime_type, "text/") == mime_type) {
        if (strstr(mime_type, "html")) return "🌐 ";
        if (strstr(mime_type, "shellscript")) return "🐚 ";
        if (strstr(mime_type, "python")) return "🐍 ";
        if (strstr(mime_type, "javascript")) return "📜 ";
        return TEXT_EMOJI;
    }
    
    if (strstr(mime_type, "image/")) return IMAGE_EMOJI;
    if (strstr(mime_type, "audio/")) return AUDIO_EMOJI;
    if (strstr(mime_type, "video/")) return VIDEO_EMOJI;
    if (strstr(mime_type, "application/pdf")) return PDF_EMOJI;
    if (strstr(mime_type, "application/zip") || 
        strstr(mime_type, "application/x-tar") ||
        strstr(mime_type, "application/x-gzip")) return ARCHIVE_EMOJI;
    
    // Fallback to file extension checks
    const char *dot = strrchr(filename, '.');
    if (dot) {
        if (strcmp(dot, ".pdf") == 0) return PDF_EMOJI;
        if (strcmp(dot, ".csv") == 0 || strcmp(dot, ".xls") == 0) return SPREADSHEET_EMOJI;
        if (strcmp(dot, ".ppt") == 0 || strcmp(dot, ".pptx") == 0) return PRESENTATION_EMOJI;
        if (strcmp(dot, ".mp3") == 0 || strcmp(dot, ".wav") == 0) return AUDIO_EMOJI;
        if (strcmp(dot, ".mp4") == 0 || strcmp(dot, ".mov") == 0) return VIDEO_EMOJI;
    }

    // Binary detection fallback
    if (strstr(mime_type, "application/octet-stream") || 
        strstr(mime_type, "application/x-executable")) {
        return BINARY_EMOJI;
    }

    // Default for unknown types
    return "🌐 ";
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

    // Prompt for the new directory name
    werase(win);
    mvwprintw(win, 0, 0, "New directory name (Esc to cancel): ");
    wrefresh(win);

    while ((ch = wgetch(win)) != '\n') {
        if (ch == 27) { // Escape key pressed
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

    // Prompt for new name
    werase(win);
    mvwprintw(win, 0, 0, "Rename (Esc to cancel): ");
    wrefresh(win);

    while ((ch = wgetch(win)) != '\n') {
        if (ch == 27) { // Escape key pressed
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

    // Prompt for the new file name
    werase(win);
    mvwprintw(win, 0, 0, "New file name (Esc to cancel): ");
    wrefresh(win);

    while ((ch = wgetch(win)) != '\n') {
        if (ch == 27) { // Escape key pressed
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

/**
 * Gets the symlink target if the path is a symlink
 * @param path The path to check
 * @param target Buffer to store the target path
 * @param target_size Size of the target buffer
 * @return true if path is a symlink, false otherwise
 */
bool get_symlink_target(const char *path, char *target, size_t target_size) {
    ssize_t len = readlink(path, target, target_size - 1);
    if (len != -1) {
        target[len] = '\0';
        return true;
    }
    return false;
}
