#define _GNU_SOURCE
#include "app_process.h"

#include <gio/gio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#include "app_session.h"
#include "globals.h"
#include "process.h"
#include "tasks.h"

static void detach(gpointer data) {
    (void)data;
    (void)setsid();
}

bool app_process_run(AppState *state, const char *command, const char *const *paths, size_t count,
                     bool block, bool orphan) {
    if (!state || !command || !*command || count > SIZE_MAX / sizeof(char *) - 5)
        return false;
    if (!block && !orphan)
        return tasks_submit_shell(command, state->current_directory, paths, count);
    char **argv = calloc(count + 5, sizeof(*argv));
    if (!argv)
        return false;
    argv[0] = "sh";
    argv[1] = "-c";
    argv[2] = (char *)command;
    argv[3] = "cupidfm";
    for (size_t i = 0; i < count; i++)
        argv[i + 4] = (char *)paths[i];
    bool ok;
    if (block) {
        def_prog_mode();
        endwin();
        ok = run_process_in_directory(argv, state->current_directory, -1, -1);
        reset_prog_mode();
        refresh();
        clearok(stdscr, TRUE);
        resized = 1;
        app_reload_view(state);
    } else {
        GSubprocessLauncher *launcher = g_subprocess_launcher_new(
            G_SUBPROCESS_FLAGS_STDOUT_SILENCE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
        g_subprocess_launcher_set_cwd(launcher, state->current_directory);
        g_subprocess_launcher_set_child_setup(launcher, detach, NULL, NULL);
        GSubprocess *child =
            g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv, NULL);
        ok = child != NULL;
        /* GSubprocess reaps the child after its last reference is released. */
        if (child)
            g_object_unref(child);
        g_object_unref(launcher);
    }
    free(argv);
    return ok;
}
