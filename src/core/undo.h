// undo.h
#ifndef UNDO_H
#define UNDO_H

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    UNDO_OP_NONE = 0,
    UNDO_OP_CREATE_FILE,
    UNDO_OP_CREATE_DIR,
    UNDO_OP_RENAME,
    UNDO_OP_MOVE,
    UNDO_OP_COPY,
    UNDO_OP_DELETE_TO_TRASH,
} UndoOpKind;

typedef struct {
    char *src; // meaning depends on kind (e.g., old path, source path)
    char *dst; // meaning depends on kind (e.g., new path, destination path)
} UndoItem;

typedef struct {
    UndoOpKind kind;
    size_t count;
    UndoItem *items;
} UndoOp;

typedef struct {
    UndoOp undo; // last applied operation, can be undone
    UndoOp redo; // operation that can be redone after an undo
} UndoState;

void undo_state_init(UndoState *st);
void undo_state_clear(UndoState *st);

// Replace current undo op and clear redo. Takes ownership by duplicating strings.
bool undo_state_set_single(UndoState *st, UndoOpKind kind, const char *src, const char *dst);
bool undo_state_set_multi(UndoState *st, UndoOpKind kind, const char *const *src, const char *const *dst, size_t n);
// Takes ownership of items/src/dst strings as provided (no additional strdup).
bool undo_state_set_owned(UndoState *st, UndoOpKind kind, UndoItem *items, size_t n);

// Perform undo/redo. On failure returns false and writes a message to err (if provided).
bool undo_state_do_undo(UndoState *st, char *err, size_t err_len);
bool undo_state_do_redo(UndoState *st, char *err, size_t err_len);

#endif // UNDO_H
