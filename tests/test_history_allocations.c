#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Inject failures only while the production bookkeeping calls are running. */
static long fail_after = -1;
static int allocation_failed;

static int should_fail(void) {
    if (fail_after < 0)
        return 0;
    if (fail_after-- == 0) {
        allocation_failed = 1;
        fail_after = -1;
        return 1;
    }
    return 0;
}

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
char *__real_strdup(const char *);

void *__wrap_malloc(size_t n) {
    return should_fail() ? NULL : __real_malloc(n);
}

void *__wrap_calloc(size_t n, size_t size) {
    return should_fail() ? NULL : __real_calloc(n, size);
}

void *__wrap_realloc(void *ptr, size_t n) {
    return should_fail() ? NULL : __real_realloc(ptr, n);
}

char *__wrap_strdup(const char *s) {
    return should_fail() ? NULL : __real_strdup(s);
}

#include "../src/core/undo.c"
#include "../src/core/utils.c"

static void test_paste_log_failures(void) {
    for (long point = 0; point < 8; ++point) {
        PasteLog log = {0};
        assert(paste_log_append(&log, PASTE_KIND_COPY, "original-source", "original-destination"));
        fail_after = point;
        allocation_failed = 0;
        bool ok = paste_log_append(&log, PASTE_KIND_COPY, "new-source", "new-destination");
        fail_after = -1;
        if (allocation_failed) {
            assert(!ok);
            assert(log.count == 1);
            assert(strcmp(log.src[0], "original-source") == 0);
            assert(strcmp(log.dst[0], "original-destination") == 0);
        } else {
            assert(ok && log.count == 2);
        }
        paste_log_free(&log);
    }
}

static void test_empty_paste_log_failures(void) {
    for (long point = 0; point < 4; ++point) {
        PasteLog log = {0};
        fail_after = point;
        assert(!paste_log_append(&log, PASTE_KIND_CUT, "a", "b"));
        fail_after = -1;
        assert(log.count == 0 && log.kind == PASTE_KIND_NONE);
        assert(!log.src && !log.dst);
        paste_log_free(&log);
    }
}

static void test_clone_failures(void) {
    for (long point = 0; point < 5; ++point) {
        UndoState state;
        undo_state_init(&state);
        assert(undo_state_set_single(&state, UNDO_OP_COPY, "source", "destination"));
        assert(undo_op_clone(&state.undo, &state.redo));
        fail_after = point;
        allocation_failed = 0;
        bool ok = undo_op_clone(&state.undo, &state.redo);
        fail_after = -1;
        assert(ok == !allocation_failed);
        assert(state.redo.count == 1);
        assert(strcmp(state.redo.items[0].src, "source") == 0);
        assert(strcmp(state.redo.items[0].dst, "destination") == 0);
        assert(undo_op_clone(&state.undo, &state.undo));
        assert(strcmp(state.undo.items[0].src, "source") == 0);
        undo_state_clear(&state);
    }
}

static void test_undo_history_failures(void) {
    for (int multi = 0; multi < 2; ++multi) {
        for (long point = 0; point < 20; ++point) {
            UndoState state;
            undo_state_init(&state);
            assert(undo_state_set_single(&state, UNDO_OP_COPY, "old-source", "old-destination"));
            assert(undo_op_clone(&state.undo, &state.redo));
            const char *sources[] = {"a", "b"};
            const char *destinations[] = {"c", "d"};
            fail_after = point;
            allocation_failed = 0;
            bool ok = multi ? undo_state_set_multi(&state, UNDO_OP_MOVE, sources, destinations, 2)
                            : undo_state_set_single(&state, UNDO_OP_MOVE, "a", "c");
            fail_after = -1;
            if (allocation_failed) {
                assert(!ok);
                assert(state.undo.count == 1 && state.redo.count == 1);
                assert(state.undo.kind == UNDO_OP_COPY && state.redo.kind == UNDO_OP_COPY);
                assert(strcmp(state.undo.items[0].src, "old-source") == 0);
                assert(strcmp(state.undo.items[0].dst, "old-destination") == 0);
                assert(strcmp(state.redo.items[0].src, "old-source") == 0);
            } else {
                assert(ok && state.undo.count == (multi ? 2u : 1u));
                assert(state.redo.count == 0);
            }
            undo_state_clear(&state);
        }
    }
}

int main(void) {
    test_paste_log_failures();
    test_empty_paste_log_failures();
    test_clone_failures();
    test_undo_history_failures();
    puts("History allocation failure checks passed");
    return 0;
}
