#include "app_layout.h"

#include <stdint.h>

static int maximum(int a, int b) {
    return a > b ? a : b;
}

AppLayout app_layout_compute(int rows, int columns, const KeyBindings *settings) {
    rows = maximum(1, rows);
    columns = maximum(1, columns);
    bool modern = settings && settings->vim_keymap;
    AppLayout result = {0};
    if (rows < 5 || columns < 12) {
        result.compact = true;
        result.header = result.body = result.current = (AppRect){0, 0, rows, columns};
        result.notice = (AppRect){rows - 1, 0, 1, columns};
        return result;
    }
    int header = modern ? 1 : (rows >= 10 ? 3 : 1);
    int status = modern ? 1 : 0;
    int body = rows - header - status - 1;
    result.header = (AppRect){0, 0, header, columns};
    result.body = (AppRect){header, 0, body, columns};
    result.notice = (AppRect){rows - 1, 0, 1, columns};
    if (status)
        result.status = (AppRect){rows - 2, 0, 1, columns};
    int available = modern ? columns : columns - 2;
    int left = modern ? 0 : 1;
    int top = modern ? header : header + 1;
    int height = modern ? body : maximum(1, body - 2);
    int parent = 0, preview = 0;
    int pw = settings ? settings->pane_parent : 1;
    int cw = maximum(1, settings ? settings->pane_current : 4);
    int vw = settings ? settings->pane_preview : 3;
    pw = maximum(0, pw);
    vw = maximum(0, vw);
    if (modern && available >= 80 && pw)
        parent = maximum(10, (int)((int64_t)available * pw / ((int64_t)pw + cw + vw)));
    if (available >= 48 && vw) {
        preview = (int)((int64_t)available * vw / ((int64_t)pw + cw + vw));
        preview = maximum(20, preview);
    }
    /* Keep the current list usable even with extreme configured ratios. */
    if (available - parent - preview < 20)
        parent = 0;
    if (available - parent - preview < 20)
        preview = maximum(0, available - 20);
    if (!modern && available >= 48)
        preview = available * 2 / 3;
    int current = available - parent - preview;
    if (parent)
        result.parent = (AppRect){top, left, height, parent};
    result.current = (AppRect){top, left + parent, height, maximum(1, current)};
    if (preview)
        result.preview = (AppRect){top, left + parent + current, height, preview};
    return result;
}
