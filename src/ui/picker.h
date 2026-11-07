#ifndef PICKER_H
#define PICKER_H

#include <stddef.h>

/* Returns the original item index, or -1 on cancellation/allocation failure.
 * Slash enters a live fuzzy query, Enter chooses, and Esc clears then cancels. */
int picker_choose(const char *title, const char *const *items, size_t count);

#endif
