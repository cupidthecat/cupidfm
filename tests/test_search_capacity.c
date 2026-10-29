#define _POSIX_C_SOURCE 200809L
#include "search.h"
#include "files.h"
#include "vecstack.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int allocations_until_failure = -1;
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);
void *__wrap_malloc(size_t size) {
    if (allocations_until_failure == 0) return NULL;
    if (allocations_until_failure > 0) allocations_until_failure--;
    return __real_malloc(size);
}
void *__wrap_realloc(void *pointer, size_t size) {
    if (allocations_until_failure == 0) return NULL;
    if (allocations_until_failure > 0) allocations_until_failure--;
    return __real_realloc(pointer, size);
}
struct FileAttributes { char *name; };
const char *FileAttr_get_name(FileAttr file) { return file->name; }

int main(void) {
    AppState state = {0};
    state.files = Vector_new(1);
    state.search_files = Vector_new(1);
    for (size_t i = 0; i < 257; i++) {
        FileAttr file = malloc(sizeof(*file)); assert(file);
        char name[32]; snprintf(name, sizeof(name), "file-%03zu", i);
        file->name = strdup(name); assert(file->name);
        assert(Vector_add(&state.files, 1));
        size_t length = Vector_len(state.files);
        state.files.el[length] = file;
        Vector_set_len_no_free(&state.files, length + 1);
    }
    for (int mode = SEARCH_MODE_FUZZY; mode <= SEARCH_MODE_REGEX; mode++) {
        state.search_mode = mode;
        assert(search_rebuild(&state, mode == SEARCH_MODE_REGEX ? "^file-" : "file") == 257);
        assert(Vector_len(state.search_files) == 257);
        for (size_t i = 0; i < 257; i++) assert(state.search_files.el[i] == state.files.el[i]);
        assert(state.search_files.el[257] == NULL);
        assert(search_rebuild(&state, "no-such-entry") == 0);
    }
    /* Fail both the result-buffer allocation and the fuzzy hit allocation. */
    Vector_set_len_no_free(&state.search_files, 0); Vector_bye(&state.search_files);
    for (int mode = SEARCH_MODE_FUZZY; mode <= SEARCH_MODE_REGEX; mode++) {
        for (int fail = 0; fail <= (mode == SEARCH_MODE_FUZZY ? 1 : 0); fail++) {
            state.search_files = Vector_new(1); state.search_mode = mode;
            allocations_until_failure = fail;
            assert(search_rebuild(&state, "file") == 0);
            allocations_until_failure = -1;
            assert(Vector_len(state.search_files) == 0);
            assert(state.search_files.el[0] == NULL);
            Vector_bye(&state.search_files);
        }
    }
    for (size_t i = 0; i < Vector_len(state.files); i++) {
        FileAttr file = state.files.el[i]; free(file->name); free(file);
    }
    Vector_set_len_no_free(&state.files, 0); Vector_bye(&state.files);

    Vector vector = Vector_new(1);
    void **original = vector.el;
    allocations_until_failure = 0;
    assert(!Vector_add(&vector, 100));
    allocations_until_failure = -1;
    assert(vector.el == original && Vector_len(vector) == 0 && vector.el[0] == NULL);
    errno = 0; assert(!Vector_add(&vector, SIZE_MAX)); assert(errno == EOVERFLOW);
    assert(vector.el == original); Vector_bye(&vector);
    errno = 0; vector = Vector_new(SIZE_MAX); assert(!vector.el && errno == EOVERFLOW);
    allocations_until_failure = 0; vector = Vector_new(1); allocations_until_failure = -1;
    assert(!vector.el); assert(Vector_add(&vector, 1)); assert(vector.el[0] == NULL); Vector_bye(&vector);

    VecStack stack = VecStack_empty();
    for (int i = 0; i < 10; i++) { void *item = malloc(1); assert(item && VecStack_push(&stack, item)); }
    void *item = malloc(1); assert(item);
    allocations_until_failure = 0; assert(!VecStack_push(&stack, item)); allocations_until_failure = -1;
    assert(Vector_len(stack.v) == 10); free(item); VecStack_bye(&stack);
    puts("Search capacity, borrowed results, overflow, and allocation failures passed");
    return 0;
}
