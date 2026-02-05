#ifndef CUPIDIMAGE_SIXEL_H
#define CUPIDIMAGE_SIXEL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declare cupidimage types */
typedef struct cupidimage_image cupidimage_image;
typedef struct cupidimage_animation cupidimage_animation;

/* Sixel rendering options */
typedef struct cupidimage_sixel_options {
    uint32_t max_colors;       /* Maximum palette size (2-256, default 256) */
    uint8_t dither_mode;       /* 0=none, 1=floyd-steinberg, 2=atkinson, 3=ordered */
    uint8_t use_transparency;  /* Enable transparency (1 = yes, 0 = no) */
    uint32_t background_color; /* Background color for alpha blending (RGB, default 0xFFFFFF) */
    float pixel_aspect_ratio;  /* Pixel aspect ratio (default 2.0) */
    int delete_previous;       /* Clear screen before rendering */
} cupidimage_sixel_options;

/* Detect if running in a Sixel-capable terminal */
int cupidimage_is_sixel_terminal(void);

/* Render static image using Sixel protocol */
int cupidimage_render_sixel(const cupidimage_image *img, FILE *out,
                            uint32_t term_width, uint32_t term_height,
                            char *err, size_t errcap);

/* Render with custom options */
int cupidimage_render_sixel_with_options(const cupidimage_image *img, FILE *out,
                                        uint32_t term_width, uint32_t term_height,
                                        const cupidimage_sixel_options *opts,
                                        char *err, size_t errcap);

/* Render animated image using Sixel protocol */
int cupidimage_render_sixel_animation(const cupidimage_animation *anim, FILE *out,
                                     uint32_t term_width, uint32_t term_height,
                                     char *err, size_t errcap);

/* Render animation with custom options */
int cupidimage_render_sixel_animation_with_options(
    const cupidimage_animation *anim, FILE *out,
    uint32_t term_width, uint32_t term_height,
    const cupidimage_sixel_options *opts,
    char *err, size_t errcap);

#ifdef __cplusplus
}
#endif

#endif /* CUPIDIMAGE_SIXEL_H */
