#define _GNU_SOURCE
#include "app_commands.h"
#include "app_windows.h"
#include <assert.h>
#include <locale.h>
#include <stdio.h>
#include <string.h>

KeyBindings g_kb;
WINDOW *bannerwin, *mainwin;

const char *basename_ptr(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int main(void) {
    assert(setlocale(LC_ALL, "C.UTF-8"));
    FILE *input = tmpfile(), *output = tmpfile();
    assert(input && output);
    SCREEN *screen = newterm("xterm-256color", output, input);
    assert(screen);
    set_term(screen);
    assert(resizeterm(30, 120) != ERR);
    bannerwin = newwin(1, 120, 0, 0);
    mainwin = newwin(20, 120, 2, 0);
    assert(bannerwin && mainwin);
    char path[256];
    memset(path, 'm', sizeof(path) - 1);
    path[0] = '/';
    path[sizeof(path) - 1] = 0;
    AppState state = {.current_directory = path};
    AppState other = {.current_directory = "/other"};
    snprintf(state.tab_name, sizeof(state.tab_name), "x");
    snprintf(other.tab_name, sizeof(other.tab_name), "y");
    AppTabs tabs = {.count = 2, .active = 1};
    tabs.saved[0] = &other;
    g_kb.vim_keymap = true;
    app_draw_tab_bar(&state, &tabs);
    char text[256];
    assert(mvwinnstr(bannerwin, 0, 0, text, 120) >= 0);
    char *label = strstr(text, "1:y");
    assert(label && strstr(text, "2:x"));
    assert(!strchr(label, 'm'));
    /* Shortening labels and closing tabs cannot retain old suffixes. */
    snprintf(state.tab_name, sizeof(state.tab_name), "abcdefghijklmnop");
    app_draw_tab_bar(&state, &tabs);
    snprintf(state.tab_name, sizeof(state.tab_name), "z");
    tabs.count = 1;
    tabs.active = 0;
    app_draw_tab_bar(&state, &tabs);
    assert(mvwinnstr(bannerwin, 0, 0, text, 120) >= 0);
    label = strstr(text, "1:z");
    assert(label && !strchr(label, 'm') && !strstr(label, "abc"));
    tabs.count = 9;
    tabs.active = 8;
    for (int i = 0; i < 8; i++)
        tabs.saved[i] = &other;
    assert(wresize(bannerwin, 1, 12) != ERR);
    app_draw_tab_bar(&state, &tabs);
    assert(mvwinnstr(bannerwin, 0, 0, text, 12) >= 0);
    assert(strstr(text, "9:"));
    g_kb.vim_keymap = false;
    app_draw_tab_bar(&state, &tabs);
    tabs.count = 1;
    tabs.active = 0;
    app_draw_tab_bar(&state, &tabs);
    assert(mvwinnstr(mainwin, 0, 0, text, 120) >= 0);
    assert(!strstr(text, "9:"));
    const char *icons[] = {"📁", "⚙️", "x"};
    for (int mode = 0; mode < 2; mode++) {
        g_kb.vim_keymap = mode;
        for (int i = 0; i < 3; i++) {
            browser_draw_entry_prefix(mainwin, i + 1, false, icons[i], "🟢");
            assert(getcurx(mainwin) == (mode ? 4 : 9));
            if (!mode) {
                char symbol[16];
                assert(mvwinnstr(mainwin, i + 1, 6, symbol, 4) >= 0);
                assert(!strncmp(symbol, "🟢", strlen("🟢")));
            }
        }
    }
    delwin(bannerwin);
    delwin(mainwin);
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    puts("Header path/tab separation, shorter labels, closing tabs, and narrow screens passed");
}
