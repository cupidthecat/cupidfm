#define _GNU_SOURCE
#include "app_bulk.h"

#include <errno.h>
#include <fcntl.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_open.h"
#include "app_process.h"
#include "app_session.h"
#include "bulk_rename.h"
#include "globals.h"
#include "picker.h"
#include "ui.h"

void app_bulk_rename(AppState *state) {
    Vector paths = {0};
    BulkRenamePlan plan = {0};
    char *error = NULL;
    Openers *set = NULL;
    GPtrArray *choices = NULL, *review = NULL;
    FILE *file = NULL;
    char *temporary = NULL;
    int fd = -1;
    if (!app_selection_paths(state, &paths))
        return;
    if (!bulk_rename_prepare(&plan, (const char *const *)paths.el, Vector_len(paths), &error))
        goto done;
    set = app_openers_load();
    if (!set)
        goto done;
    choices = openers_match(set, "bulk-rename.txt", "text/plain");
    Opener *editor = NULL;
    for (guint i = 0; i < choices->len; i++) {
        Opener *candidate = g_ptr_array_index(choices, i);
        if (candidate->block) {
            editor = candidate;
            break;
        }
    }
    if (!editor) {
        error = g_strdup("No blocking text opener is configured");
        goto done;
    }
    fd = g_file_open_tmp("cupidfm-rename-XXXXXX", &temporary, NULL);
    if (fd < 0) {
        error = g_strdup("Cannot create a private rename list");
        goto done;
    }
    file = fdopen(fd, "w");
    if (!file) {
        close(fd);
        fd = -1;
        error = g_strdup("Cannot write the rename list");
        goto done;
    }
    fd = -1;
    bool written = bulk_rename_write(&plan, file, &error);
    if (fclose(file) && written) {
        error = g_strdup("Cannot close the rename list");
        written = false;
    }
    file = NULL;
    if (!written)
        goto done;
    const char *targets[] = {temporary};
    if (!app_process_run(state, editor->command, targets, 1, true, false)) {
        error = g_strdup("The text opener did not exit successfully");
        goto done;
    }
    fd = open(temporary, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    struct stat info;
    if (fd < 0 || fstat(fd, &info) || !S_ISREG(info.st_mode)) {
        error = g_strdup("The edited rename list must be a regular file");
        goto done;
    }
    file = fdopen(fd, "r");
    if (!file) {
        error = g_strdup("Cannot read the edited rename list");
        goto done;
    }
    fd = -1;
    if (!bulk_rename_read(&plan, file, &error) || !plan.changed)
        goto done;
    review = g_ptr_array_new_with_free_func(g_free);
    for (size_t i = 0; i < plan.count; i++) {
        BulkRenameEntry *entry = &plan.entries[i];
        if (strcmp(entry->old_path, entry->new_path))
            g_ptr_array_add(review, g_strdup_printf("%s -> %s", entry->old_path, entry->new_path));
    }
    char title[96];
    snprintf(title, sizeof(title), "Rename %zu entries? Enter approves; Esc cancels", plan.changed);
    if (picker_choose(title, (const char *const *)review->pdata, review->len) < 0)
        goto done;
    bulk_rename_apply(&plan);
    /* Remove all old selections before adding new ones, including rename chains. */
    for (size_t i = 0; i < plan.count; i++)
        if (plan.entries[i].renamed)
            selection_set(&state->selection, plan.entries[i].old_path, false, false);
    for (size_t i = 0; i < plan.count; i++)
        if (plan.entries[i].renamed)
            selection_set(&state->selection, plan.entries[i].new_path, plan.entries[i].directory,
                          true);
    app_reload_view(state);
    show_notification(notifwin, "Renamed %zu; failed %zu", plan.succeeded, plan.failed);
    should_clear_notif = false;
    if (plan.failed) {
        g_ptr_array_set_size(review, 0);
        for (size_t i = 0; i < plan.count; i++)
            if (plan.entries[i].error)
                g_ptr_array_add(review,
                                g_strdup_printf("%s -> %s: %s", plan.entries[i].old_path,
                                                plan.entries[i].new_path, plan.entries[i].error));
        (void)picker_choose("Rename errors (Esc closes)", (const char *const *)review->pdata,
                            review->len);
    }
done:
    if (error) {
        show_notification(notifwin, "Bulk rename: %s", error);
        should_clear_notif = false;
    }
    if (file)
        fclose(file);
    if (fd >= 0)
        close(fd);
    if (temporary)
        unlink(temporary);
    g_free(temporary);
    if (review)
        g_ptr_array_free(review, TRUE);
    if (choices)
        g_ptr_array_free(choices, TRUE);
    openers_free(set);
    bulk_rename_free(&plan);
    g_free(error);
    Vector_bye(&paths);
}
