#include "fuzzy.h"
#define _GNU_SOURCE
#include "../src/ui/ui.c"
#include <assert.h>

WINDOW *notifwin, *bannerwin;
volatile sig_atomic_t resized;
const char *BANNER_TEXT, *BUILD_INFO;
int banner_offset;
pthread_mutex_t banner_mutex = PTHREAD_MUTEX_INITIALIZER;

void draw_scrolling_banner_nout(WINDOW *w, const char *t, const char *b, int o) {
    (void)w;
    (void)t;
    (void)b;
    (void)o;
}

static bool has(const StrVec *lines, const char *text) {
    for (size_t i = 0; i < lines->count; i++)
        if (strstr(lines->lines[i], text))
            return true;
    return false;
}

int main(void) {
    KeyBindings keys;
    load_default_keybindings(&keys);
    keys.vim_keymap = true;
    StrVec lines;
    build_help_lines(&lines, &keys, 100, "");
    assert(has(&lines, "Vim browser") && has(&lines, "Mouse") && has(&lines, "Tabs"));
    assert(has(&lines, "chooser") || has(&lines, "--cwd-file"));
    assert(has(&lines, "syntax") && has(&lines, "Editor (configured keys)"));
    assert(!has(&lines, "Ctrl+C") || !has(&lines, "Copy file/directory"));
    assert(has(&lines, "t then t (tt)") && has(&lines, "t then r (tr)"));
    assert(has(&lines, "Key sequences:") && has(&lines, "fuzzy"));
    strvec_free(&lines);
    assert(fuzzy_score("ntab", "Create a new tab") >= 0);
    assert(fuzzy_score("N TAB", "Create a new tab") >= 0);
    assert(fuzzy_score("xyz", "Create a new tab") < 0);
    assert(fuzzy_score("tab", "Tabs") < fuzzy_score("tab", "Information about another tab"));
    build_help_lines(&lines, &keys, 100, "ntab");
    assert(has(&lines, "t then t (tt)"));
    assert(!has(&lines, "Foreground") && !has(&lines, "Mouse"));
    strvec_free(&lines);
    build_help_lines(&lines, &keys, 100, "zzzzzz");
    assert(has(&lines, "No matching"));
    strvec_free(&lines);
    assert(strstr(app_help_prefix('t'), "t new tab"));
    assert(strstr(app_help_prefix('t'), "r rename tab"));
    keys.vim_keymap = false;
    keys.edit_save = KEY_F(9);
    build_help_lines(&lines, &keys, 100, "");
    assert(has(&lines, "Original browser") && has(&lines, "F9"));
    assert(has(&lines, "Additional browser keys") && has(&lines, "t then t"));
    strvec_free(&lines);
    const int widths[] = {8, 20, 60};
    for (size_t i = 0; i < 3; i++) {
        build_help_lines(&lines, &keys, widths[i], "");
        assert(lines.count);
        for (size_t j = 0; j < lines.count; j++)
            assert(strlen(lines.lines[j]) <= (size_t)widths[i]);
        strvec_free(&lines);
    }
    FILE *input = tmpfile(), *output = tmpfile();
    SCREEN *screen = newterm("xterm-256color", output, input);
    assert(screen);
    set_term(screen);
    resizeterm(30, 100);
    wtimeout(stdscr, 0);
    assert(ungetch('Q') != ERR);
    assert(ungetch(10) != ERR);
    assert(ungetch('Q') != ERR);
    assert(ungetch('/') != ERR);
    show_help_menu(&keys);
    assert(getch() == 10); // The first Q closes search mode without consuming later keys.
    assert(getch() == 'Q');
    const char *escapes[] = {"", "/", "/tab\n"};
    for (size_t i = 0; i < 3; i++) {
        assert(ungetch('!') != ERR);
        assert(ungetch(27) != ERR);
        for (size_t n = strlen(escapes[i]); n; n--)
            assert(ungetch(escapes[i][n - 1]) != ERR);
        show_help_menu(&keys);
        assert(getch() == '!');
    }
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    puts("Profile-specific help, configured editor keys, syntax setup, fuzzy ranking, and wrapping "
         "passed");
}
