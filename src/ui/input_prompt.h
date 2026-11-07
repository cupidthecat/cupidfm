#ifndef INPUT_PROMPT_H
#define INPUT_PROMPT_H

#include <stdbool.h>
#include <stddef.h>

/* Escape preserves the caller's buffer. Editing uses UTF-8 byte boundaries. */
bool input_prompt(const char *title, char *value, size_t capacity);

#endif
