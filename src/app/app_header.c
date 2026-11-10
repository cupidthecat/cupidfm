#include "app_commands.h"
#include "app_paths.h"
#include "app_windows.h"
#include "main.h"

/* The whole header is composed in one pass so labels never overlay path text. */
void app_draw_tab_bar(AppState *state, const AppTabs *tabs) {
    WINDOW *target = g_kb.vim_keymap ? bannerwin : mainwin;
    if (!target || !tabs->count || LINES < 5 || COLS < 12)
        return;
    int width = getmaxx(target);
    int first = 0, visible = (int)tabs->count, slot, start;
    if (g_kb.vim_keymap) {
        werase(target);
        int available = width - width / 3 - 2;
        visible = MIN(visible, MAX(1, available / 4));
        slot = MIN(18, available / visible);
        first = MAX(0, (int)tabs->active - visible + 1);
        first = MIN(first, (int)tabs->count - visible);
        start = width - visible * slot - 1;
        wmove(target, 0, 0);
        browser_draw_name(target, state->current_directory, MAX(0, start - 1));
    } else {
        wmove(target, 0, 1);
        whline(target, ACS_HLINE, MAX(0, width - 2));
        if (tabs->count == 1) {
            wrefresh(target);
            return;
        }
        int available = width - 4;
        visible = MIN(visible, MAX(1, available / 4));
        slot = MIN(18, available / visible);
        first = MAX(0, (int)tabs->active - visible + 1);
        first = MIN(first, (int)tabs->count - visible);
        start = 2;
    }
    for (int item = 0; item < visible; item++) {
        size_t index = (size_t)(first + item);
        const AppState *tab = index == tabs->active ? state : tabs->saved[index];
        const char *label = tab->tab_name[0] ? tab->tab_name : basename_ptr(tab->current_directory);
        if (!*label)
            label = "/";
        int x = start + item * slot;
        if (index == tabs->active)
            wattron(target, A_REVERSE);
        wmove(target, 0, x);
        whline(target, ' ', slot);
        char number[16];
        snprintf(number, sizeof(number), " %zu:", index + 1);
        waddstr(target, number);
        browser_draw_name(target, label, MAX(0, slot - 4));
        if (index == tabs->active)
            wattroff(target, A_REVERSE);
    }
    wrefresh(target);
}
