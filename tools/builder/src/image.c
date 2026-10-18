#include "image.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "../third_party/miniz.h"

#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "../third_party/stb_image.h"

#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

#define TB_PI 3.14159265358979323846
#define PRECISION_BITS (32 - 8 - 2)

Image* img_new(int w, int h) {
    Image* img = xmalloc(sizeof(Image));
    img->w = w;
    img->h = h;
    img->px = xcalloc((size_t)(w > 0 ? w : 1) * (size_t)(h > 0 ? h : 1), 4);
    return img;
}

void img_free(Image* img) {
    if (!img) return;
    free(img->px);
    free(img);
}

Image* img_clone(const Image* img) {
    Image* c = img_new(img->w, img->h);
    memcpy(c->px, img->px, (size_t)img->w * (size_t)img->h * 4);
    return c;
}

Image* img_load_png(const uint8_t* data, size_t len) {
    int w, h, n;
    unsigned char* p = stbi_load_from_memory(data, (int)len, &w, &h, &n, 4);
    if (!p) return NULL;
    Image* img = xmalloc(sizeof(Image));
    img->w = w;
    img->h = h;
    img->px = xmalloc((size_t)w * (size_t)h * 4);
    memcpy(img->px, p, (size_t)w * (size_t)h * 4);
    stbi_image_free(p);
    return img;
}

Image* img_crop(const Image* img, int x0, int y0, int x1, int y1) {
    int w = x1 - x0, h = y1 - y0;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    Image* out = img_new(w, h);
    for (int y = 0; y < h; y++) {
        int sy = y0 + y;
        if (sy < 0 || sy >= img->h) continue;
        for (int x = 0; x < w; x++) {
            int sx = x0 + x;
            if (sx < 0 || sx >= img->w) continue;
            memcpy(out->px + ((size_t)y * w + x) * 4, img->px + ((size_t)sy * img->w + sx) * 4, 4);
        }
    }
    return out;
}

void img_paste(Image* dst, const Image* src, int x, int y) {
    for (int j = 0; j < src->h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= dst->h) continue;
        int i0 = x < 0 ? -x : 0;
        int i1 = src->w;
        if (x + i1 > dst->w) i1 = dst->w - x;
        if (i1 <= i0) continue;
        memcpy(dst->px + ((size_t)dy * dst->w + x + i0) * 4, src->px + ((size_t)j * src->w + i0) * 4, (size_t)(i1 - i0) * 4);
    }
}

void img_fill_rect(Image* img, int x0, int y0, int x1, int y1, uint32_t rgba) {
    uint8_t c[4] = {(uint8_t)(rgba >> 24), (uint8_t)(rgba >> 16), (uint8_t)(rgba >> 8), (uint8_t)rgba};
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > img->w) x1 = img->w;
    if (y1 > img->h) y1 = img->h;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) memcpy(img->px + ((size_t)y * img->w + x) * 4, c, 4);
    }
}

#define SHIFTFORDIV255(a) ((((a) >> 8) + (a)) >> 8)

void img_alpha_composite(Image* dst, const Image* src, int x, int y) {
    const uint32_t pb = 7;
    for (int j = 0; j < src->h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= dst->h) continue;
        for (int i = 0; i < src->w; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= dst->w) continue;
            const uint8_t* s = src->px + ((size_t)j * src->w + i) * 4;
            uint8_t* d = dst->px + ((size_t)dy * dst->w + dx) * 4;
            if (s[3] == 0) continue;
            uint32_t sa = s[3];
            uint32_t blend = (uint32_t)d[3] * (255 - sa);
            uint32_t outa255 = sa * 255 + blend;
            uint32_t coef1 = sa * 255 * 255 * (1u << pb) / outa255;
            uint32_t coef2 = 255 * (1u << pb) - coef1;
            uint32_t tr = s[0] * coef1 + d[0] * coef2;
            uint32_t tg = s[1] * coef1 + d[1] * coef2;
            uint32_t tbv = s[2] * coef1 + d[2] * coef2;
            d[0] = (uint8_t)(SHIFTFORDIV255(tr + (0x80u << pb)) >> pb);
            d[1] = (uint8_t)(SHIFTFORDIV255(tg + (0x80u << pb)) >> pb);
            d[2] = (uint8_t)(SHIFTFORDIV255(tbv + (0x80u << pb)) >> pb);
            d[3] = (uint8_t)SHIFTFORDIV255(outa255 + 0x80);
        }
    }
}

void img_premultiply(Image* img) {
    size_t n = (size_t)img->w * (size_t)img->h;
    for (size_t i = 0; i < n; i++) {
        uint8_t* p = img->px + i * 4;
        unsigned a = p[3];
        for (int c = 0; c < 3; c++) {
            unsigned t = p[c] * a + 128;
            p[c] = (uint8_t)(((t >> 8) + t) >> 8);
        }
    }
}

typedef struct {
    double (*fn)(double);
    double support;
} FilterDef;

static double box_filter(double x) {
    if (x > -0.5 && x <= 0.5) return 1.0;
    return 0.0;
}

static double sinc_filter(double x) {
    if (x == 0.0) return 1.0;
    x = x * TB_PI;
    return sin(x) / x;
}

static double lanczos_filter(double x) {
    if (-3.0 <= x && x < 3.0) return sinc_filter(x) * sinc_filter(x / 3);
    return 0.0;
}

static const FilterDef FILTERS[2] = {{box_filter, 0.5}, {lanczos_filter, 3.0}};

static int precompute_coeffs(int in_size, float in0, float in1, int out_size, const FilterDef* f, int** boundsp, double** kkp) {
    double scale, filterscale;
    filterscale = scale = (double)(in1 - in0) / out_size;
    if (filterscale < 1.0) filterscale = 1.0;
    double support = f->support * filterscale;
    int ksize = (int)ceil(support) * 2 + 1;
    double* kk = xmalloc((size_t)out_size * (size_t)ksize * sizeof(double));
    int* bounds = xmalloc((size_t)out_size * 2 * sizeof(int));
    for (int xx = 0; xx < out_size; xx++) {
        double center = in0 + (xx + 0.5) * scale;
        double ww = 0.0;
        double ss = 1.0 / filterscale;
        int xmin = (int)(center - support + 0.5);
        if (xmin < 0) xmin = 0;
        int xmax = (int)(center + support + 0.5);
        if (xmax > in_size) xmax = in_size;
        xmax -= xmin;
        double* k = &kk[(size_t)xx * ksize];
        int x;
        for (x = 0; x < xmax; x++) {
            double w = f->fn((x + xmin - center + 0.5) * ss);
            k[x] = w;
            ww += w;
        }
        for (x = 0; x < xmax; x++) {
            if (ww != 0.0) k[x] /= ww;
        }
        for (; x < ksize; x++) k[x] = 0;
        bounds[xx * 2 + 0] = xmin;
        bounds[xx * 2 + 1] = xmax;
    }
    *boundsp = bounds;
    *kkp = kk;
    return ksize;
}

static int32_t* normalize_coeffs(int out_size, int ksize, const double* prekk) {
    size_t n = (size_t)out_size * (size_t)ksize;
    int32_t* kk = xmalloc(n * sizeof(int32_t));
    for (size_t x = 0; x < n; x++) {
        if (prekk[x] < 0) kk[x] = (int32_t)(-0.5 + prekk[x] * (1 << PRECISION_BITS));
        else kk[x] = (int32_t)(0.5 + prekk[x] * (1 << PRECISION_BITS));
    }
    return kk;
}

static inline uint8_t clip8(int32_t in) {
    int32_t v = in >> PRECISION_BITS;
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

static void resample_h(Image* out, const Image* in, int offset, int ksize, const int* bounds, const double* prekk) {
    int32_t* kk = normalize_coeffs(out->w, ksize, prekk);
    for (int yy = 0; yy < out->h; yy++) {
        const uint8_t* row = in->px + (size_t)(yy + offset) * in->w * 4;
        uint8_t* orow = out->px + (size_t)yy * out->w * 4;
        for (int xx = 0; xx < out->w; xx++) {
            int xmin = bounds[xx * 2 + 0];
            int xmax = bounds[xx * 2 + 1];
            const int32_t* k = &kk[(size_t)xx * ksize];
            int32_t s0, s1, s2, s3;
            s0 = s1 = s2 = s3 = 1 << (PRECISION_BITS - 1);
            for (int x = 0; x < xmax; x++) {
                const uint8_t* p = row + (size_t)(x + xmin) * 4;
                s0 += p[0] * k[x];
                s1 += p[1] * k[x];
                s2 += p[2] * k[x];
                s3 += p[3] * k[x];
            }
            orow[xx * 4 + 0] = clip8(s0);
            orow[xx * 4 + 1] = clip8(s1);
            orow[xx * 4 + 2] = clip8(s2);
            orow[xx * 4 + 3] = clip8(s3);
        }
    }
    free(kk);
}

static void resample_v(Image* out, const Image* in, int ksize, const int* bounds, const double* prekk) {
    int32_t* kk = normalize_coeffs(out->h, ksize, prekk);
    for (int yy = 0; yy < out->h; yy++) {
        const int32_t* k = &kk[(size_t)yy * ksize];
        int ymin = bounds[yy * 2 + 0];
        int ymax = bounds[yy * 2 + 1];
        uint8_t* orow = out->px + (size_t)yy * out->w * 4;
        for (int xx = 0; xx < out->w; xx++) {
            int32_t s0, s1, s2, s3;
            s0 = s1 = s2 = s3 = 1 << (PRECISION_BITS - 1);
            for (int y = 0; y < ymax; y++) {
                const uint8_t* p = in->px + ((size_t)(y + ymin) * in->w + xx) * 4;
                s0 += p[0] * k[y];
                s1 += p[1] * k[y];
                s2 += p[2] * k[y];
                s3 += p[3] * k[y];
            }
            orow[xx * 4 + 0] = clip8(s0);
            orow[xx * 4 + 1] = clip8(s1);
            orow[xx * 4 + 2] = clip8(s2);
            orow[xx * 4 + 3] = clip8(s3);
        }
    }
    free(kk);
}

Image* img_resize_box(const Image* img, int xsize, int ysize, Filter filter, float x0, float y0, float x1, float y1) {
    if (xsize == img->w && ysize == img->h && x0 == 0 && y0 == 0 && x1 == xsize && y1 == ysize) return img_clone(img);
    const FilterDef* f = &FILTERS[filter == FILTER_LANCZOS ? 1 : 0];
    int need_h = xsize != img->w || x0 != 0 || x1 != xsize;
    int need_v = ysize != img->h || y0 != 0 || y1 != ysize;
    int *bh, *bv;
    double *kh, *kv;
    int ksh = precompute_coeffs(img->w, x0, x1, xsize, f, &bh, &kh);
    int ksv = precompute_coeffs(img->h, y0, y1, ysize, f, &bv, &kv);
    int yfirst = bv[0];
    int ylast = bv[ysize * 2 - 2] + bv[ysize * 2 - 1];
    Image* temp = NULL;
    const Image* cur = img;
    Image* out = NULL;
    if (need_h) {
        for (int i = 0; i < ysize; i++) bv[i * 2] -= yfirst;
        temp = img_new(xsize, ylast - yfirst);
        resample_h(temp, img, yfirst, ksh, bh, kh);
        cur = temp;
        out = temp;
    }
    if (need_v) {
        out = img_new(cur->w, ysize);
        resample_v(out, cur, ksv, bv, kv);
        img_free(temp);
    }
    free(bh);
    free(kh);
    free(bv);
    free(kv);
    if (!out) out = img_clone(cur);
    return out;
}

Image* img_resize(const Image* img, int w, int h, Filter filter) {
    return img_resize_box(img, w, h, filter, 0.0f, 0.0f, (float)img->w, (float)img->h);
}

static int py_round(double v) { return (int)rint(v); }

void tex_scaled_size(int w, int h, int* ow, int* oh) {
    *ow = py_round(w * TEX_SCALE);
    *oh = py_round(h * TEX_SCALE);
    if (*ow < 1) *ow = 1;
    if (*oh < 1) *oh = 1;
}

static int split_sizes(int n, int* out) {
    int c = 0;
    while (n > 0) {
        if (n <= MAX_TEX) {
            out[c++] = n;
            break;
        }
        out[c++] = MAX_TEX;
        n -= MAX_TEX;
    }
    return c;
}

typedef struct {
    int x, y, pw, ph, stride, rows;
    Buf comp;
    uint32_t raw_len;
} Chunk;

int tex_encode(const Image* pm, int orig_w, int orig_h, Buf* out) {
    int ws[64], hs[64];
    int nw = split_sizes(pm->w, ws);
    int nh = split_sizes(pm->h, hs);
    int nchunks = nw * nh;
    Chunk* chunks = xcalloc((size_t)nchunks, sizeof(Chunk));
    int ci = 0;
    int y = 0;
    for (int j = 0; j < nh; j++) {
        int ph = hs[j];
        int x = 0;
        for (int i = 0; i < nw; i++) {
            int pw = ws[i];
            int stride = (pw + 15) / 16 * 16;
            int rows = (ph + 7) / 8 * 8;
            size_t bw = (size_t)stride * 4;
            size_t raw_len = bw * (size_t)rows;
            uint8_t* raw = xmalloc(raw_len);
            for (int r = 0; r < rows; r++) {
                int sy = y + (r < ph ? r : ph - 1);
                for (int c = 0; c < stride; c++) {
                    int sx = x + (c < pw ? c : pw - 1);
                    const uint8_t* s = pm->px + ((size_t)sy * pm->w + sx) * 4;
                    size_t lin = (size_t)r * bw + (size_t)c * 4;
                    size_t row_in_block = lin / bw % 8;
                    size_t col_byte = lin % bw;
                    size_t by = (size_t)r / 8;
                    size_t bx = col_byte / 16;
                    size_t off = ((by * (bw / 16) + bx) * 8 + row_in_block) * 16 + col_byte % 16;
                    memcpy(raw + off, s, 4);
                }
            }
            mz_ulong cap = mz_compressBound((mz_ulong)raw_len);
            Chunk* ch = &chunks[ci++];
            buf_reserve(&ch->comp, cap);
            mz_ulong clen = cap;
            if (mz_compress2(ch->comp.data, &clen, raw, (mz_ulong)raw_len, 6) != MZ_OK) {
                free(raw);
                for (int k = 0; k < ci; k++) buf_free(&chunks[k].comp);
                free(chunks);
                return 0;
            }
            ch->comp.len = clen;
            ch->x = x;
            ch->y = y;
            ch->pw = pw;
            ch->ph = ph;
            ch->stride = stride;
            ch->rows = rows;
            ch->raw_len = (uint32_t)raw_len;
            free(raw);
            x += pw;
        }
        y += ph;
    }
    size_t start = out->len;
    buf_put(out, "TTEX", 4);
    buf_u8(out, 1);
    buf_u8(out, 0);
    buf_u8(out, 1);
    buf_u8(out, 0);
    buf_u16(out, (uint16_t)pm->w);
    buf_u16(out, (uint16_t)pm->h);
    buf_u16(out, (uint16_t)orig_w);
    buf_u16(out, (uint16_t)orig_h);
    buf_u16(out, (uint16_t)nchunks);
    buf_u16(out, 0);
    uint32_t offset = (uint32_t)(out->len - start) + (uint32_t)nchunks * 24;
    for (int k = 0; k < nchunks; k++) {
        Chunk* ch = &chunks[k];
        buf_u16(out, (uint16_t)ch->x);
        buf_u16(out, (uint16_t)ch->y);
        buf_u16(out, (uint16_t)ch->pw);
        buf_u16(out, (uint16_t)ch->ph);
        buf_u16(out, (uint16_t)ch->stride);
        buf_u16(out, (uint16_t)ch->rows);
        buf_u32(out, offset);
        buf_u32(out, (uint32_t)ch->comp.len);
        buf_u32(out, ch->raw_len);
        offset += (uint32_t)ch->comp.len;
    }
    for (int k = 0; k < nchunks; k++) {
        buf_put(out, chunks[k].comp.data, chunks[k].comp.len);
        buf_free(&chunks[k].comp);
    }
    free(chunks);
    return 1;
}

static Image* resize_cells(const Image* img, int cols, int rows) {
    int w = img->w, h = img->h;
    double cw = (double)w / cols, chh = (double)h / rows;
    int out_w, out_h;
    tex_scaled_size(w, h, &out_w, &out_h);
    Image* out = img_new(out_w, out_h);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int x0 = py_round(c * cw), y0 = py_round(r * chh);
            int x1 = py_round((c + 1) * cw), y1 = py_round((r + 1) * chh);
            int ox0 = py_round((double)x0 * out_w / w), oy0 = py_round((double)y0 * out_h / h);
            int ox1 = py_round((double)x1 * out_w / w), oy1 = py_round((double)y1 * out_h / h);
            if (ox1 <= ox0 || oy1 <= oy0) continue;
            Image* cell = img_crop(img, x0, y0, x1, y1);
            img_premultiply(cell);
            Image* rs = img_resize(cell, ox1 - ox0, oy1 - oy0, FILTER_LANCZOS);
            img_paste(out, rs, ox0, oy0);
            img_free(rs);
            img_free(cell);
        }
    }
    return out;
}

int tex_from_image(const Image* straight, int cells_x, int cells_y, Buf* out) {
    Image* res;
    if (cells_x > 0 && cells_y > 0) {
        res = resize_cells(straight, cells_x, cells_y);
    } else {
        int sw, sh;
        tex_scaled_size(straight->w, straight->h, &sw, &sh);
        Image* pm = img_clone(straight);
        img_premultiply(pm);
        if (pm->w != sw || pm->h != sh) {
            res = img_resize(pm, sw, sh, FILTER_LANCZOS);
            img_free(pm);
        } else {
            res = pm;
        }
    }
    int ok = tex_encode(res, straight->w, straight->h, out);
    img_free(res);
    return ok;
}

int tex_from_png(const uint8_t* png, size_t len, int cells_x, int cells_y, Buf* out) {
    Image* img = img_load_png(png, len);
    if (!img) return 0;
    int ok = tex_from_image(img, cells_x, cells_y, out);
    img_free(img);
    return ok;
}

static void png_write_cb(void* ctx, void* data, int size) { buf_put((Buf*)ctx, data, (size_t)size); }

int png_encode(const Image* img, int with_alpha, Buf* out) {
    if (with_alpha) return stbi_write_png_to_func(png_write_cb, out, img->w, img->h, 4, img->px, img->w * 4) != 0;
    size_t n = (size_t)img->w * (size_t)img->h;
    uint8_t* rgb = xmalloc(n * 3);
    for (size_t i = 0; i < n; i++) memcpy(rgb + i * 3, img->px + i * 4, 3);
    int ok = stbi_write_png_to_func(png_write_cb, out, img->w, img->h, 3, rgb, img->w * 3) != 0;
    free(rgb);
    return ok;
}
