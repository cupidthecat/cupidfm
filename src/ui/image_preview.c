#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "image_preview.h"
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define PREVIEW_SLOTS 4
#define PREVIEW_BYTES (16u * 1024u * 1024u)
#define PREVIEW_DIMENSION 2048

typedef struct {
    char *data;
    size_t length;
    int columns, rows;
} Payload;

typedef struct {
    Payload encoded[3];
    char path[4096];
    struct stat stamp;
    int width, height;
    uint64_t generation, used;
    size_t bytes;
    cupidimage_image image;
} Preview;

static Preview previews[PREVIEW_SLOTS];
static uint64_t serial;
static size_t retained;
static size_t encoded_retained;
static bool cleanup_registered;

static void discard(Preview *preview) {
    for (size_t i = 0; i < 3; i++) {
        encoded_retained -= preview->encoded[i].length;
        free(preview->encoded[i].data);
    }
    retained -= preview->bytes;
    cupidimage_free(&preview->image);
    memset(preview, 0, sizeof(*preview));
}

void image_preview_clear_cache(void) {
    for (size_t i = 0; i < PREVIEW_SLOTS; i++)
        discard(&previews[i]);
}

static bool same_file(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size &&
           a->st_mode == b->st_mode && a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
           a->st_mtim.tv_nsec == b->st_mtim.tv_nsec && a->st_ctim.tv_sec == b->st_ctim.tv_sec &&
           a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}

static bool recent_change(const struct stat *stamp) {
    time_t now = time(NULL);
    double modified = difftime(now, stamp->st_mtim.tv_sec);
    double changed = difftime(now, stamp->st_ctim.tv_sec);
    /* Some supported filesystems report unchanged fractional timestamps for a
     * same-size rewrite. Do not trust metadata-only hits in this window. */
    return (modified >= 0 && modified < 2) || (changed >= 0 && changed < 2);
}

static Preview *oldest(void) {
    Preview *result = &previews[0];
    for (size_t i = 0; i < PREVIEW_SLOTS; i++) {
        if (!previews[i].image.rgba)
            return &previews[i];
        if (previews[i].used < result->used)
            result = &previews[i];
    }
    return result;
}

static int thumbnail(cupidimage_image *image, int max_width, int max_height) {
    double scale = 1.0;
    if (image->width > (uint32_t)max_width)
        scale = (double)max_width / image->width;
    if (image->height * scale > max_height)
        scale = (double)max_height / image->height;
    if (scale >= 1.0)
        return 1;
    uint32_t width = (uint32_t)(image->width * scale);
    uint32_t height = (uint32_t)(image->height * scale);
    if (!width)
        width = 1;
    if (!height)
        height = 1;
    uint8_t *pixels = malloc((size_t)width * height * 4);
    if (!pixels)
        return 0;
    for (uint32_t y = 0; y < height; y++) {
        double source_y = height > 1 ? (double)y * (image->height - 1) / (height - 1) : 0;
        uint32_t y0 = (uint32_t)source_y, y1 = y0 + (y0 + 1 < image->height);
        double fy = source_y - y0;
        for (uint32_t x = 0; x < width; x++) {
            double source_x = width > 1 ? (double)x * (image->width - 1) / (width - 1) : 0;
            uint32_t x0 = (uint32_t)source_x, x1 = x0 + (x0 + 1 < image->width);
            double fx = source_x - x0;
            size_t positions[] = {
                ((size_t)y0 * image->width + x0) * 4, ((size_t)y0 * image->width + x1) * 4,
                ((size_t)y1 * image->width + x0) * 4, ((size_t)y1 * image->width + x1) * 4};
            double weights[] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
            double alpha = 0;
            for (size_t i = 0; i < 4; i++)
                alpha += image->rgba[positions[i] + 3] * weights[i];
            size_t output = ((size_t)y * width + x) * 4;
            pixels[output + 3] = (uint8_t)(alpha + 0.5);
            for (size_t channel = 0; channel < 3; channel++) {
                double value = 0;
                for (size_t i = 0; i < 4; i++)
                    value += image->rgba[positions[i] + channel] * image->rgba[positions[i] + 3] *
                             weights[i];
                value = alpha > 0 ? value / alpha : 0;
                pixels[output + channel] = (uint8_t)(value > 255 ? 255 : value + 0.5);
            }
        }
    }
    cupidimage_free(image);
    image->width = width;
    image->height = height;
    image->rgba = pixels;
    return 1;
}

const cupidimage_image *image_preview_get(const char *path, int max_width, int max_height,
                                          uint64_t *generation, char *error, size_t error_size) {
    if (generation)
        *generation = 0;
    if (error && error_size)
        error[0] = 0;
    struct stat stamp;
    if (!path || strlen(path) >= sizeof(previews[0].path) || max_width <= 0 || max_height <= 0 ||
        stat(path, &stamp) || !S_ISREG(stamp.st_mode)) {
        if (error && error_size)
            snprintf(error, error_size, "Image is not a readable regular file");
        return NULL;
    }
    if (max_width > PREVIEW_DIMENSION)
        max_width = PREVIEW_DIMENSION;
    if (max_height > PREVIEW_DIMENSION)
        max_height = PREVIEW_DIMENSION;
    if (!cleanup_registered) {
        if (atexit(image_preview_clear_cache))
            return NULL;
        cleanup_registered = true;
    }
    for (size_t i = 0; i < PREVIEW_SLOTS; i++) {
        Preview *entry = &previews[i];
        if (entry->image.rgba && !strcmp(path, entry->path) && entry->width == max_width &&
            entry->height == max_height && same_file(&entry->stamp, &stamp) &&
            !recent_change(&stamp)) {
            entry->used = ++serial;
            if (generation)
                *generation = entry->generation;
            return &entry->image;
        }
    }
    cupidimage_image image = {0};
    if (!cupidimage_load_image_file(path, &image, error, error_size)) {
        cupidimage_free(&image);
        return NULL;
    }
    if (!image.width || !image.height || !image.rgba ||
        image.width > SIZE_MAX / 4u / image.height || !thumbnail(&image, max_width, max_height)) {
        cupidimage_free(&image);
        if (error && error_size)
            snprintf(error, error_size, "Unable to allocate image thumbnail");
        return NULL;
    }
    size_t bytes = (size_t)image.width * image.height * 4;
    Preview *entry = oldest();
    discard(entry);
    while (retained + bytes > PREVIEW_BYTES) {
        Preview *victim = NULL;
        for (size_t i = 0; i < PREVIEW_SLOTS; i++)
            if (previews[i].image.rgba && (!victim || previews[i].used < victim->used))
                victim = &previews[i];
        if (!victim)
            break;
        discard(victim);
    }
    entry->image = image;
    entry->bytes = bytes;
    retained += bytes;
    entry->width = max_width;
    entry->height = max_height;
    entry->stamp = stamp;
    snprintf(entry->path, sizeof(entry->path), "%s", path);
    struct stat after;
    if (stat(path, &after) || !same_file(&stamp, &after))
        entry->path[0] = 0;
    entry->generation = entry->used = ++serial;
    if (generation)
        *generation = entry->generation;
    return &entry->image;
}

#define PAYLOAD_LIMIT (8u * 1024u * 1024u)
#define PAYLOAD_BUDGET (16u * 1024u * 1024u)

typedef struct {
    char *data;
    size_t size, capacity;
} Sink;

static ssize_t sink_write(void *cookie, const char *bytes, size_t size) {
    Sink *sink = cookie;
    if (!size)
        return 0;
    if (size > PAYLOAD_LIMIT - sink->size) {
        errno = EFBIG;
        return -1;
    }
    size_t need = sink->size + size + 1;
    if (need > sink->capacity) {
        size_t capacity = sink->capacity ? sink->capacity : 4096;
        while (capacity < need)
            capacity *= 2;
        if (capacity > PAYLOAD_LIMIT + 1)
            capacity = PAYLOAD_LIMIT + 1;
        char *data = realloc(sink->data, capacity);
        if (!data) {
            errno = ENOMEM;
            return -1;
        }
        sink->data = data;
        sink->capacity = capacity;
    }
    memcpy(sink->data + sink->size, bytes, size);
    sink->size += size;
    sink->data[sink->size] = 0;
    return (ssize_t)size;
}

static void clear_payloads(void) {
    for (size_t i = 0; i < PREVIEW_SLOTS; i++)
        for (size_t j = 0; j < 3; j++) {
            free(previews[i].encoded[j].data);
            memset(&previews[i].encoded[j], 0, sizeof(Payload));
        }
    encoded_retained = 0;
}

const char *image_preview_encode(const cupidimage_image *image, enum ImagePreviewEncoding encoding,
                                 int columns, int rows, size_t *length, char *error,
                                 size_t error_size) {
    if (length)
        *length = 0;
    if (error && error_size)
        error[0] = 0;
    Preview *entry = NULL;
    for (size_t i = 0; i < PREVIEW_SLOTS; i++)
        if (image == &previews[i].image && image->rgba)
            entry = &previews[i];
    if (!entry || encoding < IMAGE_PREVIEW_ANSI || encoding > IMAGE_PREVIEW_SIXEL || columns <= 0 ||
        rows <= 0 || columns > PREVIEW_DIMENSION || rows > PREVIEW_DIMENSION)
        return NULL;
    Payload *payload = &entry->encoded[encoding];
    if (payload->data && payload->columns == columns && payload->rows == rows) {
        if (length)
            *length = payload->length;
        return payload->data;
    }
    Sink sink = {0};
    cookie_io_functions_t functions = {.write = sink_write};
    FILE *stream = fopencookie(&sink, "w", functions);
    if (!stream)
        return NULL;
    int result = 0;
    if (encoding == IMAGE_PREVIEW_ANSI) {
        cupidimage_image grid = {.width = (uint32_t)columns, .height = (uint32_t)rows};
        grid.rgba = malloc((size_t)columns * rows * 4);
        if (grid.rgba) {
            for (int y = 0; y < rows; y++)
                for (int x = 0; x < columns; x++) {
                    size_t source_x = (uint64_t)x * image->width / columns;
                    size_t source_y = (uint64_t)y * image->height / rows;
                    memcpy(grid.rgba + ((size_t)y * columns + x) * 4,
                           image->rgba + (source_y * image->width + source_x) * 4, 4);
                }
            result = cupidimage_render_ansi(&grid, stream, 0, 0);
            cupidimage_free(&grid);
        }
    } else if (encoding == IMAGE_PREVIEW_KITTY) {
        cupidimage_kitty_options options = {
            .image_id = CUPIDFM_PREVIEW_IMAGE_ID, .placement_id = 1, .compression = 1};
        result = cupidimage_render_kitty_with_options(image, stream, (uint32_t)columns,
                                                      (uint32_t)rows, &options, error, error_size);
    } else {
        cupidimage_sixel_options options = {
            .max_colors = 256, .dither_mode = 1, .pixel_aspect_ratio = 1.0f};
        result =
            cupidimage_render_sixel_with_options(image, stream, 0, 0, &options, error, error_size);
    }
    int closed = fclose(stream);
    if (!result || closed || !sink.data || !sink.size) {
        free(sink.data);
        if (error && error_size && !error[0])
            snprintf(error, error_size,
                     "Image rendering failed or exceeded the 8 MiB output limit");
        return NULL;
    }
    if (encoded_retained - payload->length + sink.size > PAYLOAD_BUDGET)
        clear_payloads();
    payload = &entry->encoded[encoding];
    encoded_retained -= payload->length;
    free(payload->data);
    *payload = (Payload){.data = sink.data, .length = sink.size, .columns = columns, .rows = rows};
    encoded_retained += sink.size;
    if (length)
        *length = payload->length;
    return payload->data;
}
