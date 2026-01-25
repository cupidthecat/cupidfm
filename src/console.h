#ifndef CONSOLE_H
#define CONSOLE_H

// Simple in-app console log for plugins and debugging.

void console_logf(const char *fmt, ...);
void console_clear(void);
void console_show(void);

#endif // CONSOLE_H

