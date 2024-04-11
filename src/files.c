#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <main.h>
#include <utils.h>
#include <files.h>
#include <curses.h>
#include <pthread.h>
#include "data_struct/hashmap.h"
#include "dir_size_calc.h"
#define MAX_PATH_LENGTH 1024

struct FileAttributes {
    char *name;  // Change from char name*;
    ino_t inode;
    bool is_dir;
};

// Structure to pass arguments to the thread function
typedef struct {
    const char *dir_path;
    WINDOW *window;
    int max_x;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool done;
} ThreadArg;

// Thread function to calculate and display directory size
void *calculate_and_display_dir_size(void *arg) {
    ThreadArg *thread_arg = (ThreadArg *)arg;
    long dir_size = get_directory_size(thread_arg->dir_path, 5);  // Added second argument
    char fileSizeStr[20];
    mvwprintw(thread_arg->window, 5, 1, "Directory Size: %.*s", thread_arg->max_x - 4, format_file_size(fileSizeStr, dir_size));

    // Signal that the directory size calculation is done
    pthread_mutex_lock(&thread_arg->mutex);
    thread_arg->done = true;
    pthread_cond_signal(&thread_arg->cond);
    pthread_mutex_unlock(&thread_arg->mutex);

    // Do not free arg here
    return NULL;
}

const char *FileAttr_get_name(FileAttr fa) {
    if (fa != NULL) {
        return fa->name;
    } else {
        // Handle the case where fa is NULL
        return "Unknown";
    }
}

bool FileAttr_is_dir(FileAttr fa) {
    return fa->is_dir;
}

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

void free_attr(FileAttr fa) {
    if (fa != NULL) {
        free(fa->name);  // Free the allocated memory for the name
        free(fa);
    }
}

void append_files_to_vec(Vector *v, const char *name) {
    DIR *dir = opendir(name);
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            // Filter out "." and ".." entries
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                char full_path[MAX_PATH_LENGTH];
                path_join(full_path, name, entry->d_name);

                bool is_dir = is_directory(name, entry->d_name);

                // Allocate memory for the FileAttr object
                FileAttr file_attr = mk_attr(entry->d_name, is_dir, entry->d_ino);

                // Add the FileAttr object to the vector
                Vector_add(v, 1);
                v->el[Vector_len(*v)] = file_attr;

                // Update the vector length
                Vector_set_len(v, Vector_len(*v) + 1);
            }
        }
        closedir(dir);
    }
}


// TODO: FIX WRONG CALCULATION OF DIRECTORY SIZE ON SYMBOLIC LINKS
// Recursive function to calculate directory size
// NOTE: this function may take long, it might be better to have the size of
//       the directories displayed as "-" until we have a value. Use of "du" or
//       another already existent tool might be better. The sizes should
//       probably be cached.
//       fork() -> exec() (if using du)
//       fork() -> calculate directory size and return it somehow (maybe print
//       as binary to the stdout)
/*
dead function
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

void display_file_info(WINDOW *window, const char *file_path, int max_x) {
    struct stat file_stat;

    // Get file information
    if (stat(file_path, &file_stat) == -1) {
        mvwprintw(window, 5, 1, "Unable to retrieve file information");
        return;
    }

    // Display file information
    if (S_ISDIR(file_stat.st_mode)) {
        // If it's a directory, calculate its size in a separate thread
        ThreadArg *thread_arg = malloc(sizeof(ThreadArg));
        thread_arg->dir_path = file_path;
        thread_arg->window = window;
        thread_arg->max_x = max_x;
        thread_arg->done = false;
        pthread_mutex_init(&thread_arg->mutex, NULL);
        pthread_cond_init(&thread_arg->cond, NULL);
        pthread_t thread;
        pthread_create(&thread, NULL, calculate_and_display_dir_size, thread_arg);

        // Wait for the directory size calculation to complete
        pthread_mutex_lock(&thread_arg->mutex);
        while (!thread_arg->done) {
            pthread_cond_wait(&thread_arg->cond, &thread_arg->mutex);
        }
        pthread_mutex_unlock(&thread_arg->mutex);

        pthread_detach(thread);
    } else {
        // If it's a regular file, display its size directly
        char fileSizeStr[20];
        mvwprintw(window, 5, 1, "File Size: %.*s", max_x - 4, format_file_size(fileSizeStr, file_stat.st_size));
    }

    // Check if this array is big enough for the number
    // My check (needs review): 18 (label) +
    char permissions[22];
    sprintf(permissions, "File Permissions: %o", file_stat.st_mode & 0777);
    mvwprintw(window, 6, 1, "%.*s", max_x - 4, permissions);

    char modTime[50];
    strftime(modTime, sizeof(modTime), "%c", localtime(&file_stat.st_mtime));
    mvwprintw(window, 7, 1, "Last Modification Time: %.24s", modTime);
}
