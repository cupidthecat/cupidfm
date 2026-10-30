#ifndef IMAGE_PREVIEW_H
#define IMAGE_PREVIEW_H
#include "cupidimage.h"
#include <stddef.h>
#include <stdint.h>

/* UI-thread cache. Borrowed pixels remain valid until another get or clear.
 * Bounds describe the preview's pixel viewport, not the source image size. */
const cupidimage_image *image_preview_get(const char *path, int max_width, int max_height,
                                          uint64_t *generation, char *error, size_t error_size);
void image_preview_clear_cache(void);

enum ImagePreviewEncoding { IMAGE_PREVIEW_ANSI, IMAGE_PREVIEW_KITTY, IMAGE_PREVIEW_SIXEL };

#define CUPIDFM_PREVIEW_IMAGE_ID 0x4355464du
/* Borrowed encoded bytes remain valid until the next cache operation. */
const char *image_preview_encode(const cupidimage_image *image, enum ImagePreviewEncoding encoding,
                                 int columns, int rows, size_t *length, char *error,
                                 size_t error_size);
#endif
