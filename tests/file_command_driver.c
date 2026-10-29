#define _POSIX_C_SOURCE 200809L
#include "clipboard.h"
#include "undo.h"
#include "utils.h"
#include "tempfiles.h"
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    if (strcmp(argv[1], "clipboard") == 0)
        return clipboard_set_from_file(argv[2]) ? 0 : 1;
    if (strcmp(argv[1], "paste") == 0 || strcmp(argv[1], "paste-undo") == 0) {
        PasteLog log = {0};
        int count = paste_from_clipboard(argv[2], NULL, &log);
        printf("%d %zu\n", count, log.count);
        bool ok = count > 0;
        if (ok && strcmp(argv[1], "paste-undo") == 0) {
            UndoState state;
            undo_state_init(&state);
            char error[256] = {0};
            ok = undo_state_set_multi(&state, log.kind == PASTE_KIND_CUT ? UNDO_OP_MOVE : UNDO_OP_COPY,
                                      (const char *const *)log.src, (const char *const *)log.dst, log.count) &&
                 undo_state_do_undo(&state, error, sizeof(error));
            if (!ok) fprintf(stderr, "%s\n", error);
            undo_state_clear(&state);
        }
        paste_log_free(&log);
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "cleanup-scope") == 0) {
        char unrelated[256], trash[256];
        snprintf(unrelated,sizeof(unrelated),"/tmp/cupidfm_unrelated_%d",getpid());
        snprintf(trash,sizeof(trash),"/tmp/cupidfm_trash_%d",getpid());
        int fd = open(unrelated,O_CREAT|O_EXCL|O_WRONLY,0600);
        if (fd < 0) return 2;
        close(fd);
        if (mkdir(trash,0700)) { unlink(unrelated); return 2; }
        cleanup_temp_files();
        bool ok = access(unrelated,F_OK)==0 && access(trash,F_OK)!=0;
        unlink(unrelated);
        rmdir(trash);
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "cut-cleanup") == 0) {
        bool ok = cut_to_clipboard(argv[2]);
        cleanup_temp_files();
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "copy-clipboard") == 0)
        return clipboard_set_paths((const char *const *)&argv[2], (size_t)argc - 2, false) ? 0 : 1;
    if (strcmp(argv[1], "cut-clipboard") == 0)
        return clipboard_set_paths((const char *const *)&argv[2], (size_t)argc - 2, true) ? 0 : 1;
    if (strcmp(argv[1], "trash-preexisting-link") == 0 && argc == 4) {
        char directory[4096], trashed[4096];
        snprintf(directory, sizeof(directory), "/tmp/cupidfm_trash_%d", getpid());
        if (symlink(argv[3], directory) != 0) return 2;
        bool ok = delete_item(argv[2], trashed, sizeof(trashed));
        unlink(directory);
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "trash-insecure-dir") == 0) {
        char directory[4096], trashed[4096];
        snprintf(directory, sizeof(directory), "/tmp/cupidfm_trash_%d", getpid());
        if (mkdir(directory, 0700) != 0 || chmod(directory, 0777) != 0) return 2;
        bool ok = delete_item(argv[2], trashed, sizeof(trashed));
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "trash-small") == 0) {
        char trashed[1];
        return delete_item(argv[2], trashed, sizeof(trashed)) ? 0 : 1;
    }
    if (strcmp(argv[1], "trash") == 0) {
        for (int i = 2; i < argc; ++i) {
            char trashed[4096] = {0};
            if (!delete_item(argv[i], trashed, sizeof(trashed))) return 1;
            printf("%s\n", trashed);
        }
        return 0;
    }
    UndoState state;
    undo_state_init(&state);
    char error[256] = {0};
    bool ok = false;
    if (strcmp(argv[1], "copy") == 0 && argc == 4) {
        if (!undo_state_set_single(&state, UNDO_OP_COPY, argv[2], argv[3])) return 2;
        state.redo = state.undo;
        state.undo = (UndoOp){0};
        ok = undo_state_do_redo(&state, error, sizeof(error));
    } else if (strcmp(argv[1], "move") == 0 && argc == 4) {
        if (!undo_state_set_single(&state, UNDO_OP_MOVE, argv[3], argv[2])) return 2;
        ok = undo_state_do_undo(&state, error, sizeof(error));
    } else if (strcmp(argv[1], "remove") == 0) {
        if (!undo_state_set_single(&state, UNDO_OP_CREATE_DIR, NULL, argv[2])) return 2;
        ok = undo_state_do_undo(&state, error, sizeof(error));
    }
    if (!ok) fprintf(stderr, "%s\n", error);
    undo_state_clear(&state);
    return ok ? 0 : 1;
}
