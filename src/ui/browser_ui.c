// browser_ui.c - directory + preview pane rendering and related helpers
#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "browser_ui.h"

#include <dirent.h>
#include <magic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

#include "files.h"
#include "globals.h"
#include "syntax.h"

#define DIRECTORY_TREE_MAX_DEPTH 4
#define DIRECTORY_TREE_MAX_TOTAL 1500

static bool tree_limit_hit = false;

static void count_directory_tree_lines(const char *dir_path, int level, int *line_count) {
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

int get_directory_tree_total_lines(const char *dir_path) {
    int line_count = 0;
    count_directory_tree_lines(dir_path, 0, &line_count);
    return line_count;
}

static void show_directory_tree(WINDOW *window,
                                const char *dir_path,
                                int level,
                                int *line_num,
                                int max_y,
                                int max_x,
                                int start_line,
                                int *current_count) {
    if (level == 0) {
        tree_limit_hit = false;
    }
    if (level == 0) {
        mvwprintw(window, 6, 2, "Directory Tree Preview:");
        (*line_num)++;
    }

    if (*line_num >= max_y - 1) {
        return;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    struct stat statbuf;
    char full_path[MAX_PATH_LENGTH];
    size_t dir_path_len = strlen(dir_path);

    const int WINDOW_SIZE = 50;

    struct {
        char name[MAX_PATH_LENGTH];
        bool is_dir;
        mode_t mode;
    } entries[WINDOW_SIZE];
    int entry_count = 0;

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

    if (entry_count == 0) {
        if (*current_count >= start_line && *line_num < max_y - 1) {
            mvwprintw(window, *line_num, 2, "[Empty directory]");
            (*line_num)++;
        }
        (*current_count)++;
        return;
    }

    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE);
    if (magic_cookie && magic_load(magic_cookie, NULL) != 0) {
        magic_close(magic_cookie);
        magic_cookie = NULL;
    }

    for (int i = 0; i < entry_count; i++) {
        if (*current_count < start_line) {
            (*current_count)++;
            if (entries[i].is_dir && level < DIRECTORY_TREE_MAX_DEPTH) {
                size_t name_len = strlen(entries[i].name);
                if (dir_path_len + name_len + 2 <= MAX_PATH_LENGTH) {
                    strcpy(full_path, dir_path);
                    if (full_path[dir_path_len - 1] != '/') {
                        strcat(full_path, "/");
                    }
                    strcat(full_path, entries[i].name);
                    show_directory_tree(window, full_path, level + 1, line_num, max_y, max_x, start_line, current_count);
                }
            }
            continue;
        }

        if (*line_num >= max_y - 1) {
            break;
        }

        full_path[0] = '\0';
        size_t name_len = strlen(entries[i].name);
        if (dir_path_len + name_len + 2 <= MAX_PATH_LENGTH) {
            strcpy(full_path, dir_path);
            if (full_path[dir_path_len - 1] != '/') {
                strcat(full_path, "/");
            }
            strcat(full_path, entries[i].name);
        }

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
        } else if (magic_cookie) {
            if (dir_path_len + name_len + 2 <= MAX_PATH_LENGTH) {
                const char *mime_type = magic_file(magic_cookie, full_path);
                emoji = get_file_emoji(mime_type, entries[i].name);
            } else {
                emoji = "📄";
            }
        } else {
            emoji = "📄";
        }

        wmove(window, *line_num, 2 + level * 2);
        for (int clear_x = 2 + level * 2; clear_x < max_x - 10; clear_x++) {
            waddch(window, ' ');
        }

        int available_width = max_x - 4 - level * 2 - 10;
        int display_len = (int)name_len + (is_symlink ? (4 + (int)strlen(symlink_target)) : 0);

        if (display_len > available_width) {
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

void draw_directory_window(WINDOW *window,
                           const char *directory,
                           Vector *files_vector,
                           CursorAndSlice *cas) {
    int cols;
    int rows;
    getmaxyx(window, rows, cols);

    size_t vec_len = files_vector ? Vector_len(*files_vector) : 0;
    cas->num_files = (SIZE)vec_len;
    if (cas->num_files < 0) cas->num_files = 0;
    if (cas->start < 0) cas->start = 0;
    if (cas->cursor < 0) cas->cursor = 0;
    if (cas->num_files == 0) {
        cas->start = 0;
        cas->cursor = 0;
    } else {
        if (cas->start >= cas->num_files) cas->start = 0;
        if (cas->cursor >= cas->num_files) cas->cursor = cas->num_files - 1;
    }

    werase(window);
    box(window, 0, 0);

    if (cas->num_files == 0) {
        mvwprintw(window, 1, 1, "This directory is empty");
        wrefresh(window);
        return;
    }

    int max_visible_items = rows - 2;

    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE);
    if (magic_cookie == NULL || magic_load(magic_cookie, NULL) != 0) {
        for (int i = 0; i < max_visible_items && (cas->start + i) < cas->num_files; i++) {
            FileAttr fa = (FileAttr)files_vector->el[cas->start + i];
            const char *name = FileAttr_get_name(fa);

            char full_path[MAX_PATH_LENGTH];
            path_join(full_path, directory, name);

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

            wmove(window, i + 1, 1);
            for (int j = 1; j < cols - 1; j++) {
                waddch(window, ' ');
            }

            bool is_cursor = ((cas->start + i) == cas->cursor);
            bool is_selected = g_select_all_highlight || is_cursor;
            if (is_selected) wattron(window, A_REVERSE);
            if (g_select_all_highlight && is_cursor) wattron(window, A_BOLD);

            int name_len = (int)strlen(name);
            int target_len = is_symlink ? (int)strlen(symlink_target) : 0;
            int total_len = name_len + (is_symlink ? (4 + target_len) : 0);
            int available_width = cols - 8;

            if (total_len > available_width) {
                if (is_symlink && target_len > 0) {
                    int name_part = available_width / 2;
                    int target_part = available_width - name_part - 4;
                    mvwprintw(window, i + 1, 1, "%s %.*s -> %.*s...", emoji,
                              name_part, name, target_part, symlink_target);
                } else {
                    mvwprintw(window, i + 1, 1, "%s %.*s", emoji, available_width, name);
                }
            } else {
                if (is_symlink && target_len > 0) {
                    mvwprintw(window, i + 1, 1, "%s %s -> %s", emoji, name, symlink_target);
                } else {
                    mvwprintw(window, i + 1, 1, "%s %s", emoji, name);
                }
            }

            if (g_select_all_highlight && is_cursor) wattroff(window, A_BOLD);
            if (is_selected) wattroff(window, A_REVERSE);
        }

        wrefresh(window);
        return;
    }

    for (int i = 0; i < max_visible_items && (cas->start + i) < cas->num_files; i++) {
        FileAttr fa = (FileAttr)files_vector->el[cas->start + i];
        const char *name = FileAttr_get_name(fa);

        char full_path[MAX_PATH_LENGTH];
        path_join(full_path, directory, name);

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

        wmove(window, i + 1, 1);
        for (int j = 1; j < cols - 1; j++) {
            waddch(window, ' ');
        }

        bool is_cursor = ((cas->start + i) == cas->cursor);
        bool is_selected = g_select_all_highlight || is_cursor;
        if (is_selected) wattron(window, A_REVERSE);
        if (g_select_all_highlight && is_cursor) wattron(window, A_BOLD);

        int name_len = (int)strlen(name);
        int target_len = is_symlink ? (int)strlen(symlink_target) : 0;
        int total_len = name_len + (is_symlink ? (4 + target_len) : 0);
        int available_width = cols - 8;

        if (total_len > available_width) {
            if (is_symlink && target_len > 0) {
                int name_part = available_width / 2;
                int target_part = available_width - name_part - 4;
                mvwprintw(window, i + 1, 1, "%s %.*s -> %.*s...", emoji,
                          name_part, name, target_part, symlink_target);
            } else {
                mvwprintw(window, i + 1, 1, "%s %.*s", emoji, available_width, name);
            }
        } else {
            if (is_symlink && target_len > 0) {
                mvwprintw(window, i + 1, 1, "%s %s -> %s", emoji, name, symlink_target);
            } else {
                mvwprintw(window, i + 1, 1, "%s %s", emoji, name);
            }
        }

        if (g_select_all_highlight && is_cursor) wattroff(window, A_BOLD);
        if (is_selected) wattroff(window, A_REVERSE);
    }

    magic_close(magic_cookie);
    wrefresh(window);
}

static const char *basename_ptr_local(const char *p) {
    if (!p) return "";
    size_t len = strlen(p);
    while (len > 0 && p[len - 1] == '/') len--;
    if (len == 0) return "";
    const char *end = p + len;
    const char *slash = memrchr(p, '/', (size_t)(end - p));
    return slash ? (slash + 1) : p;
}

void draw_preview_window_path(WINDOW *window, const char *full_path, const char *display_name, int start_line) {
    // Clear the window and draw border
    werase(window);
    box(window, 0, 0);

    int max_y, max_x;
    getmaxyx(window, max_y, max_x);

    if (!full_path || !*full_path) {
        mvwprintw(window, 1, 2, "No file selected");
        wrefresh(window);
        return;
    }

    const char *name = (display_name && *display_name) ? display_name : basename_ptr_local(full_path);
    mvwprintw(window, 1, 2, "Preview: %s", name);

    struct stat file_stat;
    if (stat(full_path, &file_stat) == -1) {
        mvwprintw(window, 2, 2, "Unable to retrieve file information");
        wrefresh(window);
        return;
    }

    char fileSizeStr[64];
    if (S_ISDIR(file_stat.st_mode)) {
        static char last_preview_size_path[MAX_PATH_LENGTH] = "";
        static struct timespec last_preview_size_change = {0};
        static bool last_preview_size_initialized = false;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        bool path_changed = !last_preview_size_initialized ||
                            strncmp(last_preview_size_path, full_path, MAX_PATH_LENGTH) != 0;
        if (path_changed) {
            strncpy(last_preview_size_path, full_path, MAX_PATH_LENGTH - 1);
            last_preview_size_path[MAX_PATH_LENGTH - 1] = '\0';
            last_preview_size_change = now;
            last_preview_size_initialized = true;
        }

        long elapsed_ns = (now.tv_sec - last_preview_size_change.tv_sec) * 1000000000L +
                          (now.tv_nsec - last_preview_size_change.tv_nsec);
        bool allow_enqueue = (elapsed_ns >= DIR_SIZE_REQUEST_DELAY_NS) && dir_size_can_enqueue();

        long dir_size = allow_enqueue ? get_directory_size(full_path)
                                      : get_directory_size_peek(full_path);
        if (dir_size == -1) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Error");
        } else if (dir_size == DIR_SIZE_VIRTUAL_FS) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Virtual FS");
        } else if (dir_size == DIR_SIZE_TOO_LARGE) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Too large");
        } else if (dir_size == DIR_SIZE_PERMISSION_DENIED) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Permission denied");
        } else if (dir_size == DIR_SIZE_PENDING) {
            long p = dir_size_get_progress(full_path);
            if (p > 0) {
                char tmp[32];
                format_file_size(tmp, (size_t)p);
                snprintf(fileSizeStr, sizeof(fileSizeStr), "Calculating... %s", tmp);
            } else {
                snprintf(fileSizeStr, sizeof(fileSizeStr), allow_enqueue ? "Calculating..." : "Waiting...");
            }
        } else {
            format_file_size(fileSizeStr, (size_t)dir_size);
        }
        mvwprintw(window, 2, 2, "📁 Directory Size: %s", fileSizeStr);
    } else {
        format_file_size(fileSizeStr, (size_t)file_stat.st_size);
        mvwprintw(window, 2, 2, "📏 File Size: %s", fileSizeStr);
    }

    char permissions[10];
    snprintf(permissions, sizeof(permissions), "%o", file_stat.st_mode & 0777);
    mvwprintw(window, 3, 2, "🔒 Permissions: %s", permissions);

    char modTime[50];
    strftime(modTime, sizeof(modTime), "%c", localtime(&file_stat.st_mtime));
    mvwprintw(window, 4, 2, "🕒 Last Modified: %s", modTime);

    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE);
    if (magic_cookie != NULL && magic_load(magic_cookie, NULL) == 0) {
        const char *mime_type = magic_file(magic_cookie, full_path);
        mvwprintw(window, 5, 2, "MIME Type: %s", mime_type ? mime_type : "Unknown");
        magic_close(magic_cookie);
    } else {
        mvwprintw(window, 5, 2, "MIME Type: Unable to detect");
    }

    if (S_ISDIR(file_stat.st_mode)) {
        int line_num = 7;
        int current_count = 0;
        show_directory_tree(window, full_path, 0, &line_num, max_y, max_x, start_line, &current_count);
    } else if (is_archive_file(full_path)) {
        display_archive_preview(window, full_path, start_line, max_y, max_x);
    } else if (is_supported_file_type(full_path)) {
        FILE *file = fopen(full_path, "r");
        if (file) {
            // Read all lines into a buffer first to enable proper block comment state tracking
            char **all_lines = NULL;
            int total_lines = 0;
            int capacity = 100;
            all_lines = malloc(capacity * sizeof(char*));
            if (!all_lines) {
                fclose(file);
                mvwprintw(window, 7, 2, "Memory allocation failed");
                wrefresh(window);
                return;
            }
            
            char line_buffer[256];
            while (fgets(line_buffer, sizeof(line_buffer), file)) {
                if (total_lines >= capacity) {
                    capacity *= 2;
                    char **new_lines = realloc(all_lines, capacity * sizeof(char*));
                    if (!new_lines) {
                        for (int i = 0; i < total_lines; i++) free(all_lines[i]);
                        free(all_lines);
                        fclose(file);
                        mvwprintw(window, 7, 2, "Memory allocation failed");
                        wrefresh(window);
                        return;
                    }
                    all_lines = new_lines;
                }
                line_buffer[strcspn(line_buffer, "\n")] = '\0';
                all_lines[total_lines] = strdup(line_buffer);
                if (!all_lines[total_lines]) {
                    for (int i = 0; i < total_lines; i++) free(all_lines[i]);
                    free(all_lines);
                    fclose(file);
                    mvwprintw(window, 7, 2, "Memory allocation failed");
                    wrefresh(window);
                    return;
                }
                total_lines++;
            }
            fclose(file);
            
            // Get syntax definition for this file
            SyntaxDef *syntax = syntax_get_for_file(full_path);
            
            // IMPORTANT: Compute block comment state for the first visible line AFTER determining start_line
            int in_block_comment = 0;
            if (syntax && start_line > 0) {
                in_block_comment = get_initial_block_comment_state(all_lines, total_lines, start_line, syntax);
            }
            
            // Display lines starting from start_line
            int line_num = 7;
            for (int i = start_line; i < total_lines && line_num < max_y - 1; i++) {
                char line[256];
                strncpy(line, all_lines[i], sizeof(line) - 1);
                line[sizeof(line) - 1] = '\0';
                
                // Clean up non-printable characters
                for (char *p = line; *p; p++) {
                    unsigned char c = (unsigned char)*p;
                    if (c == '\t') {
                        *p = ' ';
                    } else if (isspace(c) && c != ' ') {
                        *p = ' ';
                    } else if (!isprint(c)) {
                        *p = ' ';
                    }
                }

                // Use syntax highlighting if available, otherwise plain text
                // Pass full buffer context for proper state tracking
                syntax_highlight_line(window, line, syntax, &in_block_comment, 
                                     line_num++, 2, max_x - 4, all_lines, total_lines, i);
            }
            
            // Free all lines
            for (int i = 0; i < total_lines; i++) {
                free(all_lines[i]);
            }
            free(all_lines);

            if (line_num < max_y - 1) {
                mvwprintw(window, line_num++, 2, "--------------------------------");
                mvwprintw(window, line_num++, 2, "[End of file]");
            }
        } else {
            mvwprintw(window, 7, 2, "Unable to open file for preview");
        }
    } else {
        mvwprintw(window, 7, 2, "No preview available");
    }

    wrefresh(window);
}

void draw_preview_window(WINDOW *window, const char *current_directory, const char *selected_entry, int start_line) {
    if (selected_entry == NULL || selected_entry[0] == '\0') {
        draw_preview_window_path(window, NULL, NULL, start_line);
        return;
    }

    char file_path[MAX_PATH_LENGTH];
    path_join(file_path, current_directory, selected_entry);
    draw_preview_window_path(window, file_path, selected_entry, start_line);
}

void fix_cursor(CursorAndSlice *cas) {
    cas->cursor = MIN(cas->cursor, cas->num_files - 1);
    cas->cursor = MAX(0, cas->cursor);

    int visible_lines = cas->num_lines - 2;

    if (cas->num_files <= visible_lines) {
        cas->start = 0;
        return;
    }

    if (cas->cursor < cas->start) {
        cas->start = cas->cursor;
    } else if (cas->cursor >= cas->start + visible_lines) {
        cas->start = cas->cursor - visible_lines + 1;
    }

    int max_start = cas->num_files - visible_lines;
    if (max_start < 0) max_start = 0;
    cas->start = MIN(cas->start, max_start);
    cas->start = MAX(0, cas->start);

    int cursor_relative_pos = cas->cursor - cas->start;
    if (cursor_relative_pos < 0 || cursor_relative_pos >= visible_lines) {
        if (cursor_relative_pos < 0) {
            cas->start = cas->cursor;
        } else {
            cas->start = cas->cursor - visible_lines + 1;
            if (cas->start < 0) cas->start = 0;
            if (cas->start > max_start) cas->start = max_start;
        }
    }
}
