#include <vector.h>
#include <curses.h>

// 256 in most systems
#define MAX_FILENAME_LEN 512

typedef struct FileAttributes* FileAttr;

const char *FileAttr_get_name(FileAttr fa);
bool FileAttr_is_dir(FileAttr fa);
void append_files_to_vec(Vector *v, const char *name);
void display_file_info(WINDOW *window, const char *file_path, int max_x);
char* format_file_size(char *buffer, size_t size);
