#ifndef FILES_H
#define FILES_H

#include <curses.h>
#include "config.h"  
#include "vector.h"
// 256 in most systems
#define MAX_FILENAME_LEN 512
#define DIR_SIZE_TOO_LARGE (-2)
#define DIR_SIZE_VIRTUAL_FS (-3)
#define DIR_SIZE_PENDING (-4)
#define DIR_SIZE_PERMISSION_DENIED (-5)
#define DIR_SIZE_REQUEST_DELAY_NS 200000000L

typedef struct FileAttributes* FileAttr;

const char *FileAttr_get_name(FileAttr fa);
bool FileAttr_is_dir(FileAttr fa);
void free_attr(FileAttr fa);
void append_files_to_vec(Vector *v, const char *name);
void append_files_to_vec_lazy(Vector *v, const char *name, size_t max_files, size_t *files_loaded);
size_t count_directory_files(const char *name);
void display_file_info(WINDOW *window, const char *file_path, int max_x);
bool is_supported_file_type(const char *filename);

/**
 * Now the compiler knows what KeyBindings is 
 * because config.h is included above.
 */
void edit_file_in_terminal(WINDOW *window, 
                           const char *file_path, 
                           WINDOW *notifwin, 
                           KeyBindings *kb);

char* format_file_size(char *buffer, size_t size);
long get_directory_size(const char *dir_path);
long get_directory_size_peek(const char *dir_path);
void dir_size_cache_start(void);
void dir_size_cache_stop(void);
void dir_size_note_user_activity(void);
bool dir_size_can_enqueue(void);

#endif // FILES_H
