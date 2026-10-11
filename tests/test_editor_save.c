#define _POSIX_C_SOURCE 200809L
#include "../src/fs/files.c"
#include <assert.h>

volatile sig_atomic_t is_editing = 1;

static TextBuffer buffer;
static void setup(void) {
    buffer = (TextBuffer){.num_lines = 2, .capacity = 2};
    buffer.lines = calloc(2, sizeof(char *));
    assert(buffer.lines);
    buffer.lines[0] = strdup("abc");
    buffer.lines[1] = strdup("def");
    assert(buffer.lines[0] && buffer.lines[1]);
    g_editor_buffer = &buffer;
    g_editor_cursor_line = 0;
    g_editor_cursor_col = 0;
    g_editor_dirty = false;
}
static void teardown(void) {
    for (int i = 0; i < buffer.num_lines; ++i) free(buffer.lines[i]);
    free(buffer.lines);
    g_editor_buffer = NULL;
}

char g_editor_path[MAX_PATH_LENGTH];
static int save_notifications;
static int capture_open;
static char opened_path[MAX_PATH_LENGTH * 2];
FILE *__real_fopen(const char *, const char *);
FILE *__wrap_fopen(const char *path, const char *mode) {
    if (!capture_open) return __real_fopen(path, mode);
    snprintf(opened_path, sizeof(opened_path), "%s", path);
    errno = EACCES;
    return NULL;
}
void plugins_request_reload(PluginManager *pm) { (void)pm; }
void plugins_request_select(PluginManager *pm, const char *name) { (void)pm; (void)name; }

void plugins_notify_editor_save(PluginManager *pm, const char *path) {
    (void)pm; (void)path; ++save_notifications;
}
int main(void) {
    setup();
    capture_open = 1;
    strcpy(g_editor_path, "/original.txt");
    assert(!editor_save_as(NULL, "sibling.txt"));
    assert(strcmp(opened_path, "/sibling.txt") == 0);
    strcpy(g_editor_path, "/some/dir/original.txt");
    assert(!editor_save_as(NULL, "sibling.txt"));
    assert(strcmp(opened_path, "/some/dir/sibling.txt") == 0);
    char oversized[MAX_PATH_LENGTH + 1];
    memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = 0;
    opened_path[0] = 0;
    assert(!editor_save_as(NULL, oversized) && !opened_path[0]);
    capture_open = 0;
    strcpy(g_editor_path, "/dev/full");
    g_editor_dirty = true; g_editor_dirty_clear_requested = false;
    assert(!editor_save_current((PluginManager *)&buffer));
    assert(g_editor_dirty && !g_editor_dirty_clear_requested && !save_notifications);
    char path[] = "/tmp/cupidfm-save-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    strcpy(g_editor_path, path);
    assert(editor_save_current((PluginManager *)&buffer));
    assert(!g_editor_dirty && g_editor_dirty_clear_requested && save_notifications == 1);
    FILE *f = fopen(path, "r"); assert(f);
    char result[64] = {0}; assert(fread(result, 1, sizeof(result) - 1, f) == 8);
    assert(strcmp(result, "abc\ndef\n") == 0);
    fclose(f);
    g_editor_dirty = true;
    assert(editor_save_as(NULL, g_editor_path));
    assert(strcmp(g_editor_path, path) == 0 && !g_editor_dirty);
    unlink(path); teardown();
    puts("Editor save checks passed");
}
