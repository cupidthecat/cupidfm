#define _GNU_SOURCE
#include "input_prompt.h"

#include <limits.h>
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "browser_ui.h"
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

typedef struct {
    char *text;
    size_t cursor;
} InputSnapshot;

typedef struct {
    char *key;
    char *values[32];
    size_t count;
} InputHistory;

static InputHistory histories[16];
static size_t next_history;
static bool history_initialized;

static void history_free(void) {
    for (size_t i = 0; i < 16; i++) {
        free(histories[i].key);
        for (size_t j = 0; j < histories[i].count; j++)
            free(histories[i].values[j]);
    }
}

static InputHistory *history_get(const char *key) {
    if (!key || !*key)
        return NULL;
    if (!history_initialized) {
        atexit(history_free);
        history_initialized = true;
    }
    for (size_t i = 0; i < 16; i++)
        if (histories[i].key && !strcmp(key, histories[i].key))
            return &histories[i];
    InputHistory *history = &histories[next_history++ % 16];
    free(history->key);
    for (size_t i = 0; i < history->count; i++)
        free(history->values[i]);
    *history = (InputHistory){.key = strdup(key)};
    return history->key ? history : NULL;
}

static void history_add(InputHistory *history, const char *text) {
    if (!history || !*text ||
        (history->count && !strcmp(history->values[history->count - 1], text)))
        return;
    char *copy = strdup(text);
    if (!copy)
        return;
    if (history->count == 32) {
        free(history->values[0]);
        memmove(history->values, history->values + 1, 31 * sizeof(char *));
        history->count--;
    }
    history->values[history->count++] = copy;
}

static void snapshots_free(InputSnapshot *items, size_t *count) {
    while (*count)
        free(items[--*count].text);
}

static void snapshot_push(InputSnapshot *items, size_t *count, const char *text, size_t cursor) {
    char *copy = strdup(text);
    if (!copy)
        return;
    if (*count == 32) {
        free(items[0].text);
        memmove(items, items + 1, 31 * sizeof(*items));
        (*count)--;
    }
    items[(*count)++] = (InputSnapshot){copy, cursor};
}

static int cells(const char *text, size_t length) {
    mbstate_t state = {0};
    int result = 0;
    while (length) {
        wchar_t c;
        size_t bytes = mbrtowc(&c, text, length, &state);
        if (!bytes || bytes == (size_t)-1 || bytes == (size_t)-2) {
            state = (mbstate_t){0};
            bytes = 1;
            result++;
        } else {
            int width = wcwidth(c);
            result += c == '\t' || c == '\n' ? 2 : MAX(0, width < 0 ? 1 : width);
        }
        text += bytes;
        length -= bytes;
    }
    return result;
}

bool input_prompt_ex(const char *title, char *value, size_t capacity,
                     const InputPromptOptions *options) {
    if (!title || !value || !capacity)
        return false;
    char *text = calloc(capacity, 1), *draft = calloc(capacity, 1);
    if (!text || !draft) {
        free(text);
        free(draft);
        return false;
    }
    snprintf(text, capacity, "%s", value);
    size_t length = strlen(text), position = length;
    if (options && options->cursor == INPUT_CURSOR_START)
        position = 0;
    else if (options && options->cursor == INPUT_CURSOR_BEFORE_EXT) {
        char *dot = strrchr(text + (text[0] == '.'), '.');
        if (dot)
            position = (size_t)(dot - text);
    }
    InputHistory *history = history_get(options ? options->history : NULL);
    size_t history_position = history ? history->count : 0;
    InputSnapshot undo[32] = {0}, redo[32] = {0};
    size_t undo_count = 0, redo_count = 0;
    int delay = wgetdelay(stdscr);
    wtimeout(stdscr, options && options->changed ? 100 : -1);
    curs_set(1);
    bool submitted = false;
    if (options && options->changed)
        options->changed(text, options->context);
    for (;;) {
        int width = getmaxx(notifwin);
        werase(notifwin);
        wmove(notifwin, 0, 0);
        int prefix = browser_draw_name(notifwin, title, MAX(0, width / 2 - 2));
        waddstr(notifwin, ": ");
        prefix += 2;
        int room = MAX(1, width - prefix - 1);
        size_t start = 0;
        while (start < position && cells(text + start, position - start) >= room)
            start = next_char(text, start);
        char saved = text[position];
        text[position] = 0;
        int cursor = browser_draw_name(notifwin, text + start, room);
        text[position] = saved;
        browser_draw_name(notifwin, text + position, room - cursor);
        wmove(notifwin, 0, MIN(width - 1, prefix + cursor));
        wrefresh(notifwin);
        int key = getch();
        if (key == ERR) {
            if (options && options->changed)
                options->changed(text, options->context);
            continue;
        }
        if (key == 27 || key == 3)
            break;
        if (key == '\n' || key == '\r' || key == KEY_ENTER) {
            snprintf(value, capacity, "%s", text);
            history_add(history, text);
            submitted = true;
            break;
        }
        if (key == KEY_RESIZE) {
            resized = 1;
            break;
        }
        char *before = strdup(text);
        size_t before_position = position;
        bool history_navigation = false, undoing = false;
        if (key == KEY_LEFT || key == 2)
            position = previous_char(text, position);
        else if (key == KEY_RIGHT || key == 6)
            position = next_char(text, position);
        else if (key == KEY_HOME || key == 1)
            position = 0;
        else if (key == KEY_END || key == 5)
            position = length;
        else if (history && (key == KEY_UP || key == 16 || key == KEY_DOWN || key == 14)) {
            bool previous = key == KEY_UP || key == 16;
            if (history_position == history->count)
                snprintf(draft, capacity, "%s", text);
            if (previous && history_position)
                history_position--;
            else if (!previous && history_position < history->count)
                history_position++;
            snprintf(text, capacity, "%s",
                     history_position < history->count ? history->values[history_position] : draft);
            position = length = strlen(text);
            history_navigation = true;
        } else if (key == 26 || key == 31 || key == 25) {
            InputSnapshot *from = key == 25 ? redo : undo;
            InputSnapshot *to = key == 25 ? undo : redo;
            size_t *from_count = key == 25 ? &redo_count : &undo_count;
            size_t *to_count = key == 25 ? &undo_count : &redo_count;
            if (*from_count) {
                snapshot_push(to, to_count, text, position);
                InputSnapshot item = from[--*from_count];
                snprintf(text, capacity, "%s", item.text);
                position = item.cursor;
                length = strlen(text);
                free(item.text);
            }
            undoing = true;
        } else if (key == 21) {
            memmove(text, text + position, length - position + 1);
            length -= position;
            position = 0;
        } else if (key == 11) {
            text[position] = 0;
            length = position;
        } else if (key == 23) {
            size_t first = position;
            while (first && text[first - 1] == ' ')
                first = previous_char(text, first);
            while (first && text[first - 1] != ' ')
                first = previous_char(text, first);
            memmove(text + first, text + position, length - position + 1);
            length -= position - first;
            position = first;
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
        if (before && strcmp(before, text)) {
            if (!undoing) {
                if (key > 255 || key < 128 || (key & 0xc0) != 0x80)
                    snapshot_push(undo, &undo_count, before, before_position);
                snapshots_free(redo, &redo_count);
            }
            if (history && !history_navigation)
                history_position = history->count;
            if (options && options->changed)
                options->changed(text, options->context);
        }
        free(before);
    }
    snapshots_free(undo, &undo_count);
    snapshots_free(redo, &redo_count);
    free(draft);
    free(text);
    curs_set(0);
    wtimeout(stdscr, delay);
    werase(notifwin);
    wrefresh(notifwin);
    return submitted;
}

bool input_prompt(const char *title, char *value, size_t capacity) {
    return input_prompt_ex(title, value, capacity, NULL);
}
