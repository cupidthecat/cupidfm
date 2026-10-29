#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "tempfiles.h"
#include "process.h"
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

void cleanup_temp_files(void) {
    char directory[128];
    int length = snprintf(directory, sizeof(directory), "/tmp/cupidfm_trash_%d", getpid());
    if (length < 0 || (size_t)length >= sizeof(directory)) return;
    struct stat st;
    if (lstat(directory, &st) != 0 || !S_ISDIR(st.st_mode) ||
        st.st_uid != geteuid() || (st.st_mode & 0077)) return;
    if (!file_command_remove_directory(directory))
        fprintf(stderr, "Unable to remove session trash: %s\n", directory);
}
