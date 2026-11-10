#include "app_layout.h"

#include <assert.h>
#include <stdio.h>

static void bounded(AppRect rect, int rows, int columns) {
    if (!rect.rows || !rect.columns)
        return;
    assert(rect.x >= 0 && rect.y >= 0 && rect.rows > 0 && rect.columns > 0);
    assert(rect.x + rect.columns <= columns);
    assert(rect.y + rect.rows <= rows);
}

int main(void) {
    const int ratios[][3] = {{1, 4, 3}, {0, 1, 0}, {100, 1, 100}, {0, 0, 0}};
    for (int modern = 0; modern < 2; modern++) {
        for (unsigned setting = 0; setting < sizeof(ratios) / sizeof(*ratios); setting++) {
            KeyBindings config = {.vim_keymap = modern,
                                  .pane_parent = ratios[setting][0],
                                  .pane_current = ratios[setting][1],
                                  .pane_preview = ratios[setting][2]};
            for (int rows = 1; rows <= 50; rows++) {
                for (int columns = 1; columns <= 180; columns++) {
                    AppLayout layout = app_layout_compute(rows, columns, &config);
                    bounded(layout.header, rows, columns);
                    bounded(layout.body, rows, columns);
                    bounded(layout.parent, rows, columns);
                    bounded(layout.current, rows, columns);
                    bounded(layout.preview, rows, columns);
                    bounded(layout.status, rows, columns);
                    bounded(layout.notice, rows, columns);
                    assert(layout.current.columns > 0 && layout.current.rows > 0);
                    if (layout.parent.columns)
                        assert(layout.parent.x + layout.parent.columns == layout.current.x);
                    if (layout.preview.columns)
                        assert(layout.current.x + layout.current.columns == layout.preview.x);
                }
            }
        }
    }
    KeyBindings defaults = {
        .vim_keymap = true, .pane_parent = 1, .pane_current = 4, .pane_preview = 3};
    AppLayout layout = app_layout_compute(32, 120, &defaults);
    assert(layout.parent.columns == 15 && layout.current.columns == 60 &&
           layout.preview.columns == 45);
    assert(layout.header.rows == 1 && layout.status.rows == 1);
    layout = app_layout_compute(20, 60, &defaults);
    assert(!layout.parent.columns && layout.preview.columns && layout.current.columns >= 20);
    layout = app_layout_compute(20, 30, &defaults);
    assert(!layout.parent.columns && !layout.preview.columns && layout.current.columns == 30);
    puts("Responsive layout bounds, pane ratios, and collapsed panes passed");
    return 0;
}
