#define _POSIX_C_SOURCE 200809L
#include "text_preview.h"
#include <ctype.h>
#include <stdlib.h>

void draw_text_preview(WINDOW *window, FILE *stream, SyntaxDef *syntax,
                       int start_line, int max_y, int max_x) {
    if (!window || !stream || max_y <= 8 || max_x <= 4) return;
    size_t first = start_line > 0 ? (size_t)start_line : 0;
    size_t index = 0;
    int row = 7, state = 0;
    char *line = NULL;
    size_t capacity = 0;
    while (row < max_y - 1) {
        ssize_t length = getline(&line, &capacity, stream);
        if (length < 0) {
            if (!feof(stream)) {
                mvwprintw(window, row, 2, "Unable to read file for preview");
            } else {
                mvwprintw(window, row++, 2, "--------------------------------");
                if (row < max_y - 1) mvwprintw(window, row, 2, "[End of file]");
            }
            break;
        }
        while (length && (line[length - 1] == '\n' || line[length - 1] == '\r')) line[--length] = 0;
        /* Retain physical lines, replacing terminal control bytes before display. */
        for (ssize_t i = 0; i < length; i++) {
            unsigned char c = (unsigned char)line[i];
            if (!isprint(c)) line[i] = ' ';
        }
        int next_state = syntax_scan_line_state(line, syntax, state);
        if (index >= first) {
            syntax_highlight_line(window, line, syntax, &state, row++, 2,
                                  max_x - 4, NULL, 0, 0);
        }
        /* Delimiters beyond the visible columns still affect the next line. */
        state = next_state;
        index++;
    }
    free(line);
}
