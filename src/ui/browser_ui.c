// browser_ui.c - directory + preview pane rendering and related helpers
#define _GNU_SOURCE
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "browser_ui.h"

#include <dirent.h>
#include <errno.h>
#include <magic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>

#include "files.h"
#include "git.h"
#include "globals.h"
#include "syntax.h"
#include "mime.h"
#include "cupidimage.h"

#define DIRECTORY_TREE_MAX_DEPTH 4
#define DIRECTORY_TREE_MAX_TOTAL 1500

static bool tree_limit_hit = false;

static void count_directory_tree_lines(const char *dir_path, int level, int *line_count) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    struct stat statbuf;
    char full_path[MAX_PATH_LENGTH];
    size_t dir_path_len = strlen(dir_path);

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        size_t name_len = strlen(entry->d_name);
        if (dir_path_len + name_len + 2 > MAX_PATH_LENGTH) continue;

        strcpy(full_path, dir_path);
        if (full_path[dir_path_len - 1] != '/') {
            strcat(full_path, "/");
        }
        strcat(full_path, entry->d_name);

        if (lstat(full_path, &statbuf) == -1) continue;

        (*line_count)++; // Count this entry
        if (*line_count >= DIRECTORY_TREE_MAX_TOTAL) {
            break;
        }

        if (S_ISDIR(statbuf.st_mode) &&
            level < DIRECTORY_TREE_MAX_DEPTH) {
            count_directory_tree_lines(full_path, level + 1, line_count);
            if (*line_count >= DIRECTORY_TREE_MAX_TOTAL) {
                break;
            }
        }
    }

    closedir(dir);
}

int get_directory_tree_total_lines(const char *dir_path) {
    int line_count = 0;
    count_directory_tree_lines(dir_path, 0, &line_count);
    return line_count;
}

typedef struct {
    int base_pair;
    int size;
    bool initialized;
} ImagePalette;

static ImagePalette image_palette = {0, 0, false};
static bool last_truecolor_active = false;
static char last_kitty_image_path[MAX_PATH_LENGTH] = {0};
static char last_sixel_image_path[MAX_PATH_LENGTH] = {0};

static const int image_palette_reserved_start = 32;

static bool supports_truecolor(void) {
    if (!isatty(fileno(stdout))) return false;
    const char *ct = getenv("COLORTERM");
    if (ct && (strstr(ct, "truecolor") || strstr(ct, "24bit"))) return true;
    if (getenv("KITTY_WINDOW_ID")) return true;
    const char *term = getenv("TERM");
    if (term && (strstr(term, "kitty") || strstr(term, "direct"))) return true;
    return false;
}

static void clear_truecolor_overlay(WINDOW *window, int max_y, int max_x) {
    int content_top = 7;
    int content_left = 2;
    int content_width = max_x - 4;
    int content_height = max_y - content_top - 1;
    if (content_width <= 0 || content_height <= 0) return;

    int win_y = 0, win_x = 0;
    getbegyx(window, win_y, win_x);

    for (int row = 0; row < content_height; row++) {
        int abs_row = win_y + content_top + row;
        int abs_col = win_x + content_left;
        printf("\x1b[%d;%dH\x1b[0m", abs_row + 1, abs_col + 1);
        for (int col = 0; col < content_width; col++) {
            fputc(' ', stdout);
        }
    }
    fflush(stdout);
}

/* Clear Sixel graphics from the content area by overwriting with a black Sixel image */
static void clear_sixel_overlay(WINDOW *window, int max_y, int max_x) {
    int content_top = 7;
    int content_left = 2;
    int content_width = max_x - 4;
    int content_height = max_y - content_top - 1;
    if (content_width <= 0 || content_height <= 0) return;

    int win_y = 0, win_x = 0;
    getbegyx(window, win_y, win_x);

    /* Calculate pixel dimensions to cover the entire content area */
    const int cell_width_px = 8;
    const int cell_height_px = 16;
    int pixel_width = content_width * cell_width_px;
    int pixel_height = content_height * cell_height_px;
    
    /* Limit size to prevent huge allocations */
    if (pixel_width > 2000) pixel_width = 2000;
    if (pixel_height > 2000) pixel_height = 2000;

    /* Position cursor at content area */
    int abs_row = win_y + content_top;
    int abs_col = win_x + content_left;
    printf("\x1b[%d;%dH", abs_row + 1, abs_col + 1);
    
    /* Output a black Sixel image that covers the content area
     * Sixel encodes 6 vertical pixels per row, character '?' (0x3F) = all 6 pixels off (black with color 0)
     * Format: DCS q "1;1;W;H (raster attributes) #0;2;0;0;0 (color 0 = black) <data> ST */
    printf("\x1bPq");
    printf("\"1;1;%d;%d", pixel_width, pixel_height);  /* Raster attributes */
    printf("#0;2;0;0;0");  /* Define color 0 as black (RGB 0,0,0) */
    printf("#0");  /* Select color 0 */
    
    /* Output Sixel data - '?' means all 6 pixels are off (transparent/background)
     * We need to cover pixel_height/6 sixel rows */
    int sixel_rows = (pixel_height + 5) / 6;
    for (int row = 0; row < sixel_rows; row++) {
        /* Output a row of '?' characters (all pixels off) for the width */
        /* Use run-length encoding: !<count><char> */
        if (pixel_width > 3) {
            printf("!%d?", pixel_width);
        } else {
            for (int x = 0; x < pixel_width; x++) {
                putchar('?');
            }
        }
        /* Move to next sixel row (except for last row) */
        if (row + 1 < sixel_rows) {
            putchar('-');  /* Graphics newline */
        }
    }
    
    printf("\x1b\\");  /* String Terminator - end Sixel sequence */
    
    /* Also clear the text layer */
    for (int row = 0; row < content_height; row++) {
        abs_row = win_y + content_top + row;
        abs_col = win_x + content_left;
        printf("\x1b[%d;%dH\x1b[0m", abs_row + 1, abs_col + 1);
        for (int col = 0; col < content_width; col++) {
            fputc(' ', stdout);
        }
    }
    
    fflush(stdout);
}

static bool is_image_extension(const char *path) {
    if (!path) return false;
    const char *ext = strrchr(path, '.');
    if (!ext) return false;
    return strcasecmp(ext, ".png") == 0 ||
           strcasecmp(ext, ".jpg") == 0 ||
           strcasecmp(ext, ".jpeg") == 0 ||
           strcasecmp(ext, ".webp") == 0 ||
           strcasecmp(ext, ".gif") == 0 ||
           strcasecmp(ext, ".bmp") == 0 ||
           strcasecmp(ext, ".ico") == 0 ||
           strcasecmp(ext, ".tif") == 0 ||
           strcasecmp(ext, ".tiff") == 0;
}

static int load_cupidimage_image(const char *path, cupidimage_image *out, char *err, size_t errcap) {
    if (!path || !out) return 0;
    if (err && errcap > 0) err[0] = '\0';

    int rc = cupidimage_load_image_file(path, out, err, errcap);
    if (rc != 0) return 1;

    const char *ext = strrchr(path, '.');
    if (!ext) return 0;

    if (err && errcap > 0) err[0] = '\0';
    if (strcasecmp(ext, ".png") == 0) {
        return cupidimage_load_png_file(path, out, err, errcap) != 0;
    }
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        return cupidimage_load_jpeg_file(path, out, err, errcap) != 0;
    }
    if (strcasecmp(ext, ".webp") == 0) {
        return cupidimage_load_webp_file(path, out, err, errcap) != 0;
    }
    if (strcasecmp(ext, ".gif") == 0) {
        return cupidimage_load_gif_file(path, out, err, errcap) != 0;
    }
    if (strcasecmp(ext, ".bmp") == 0) {
        return cupidimage_load_bmp_file(path, out, err, errcap) != 0;
    }
    if (strcasecmp(ext, ".ico") == 0) {
        return cupidimage_load_ico_page(path, 0, out, err, errcap) != 0;
    }
    if (strcasecmp(ext, ".tif") == 0 || strcasecmp(ext, ".tiff") == 0) {
        return cupidimage_load_tiff_file(path, out, err, errcap) != 0;
    }

    return 0;
}

static bool is_image_file(const char *path) {
    if (!path || !*path) return false;
    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE | MAGIC_SYMLINK | MAGIC_CHECK);
    if (magic_cookie != NULL && magic_load(magic_cookie, NULL) == 0) {
        const char *mime_type = magic_file(magic_cookie, path);
        bool is_image = (mime_type != NULL && strncmp(mime_type, "image/", 6) == 0);
        magic_close(magic_cookie);
        if (is_image) return true;
    } else if (magic_cookie) {
        magic_close(magic_cookie);
    }
    return is_image_extension(path);
}

static void ansi256_to_rgb(int idx, int *r, int *g, int *b) {
    if (idx < 16) {
        static const int base16[16][3] = {
            {0, 0, 0},       {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
            {0, 0, 238},     {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
            {127, 127, 127}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
            {92, 92, 255},   {255, 0, 255},   {0, 255, 255},   {255, 255, 255}
        };
        *r = base16[idx][0];
        *g = base16[idx][1];
        *b = base16[idx][2];
        return;
    }
    if (idx >= 232) {
        int v = 8 + (idx - 232) * 10;
        if (v > 255) v = 255;
        *r = v;
        *g = v;
        *b = v;
        return;
    }
    int i = idx - 16;
    int r6 = i / 36;
    int g6 = (i / 6) % 6;
    int b6 = i % 6;
    static const int steps[6] = {0, 95, 135, 175, 215, 255};
    *r = steps[r6];
    *g = steps[g6];
    *b = steps[b6];
}

static int rgb_to_ansi256(uint8_t r, uint8_t g, uint8_t b) {
    if (r == g && g == b) {
        if (r < 8) return 16;
        if (r > 248) return 231;
        return 232 + (int)((r - 8) / 10);
    }
    int r6 = (r * 5 + 127) / 255;
    int g6 = (g * 5 + 127) / 255;
    int b6 = (b * 5 + 127) / 255;
    return 16 + 36 * r6 + 6 * g6 + b6;
}

static int rgb_to_palette_index(uint8_t r, uint8_t g, uint8_t b, int palette_size) {
    if (palette_size >= 256) {
        return rgb_to_ansi256(r, g, b);
    }

    static const uint8_t palette16[16][3] = {
        {0, 0, 0},       {205, 0, 0},     {0, 205, 0},     {205, 205, 0},
        {0, 0, 238},     {205, 0, 205},   {0, 205, 205},   {229, 229, 229},
        {127, 127, 127}, {255, 0, 0},     {0, 255, 0},     {255, 255, 0},
        {92, 92, 255},   {255, 0, 255},   {0, 255, 255},   {255, 255, 255}
    };

    int best_idx = 0;
    int best_dist = INT32_MAX;
    int limit = (palette_size >= 16) ? 16 : 8;
    for (int i = 0; i < limit; i++) {
        int dr = (int)r - palette16[i][0];
        int dg = (int)g - palette16[i][1];
        int db = (int)b - palette16[i][2];
        int dist = dr * dr + dg * dg + db * db;
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }
    return best_idx;
}

static void ensure_image_palette(void) {
    if (image_palette.initialized) return;
    image_palette.initialized = true;

    if (!has_colors()) return;

    int max_colors = COLORS;
    int max_pairs = COLOR_PAIRS;
    int desired = 0;
    if (max_colors >= 256) {
        desired = 256;
    } else if (max_colors >= 16) {
        desired = 16;
    } else if (max_colors >= 8) {
        desired = 8;
    }

    if (desired <= 0 || max_pairs <= 1) return;

    int base = image_palette_reserved_start;
    if (base <= COLOR_SYNTAX_ESCAPE) {
        base = COLOR_SYNTAX_ESCAPE + 1;
    }
    if (base + desired > max_pairs) {
        while (desired > 0 && base + desired > max_pairs) {
            desired /= 2;
        }
    }

    if (desired <= 0) return;

    image_palette.base_pair = base;
    image_palette.size = desired;

    if (can_change_color() && max_colors >= 256) {
        int max_idx = max_colors - 1;
        if (max_idx > 255) max_idx = 255;
        for (int i = 16; i <= max_idx; i++) {
            int r = 0, g = 0, b = 0;
            ansi256_to_rgb(i, &r, &g, &b);
            init_color((short)i,
                       (short)(r * 1000 / 255),
                       (short)(g * 1000 / 255),
                       (short)(b * 1000 / 255));
        }
    }

    for (int i = 0; i < desired; i++) {
        init_pair((short)(base + i), (short)i, COLOR_BLACK);
    }
}

static const double image_cell_aspect = 2.0;

static void compute_ansi_scaled_dims(uint32_t src_w, uint32_t src_h,
                                     int max_width, int max_height,
                                     int *out_w, int *out_h) {
    if (src_w == 0 || src_h == 0) {
        *out_w = 0;
        *out_h = 0;
        return;
    }

    int width = (int)src_w;
    int height = (int)src_h;
    double scale = 1.0;
    if (max_width > 0 && width > max_width) {
        double s = (double)max_width / (double)width;
        if (s < scale) scale = s;
    }
    if (max_height > 0 && height > max_height) {
        double s = (double)max_height / (double)height;
        if (s < scale) scale = s;
    }
    int outw = (int)(width * scale);
    int outh = (int)(height * scale);
    if (outw < 1) outw = 1;
    if (outh < 1) outh = 1;
    *out_w = outw;
    *out_h = outh;
}

static void scale_image_to_fit(uint32_t src_w, uint32_t src_h,
                               int max_width, int max_height,
                               int *out_w, int *out_h) {
    if (src_w == 0 || src_h == 0 || max_width <= 0 || max_height <= 0) {
        *out_w = 0;
        *out_h = 0;
        return;
    }

    double scale_w = (double)max_width / (double)src_w;
    double scale_h = ((double)max_height * image_cell_aspect) / (double)src_h;
    double scale = scale_w < scale_h ? scale_w : scale_h;
    if (scale > 1.0) scale = 1.0;
    if (scale <= 0.0) {
        *out_w = 0;
        *out_h = 0;
        return;
    }

    int target_w = (int)(src_w * scale + 0.5);
    int target_h = (int)(src_h * scale / image_cell_aspect + 0.5);
    if (target_w < 1) target_w = 1;
    if (target_h < 1) target_h = 1;
    if (target_w > max_width) target_w = max_width;
    if (target_h > max_height) target_h = max_height;

    *out_w = target_w;
    *out_h = target_h;
}

static void draw_image_preview(WINDOW *window, const char *full_path, int start_line,
                               int max_y, int max_x) {
    int content_top = 7;
    int content_left = 2;
    int content_width = max_x - 4;
    int content_height = max_y - content_top - 1;

    if (content_width <= 0 || content_height <= 0) {
        mvwprintw(window, content_top, 2, "Preview area too small");
        return;
    }

    ensure_image_palette();
    if (image_palette.size <= 0) {
        mvwprintw(window, content_top, 2, "Image preview requires terminal color support");
        return;
    }

    if (access(full_path, R_OK) != 0) {
        mvwprintw(window, content_top, 2, "Image preview failed: %s", strerror(errno));
        return;
    }

    cupidimage_image img = {0};
    char err[256] = {0};
    if (!load_cupidimage_image(full_path, &img, err, sizeof(err))) {
        mvwprintw(window, content_top, 2, "Image preview failed: %s",
                  err[0] ? err : "Unsupported format or decode error");
        return;
    }

    int target_w = 0;
    int target_h = 0;
    scale_image_to_fit(img.width, img.height, content_width, content_height, &target_w, &target_h);

    if (target_w <= 0 || target_h <= 0) {
        cupidimage_free(&img);
        mvwprintw(window, content_top, 2, "Image preview failed: invalid image size");
        return;
    }

    if (start_line < 0) start_line = 0;
    if (start_line >= target_h) start_line = target_h - 1;

    int rows_to_draw = MIN(content_height, target_h - start_line);
    int cols_to_draw = MIN(content_width, target_w);

    bool use_alpha = false;
    bool alpha_all_zero = true;
    bool alpha_all_opaque = true;
    bool alpha_seen_zero = false;
    bool alpha_seen_opaque = false;
    size_t total_pixels = (size_t)img.width * (size_t)img.height;
    size_t step = total_pixels / 10000;
    if (step < 1) step = 1;
    for (size_t i = 0; i < total_pixels; i += step) {
        uint8_t a = img.rgba[i * 4 + 3];
        if (a != 0) alpha_all_zero = false;
        if (a != 255) alpha_all_opaque = false;
        if (a == 0) alpha_seen_zero = true;
        if (a == 255) alpha_seen_opaque = true;
        if (a > 0 && a < 255) {
            use_alpha = true;
            break;
        }
    }
    if (!use_alpha && alpha_seen_zero && alpha_seen_opaque) {
        use_alpha = true;
    }
    if (alpha_all_zero || alpha_all_opaque) {
        use_alpha = false;
    }

    for (int row = 0; row < rows_to_draw; row++) {
        int out_y = content_top + row;
        int render_y = start_line + row;
            int src_y = (int)(((uint64_t)render_y * img.height) / target_h);

        wmove(window, out_y, content_left);

        int current_pair = -1;
        for (int col = 0; col < cols_to_draw; col++) {
            int src_x = (int)(((uint64_t)col * img.width) / target_w);
            size_t idx = ((size_t)src_y * img.width + (size_t)src_x) * 4;
            uint8_t r = img.rgba[idx];
            uint8_t g = img.rgba[idx + 1];
            uint8_t b = img.rgba[idx + 2];
            uint8_t a = img.rgba[idx + 3];

            int pair = -1;
            if (!use_alpha || a > 16) {
                int palette_idx = rgb_to_palette_index(r, g, b, image_palette.size);
                if (palette_idx >= image_palette.size) palette_idx = image_palette.size - 1;
                pair = image_palette.base_pair + palette_idx;
            }

            if (pair != current_pair) {
                if (pair >= 0) {
                    wattrset(window, COLOR_PAIR(pair));
                } else {
                    wattrset(window, A_NORMAL);
                }
                current_pair = pair;
            }

            if (pair >= 0) {
                waddch(window, ACS_BLOCK);
            } else {
                waddch(window, ' ');
            }
        }

        if (current_pair >= 0) {
            wattrset(window, A_NORMAL);
        }
    }

    cupidimage_free(&img);
}

static void draw_image_preview_truecolor(WINDOW *window, const char *full_path, int start_line,
                                         int max_y, int max_x) {
    int content_top = 7;
    int content_left = 2;
    int content_width = max_x - 4;
    int content_height = max_y - content_top - 1;

    last_truecolor_active = false;
    if (content_width <= 0 || content_height <= 0) {
        mvwprintw(window, content_top, 2, "Preview area too small");
        wrefresh(window);
        return;
    }

    if (access(full_path, R_OK) != 0) {
        mvwprintw(window, content_top, 2, "Image preview failed: %s", strerror(errno));
        wrefresh(window);
        return;
    }

    cupidimage_image img = {0};
    char err[256] = {0};
    if (!load_cupidimage_image(full_path, &img, err, sizeof(err))) {
        mvwprintw(window, content_top, 2, "Image preview failed: %s",
                  err[0] ? err : "Unsupported format or decode error");
        wrefresh(window);
        return;
    }

    int ansi_max_h = (int)((double)content_height / image_cell_aspect + 0.5);
    if (ansi_max_h < 1) ansi_max_h = 1;

    int out_w = 0;
    int out_h = 0;
    compute_ansi_scaled_dims(img.width, img.height, content_width, ansi_max_h, &out_w, &out_h);

    if (start_line < 0) start_line = 0;
    if (start_line >= out_h) start_line = out_h - 1;

    char *buf = NULL;
    size_t buf_len = 0;
    FILE *mem = open_memstream(&buf, &buf_len);
    if (!mem) {
        cupidimage_free(&img);
        mvwprintw(window, content_top, 2, "Image preview failed: out of memory");
        wrefresh(window);
        return;
    }

    if (!cupidimage_render_ansi(&img, mem, content_width, ansi_max_h)) {
        fclose(mem);
        free(buf);
        cupidimage_free(&img);
        mvwprintw(window, content_top, 2, "Image preview failed: render error");
        wrefresh(window);
        return;
    }
    fclose(mem);

    wrefresh(window);

    clear_truecolor_overlay(window, max_y, max_x);

    int win_y = 0, win_x = 0;
    getbegyx(window, win_y, win_x);

    char *line_start = buf;
    int current_line = 0;
    while (current_line < start_line && line_start) {
        char *nl = strchr(line_start, '\n');
        if (!nl) {
            line_start = NULL;
            break;
        }
        line_start = nl + 1;
        current_line++;
    }

    int rows_to_draw = MIN(content_height, out_h - start_line);
    for (int row = 0; row < rows_to_draw && line_start; row++) {
        char *nl = strchr(line_start, '\n');
        size_t line_len = nl ? (size_t)(nl - line_start) : strlen(line_start);

        int abs_row = win_y + content_top + row;
        int abs_col = win_x + content_left;
        printf("\x1b[%d;%dH", abs_row + 1, abs_col + 1);
        if (line_len > 0) {
            fwrite(line_start, 1, line_len, stdout);
        }
        fflush(stdout);

        line_start = nl ? nl + 1 : NULL;
    }

    free(buf);
    cupidimage_free(&img);
    last_truecolor_active = true;
}

static void draw_image_preview_kitty(WINDOW *window, const char *full_path, int start_line,
                                    int max_y, int max_x) {
    (void)start_line;  /* Kitty renders full image, no line scrolling needed */
    
    int content_top = 7;
    int content_left = 2;
    int content_width = max_x - 4;
    int content_height = max_y - content_top - 1;

    last_truecolor_active = false;
    if (content_width <= 0 || content_height <= 0) {
        mvwprintw(window, content_top, 2, "Preview area too small");
        wrefresh(window);
        return;
    }

    if (access(full_path, R_OK) != 0) {
        mvwprintw(window, content_top, 2, "Image preview failed: %s", strerror(errno));
        wrefresh(window);
        return;
    }

    /* Check if this is the same image we already displayed - do this BEFORE loading */
    if (last_kitty_image_path[0] != '\0' && strcmp(last_kitty_image_path, full_path) == 0) {
        /* Same image, no need to re-render */
        last_truecolor_active = true;
        return;
    }

    cupidimage_image img = {0};
    char err[256] = {0};
    if (!load_cupidimage_image(full_path, &img, err, sizeof(err))) {
        mvwprintw(window, content_top, 2, "Image preview failed: %s",
                  err[0] ? err : "Unsupported format or decode error");
        wrefresh(window);
        return;
    }

    wrefresh(window);
    clear_truecolor_overlay(window, max_y, max_x);

    /* Clear any previous Kitty graphics only when switching images */
    if (last_kitty_image_path[0] != '\0') {
        cupidimage_kitty_delete_all(stdout, NULL, 0);
        fflush(stdout);
    }

    /* Get window position for absolute cursor positioning */
    int win_y = 0, win_x = 0;
    getbegyx(window, win_y, win_x);

    /* Hide cursor during image rendering */
    curs_set(0);
    printf("\x1b[?25l");   /* Also hide via escape sequence for terminals that need it */
    fflush(stdout);

    /* Move cursor to content area */
    int abs_row = win_y + content_top;
    int abs_col = win_x + content_left;
    printf("\x1b[%d;%dH", abs_row + 1, abs_col + 1);
    fflush(stdout);

    /* Render using Kitty graphics protocol */
    cupidimage_kitty_options opts = {0};
    opts.compression = 1;  /* Enable compression */
    opts.delete_previous = 0;  /* Don't delete other images */
    
    /* Use content dimensions as display size in character cells */
    if (!cupidimage_render_kitty_with_options(&img, stdout, content_width, content_height,
                                             &opts, err, sizeof(err))) {
        /* Reset cursor to safe position */
        printf("\x1b[%d;%dH", 1, 1);
        fflush(stdout);
        wmove(window, 0, 0);
        wrefresh(window);
        curs_set(0);
        cupidimage_free(&img);
        mvwprintw(window, content_top, 2, "Kitty preview failed: %s", 
                  err[0] ? err : "Render error");
        wrefresh(window);
        return;
    }

    /* Reset cursor to top-left of window to prevent it getting stuck */
    printf("\x1b[%d;%dH", win_y + 1, win_x + 1);
    fflush(stdout);
    
    /* Sync ncurses cursor position and refresh */
    wmove(window, 0, 0);
    wrefresh(window);
    curs_set(0);  /* Keep cursor hidden */

    /* Track this image path to avoid re-rendering */
    snprintf(last_kitty_image_path, sizeof(last_kitty_image_path), "%s", full_path);
    
    cupidimage_free(&img);
    last_truecolor_active = true;
}

static void draw_image_preview_sixel(WINDOW *window, const char *full_path, int start_line,
                                     int max_y, int max_x) {
    (void)start_line;  /* Sixel renders full image, no line scrolling needed */
    
    int content_top = 7;
    int content_left = 2;
    int content_width = max_x - 4;
    int content_height = max_y - content_top - 1;

    last_truecolor_active = false;
    if (content_width <= 0 || content_height <= 0) {
        mvwprintw(window, content_top, 2, "Preview area too small");
        wrefresh(window);
        return;
    }

    if (access(full_path, R_OK) != 0) {
        mvwprintw(window, content_top, 2, "Image preview failed: %s", strerror(errno));
        wrefresh(window);
        return;
    }

    /* Check if this is the same image we already displayed - do this BEFORE loading */
    if (last_sixel_image_path[0] != '\0' && strcmp(last_sixel_image_path, full_path) == 0) {
        /* Same image, no need to re-render */
        last_truecolor_active = true;
        return;
    }

    cupidimage_image img = {0};
    char err[256] = {0};
    if (!load_cupidimage_image(full_path, &img, err, sizeof(err))) {
        mvwprintw(window, content_top, 2, "Image preview failed: %s",
                  err[0] ? err : "Unsupported format or decode error");
        wrefresh(window);
        return;
    }

    wrefresh(window);
    
    /* Clear any previous Sixel image before rendering new one */
    if (last_sixel_image_path[0] != '\0') {
        clear_sixel_overlay(window, max_y, max_x);
    }
    clear_truecolor_overlay(window, max_y, max_x);

    /* Calculate pixel dimensions for the content area
     * Typical terminal character cells are ~8x16 pixels (width x height)
     * We use conservative estimates to ensure image fits within bounds */
    const int cell_width_px = 8;
    const int cell_height_px = 16;
    int max_pixel_width = content_width * cell_width_px;
    int max_pixel_height = content_height * cell_height_px;

    /* Scale image to fit within the content area */
    uint32_t src_w = img.width;
    uint32_t src_h = img.height;
    int target_w = (int)src_w;
    int target_h = (int)src_h;

    if (src_w > 0 && src_h > 0 && (target_w > max_pixel_width || target_h > max_pixel_height)) {
        double scale_w = (double)max_pixel_width / (double)src_w;
        double scale_h = (double)max_pixel_height / (double)src_h;
        double scale = scale_w < scale_h ? scale_w : scale_h;
        if (scale > 1.0) scale = 1.0;
        
        target_w = (int)(src_w * scale);
        target_h = (int)(src_h * scale);
        if (target_w < 1) target_w = 1;
        if (target_h < 1) target_h = 1;
    }

    /* Resize image if needed */
    cupidimage_image scaled_img = {0};
    cupidimage_image *render_img = &img;

    if (target_w != (int)src_w || target_h != (int)src_h) {
        scaled_img.width = (uint32_t)target_w;
        scaled_img.height = (uint32_t)target_h;
        scaled_img.rgba = (uint8_t *)malloc((size_t)target_w * (size_t)target_h * 4);
        
        if (!scaled_img.rgba) {
            cupidimage_free(&img);
            mvwprintw(window, content_top, 2, "Sixel preview failed: out of memory");
            wrefresh(window);
            return;
        }

        /* Bilinear interpolation resize */
        for (int y = 0; y < target_h; y++) {
            for (int x = 0; x < target_w; x++) {
                double src_x = (double)x * (double)(src_w - 1) / (double)(target_w - 1 > 0 ? target_w - 1 : 1);
                double src_y = (double)y * (double)(src_h - 1) / (double)(target_h - 1 > 0 ? target_h - 1 : 1);
                
                int x0 = (int)src_x;
                int y0 = (int)src_y;
                int x1 = x0 + 1 < (int)src_w ? x0 + 1 : x0;
                int y1 = y0 + 1 < (int)src_h ? y0 + 1 : y0;
                
                double xf = src_x - x0;
                double yf = src_y - y0;
                
                for (int c = 0; c < 4; c++) {
                    double v00 = img.rgba[(y0 * src_w + x0) * 4 + c];
                    double v10 = img.rgba[(y0 * src_w + x1) * 4 + c];
                    double v01 = img.rgba[(y1 * src_w + x0) * 4 + c];
                    double v11 = img.rgba[(y1 * src_w + x1) * 4 + c];
                    
                    double v = v00 * (1 - xf) * (1 - yf) +
                               v10 * xf * (1 - yf) +
                               v01 * (1 - xf) * yf +
                               v11 * xf * yf;
                    
                    scaled_img.rgba[(y * target_w + x) * 4 + c] = (uint8_t)(v + 0.5);
                }
            }
        }
        
        render_img = &scaled_img;
    }

    /* Get window position for absolute cursor positioning */
    int win_y = 0, win_x = 0;
    getbegyx(window, win_y, win_x);

    /* Hide cursor during image rendering */
    curs_set(0);
    printf("\x1b[?25l");   /* Also hide via escape sequence for terminals that need it */
    fflush(stdout);

    /* Move cursor to content area */
    int abs_row = win_y + content_top;
    int abs_col = win_x + content_left;
    printf("\x1b[%d;%dH", abs_row + 1, abs_col + 1);
    fflush(stdout);

    /* Render using Sixel graphics protocol */
    cupidimage_sixel_options opts = {0};
    opts.max_colors = 256;           /* Use full 256-color palette */
    opts.dither_mode = 1;            /* Floyd-Steinberg dithering for best quality */
    opts.use_transparency = 0;       /* Blend with background */
    opts.background_color = 0x000000; /* Black background to match terminal */
    opts.pixel_aspect_ratio = 1.0f;  /* Image is already properly scaled */
    opts.delete_previous = 0;        /* Don't clear screen */
    
    /* Render the (possibly scaled) image */
    if (!cupidimage_render_sixel_with_options(render_img, stdout, 0, 0,
                                              &opts, err, sizeof(err))) {
        /* Reset cursor to safe position */
        printf("\x1b[%d;%dH", 1, 1);
        fflush(stdout);
        wmove(window, 0, 0);
        wrefresh(window);
        curs_set(0);
        if (scaled_img.rgba) free(scaled_img.rgba);
        cupidimage_free(&img);
        mvwprintw(window, content_top, 2, "Sixel preview failed: %s", 
                  err[0] ? err : "Render error");
        wrefresh(window);
        return;
    }

    /* Reset cursor to top-left of window to prevent it getting stuck */
    printf("\x1b[%d;%dH", win_y + 1, win_x + 1);
    fflush(stdout);
    
    /* Sync ncurses cursor position and refresh */
    wmove(window, 0, 0);
    wrefresh(window);
    curs_set(0);  /* Keep cursor hidden */

    /* Clean up scaled image if we created one */
    if (scaled_img.rgba) free(scaled_img.rgba);

    /* Track this image path to avoid re-rendering */
    snprintf(last_sixel_image_path, sizeof(last_sixel_image_path), "%s", full_path);
    
    cupidimage_free(&img);
    last_truecolor_active = true;
}

static void show_directory_tree(WINDOW *window,
                                const char *dir_path,
                                int level,
                                int *line_num,
                                int max_y,
                                int max_x,
                                int start_line,
                                int *current_count) {
    if (level == 0) {
        tree_limit_hit = false;
    }
    if (level == 0) {
        mvwprintw(window, 6, 2, "Directory Tree Preview:");
        (*line_num)++;
    }

    if (*line_num >= max_y - 1) {
        return;
    }

    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    struct stat statbuf;
    char full_path[MAX_PATH_LENGTH];
    size_t dir_path_len = strlen(dir_path);

    const int WINDOW_SIZE = 50;

    struct {
        char name[MAX_PATH_LENGTH];
        bool is_dir;
        mode_t mode;
    } entries[WINDOW_SIZE];
    int entry_count = 0;

    while ((entry = readdir(dir)) != NULL && entry_count < WINDOW_SIZE) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        size_t name_len = strlen(entry->d_name);
        if (dir_path_len + name_len + 2 > MAX_PATH_LENGTH) continue;

        strcpy(full_path, dir_path);
        if (full_path[dir_path_len - 1] != '/') {
            strcat(full_path, "/");
        }
        strcat(full_path, entry->d_name);

        if (lstat(full_path, &statbuf) == -1) continue;

        strncpy(entries[entry_count].name, entry->d_name, MAX_PATH_LENGTH - 1);
        entries[entry_count].name[MAX_PATH_LENGTH - 1] = '\0';
        entries[entry_count].is_dir = S_ISDIR(statbuf.st_mode);
        entries[entry_count].mode = statbuf.st_mode;
        entry_count++;
    }
    closedir(dir);

    if (entry_count == 0) {
        if (*current_count >= start_line && *line_num < max_y - 1) {
            mvwprintw(window, *line_num, 2, "[Empty directory]");
            (*line_num)++;
        }
        (*current_count)++;
        return;
    }

    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE);
    if (magic_cookie && magic_load(magic_cookie, NULL) != 0) {
        magic_close(magic_cookie);
        magic_cookie = NULL;
    }

    for (int i = 0; i < entry_count; i++) {
        if (*current_count < start_line) {
            (*current_count)++;
            if (entries[i].is_dir && level < DIRECTORY_TREE_MAX_DEPTH) {
                size_t name_len = strlen(entries[i].name);
                if (dir_path_len + name_len + 2 <= MAX_PATH_LENGTH) {
                    strcpy(full_path, dir_path);
                    if (full_path[dir_path_len - 1] != '/') {
                        strcat(full_path, "/");
                    }
                    strcat(full_path, entries[i].name);
                    show_directory_tree(window, full_path, level + 1, line_num, max_y, max_x, start_line, current_count);
                }
            }
            continue;
        }

        if (*line_num >= max_y - 1) {
            break;
        }

        full_path[0] = '\0';
        size_t name_len = strlen(entries[i].name);
        if (dir_path_len + name_len + 2 <= MAX_PATH_LENGTH) {
            strcpy(full_path, dir_path);
            if (full_path[dir_path_len - 1] != '/') {
                strcat(full_path, "/");
            }
            strcat(full_path, entries[i].name);
        }

        struct stat link_statbuf;
        bool is_symlink = (lstat(full_path, &link_statbuf) == 0 && S_ISLNK(link_statbuf.st_mode));
        char symlink_target[MAX_PATH_LENGTH] = {0};

        if (is_symlink) {
            ssize_t target_len = readlink(full_path, symlink_target, sizeof(symlink_target) - 1);
            if (target_len > 0) {
                symlink_target[target_len] = '\0';
            }
        }

        const char *emoji;
        if (entries[i].is_dir) {
            emoji = "📁";
        } else if (magic_cookie) {
            if (dir_path_len + name_len + 2 <= MAX_PATH_LENGTH) {
                const char *mime_type = magic_file(magic_cookie, full_path);
                emoji = get_file_emoji(mime_type, entries[i].name);
            } else {
                emoji = "📄";
            }
        } else {
            emoji = "📄";
        }

        wmove(window, *line_num, 2 + level * 2);
        for (int clear_x = 2 + level * 2; clear_x < max_x - 10; clear_x++) {
            waddch(window, ' ');
        }

        int available_width = max_x - 4 - level * 2 - 10;
        int display_len = (int)name_len + (is_symlink ? (4 + (int)strlen(symlink_target)) : 0);

        if (display_len > available_width) {
            if (is_symlink && strlen(symlink_target) > 0) {
                int name_part = available_width / 2;
                int target_part = available_width - name_part - 4;
                mvwprintw(window, *line_num, 2 + level * 2, "%s %.*s -> %.*s...",
                          emoji, name_part, entries[i].name, target_part, symlink_target);
            } else {
                mvwprintw(window, *line_num, 2 + level * 2, "%s %.*s",
                          emoji, available_width, entries[i].name);
            }
        } else {
            if (is_symlink && strlen(symlink_target) > 0) {
                mvwprintw(window, *line_num, 2 + level * 2, "%s %s -> %s",
                          emoji, entries[i].name, symlink_target);
            } else {
                mvwprintw(window, *line_num, 2 + level * 2, "%s %.*s",
                          emoji, available_width, entries[i].name);
            }
        }

        char perm[10];
        snprintf(perm, sizeof(perm), "%o", entries[i].mode & 0777);
        mvwprintw(window, *line_num, max_x - 10, "%s", perm);
        (*line_num)++;
        (*current_count)++;
        if (*current_count >= DIRECTORY_TREE_MAX_TOTAL) {
            tree_limit_hit = true;
            break;
        }

        if (entries[i].is_dir &&
            *line_num < max_y - 1 &&
            level < DIRECTORY_TREE_MAX_DEPTH) {
            if (dir_path_len + name_len + 2 <= MAX_PATH_LENGTH) {
                show_directory_tree(window, full_path, level + 1, line_num, max_y, max_x, start_line, current_count);
                if (tree_limit_hit) {
                    break;
                }
            }
        }
    }

    if (magic_cookie) {
        magic_close(magic_cookie);
    }

    if (level == 0 && tree_limit_hit && *line_num < max_y - 1) {
        mvwprintw(window, *line_num, 2, "[Preview truncated]");
        (*line_num)++;
    }
}

int get_total_lines(const char *file_path) {
    FILE *file = fopen(file_path, "r");
    if (!file) return 0;

    int total_lines = 0;
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        total_lines++;
    }

    fclose(file);
    return total_lines;
}

int get_preview_total_lines(const char *file_path, int content_width, int content_height) {
    if (!file_path || !*file_path) return 0;

    struct stat file_stat;
    if (stat(file_path, &file_stat) == 0 && S_ISDIR(file_stat.st_mode)) {
        return get_directory_tree_total_lines(file_path);
    }

    if (is_image_file(file_path)) {
        cupidimage_image img = {0};
        char err[256] = {0};
        if (load_cupidimage_image(file_path, &img, err, sizeof(err))) {
            int target_w = 0;
            int target_h = 0;
            if (supports_truecolor()) {
                int ansi_max_h = (int)((double)content_height / image_cell_aspect + 0.5);
                if (ansi_max_h < 1) ansi_max_h = 1;
                compute_ansi_scaled_dims(img.width, img.height, content_width, ansi_max_h, &target_w, &target_h);
            } else {
                scale_image_to_fit(img.width, img.height, content_width, content_height, &target_w, &target_h);
            }
            cupidimage_free(&img);
            return target_h;
        }
        return 0;
    }

    return get_total_lines(file_path);
}

void draw_directory_window(WINDOW *window,
                           const char *directory,
                           Vector *files_vector,
                           CursorAndSlice *cas) {
    int cols;
    int rows;
    getmaxyx(window, rows, cols);

    size_t vec_len = files_vector ? Vector_len(*files_vector) : 0;
    cas->num_files = (SIZE)vec_len;
    if (cas->num_files < 0) cas->num_files = 0;
    if (cas->start < 0) cas->start = 0;
    if (cas->cursor < 0) cas->cursor = 0;
    if (cas->num_files == 0) {
        cas->start = 0;
        cas->cursor = 0;
    } else {
        if (cas->start >= cas->num_files) cas->start = 0;
        if (cas->cursor >= cas->num_files) cas->cursor = cas->num_files - 1;
    }

    werase(window);
    box(window, 0, 0);

    if (cas->num_files == 0) {
        mvwprintw(window, 1, 1, "This directory is empty");
        wrefresh(window);
        return;
    }

    int max_visible_items = rows - 2;

    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE);
    if (magic_cookie == NULL || magic_load(magic_cookie, NULL) != 0) {
        for (int i = 0; i < max_visible_items && (cas->start + i) < cas->num_files; i++) {
            FileAttr fa = (FileAttr)files_vector->el[cas->start + i];
            const char *name = FileAttr_get_name(fa);

            char full_path[MAX_PATH_LENGTH];
            path_join(full_path, directory, name);

            struct stat statbuf;
            bool is_symlink = (lstat(full_path, &statbuf) == 0 && S_ISLNK(statbuf.st_mode));
            char symlink_target[MAX_PATH_LENGTH] = {0};

            if (is_symlink) {
                ssize_t target_len = readlink(full_path, symlink_target, sizeof(symlink_target) - 1);
                if (target_len > 0) {
                    symlink_target[target_len] = '\0';
                }
            }

            const char *emoji = FileAttr_is_dir(fa) ? "📁" : "📄";
            const char *git_emoji = git_status_to_emoji(FileAttr_get_git_status(fa));

            wmove(window, i + 1, 1);
            for (int j = 1; j < cols - 1; j++) {
                waddch(window, ' ');
            }

            bool is_cursor = ((cas->start + i) == cas->cursor);
            bool is_selected = g_select_all_highlight || is_cursor;
            if (is_selected) wattron(window, A_REVERSE);
            if (g_select_all_highlight && is_cursor) wattron(window, A_BOLD);

            int name_len = (int)strlen(name);
            int target_len = is_symlink ? (int)strlen(symlink_target) : 0;
            int total_len = name_len + (is_symlink ? (4 + target_len) : 0);
            int available_width = cols - 8;

            if (total_len > available_width) {
                if (is_symlink && target_len > 0) {
                    int name_part = available_width / 2;
                    int target_part = available_width - name_part - 4;
                    mvwprintw(window, i + 1, 1, "%s %s %.*s -> %.*s...", emoji, git_emoji,
                              name_part, name, target_part, symlink_target);
                } else {
                    mvwprintw(window, i + 1, 1, "%s %s %.*s", emoji, git_emoji, available_width, name);
                }
            } else {
                if (is_symlink && target_len > 0) {
                    mvwprintw(window, i + 1, 1, "%s %s %s -> %s", emoji, git_emoji, name, symlink_target);
                } else {
                    mvwprintw(window, i + 1, 1, "%s %s %s", emoji, git_emoji, name);
                }
            }

            if (g_select_all_highlight && is_cursor) wattroff(window, A_BOLD);
            if (is_selected) wattroff(window, A_REVERSE);
        }

        wrefresh(window);
        return;
    }

    for (int i = 0; i < max_visible_items && (cas->start + i) < cas->num_files; i++) {
        FileAttr fa = (FileAttr)files_vector->el[cas->start + i];
        const char *name = FileAttr_get_name(fa);

        char full_path[MAX_PATH_LENGTH];
        path_join(full_path, directory, name);

        struct stat statbuf;
        bool is_symlink = (lstat(full_path, &statbuf) == 0 && S_ISLNK(statbuf.st_mode));
        char symlink_target[MAX_PATH_LENGTH] = {0};

        if (is_symlink) {
            ssize_t target_len = readlink(full_path, symlink_target, sizeof(symlink_target) - 1);
            if (target_len > 0) {
                symlink_target[target_len] = '\0';
            }
        }

        const char *emoji;
        if (FileAttr_is_dir(fa)) {
            emoji = "📁";
        } else {
            const char *mime_type = magic_file(magic_cookie, full_path);
            emoji = get_file_emoji(mime_type, name);
        }
        const char *git_emoji = git_status_to_emoji(FileAttr_get_git_status(fa));

        wmove(window, i + 1, 1);
        for (int j = 1; j < cols - 1; j++) {
            waddch(window, ' ');
        }

        bool is_cursor = ((cas->start + i) == cas->cursor);
        bool is_selected = g_select_all_highlight || is_cursor;
        if (is_selected) wattron(window, A_REVERSE);
        if (g_select_all_highlight && is_cursor) wattron(window, A_BOLD);

        int name_len = (int)strlen(name);
        int target_len = is_symlink ? (int)strlen(symlink_target) : 0;
        int total_len = name_len + (is_symlink ? (4 + target_len) : 0);
        int available_width = cols - 8;

        if (total_len > available_width) {
            if (is_symlink && target_len > 0) {
                int name_part = available_width / 2;
                int target_part = available_width - name_part - 4;
                mvwprintw(window, i + 1, 1, "%s %s %.*s -> %.*s...", emoji, git_emoji,
                          name_part, name, target_part, symlink_target);
            } else {
                mvwprintw(window, i + 1, 1, "%s %s %.*s", emoji, git_emoji, available_width, name);
            }
        } else {
            if (is_symlink && target_len > 0) {
                mvwprintw(window, i + 1, 1, "%s %s %s -> %s", emoji, git_emoji, name, symlink_target);
            } else {
                mvwprintw(window, i + 1, 1, "%s %s %s", emoji, git_emoji, name);
            }
        }

        if (g_select_all_highlight && is_cursor) wattroff(window, A_BOLD);
        if (is_selected) wattroff(window, A_REVERSE);
    }

    magic_close(magic_cookie);
    wrefresh(window);
}

static const char *basename_ptr_local(const char *p) {
    if (!p) return "";
    size_t len = strlen(p);
    while (len > 0 && p[len - 1] == '/') len--;
    if (len == 0) return "";
    const char *end = p + len;
    const char *slash = memrchr(p, '/', (size_t)(end - p));
    return slash ? (slash + 1) : p;
}

void draw_preview_window_path(WINDOW *window, const char *full_path, const char *display_name, int start_line) {
    // Clear the window and draw border
    werase(window);
    box(window, 0, 0);

    int max_y, max_x;
    getmaxyx(window, max_y, max_x);

    if (!full_path || !*full_path) {
        mvwprintw(window, 1, 2, "No file selected");
        wrefresh(window);
        return;
    }

    const char *name = (display_name && *display_name) ? display_name : basename_ptr_local(full_path);
    mvwprintw(window, 1, 2, "Preview: %s", name);

    struct stat file_stat;
    if (stat(full_path, &file_stat) == -1) {
        mvwprintw(window, 2, 2, "Unable to retrieve file information");
        wrefresh(window);
        return;
    }

    char fileSizeStr[64];
    if (S_ISDIR(file_stat.st_mode)) {
        static char last_preview_size_path[MAX_PATH_LENGTH] = "";
        static struct timespec last_preview_size_change = {0};
        static bool last_preview_size_initialized = false;

        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);

        bool path_changed = !last_preview_size_initialized ||
                            strncmp(last_preview_size_path, full_path, MAX_PATH_LENGTH) != 0;
        if (path_changed) {
            strncpy(last_preview_size_path, full_path, MAX_PATH_LENGTH - 1);
            last_preview_size_path[MAX_PATH_LENGTH - 1] = '\0';
            last_preview_size_change = now;
            last_preview_size_initialized = true;
        }

        long elapsed_ns = (now.tv_sec - last_preview_size_change.tv_sec) * 1000000000L +
                          (now.tv_nsec - last_preview_size_change.tv_nsec);
        bool allow_enqueue = (elapsed_ns >= DIR_SIZE_REQUEST_DELAY_NS) && dir_size_can_enqueue();

        long dir_size = allow_enqueue ? get_directory_size(full_path)
                                      : get_directory_size_peek(full_path);
        if (dir_size == -1) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Error");
        } else if (dir_size == DIR_SIZE_VIRTUAL_FS) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Virtual FS");
        } else if (dir_size == DIR_SIZE_TOO_LARGE) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Too large");
        } else if (dir_size == DIR_SIZE_PERMISSION_DENIED) {
            snprintf(fileSizeStr, sizeof(fileSizeStr), "Permission denied");
        } else if (dir_size == DIR_SIZE_PENDING) {
            long p = dir_size_get_progress(full_path);
            if (p > 0) {
                char tmp[32];
                format_file_size(tmp, (size_t)p);
                snprintf(fileSizeStr, sizeof(fileSizeStr), "Calculating... %s", tmp);
            } else {
                snprintf(fileSizeStr, sizeof(fileSizeStr), allow_enqueue ? "Calculating..." : "Waiting...");
            }
        } else {
            format_file_size(fileSizeStr, (size_t)dir_size);
        }
        mvwprintw(window, 2, 2, "📁 Directory Size: %s", fileSizeStr);
    } else {
        format_file_size(fileSizeStr, (size_t)file_stat.st_size);
        mvwprintw(window, 2, 2, "📏 File Size: %s", fileSizeStr);
    }

    char permissions[10];
    snprintf(permissions, sizeof(permissions), "%o", file_stat.st_mode & 0777);
    mvwprintw(window, 3, 2, "🔒 Permissions: %s", permissions);

    char modTime[50];
    strftime(modTime, sizeof(modTime), "%c", localtime(&file_stat.st_mtime));
    mvwprintw(window, 4, 2, "🕒 Last Modified: %s", modTime);

    bool is_image = false;
    bool used_truecolor = false;
    magic_t magic_cookie = magic_open(MAGIC_MIME_TYPE);
    if (magic_cookie != NULL && magic_load(magic_cookie, NULL) == 0) {
        const char *mime_type = magic_file(magic_cookie, full_path);
        if (mime_type && strncmp(mime_type, "image/", 6) == 0) {
            is_image = true;
        }
        mvwprintw(window, 5, 2, "MIME Type: %s", mime_type ? mime_type : "Unknown");
        magic_close(magic_cookie);
    } else {
        if (magic_cookie) {
            magic_close(magic_cookie);
        }
        mvwprintw(window, 5, 2, "MIME Type: Unable to detect");
    }
    if (!is_image) {
        is_image = is_image_extension(full_path);
    }

    /* Clear any previous graphics when switching away from images */
    if (!is_image) {
        /* Clear Sixel graphics if we had a Sixel image displayed */
        if (last_sixel_image_path[0] != '\0') {
            clear_sixel_overlay(window, max_y, max_x);
            last_sixel_image_path[0] = '\0';
        }
        /* Clear Kitty graphics if we had a Kitty image displayed */
        if (last_kitty_image_path[0] != '\0') {
            cupidimage_kitty_delete_all(stdout, NULL, 0);
            fflush(stdout);
            last_kitty_image_path[0] = '\0';
        }
        /* Clear truecolor overlay if active */
        if (last_truecolor_active) {
            clear_truecolor_overlay(window, max_y, max_x);
            last_truecolor_active = false;
        }
    }

    if (S_ISDIR(file_stat.st_mode)) {
        int line_num = 7;
        int current_count = 0;
        show_directory_tree(window, full_path, 0, &line_num, max_y, max_x, start_line, &current_count);
    } else if (is_archive_file(full_path)) {
        display_archive_preview(window, full_path, start_line, max_y, max_x);
    } else if (is_image) {
        if (cupidimage_is_kitty_terminal()) {
            draw_image_preview_kitty(window, full_path, start_line, max_y, max_x);
            used_truecolor = true;
        } else if (cupidimage_is_sixel_terminal()) {
            draw_image_preview_sixel(window, full_path, start_line, max_y, max_x);
            used_truecolor = true;
        } else if (supports_truecolor()) {
            draw_image_preview_truecolor(window, full_path, start_line, max_y, max_x);
            used_truecolor = true;
        } else {
            draw_image_preview(window, full_path, start_line, max_y, max_x);
        }
    } else if (is_supported_file_type(full_path)) {
        FILE *file = fopen(full_path, "r");
        if (file) {
            // Read all lines into a buffer first to enable proper block comment state tracking
            char **all_lines = NULL;
            int total_lines = 0;
            int capacity = 100;
            all_lines = malloc(capacity * sizeof(char*));
            if (!all_lines) {
                fclose(file);
                mvwprintw(window, 7, 2, "Memory allocation failed");
                wrefresh(window);
                return;
            }
            
            char line_buffer[256];
            while (fgets(line_buffer, sizeof(line_buffer), file)) {
                if (total_lines >= capacity) {
                    capacity *= 2;
                    char **new_lines = realloc(all_lines, capacity * sizeof(char*));
                    if (!new_lines) {
                        for (int i = 0; i < total_lines; i++) free(all_lines[i]);
                        free(all_lines);
                        fclose(file);
                        mvwprintw(window, 7, 2, "Memory allocation failed");
                        wrefresh(window);
                        return;
                    }
                    all_lines = new_lines;
                }
                line_buffer[strcspn(line_buffer, "\n")] = '\0';
                all_lines[total_lines] = strdup(line_buffer);
                if (!all_lines[total_lines]) {
                    for (int i = 0; i < total_lines; i++) free(all_lines[i]);
                    free(all_lines);
                    fclose(file);
                    mvwprintw(window, 7, 2, "Memory allocation failed");
                    wrefresh(window);
                    return;
                }
                total_lines++;
            }
            fclose(file);
            
            // Get syntax definition for this file
            SyntaxDef *syntax = syntax_get_for_file(full_path);
            
            // IMPORTANT: Compute block comment state for the first visible line AFTER determining start_line
            int in_block_comment = 0;
            if (syntax && start_line > 0) {
                in_block_comment = get_initial_block_comment_state(all_lines, total_lines, start_line, syntax);
            }
            
            // Display lines starting from start_line
            int line_num = 7;
            for (int i = start_line; i < total_lines && line_num < max_y - 1; i++) {
                char line[256];
                strncpy(line, all_lines[i], sizeof(line) - 1);
                line[sizeof(line) - 1] = '\0';
                
                // Clean up non-printable characters
                for (char *p = line; *p; p++) {
                    unsigned char c = (unsigned char)*p;
                    if (c == '\t') {
                        *p = ' ';
                    } else if (isspace(c) && c != ' ') {
                        *p = ' ';
                    } else if (!isprint(c)) {
                        *p = ' ';
                    }
                }

                // Use syntax highlighting if available, otherwise plain text
                // Pass full buffer context for proper state tracking
                syntax_highlight_line(window, line, syntax, &in_block_comment, 
                                     line_num++, 2, max_x - 4, all_lines, total_lines, i);
            }
            
            // Free all lines
            for (int i = 0; i < total_lines; i++) {
                free(all_lines[i]);
            }
            free(all_lines);

            if (line_num < max_y - 1) {
                mvwprintw(window, line_num++, 2, "--------------------------------");
                mvwprintw(window, line_num++, 2, "[End of file]");
            }
        } else {
            mvwprintw(window, 7, 2, "Unable to open file for preview");
        }
    } else {
        mvwprintw(window, 7, 2, "No preview available");
    }

    if (!used_truecolor) {
        wrefresh(window);
    }
}

void draw_preview_window(WINDOW *window, const char *current_directory, const char *selected_entry, int start_line) {
    if (selected_entry == NULL || selected_entry[0] == '\0') {
        draw_preview_window_path(window, NULL, NULL, start_line);
        return;
    }

    char file_path[MAX_PATH_LENGTH];
    path_join(file_path, current_directory, selected_entry);
    draw_preview_window_path(window, file_path, selected_entry, start_line);
}

void fix_cursor(CursorAndSlice *cas) {
    cas->cursor = MIN(cas->cursor, cas->num_files - 1);
    cas->cursor = MAX(0, cas->cursor);

    int visible_lines = cas->num_lines - 2;

    if (cas->num_files <= visible_lines) {
        cas->start = 0;
        return;
    }

    if (cas->cursor < cas->start) {
        cas->start = cas->cursor;
    } else if (cas->cursor >= cas->start + visible_lines) {
        cas->start = cas->cursor - visible_lines + 1;
    }

    int max_start = cas->num_files - visible_lines;
    if (max_start < 0) max_start = 0;
    cas->start = MIN(cas->start, max_start);
    cas->start = MAX(0, cas->start);

    int cursor_relative_pos = cas->cursor - cas->start;
    if (cursor_relative_pos < 0 || cursor_relative_pos >= visible_lines) {
        if (cursor_relative_pos < 0) {
            cas->start = cas->cursor;
        } else {
            cas->start = cas->cursor - visible_lines + 1;
            if (cas->start < 0) cas->start = 0;
            if (cas->start > max_start) cas->start = max_start;
        }
    }
}
