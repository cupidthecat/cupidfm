#ifndef APP_LAYOUT_H
#define APP_LAYOUT_H

#include "config.h"

typedef struct {
    int y, x, rows, columns;
} AppRect;

typedef struct {
    AppRect header, body, parent, current, preview, status, notice;
    bool compact;
} AppLayout;

AppLayout app_layout_compute(int rows, int columns, const KeyBindings *settings);

#endif
