#ifndef CUPIDIMAGE_KITTY_H
#define CUPIDIMAGE_KITTY_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declare cupidimage types */
typedef struct cupidimage_image cupidimage_image;
typedef struct cupidimage_animation cupidimage_animation;

/* Kitty rendering options */
typedef struct cupidimage_kitty_options {
    uint32_t image_id;         /* Image ID (0 = auto-assign) */
    uint32_t placement_id;     /* Placement ID (0 = auto-assign) */
    int compression;           /* Use zlib compression (1 = yes, 0 = no) */
    int delete_previous;       /* Delete previous images before rendering */
} cupidimage_kitty_options;

/* Detect if running in Kitty terminal */
int cupidimage_is_kitty_terminal(void);

/* Render static image to Kitty graphics protocol */
int cupidimage_render_kitty(const cupidimage_image *img, FILE *out,
                           uint32_t term_width, uint32_t term_height,
                           char *err, size_t errcap);

/* Render with custom options */
int cupidimage_render_kitty_with_options(const cupidimage_image *img, FILE *out,
                                        uint32_t term_width, uint32_t term_height,
                                        const cupidimage_kitty_options *opts,
                                        char *err, size_t errcap);

/* Render animated image to Kitty graphics protocol */
int cupidimage_render_kitty_animation(const cupidimage_animation *anim, FILE *out,
                                     uint32_t term_width, uint32_t term_height,
                                     char *err, size_t errcap);

/* Render animation with custom options */
int cupidimage_render_kitty_animation_with_options(
    const cupidimage_animation *anim, FILE *out,
    uint32_t term_width, uint32_t term_height,
    const cupidimage_kitty_options *opts,
    char *err, size_t errcap);

/* Delete all images from terminal */
int cupidimage_kitty_delete_all(FILE *out, char *err, size_t errcap);

/* Delete specific image by ID */
int cupidimage_kitty_delete_image(FILE *out, uint32_t image_id,
                                 char *err, size_t errcap);

/* Delete specific placement */
int cupidimage_kitty_delete_placement(FILE *out, uint32_t image_id,
                                     uint32_t placement_id,
                                     char *err, size_t errcap);

#ifdef __cplusplus
}
#endif

#endif /* CUPIDIMAGE_KITTY_H */
