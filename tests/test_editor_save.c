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
void plugins_notify_editor_save(PluginManager *pm, const char *path) {
    (void)pm; (void)path; ++save_notifications;
}
int main(void) {
    setup();
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
    fclose(f); unlink(path); teardown();
    puts("Editor save checks passed");
}
