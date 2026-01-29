// banner.c
#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "banner.h"

#include <ncurses.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "globals.h"
#include "main.h"

int banner_initial_offset_for_center(const char *text, const char *build_info) {
    // draw_scrolling_banner() builds: [text][2 spaces][build_with_time], then pads with spaces.
    // To start "centered", we start the viewport inside the trailing-space region so the
    // first frame shows leading spaces, then the text.
    int width = COLS - 2;
    if (width <= 0) return 0;

    int text_len = (int)strlen(text ? text : "");

    // build_with_time = build_info + " | " + "YYYY-MM-DD HH:MM:SS"
    int build_len = (int)strlen(build_info ? build_info : "") + BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN;

    int content_len = text_len + 2 + build_len;
    int start_pad = (width - content_len) / 2;
    if (start_pad <= 0) return 0;

    int total_len = width + text_len + build_len + 4;
    if (total_len <= 0) return 0;

    int off = total_len - start_pad;
    off %= total_len;
    if (off < 0) off += total_len;
    return off;
}

void draw_scrolling_banner(WINDOW *window, const char *text, const char *build_info, int offset) {
    // Include local date/time in the banner (fixed width so the scroll math stays stable).
    time_t t = time(NULL);
    struct tm tm_local;
    localtime_r(&t, &tm_local);
    char time_buf[32] = {0};
    strftime(time_buf, sizeof(time_buf), BANNER_TIME_FORMAT, &tm_local);

    char build_with_time[256];
    snprintf(build_with_time, sizeof(build_with_time), "%s%s%s",
             build_info ? build_info : "",
             BANNER_TIME_PREFIX,
             time_buf);

    int width = COLS - 2;
    int text_len = (int)strlen(text ? text : "");
    int build_len = (int)strlen(build_with_time);

    // Calculate total length including padding
    int total_len = width + text_len + build_len + 4;

    // Create the scroll text buffer
    char *scroll_text = malloc(2 * (size_t)total_len + 1);
    if (!scroll_text) return;

    memset(scroll_text, ' ', 2 * (size_t)total_len);
    scroll_text[2 * (size_t)total_len] = '\0';

    // Copy the text pattern twice for smooth wrapping
    for (int i = 0; i < 2; i++) {
        int pos = i * total_len;
        memcpy(scroll_text + pos, text ? text : "", (size_t)text_len);
        memcpy(scroll_text + pos + text_len + 2, build_with_time, (size_t)build_len);
    }

    // Draw the banner
    werase(window);
    box(window, 0, 0);
    mvwprintw(window, 1, 1, "%.*s", width, scroll_text + offset);
    wrefresh(window);

    free(scroll_text);
}

void *banner_scrolling_thread(void *arg) {
    WINDOW *window = (WINDOW *)arg;
    struct timespec last_update_time;
    clock_gettime(CLOCK_MONOTONIC, &last_update_time);

    int total_scroll_length = (COLS - 2) + (int)strlen(BANNER_TEXT) + (int)strlen(BUILD_INFO) +
                              BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

    while (1) {
        struct timespec current_time;
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        long time_diff = (current_time.tv_sec - last_update_time.tv_sec) * 1000000 +
                         (current_time.tv_nsec - last_update_time.tv_nsec) / 1000;

        if (time_diff >= BANNER_SCROLL_INTERVAL) {
            draw_scrolling_banner(window, BANNER_TEXT, BUILD_INFO, banner_offset);
            banner_offset = (banner_offset + 1) % total_scroll_length;
            last_update_time = current_time;
        }

        usleep(10000); // 10ms
    }

    return NULL;
}
