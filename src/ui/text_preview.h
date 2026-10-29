#ifndef TEXT_PREVIEW_H
#define TEXT_PREVIEW_H
#include <stdio.h>
#include <ncurses.h>
#include "syntax.h"

/* Reads only the prefix needed for this viewport; the caller owns stream. */
void draw_text_preview(WINDOW *window, FILE *stream, SyntaxDef *syntax,
                       int start_line, int max_y, int max_x);
#endif
