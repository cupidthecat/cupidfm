// undo.c
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "undo.h"
#include "process.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>

static void undo_op_clear(UndoOp *op) {
    if (!op) return;
    for (size_t i = 0; i < op->count; i++) {
        free(op->items[i].src);
        free(op->items[i].dst);
    }
    free(op->items);
    op->items = NULL;
    op->count = 0;
    op->kind = UNDO_OP_NONE;
}

static bool undo_op_clone(const UndoOp *src, UndoOp *dst) {
    if (!dst) return false;
    UndoOp next = {0};
    if (src && src->kind != UNDO_OP_NONE && src->count != 0) {
        next.items = calloc(src->count, sizeof(UndoItem));
        if (!next.items) return false;
        next.count = src->count;
        next.kind = src->kind;
        for (size_t i = 0; i < src->count; i++) {
            next.items[i].src = src->items[i].src ? strdup(src->items[i].src) : NULL;
            next.items[i].dst = src->items[i].dst ? strdup(src->items[i].dst) : NULL;
            if ((src->items[i].src && !next.items[i].src) ||
                (src->items[i].dst && !next.items[i].dst)) {
                undo_op_clear(&next);
                return false;
            }
        }
    }
    undo_op_clear(dst);
    *dst = next;
    return true;
}

static bool ensure_parent_dir(const char *path) {
    if (!path || !*path) return false;
    char tmp[1024];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) return true;
    *slash = '\0';
    if (mkdir(tmp, 0700) == 0) return true;
    return (errno == EEXIST);
}

static bool move_path(const char *src, const char *dst, char *err, size_t err_len) {
    if (!src || !*src || !dst || !*dst) {
        if (err && err_len) snprintf(err, err_len, "Invalid move paths");
        return false;
    }
    if (strcmp(src, dst) == 0) return true;
    struct stat destination_stat;
    if (lstat(dst, &destination_stat) == 0 || errno != ENOENT) {
        if (err && err_len) snprintf(err, err_len, "Destination already exists");
        return false;
    }
    (void)ensure_parent_dir(dst);
    if (rename(src, dst) == 0) return true;
    // Cross-device moves or other cases: fall back to mv.
    if (!file_command_move(src, dst)) {
        if (err && err_len) snprintf(err, err_len, "Move failed");
        return false;
    }
    return true;
}

static bool remove_path_any(const char *path, char *err, size_t err_len) {
    if (!path || !*path) {
        if (err && err_len) snprintf(err, err_len, "Invalid path");
        return false;
    }
    struct stat st;
    if (lstat(path, &st) != 0) {
        if (errno == ENOENT) return true; // already gone
        if (err && err_len) snprintf(err, err_len, "Stat failed: %s", strerror(errno));
        return false;
    }
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        if (!file_command_remove_directory(path)) {
            if (err && err_len) snprintf(err, err_len, "Remove directory failed");
            return false;
        }
        return true;
    }
    if (unlink(path) != 0) {
        if (errno == ENOENT) return true;
        if (err && err_len) snprintf(err, err_len, "Remove failed: %s", strerror(errno));
        return false;
    }
    return true;
}

static bool copy_path(const char *src, const char *dst, char *err, size_t err_len) {
    if (!src || !*src || !dst || !*dst) {
        if (err && err_len) snprintf(err, err_len, "Invalid copy paths");
        return false;
    }
    struct stat destination_stat;
    if (lstat(dst, &destination_stat) == 0 || errno != ENOENT) {
        if (err && err_len) snprintf(err, err_len, "Destination already exists");
        return false;
    }
    struct stat st;
    if (lstat(src, &st) != 0) {
        if (err && err_len) snprintf(err, err_len, "Copy source missing");
        return false;
    }
    (void)ensure_parent_dir(dst);
    if (!file_command_copy(src, dst, S_ISDIR(st.st_mode))) {
        if (err && err_len) snprintf(err, err_len, "Copy failed");
        return false;
    }
    return true;
}

static bool create_empty_file(const char *path, char *err, size_t err_len) {
    if (!path || !*path) return false;
    (void)ensure_parent_dir(path);
    FILE *f = fopen(path, "wx");
    if (!f) {
        if (errno == EEXIST) return true;
        if (err && err_len) snprintf(err, err_len, "Create file failed: %s", strerror(errno));
        return false;
    }
    fclose(f);
    return true;
}

static bool create_dir(const char *path, char *err, size_t err_len) {
    if (!path || !*path) return false;
    (void)ensure_parent_dir(path);
    if (mkdir(path, 0755) == 0) return true;
    if (errno == EEXIST) return true;
    if (err && err_len) snprintf(err, err_len, "Create directory failed: %s", strerror(errno));
    return false;
}

static bool apply_forward(const UndoOp *op, char *err, size_t err_len) {
    if (!op || op->kind == UNDO_OP_NONE || op->count == 0) {
        if (err && err_len) snprintf(err, err_len, "Nothing to redo");
        return false;
    }

    for (size_t i = 0; i < op->count; i++) {
        const char *src = op->items[i].src;
        const char *dst = op->items[i].dst;
        bool ok = false;
        switch (op->kind) {
            case UNDO_OP_CREATE_FILE:
                ok = create_empty_file(dst, err, err_len);
                break;
            case UNDO_OP_CREATE_DIR:
                ok = create_dir(dst, err, err_len);
                break;
            case UNDO_OP_RENAME:
            case UNDO_OP_MOVE:
            case UNDO_OP_DELETE_TO_TRASH:
                ok = move_path(src, dst, err, err_len);
                break;
            case UNDO_OP_COPY:
                ok = copy_path(src, dst, err, err_len);
                break;
            default:
                if (err && err_len) snprintf(err, err_len, "Unsupported operation");
                return false;
        }
        if (!ok) return false;
    }
    return true;
}

static bool apply_inverse(const UndoOp *op, char *err, size_t err_len) {
    if (!op || op->kind == UNDO_OP_NONE || op->count == 0) {
        if (err && err_len) snprintf(err, err_len, "Nothing to undo");
        return false;
    }

    for (size_t i = 0; i < op->count; i++) {
        const char *src = op->items[i].src;
        const char *dst = op->items[i].dst;
        bool ok = false;
        switch (op->kind) {
            case UNDO_OP_CREATE_FILE:
            case UNDO_OP_CREATE_DIR:
                ok = remove_path_any(dst, err, err_len);
                break;
            case UNDO_OP_RENAME:
            case UNDO_OP_MOVE:
            case UNDO_OP_DELETE_TO_TRASH:
                ok = move_path(dst, src, err, err_len);
                break;
            case UNDO_OP_COPY:
                ok = remove_path_any(dst, err, err_len);
                break;
            default:
                if (err && err_len) snprintf(err, err_len, "Unsupported operation");
                return false;
        }
        if (!ok) return false;
    }
    return true;
}

void undo_state_init(UndoState *st) {
    if (!st) return;
    st->undo.kind = UNDO_OP_NONE;
    st->undo.count = 0;
    st->undo.items = NULL;
    st->redo.kind = UNDO_OP_NONE;
    st->redo.count = 0;
    st->redo.items = NULL;
}

void undo_state_clear(UndoState *st) {
    if (!st) return;
    undo_op_clear(&st->undo);
    undo_op_clear(&st->redo);
}

static bool undo_state_set_from_op(UndoState *st, const UndoOp *op) {
    if (!st) return false;
    UndoOp next = {0};
    if (!undo_op_clone(op, &next)) return false;
    undo_op_clear(&st->redo);
    undo_op_clear(&st->undo);
    st->undo = next;
    return true;
}

bool undo_state_set_owned(UndoState *st, UndoOpKind kind, UndoItem *items, size_t n) {
    if (!st || !items || n == 0) return false;
    // Any new operation clears redo history.
    undo_op_clear(&st->redo);
    undo_op_clear(&st->undo);
    st->undo.kind = kind;
    st->undo.items = items;
    st->undo.count = n;
    return true;
}

bool undo_state_set_single(UndoState *st, UndoOpKind kind, const char *src, const char *dst) {
    UndoOp op = {0};
    op.kind = kind;
    op.count = 1;
    op.items = (UndoItem *)calloc(1, sizeof(UndoItem));
    if (!op.items) return false;
    op.items[0].src = src ? strdup(src) : NULL;
    op.items[0].dst = dst ? strdup(dst) : NULL;
    if ((src && !op.items[0].src) || (dst && !op.items[0].dst)) {
        undo_op_clear(&op);
        return false;
    }
    bool ok = undo_state_set_from_op(st, &op);
    undo_op_clear(&op);
    return ok;
}

bool undo_state_set_multi(UndoState *st, UndoOpKind kind, const char *const *src, const char *const *dst, size_t n) {
    if (!st || n == 0) return false;
    UndoOp op = {0};
    op.kind = kind;
    op.count = n;
    op.items = (UndoItem *)calloc(n, sizeof(UndoItem));
    if (!op.items) return false;
    for (size_t i = 0; i < n; i++) {
        op.items[i].src = src && src[i] ? strdup(src[i]) : NULL;
        op.items[i].dst = dst && dst[i] ? strdup(dst[i]) : NULL;
        if ((src && src[i] && !op.items[i].src) ||
            (dst && dst[i] && !op.items[i].dst)) {
            undo_op_clear(&op);
            return false;
        }
    }
    bool ok = undo_state_set_from_op(st, &op);
    undo_op_clear(&op);
    return ok;
}

bool undo_state_do_undo(UndoState *st, char *err, size_t err_len) {
    if (!st || st->undo.kind == UNDO_OP_NONE || st->undo.count == 0) {
        if (err && err_len) snprintf(err, err_len, "Nothing to undo");
        return false;
    }
    if (!apply_inverse(&st->undo, err, err_len)) return false;
    // Move undo -> redo (ownership transfer) and clear undo.
    undo_op_clear(&st->redo);
    st->redo = st->undo;
    st->undo.kind = UNDO_OP_NONE;
    st->undo.count = 0;
    st->undo.items = NULL;
    return true;
}

bool undo_state_do_redo(UndoState *st, char *err, size_t err_len) {
    if (!st || st->redo.kind == UNDO_OP_NONE || st->redo.count == 0) {
        if (err && err_len) snprintf(err, err_len, "Nothing to redo");
        return false;
    }
    if (!apply_forward(&st->redo, err, err_len)) return false;
    // Move redo -> undo (ownership transfer) and clear redo.
    undo_op_clear(&st->undo);
    st->undo = st->redo;
    st->redo.kind = UNDO_OP_NONE;
    st->redo.count = 0;
    st->redo.items = NULL;
    return true;
}
