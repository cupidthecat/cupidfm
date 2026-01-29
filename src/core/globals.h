#ifndef GLOBALS_H
#define GLOBALS_H

#include <signal.h>  // For sig_atomic_t
#include <time.h>
#include <stdbool.h>

#include "config.h"

#define MAX_PATH_LENGTH 1024  // Define it here consistently
#define NOTIFICATION_TIMEOUT_MS 250  // 1 second timeout for notifications
#define MAX_DIR_NAME 256
#define MAX_DISPLAY_LENGTH 32
#define TAB 9
#define CTRL_E 5
#define BANNER_SCROLL_INTERVAL 250000  // Microseconds between scroll updates (250ms)
#define BANNER_TIME_FORMAT "%Y-%m-%d %H:%M:%S"
// strlen("YYYY-MM-DD HH:MM:SS") == 19
#define BANNER_TIME_LEN 19
#define BANNER_TIME_PREFIX " | "
#define BANNER_TIME_PREFIX_LEN 3
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
extern int banner_offset;  // Shared banner scroll offset for synchronization
extern bool g_select_all_highlight; // Global highlight flag for Select All in current view

// If set, the notif bar should not be auto-cleared until this time.
extern bool notification_hold_active;
extern struct timespec notification_hold_until;
#endif // GLOBALS_H
