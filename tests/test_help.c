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
    build_help_lines(&lines, &keys, 100);
    assert(has(&lines, "Vim browser") && has(&lines, "Mouse") && has(&lines, "Tabs"));
    assert(has(&lines, "chooser") || has(&lines, "--cwd-file"));
    assert(has(&lines, "syntax") && has(&lines, "Editor (configured keys)"));
    assert(!has(&lines, "Ctrl+C") || !has(&lines, "Copy file/directory"));
    strvec_free(&lines);
    keys.vim_keymap = false;
    keys.edit_save = KEY_F(9);
    build_help_lines(&lines, &keys, 100);
    assert(has(&lines, "Original browser") && has(&lines, "F9"));
    strvec_free(&lines);
    const int widths[] = {8, 20, 60};
    for (size_t i = 0; i < 3; i++) {
        build_help_lines(&lines, &keys, widths[i]);
        assert(lines.count);
        for (size_t j = 0; j < lines.count; j++)
            assert(strlen(lines.lines[j]) <= (size_t)widths[i]);
        strvec_free(&lines);
    }
    puts("Profile-specific help, configured editor keys, syntax setup, and wrapping passed");
}
