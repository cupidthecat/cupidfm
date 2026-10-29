#define _POSIX_C_SOURCE 200809L
#include "files.h"
#include "git.h"
#include "vector.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int until_failure = -1;
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);
char *__real_strdup(const char *);
static bool fail(void) {if (until_failure == 0) return true; if (until_failure > 0) until_failure--; return false;}
void *__wrap_malloc(size_t size) {return fail() ? NULL : __real_malloc(size);}
void *__wrap_realloc(void *pointer, size_t size) {return fail() ? NULL : __real_realloc(pointer, size);}
char *__wrap_strdup(const char *text) {return fail() ? NULL : __real_strdup(text);}
GitStatus git_query_file_status(const char *path) {(void)path; return GIT_STATUS_ERROR;}
static void release(Vector *vector) {
    for (size_t i = 0; i < Vector_len(*vector); i++) free_attr(vector->el[i]);
    Vector_set_len_no_free(vector, 0); Vector_bye(vector);
}
int main(void) {
    char directory[] = "/tmp/cupidfm-directory-XXXXXX"; assert(mkdtemp(directory));
    char path[512]; snprintf(path, sizeof(path), "%s/dir", directory); assert(mkdir(path, 0755) == 0);
    snprintf(path, sizeof(path), "%s/file", directory); FILE *file = fopen(path, "w"); assert(file); fclose(file);
    snprintf(path, sizeof(path), "%s/link", directory); assert(symlink("dir", path) == 0);
    snprintf(path, sizeof(path), "%s/broken", directory); assert(symlink("missing", path) == 0);
    for (int lazy = 0; lazy < 2; lazy++) {
        Vector vector = Vector_new(0); size_t loaded = 0;
        if (lazy) append_files_to_vec_lazy(&vector, directory, 10, &loaded);
        else append_files_to_vec(&vector, directory);
        assert(Vector_len(vector) == 4);
        for (size_t i = 0; i < Vector_len(vector); i++) {
            FileAttr entry = vector.el[i]; const char *name = FileAttr_get_name(entry);
            assert(FileAttr_is_dir(entry) == (!strcmp(name, "dir") || !strcmp(name, "link")));
        }
        release(&vector);
        for (int allocation = 0; allocation < 16; allocation++) {
            vector = Vector_new(0); loaded = 0;
            until_failure = allocation;
            if (lazy) append_files_to_vec_lazy(&vector, directory, 10, &loaded);
            else append_files_to_vec(&vector, directory);
            until_failure = -1;
            assert(Vector_len(vector) <= 4);
            assert(vector.el[Vector_len(vector)] == NULL);
            if (lazy) assert(loaded == Vector_len(vector));
            release(&vector);
        }
    }
    snprintf(path, sizeof(path), "%s/link", directory); unlink(path);
    snprintf(path, sizeof(path), "%s/broken", directory); unlink(path);
    snprintf(path, sizeof(path), "%s/file", directory); unlink(path);
    snprintf(path, sizeof(path), "%s/dir", directory); rmdir(path); rmdir(directory);
    puts("Directory symlinks and eager/lazy allocation failures passed");
    return 0;
}
