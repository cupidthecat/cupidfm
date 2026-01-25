// ui.h
#ifndef UI_H
#define UI_H

#include <ncurses.h>

void show_notification(WINDOW *win, const char *format, ...);
void show_popup(const char *title, const char *fmt, ...);
void hold_notification_for_ms(long ms);

#endif // UI_H
