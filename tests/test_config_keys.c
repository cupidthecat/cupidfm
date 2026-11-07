#define _POSIX_C_SOURCE 200809L
#include "../src/core/config.c"
#include <assert.h>
#include <unistd.h>

int main(void) {
    char name[32];
    for (int number = 1; number <= 63; number++) {
        keycode_to_config_string(KEY_F(number), name, sizeof(name));
        assert(parse_key(name) == KEY_F(number));
        snprintf(name, sizeof(name), "KEY_F(%d)", number);
        assert(parse_key(name) == KEY_F(number));
    }
    assert(parse_key("F0") == -1 && parse_key("F64") == -1);
    assert(parse_key("F1junk") == -1 && parse_key("KEY_F(1junk)") == -1);
    assert(parse_key("KEY_F(1)junk") == -1);
    assert(parse_key("KEY_F(999999999999999999999)") == -1);
    assert(parse_key("Shift+Minus") == '_');
    assert(parse_key("Shift+Comma") == '<');
    assert(parse_key("Shift+7") == '&');
    assert(parse_key("Shift+a") == 'A');
    assert(parse_key("Shift+Invalid") == -1 && parse_key("Shift+") == -1);
    char path[] = "/tmp/cupidfm-config-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && !close(fd));
    KeyBindings config;
    load_default_keybindings(&config);
    assert(config.pane_parent == 1 && config.pane_current == 4 && config.pane_preview == 3 &&
           config.scrolloff == 5);
    char errors[512];
    assert(write_default_config_file(path, &config, errors, sizeof(errors)));
    config.pane_parent = config.pane_current = config.pane_preview = config.scrolloff = 0;
    assert(!load_config_file(&config, path, errors, sizeof(errors)));
    assert(config.pane_parent == 1 && config.pane_current == 4 && config.pane_preview == 3 &&
           config.scrolloff == 5);
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs("keymap=vim\npane_parent=0\npane_current=1\npane_preview=0\nscrolloff=0\n",
                 file) >= 0);
    assert(!fclose(file));
    assert(!load_config_file(&config, path, errors, sizeof(errors)));
    assert(config.vim_keymap && !config.pane_parent && config.pane_current == 1 &&
           !config.pane_preview && !config.scrolloff);
    const char *invalid[] = {"pane_current=0", "pane_parent=-1", "pane_preview=101",
                             "scrolloff=999999999999999999999", "scrolloff=4junk"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); i++) {
        file = fopen(path, "w");
        assert(file && fputs(invalid[i], file) >= 0 && !fclose(file));
        assert(load_config_file(&config, path, errors, sizeof(errors)) == 1);
        assert(strstr(errors, "Invalid "));
    }
    assert(!unlink(path));
    puts("Configuration function-key checks passed");
}
