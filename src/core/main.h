#ifndef CUPIDFM_MAIN_H
#define CUPIDFM_MAIN_H

#include <curses.h>    // For WINDOW type
#include <time.h>      // For struct timespec
#include <pthread.h>   // For mutex
#include <signal.h>    // For sig_atomic_t
#include "globals.h"

// Global variables
extern const char *BANNER_TEXT;
extern const char *BUILD_INFO;
extern WINDOW *bannerwin;
extern WINDOW *notifwin;
extern struct timespec last_scroll_time;

// Mutex for banner window
extern pthread_mutex_t banner_mutex;
extern bool banner_running; 
extern volatile sig_atomic_t running;

	// Function declarations
	void draw_scrolling_banner(WINDOW *window, const char *text, const char *build_info, int offset);
	void draw_scrolling_banner_nout(WINDOW *window, const char *text, const char *build_info, int offset);
	void *banner_scrolling_thread(void *arg);
	void show_notification(WINDOW *win, const char *format, ...);

#define BANNER_UPDATE_INTERVAL 250000  // 250ms in microseconds

#endif //CUPIDFM_MAIN_H 
