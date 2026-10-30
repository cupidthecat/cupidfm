#ifndef APP_INPUT_H
#define APP_INPUT_H

#include <stdbool.h>
#include <time.h>

// Returns a human-readable name for a key code.
const char *keycode_to_string(int keycode);

// Preserve queued keys while combining movement bursts into one repaint.
int app_input_next_key(void);
bool app_input_defer_scroll_frame(int key, int up, int down, const struct timespec *last_frame);

#endif // APP_INPUT_H
