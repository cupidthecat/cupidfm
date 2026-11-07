#define _GNU_SOURCE
#include "app_open.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "app_process.h"
#include "globals.h"
#include "input_prompt.h"
#include "mime.h"
#include "picker.h"
#include "ui.h"

Openers *app_openers_load(void) {
    const char *override = getenv("CUPIDFM_OPENERS");
    char *filename =
        override ? g_strdup(override)
                 : g_build_filename(g_get_home_dir(), ".cupidfm", "openers.cupidconf", NULL);
    GError *error = NULL;
    Openers *set = openers_load(filename, &error);
    if (!set) {
        show_notification(notifwin, "Openers: %s", error ? error->message : "Cannot read settings");
        should_clear_notif = false;
    }
    g_clear_error(&error);
    g_free(filename);
    return set;
}

static GPtrArray *choices(Openers *set, magic_t cookie, const char *path) {
    char type[512];
    struct stat info;
    const char *mime = NULL;
    if (!stat(path, &info) && S_ISDIR(info.st_mode))
        mime = "folder/local";
    else
        mime = mime_type_cached(cookie, path, MAGIC_MIME_TYPE | MAGIC_SYMLINK, type, sizeof(type));
    return openers_match(set, path, mime);
}

static bool launch(AppState *state, const Opener *opener, const char *const *paths, size_t count) {
    return app_process_run(state, opener->command, paths, count, opener->block, opener->orphan);
}

void app_open_paths(AppState *state, bool interactive, const char *const *paths, size_t count) {
    if (!count)
        return;
    Openers *set = app_openers_load();
    if (!set)
        return;
    magic_t cookie = magic_open(MAGIC_MIME_TYPE | MAGIC_SYMLINK);
    if (cookie && magic_load(cookie, NULL)) {
        magic_close(cookie);
        cookie = NULL;
    }
    GPtrArray *candidates = choices(set, cookie, paths[0]);
    GPtrArray *groups = g_ptr_array_new();
    GPtrArray *group_paths = g_ptr_array_new_with_free_func((GDestroyNotify)g_ptr_array_unref);
    if (interactive) {
        for (size_t i = 1; i < count && candidates->len; i++) {
            GPtrArray *other = choices(set, cookie, paths[i]);
            for (guint j = 0; j < candidates->len;) {
                if (g_ptr_array_find(other, g_ptr_array_index(candidates, j), NULL))
                    j++;
                else
                    g_ptr_array_remove_index(candidates, j);
            }
            g_ptr_array_free(other, TRUE);
        }
        GPtrArray *labels = g_ptr_array_new();
        for (guint i = 0; i < candidates->len; i++) {
            Opener *opener = g_ptr_array_index(candidates, i);
            g_ptr_array_add(labels, opener->description && *opener->description
                                        ? opener->description
                                        : opener->name);
        }
        g_ptr_array_add(labels, "Custom command...");
        int selected = picker_choose("Open with", (const char *const *)labels->pdata, labels->len);
        if (selected >= 0 && (guint)selected < candidates->len) {
            Opener *opener = g_ptr_array_index(candidates, (guint)selected);
            bool ok = launch(state, opener, paths, count);
            show_notification(notifwin, "%s%s",
                              opener->description ? opener->description : opener->name,
                              ok ? "" : ": failed");
        } else if (selected >= 0) {
            char command[4096] = {0};
            if (input_prompt("Open command (selected paths in \"$@\")", command, sizeof(command)) &&
                *command)
                show_notification(notifwin, "%s",
                                  app_process_run(state, command, paths, count, true, false)
                                      ? "Command finished"
                                      : "Command failed");
        }
        g_ptr_array_free(labels, TRUE);
    } else {
        /* Files with different default openers are dispatched in separate groups. */
        for (size_t i = 0; i < count; i++) {
            GPtrArray *matched = choices(set, cookie, paths[i]);
            if (matched->len) {
                Opener *opener = g_ptr_array_index(matched, 0);
                guint group;
                if (!g_ptr_array_find(groups, opener, &group)) {
                    group = groups->len;
                    g_ptr_array_add(groups, opener);
                    g_ptr_array_add(group_paths, g_ptr_array_new());
                }
                g_ptr_array_add(g_ptr_array_index(group_paths, group), (gpointer)paths[i]);
            }
            g_ptr_array_free(matched, TRUE);
        }
        size_t failed = 0;
        for (guint i = 0; i < groups->len; i++) {
            GPtrArray *targets = g_ptr_array_index(group_paths, i);
            if (!launch(state, g_ptr_array_index(groups, i), (const char *const *)targets->pdata,
                        targets->len))
                failed++;
        }
        show_notification(notifwin, "Open: %u group(s)%s", groups->len,
                          failed ? "; some failed to start (w to inspect tasks)" : "");
    }
    should_clear_notif = false;
    g_ptr_array_free(group_paths, TRUE);
    g_ptr_array_free(groups, TRUE);
    g_ptr_array_free(candidates, TRUE);
    if (cookie)
        magic_close(cookie);
    openers_free(set);
}
