// tempfiles.c
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "tempfiles.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void cleanup_temp_files(void) {
    char command[1024];
    snprintf(command, sizeof(command), "rm -rf /tmp/cupidfm_*_%d", getpid());
    system(command);
}
