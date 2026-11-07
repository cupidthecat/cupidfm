#define _GNU_SOURCE
#include "openers.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void first(Openers *set, const char *path, const char *mime, const char *name) {
    GPtrArray *matched = openers_match(set, path, mime);
    assert(matched && matched->len);
    Opener *opener = g_ptr_array_index(matched, 0);
    if (strcmp(opener->name, name))
        fprintf(stderr, "%s (%s): expected %s, got %s\n", path, mime ? mime : "unknown", name,
                opener->name);
    assert(!strcmp(opener->name, name));
    g_ptr_array_free(matched, TRUE);
}

int main(void) {
    Openers *set = openers_load(NULL, NULL);
    assert(set);
    first(set, "/src/file.c", "text/x-c", "edit");
    first(set, "/src/file.sh", "application/x-sh", "edit");
    first(set, "/src/file.xhtml", "application/xhtml+xml", "edit");
    first(set, "/src/logo.svg", "image/svg+xml", "open");
    first(set, "/src/song", "audio/ogg", "play");
    first(set, "/src/", "folder/local", "edit");
    first(set, "/src/unknown", NULL, "open");
    openers_free(set);
    char filename[] = "/tmp/cupidfm-openers-XXXXXX";
    int fd = mkstemp(filename);
    assert(fd >= 0);
    close(fd);
    const char *config = "# Rules keep their first-appearance order.\n"
                         "opener.capture.command = 'record \"$@\"'\n"
                         "opener.capture.description = Capture files\n"
                         "opener.capture.block = true\n"
                         "opener.viewer.command = 'view \"$@\"'\n"
                         "opener.viewer.orphan = true\n"
                         "opener.windows.command = windows-only\n"
                         "opener.windows.for = windows\n"
                         "rule.literal.path = 'brace\\{name\\}.txt'\n"
                         "rule.literal.use = viewer\n"
                         "rule.character.path = 'brace[{]other[}].txt'\n"
                         "rule.character.use = viewer\n"
                         "rule.comma.path = 'file{a\\,b,c}.txt'\n"
                         "rule.comma.use = viewer\n"
                         "rule.special.path = '*.{foo,{bar,baz}}'\n"
                         "rule.special.mime = 'text/*'\n"
                         "rule.special.use = capture, viewer\n"
                         "rule.special.use = windows\n"
                         "rule.path.path = '/tmp/*/specific'\n"
                         "rule.path.use = viewer\n"
                         "rule.text.mime = 'application/{json,*+xml}'\n"
                         "rule.text.use = capture\n";
    assert(g_file_set_contents(filename, config, -1, NULL));
    GError *error = NULL;
    set = openers_load(filename, &error);
    assert(set && !error);
    first(set, "/src/brace{name}.txt", "text/plain", "viewer");
    first(set, "/src/brace{other}.txt", "text/plain", "viewer");
    first(set, "/src/filea,b.txt", "text/plain", "viewer");
    first(set, "/src/.odd file.foo", "text/plain", "capture");
    first(set, "/src/a.bar", "text/plain", "capture");
    first(set, "/src/a.baz", "text/plain", "capture");
    first(set, "/src/a.foo", "image/png", "open");
    first(set, "/tmp/folder/specific", "text/plain", "viewer");
    first(set, "/x/any", "application/atom+xml", "capture");
    GPtrArray *matched = openers_match(set, "/x/a.foo", "text/plain");
    assert(matched->len == 2);
    Opener *capture = g_ptr_array_index(matched, 0), *viewer = g_ptr_array_index(matched, 1);
    assert(capture->block && !capture->orphan);
    assert(viewer->orphan && !viewer->block);
    assert(!strcmp(capture->command, "record \"$@\""));
    g_ptr_array_free(matched, TRUE);
    openers_free(set);
    const char *invalid[] = {"opener.edit.block = maybe\n",
                             "rule.bad.mime = text/*\n",
                             "rule.bad.path = *\nrule.bad.use = absent\n",
                             "opener.x.for = typo\n",
                             "opener.incomplete.description = Missing command\n",
                             "opener.edit.wrong = true\n"};
    for (size_t i = 0; i < G_N_ELEMENTS(invalid); i++) {
        assert(g_file_set_contents(filename, invalid[i], -1, NULL));
        assert(!openers_load(filename, &error) && error);
        g_clear_error(&error);
    }
    unlink(filename);
    puts("Opener order, MIME/path globs, alternatives, platforms, flags, and invalid settings "
         "passed");
}
