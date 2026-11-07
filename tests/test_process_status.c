#define _GNU_SOURCE
#include "process.h"

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char *success[] = {"sh", "-c", "exit 0", NULL};
    char *failed[] = {"sh", "-c", "exit 7", NULL};
    char *cancelled[] = {"sh", "-c", "exit 130", NULL};
    char *signalled[] = {"sh", "-c", "kill -TERM $$", NULL};
    char *missing[] = {"cupidfm-no-such-executable-for-tests", NULL};
    assert(run_process_status_in_directory(success, NULL, -1, -1) == 0);
    assert(run_process_status_in_directory(failed, NULL, -1, -1) == 7);
    assert(run_process_status_in_directory(cancelled, NULL, -1, -1) == 130);
    assert(run_process_status_in_directory(signalled, NULL, -1, -1) == 128 + SIGTERM);
    assert(!run_process(failed, -1, -1));
    errno = 0;
    assert(run_process_status_in_directory(missing, NULL, -1, -1) == -1 && errno != 0);
    assert(run_process_status_in_directory(NULL, NULL, -1, -1) == -1 && errno == EINVAL);
    char directory[] = "/tmp/cupidfm-process-XXXXXX";
    assert(mkdtemp(directory));
    FILE *output = tmpfile();
    assert(output);
    char *cwd[] = {"sh", "-c", "printf '%s' \"$PWD\"", NULL};
    assert(run_process_status_in_directory(cwd, directory, -1, fileno(output)) == 0);
    rewind(output);
    char text[512] = {0};
    assert(fread(text, 1, sizeof(text) - 1, output) == strlen(directory));
    assert(!strcmp(text, directory));
    fclose(output);
    assert(!rmdir(directory));
    puts("Process exit codes, signals, launch errors, boolean compatibility, and child cwd passed");
}
