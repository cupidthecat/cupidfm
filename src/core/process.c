#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "process.h"

#include <errno.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

bool run_process(char *const argv[], int input_fd, int output_fd) {
    if (!argv || !argv[0]) return false;
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return false;
    int error = 0;
    if (input_fd >= 0)
        error = posix_spawn_file_actions_adddup2(&actions, input_fd, STDIN_FILENO);
    if (!error && output_fd >= 0)
        error = posix_spawn_file_actions_adddup2(&actions, output_fd, STDOUT_FILENO);
    if (!error && input_fd > STDERR_FILENO)
        error = posix_spawn_file_actions_addclose(&actions, input_fd);
    if (!error && output_fd > STDERR_FILENO && output_fd != input_fd)
        error = posix_spawn_file_actions_addclose(&actions, output_fd);
    pid_t child;
    if (!error) error = posix_spawnp(&child, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error) {
        errno = error;
        return false;
    }
    int status;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    return waited == child && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool file_command_copy(const char *src, const char *dst, bool recursive) {
    if (!src || !*src || !dst || !*dst) return false;
    char *args[] = {"cp", "-P", "--", (char *)src, (char *)dst, NULL};
    char *recursive_args[] = {"cp", "-R", "-P", "--", (char *)src, (char *)dst, NULL};
    return run_process(recursive ? recursive_args : args, -1, -1);
}

bool file_command_move(const char *src, const char *dst) {
    if (!src || !*src || !dst || !*dst) return false;
    char *args[] = {"mv", "--", (char *)src, (char *)dst, NULL};
    return run_process(args, -1, -1);
}

bool file_command_remove_directory(const char *path) {
    if (!path || !*path) return false;
    char *args[] = {"rm", "-rf", "--", (char *)path, NULL};
    return run_process(args, -1, -1);
}
