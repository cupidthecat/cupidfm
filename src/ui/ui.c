#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <ctype.h>   // For isprint
#include <ncurses.h> // For WINDOW, werase, wmove, vw_printw, wrefresh
#include <signal.h>  // for signal, SIGWINCH
#include <stdarg.h>  // For va_list, va_start, va_end, vw_printw
#include <stdio.h>   // For vsnprintf, snprintf
#include <stdlib.h>  // For malloc, free
#include <string.h>  // For strlen, memcpy
#include <time.h>    // For clock_gettime, struct timespec, CLOCK_MONOTONIC

#include "app_input.h" // for keycode_to_string
#include "globals.h"
#include "main.h" // for banner globals and draw_scrolling_banner

void hold_notification_for_ms(long ms) {
  if (ms <= 0) {
    notification_hold_active = false;
    return;
  }
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  long add_ns = ms * 1000000L;
  notification_hold_until = now;
  notification_hold_until.tv_sec += add_ns / 1000000000L;
  notification_hold_until.tv_nsec += add_ns % 1000000000L;
  if (notification_hold_until.tv_nsec >= 1000000000L) {
    notification_hold_until.tv_sec += 1;
    notification_hold_until.tv_nsec -= 1000000000L;
  }
  notification_hold_active = true;
}

void show_notification(WINDOW *win, const char *format, ...) {
  va_list args;
  va_start(args, format);
  werase(win);
  wmove(win, 0, 0);
  vw_printw(win, format, args);
  va_end(args);
  wrefresh(win);
  clock_gettime(CLOCK_MONOTONIC, &last_notification_time);
}

void show_popup(const char *title, const char *fmt, ...) {
  // If not initialized, do it. Usually you have initscr() done already.
  if (!stdscr)
    initscr();

  // Format message first so we can size the popup and avoid ncurses wrapping
  // into the border (which makes the popup look "corrupted").
  va_list args;
  va_start(args, fmt);
  va_list args_copy;
  va_copy(args_copy, args);
  int needed = vsnprintf(NULL, 0, fmt, args);
  va_end(args);
  if (needed < 0) {
    va_end(args_copy);
    return;
  }

  size_t cap = (size_t)needed + 1;
  char *msg = (char *)malloc(cap);
  if (!msg) {
    va_end(args_copy);
    return;
  }
  vsnprintf(msg, cap, fmt, args_copy);
  va_end(args_copy);

  // Sanitize control characters (keep '\n' for line breaks).
  for (size_t i = 0; i < cap; i++) {
    unsigned char c = (unsigned char)msg[i];
    if (c == '\0')
      break;
    if (c == '\n')
      continue;
    if (c == '\r') {
      msg[i] = ' ';
      continue;
    }
    if (!isprint(c))
      msg[i] = '?';
  }

  // Split into lines in-place.
  int line_count = 1;
  size_t max_line_len = 0;
  size_t cur_len = 0;
  for (size_t i = 0; msg[i] != '\0'; i++) {
    if (msg[i] == '\n') {
      if (cur_len > max_line_len)
        max_line_len = cur_len;
      cur_len = 0;
      line_count++;
    } else {
      cur_len++;
    }
  }
  if (cur_len > max_line_len)
    max_line_len = cur_len;

  // Size the popup within the current terminal.
  int max_rows = LINES - 2;
  int max_cols = COLS - 2;
  if (max_rows < 6)
    max_rows = 6;
  if (max_cols < 20)
    max_cols = 20;

  // Ensure popup is wide enough for content, title, and footer
  const char *footer_text = "Press any key to close";
  size_t footer_len = strlen(footer_text);
  size_t min_width = footer_len + 4; // footer + padding + borders
  if (min_width < 30)
    min_width = 30; // absolute minimum for readability

  int cols = (int)max_line_len + 4; // borders + padding
  if ((size_t)cols < min_width)
    cols = (int)min_width;
  if (cols > max_cols)
    cols = max_cols;

  // title line + blank + content + blank + footer + borders
  int max_content_rows = max_rows - 4; // top/title area and footer
  if (max_content_rows < 1)
    max_content_rows = 1;
  int content_rows = line_count;
  if (content_rows > max_content_rows)
    content_rows = max_content_rows;
  int rows = content_rows + 4;
  if (rows < 6)
    rows = 6;
  if (rows > max_rows)
    rows = max_rows;

  // Center the popup.
  int starty = (LINES - rows) / 2;
  int startx = (COLS - cols) / 2;
  if (starty < 0)
    starty = 0;
  if (startx < 0)
    startx = 0;

  WINDOW *popup = newwin(rows, cols, starty, startx);
  if (!popup) {
    free(msg);
    return;
  }
  keypad(popup, TRUE);
  werase(popup);
  box(popup, 0, 0);

  // Title (truncate to fit).
  wattron(popup, A_BOLD);
  int title_space = cols - 6; // "[ " + " ]" plus margin
  if (title_space < 0)
    title_space = 0;
  mvwprintw(popup, 0, 2, "[ %.*s ]", title_space, title ? title : "");
  wattroff(popup, A_BOLD);

  // Print content line-by-line with truncation to prevent wrapping.
  int y = 2;
  int printable_w = cols - 4; // left/right padding inside border
  if (printable_w < 1)
    printable_w = 1;

  int printed = 0;
  char *p = msg;
  while (printed < content_rows && p && *p) {
    char *nl = strchr(p, '\n');
    if (nl)
      *nl = '\0';
    mvwaddnstr(popup, y + printed, 2, p, printable_w);
    printed++;
    if (!nl)
      break;
    p = nl + 1;
  }
  if (line_count > content_rows && content_rows > 0) {
    // Show truncation hint on the last visible line.
    int remaining = line_count - content_rows;
    char tail[64];
    snprintf(tail, sizeof(tail), "... (%d more line%s)", remaining,
             remaining == 1 ? "" : "s");
    mvwaddnstr(popup, y + content_rows - 1, 2, tail, printable_w);
  }

  // Footer.
  const char *footer_msg = "Press any key to close";
  mvwaddnstr(popup, rows - 2, 2, footer_msg, printable_w);

  wrefresh(popup);
  wgetch(popup);

  delwin(popup);
  free(msg);

  // Ensure the underlying UI is repainted by the caller; do a minimal refresh
  // so the popup isn't left behind on terminals without full redraw.
  touchwin(stdscr);
  refresh();
}

// --------------------------------------------------------------------
// Helper: Dynamic string vector for building help content
// --------------------------------------------------------------------
typedef struct {
  char **lines;
  size_t count;
  size_t capacity;
} StrVec;

static void strvec_init(StrVec *v) {
  v->lines = NULL;
  v->count = 0;
  v->capacity = 0;
}

static void strvec_free(StrVec *v) {
  for (size_t i = 0; i < v->count; i++) {
    free(v->lines[i]);
  }
  free(v->lines);
  v->lines = NULL;
  v->count = 0;
  v->capacity = 0;
}

static void strvec_push(StrVec *v, const char *str) {
  if (v->count >= v->capacity) {
    size_t new_cap = v->capacity == 0 ? 16 : v->capacity * 2;
    char **new_lines = realloc(v->lines, new_cap * sizeof(char *));
    if (!new_lines)
      return;
    v->lines = new_lines;
    v->capacity = new_cap;
  }
  v->lines[v->count++] = strdup(str ? str : "");
}

// --------------------------------------------------------------------
// Build help content with word-wrapping
// --------------------------------------------------------------------
static void build_help_lines(StrVec *out, const KeyBindings *kb,
                             int max_width) {
  if (!kb || max_width < 10)
    return;

  strvec_init(out);

  // Helper to format a keybinding line
  char line_buf[512];

  strvec_push(out, "Navigation:");
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Move up",
           keycode_to_string(kb->key_up));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Move down",
           keycode_to_string(kb->key_down));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Go to parent directory",
           keycode_to_string(kb->key_left));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf),
           "  %-20s - Enter directory / Switch to preview",
           keycode_to_string(kb->key_right));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf),
           "  %-20s - Switch between directory and preview",
           keycode_to_string(kb->key_tab));
  strvec_push(out, line_buf);
  strvec_push(out, "");

  strvec_push(out, "File Operations:");
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Edit file",
           keycode_to_string(kb->key_edit));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Copy file/directory",
           keycode_to_string(kb->key_copy));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Cut file/directory",
           keycode_to_string(kb->key_cut));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Paste",
           keycode_to_string(kb->key_paste));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Delete",
           keycode_to_string(kb->key_delete));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Rename",
           keycode_to_string(kb->key_rename));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - New file",
           keycode_to_string(kb->key_new));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - New directory",
           keycode_to_string(kb->key_new_dir));
  strvec_push(out, line_buf);
  strvec_push(out, "");

  strvec_push(out, "Other Functions:");
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Search",
           keycode_to_string(kb->key_search));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Select all",
           keycode_to_string(kb->key_select_all));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - File info",
           keycode_to_string(kb->key_info));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Undo",
           keycode_to_string(kb->key_undo));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Redo",
           keycode_to_string(kb->key_redo));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Change permissions",
           keycode_to_string(kb->key_permissions));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Console",
           keycode_to_string(kb->key_console));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Help (this menu)",
           keycode_to_string(kb->key_help));
  strvec_push(out, line_buf);
  snprintf(line_buf, sizeof(line_buf), "  %-20s - Exit",
           keycode_to_string(kb->key_exit));
  strvec_push(out, line_buf);
}

// --------------------------------------------------------------------
// Scrollable help menu using ncurses pad
// --------------------------------------------------------------------
void show_help_menu(const KeyBindings *kb) {
  if (!kb)
    return;
  if (!stdscr)
    initscr();

  // Build help content
  StrVec help_lines;
  strvec_init(&help_lines);
  build_help_lines(&help_lines, kb, 120);

  if (help_lines.count == 0) {
    strvec_free(&help_lines);
    return;
  }

  const char *title = "CupidFM - Help Menu";
  const char *footer = "↑/↓: Scroll | PgUp/PgDn | Home/End | q/Esc: Close";

  WINDOW *popup_win = NULL;
  WINDOW *content_pad = NULL;

  int scroll_pos = 0;
  bool done = false;

  // Track terminal + geometry so we only recreate on resize
  int last_lines = -1, last_cols = -1;
  int popup_rows = 0, popup_cols = 0;
  int starty = 0, startx = 0;
  int content_start_y = 2;
  int content_height = 0, content_width = 0;
  int pad_height = 0, pad_width = 0;

  // Banner animation support
  struct timespec last_banner_update;
  clock_gettime(CLOCK_MONOTONIC, &last_banner_update);

  // Compute a safe pad width based on longest help line
  size_t max_line_len = 0;
  for (size_t i = 0; i < help_lines.count; i++) {
    size_t len = strlen(help_lines.lines[i]);
    if (len > max_line_len)
      max_line_len = len;
  }
  if (max_line_len < 1)
    max_line_len = 1;

  bool need_recreate = true;
  bool need_frame_redraw = true;
  bool force_repaint = true;

  while (!done) {
    // Recreate windows only if resized or first time
    if (need_recreate || LINES != last_lines || COLS != last_cols) {
      last_lines = LINES;
      last_cols = COLS;

      // Compute popup size based on current terminal size
      int term_rows = LINES;
      int term_cols = COLS;

      popup_rows = term_rows - 4;
      popup_cols = term_cols - 4;
      if (popup_rows < 10)
        popup_rows = 10;
      if (popup_cols < 50)
        popup_cols = 50;
      if (popup_rows > 40)
        popup_rows = 40;
      if (popup_cols > 100)
        popup_cols = 100;

      starty = (term_rows - popup_rows) / 2;
      startx = (term_cols - popup_cols) / 2;
      if (starty < 0)
        starty = 0;
      if (startx < 0)
        startx = 0;

      // Content area dimensions (inside border, minus title/footer rows)
      content_start_y = 2;
      content_height = popup_rows - 4;
      content_width = popup_cols - 4;
      if (content_height < 1)
        content_height = 1;
      if (content_width < 1)
        content_width = 1;

      // Pad dims: tall enough for all lines, wide enough for longest line
      pad_height = (int)help_lines.count;
      if (pad_height < content_height)
        pad_height = content_height;

      pad_width = (int)max_line_len;
      if (pad_width < content_width)
        pad_width = content_width;
      if (pad_width < 1)
        pad_width = 1;

      // Destroy old windows
      if (content_pad) {
        delwin(content_pad);
        content_pad = NULL;
      }
      if (popup_win) {
        delwin(popup_win);
        popup_win = NULL;
      }

      // Create popup window
      popup_win = newwin(popup_rows, popup_cols, starty, startx);
      if (!popup_win) {
        strvec_free(&help_lines);
        return;
      }
      keypad(popup_win, TRUE);
      wtimeout(popup_win, 10); // non-blocking
      mouseinterval(0);

      // Create pad
      content_pad = newpad(pad_height, pad_width);
      if (!content_pad) {
        delwin(popup_win);
        strvec_free(&help_lines);
        return;
      }

      // Fill pad once
      werase(content_pad);
      for (size_t i = 0; i < help_lines.count; i++) {
        // write line, then clear rest so old chars never remain
        mvwaddnstr(content_pad, (int)i, 0, help_lines.lines[i], pad_width);
        wclrtoeol(content_pad);
      }

      need_frame_redraw = true;
      force_repaint = true;
      need_recreate = false;
    }

    // Clamp scroll position
    int max_scroll = (int)help_lines.count - content_height;
    if (max_scroll < 0)
      max_scroll = 0;
    if (scroll_pos < 0)
      scroll_pos = 0;
    if (scroll_pos > max_scroll)
      scroll_pos = max_scroll;

    // Redraw the frame only when needed (resize, etc.)
    if (need_frame_redraw) {
      werase(popup_win);
      box(popup_win, 0, 0);

      // Title (truncate to fit)
      wattron(popup_win, A_BOLD);
      int title_space = popup_cols - 6; // "[ " + " ]"
      if (title_space < 0)
        title_space = 0;
      mvwprintw(popup_win, 0, 2, "[ %.*s ]", title_space, title ? title : "");
      wattroff(popup_win, A_BOLD);

      // Footer goes on the last *inside* row, not the border row
      int footer_y = popup_rows - 2;
      if (footer_y < 1)
        footer_y = 1;
      if (popup_cols > 2)
        mvwhline(popup_win, footer_y, 1, ' ', popup_cols - 2);
      int footer_space = popup_cols - 4;
      if (footer_space < 0)
        footer_space = 0;
      mvwaddnstr(popup_win, footer_y, 2, footer ? footer : "", footer_space);

      need_frame_redraw = false;
      force_repaint = true;
    }

    if (force_repaint) {
      // Mark as dirty so ncurses repaints over whatever refreshed the screen.
      touchwin(popup_win);
      touchwin(content_pad);
      force_repaint = false;
    }

    // Batch refresh to avoid flicker
    wnoutrefresh(popup_win);
    pnoutrefresh(content_pad, scroll_pos, 0, // pad start
                 starty + content_start_y,   // screen start y
                 startx + 2,                 // screen start x
                 starty + content_start_y + content_height - 1, // screen end y
                 startx + popup_cols - 3);                      // screen end x
    doupdate();

    int ch = wgetch(popup_win);

    // Banner animation while idle
    if (ch == ERR) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);

      long banner_time_diff =
          (now.tv_sec - last_banner_update.tv_sec) * 1000000L +
          (now.tv_nsec - last_banner_update.tv_nsec) / 1000L;

      if (banner_time_diff >= BANNER_SCROLL_INTERVAL && BANNER_TEXT &&
          bannerwin) {
	        int total_scroll_length = (COLS - 2) +
	                                  (BANNER_TEXT ? (int)strlen(BANNER_TEXT) : 0) +
	                                  (BUILD_INFO ? (int)strlen(BUILD_INFO) : 0) +
	                                  BANNER_TIME_PREFIX_LEN + BANNER_TIME_LEN + 4;

        pthread_mutex_lock(&banner_mutex);
        draw_scrolling_banner_nout(bannerwin, BANNER_TEXT, BUILD_INFO,
                                   banner_offset);
        pthread_mutex_unlock(&banner_mutex);
	
	        if (total_scroll_length > 0)
	          banner_offset = (banner_offset + 1) % total_scroll_length;
	
	        last_banner_update = now;

        // Banner redraw can overwrite parts of the popup on the physical
        // terminal; repaint without clearing/reboxing (avoids flicker).
        touchwin(popup_win);
        touchwin(content_pad);
        wnoutrefresh(popup_win);
        pnoutrefresh(content_pad, scroll_pos, 0, // pad start
                     starty + content_start_y,   // screen start y
                     startx + 2,                 // screen start x
                     starty + content_start_y + content_height - 1,
                     startx + popup_cols - 3); // screen end x
        doupdate();
	      }

      continue;
    }

    switch (ch) {
    case KEY_UP:
      if (scroll_pos > 0)
        scroll_pos--;
      break;
    case KEY_DOWN:
      if (scroll_pos < max_scroll)
        scroll_pos++;
      break;
    case KEY_PPAGE:
      scroll_pos -= content_height;
      if (scroll_pos < 0)
        scroll_pos = 0;
      break;
    case KEY_NPAGE:
      scroll_pos += content_height;
      if (scroll_pos > max_scroll)
        scroll_pos = max_scroll;
      break;
    case KEY_HOME:
      scroll_pos = 0;
      break;
    case KEY_END:
      scroll_pos = max_scroll;
      break;
    case KEY_MOUSE: {
      MEVENT event;
      if (getmouse(&event) == OK) {
        if (event.bstate & BUTTON4_PRESSED) {
          if (scroll_pos > 0)
            scroll_pos--;
        } else if (event.bstate & BUTTON5_PRESSED) {
          if (scroll_pos < max_scroll)
            scroll_pos++;
        }
      }
      break;
    }
    case KEY_RESIZE:
      need_recreate = true;
      break;
    case 'q':
    case 'Q':
    case 27:
      done = true;
      break;
    default:
      if (ch == kb->key_help || ch == toupper(kb->key_help) ||
          ch == tolower(kb->key_help)) {
        done = true;
      }
      break;
    }
  }

  if (content_pad)
    delwin(content_pad);
  if (popup_win)
    delwin(popup_win);
  strvec_free(&help_lines);

  touchwin(stdscr);
  refresh();
}
