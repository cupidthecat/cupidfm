#include <curses.h>
#include <pthread.h> 
#include "globals.h"

// Global variable definitions
const char *BANNER_TEXT = NULL;  // To be initialized in main()
const char *BUILD_INFO = "Version 1.0";
WINDOW *bannerwin = NULL;
WINDOW *notifwin = NULL;
struct timespec last_scroll_time = {0, 0};
pthread_mutex_t banner_mutex = PTHREAD_MUTEX_INITIALIZER;
bool should_clear_notif = true;
struct timespec last_notification_time = {0, 0};
KeyBindings g_kb;
int banner_offset = 0;  // Shared banner scroll offset
