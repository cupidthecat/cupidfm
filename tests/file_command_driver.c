#include "clipboard.h"
#include "undo.h"
#include "utils.h"
#include "tempfiles.h"
#include <stdio.h>
#include <string.h>

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
    if (strcmp(argv[1], "cut-cleanup") == 0) {
        bool ok = cut_to_clipboard(argv[2]);
        cleanup_temp_files();
        return ok ? 0 : 1;
    }
    if (strcmp(argv[1], "copy-clipboard") == 0)
        return clipboard_set_paths((const char *const *)&argv[2], (size_t)argc - 2, false) ? 0 : 1;
    if (strcmp(argv[1], "cut-clipboard") == 0)
        return clipboard_set_paths((const char *const *)&argv[2], (size_t)argc - 2, true) ? 0 : 1;
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
