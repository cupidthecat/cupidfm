#ifndef APP_INPUT_H
#define APP_INPUT_H

#include <time.h>

// Returns a human-readable name for a key code.
const char *keycode_to_string(int keycode);

// Flushes any buffered input if the loop is taking too long.
void maybe_flush_input(struct timespec loop_start_time);

#endif // APP_INPUT_H
