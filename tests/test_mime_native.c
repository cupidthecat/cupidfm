#define _POSIX_C_SOURCE 200809L
#include "mime.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int magic_calls;

const char *__wrap_magic_file(magic_t cookie, const char *path) {
    (void)cookie;
    (void)path;
    magic_calls++;
    return "application/example+xml";
}

static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    assert(file && fputs(text, file) >= 0 && !fclose(file));
    mime_cache_clear();
}

int main(void) {
    char root[] = "/tmp/cupidfm-native-mime-XXXXXX";
    assert(mkdtemp(root));
    char path[512], symlink_path[512], fifo[512], type[512];
    snprintf(path, sizeof(path), "%s/misleading.txt", root);
    snprintf(symlink_path, sizeof(symlink_path), "%s/link", root);
    snprintf(fifo, sizeof(fifo), "%s/pipe", root);
    static const unsigned char gif[] = {'G', 'I', 'F', '8', '9', 'a', 1,   0,    1, 0, 0x80, 0,
                                        0,   0,   0,   0,   255, 255, 255, 0x2c, 0, 0, 0,    0,
                                        1,   0,   1,   0,   0,   2,   2,   0x44, 1, 0, 0x3b};
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(gif, 1, sizeof(gif), file) == sizeof(gif));
    assert(!fclose(file));
    assert(mime_type_cached(NULL, path, MAGIC_MIME_TYPE, type, sizeof(type)));
    assert(!strcmp(type, "image/gif"));
    assert(!symlink(path, symlink_path));
    assert(!mime_type_cached(NULL, symlink_path, MAGIC_MIME_TYPE, type, sizeof(type)));
    assert(
        mime_type_cached(NULL, symlink_path, MAGIC_MIME_TYPE | MAGIC_SYMLINK, type, sizeof(type)));
    assert(!strcmp(type, "image/gif"));
    assert(!mkfifo(fifo, 0600));
    assert(!mime_type_cached(NULL, fifo, MAGIC_MIME_TYPE, type, sizeof(type)));
    assert(!mime_type_cached(NULL, root, MAGIC_MIME_TYPE, type, sizeof(type)));
    char tiny[2];
    assert(!mime_type_cached(NULL, path, MAGIC_MIME_TYPE, tiny, sizeof(tiny)));
    assert(is_supported_mime_type("text/x-made-up"));
    assert(!is_supported_mime_type("application/json-invalid"));
    assert(!strcmp(get_file_emoji("text/plain", "example.py"), "🐍"));
    magic_t cookie = magic_open(MAGIC_MIME_TYPE);
    assert(cookie);
    write_text(path, "<?xml version='1.0'?><!-- prolog --><svg "
                     "xmlns='http://www.w3.org/2000/svg'/>");
    assert(mime_type_cached(cookie, path, MAGIC_MIME_TYPE, type, sizeof(type)));
    assert(!strcmp(type, "image/svg+xml") && magic_calls == 0);
    write_text(path, "<html xmlns='http://www.w3.org/1999/xhtml'/>");
    assert(mime_type_cached(cookie, path, MAGIC_MIME_TYPE, type, sizeof(type)));
    assert(!strcmp(type, "application/xhtml+xml") && magic_calls == 0);
    assert(is_supported_mime_type(type));
    write_text(path, "<?xml version='1.0'?><document xmlns='urn:example'/>");
    assert(mime_type_cached(cookie, path, MAGIC_MIME_TYPE, type, sizeof(type)));
    assert(!strcmp(type, "application/example+xml") && magic_calls == 1);
    assert(is_supported_mime_type(type));
    assert(!is_supported_mime_type("application/example+xml-invalid"));
    magic_close(cookie);
    assert(!unlink(path));
    assert(!mime_type_cached(NULL, path, MAGIC_MIME_TYPE, type, sizeof(type)));
    assert(!unlink(symlink_path) && !unlink(fifo) && !rmdir(root));
    mime_cache_clear();
    puts("CupidMIME detection, XML subtype refinement, symlinks, nonblocking special files, and "
         "MIME patterns passed");
    return 0;
}
