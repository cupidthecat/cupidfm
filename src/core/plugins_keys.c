// plugins_keys.c
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "plugins_keys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ncurses.h>

#include "../fs/files.h" // CTRL_SHIFT_*_CODE

const char *plugins_keycode_to_name_local(int keycode, char buf[32]) {
  if (!buf)
    return "UNKNOWN";

  // Function keys
  if (keycode >= KEY_F(1) && keycode <= KEY_F(63)) {
    int func_num = keycode - (KEY_F(1) - 1);
    snprintf(buf, 32, "F%d", func_num);
    return buf;
  }

  // Control characters: Ctrl+A..Ctrl+Z
  if (keycode >= 1 && keycode <= 26) {
    snprintf(buf, 32, "^%c", 'A' + (keycode - 1));
    return buf;
  }

  // Ctrl+Shift+Letter combinations (custom key codes 0x2001-0x201A)
  // Format: ^_A through ^_Z
  if (keycode >= CTRL_SHIFT_A_CODE && keycode <= CTRL_SHIFT_Z_CODE) {
    char letter = 'A' + (keycode - CTRL_SHIFT_A_CODE);
    snprintf(buf, 32, "^_%c", letter);
    return buf;
  }

  switch (keycode) {
  case KEY_UP:
    return "KEY_UP";
  case KEY_DOWN:
    return "KEY_DOWN";
  case KEY_LEFT:
    return "KEY_LEFT";
  case KEY_RIGHT:
    return "KEY_RIGHT";
  case KEY_BACKSPACE:
    return "KEY_BACKSPACE";
  case '\t':
    return "Tab";
  default:
    break;
  }

  // Printable ASCII (lowercase and other characters)
  if (keycode >= 32 && keycode <= 126) {
    buf[0] = (char)keycode;
    buf[1] = '\0';
    return buf;
  }

  return "UNKNOWN";
}

int plugins_parse_key_name_local(const char *s) {
  if (!s || !*s)
    return -1;

  // Ctrl+Shift sequences: ^_A..^_Z
  if (s[0] == '^' && s[1] == '_' && s[2] && !s[3]) {
    char c = s[2];
    if (c >= 'a' && c <= 'z')
      c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z')
      return CTRL_SHIFT_A_CODE + (c - 'A');
    return -1;
  }

  // Ctrl sequences: ^A..^Z
  if (s[0] == '^' && s[1] && !s[2]) {
    char c = s[1];
    if (c >= 'a' && c <= 'z')
      c = (char)(c - 32);
    if (c >= 'A' && c <= 'Z')
      return (c - 'A') + 1;
    return -1;
  }

  if (strncmp(s, "F", 1) == 0 && s[1]) {
    char *end = NULL;
    long n = strtol(s + 1, &end, 10);
    if (end && *end == '\0' && n >= 1 && n <= 63) {
      return KEY_F((int)n);
    }
  }

  if (strcmp(s, "KEY_UP") == 0)
    return KEY_UP;
  if (strcmp(s, "KEY_DOWN") == 0)
    return KEY_DOWN;
  if (strcmp(s, "KEY_LEFT") == 0)
    return KEY_LEFT;
  if (strcmp(s, "KEY_RIGHT") == 0)
    return KEY_RIGHT;
  if (strcmp(s, "KEY_BACKSPACE") == 0)
    return KEY_BACKSPACE;
  if (strcmp(s, "Tab") == 0)
    return '\t';

  // Single printable char
  if (s[0] && !s[1])
    return (unsigned char)s[0];

  return -1;
}
