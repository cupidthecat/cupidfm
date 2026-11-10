#ifndef APP_SEARCH_H
#define APP_SEARCH_H

#include "app_state.h"

void app_search_prompt(AppState *state, bool content);
void app_search_reload(AppState *state);
void app_search_stop(AppState *state, bool reload);
bool app_search_poll(AppState *state);

#endif
