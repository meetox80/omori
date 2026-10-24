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
    *ow = py_round(w * g_target->tex_scale);
    *oh = py_round(h * g_target->tex_scale);
    if (*ow < 1) *ow = 1;
    if (*oh < 1) *oh = 1;
}

static int pow2_at_least(int n) {
    int p = 8;
    while (p < n) p <<= 1;
    return p;
}

static int split_sizes(int n, int block, int* out) {
    int c = 0;
    int max = g_target->max_tex;
    if (block > 0 && block <= max && n > block && n % block == 0 && n / block <= 64) {
        for (; c < n / block; c++) out[c] = block;
        return c;
    }
    while (n > 0) {
        if (g_target->layout == TEX_PICA) {
            if (n >= max) {
                out[c++] = max;
                n -= max;
                continue;
            }
            int p = pow2_at_least(n);
            if (p <= 128 || p - n <= n / 4) {
                out[c++] = n;
                break;
            }
            out[c++] = p / 2;
            n -= p / 2;
            continue;
        }
        if (n <= max) {
            out[c++] = n;
            break;
        }
        out[c++] = max;
        n -= max;
    }
    return c;
}

static void chunk_extent(int pw, int ph, int* stride, int* rows) {
    switch (g_target->layout) {
        case TEX_PSP:
            *stride = (pw + 15) / 16 * 16;
            *rows = (ph + 7) / 8 * 8;
            break;
        case TEX_PICA:
            *stride = pow2_at_least(pw);
            *rows = pow2_at_least(ph);
            break;
        case TEX_GX:
            *stride = (pw + 3) / 4 * 4;
            *rows = (ph + 3) / 4 * 4;
            break;
        case TEX_GS:
            *stride = (pw + 3) / 4 * 4;
            *rows = ph;
            break;
        default:
            *stride = (pw + 15) / 16 * 16;
            *rows = ph;
            break;
    }
}

static int morton8(int x, int y) {
    int r = 0;
    for (int i = 0; i < 3; i++) r |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1);
    return r;
}

static void put_texel(uint8_t* raw, int stride, int rows, int c, int r, const uint8_t* s) {
    switch (g_target->layout) {
        case TEX_PSP: {
            size_t bw = (size_t)stride * 4, col = (size_t)c * 4;
            uint8_t* d = raw + (((size_t)r / 8 * (bw / 16) + col / 16) * 8 + (size_t)r % 8) * 16 + col % 16;
            memcpy(d, s, 4);
            break;
        }
        case TEX_PICA: {
            int row = rows - 1 - r;
            uint8_t* d = raw + (((size_t)(row >> 3) * (size_t)(stride >> 3) + (size_t)(c >> 3)) * 64 + (size_t)morton8(c & 7, row & 7)) * 4;
            d[0] = s[3];
            d[1] = s[2];
            d[2] = s[1];
            d[3] = s[0];
            break;
        }
        case TEX_GX: {
            uint8_t* tile = raw + ((size_t)(r >> 2) * (size_t)(stride >> 2) + (size_t)(c >> 2)) * 64;
            size_t at = (size_t)((r & 3) * 4 + (c & 3)) * 2;
            tile[at] = s[3];
            tile[at + 1] = s[0];
            tile[32 + at] = s[1];
            tile[32 + at + 1] = s[2];
            break;
        }
        case TEX_ARGB: {
            uint8_t* d = raw + ((size_t)r * stride + c) * 4;
            d[0] = s[3];
            d[1] = s[0];
            d[2] = s[1];
            d[3] = s[2];
            break;
        }
        case TEX_GS: {
            uint8_t* d = raw + ((size_t)r * stride + c) * 4;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = (uint8_t)((s[3] * 128 + 127) / 255);
            break;
        }
        default: memcpy(raw + ((size_t)r * stride + c) * 4, s, 4); break;
    }
}

typedef struct {
    int x, y, pw, ph, stride, rows;
    Buf comp;
    uint32_t raw_len;
} Chunk;

static Image* to_straight(const Image* pm) {
    int w = pm->w, h = pm->h;
    size_t n = (size_t)w * (size_t)h;
    Image* st = img_clone(pm);
    uint8_t* known = xcalloc(n, 1);
    for (size_t i = 0; i < n; i++) {
        uint8_t* p = st->px + i * 4;
        int a = p[3];
        if (!a) continue;
        known[i] = 1;
        for (int c = 0; c < 3; c++) {
            int v = (p[c] * 255 + a / 2) / a;
            p[c] = (uint8_t)(v > 255 ? 255 : v);
        }
    }
    uint8_t* next = xmalloc(n);
    for (int pass = 0; pass < 4; pass++) {
        memcpy(next, known, n);
        int changed = 0;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                size_t i = (size_t)y * w + x;
                if (known[i]) continue;
                int sum[3] = {0, 0, 0}, cnt = 0;
                static const int dx[4] = {-1, 1, 0, 0}, dy[4] = {0, 0, -1, 1};
                for (int k = 0; k < 4; k++) {
                    int nx = x + dx[k], ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    size_t j = (size_t)ny * w + nx;
                    if (!known[j]) continue;
                    for (int c = 0; c < 3; c++) sum[c] += st->px[j * 4 + c];
                    cnt++;
                }
                if (!cnt) continue;
                for (int c = 0; c < 3; c++) st->px[i * 4 + c] = (uint8_t)((sum[c] + cnt / 2) / cnt);
                next[i] = 1;
                changed = 1;
            }
        }
        memcpy(known, next, n);
        if (!changed) break;
    }
    free(next);
    free(known);
    return st;
}

static void screen_frames(const Image* pm, int orig_w, int orig_h, int* bw, int* bh) {
    *bw = *bh = 0;
    if (g_target->layout != TEX_GS || orig_w % 640 || orig_h % 480 || (orig_w == 640 && orig_h == 480)) return;
    int fw, fh;
    tex_scaled_size(640, 480, &fw, &fh);
    if (pm->w != orig_w / 640 * fw || pm->h != orig_h / 480 * fh) return;
    *bw = fw;
    *bh = fh;
}

#define T8_MIN_BYTES (512u << 10)
#define T8_ROUNDS 6
#define T8_CHUNK 256

typedef struct {
    uint32_t c;
    uint32_t n;
} Tally;

typedef struct {
    int lo, hi, axis, range;
} TallyBox;

static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static int dither_q(int v, int max, int d) { return (v * max * 32 + (2 * d + 1) * 255) / (255 * 32); }

static int imin(int a, int b) { return a < b ? a : b; }

static uint64_t packed_error(const Image* pm, int mode) {
    uint64_t err = 0;
    for (int y = 0; y < pm->h; y++) {
        for (int x = 0; x < pm->w; x++) {
            const uint8_t* s = pm->px + ((size_t)y * pm->w + x) * 4;
            int d = BAYER[((y & 3) << 2) | (x & 3)];
            int r = s[0], g = s[1], b = s[2], a = s[3], o[4];
            if (mode == 0) {
                int r5 = dither_q(r, 31, d), g6 = dither_q(g, 63, d), b5 = dither_q(b, 31, d);
                o[0] = (r5 << 3) | (r5 >> 2);
                o[1] = (g6 << 2) | (g6 >> 4);
                o[2] = (b5 << 3) | (b5 >> 2);
                o[3] = 255;
            } else if (mode == 1) {
                if (a * 32 <= (2 * d + 1) * 255) {
                    o[0] = o[1] = o[2] = o[3] = 0;
                } else {
                    if (a < 255) {
                        r = imin(255, r * 255 / a);
                        g = imin(255, g * 255 / a);
                        b = imin(255, b * 255 / a);
                    }
                    int r5 = dither_q(r, 31, d), g5 = dither_q(g, 31, d), b5 = dither_q(b, 31, d);
                    o[0] = (r5 << 3) | (r5 >> 2);
                    o[1] = (g5 << 3) | (g5 >> 2);
                    o[2] = (b5 << 3) | (b5 >> 2);
                    o[3] = 255;
                }
            } else {
                int a4 = (a * 15 + 127) / 255;
                o[0] = imin(dither_q(r, 15, d), a4) * 17;
                o[1] = imin(dither_q(g, 15, d), a4) * 17;
                o[2] = imin(dither_q(b, 15, d), a4) * 17;
                o[3] = a4 * 17;
            }
            for (int c = 0; c < 4; c++) {
                int e = o[c] - s[c];
                err += (uint64_t)(e * e);
            }
        }
    }
    return err;
}

static int nearest_entry(const int* pal, int count, uint32_t c) {
    int r = (int)(c & 255), g = (int)((c >> 8) & 255), b = (int)((c >> 16) & 255), a = (int)(c >> 24);
    int best = 0, best_d = 0x7FFFFFFF;
    for (int i = 0; i < count; i++) {
        const int* p = pal + i * 4;
        int dr = r - p[0], dg = g - p[1], db = b - p[2], da = a - p[3];
        int d = dr * dr + dg * dg + db * db + da * da;
        if (d < best_d) {
            best_d = d;
            best = i;
            if (!d) break;
        }
    }
    return best;
}

static void box_measure(const Tally* t, TallyBox* b) {
    int lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0};
    for (int i = b->lo; i < b->hi; i++) {
        for (int ch = 0; ch < 4; ch++) {
            int v = (int)((t[i].c >> (ch * 8)) & 255);
            if (v < lo[ch]) lo[ch] = v;
            if (v > hi[ch]) hi[ch] = v;
        }
    }
    b->axis = 0;
    b->range = -1;
    for (int ch = 0; ch < 4; ch++) {
        if (hi[ch] - lo[ch] > b->range) {
            b->range = hi[ch] - lo[ch];
            b->axis = ch;
        }
    }
    if (b->hi - b->lo < 2) b->range = -1;
}

static void sort_channel(Tally* t, Tally* tmp, int lo, int hi, int ch) {
    size_t pos[257] = {0};
    for (int i = lo; i < hi; i++) pos[((t[i].c >> (ch * 8)) & 255) + 1]++;
    for (int v = 0; v < 256; v++) pos[v + 1] += pos[v];
    for (int i = lo; i < hi; i++) tmp[pos[(t[i].c >> (ch * 8)) & 255]++] = t[i];
    memcpy(t + lo, tmp, sizeof(Tally) * (size_t)(hi - lo));
}

static int cmp_u32(const void* a, const void* b);

static uint8_t* t8_indices(const Image* pm, int mode, uint32_t* pal_out) {
    size_t n = (size_t)pm->w * pm->h;
    uint32_t* all = xmalloc(n * sizeof(uint32_t));
    for (size_t i = 0; i < n; i++) {
        const uint8_t* p = pm->px + i * 4;
        all[i] = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }
    qsort(all, n, sizeof(uint32_t), cmp_u32);
    Tally* tally = xmalloc(n * sizeof(Tally));
    int m = 0;
    for (size_t i = 0; i < n; i++) {
        if (m && tally[m - 1].c == all[i]) tally[m - 1].n++;
        else tally[m++] = (Tally){all[i], 1};
    }
    free(all);
    int pal[256 * 4];
    int count = 0;
    if (m <= 256) {
        for (int i = 0; i < m; i++) {
            for (int ch = 0; ch < 4; ch++) pal[count * 4 + ch] = (int)((tally[i].c >> (ch * 8)) & 255);
            count++;
        }
    } else {
        int fixed = tally[0].c == 0;
        if (fixed) {
            pal[0] = pal[1] = pal[2] = pal[3] = 0;
            count = 1;
        }
        int lo = fixed, want = 256 - fixed;
        Tally* work = xmalloc(sizeof(Tally) * (size_t)m);
        Tally* tmp = xmalloc(sizeof(Tally) * (size_t)m);
        memcpy(work, tally, sizeof(Tally) * (size_t)m);
        TallyBox boxes[256];
        boxes[0] = (TallyBox){lo, m, 0, 0};
        box_measure(work, &boxes[0]);
        int nb = 1;
        while (nb < want) {
            int pick = -1;
            for (int k = 0; k < nb; k++) {
                if (boxes[k].range > 0 && (pick < 0 || boxes[k].range > boxes[pick].range)) pick = k;
            }
            if (pick < 0) break;
            TallyBox b = boxes[pick];
            sort_channel(work, tmp, b.lo, b.hi, b.axis);
            uint64_t total = 0, acc = 0;
            for (int i = b.lo; i < b.hi; i++) total += work[i].n;
            int mid = b.lo + 1;
            for (int i = b.lo; i < b.hi - 1; i++) {
                acc += work[i].n;
                mid = i + 1;
                if (acc * 2 >= total) break;
            }
            boxes[pick] = (TallyBox){b.lo, mid, 0, 0};
            boxes[nb] = (TallyBox){mid, b.hi, 0, 0};
            box_measure(work, &boxes[pick]);
            box_measure(work, &boxes[nb]);
            nb++;
        }
        for (int k = 0; k < nb; k++) {
            uint64_t sum[4] = {0, 0, 0, 0}, w = 0;
            for (int i = boxes[k].lo; i < boxes[k].hi; i++) {
                for (int ch = 0; ch < 4; ch++) sum[ch] += (uint64_t)((work[i].c >> (ch * 8)) & 255) * work[i].n;
                w += work[i].n;
            }
            for (int ch = 0; ch < 4; ch++) pal[count * 4 + ch] = (int)((sum[ch] + w / 2) / w);
            count++;
        }
        free(work);
        free(tmp);
        uint64_t (*sums)[5] = xmalloc(sizeof(uint64_t[5]) * 256);
        for (int round = 0; round < T8_ROUNDS; round++) {
            memset(sums, 0, sizeof(uint64_t[5]) * 256);
            for (int i = lo; i < m; i++) {
                int k = nearest_entry(pal, count, tally[i].c);
                for (int ch = 0; ch < 4; ch++) sums[k][ch] += (uint64_t)((tally[i].c >> (ch * 8)) & 255) * tally[i].n;
                sums[k][4] += tally[i].n;
            }
            for (int k = fixed; k < count; k++) {
                if (!sums[k][4]) continue;
                for (int ch = 0; ch < 4; ch++) pal[k * 4 + ch] = (int)((sums[k][ch] + sums[k][4] / 2) / sums[k][4]);
            }
        }
        free(sums);
    }
    uint8_t* map = xmalloc((size_t)m);
    uint64_t err = 0;
    for (int i = 0; i < m; i++) {
        int k = nearest_entry(pal, count, tally[i].c);
        map[i] = (uint8_t)k;
        for (int ch = 0; ch < 4; ch++) {
            int e = (int)((tally[i].c >> (ch * 8)) & 255) - pal[k * 4 + ch];
            err += (uint64_t)(e * e) * tally[i].n;
        }
    }
    uint8_t* idx = NULL;
    if (err <= packed_error(pm, mode)) {
        idx = xmalloc(n);
        for (size_t i = 0; i < n; i++) {
            const uint8_t* p = pm->px + i * 4;
            uint32_t c = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
            int lo = 0, hi = m - 1;
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (tally[mid].c < c) lo = mid + 1;
                else hi = mid;
            }
            idx[i] = map[lo];
        }
        for (int k = 0; k < 256; k++) {
            pal_out[k] = k < count ? (uint32_t)pal[k * 4] | (uint32_t)pal[k * 4 + 1] << 8 | (uint32_t)pal[k * 4 + 2] << 16 | (uint32_t)pal[k * 4 + 3] << 24
                                   : 0;
        }
    }
    free(map);
    free(tally);
    return idx;
}

static int tex_encode_blocks(const Image* pm, int orig_w, int orig_h, int bw, int bh, Buf* out) {
    int ws[64], hs[64];
    int nw = split_sizes(pm->w, bw, ws);
    int nh = split_sizes(pm->h, bh, hs);
    int nchunks = nw * nh;
    Chunk* chunks = xcalloc((size_t)nchunks, sizeof(Chunk));
    Image* straight = g_target->layout == TEX_GS ? to_straight(pm) : NULL;
    const Image* src = straight ? straight : pm;
    int ci = 0;
    int y = 0;
    for (int j = 0; j < nh; j++) {
        int ph = hs[j];
        int x = 0;
        for (int i = 0; i < nw; i++) {
            int pw = ws[i];
            int stride, rows;
            chunk_extent(pw, ph, &stride, &rows);
            size_t raw_len = (size_t)stride * 4 * (size_t)rows;
            uint8_t* raw = xmalloc(raw_len);
            for (int r = 0; r < rows; r++) {
                int sy = y + (r < ph ? r : ph - 1);
                for (int c = 0; c < stride; c++) {
                    int sx = x + (c < pw ? c : pw - 1);
                    put_texel(raw, stride, rows, c, r, src->px + ((size_t)sy * src->w + sx) * 4);
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
                img_free(straight);
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
    img_free(straight);
    int opaque = 1, hard = 1;
    size_t visible = 0, soft = 0;
    for (size_t i = 0, n = (size_t)pm->w * pm->h; i < n; i++) {
        uint8_t a = pm->px[i * 4 + 3];
        if (a != 255) opaque = 0;
        if (a != 0 && a != 255) hard = 0;
        if (a >= 8) visible++;
        if (a >= 8 && a <= 247) soft++;
    }
    int edged = !opaque && !hard && soft * 100 <= visible * 15;
    size_t packed_bytes = 0;
    for (int k = 0; k < nchunks; k++) packed_bytes += chunks[k].raw_len / 2;
    uint32_t pal[256];
    uint8_t* idx = g_target->layout == TEX_PSP && packed_bytes >= T8_MIN_BYTES ? t8_indices(pm, opaque ? 0 : (hard || edged) ? 1 : 2, pal) : NULL;
    Chunk* alt = NULL;
    int nalt = 0;
    if (idx) {
        int nx = (pm->w + T8_CHUNK - 1) / T8_CHUNK, ny = (pm->h + T8_CHUNK - 1) / T8_CHUNK;
        nalt = nx * ny;
        alt = xcalloc((size_t)nalt, sizeof(Chunk));
        for (int k = 0; k < nalt && alt; k++) {
            Chunk* ch = &alt[k];
            ch->x = (k % nx) * T8_CHUNK;
            ch->y = (k / nx) * T8_CHUNK;
            ch->pw = imin(T8_CHUNK, pm->w - ch->x);
            ch->ph = imin(T8_CHUNK, pm->h - ch->y);
            chunk_extent(ch->pw, ch->ph, &ch->stride, &ch->rows);
            ch->raw_len = (uint32_t)ch->stride * (uint32_t)ch->rows;
            uint8_t* raw = xmalloc(ch->raw_len);
            for (int r = 0; r < ch->rows; r++) {
                int sy = ch->y + (r < ch->ph ? r : ch->ph - 1);
                for (int c = 0; c < ch->stride; c++) {
                    int sx = ch->x + (c < ch->pw ? c : ch->pw - 1);
                    raw[((size_t)(r / 8) * (size_t)(ch->stride / 16) + (size_t)(c / 16)) * 128 + (size_t)(r % 8) * 16 + (size_t)(c % 16)] = idx[(size_t)sy * pm->w + sx];
                }
            }
            mz_ulong clen = mz_compressBound((mz_ulong)ch->raw_len);
            buf_reserve(&ch->comp, clen);
            if (mz_compress2(ch->comp.data, &clen, raw, (mz_ulong)ch->raw_len, 6) == MZ_OK) {
                ch->comp.len = clen;
            } else {
                for (int j = 0; j < nalt; j++) buf_free(&alt[j].comp);
                free(alt);
                alt = NULL;
            }
            free(raw);
        }
        free(idx);
    }
    size_t start = out->len;
    buf_put(out, "TTEX", 4);
    buf_u8(out, (uint8_t)(g_target->layout == TEX_PICA ? 2 : 1));
    buf_u8(out, 0);
    buf_u8(out, (uint8_t)((g_target->layout == TEX_LINEAR ? 0 : 1) | (opaque ? 4 : 0) | (hard ? 8 : 0) | (edged ? 16 : 0) | (alt ? 32 : 0)));
    buf_u8(out, 0);
    buf_u16(out, (uint16_t)pm->w);
    buf_u16(out, (uint16_t)pm->h);
    buf_u16(out, (uint16_t)orig_w);
    buf_u16(out, (uint16_t)orig_h);
    buf_u16(out, (uint16_t)nchunks);
    buf_u16(out, 0);
    uint32_t offset = (uint32_t)(out->len - start) + (uint32_t)nchunks * 24 + (alt ? 4 : 0);
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
    if (alt) buf_u32(out, offset);
    for (int k = 0; k < nchunks; k++) {
        buf_put(out, chunks[k].comp.data, chunks[k].comp.len);
        buf_free(&chunks[k].comp);
    }
    if (alt) {
        buf_u16(out, (uint16_t)nalt);
        buf_u16(out, 0);
        for (int k = 0; k < 256; k++) buf_u32(out, pal[k]);
        uint32_t at = offset + 4 + 1024 + (uint32_t)nalt * 24;
        for (int k = 0; k < nalt; k++) {
            Chunk* ch = &alt[k];
            buf_u16(out, (uint16_t)ch->x);
            buf_u16(out, (uint16_t)ch->y);
            buf_u16(out, (uint16_t)ch->pw);
            buf_u16(out, (uint16_t)ch->ph);
            buf_u16(out, (uint16_t)ch->stride);
            buf_u16(out, (uint16_t)ch->rows);
            buf_u32(out, at);
            buf_u32(out, (uint32_t)ch->comp.len);
            buf_u32(out, ch->raw_len);
            at += (uint32_t)ch->comp.len;
        }
        for (int k = 0; k < nalt; k++) {
            buf_put(out, alt[k].comp.data, alt[k].comp.len);
            buf_free(&alt[k].comp);
        }
        free(alt);
    }
    free(chunks);
    return 1;
}

int tex_encode(const Image* pm, int orig_w, int orig_h, Buf* out) {
    int bw, bh;
    screen_frames(pm, orig_w, orig_h, &bw, &bh);
    return tex_encode_blocks(pm, orig_w, orig_h, bw, bh, out);
}

static int cell_blocks(const Image* pm, int cells_x, int cells_y, int* bw, int* bh) {
    *bw = *bh = 0;
    if (cells_x <= 0 || cells_y <= 0 || cells_x * cells_y < 2 || pm->w % cells_x || pm->h % cells_y) return 0;
    int cw = pm->w / cells_x, ch = pm->h / cells_y, stride, rows;
    chunk_extent(cw, ch, &stride, &rows);
    if ((double)stride * rows > (double)cw * ch * 1.08) return 0;
    *bw = cw;
    *bh = ch;
    return 1;
}

int tex_encode_font(const Image* pm, Buf* out) {
    size_t n = (size_t)pm->w * pm->h, raw_len = n * 2;
    uint8_t* raw = xmalloc(raw_len ? raw_len : 1);
    for (size_t i = 0; i < n; i++) {
        raw[i * 2] = pm->px[i * 4];
        raw[i * 2 + 1] = pm->px[i * 4 + 3];
    }
    mz_ulong clen = mz_compressBound((mz_ulong)raw_len);
    Buf comp = {0};
    buf_reserve(&comp, clen);
    int ok = mz_compress2(comp.data, &clen, raw, (mz_ulong)raw_len, 9) == MZ_OK;
    free(raw);
    if (!ok) {
        buf_free(&comp);
        return 0;
    }
    size_t start = out->len;
    buf_put(out, "TTEX", 4);
    buf_u8(out, 1);
    buf_u8(out, TEX_FORMAT_FONT);
    buf_u8(out, 0);
    buf_u8(out, 0);
    buf_u16(out, (uint16_t)pm->w);
    buf_u16(out, (uint16_t)pm->h);
    buf_u16(out, (uint16_t)pm->w);
    buf_u16(out, (uint16_t)pm->h);
    buf_u16(out, 1);
    buf_u16(out, 0);
    buf_u16(out, 0);
    buf_u16(out, 0);
    buf_u16(out, (uint16_t)pm->w);
    buf_u16(out, (uint16_t)pm->h);
    buf_u16(out, (uint16_t)pm->w);
    buf_u16(out, (uint16_t)pm->h);
    buf_u32(out, (uint32_t)(out->len - start) + 12);
    buf_u32(out, (uint32_t)clen);
    buf_u32(out, (uint32_t)raw_len);
    buf_put(out, comp.data, clen);
    buf_free(&comp);
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

int tex_from_image(const Image* straight, int cells_x, int cells_y, int cell_chunks, Buf* out) {
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
    int bw, bh;
    int ok = cell_chunks && cell_blocks(res, cells_x, cells_y, &bw, &bh) ? tex_encode_blocks(res, straight->w, straight->h, bw, bh, out)
                                                                          : tex_encode(res, straight->w, straight->h, out);
    img_free(res);
    return ok;
}

int tex_from_png(const uint8_t* png, size_t len, int cells_x, int cells_y, Buf* out) {
    Image* img = img_load_png(png, len);
    if (!img) return 0;
    int ok = tex_from_image(img, cells_x, cells_y, 0, out);
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

typedef struct {
    uint32_t rgb;
    uint32_t count;
} ColorCount;

typedef struct {
    int lo, hi;
} ColorBox;

static int cmp_u32(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? -1 : x > y;
}

static int g_sort_shift;

static int cmp_channel(const void* a, const void* b) {
    int x = (int)((((const ColorCount*)a)->rgb >> g_sort_shift) & 255), y = (int)((((const ColorCount*)b)->rgb >> g_sort_shift) & 255);
    return x - y;
}

static int box_axis(const ColorCount* c, ColorBox b, int* range) {
    int best = 0, best_range = -1;
    for (int ch = 0; ch < 3; ch++) {
        int lo = 255, hi = 0;
        for (int i = b.lo; i < b.hi; i++) {
            int v = (int)((c[i].rgb >> (ch * 8)) & 255);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        if (hi - lo > best_range) {
            best_range = hi - lo;
            best = ch;
        }
    }
    *range = best_range;
    return best;
}

static int build_palette(const Image* img, uint32_t* pal) {
    size_t n = (size_t)img->w * img->h;
    uint32_t* all = xmalloc(n * sizeof(uint32_t));
    for (size_t i = 0; i < n; i++) all[i] = (uint32_t)img->px[i * 4] | (uint32_t)img->px[i * 4 + 1] << 8 | (uint32_t)img->px[i * 4 + 2] << 16;
    qsort(all, n, sizeof(uint32_t), cmp_u32);
    ColorCount* cc = xmalloc(n * sizeof(ColorCount));
    int unique = 0;
    for (size_t i = 0; i < n; i++) {
        if (unique && cc[unique - 1].rgb == all[i]) cc[unique - 1].count++;
        else cc[unique++] = (ColorCount){all[i], 1};
    }
    free(all);
    int count = 0;
    if (unique <= 256) {
        for (int i = 0; i < unique; i++) pal[count++] = cc[i].rgb;
        free(cc);
        return count;
    }
    ColorBox boxes[256];
    boxes[0] = (ColorBox){0, unique};
    count = 1;
    while (count < 256) {
        int pick = -1, pick_range = 0, pick_axis = 0;
        for (int k = 0; k < count; k++) {
            if (boxes[k].hi - boxes[k].lo < 2) continue;
            int range;
            int axis = box_axis(cc, boxes[k], &range);
            if (range > pick_range) {
                pick = k;
                pick_range = range;
                pick_axis = axis;
            }
        }
        if (pick < 0) break;
        ColorBox b = boxes[pick];
        g_sort_shift = pick_axis * 8;
        qsort(cc + b.lo, (size_t)(b.hi - b.lo), sizeof(ColorCount), cmp_channel);
        uint64_t total = 0, acc = 0;
        for (int i = b.lo; i < b.hi; i++) total += cc[i].count;
        int mid = b.lo + 1;
        for (int i = b.lo; i < b.hi - 1; i++) {
            acc += cc[i].count;
            mid = i + 1;
            if (acc * 2 >= total) break;
        }
        boxes[pick] = (ColorBox){b.lo, mid};
        boxes[count++] = (ColorBox){mid, b.hi};
    }
    for (int k = 0; k < count; k++) {
        uint64_t sum[3] = {0, 0, 0}, w = 0;
        for (int i = boxes[k].lo; i < boxes[k].hi; i++) {
            for (int ch = 0; ch < 3; ch++) sum[ch] += (uint64_t)((cc[i].rgb >> (ch * 8)) & 255) * cc[i].count;
            w += cc[i].count;
        }
        pal[k] = (uint32_t)((sum[0] + w / 2) / w) | (uint32_t)((sum[1] + w / 2) / w) << 8 | (uint32_t)((sum[2] + w / 2) / w) << 16;
    }
    free(cc);
    return count;
}

static int nearest(const uint32_t* pal, int count, int r, int g, int b) {
    int best = 0, best_d = 0x7FFFFFFF;
    for (int i = 0; i < count; i++) {
        int dr = r - (int)(pal[i] & 255), dg = g - (int)((pal[i] >> 8) & 255), db = b - (int)((pal[i] >> 16) & 255);
        int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (d < best_d) {
            best_d = d;
            best = i;
            if (!d) break;
        }
    }
    return best;
}

static void put_be32(Buf* b, uint32_t v) {
    uint8_t x[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
    buf_put(b, x, 4);
}

static void png_chunk(Buf* out, const char* type, const uint8_t* data, size_t len) {
    put_be32(out, (uint32_t)len);
    size_t start = out->len;
    buf_put(out, type, 4);
    if (len) buf_put(out, data, len);
    put_be32(out, (uint32_t)mz_crc32(MZ_CRC32_INIT, out->data + start, len + 4));
}

int png_encode_indexed(const Image* img, Buf* out) {
    uint32_t pal[256];
    int count = build_palette(img, pal);
    int w = img->w, h = img->h;
    size_t row = (size_t)w + 1;
    uint8_t* idx = xcalloc(row * (size_t)h, 1);
    float* err = xcalloc((size_t)(w + 2) * 2 * 3, sizeof(float));
    for (int y = 0; y < h; y++) {
        float* cur = err + (size_t)(y & 1) * (w + 2) * 3;
        float* nxt = err + (size_t)((y + 1) & 1) * (w + 2) * 3;
        memset(nxt, 0, (size_t)(w + 2) * 3 * sizeof(float));
        for (int x = 0; x < w; x++) {
            const uint8_t* p = img->px + ((size_t)y * w + x) * 4;
            int c[3];
            for (int ch = 0; ch < 3; ch++) {
                int v = (int)lrintf((float)p[ch] + cur[(x + 1) * 3 + ch]);
                c[ch] = v < 0 ? 0 : v > 255 ? 255 : v;
            }
            int k = nearest(pal, count, c[0], c[1], c[2]);
            idx[(size_t)y * row + 1 + x] = (uint8_t)k;
            for (int ch = 0; ch < 3; ch++) {
                float e = (float)(c[ch] - (int)((pal[k] >> (ch * 8)) & 255));
                cur[(x + 2) * 3 + ch] += e * 7.0f / 16.0f;
                nxt[x * 3 + ch] += e * 3.0f / 16.0f;
                nxt[(x + 1) * 3 + ch] += e * 5.0f / 16.0f;
                nxt[(x + 2) * 3 + ch] += e * 1.0f / 16.0f;
            }
        }
    }
    free(err);
    mz_ulong cap = mz_compressBound((mz_ulong)(row * h));
    uint8_t* z = xmalloc(cap);
    mz_ulong zlen = cap;
    int ok = mz_compress2(z, &zlen, idx, (mz_ulong)(row * h), 9) == MZ_OK;
    free(idx);
    if (ok) {
        static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        buf_put(out, sig, 8);
        Buf ihdr = {0};
        put_be32(&ihdr, (uint32_t)w);
        put_be32(&ihdr, (uint32_t)h);
        const uint8_t tail[5] = {8, 3, 0, 0, 0};
        buf_put(&ihdr, tail, 5);
        png_chunk(out, "IHDR", ihdr.data, ihdr.len);
        buf_free(&ihdr);
        uint8_t plte[768];
        for (int i = 0; i < count; i++) {
            plte[i * 3] = (uint8_t)(pal[i] & 255);
            plte[i * 3 + 1] = (uint8_t)((pal[i] >> 8) & 255);
            plte[i * 3 + 2] = (uint8_t)((pal[i] >> 16) & 255);
        }
        png_chunk(out, "PLTE", plte, (size_t)count * 3);
        png_chunk(out, "IDAT", z, zlen);
        png_chunk(out, "IEND", NULL, 0);
    }
    free(z);
    return ok;
}
