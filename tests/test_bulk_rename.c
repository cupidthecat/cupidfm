#define _GNU_SOURCE
#include "bulk_rename.h"

#include <assert.h>
#include <errno.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void write_file(const char *path, const char *text) {
    assert(g_file_set_contents(path, text, -1, NULL));
}

static void contents(const char *path, const char *expected) {
    char *text = NULL;
    assert(g_file_get_contents(path, &text, NULL, NULL));
    assert(!strcmp(text, expected));
    g_free(text);
}

static bool read_names(BulkRenamePlan *plan, const char *names, char **error) {
    FILE *file = fmemopen((char *)names, strlen(names), "r");
    assert(file);
    bool ok = bulk_rename_read(plan, file, error);
    fclose(file);
    return ok;
}

int main(void) {
    char root[] = "/tmp/cupidfm-bulk-XXXXXX";
    assert(mkdtemp(root));
    char *a = g_build_filename(root, "a", NULL), *b = g_build_filename(root, "b", NULL);
    char *c = g_build_filename(root, "c", NULL), *d = g_build_filename(root, "d", NULL);
    const char *paths[] = {a, b, c};
    write_file(a, "one");
    write_file(b, "two");
    write_file(c, "three");
    BulkRenamePlan plan;
    char *error = NULL;
    assert(bulk_rename_prepare(&plan, paths, 3, &error));
    FILE *list = tmpfile();
    assert(list && bulk_rename_write(&plan, list, &error));
    rewind(list);
    char text[16];
    assert(fgets(text, sizeof(text), list) && !strcmp(text, "a\n"));
    fclose(list);
    assert(read_names(&plan, "b\nc\nd\n", &error));
    assert(plan.changed == 3);
    bulk_rename_apply(&plan);
    assert(plan.succeeded == 3 && !plan.failed);
    assert(access(a, F_OK));
    contents(b, "one");
    contents(c, "two");
    contents(d, "three");
    bulk_rename_free(&plan);
    const char *cycle[] = {b, c};
    assert(bulk_rename_prepare(&plan, cycle, 2, &error));
    assert(read_names(&plan, "c\nb\n", &error));
    bulk_rename_apply(&plan);
    assert(plan.failed == 2 && !plan.succeeded);
    contents(b, "one");
    contents(c, "two");
    bulk_rename_free(&plan);
    const char *invalid[] = {"d\nx\n", "same\nsame\n", "\nx\n", "x\n", "x\ny\nz\n"};
    for (size_t i = 0; i < G_N_ELEMENTS(invalid); i++) {
        assert(bulk_rename_prepare(&plan, cycle, 2, &error));
        assert(!read_names(&plan, invalid[i], &error) && error);
        g_clear_pointer(&error, g_free);
        contents(b, "one");
        contents(c, "two");
        bulk_rename_free(&plan);
    }
    assert(bulk_rename_prepare(&plan, cycle, 2, &error));
    assert(read_names(&plan, "safe one\nsafe two", &error));
    /* Keep the original inode alive elsewhere so replacement cannot reuse it. */
    assert(!rename(b, a));
    write_file(b, "replacement");
    bulk_rename_apply(&plan);
    assert(plan.failed == 1 && plan.succeeded == 1);
    contents(a, "one");
    contents(b, "replacement");
    char *safe = g_build_filename(root, "safe two", NULL);
    contents(safe, "two");
    bulk_rename_free(&plan);
    char *sub = g_build_filename(root, "sub", NULL), *child = g_build_filename(sub, "child", NULL);
    assert(!mkdir(sub, 0700));
    write_file(child, "child");
    const char *overlap[] = {sub, child};
    assert(!bulk_rename_prepare(&plan, overlap, 2, &error));
    g_clear_pointer(&error, g_free);
    char *break_name = g_build_filename(root, "line\nbreak", NULL);
    write_file(break_name, "line");
    const char *broken[] = {break_name};
    assert(!bulk_rename_prepare(&plan, broken, 1, &error));
    g_clear_pointer(&error, g_free);
    assert(!unlink(break_name) && !unlink(child) && !rmdir(sub));
    assert(!unlink(a) && !unlink(b) && !unlink(d) && !unlink(safe) && !rmdir(root));
    g_free(a);
    g_free(b);
    g_free(c);
    g_free(d);
    g_free(safe);
    g_free(sub);
    g_free(child);
    g_free(break_name);
    puts("Bulk rename chains, cycles, conflicts, list validation, and source replacement passed");
}
