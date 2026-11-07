#ifndef APP_OPEN_H
#define APP_OPEN_H

#include "app_state.h"
#include "openers.h"

Openers *app_openers_load(void);
void app_open_paths(AppState *state, bool interactive, const char *const *paths, size_t count);

#endif
