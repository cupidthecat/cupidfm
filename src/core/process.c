#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "process.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

bool run_process(char *const argv[], int input_fd, int output_fd) {
    return run_process_in_directory(argv, NULL, input_fd, output_fd);
}

bool run_process_in_directory(char *const argv[], const char *directory, int input_fd,
                              int output_fd) {
    return run_process_status_in_directory(argv, directory, input_fd, output_fd) == 0;
}

int run_process_status_in_directory(char *const argv[], const char *directory, int input_fd,
                                    int output_fd) {
    if (!argv || !argv[0]) {
        errno = EINVAL;
        return -1;
    }
    posix_spawn_file_actions_t actions;
    int action_error = posix_spawn_file_actions_init(&actions);
    if (action_error) {
        errno = action_error;
        return -1;
    }
    int error = 0;
    if (directory)
        error = posix_spawn_file_actions_addchdir_np(&actions, directory);
    if (!error && input_fd >= 0)
        error = posix_spawn_file_actions_adddup2(&actions, input_fd, STDIN_FILENO);
    if (!error && output_fd >= 0)
        error = posix_spawn_file_actions_adddup2(&actions, output_fd, STDOUT_FILENO);
    if (!error && input_fd > STDERR_FILENO)
        error = posix_spawn_file_actions_addclose(&actions, input_fd);
    if (!error && output_fd > STDERR_FILENO && output_fd != input_fd)
        error = posix_spawn_file_actions_addclose(&actions, output_fd);
    pid_t child;
    posix_spawnattr_t attributes;
    int attribute_error = posix_spawnattr_init(&attributes);
    if (!error)
        error = attribute_error;
    if (!attribute_error) {
        sigset_t defaults;
        sigemptyset(&defaults);
        sigaddset(&defaults, SIGINT);
        sigaddset(&defaults, SIGQUIT);
        sigaddset(&defaults, SIGTSTP);
        if (!error)
            error = posix_spawnattr_setsigdefault(&attributes, &defaults);
        if (!error)
            error = posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGDEF);
        if (!error)
            error = posix_spawnp(&child, argv[0], &actions, &attributes, argv, environ);
        posix_spawnattr_destroy(&attributes);
    }
    posix_spawn_file_actions_destroy(&actions);
    if (error) {
        errno = error;
        return -1;
    }
    int status;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child)
        return -1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
}

bool file_command_copy(const char *src, const char *dst, bool recursive) {
    if (!src || !*src || !dst || !*dst)
        return false;
    char *args[] = {"cp", "-P", "--", (char *)src, (char *)dst, NULL};
    char *recursive_args[] = {"cp", "-R", "-P", "--", (char *)src, (char *)dst, NULL};
    return run_process(recursive ? recursive_args : args, -1, -1);
}

bool file_command_move(const char *src, const char *dst) {
    if (!src || !*src || !dst || !*dst)
        return false;
    char *args[] = {"mv", "--", (char *)src, (char *)dst, NULL};
    return run_process(args, -1, -1);
}

bool file_command_rename(const char *src, const char *dst) {
    if (!src || !*src || !dst || !*dst) {
        errno = EINVAL;
        return false;
    }
    if (!strcmp(src, dst)) {
        struct stat info;
        return lstat(src, &info) == 0;
    }
#ifdef __linux__
    return renameat2(AT_FDCWD, src, AT_FDCWD, dst, RENAME_NOREPLACE) == 0;
#else
    struct stat existing;
    if (!lstat(dst, &existing)) {
        errno = EEXIST;
        return false;
    }
    if (errno != ENOENT)
        return false;
    return rename(src, dst) == 0;
#endif
}

bool file_command_remove_directory(const char *path) {
    if (!path || !*path)
        return false;
    char *args[] = {"rm", "-rf", "--", (char *)path, NULL};
    return run_process(args, -1, -1);
}
