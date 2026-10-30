#ifndef TEXT_PREVIEW_H
#define TEXT_PREVIEW_H
#include "syntax.h"
#include <ncurses.h>
#include <stdio.h>

void text_preview_clear_index(void);

/* Count at most limit physical lines; the caller owns stream. */
int text_preview_count_lines(FILE *stream, int limit);

/* Reads only the prefix needed for this viewport; the caller owns stream. */
void draw_text_preview(WINDOW *window, FILE *stream, SyntaxDef *syntax, int start_line, int max_y,
                       int max_x);
#endif
