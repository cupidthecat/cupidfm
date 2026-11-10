#define _GNU_SOURCE
#include "syntax.h"
#include "syntax_rules.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void expect(SyntaxRules *rules, const char *line, const char *token, int color, int *state) {
    unsigned char colors[4096];
    assert(strlen(line) < sizeof(colors));
    int scan = syntax_rules_line(rules, line, *state, NULL);
    *state = syntax_rules_line(rules, line, *state, colors);
    assert(scan == *state);
    const char *at = strstr(line, token);
    assert(at);
    for (size_t i = 0; i < strlen(token); i++)
        assert(colors[at - line + i] == color);
}

int main(void) {
    DIR *dir = opendir("../syntax");
    assert(dir);
    struct dirent *entry;
    int count = 0;
    while ((entry = readdir(dir))) {
        if (!strstr(entry->d_name, ".cupidconf"))
            continue;
        char path[1024];
        snprintf(path, sizeof(path), "../syntax/%s", entry->d_name);
        SyntaxRules *rules = syntax_rules_load(path);
        assert(rules);
        unsigned char colors[256];
        int state = 0;
        const char *lines[] = {"", "hello world", "\"escaped \\\" quote\" // test", "/* test",
                               "end */"};
        for (size_t i = 0; i < 5; i++)
            state = syntax_rules_line(rules, lines[i], state, colors);
        syntax_rules_free(rules);
        count++;
    }
    closedir(dir);
    assert(count == 158);
    SyntaxRules *c = syntax_rules_load("../syntax/c.cupidconf");
    assert(c && syntax_rules_detect(c, "filename", "hello.c"));
    int state = 0;
    expect(c, "int main() { return 12; }", "int", COLOR_SYNTAX_TYPE, &state);
    expect(c, "/* TODO: comment", "TODO", 23, &state);
    assert(state);
    expect(c, "still a comment */ int n;", "still", COLOR_SYNTAX_COMMENT, &state);
    assert(!state);
    expect(c, "\"escaped \\\" quote\"; int n;", "quote", COLOR_SYNTAX_STRING, &state);
    assert(!state);
    expect(c, "\"a\" \"b\" int n;", "int", COLOR_SYNTAX_TYPE, &state);
    assert(!state);
    char repeated[2048] = {0};
    for (int i = 0; i < 200; i++)
        strcat(repeated, "\"x\" ");
    strcat(repeated, "int n;");
    state = 0;
    expect(c, repeated, "int", COLOR_SYNTAX_TYPE, &state);
    assert(!state);
    syntax_rules_free(c);
    char home[] = "/tmp/cupidfm-syntax-XXXXXX";
    assert(mkdtemp(home));
    assert(!setenv("HOME", home, 1));
    assert(!setenv("CUPIDFM_SYNTAX_DIR", "../syntax", 1));
    syntax_init();
    SyntaxDef *definition = syntax_get_for_file("test.py");
    assert(definition && definition->rules && !strcmp(definition->language, "python"));
    char script[4096];
    snprintf(script, sizeof(script), "%s/unknown", home);
    FILE *file = fopen(script, "w");
    assert(file && fputs("#!/usr/bin/env python3\nprint(1)\n", file) >= 0 && !fclose(file));
    definition = syntax_get_for_file(script);
    assert(definition && !strcmp(definition->language, "python"));
    file = tmpfile();
    FILE *input = tmpfile();
    SCREEN *screen = newterm("xterm-256color", file, input);
    assert(screen);
    set_term(screen);
    start_color();
    syntax_init_colors();
    short foreground, background;
    assert(pair_content(44, &foreground, &background) != ERR && background == -1);
    assert(pair_content(COLOR_SYNTAX_COMMENT, &foreground, &background) != ERR && background == -1);
    WINDOW *window = newwin(5, 30, 0, 0);
    assert(window);
    definition = syntax_get_for_file("test.c");
    state = 0;
    syntax_highlight_line(window, "int n; /* offscreen comment", definition, &state, 0, 0, 5, NULL,
                          0, 0);
    assert(state);
    syntax_highlight_line(window, "still */ int n;", definition, &state, 1, 0, 28, NULL, 0, 0);
    assert(!state);
    assert(PAIR_NUMBER(mvwinch(window, 1, 0)) == COLOR_SYNTAX_COMMENT);
    delwin(window);
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(file);
    syntax_cleanup();
    char preferences[4096], directory[4096], override[4096];
    snprintf(preferences, sizeof(preferences), "%s/.cupidfm", home);
    snprintf(directory, sizeof(directory), "%s/.cupidfm/syntax", home);
    snprintf(override, sizeof(override), "%s/c.cupidconf", directory);
    assert(!mkdir(preferences, 0700) && !mkdir(directory, 0700));
    file = fopen(override, "w");
    assert(file);
    assert(fputs("format=2\nname='c'\ndetect.filename='\\.c$'\n"
                 "rule.0.parent=-1\nrule.0.group='comment'\nrule.0.match='.'\nrules=1\n",
                 file) >= 0 &&
           !fclose(file));
    syntax_init();
    definition = syntax_get_for_file("test.c");
    assert(definition && definition->rules);
    state = 0;
    expect(definition->rules, "int n;", "int", COLOR_SYNTAX_COMMENT, &state);
    syntax_cleanup();
    file = fopen(override, "w");
    assert(file && fputs("format=2\nname='invalid'\nrules=1\n", file) >= 0 && !fclose(file));
    assert(!syntax_rules_load(override));
    assert(!unlink(override) && !rmdir(directory) && !rmdir(preferences));
    assert(!unlink(script) && !rmdir(home));
    puts("158 syntax definitions, detection, escapes, multiline states, and tokens passed");
}
