#ifndef INPUT_PROMPT_H
#define INPUT_PROMPT_H

#include <stdbool.h>
#include <stddef.h>

typedef enum { INPUT_CURSOR_END, INPUT_CURSOR_START, INPUT_CURSOR_BEFORE_EXT } InputCursor;

typedef struct {
    InputCursor cursor;
    const char *history;
    void (*changed)(const char *text, void *context);
    void *context;
} InputPromptOptions;

/* Escape preserves the caller's buffer. Live callbacks receive uncommitted text;
 * callers restore their own preview state on cancellation. */
bool input_prompt_ex(const char *title, char *value, size_t capacity,
                     const InputPromptOptions *options);
bool input_prompt(const char *title, char *value, size_t capacity);

#endif
