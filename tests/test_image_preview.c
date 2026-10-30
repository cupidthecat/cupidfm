#define _GNU_SOURCE
#include "../src/ui/image_preview.c"
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

static int loads, kitty_calls, sixel_calls, fail_malloc = -1, fail_realloc = -1;
static uint32_t source_width = 64, source_height = 32;
static bool decode_failure, change_during_decode, alpha_fixture;
void *__real_malloc(size_t);
void *__real_realloc(void *, size_t);

void *__wrap_malloc(size_t size) {
    if (fail_malloc == 0) {
        fail_malloc = -1;
        return NULL;
    }
    if (fail_malloc > 0)
        fail_malloc--;
    return __real_malloc(size);
}

void *__wrap_realloc(void *pointer, size_t size) {
    if (fail_realloc == 0) {
        fail_realloc = -1;
        return NULL;
    }
    if (fail_realloc > 0)
        fail_realloc--;
    return __real_realloc(pointer, size);
}

int __wrap_cupidimage_load_image_file(const char *path, cupidimage_image *image, char *error,
                                      size_t capacity) {
    (void)error;
    (void)capacity;
    loads++;
    if (decode_failure)
        return 0;
    image->width = source_width;
    image->height = source_height;
    image->rgba = __real_malloc((size_t)source_width * source_height * 4);
    assert(image->rgba);
    for (size_t i = 0; i < (size_t)source_width * source_height; i++) {
        image->rgba[i * 4] = 255;
        image->rgba[i * 4 + 1] = image->rgba[i * 4 + 2] = 0;
        image->rgba[i * 4 + 3] = 255;
        if (alpha_fixture && (i == 2 || i == 3)) {
            image->rgba[i * 4] = 0;
            image->rgba[i * 4 + 2] = 255;
            image->rgba[i * 4 + 3] = 0;
        }
    }
    if (change_during_decode) {
        int fd = open(path, O_WRONLY | O_APPEND);
        assert(fd >= 0);
        assert(write(fd, "x", 1) == 1);
        close(fd);
    }
    return 1;
}

int __real_cupidimage_render_kitty_with_options(const cupidimage_image *, FILE *, uint32_t,
                                                uint32_t, const cupidimage_kitty_options *, char *,
                                                size_t);

int __wrap_cupidimage_render_kitty_with_options(const cupidimage_image *image, FILE *out,
                                                uint32_t width, uint32_t height,
                                                const cupidimage_kitty_options *options,
                                                char *error, size_t capacity) {
    kitty_calls++;
    return __real_cupidimage_render_kitty_with_options(image, out, width, height, options, error,
                                                       capacity);
}

int __real_cupidimage_render_sixel_with_options(const cupidimage_image *, FILE *, uint32_t,
                                                uint32_t, const cupidimage_sixel_options *, char *,
                                                size_t);

int __wrap_cupidimage_render_sixel_with_options(const cupidimage_image *image, FILE *out,
                                                uint32_t width, uint32_t height,
                                                const cupidimage_sixel_options *options,
                                                char *error, size_t capacity) {
    sixel_calls++;
    return __real_cupidimage_render_sixel_with_options(image, out, width, height, options, error,
                                                       capacity);
}

static const cupidimage_image *get(const char *path, int width, int height, uint64_t *generation) {
    char error[256] = {0};
    return image_preview_get(path, width, height, generation, error, sizeof(error));
}

int main(void) {
    char directory[] = "/tmp/cupidfm-image-cache-XXXXXX";
    assert(mkdtemp(directory));
    char names[6][256];
    for (int i = 0; i < 6; i++) {
        snprintf(names[i], sizeof(names[i]), "%s/%d.bmp", directory, i);
        int fd = open(names[i], O_WRONLY | O_CREAT | O_EXCL, 0600);
        assert(fd >= 0);
        assert(write(fd, "x", 1) == 1);
        close(fd);
    }
    struct timespec settle = {2, 100000000};
    nanosleep(&settle, NULL);
    uint64_t first, second;
    char error[256] = {0};
    size_t length;
    const cupidimage_image *image = get(names[0], 16, 8, &first);
    assert(image && image->width == 16 && image->height == 8 && loads == 1);
    assert(get(names[0], 16, 8, &second) == image && first == second && loads == 1);
    const char *kitty =
        image_preview_encode(image, IMAGE_PREVIEW_KITTY, 2, 1, &length, error, sizeof(error));
    assert(kitty && length && strstr(kitty, "s=16,v=8") && kitty_calls == 1);
    assert(image_preview_encode(image, IMAGE_PREVIEW_KITTY, 2, 1, &length, error, sizeof(error)) ==
               kitty &&
           kitty_calls == 1);
    assert(image_preview_encode(image, IMAGE_PREVIEW_KITTY, 3, 1, &length, error, sizeof(error)) &&
           kitty_calls == 2);
    const char *sixel =
        image_preview_encode(image, IMAGE_PREVIEW_SIXEL, 2, 1, &length, error, sizeof(error));
    assert(sixel && sixel_calls == 1);
    assert(image_preview_encode(image, IMAGE_PREVIEW_SIXEL, 2, 1, &length, error, sizeof(error)) ==
               sixel &&
           sixel_calls == 1);
    const char *ansi =
        image_preview_encode(image, IMAGE_PREVIEW_ANSI, 3, 2, &length, error, sizeof(error));
    assert(ansi);
    int lines = 0;
    for (size_t i = 0; i < length; i++)
        if (ansi[i] == '\n')
            lines++;
    assert(lines == 2);
    assert(!get(NULL, 1, 1, NULL) && !get(names[0], 0, 1, NULL));
    image = get(names[0], 7, 7, &second);
    assert(image && image->width == 7 && image->height == 3 && second != first);
    struct stat before;
    assert(stat(names[0], &before) == 0);
    struct timespec pause = {0, 1000000};
    nanosleep(&pause, NULL);
    int fd = open(names[0], O_WRONLY);
    assert(fd >= 0);
    assert(write(fd, "y", 1) == 1);
    struct timespec times[2] = {before.st_atim, before.st_mtim};
    assert(futimens(fd, times) == 0);
    close(fd);
    uint64_t changed;
    assert(get(names[0], 7, 7, &changed) && changed != second);
    int count = loads;
    for (int i = 1; i < 6; i++)
        assert(get(names[i], 7, 7, NULL));
    assert(get(names[0], 7, 7, NULL) && loads == count + 6);
    image_preview_clear_cache();
    assert(!retained && !encoded_retained);
    decode_failure = true;
    assert(!get(names[0], 16, 8, NULL));
    decode_failure = false;
    assert(get(names[0], 16, 8, NULL));
    image_preview_clear_cache();
    fail_malloc = 0;
    assert(!get(names[0], 16, 8, NULL));
    assert(get(names[0], 16, 8, NULL));
    image = get(names[0], 16, 8, NULL);
    fail_realloc = 0;
    assert(!image_preview_encode(image, IMAGE_PREVIEW_ANSI, 3, 2, &length, error, sizeof(error)));
    assert(image_preview_encode(image, IMAGE_PREVIEW_ANSI, 3, 2, &length, error, sizeof(error)));
    image_preview_clear_cache();
    change_during_decode = true;
    count = loads;
    assert(get(names[0], 16, 8, NULL));
    assert(get(names[0], 16, 8, NULL) && loads == count + 2);
    change_during_decode = false;
    image_preview_clear_cache();
    source_width = 5;
    source_height = 1;
    alpha_fixture = true;
    image = get(names[0], 4, 1, NULL);
    assert(image && image->width == 4 && image->height == 1 && image->rgba[4] == 255 &&
           image->rgba[6] == 0 && image->rgba[7] == 170);
    alpha_fixture = false;
    image_preview_clear_cache();
    source_width = source_height = 2048;
    assert(get(names[0], 2048, 2048, NULL));
    assert(retained == PREVIEW_BYTES);
    assert(get(names[1], 2048, 2048, NULL));
    assert(retained == PREVIEW_BYTES);
    image_preview_clear_cache();
    assert(!retained && !encoded_retained);
    for (int i = 0; i < 6; i++)
        assert(unlink(names[i]) == 0);
    assert(rmdir(directory) == 0);
    puts("Image previews: bounded thumbnails, render reuse, metadata/resize/eviction, alpha, "
         "failures, and retries passed");
    return 0;
}
