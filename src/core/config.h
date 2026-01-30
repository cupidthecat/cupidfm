// config.h
#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int key_up;
    int key_down;
    int key_left;
    int key_right;
    int key_tab;
    int key_exit;

    int key_edit;    // e.g., Ctrl+E
    int key_copy;    // e.g., Ctrl+C
    int key_paste;   // e.g., Ctrl+V
    int key_cut;     // e.g., Ctrl+X
    int key_delete;  // e.g., Ctrl+D
    int key_rename;  // e.g., Ctrl+R
    int key_new;     // e.g., Ctrl+N
    int key_save;    // e.g., Ctrl+S
    int key_new_dir; // e.g., Shift+N
    int key_search;  // e.g., Ctrl+F
    int key_select_all; // e.g., Ctrl+A (Select all in current view)
    int key_info;    // e.g., Ctrl+T (Quick file info)
    int key_undo;    // e.g., Ctrl+Z (Undo last file op)
    int key_redo;    // e.g., Ctrl+Y (Redo last file op)
    int key_permissions; // e.g., Ctrl+P (Edit permissions)
    int key_console; // e.g., Ctrl+O (Open console)
    int key_help;    // e.g., H (Show help menu)

    // Dedicated editing keys
    int edit_up;
    int edit_down;
    int edit_left;
    int edit_right;
    int edit_save;
    int edit_quit;
    int edit_backspace;
    int edit_copy;
    int edit_cut;
    int edit_paste;
    int edit_select_all;
    int edit_undo;
    int edit_redo;
    int edit_uppercase;

    // file 
    int info_label_width;
} KeyBindings;


/**
 * Loads user configuration from a file, overriding defaults.
 * 
 * @param kb           Pointer to KeyBindings structure to populate.
 * @param filepath     Path to the configuration file.
 * @param error_buffer Buffer to store error messages.
 * @param buffer_size  Size of the error_buffer.
 * @return             Number of errors encountered. 0 indicates success.
 */
int load_config_file(KeyBindings *kb, const char *filepath, char *error_buffer, size_t buffer_size);

/**
 * Loads default keybindings into the provided KeyBindings structure.
 */
void load_default_keybindings(KeyBindings *kb);

/**
 * Writes a default configuration file to `filepath`, using the provided KeyBindings
 * values for key assignments. Returns true on success.
 */
bool write_default_config_file(const char *filepath, const KeyBindings *kb, char *error_buffer, size_t buffer_size);

// The macro stringifies the field name and, if it matches, assigns the parsed value.
#define CUPID_CFGCMP(x) else if (strcasecmp(name, #x) == 0) { kb->x = parsed; }

#endif // CONFIG_H
