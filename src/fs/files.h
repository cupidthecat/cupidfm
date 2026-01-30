#ifndef FILES_H
#define FILES_H

#include "core/main.h"
#include <stdbool.h>

// Ctrl+Shift+Letter key codes (defined in files.c)
#define CTRL_SHIFT_A_CODE 0x2001
#define CTRL_SHIFT_B_CODE 0x2002
#define CTRL_SHIFT_C_CODE 0x2003
#define CTRL_SHIFT_D_CODE 0x2004
#define CTRL_SHIFT_E_CODE 0x2005
#define CTRL_SHIFT_F_CODE 0x2006
#define CTRL_SHIFT_G_CODE 0x2007
#define CTRL_SHIFT_H_CODE 0x2008
#define CTRL_SHIFT_I_CODE 0x2009
#define CTRL_SHIFT_J_CODE 0x200A
#define CTRL_SHIFT_K_CODE 0x200B
#define CTRL_SHIFT_L_CODE 0x200C
#define CTRL_SHIFT_M_CODE 0x200D
#define CTRL_SHIFT_N_CODE 0x200E
#define CTRL_SHIFT_O_CODE 0x200F
#define CTRL_SHIFT_P_CODE 0x2010
#define CTRL_SHIFT_Q_CODE 0x2011
#define CTRL_SHIFT_R_CODE 0x2012
#define CTRL_SHIFT_S_CODE 0x2013
#define CTRL_SHIFT_T_CODE 0x2014
#define CTRL_SHIFT_U_CODE 0x2015
#define CTRL_SHIFT_V_CODE 0x2016
#define CTRL_SHIFT_W_CODE 0x2017
#define CTRL_SHIFT_X_CODE 0x2018
#define CTRL_SHIFT_Y_CODE 0x2019
#define CTRL_SHIFT_Z_CODE 0x201A
#include "config.h"
#include "vector.h"
#include <curses.h>


// Forward declaration
struct PluginManager;

// 256 in most systems
#define MAX_FILENAME_LEN 512
#define DIR_SIZE_TOO_LARGE (-2)
#define DIR_SIZE_VIRTUAL_FS (-3)
#define DIR_SIZE_PENDING (-4)
#define DIR_SIZE_PERMISSION_DENIED (-5)
#define DIR_SIZE_REQUEST_DELAY_NS 200000000L

typedef struct FileAttributes *FileAttr;

const char *FileAttr_get_name(FileAttr fa);
bool FileAttr_is_dir(FileAttr fa);
void free_attr(FileAttr fa);
void append_files_to_vec(Vector *v, const char *name);
void append_files_to_vec_lazy(Vector *v, const char *name, size_t max_files,
                              size_t *files_loaded);
size_t count_directory_files(const char *name);
void display_file_info(WINDOW *window, const char *file_path, int max_x);
bool is_supported_file_type(const char *filename);
bool is_archive_file(const char *filename);
void display_archive_preview(WINDOW *window, const char *file_path,
                             int start_line, int max_y, int max_x);
void format_dir_size_pending_animation(char *buffer, size_t len, bool reset);
// Returns the best-known in-progress byte total for a directory size job, or 0
// if none.
long dir_size_get_progress(const char *dir_path);

// Returns a newly allocated copy of the current editor buffer, or NULL if not
// editing. Caller must free the returned string.
char *editor_get_content_copy(void);

// Returns a newly allocated copy of a 1-indexed line from the editor, or NULL.
// Caller must free the returned string.
char *editor_get_line_copy(int line_num);

// Returns the total number of lines in the editor, or 0 if not editing.
int editor_get_line_count(void);

// Returns the current cursor position (1-indexed). Returns false if not
// editing.
bool editor_get_cursor(int *line, int *col);

// Sets the cursor position (1-indexed). Returns false if not editing or invalid
// position.
bool editor_set_cursor(int line, int col);

// Returns the current selection bounds (1-indexed). Returns false if no
// selection.
bool editor_get_selection(int *start_line, int *start_col, int *end_line,
                          int *end_col);

// Inserts text at the current cursor position. Returns false on failure.
bool editor_insert_text(const char *text);

// Replaces text in the specified range (1-indexed). Returns false on failure.
bool editor_replace_text(int start_line, int start_col, int end_line,
                         int end_col, const char *text);

// Deletes text in the specified range (1-indexed). Returns false on failure.
bool editor_delete_range(int start_line, int start_col, int end_line,
                         int end_col);

/**
 * Now the compiler knows what KeyBindings is
 * because config.h is included above.
 */
void edit_file_in_terminal(WINDOW *window, const char *file_path,
                           WINDOW *notifwin, KeyBindings *kb,
                           struct PluginManager *pm);

char *format_file_size(char *buffer, size_t size);
long get_directory_size(const char *dir_path);
long get_directory_size_peek(const char *dir_path);
void dir_size_cache_start(void);
void dir_size_cache_stop(void);
void dir_size_note_user_activity(void);
bool dir_size_can_enqueue(void);

#endif // FILES_H
