#ifndef APP_COMMANDS_H
#define APP_COMMANDS_H

#include "app_session.h"
#include "config.h"

typedef enum { APP_KEY_PASS, APP_KEY_HANDLED, APP_KEY_QUIT } AppKeyResult;

/* May translate a profile key to a configured CupidFM action. */
AppKeyResult app_browser_key(AppState *state, AppTabs *tabs, int *key, const KeyBindings *bindings);
void app_draw_tab_bar(AppState *state, const AppTabs *tabs);

#endif
