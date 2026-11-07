#define _GNU_SOURCE
#include "syntax_rules.h"
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    SyntaxRules *r = syntax_rules_load(argv[1]);
    if (!r)
        return 3;
    FILE *f = fopen(argv[2], "r");
    if (!f)
        return 4;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    int state = 0;
    puts("[");
    int first = 1;
    while ((n = getline(&line, &cap, f)) >= 0) {
        if (n && line[n - 1] == '\n')
            line[--n] = 0;
        unsigned char *colors = malloc(n + 1);
        state = syntax_rules_line(r, line, state, colors);
        if (!first)
            puts(",");
        first = 0;
        putchar('[');
        for (int i = 0; i < n;) {
            if (i)
                putchar(',');
            printf("%u", colors[i]);
            const char *next = g_utf8_next_char(line + i);
            i = (int)(next - line);
        }
        putchar(']');
        free(colors);
    }
    puts("]");
    free(line);
    fclose(f);
    syntax_rules_free(r);
}
