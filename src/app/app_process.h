#ifndef APP_PROCESS_H
#define APP_PROCESS_H

#include "app_state.h"

bool app_process_run(AppState *state, const char *command, const char *const *paths, size_t count,
                     bool block, bool orphan);

#endif
