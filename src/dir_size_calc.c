#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include "hashmap.h"
#include "dir_size_calc.h"

#define MAX_PATH_LENGTH 1024

typedef struct {
    char *dir_path;
    HashMap *hash_map;
} ThreadArg;

// Global HashMap to store directory sizes
HashMap dir_sizes;

// Mutex for thread-safe access to the HashMap
pthread_mutex_t dir_sizes_mutex = PTHREAD_MUTEX_INITIALIZER;

long get_directory_size(const char *dir_path, int max_depth) {
    // If max_depth is 0, return 0
    if (max_depth == 0) {
        return 0;
    }

    DIR *dir;
    struct dirent *entry;
    struct stat statbuf;
    long total_size = 0;

    // Open directory
    if (!(dir = opendir(dir_path)))
        return -1;

    // Iterate over directory entries
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        // Construct full path to entry
        char path[MAX_PATH_LENGTH];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
        // Get entry's information
        if (lstat(path, &statbuf) == -1)
            continue;
        // If entry is a directory, recursively calculate its size
        if (S_ISDIR(statbuf.st_mode) && !S_ISLNK(statbuf.st_mode))
            total_size += get_directory_size(path, max_depth - 1);
        else
            total_size += statbuf.st_size; // Add size of regular file or symbolic link
    }

    closedir(dir);
    return total_size;
}
long *calculate_directory_size(void *arg) {
    ThreadArg *thread_arg = (ThreadArg *)arg;
    long *dir_size = malloc(sizeof(long));
    *dir_size = get_directory_size(thread_arg->dir_path, 5);  // Added second argument
    insert(thread_arg->hash_map, thread_arg->dir_path, *dir_size);
    return dir_size;
}

void display_directory_size(const char *dir_path, HashMap *hash_map) {
    long dir_size = get_directory_size(dir_path, 5);  // Added second argument
    printf("Directory: %s, Size: %ld\n", dir_path, dir_size);
}

void periodically_update_ui(HashMap *hash_map) {
    while (1) {
        // Iterate over the hash_map and update the UI for each directory
        sleep(1); // Sleep for a while to not hog the CPU
    }
}