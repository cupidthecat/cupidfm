#ifndef GLOBALS_H
#define GLOBALS_H

#include <signal.h>  // For sig_atomic_t
#include <time.h>
#include <stdbool.h>
#include <magic.h>
#include "config.h"

#define MAX_PATH_LENGTH 1024  // Define it here consistently
#define NOTIFICATION_TIMEOUT_MS 250  // 1 second timeout for notifications
#define MAX_DIR_NAME 256
#define MAX_DISPLAY_LENGTH 32
#define TAB 9
#define CTRL_E 5
#define BANNER_SCROLL_INTERVAL 250000  // Microseconds between scroll updates (250ms)
#define INPUT_CHECK_INTERVAL 10        // Milliseconds for input checking (10ms)
#define ERROR_BUFFER_SIZE 2048         // Increased buffer size for error messages
#define NOTIFICATION_TIMEOUT_MS 250    // 250ms timeout for notifications
#define PATH_MAX 4096

extern volatile sig_atomic_t resized;
extern volatile sig_atomic_t is_editing;
extern char copied_filename[MAX_PATH_LENGTH];
extern struct timespec last_notification_time;
extern bool should_clear_notif;
extern KeyBindings g_kb;
extern magic_t g_magic_cookie;


#endif // GLOBALS_H
