#define _GNU_SOURCE
#include "clipboard.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static void packet(const void *text, size_t length, const char *base64, bool tmux) {
    char *output = NULL;
    size_t size = 0;
    FILE *stream = open_memstream(&output, &size);
    assert(stream);
    assert(clipboard_write_osc52(stream, text, length, tmux));
    assert(!fclose(stream));
    const char *head = tmux ? "\033Ptmux;\033\033]52;c;" : "\033]52;c;";
    const char *tail = tmux ? "\033\033\\\033\\" : "\033\\";
    assert(size == strlen(head) + strlen(base64) + strlen(tail));
    assert(!memcmp(output, head, strlen(head)));
    assert(!memcmp(output + strlen(head), base64, strlen(base64)));
    assert(!strcmp(output + strlen(head) + strlen(base64), tail));
    free(output);
}

int main(void) {
    packet(NULL, 0, "", false);
    packet("f", 1, "Zg==", false);
    packet("fo", 2, "Zm8=", false);
    packet("foo", 3, "Zm9v", false);
    packet("é", 2, "w6k=", false);
    const unsigned char bytes[] = {10, 27, 0};
    packet(bytes, sizeof(bytes), "ChsA", true);
    FILE *stream = tmpfile();
    assert(stream);
    assert(!clipboard_write_osc52(stream, NULL, 1, false));
    assert(!clipboard_write_osc52(stream, "", 1024 * 1024 + 1, false));
    fclose(stream);
    unsetenv("DISPLAY");
    unsetenv("WAYLAND_DISPLAY");
    char root[] = "/tmp/cupidfm-clipboard-XXXXXX";
    assert(mkdtemp(root));
    char pipe[512];
    snprintf(pipe, sizeof(pipe), "%s/pipe", root);
    assert(!mkfifo(pipe, 0600));
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    assert(!clipboard_set_from_file(pipe));
    clock_gettime(CLOCK_MONOTONIC, &end);
    assert(end.tv_sec - start.tv_sec < 2);
    assert(!unlink(pipe) && !rmdir(root));
    puts("OSC 52 padding, Unicode/binary payloads, tmux framing, bounds, and nonblocking FIFO "
         "rejection passed");
}
