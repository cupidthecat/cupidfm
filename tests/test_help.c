#define _GNU_SOURCE
#include "../src/ui/ui.c"
#include <assert.h>

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
    assert(app_help_fuzzy_score("ntab", "Create a new tab") >= 0);
    assert(app_help_fuzzy_score("N TAB", "Create a new tab") >= 0);
    assert(app_help_fuzzy_score("xyz", "Create a new tab") < 0);
    assert(app_help_fuzzy_score("tab", "Tabs") <
           app_help_fuzzy_score("tab", "Information about another tab"));
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
    puts("Profile-specific help, configured editor keys, syntax setup, fuzzy ranking, and wrapping "
         "passed");
}
