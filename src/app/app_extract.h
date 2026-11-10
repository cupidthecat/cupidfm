#ifndef APP_EXTRACT_H
#define APP_EXTRACT_H

#include "app_state.h"

size_t app_extract_paths(AppState *state, const char *const *paths, size_t count,
                         const char *directory);
void app_extract_selected(AppState *state, bool choose_directory);

#endif
