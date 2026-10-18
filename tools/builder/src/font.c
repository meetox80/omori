#include "font.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image.h"
#include "node.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "../third_party/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#define SUPER 4
#define PHASES 3
#define ATLAS_W 512

static const double SCALE = TEX_SCALE;
static const double OUTLINE_LOGICAL = 4.0;
static const int OUTLINE_ALPHA = 255;
static const int EMBOLDEN = 1;

typedef struct {
    uint32_t cp;
    int phase;
    int w;
    int h;
    int ox;
    int oy;
    uint8_t* px;
    int order;
    int ax;
    int ay;
} Cell;

extern const unsigned char fallback_noto_sans[];
extern const size_t fallback_noto_sans_size;
extern const unsigned char fallback_noto_symbols2[];
extern const size_t fallback_noto_symbols2_size;
extern const unsigned char fallback_noto_math[];
extern const size_t fallback_noto_math_size;
extern const unsigned char fallback_noto_arabic[];
extern const size_t fallback_noto_arabic_size;
extern const unsigned char fallback_noto_sc[];
extern const size_t fallback_noto_sc_size;

#define NFALLBACK 5

static int load_fallbacks(stbtt_fontinfo* out) {
    const unsigned char* data[NFALLBACK] = {fallback_noto_sans, fallback_noto_symbols2, fallback_noto_math, fallback_noto_arabic, fallback_noto_sc};
    const size_t sizes[NFALLBACK] = {fallback_noto_sans_size, fallback_noto_symbols2_size, fallback_noto_math_size, fallback_noto_arabic_size,
                                     fallback_noto_sc_size};
    int n = 0;
    for (int i = 0; i < NFALLBACK; i++) {
        if (!sizes[i]) continue;
        if (stbtt_InitFont(&out[n], data[i], stbtt_GetFontOffsetForIndex(data[i], 0))) n++;
    }
    return n;
}

static int is_default_ignorable(uint32_t cp) {
    return cp == 0xAD || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) || (cp >= 0x2060 && cp <= 0x206F) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF;
}

static int floor_div(int a, int b) {
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

static int py_round(double v) { return (int)nearbyint(v); }

static int cmp_u32(const void* a, const void* b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;
    return x < y ? -1 : x > y;
}

static int cmp_cell(const void* a, const void* b) {
    const Cell* x = *(const Cell* const*)a;
    const Cell* y = *(const Cell* const*)b;
    if (x->h != y->h) return y->h - x->h;
    return x->order - y->order;
}

static void dilate(const uint8_t* src, uint8_t* dst, int w, int h, int r) {
    int n = 0;
    int offs[512][3];
    for (int dy = -r - 1; dy <= r + 1; dy++) {
        for (int dx = -r - 1; dx <= r + 1; dx++) {
            double wgt = r + 1.0 - sqrt((double)(dx * dx + dy * dy));
            if (wgt <= 0 || n >= 512) continue;
            offs[n][0] = dx;
            offs[n][1] = dy;
            offs[n][2] = wgt >= 1 ? 256 : (int)(wgt * 256 + 0.5);
            n++;
        }
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int m = 0;
            for (int k = 0; k < n; k++) {
                int sx = x + offs[k][0], sy = y + offs[k][1];
                if (sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
                int v = (src[sy * w + sx] * offs[k][2] + 128) >> 8;
                if (v > m) m = v;
            }
            dst[y * w + x] = (uint8_t)m;
        }
    }
}

static void box_down(const uint8_t* src, int w, int h, uint8_t* dst) {
    int sw = w / SUPER, sh = h / SUPER;
    uint8_t* tmp = xmalloc((size_t)sw * (size_t)h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < sw; x++) {
            int s = 0;
            for (int k = 0; k < SUPER; k++) s += src[y * w + x * SUPER + k];
            tmp[y * sw + x] = (uint8_t)((s + SUPER / 2) / SUPER);
        }
    }
    for (int y = 0; y < sh; y++) {
        for (int x = 0; x < sw; x++) {
            int s = 0;
            for (int k = 0; k < SUPER; k++) s += tmp[(y * SUPER + k) * sw + x];
            dst[y * sw + x] = (uint8_t)((s + SUPER / 2) / SUPER);
        }
    }
    free(tmp);
}

static void draw_glyph(const stbtt_fontinfo* info, float sc, int glyph, double ox, double baseline, uint8_t* buf, int w, int h) {
    double fx = floor(ox);
    float shift = (float)(ox - fx);
    int ix0, iy0, ix1, iy1;
    stbtt_GetGlyphBitmapBoxSubpixel(info, glyph, sc, sc, shift, 0, &ix0, &iy0, &ix1, &iy1);
    int gw = ix1 - ix0, gh = iy1 - iy0;
    if (gw <= 0 || gh <= 0) return;
    uint8_t* g = xcalloc((size_t)gw * (size_t)gh, 1);
    stbtt_MakeGlyphBitmapSubpixel(info, g, gw, gh, gw, sc, sc, shift, 0, glyph);
    int bx = (int)fx + ix0;
    int by = (int)floor(baseline) + iy0;
    for (int y = 0; y < gh; y++) {
        int ty = by + y;
        if (ty < 0 || ty >= h) continue;
        for (int x = 0; x < gw; x++) {
            int tx = bx + x;
            if (tx < 0 || tx >= w) continue;
            uint8_t v = g[y * gw + x];
            if (v > buf[ty * w + tx]) buf[ty * w + tx] = v;
        }
    }
    free(g);
}

static uint8_t muldiv255(int a, int b) {
    int t = a * b + 128;
    return (uint8_t)(((t >> 8) + t) >> 8);
}

int font_build(const uint8_t* ttf, size_t ttf_len, int logical_size, int outline, const uint32_t* codepoints, int count, Buf* tex, Buf* fnt) {
    (void)ttf_len;
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) return 0;
    double px_size = logical_size * SCALE * 1.0;
    int big = py_round(px_size * SUPER);
    if (big < 1) big = 1;
    float sc = stbtt_ScaleForMappingEmToPixels(&info, (float)big);
    int stroke = outline ? py_round(OUTLINE_LOGICAL * SCALE * 1.0 * SUPER / 2) : 0;
    if (outline && stroke < 1) stroke = 1;
    int asc_u, desc_u, gap_u;
    stbtt_GetFontVMetrics(&info, &asc_u, &desc_u, &gap_u);
    int upem = (int)ttUSHORT(info.data + info.head + 18);
    int ascent = (int)ceil((double)asc_u * big / upem);
    int descent = (int)ceil((double)-desc_u * big / upem);
    int pad = stroke + EMBOLDEN + 2;
    int sw_total = stroke + EMBOLDEN;

    uint32_t* cps = xmalloc(sizeof(uint32_t) * (size_t)(count ? count : 1));
    memcpy(cps, codepoints, sizeof(uint32_t) * (size_t)count);
    qsort(cps, (size_t)count, sizeof(uint32_t), cmp_u32);
    int n = 0;
    for (int i = 0; i < count; i++) {
        if (n == 0 || cps[n - 1] != cps[i]) cps[n++] = cps[i];
    }

    double* advs = xmalloc(sizeof(double) * (size_t)(n ? n : 1));
    Cell* cells = xcalloc((size_t)(n ? n : 1) * PHASES, sizeof(Cell));
    int ncell = 0;
    stbtt_fontinfo fallbacks[NFALLBACK];
    int nfallbacks = load_fallbacks(fallbacks);
    for (int i = 0; i < n; i++) {
        if (is_default_ignorable(cps[i])) {
            advs[i] = 0;
            continue;
        }
        const stbtt_fontinfo* fi = &info;
        int glyph = stbtt_FindGlyphIndex(&info, (int)cps[i]);
        for (int f = 0; !glyph && f < nfallbacks; f++) {
            int g = stbtt_FindGlyphIndex(&fallbacks[f], (int)cps[i]);
            if (g) {
                fi = &fallbacks[f];
                glyph = g;
            }
        }
        float gsc = fi == &info ? sc : stbtt_ScaleForMappingEmToPixels(fi, (float)big);
        int gupem = fi == &info ? upem : (int)ttUSHORT(fi->data + fi->head + 18);
        int aw, lsb;
        stbtt_GetGlyphHMetrics(fi, glyph, &aw, &lsb);
        double adv = nearbyint((double)aw * big * 64.0 / gupem) / 64.0;
        advs[i] = adv / SUPER / SCALE;
        int ix0, iy0, ix1, iy1;
        int x0 = 0, x1 = py_round(adv), y0 = ascent, y1 = ascent;
        if (!stbtt_IsGlyphEmpty(fi, glyph)) {
            stbtt_GetGlyphBitmapBox(fi, glyph, gsc, gsc, &ix0, &iy0, &ix1, &iy1);
            if (ix1 > ix0 && iy1 > iy0) {
                if (ix0 < x0) x0 = ix0;
                if (ix1 > x1) x1 = ix1;
                if (ascent + iy0 < y0) y0 = ascent + iy0;
                if (ascent + iy1 > y1) y1 = ascent + iy1;
            }
        }
        x0 -= sw_total;
        y0 -= sw_total;
        x1 += sw_total;
        y1 += sw_total;
        if (x1 <= x0 || y1 <= y0) continue;
        int xs = floor_div(x0 - pad, SUPER) * SUPER;
        int ys = floor_div(y0 - pad, SUPER) * SUPER;
        for (int phase = 0; phase < PHASES; phase++) {
            double shift = phase * (double)SUPER / PHASES;
            int w = x1 + pad + SUPER - xs, h = y1 + pad - ys;
            w = (w + SUPER - 1) / SUPER * SUPER;
            h = (h + SUPER - 1) / SUPER * SUPER;
            uint8_t* cov = xcalloc((size_t)w * (size_t)h, 1);
            draw_glyph(fi, gsc, glyph, -xs + shift, -ys + ascent, cov, w, h);
            uint8_t* fill = xmalloc((size_t)w * (size_t)h);
            uint8_t* outl = xmalloc((size_t)w * (size_t)h);
            dilate(cov, fill, w, h, EMBOLDEN);
            dilate(cov, outl, w, h, stroke + EMBOLDEN);
            int sw = w / SUPER, sh = h / SUPER;
            uint8_t* fs = xmalloc((size_t)sw * (size_t)sh);
            uint8_t* os = xmalloc((size_t)sw * (size_t)sh);
            box_down(fill, w, h, fs);
            box_down(outl, w, h, os);
            Cell* c = &cells[ncell];
            c->cp = cps[i];
            c->phase = phase;
            c->w = sw;
            c->h = sh;
            c->ox = xs / SUPER;
            c->oy = ys / SUPER;
            c->order = ncell;
            c->px = xmalloc((size_t)sw * (size_t)sh * 4);
            for (int k = 0; k < sw * sh; k++) {
                int f = fs[k];
                int o = outline ? os[k] * OUTLINE_ALPHA / 255 : 0;
                int a = f + o * (255 - f) / 255;
                uint8_t pm = muldiv255(f, a);
                c->px[k * 4 + 0] = pm;
                c->px[k * 4 + 1] = pm;
                c->px[k * 4 + 2] = pm;
                c->px[k * 4 + 3] = (uint8_t)a;
            }
            ncell++;
            free(cov);
            free(fill);
            free(outl);
            free(fs);
            free(os);
        }
    }

    Cell** order = xmalloc(sizeof(Cell*) * (size_t)(ncell ? ncell : 1));
    for (int i = 0; i < ncell; i++) order[i] = &cells[i];
    qsort(order, (size_t)ncell, sizeof(Cell*), cmp_cell);
    int x = 0, y = 0, row_h = 0;
    for (int i = 0; i < ncell; i++) {
        Cell* c = order[i];
        if (x + c->w > ATLAS_W) {
            x = 0;
            y += row_h + 1;
            row_h = 0;
        }
        c->ax = x;
        c->ay = y;
        x += c->w + 1;
        if (c->h > row_h) row_h = c->h;
    }
    int atlas_h = y + row_h + 1;
    Image* atlas = img_new(ATLAS_W, atlas_h);
    memset(atlas->px, 0, (size_t)ATLAS_W * (size_t)atlas_h * 4);
    for (int i = 0; i < ncell; i++) {
        Cell* c = &cells[i];
        for (int yy = 0; yy < c->h; yy++) {
            memcpy(atlas->px + ((size_t)(c->ay + yy) * ATLAS_W + (size_t)c->ax) * 4, c->px + (size_t)yy * (size_t)c->w * 4, (size_t)c->w * 4);
        }
    }

    Node* meta = node_obj();
    node_set(meta, "size", node_num(logical_size));
    node_set(meta, "px", node_num(px_size));
    node_set(meta, "phases", node_num(PHASES));
    node_set(meta, "outline", node_num(outline));
    node_set(meta, "ascent", node_num((double)ascent / SUPER / SCALE));
    node_set(meta, "descent", node_num((double)descent / SUPER / SCALE));
    Node* glyphs = node_obj();
    int ci = 0;
    for (int i = 0; i < n; i++) {
        Node* g = node_arr();
        node_push(g, node_num(advs[i]));
        for (int ph = 0; ph < PHASES; ph++) {
            Node* q = node_arr();
            Cell* c = (ci < ncell && cells[ci].cp == cps[i] && cells[ci].phase == ph) ? &cells[ci++] : NULL;
            if (c) {
                node_push(q, node_num(c->ax));
                node_push(q, node_num(c->ay));
                node_push(q, node_num(c->w));
                node_push(q, node_num(c->h));
                node_push(q, node_num(c->ox));
                node_push(q, node_num(c->oy));
            } else {
                for (int k = 0; k < 6; k++) node_push(q, node_num(0));
            }
            node_push(g, q);
        }
        char key[16];
        snprintf(key, sizeof(key), "%u", (unsigned)cps[i]);
        node_set(glyphs, key, g);
    }
    node_set(meta, "glyphs", glyphs);
    vbin_dump(meta, fnt);
    node_free(meta);

    int ok = tex_encode(atlas, ATLAS_W, atlas_h, tex);
    img_free(atlas);
    for (int i = 0; i < ncell; i++) free(cells[i].px);
    free(cells);
    free(order);
    free(advs);
    free(cps);
    return ok;
}
