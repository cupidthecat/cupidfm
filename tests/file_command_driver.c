#include "clipboard.h"
#include "undo.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    if (strcmp(argv[1], "clipboard") == 0)
        return clipboard_set_from_file(argv[2]) ? 0 : 1;
    if (strcmp(argv[1], "paste") == 0) {
        PasteLog log = {0};
        int count = paste_from_clipboard(argv[2], NULL, &log);
        printf("%d %zu\n", count, log.count);
        paste_log_free(&log);
        return count > 0 ? 0 : 1;
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
