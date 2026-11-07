#ifndef APP_MOUSE_H
#define APP_MOUSE_H

#include "app_state.h"

typedef enum {
    APP_MOUSE_NONE,
    APP_MOUSE_HANDLED,
    APP_MOUSE_ENTER,
    APP_MOUSE_PREVIEW_UP,
    APP_MOUSE_PREVIEW_DOWN
} AppMouseResult;

AppMouseResult app_mouse_event(AppState *state, const MEVENT *event);

#endif
