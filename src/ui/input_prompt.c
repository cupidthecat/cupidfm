#include "input_prompt.h"

#include <ncurses.h>
#include <stdlib.h>
#include <string.h>

#include "globals.h"
#include "main.h"
#include "utils.h"

static size_t previous_char(const char *text, size_t position) {
    if (position)
        position--;
    while (position && ((unsigned char)text[position] & 0xc0) == 0x80)
        position--;
    return position;
}

static size_t next_char(const char *text, size_t position) {
    if (text[position])
        position++;
    while (text[position] && ((unsigned char)text[position] & 0xc0) == 0x80)
        position++;
    return position;
}

bool input_prompt(const char *title, char *value, size_t capacity) {
    if (!title || !value || !capacity)
        return false;
    char *text = calloc(capacity, 1);
    if (!text)
        return false;
    snprintf(text, capacity, "%s", value);
    size_t length = strlen(text), position = length;
    int delay = wgetdelay(stdscr);
    wtimeout(stdscr, -1);
    curs_set(1);
    bool submitted = false;
    for (;;) {
        int width = getmaxx(notifwin);
        int prefix = MIN((int)strlen(title) + 2, MAX(0, width / 2));
        int room = MAX(1, width - prefix - 1);
        size_t start = position > (size_t)room ? position - (size_t)room : 0;
        while (start && ((unsigned char)text[start] & 0xc0) == 0x80)
            start--;
        werase(notifwin);
        mvwaddnstr(notifwin, 0, 0, title, MAX(0, prefix - 2));
        waddstr(notifwin, ": ");
        waddnstr(notifwin, text + start, room);
        wmove(notifwin, 0, MIN(width - 1, prefix + (int)(position - start)));
        wrefresh(notifwin);
        int key = getch();
        if (key == 27 || key == 3)
            break;
        if (key == '\n' || key == '\r' || key == KEY_ENTER) {
            snprintf(value, capacity, "%s", text);
            submitted = true;
            break;
        }
        if (key == KEY_RESIZE) {
            resized = 1;
            break;
        }
        if (key == KEY_LEFT || key == 2)
            position = previous_char(text, position);
        else if (key == KEY_RIGHT || key == 6)
            position = next_char(text, position);
        else if (key == KEY_HOME || key == 1)
            position = 0;
        else if (key == KEY_END || key == 5)
            position = length;
        else if (key == 21) {
            memmove(text, text + position, length - position + 1);
            length -= position;
            position = 0;
        } else if (key == 11) {
            text[position] = '\0';
            length = position;
        } else if (key == KEY_BACKSPACE || key == 127 || key == 8 || key == KEY_DC) {
            size_t first = key == KEY_DC ? position : previous_char(text, position);
            size_t last = key == KEY_DC ? next_char(text, position) : position;
            memmove(text + first, text + last, length - last + 1);
            length -= last - first;
            position = first;
        } else if (key >= 32 && key <= 255 && length + 1 < capacity) {
            memmove(text + position + 1, text + position, length - position + 1);
            text[position++] = (char)key;
            length++;
        }
    }
    free(text);
    curs_set(0);
    wtimeout(stdscr, delay);
    werase(notifwin);
    wrefresh(notifwin);
    return submitted;
}
