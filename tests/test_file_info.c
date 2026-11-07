#define _GNU_SOURCE
#include "file_info.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>

static const char *value(const FileInfo *info, const char *label) {
    for (size_t i = 0; i < info->count; i++)
        if (!strcmp(info->fields[i].label, label))
            return info->fields[i].value;
    return NULL;
}

int main(void) {
    GError *error = NULL;
    char *root = g_dir_make_tmp("cupidfm-info-XXXXXX", &error);
    assert(root && !error);
    char *path = g_build_filename(root, "file\nname.svg", NULL);
    assert(g_file_set_contents(path, "<svg xmlns='http://www.w3.org/2000/svg'/>", -1, &error));
    assert(!chmod(path, 04751));
    magic_t cookie = magic_open(MAGIC_MIME_TYPE);
    assert(cookie && !magic_load(cookie, NULL));
    FileInfo info = {0};
    file_info_read(&info, path, cookie);
    assert(!strcmp(value(&info, "Name"), "file\nname.svg"));
    assert(!strcmp(value(&info, "Path"), path));
    assert(!strcmp(value(&info, "Type"), "Regular file"));
    assert(!strcmp(value(&info, "MIME"), "image/svg+xml"));
    assert(!strcmp(value(&info, "Permissions"), "rwsr-x--x (4751)"));
    assert(value(&info, "Modified") && value(&info, "Created") && value(&info, "Owner"));
    char *hard = g_build_filename(root, "hard", NULL);
    assert(!link(path, hard));
    file_info_read(&info, hard, cookie);
    assert(!strcmp(value(&info, "Links"), "2"));
    char *link_path = g_build_filename(root, "broken", NULL);
    assert(!symlink("missing", link_path));
    file_info_read(&info, link_path, cookie);
    assert(!strcmp(value(&info, "Type"), "Symbolic link"));
    assert(!strcmp(value(&info, "Target"), "missing"));
    assert(!value(&info, "Error"));
    char *fifo = g_build_filename(root, "pipe", NULL);
    assert(!mkfifo(fifo, 0600));
    file_info_read(&info, fifo, cookie);
    assert(!strcmp(value(&info, "Type"), "FIFO"));
    assert(!value(&info, "MIME"));
    file_info_read(&info, root, cookie);
    assert(!strcmp(value(&info, "Type"), "Directory"));
    assert(value(&info, "Entry size") && !value(&info, "Target"));
    assert(!unlink(path));
    file_info_read(&info, path, cookie);
    assert(value(&info, "Error") && info.count == 3);
    file_info_read(&info, NULL, cookie);
    assert(!info.count);
    file_info_clear(&info);
    magic_close(cookie);
    assert(!unlink(hard) && !unlink(fifo) && !unlink(link_path) && !rmdir(root));
    g_free(path);
    g_free(hard);
    g_free(fifo);
    g_free(link_path);
    g_free(root);
    puts("File info: literal names, MIME, metadata, permissions, links, FIFO, deletion, and "
         "refresh passed");
}
