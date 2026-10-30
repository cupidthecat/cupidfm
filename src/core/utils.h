#ifndef UTILS_H
#define UTILS_H

#include "vecstack.h"
#include "vector.h"
#include <ncurses.h>
#include <stdbool.h>
#define SIZE int

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

__attribute__((noreturn)) void die(int r, const char *format, ...);
bool is_directory(const char *path, const char *filename);

void create_file(const char *filename);
void display_files(const char *directory);
void preview_file(const char *filename);
void change_directory(const char *new_directory, const char ***files, int *num_files,
                      int *selected_entry, int *start_entry, int *end_entry);
void path_join(char *result, const char *base, const char *extra);
void reload_directory(Vector *files, const char *current_directory);
void reload_directory_lazy(Vector *files, const char *current_directory, size_t *files_loaded,
                           size_t *total_files);
void load_more_files_if_needed(Vector *files, const char *current_directory, void *cas,
                               size_t *files_loaded, size_t total_files);

// short cut utils
bool copy_to_clipboard(const char *path);

typedef enum {
    PASTE_KIND_NONE = 0,
    PASTE_KIND_COPY,
    PASTE_KIND_CUT,
} PasteKind;

typedef struct {
    PasteKind kind;
    size_t count;
    char **src; // source paths used for the operation (cut uses storage paths)
    char **dst; // destination paths created in target_directory
} PasteLog;

void paste_log_free(PasteLog *log);

// Returns number of pasted items on success, -1 on failure.
// If log is non-NULL, it will be filled with per-item src/dst info (best effort).
int paste_from_clipboard(const char *target_directory, const char *filename, PasteLog *log);

// Record a cut without moving or deleting the source. Paste performs the move.
bool cut_to_clipboard(const char *path);

// Soft-delete to session trash (to support undo). On success, fills out_trashed_path.
bool delete_item(const char *path, char *out_trashed_path, size_t out_len);
bool confirm_delete(const char *path, bool *should_delete);
bool rename_item(WINDOW *notifwin, const char *old_path, char *out_new_path, size_t out_len);
bool create_new_file(WINDOW *win, const char *dir_path, char *out_created_path, size_t out_len);
bool create_new_directory(WINDOW *win, const char *dir_path, char *out_created_path,
                          size_t out_len);
bool change_permissions(WINDOW *win, const char *path);
#endif // UTILS_H
