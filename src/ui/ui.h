// ui.h
#ifndef UI_H
#define UI_H

#include <ncurses.h>

#include "config.h"

void show_notification(WINDOW *win, const char *format, ...);
void show_popup(const char *title, const char *fmt, ...);
void hold_notification_for_ms(long ms);
void show_help_menu(const KeyBindings *kb);

#endif // UI_H
