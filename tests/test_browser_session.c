#define _GNU_SOURCE
#include "app_mouse.h"
#include "app_navigation.h"
#include "app_session.h"
#include "git.h"
#include "search.h"
#include "utils.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool g_select_all_highlight;
bool should_clear_notif;
WINDOW *notifwin;
WINDOW *dirwin;
WINDOW *previewwin;

GitStatus git_query_file_status(const char *path) {
    (void)path;
    return GIT_STATUS_ERROR;
}

void git_update_directory(const char *path) {
    (void)path;
}

void mime_cache_clear(void) {
}

void text_preview_clear_index(void) {
}

void show_notification(WINDOW *window, const char *format, ...) {
    (void)window;
    (void)format;
}

static void fixture_file(const char *root, const char *name, const char *text) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", root, name);
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(!fclose(file));
}

static const char *name_at(AppState *state, size_t index) {
    return FileAttr_get_name(active_files(state)->el[index]);
}

int main(void) {
    char directory[] = "/tmp/cupidfm-session-XXXXXX";
    assert(mkdtemp(directory));
    char child[4096];
    snprintf(child, sizeof(child), "%s/child", directory);
    assert(!mkdir(child, 0700));
    fixture_file(directory, "file2.txt", "two");
    fixture_file(directory, "file10.txt", "ten");
    fixture_file(directory, ".hidden", "hidden");
    FileListingOptions options = {.sort = FILE_SORT_NATURAL, .dir_first = true};
    file_listing_configure(options);
    AppState state = {0};
    state.files = Vector_new(1);
    state.search_files = Vector_new(1);
    state.current_directory = strdup(directory);
    state.lazy_load.directory_path = strdup(directory);
    state.dir_window_cas.num_lines = 20;
    app_reload_view(&state);
    assert(Vector_len(state.files) == 3);
    assert(!strcmp(name_at(&state, 0), "child"));
    assert(!strcmp(name_at(&state, 1), "file2.txt"));
    assert(!strcmp(name_at(&state, 2), "file10.txt"));
    FILE *input = tmpfile(), *output = tmpfile();
    assert(input && output);
    SCREEN *screen = newterm("xterm-256color", output, input);
    assert(screen);
    set_term(screen);
    assert(resizeterm(32, 120) != ERR);
    dirwin = newwin(26, 35, 4, 1);
    previewwin = newwin(26, 75, 4, 40);
    assert(dirwin && previewwin);
    MEVENT event = {.x = 5, .y = 7, .bstate = BUTTON1_PRESSED};
    assert(app_mouse_event(&state, &event) == APP_MOUSE_HANDLED);
    assert(state.dir_window_cas.cursor == 2);
    event.y = 4; /* The border does not select an entry. */
    event.bstate = BUTTON1_RELEASED;
    assert(app_mouse_event(&state, &event) == APP_MOUSE_NONE);
    assert(!state.mouse_drag && state.dir_window_cas.cursor == 2);
    event.y = 5;
    event.bstate = BUTTON1_PRESSED;
    assert(app_mouse_event(&state, &event) == APP_MOUSE_HANDLED);
    event.y = 7;
    event.bstate = REPORT_MOUSE_POSITION | BUTTON1_PRESSED;
    assert(app_mouse_event(&state, &event) == APP_MOUSE_HANDLED);
    assert(app_selection_count(&state) == 3);
    event.y = 6;
    assert(app_mouse_event(&state, &event) == APP_MOUSE_HANDLED);
    assert(app_selection_count(&state) == 2);
    event.bstate = BUTTON1_RELEASED;
    assert(app_mouse_event(&state, &event) == APP_MOUSE_HANDLED);
    assert(!state.mouse_drag && !state.visual_active);
    app_selection_clear(&state);
    event.bstate = BUTTON4_PRESSED;
    assert(app_mouse_event(&state, &event) == APP_MOUSE_HANDLED);
    assert(state.dir_window_cas.cursor == 0);
#ifdef BUTTON5_PRESSED
    event.bstate = BUTTON5_PRESSED;
    assert(app_mouse_event(&state, &event) == APP_MOUSE_HANDLED);
    assert(state.dir_window_cas.cursor == 2);
#endif
    event.y = 15; /* Empty rows cannot open the hovered entry. */
    event.bstate = BUTTON3_PRESSED;
    assert(app_mouse_event(&state, &event) == APP_MOUSE_HANDLED);
    event.y = 5;
    assert(app_mouse_event(&state, &event) == APP_MOUSE_ENTER);
    assert(state.dir_window_cas.cursor == 0);
    delwin(dirwin);
    delwin(previewwin);
    dirwin = previewwin = NULL;
    endwin();
    delscreen(screen);
    fclose(input);
    fclose(output);
    app_move_cursor(&state, 2);
    FileAttr_select(state.files.el[1], true);
    assert(app_selection_count(&state) == 1);
    app_reload_view(&state);
    assert(!strcmp(state.selected_entry, "file10.txt"));
    assert(app_selection_count(&state) == 1);

    /* Back/forward restores the hovered entry and drops forward history on a new branch. */
    app_history_remember(&state);
    assert(app_change_directory(&state, child));
    app_history_remember(&state);
    assert(state.history_length == 2);
    assert(app_history_move(&state, -1));
    assert(!strcmp(state.selected_entry, "file10.txt"));
    assert(app_history_move(&state, 1));
    assert(!strcmp(state.current_directory, child));
    assert(!app_change_directory(&state, "/no-such-cupidfm-directory"));
    assert(!strcmp(state.current_directory, child));
    assert(app_history_move(&state, -1));
    assert(app_change_directory(&state, "/tmp"));
    app_history_remember(&state);
    assert(state.history_length == 2 && !strcmp(state.history[1], "/tmp"));
    assert(!app_history_move(&state, 1));
    assert(app_change_directory(&state, directory));

    /* A visual range shrinks, restores its baseline, and can cross an end. */
    app_selection_clear(&state);
    FileAttr_select(state.files.el[0], true);
    for (size_t i = 0; i < Vector_len(state.files); i++)
        FileAttr_visual_begin(state.files.el[i]);
    state.visual_active = true;
    state.visual_anchor = 1;
    app_move_cursor(&state, 2);
    assert(app_selection_count(&state) == 3);
    app_move_cursor(&state, 1);
    assert(app_selection_count(&state) == 2);
    assert(!FileAttr_selected(state.files.el[2]));
    state.visual_wraps = 1;
    app_move_cursor(&state, 0);
    assert(app_selection_count(&state) == 3);
    app_selection_clear(&state);

    /* Independent tab ownership includes cursor, query, options, selection, and history. */
    AppTabs tabs = {.count = 1};
    snprintf(state.tab_name, sizeof(state.tab_name), "first");
    FileAttr_select(state.files.el[1], true);
    app_move_cursor(&state, 2);
    assert(app_tab_create(&tabs, &state));
    assert(tabs.count == 2 && tabs.active == 1);
    assert(!state.tab_name[0] && !app_selection_count(&state));
    snprintf(state.tab_name, sizeof(state.tab_name), "second");
    options.reverse = true;
    options.show_hidden = true;
    file_listing_configure(options);
    app_reload_view(&state);
    assert(Vector_len(state.files) == 4);
    assert(app_tab_switch(&tabs, &state, 0));
    assert(!strcmp(state.tab_name, "first"));
    assert(!strcmp(state.selected_entry, "file10.txt"));
    assert(app_selection_count(&state) == 1);
    assert(!file_listing_options().reverse && !file_listing_options().show_hidden);
    assert(app_tab_create(&tabs, &state));
    snprintf(state.tab_name, sizeof(state.tab_name), "inserted");
    assert(tabs.active == 1 && !strcmp(tabs.saved[2]->tab_name, "second"));
    assert(app_tab_close(&tabs, &state));
    assert(tabs.active == 0 && !strcmp(state.tab_name, "first"));
    assert(app_tab_switch(&tabs, &state, 1));
    assert(!strcmp(state.tab_name, "second") && file_listing_options().reverse);
    while (tabs.count < APP_TAB_LIMIT)
        assert(app_tab_create(&tabs, &state));
    assert(!app_tab_create(&tabs, &state));
    while (tabs.count > 1)
        assert(app_tab_close(&tabs, &state));
    assert(!app_tab_close(&tabs, &state));
    app_tabs_dispose(&tabs);
    app_state_dispose(&state);

    /* Global sorting must include entries after the former 200-entry lazy boundary. */
    file_listing_configure((FileListingOptions){.sort = FILE_SORT_ALPHABETICAL});
    Vector files = Vector_new(1);
    for (int i = 0; i < 260; i++) {
        char name[32];
        snprintf(name, sizeof(name), "bulk-%03d", i);
        fixture_file(directory, name, "");
    }
    size_t loaded = 0, total = 0;
    reload_directory_lazy(&files, directory, &loaded, &total);
    assert(loaded == 263 && total == loaded && Vector_len(files) == loaded);
    assert(!strcmp(FileAttr_get_name(files.el[0]), "bulk-000"));
    for (size_t i = 0; i < Vector_len(files); i++)
        free_attr(files.el[i]);
    Vector_set_len_no_free(&files, 0);
    Vector_bye(&files);
    for (int i = 0; i < 260; i++) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/bulk-%03d", directory, i);
        assert(!unlink(path));
    }
    const char *names[] = {"file2.txt", "file10.txt", ".hidden"};
    for (size_t i = 0; i < 3; i++) {
        char path[4096];
        snprintf(path, sizeof(path), "%s/%s", directory, names[i]);
        assert(!unlink(path));
    }
    assert(!rmdir(child));
    assert(!rmdir(directory));
    puts("Browser sorting, selection, visual ranges, history, and tab ownership passed");
    return 0;
}
