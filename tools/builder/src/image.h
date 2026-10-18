#ifndef TB_IMAGE_H
#define TB_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#include "util.h"

#define TEX_SCALE 0.5625
#define MAX_TEX 512

typedef struct {
    int w;
    int h;
    uint8_t* px;
} Image;

typedef enum { FILTER_BOX, FILTER_LANCZOS } Filter;

Image* img_new(int w, int h);
void img_free(Image* img);
Image* img_clone(const Image* img);
Image* img_load_png(const uint8_t* data, size_t len);
Image* img_crop(const Image* img, int x0, int y0, int x1, int y1);
void img_paste(Image* dst, const Image* src, int x, int y);
void img_fill_rect(Image* img, int x0, int y0, int x1, int y1, uint32_t rgba);
void img_alpha_composite(Image* dst, const Image* src, int x, int y);
void img_premultiply(Image* img);
Image* img_resize(const Image* img, int w, int h, Filter filter);
Image* img_resize_box(const Image* img, int w, int h, Filter filter, float x0, float y0, float x1, float y1);

void tex_scaled_size(int w, int h, int* ow, int* oh);
int tex_encode(const Image* premul, int orig_w, int orig_h, Buf* out);
int tex_from_png(const uint8_t* png, size_t len, int cells_x, int cells_y, Buf* out);
int tex_from_image(const Image* straight, int cells_x, int cells_y, Buf* out);
int png_encode(const Image* img, int with_alpha, Buf* out);

#endif
