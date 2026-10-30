#define _GNU_SOURCE
#include "../src/ui/browser_ui.c"
#include <assert.h>
#include <fcntl.h>

static int loads, kitty_encodes, sixel_encodes;
int __real_cupidimage_load_image_file(const char *, cupidimage_image *, char *, size_t);

int __wrap_cupidimage_load_image_file(const char *path, cupidimage_image *out, char *error,
                                      size_t size) {
    loads++;
    return __real_cupidimage_load_image_file(path, out, error, size);
}

int __real_cupidimage_render_kitty_with_options(const cupidimage_image *, FILE *, uint32_t,
                                                uint32_t, const cupidimage_kitty_options *, char *,
                                                size_t);

int __wrap_cupidimage_render_kitty_with_options(const cupidimage_image *image, FILE *stream,
                                                uint32_t columns, uint32_t rows,
                                                const cupidimage_kitty_options *options,
                                                char *error, size_t size) {
    assert(options->image_id == CUPIDFM_PREVIEW_IMAGE_ID);
    kitty_encodes++;
    return __real_cupidimage_render_kitty_with_options(image, stream, columns, rows, options, error,
                                                       size);
}

int __real_cupidimage_render_sixel_with_options(const cupidimage_image *, FILE *, uint32_t,
                                                uint32_t, const cupidimage_sixel_options *, char *,
                                                size_t);

int __wrap_cupidimage_render_sixel_with_options(const cupidimage_image *image, FILE *stream,
                                                uint32_t columns, uint32_t rows,
                                                const cupidimage_sixel_options *options,
                                                char *error, size_t size) {
    sixel_encodes++;
    return __real_cupidimage_render_sixel_with_options(image, stream, columns, rows, options, error,
                                                       size);
}

static void put32(unsigned char *p, uint32_t v) {
    for (int i = 0; i < 4; i++)
        p[i] = (unsigned char)(v >> (8 * i));
}

static void bitmap(const char *path, unsigned char color) {
    unsigned char header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    put32(header + 2, 54 + 128 * 96 * 3);
    put32(header + 10, 54);
    put32(header + 14, 40);
    put32(header + 18, 128);
    put32(header + 22, 96);
    header[26] = 1;
    header[28] = 24;
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fwrite(header, 1, sizeof(header), file) == sizeof(header));
    unsigned char row[128 * 3];
    for (size_t i = 0; i < sizeof(row); i++)
        row[i] = (unsigned char)(color + i);
    for (int i = 0; i < 96; i++)
        assert(fwrite(row, 1, sizeof(row), file) == sizeof(row));
    assert(!fclose(file));
}

static long wire_size(FILE *wire) {
    assert(!fflush(stdout));
    return ftell(wire);
}

int main(void) {
    char directory[] = "/tmp/cupidfm-image-ui-XXXXXX";
    assert(mkdtemp(directory));
    char first[256], second[256];
    snprintf(first, sizeof(first), "%s/a.bmp", directory);
    snprintf(second, sizeof(second), "%s/b.bmp", directory);
    bitmap(first, 0);
    bitmap(second, 53);
    struct timespec settle = {2, 100000000};
    nanosleep(&settle, NULL);
    FILE *input = tmpfile(), *output = tmpfile(), *wire = tmpfile();
    assert(input && output && wire);
    SCREEN *screen = newterm("xterm-256color", output, input);
    assert(screen);
    set_term(screen);
    start_color();
    assert(resizeterm(60, 140) != ERR);
    WINDOW *window = newwin(30, 100, 0, 0);
    assert(window);
    int saved = dup(STDOUT_FILENO);
    assert(saved >= 0);
    assert(!fflush(stdout));
    assert(dup2(fileno(wire), STDOUT_FILENO) >= 0);
    draw_image_preview_kitty(window, first, 0, 30, 100);
    assert(loads == 1 && kitty_encodes == 1);
    long written = wire_size(wire);
    draw_image_preview_kitty(window, first, 0, 30, 100);
    assert(wire_size(wire) == written && loads == 1 && kitty_encodes == 1);
    assert(wresize(window, 40, 120) != ERR);
    draw_image_preview_kitty(window, first, 0, 40, 120);
    assert(loads == 2 && kitty_encodes == 2 && wire_size(wire) > written);
    written = wire_size(wire);
    assert(mvwin(window, 1, 2) != ERR);
    draw_image_preview_kitty(window, first, 0, 40, 120);
    assert(loads == 2 && kitty_encodes == 2 && wire_size(wire) > written);
    written = wire_size(wire);
    invalidate_image_preview_overlay();
    draw_image_preview_kitty(window, first, 0, 40, 120);
    assert(loads == 2 && kitty_encodes == 2 && wire_size(wire) > written);
    draw_image_preview_kitty(window, second, 0, 40, 120);
    assert(loads == 3 && kitty_encodes == 3);
    draw_image_preview_kitty(window, first, 0, 40, 120);
    assert(loads == 3 && kitty_encodes == 3);
    draw_image_preview_sixel(window, first, 0, 40, 120);
    assert(loads == 3 && sixel_encodes == 1);
    draw_image_preview_sixel(window, first, 0, 40, 120);
    assert(loads == 3 && sixel_encodes == 1);
    draw_image_preview_kitty(window, first, 0, 40, 120);
    assert(loads == 3 && kitty_encodes == 3);
    struct stat before;
    assert(!stat(first, &before));
    FILE *file = fopen(first, "r+b");
    assert(file);
    assert(!fseek(file, 54, SEEK_SET));
    assert(fputc(123, file) != EOF);
    assert(!fflush(file));
    struct timespec times[2] = {before.st_atim, before.st_mtim};
    assert(!futimens(fileno(file), times));
    assert(!fclose(file));
    draw_image_preview_kitty(window, first, 0, 40, 120);
    assert(loads == 4 && kitty_encodes == 4);
    assert(!unlink(first));
    draw_image_preview_kitty(window, first, 0, 40, 120);
    assert(!last_kitty_image_path[0] && !last_sixel_image_path[0] && !kitty_overlay.generation &&
           !sixel_overlay.generation);
    image_preview_clear_cache();
    delwin(window);
    endwin();
    delscreen(screen);
    assert(!fflush(stdout));
    assert(dup2(saved, STDOUT_FILENO) >= 0);
    close(saved);
    fclose(input);
    fclose(output);
    fclose(wire);
    assert(!unlink(second));
    assert(!rmdir(directory));
    puts("Image UI: viewport sizing, retained uploads, resize/move/repaint, backend switches, "
         "edits, and missing files passed");
    return 0;
}
