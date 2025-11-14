#ifndef FILES_H
#define FILES_H

#include <curses.h>
#include "config.h"  
#include "vector.h"
// 256 in most systems
#define MAX_FILENAME_LEN 512

typedef struct FileAttributes* FileAttr;

const char *FileAttr_get_name(FileAttr fa);
bool FileAttr_is_dir(FileAttr fa);
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

#endif // FILES_H
