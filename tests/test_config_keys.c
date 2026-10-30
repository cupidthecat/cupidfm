#define _POSIX_C_SOURCE 200809L
#include "../src/core/config.c"
#include <assert.h>

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
    puts("Configuration function-key checks passed");
}
