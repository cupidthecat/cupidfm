#ifndef CUPIDFM_PROCESS_H
#define CUPIDFM_PROCESS_H

#include <stdbool.h>

/* Run literal arguments, optionally redirecting stdin/stdout. -1 inherits the
 * corresponding descriptor. Only a normal exit with status zero succeeds. */
bool run_process(char *const argv[], int input_fd, int output_fd);
bool file_command_copy(const char *src, const char *dst, bool recursive);
bool file_command_move(const char *src, const char *dst);
bool file_command_remove_directory(const char *path);

#endif
