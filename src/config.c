// config.c
#include "config.h"
#include "../lib/cupidconf.h"
#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <ncurses.h>
#include <errno.h>

/**
 * Parses the key value from the configuration file.
 * Returns the corresponding key code or -1 on failure.
 */
static int parse_key(const char *val);
static void keycode_to_config_string(int keycode, char *buf, size_t buf_size);
static void write_kv_line(FILE *fp, const char *key, int keycode, const char *comment);

void load_default_keybindings(KeyBindings *kb) {
    // Navigation keys
    kb->key_up = KEY_UP;
    kb->key_down = KEY_DOWN;
    kb->key_left = KEY_LEFT;
    kb->key_right = KEY_RIGHT;
    kb->key_tab = '\t';
    kb->key_exit = KEY_F(1);

    // File operations
    kb->key_edit = 5;    // Ctrl+E
    kb->key_copy = 3;    // Ctrl+C
    kb->key_paste = 22;  // Ctrl+V
    kb->key_cut = 24;    // Ctrl+X
    kb->key_delete = 4;  // Ctrl+D
    kb->key_rename = 18; // Ctrl+R
    kb->key_new = 14;    // Ctrl+N
    kb->key_save = 19;   // Ctrl+S
    kb->key_new_dir = 'N'; // Shift+N
    kb->key_search = 6;  // Ctrl+F

    // Editing keys
    kb->edit_up = KEY_UP;
    kb->edit_down = KEY_DOWN;
    kb->edit_left = KEY_LEFT;
    kb->edit_right = KEY_RIGHT;
    kb->edit_save = 7;   // Ctrl+G
    kb->edit_quit = 17;  // Ctrl+Q
    kb->edit_backspace = KEY_BACKSPACE;

    // Default label width
    kb->info_label_width = 15;
}

static void keycode_to_config_string(int keycode, char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;

    // Function keys: KEY_F(1) is typically 265.
    if (keycode >= KEY_F(1) && keycode <= KEY_F(63)) {
        snprintf(buf, buf_size, "F%d", keycode - KEY_F(1) + 1);
        return;
    }

    // Control characters: Ctrl+A..Ctrl+Z
    if (keycode >= 1 && keycode <= 26) {
        snprintf(buf, buf_size, "^%c", 'A' + (keycode - 1));
        return;
    }

    // Common ncurses named keys we support in parse_key().
    switch (keycode) {
        case KEY_UP:        snprintf(buf, buf_size, "KEY_UP"); return;
        case KEY_DOWN:      snprintf(buf, buf_size, "KEY_DOWN"); return;
        case KEY_LEFT:      snprintf(buf, buf_size, "KEY_LEFT"); return;
        case KEY_RIGHT:     snprintf(buf, buf_size, "KEY_RIGHT"); return;
        case KEY_BACKSPACE: snprintf(buf, buf_size, "KEY_BACKSPACE"); return;
        default: break;
    }

    if (keycode == '\t') {
        snprintf(buf, buf_size, "Tab");
        return;
    }
    if (keycode == ' ') {
        snprintf(buf, buf_size, "Space");
        return;
    }

    // Printable ASCII characters.
    if (keycode >= 32 && keycode <= 126) {
        snprintf(buf, buf_size, "%c", (char)keycode);
        return;
    }

    // Fallback: numeric code (lets users still override even if unknown).
    snprintf(buf, buf_size, "%d", keycode);
}

static void write_kv_line(FILE *fp, const char *key, int keycode, const char *comment) {
    char val[64];
    keycode_to_config_string(keycode, val, sizeof(val));
    if (comment && *comment) {
        fprintf(fp, "%s=%s  # %s\n", key, val, comment);
    } else {
        fprintf(fp, "%s=%s\n", key, val);
    }
}

bool write_default_config_file(const char *filepath, const KeyBindings *kb, char *error_buffer, size_t buffer_size) {
    if (!filepath || !*filepath || !kb) return false;

    FILE *fp = fopen(filepath, "w");
    if (!fp) {
        if (error_buffer && buffer_size > 0) {
            snprintf(error_buffer, buffer_size, "Failed to open config for writing: %s\n", filepath);
        }
        return false;
    }

    fputs("# CupidFM Configuration File\n", fp);
    fputs("# Automatically generated on first run.\n\n", fp);

    fputs("# Navigation Keys\n", fp);
    write_kv_line(fp, "key_up", kb->key_up, NULL);
    write_kv_line(fp, "key_down", kb->key_down, NULL);
    write_kv_line(fp, "key_left", kb->key_left, NULL);
    write_kv_line(fp, "key_right", kb->key_right, NULL);
    write_kv_line(fp, "key_tab", kb->key_tab, NULL);
    write_kv_line(fp, "key_exit", kb->key_exit, NULL);
    fputc('\n', fp);

    fputs("# File Management\n", fp);
    write_kv_line(fp, "key_edit", kb->key_edit, "Enter edit mode");
    write_kv_line(fp, "key_copy", kb->key_copy, "Copy selected file");
    write_kv_line(fp, "key_paste", kb->key_paste, "Paste copied file");
    write_kv_line(fp, "key_cut", kb->key_cut, "Cut (move) file");
    write_kv_line(fp, "key_delete", kb->key_delete, "Delete selected file");
    write_kv_line(fp, "key_rename", kb->key_rename, "Rename file");
    write_kv_line(fp, "key_new", kb->key_new, "Create new file");
    write_kv_line(fp, "key_new_dir", kb->key_new_dir, "Create new directory");
    write_kv_line(fp, "key_search", kb->key_search, "Fuzzy search in directory list");
    write_kv_line(fp, "key_save", kb->key_save, "Save changes");
    fputc('\n', fp);

    fputs("# Editing Mode Keys\n", fp);
    write_kv_line(fp, "edit_up", kb->edit_up, NULL);
    write_kv_line(fp, "edit_down", kb->edit_down, NULL);
    write_kv_line(fp, "edit_left", kb->edit_left, NULL);
    write_kv_line(fp, "edit_right", kb->edit_right, NULL);
    write_kv_line(fp, "edit_save", kb->edit_save, "Save in editor");
    write_kv_line(fp, "edit_quit", kb->edit_quit, "Quit editor");
    write_kv_line(fp, "edit_backspace", kb->edit_backspace, NULL);
    fputc('\n', fp);

    fprintf(fp, "info_label_width=%d\n", kb->info_label_width);

    bool ok = (fclose(fp) == 0);
    if (!ok && error_buffer && buffer_size > 0) {
        snprintf(error_buffer, buffer_size, "Failed to finalize config file: %s\n", filepath);
    }
    return ok;
}

int load_config_file(KeyBindings *kb, const char *filepath, char *error_buffer, size_t buffer_size) {
    // Clear error_buffer
    if (error_buffer && buffer_size > 0) {
        error_buffer[0] = '\0';
    }
    
    // 1) Detect missing vs unreadable config file. We only auto-generate a config on missing file.
    FILE *probe = fopen(filepath, "r");
    if (!probe) {
        if (errno == ENOENT) {
            snprintf(error_buffer, buffer_size,
                     "Configuration file not found: %s\nUsing defaults.\n",
                     filepath);
            return 1; // Missing file
        }
        snprintf(error_buffer, buffer_size,
                 "Unable to read configuration file: %s\nUsing defaults.\n",
                 filepath);
        return 2; // Unreadable for some other reason
    }
    fclose(probe);

    // 2) Load config via CupidConf
    cupidconf_t *conf = cupidconf_load(filepath);
    if (!conf) {
        snprintf(error_buffer, buffer_size,
                 "Failed to load configuration file: %s\nUsing defaults.\n",
                 filepath);
        return 3;
    }

    // 2) For each known key in KeyBindings, retrieve from config
    //    We read them as strings, then parse them with parse_key.
    struct {
        const char *cfg_key_name; // name in the config file
        int        *kb_field;     // pointer to the KeyBindings field
    } table[] = {
        {"key_up",      &kb->key_up},
        {"key_down",    &kb->key_down},
        {"key_left",    &kb->key_left},
        {"key_right",   &kb->key_right},
        {"key_tab",     &kb->key_tab},
        {"key_exit",    &kb->key_exit},
        {"key_edit",    &kb->key_edit},
        {"key_copy",    &kb->key_copy},
        {"key_paste",   &kb->key_paste},
        {"key_cut",     &kb->key_cut},
        {"key_delete",  &kb->key_delete},
        {"key_rename",  &kb->key_rename},
        {"key_new",     &kb->key_new},
        {"key_save",    &kb->key_save},
        {"key_new_dir", &kb->key_new_dir},
        {"key_search",  &kb->key_search},

        {"edit_up",        &kb->edit_up},
        {"edit_down",      &kb->edit_down},
        {"edit_left",      &kb->edit_left},
        {"edit_right",     &kb->edit_right},
        {"edit_save",      &kb->edit_save},
        {"edit_quit",      &kb->edit_quit},
        {"edit_backspace", &kb->edit_backspace},
        {NULL, NULL} // sentinel
    };

    int errors = 0;

    // 3) Loop table[] & parse the user's config
    for (int i = 0; table[i].cfg_key_name != NULL; i++) {
        const char *val = cupidconf_get(conf, table[i].cfg_key_name);
        if (!val) {
            // Not present in config, skip. (We keep default.)
            continue;
        }

        int parsed = parse_key(val);
        if (parsed == -1) {
            // If you track line numbers or want an error
            errors++;
            if (error_buffer && (strlen(error_buffer) + 128 < buffer_size)) {
                snprintf(error_buffer + strlen(error_buffer),
                         buffer_size - strlen(error_buffer),
                         "Invalid value for %s: %s\n",
                         table[i].cfg_key_name, val);
            }
            continue;
        }

        // Otherwise store it
        *(table[i].kb_field) = parsed;
    }

    // 4) If user wants to override label_width
    const char *label_val = cupidconf_get(conf, "info_label_width");
    if (!label_val) {
        // Maybe fallback to 'label_width' wildcard or synonyms
        label_val = cupidconf_get(conf, "label_width");
    }
    if (label_val) {
        char *endptr;
        int user_val = strtol(label_val, &endptr, 10);
        if (*endptr == '\0') {
            kb->info_label_width = user_val;
        } else {
            errors++;
            if (error_buffer && (strlen(error_buffer) + 128 < buffer_size)) {
                snprintf(error_buffer + strlen(error_buffer),
                         buffer_size - strlen(error_buffer),
                         "Invalid label_width: %s\n", label_val);
            }
        }
    }

    // 5) Free conf
    cupidconf_free(conf);

    return errors; // 0 means no errors
}

/**
 * Utility function to parse textual representations of keys:
 *   parse_key("KEY_UP") -> KEY_UP
 *   parse_key("^C")     -> 3
 *   parse_key("F1")     -> KEY_F(1)
 *   parse_key("Tab")    -> '\t'
 *   parse_key("x")      -> 'x'
 *   parse_key("Shift+A") -> 'A'
 *
 * Returns -1 on failure.
 */
static int parse_key(const char *val) {
    // Check for special ncurses keys
    if (strcasecmp(val, "KEY_UP") == 0)        return KEY_UP;
    if (strcasecmp(val, "KEY_DOWN") == 0)      return KEY_DOWN;
    if (strcasecmp(val, "KEY_LEFT") == 0)      return KEY_LEFT;
    if (strcasecmp(val, "KEY_RIGHT") == 0)     return KEY_RIGHT;

    if (strncasecmp(val, "KEY_F(", 6) == 0) {
        // e.g., KEY_F(1), KEY_F(2), extract number inside KEY_F(n)
        int fnum = atoi(val + 6);
        if (fnum >= 1 && fnum <= 63) { // ncurses typically supports F1-F63
            return KEY_F(fnum);
        }
        return -1;
    }

    if (strcasecmp(val, "KEY_BACKSPACE") == 0 ||
        strcasecmp(val, "Backspace") == 0)
    {
        return KEY_BACKSPACE;
    }

    // Support for F1 - F12
    for (int i = 1; i <= 12; i++) {
        char fn_key[5];
        snprintf(fn_key, sizeof(fn_key), "F%d", i);
        if (strcasecmp(val, fn_key) == 0) {
            return KEY_F(i);
        }
    }

    // Handle Ctrl+X keys: "^C" -> ASCII 3
    if (val[0] == '^' && val[1] != '\0' && val[2] == '\0') {
        char c = toupper(val[1]);
        if (c >= 'A' && c <= 'Z') {
            return c - 'A' + 1;  // ASCII control code
        }
        return -1; // Invalid
    }

    // Support "Tab" and "Space"
    if (strcasecmp(val, "Tab") == 0) return '\t';
    if (strcasecmp(val, "Space") == 0) return ' ';

    // Handle Shift-modified keys (e.g., "Shift+A" -> 'A')
    if (strncasecmp(val, "Shift+", 6) == 0) {
        char shift_key = val[6];

        // Convert letter keys to uppercase
        if (isalpha(shift_key)) {
            return toupper(shift_key);
        }

        // Handle Shift-modified number keys
        static const char shift_symbols[] = ")!@#$%^&*(";
        if (isdigit(shift_key)) {
            int num = shift_key - '0';
            return shift_symbols[num]; // Returns corresponding symbol
        }

        // Handle common Shift-symbol keys
        if (strcmp(val + 6, "Minus") == 0) return '_';
        if (strcmp(val + 6, "Equals") == 0) return '+';
        if (strcmp(val + 6, "LeftBracket") == 0) return '{';
        if (strcmp(val + 6, "RightBracket") == 0) return '}';
        if (strcmp(val + 6, "Semicolon") == 0) return ':';
        if (strcmp(val + 6, "Apostrophe") == 0) return '"';
        if (strcmp(val + 6, "Comma") == 0) return '<';
        if (strcmp(val + 6, "Period") == 0) return '>';
        if (strcmp(val + 6, "Slash") == 0) return '?';
        if (strcmp(val + 6, "Backslash") == 0) return '|';
        if (strcmp(val + 6, "Grave") == 0) return '~';

        return -1; // Invalid Shift-modified key
    }

    // Single character key (non-shifted)
    if (strlen(val) == 1) {
        return (unsigned char)val[0];
    }

    return -1; // Unknown key
}
