// clipboard.c
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "clipboard.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool clipboard_set_from_file(const char *path) {
    if (!path || !*path) return false;
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "xclip -selection clipboard -i < \"%s\"", path);
    return system(cmd) != -1;
}

bool ensure_cut_storage_dir(char *out_dir, size_t out_len) {
    if (!out_dir || out_len == 0) return false;
    snprintf(out_dir, out_len, "/tmp/cupidfm_cut_storage_%d", getpid());
    out_dir[out_len - 1] = '\0';
    if (mkdir(out_dir, 0700) == 0) return true;
    return (errno == EEXIST);
}
