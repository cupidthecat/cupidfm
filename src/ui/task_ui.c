#include "task_ui.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "input_prompt.h"
#include "tasks.h"
#include "ui.h"
#include "utils.h"

bool task_ui_can_quit(void) {
    if (!tasks_busy())
        return true;
    char answer[8] = {0};
    return input_prompt("Unfinished tasks. Cancel them and quit? (y/N)", answer, sizeof(answer)) &&
           (!strcmp(answer, "y") || !strcmp(answer, "Y"));
}

void task_ui_show(void) {
    TaskInfo *info = calloc(128, sizeof(*info));
    if (!info)
        return;
    int rows = MAX(3, MIN(LINES - 2, 22)), cols = MAX(12, MIN(COLS - 2, 100));
    WINDOW *window = newwin(rows, cols, MAX(0, (LINES - rows) / 2), MAX(0, (COLS - cols) / 2));
    if (!window) {
        free(info);
        return;
    }
    keypad(window, TRUE);
    wtimeout(window, 100);
    size_t cursor = 0;
    static const char *states[] = {"queued", "running", "done", "failed", "cancelled"};
    static const char *kinds[] = {"copy",     "move",    "trash",
                                  "delete",   "symlink", "relative symlink",
                                  "hardlink", "shell",   "create"};
    for (;;) {
        size_t count = tasks_snapshot(info, 128);
        if (count && cursor >= count)
            cursor = count - 1;
        size_t start = cursor >= (size_t)(rows - 3) ? cursor - (size_t)(rows - 3) + 1 : 0;
        werase(window);
        box(window, 0, 0);
        mvwaddnstr(window, 0, 2, " Tasks: x cancel, Enter inspect, w/Esc close ", cols - 4);
        if (!count)
            mvwaddnstr(window, 1, 2, "No tasks", cols - 4);
        for (size_t i = start; i < count && i - start < (size_t)(rows - 2); i++) {
            char line[256];
            snprintf(line, sizeof(line), "%u %-9s %-16s %zu/%zu (%llu bytes)", info[i].id,
                     states[info[i].status], kinds[info[i].kind], info[i].completed, info[i].total,
                     (unsigned long long)info[i].bytes);
            if (i == cursor)
                wattron(window, A_REVERSE);
            mvwaddnstr(window, (int)(i - start) + 1, 1, line, cols - 2);
            if (i == cursor)
                wattroff(window, A_REVERSE);
        }
        wrefresh(window);
        int key = wgetch(window);
        if (key == 27 || key == 'w' || key == 3)
            break;
        if (key == KEY_RESIZE) {
            resized = 1;
            break;
        }
        if ((key == 'j' || key == KEY_DOWN) && cursor + 1 < count)
            cursor++;
        else if ((key == 'k' || key == KEY_UP) && cursor)
            cursor--;
        else if (key == 'x' && count)
            tasks_cancel(info[cursor].id);
        else if ((key == '\n' || key == KEY_ENTER) && count)
            show_popup("Task details", "%s",
                       info[cursor].message[0] ? info[cursor].message
                                               : states[info[cursor].status]);
    }
    delwin(window);
    free(info);
    touchwin(stdscr);
    refresh();
}
