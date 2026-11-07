#define _GNU_SOURCE
#include "app_jump.h"

#include <glib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_session.h"
#include "process.h"
#include "search.h"
#include "ui.h"

static GPtrArray *read_result(FILE *file, bool history) {
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    char path[MAX_PATH_LENGTH];
    size_t used = 0;
    int c;
    while ((c = fgetc(file)) != EOF) {
        if (!history && !c) {
            path[used] = 0;
            if (used)
                g_ptr_array_add(paths, g_strdup(path));
            used = 0;
            if (paths->len > 65536)
                goto fail;
        } else {
            if (!c || used + 1 >= sizeof(path))
                goto fail;
            path[used++] = (char)c;
        }
    }
    if (ferror(file) || (!history && used))
        goto fail;
    if (history && used) {
        if (path[used - 1] == '\n')
            used--;
        path[used] = 0;
        if (used)
            g_ptr_array_add(paths, g_strdup(path));
    }
    return paths;
fail:
    g_ptr_array_free(paths, TRUE);
    return NULL;
}

static bool reveal(AppState *state, const char *path, bool enter) {
    struct stat info;
    if (enter && !stat(path, &info) && S_ISDIR(info.st_mode))
        return app_change_directory(state, path);
    if (lstat(path, &info))
        return false;
    char *parent = g_path_get_dirname(path), *name = g_path_get_basename(path);
    bool ok = app_change_directory(state, parent);
    if (ok) {
        SIZE index = find_loaded_index_by_name(&state->files, name);
        if (index != (SIZE)-1)
            app_move_cursor(state, index);
        else
            ok = false;
    }
    g_free(parent);
    g_free(name);
    return ok;
}

void app_jump(AppState *state, bool history) {
    const char *name = history ? "zoxide" : "fzf";
    char *program = g_find_program_in_path(name);
    if (!program) {
        show_notification(notifwin, "%s is not installed", name);
        should_clear_notif = false;
        return;
    }
    Vector selected = {0};
    FILE *input = NULL, *output = tmpfile();
    GPtrArray *result = NULL;
    char *error = NULL;
    size_t selected_count = app_selection_count(state);
    if (!output || (!history && selected_count && !app_selection_paths(state, &selected))) {
        error = g_strdup("Cannot prepare navigation");
        goto done;
    }
    if (!history && selected_count) {
        input = tmpfile();
        if (!input) {
            error = g_strdup("Cannot prepare selected paths");
            goto done;
        }
        for (size_t i = 0; i < Vector_len(selected); i++) {
            const char *path = selected.el[i];
            if (fwrite(path, 1, strlen(path) + 1, input) != strlen(path) + 1) {
                error = g_strdup("Cannot write selected paths");
                goto done;
            }
        }
        if (fflush(input) || fseek(input, 0, SEEK_SET)) {
            error = g_strdup("Cannot read selected paths");
            goto done;
        }
    }
    char *fzf[] = {program, "--multi", "--print0", input ? "--read0" : NULL, NULL};
    char *zoxide[] = {program, "query", "-i", "--exclude", state->current_directory, NULL};
    def_prog_mode();
    endwin();
    int status = run_process_status_in_directory(history ? zoxide : fzf, state->current_directory,
                                                 input ? fileno(input) : -1, fileno(output));
    reset_prog_mode();
    refresh();
    clearok(stdscr, TRUE);
    resized = 1;
    if (status == 130)
        goto done;
    if (status != 0) {
        error = g_strdup(history && status == 1
                             ? "No directory history match; configure zoxide in your shell"
                             : "Navigation command failed or found no selection");
        goto done;
    }
    if (fflush(output) || fseek(output, 0, SEEK_SET) || !(result = read_result(output, history))) {
        error = g_strdup("Navigation returned an invalid or oversized path list");
        goto done;
    }
    for (guint i = 0; i < result->len; i++) {
        char *path =
            g_canonicalize_filename(g_ptr_array_index(result, i), state->current_directory);
        if (strlen(path) >= MAX_PATH_LENGTH) {
            g_free(path);
            error = g_strdup("Selected path exceeds the browser path limit");
            goto done;
        }
        g_free(g_ptr_array_index(result, i));
        g_ptr_array_index(result, i) = path;
    }
    if (result->len == 1) {
        const char *path = g_ptr_array_index(result, 0);
        bool ok =
            history ? app_change_directory(state, path) : reveal(state, path, !selected_count);
        if (!ok)
            error = g_strdup("Cannot open the selected location");
    } else if (result->len > 1) {
        size_t failed = 0;
        for (guint i = 0; i < result->len; i++) {
            const char *path = g_ptr_array_index(result, i);
            struct stat info, target;
            if (lstat(path, &info)) {
                failed++;
                continue;
            }
            bool directory =
                S_ISDIR(info.st_mode) ||
                (S_ISLNK(info.st_mode) && !stat(path, &target) && S_ISDIR(target.st_mode));
            if (!selection_set(&state->selection, path, directory, selected_count == 0))
                failed++;
        }
        app_reload_view(state);
        if (failed)
            error = g_strdup("Some selected paths were missing or overlapped another selection");
    }
done:
    if (error) {
        show_notification(notifwin, "%s: %s", name, error);
        should_clear_notif = false;
    }
    if (result)
        g_ptr_array_free(result, TRUE);
    if (input)
        fclose(input);
    if (output)
        fclose(output);
    Vector_bye(&selected);
    g_free(error);
    g_free(program);
}
