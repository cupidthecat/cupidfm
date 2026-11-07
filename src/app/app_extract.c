#define _GNU_SOURCE
#include "app_extract.h"

#include <glib.h>
#include <string.h>
#include <sys/stat.h>

#include "app_paths.h"
#include "app_session.h"
#include "input_prompt.h"
#include "tasks.h"
#include "ui.h"

static char *archive_stem(const char *path) {
    char *name = g_path_get_basename(path);
    const char *extensions[] = {"tar", "gz",  "gzip", "bz2", "bzip2", "xz",  "zip", "7z",
                                "tgz", "tbz", "tbz2", "txz", "cbz",   "jar", "apk", "epub"};
    for (;;) {
        char *dot = strrchr(name, '.');
        bool matched = false;
        if (!dot || dot == name)
            break;
        for (size_t i = 0; i < G_N_ELEMENTS(extensions); i++)
            if (!g_ascii_strcasecmp(dot + 1, extensions[i])) {
                *dot = 0;
                matched = true;
                break;
            }
        if (!matched)
            break;
    }
    if (!*name || !strcmp(name, ".") || !strcmp(name, "..")) {
        g_free(name);
        return g_strdup("extracted");
    }
    return name;
}

size_t app_extract_paths(AppState *state, const char *const *paths, size_t count,
                         const char *directory) {
    (void)state;
    size_t queued = 0;
    for (size_t i = 0; i < count; i++) {
        char *parent = directory ? g_strdup(directory) : g_path_get_dirname(paths[i]);
        char *stem = archive_stem(paths[i]);
        char *target = g_build_filename(parent, stem, NULL);
        if (strlen(target) + 5 < MAX_PATH_LENGTH &&
            tasks_submit(TASK_EXTRACT, &paths[i], 1, target, false))
            queued++;
        g_free(target);
        g_free(stem);
        g_free(parent);
    }
    return queued;
}

void app_extract_selected(AppState *state, bool choose_directory) {
    char directory[MAX_PATH_LENGTH] = {0};
    if (choose_directory) {
        snprintf(directory, sizeof(directory), "%s", state->current_directory);
        InputPromptOptions options = {.directory = state->current_directory,
                                      .directories_only = true,
                                      .expand_home = true,
                                      .history = "extract"};
        if (!input_prompt_ex("Extract into directory", directory, sizeof(directory), &options))
            return;
        if (directory[0] == '~' && (directory[1] == '/' || !directory[1])) {
            const char *home = getenv("HOME");
            char expanded[MAX_PATH_LENGTH];
            int size =
                home ? snprintf(expanded, sizeof(expanded), "%s%s", home, directory + 1) : -1;
            if (size < 0 || (size_t)size >= sizeof(expanded))
                directory[0] = 0;
            else
                strcpy(directory, expanded);
        }
        resolve_path_under_cwd(directory, state->current_directory, directory);
        struct stat info;
        if (!*directory || stat(directory, &info) || !S_ISDIR(info.st_mode)) {
            show_notification(notifwin, "Choose an existing extraction directory");
            should_clear_notif = false;
            return;
        }
    }
    Vector paths;
    if (!app_selection_paths(state, &paths))
        return;
    size_t count = Vector_len(paths);
    size_t queued = app_extract_paths(state, (const char *const *)paths.el, count,
                                      choose_directory ? directory : NULL);
    show_notification(notifwin, "Extraction: %zu/%zu queued; w shows progress and errors", queued,
                      count);
    should_clear_notif = false;
    Vector_bye(&paths);
}
