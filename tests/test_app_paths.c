#define _GNU_SOURCE
#include "app_paths.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    char out[MAX_PATH_LENGTH];
    resolve_path_under_cwd(out, "/tmp/base", "file");
    assert(!strcmp(out, "/tmp/base/file"));
    resolve_path_under_cwd(out, "/tmp/base/", "file");
    assert(!strcmp(out, "/tmp/base/file"));
    resolve_path_under_cwd(out, "/tmp/base", "/other");
    assert(!strcmp(out, "/other"));
    strcpy(out, "file");
    resolve_path_under_cwd(out, "/tmp/base", out);
    assert(!strcmp(out, "/tmp/base/file"));
    resolve_path_under_cwd(out, NULL, "relative");
    assert(!strcmp(out, "relative"));
    char too_long[MAX_PATH_LENGTH + 10];
    memset(too_long, 'a', sizeof(too_long));
    too_long[sizeof(too_long) - 1] = 0;
    resolve_path_under_cwd(out, "/tmp/base", too_long);
    assert(!*out && errno == ENAMETOOLONG);
    too_long[0] = '/';
    resolve_path_under_cwd(out, "/tmp/base", too_long);
    assert(!*out && errno == ENAMETOOLONG);
    resolve_path_under_cwd(out, "/tmp/base", "");
    assert(!*out);
    char root[] = "/tmp/cupidfm-startup-XXXXXX";
    assert(mkdtemp(root));
    char file[MAX_PATH_LENGTH], child[MAX_PATH_LENGTH], link[MAX_PATH_LENGTH];
    snprintf(file, sizeof(file), "%s/file\nname", root);
    snprintf(child, sizeof(child), "%s/child", root);
    snprintf(link, sizeof(link), "%s/link", root);
    FILE *stream = fopen(file, "w");
    assert(stream && !fclose(stream));
    assert(!mkdir(child, 0700));
    AppStartupEntry entry;
    assert(app_startup_entry(&entry, root, "file\nname"));
    assert(!strcmp(entry.directory, root) && !strcmp(entry.name, "file\nname"));
    assert(app_startup_entry(&entry, root, "child/"));
    assert(!strcmp(entry.directory, child) && !*entry.name);
    assert(!symlink(child, link));
    assert(app_startup_entry(&entry, root, "link"));
    assert(!strcmp(entry.directory, child) && !*entry.name);
    assert(!unlink(link) && !symlink("missing", link));
    assert(app_startup_entry(&entry, root, "link"));
    assert(!strcmp(entry.directory, root) && !strcmp(entry.name, "link"));
    assert(!app_startup_entry(&entry, root, "missing") && errno == ENOENT);
    assert(!app_startup_entry(&entry, root, "") && errno == EINVAL);
    assert(!app_startup_entry(&entry, root, too_long) && errno == ENAMETOOLONG);
    assert(!unlink(file) && !unlink(link) && !rmdir(child) && !rmdir(root));
    puts("Startup directories, literal files, symlinks, missing entries, and path limits passed");
    puts("Path resolution, trailing separators, aliasing, and rejection without truncation passed");
}
