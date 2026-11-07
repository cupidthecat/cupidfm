#define _GNU_SOURCE
#include "input_prompt.h"

#include <assert.h>
#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

WINDOW *notifwin;
volatile sig_atomic_t resized;

static char last_live[256];
static int updates;

static void changed(const char *text, void *context) {
    assert(context == &updates);
    snprintf(last_live, sizeof(last_live), "%s", text);
    updates++;
}

static void keys(const char *text) {
    size_t length = strlen(text);
    while (length)
        assert(ungetch((unsigned char)text[--length]) != ERR);
}

int main(void) {
    assert(setlocale(LC_ALL, "C.UTF-8"));
    FILE *input = tmpfile(), *output = tmpfile();
    SCREEN *screen = newterm("xterm-256color", output, input);
    assert(screen && set_term(screen));
    assert(resizeterm(30, 100) != ERR);
    notifwin = newwin(1, 100, 29, 0);
    assert(notifwin);
    char value[256] = "sample.txt";
    InputPromptOptions options = {.cursor = INPUT_CURSOR_BEFORE_EXT};
    keys("_new\n");
    assert(input_prompt_ex("Rename", value, sizeof(value), &options));
    assert(!strcmp(value, "sample_new.txt"));
    snprintf(value, sizeof(value), ".config");
    keys("_new\n");
    assert(input_prompt_ex("Rename", value, sizeof(value), &options));
    assert(!strcmp(value, ".config_new"));
    value[0] = 0;
    keys("ab\032\031\n");
    assert(input_prompt("Input", value, sizeof(value)) && !strcmp(value, "ab"));
    value[0] = 0;
    keys("é\032\n");
    assert(input_prompt("Input", value, sizeof(value)) && !*value);
    value[0] = 0;
    keys("é\032\031\n");
    assert(input_prompt("Input", value, sizeof(value)) && !strcmp(value, "é"));
    options = (InputPromptOptions){.history = "test commands"};
    value[0] = 0;
    keys("first command\n");
    assert(input_prompt_ex("Command", value, sizeof(value), &options));
    value[0] = 0;
    keys("draft\020\016\n");
    assert(input_prompt_ex("Command", value, sizeof(value), &options));
    assert(!strcmp(value, "draft"));
    value[0] = 0;
    keys("\020\n");
    assert(input_prompt_ex("Command", value, sizeof(value), &options));
    assert(!strcmp(value, "draft"));
    options.history = "separate context";
    value[0] = 0;
    keys("\020\n");
    assert(input_prompt_ex("Command", value, sizeof(value), &options) && !*value);
    options = (InputPromptOptions){.changed = changed, .context = &updates};
    snprintf(value, sizeof(value), "before");
    keys("_edit\033");
    assert(!input_prompt_ex("Filter", value, sizeof(value), &options));
    assert(!strcmp(value, "before") && !strcmp(last_live, "before_edit") && updates > 1);
    delwin(notifwin);
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    puts("Prompt cursor presets, UTF-8 undo/redo, history, live updates, and cancellation passed");
}
