#ifndef CUPID_GETCH_H
#define CUPID_GETCH_H

#include "../fs/files.h"
#include <ctype.h>
#include <ncurses.h>


// Parse xterm-style "CSI u" key events: ESC [ code ; mod u
// Example: Ctrl+Shift+C often arrives as ESC [ 67 ; 6 u  (67='C',
// mod=6=Shift+Ctrl)
static int cupid_parse_csi_u(WINDOW *w, int *out_key) {
  int consumed[64];
  int n = 0;

  int ch = wgetch(w);
  if (ch == ERR)
    return 0;
  consumed[n++] = ch;

  if (ch != '[')
    goto fail;

  long code = 0;
  int got_code = 0;

  while ((ch = wgetch(w)) != ERR && isdigit((unsigned char)ch)) {
    if (n < (int)(sizeof(consumed) / sizeof(consumed[0])))
      consumed[n++] = ch;
    got_code = 1;
    code = code * 10 + (ch - '0');
  }
  if (!got_code || ch == ERR)
    goto fail;
  if (n < (int)(sizeof(consumed) / sizeof(consumed[0])))
    consumed[n++] = ch;

  long mod = 1; // default: no modifiers
  if (ch == ';') {
    mod = 0;
    int got_mod = 0;
    while ((ch = wgetch(w)) != ERR && isdigit((unsigned char)ch)) {
      if (n < (int)(sizeof(consumed) / sizeof(consumed[0])))
        consumed[n++] = ch;
      got_mod = 1;
      mod = mod * 10 + (ch - '0');
    }
    if (!got_mod || ch == ERR)
      goto fail;
    if (n < (int)(sizeof(consumed) / sizeof(consumed[0])))
      consumed[n++] = ch;
  }

  if (ch != 'u')
    goto fail;

  // Decode modifiers (CSI u):
  // 1=none, 2=Shift, 3=Alt, 4=Shift+Alt, 5=Ctrl, 6=Shift+Ctrl, 7=Alt+Ctrl,
  // 8=Shift+Alt+Ctrl
  if (code >= 'A' && code <= 'Z') {
    char letter = (char)code;

    if (mod == 6 || mod == 8) { // Ctrl+Shift (or Ctrl+Shift+Alt)
      *out_key = CTRL_SHIFT_A_CODE + (letter - 'A');
      return 1;
    }
    if (mod == 5 || mod == 7) {      // Ctrl (or Ctrl+Alt)
      *out_key = (letter - 'A') + 1; // ^A..^Z
      return 1;
    }
    if (mod == 2 || mod == 4) { // Shift (or Shift+Alt)
      *out_key = (int)letter;
      return 1;
    }
  }

  if (code >= 'a' && code <= 'z') {
    char letter = (char)(code - 32); // to upper for ctrl math

    if (mod == 6 || mod == 8) {
      *out_key = CTRL_SHIFT_A_CODE + (letter - 'A');
      return 1;
    }
    if (mod == 5 || mod == 7) {
      *out_key = (letter - 'A') + 1;
      return 1;
    }
    // Shift on lowercase usually implies uppercase; fallthrough to raw code
    // otherwise.
  }

  // Fallback: return raw codepoint if it's reasonable
  if (code > 0 && code < 0x110000) {
    *out_key = (int)code;
    return 1;
  }

fail:
  // Put bytes back so normal ESC handling still works
  for (int i = n - 1; i >= 0; i--)
    ungetch(consumed[i]);
  return 0;
}

// Drop-in replacement for wgetch() in your main input loop.
static int cupid_getch_extended(WINDOW *w) {
  int ch = wgetch(w);
  if (ch != 27)
    return ch; // not ESC

  int key = 0;
  if (cupid_parse_csi_u(w, &key))
    return key;

  return 27; // plain ESC (let existing logic handle alt/meta/etc)
}

#endif // CUPID_GETCH_H
