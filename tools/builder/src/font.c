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

#define SCALE (g_target->tex_scale)
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
    int single;
    uint32_t freq;
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
extern const unsigned char fallback_noto_kr[];
extern const size_t fallback_noto_kr_size;

#define PAGE_H 64

struct FontSet {
    const uint8_t** data;
    int nfonts;
    int nfixed;
    uint32_t* cps;
    int* src;
    uint32_t* freq;
    int count;
};

static int font_usable(const uint8_t* data, size_t len) {
    if (!data || len < 12) return 0;
    stbtt_fontinfo info;
    return stbtt_InitFont(&info, data, stbtt_GetFontOffsetForIndex(data, 0));
}

static void set_push(FontSet* fs, const uint8_t* data, size_t len) {
    if (!font_usable(data, len)) return;
    fs->data = xrealloc(fs->data, sizeof(*fs->data) * (size_t)(fs->nfonts + 1));
    fs->data[fs->nfonts++] = data;
}

FontSet* fontset_new(const uint8_t* primary, size_t len) {
    if (!font_usable(primary, len)) return NULL;
    FontSet* fs = xcalloc(1, sizeof(FontSet));
    set_push(fs, primary, len);
    set_push(fs, fallback_noto_sans, fallback_noto_sans_size);
    set_push(fs, fallback_noto_symbols2, fallback_noto_symbols2_size);
    set_push(fs, fallback_noto_math, fallback_noto_math_size);
    set_push(fs, fallback_noto_arabic, fallback_noto_arabic_size);
    fs->nfixed = fs->nfonts;
    return fs;
}

void fontset_add(FontSet* fs, const uint8_t* data, size_t len) { set_push(fs, data, len); }

void fontset_add_cjk(FontSet* fs) {
    set_push(fs, fallback_noto_sc, fallback_noto_sc_size);
    set_push(fs, fallback_noto_kr, fallback_noto_kr_size);
}

static int is_default_ignorable(uint32_t cp);

void fontset_resolve(FontSet* fs, const uint32_t* cps, const uint32_t* freq, int count) {
    fs->cps = xmalloc(sizeof(uint32_t) * (size_t)(count ? count : 1));
    fs->freq = xmalloc(sizeof(uint32_t) * (size_t)(count ? count : 1));
    fs->src = xmalloc(sizeof(int) * (size_t)(count ? count : 1));
    fs->count = count;
    memcpy(fs->cps, cps, sizeof(uint32_t) * (size_t)count);
    memcpy(fs->freq, freq, sizeof(uint32_t) * (size_t)count);
    stbtt_fontinfo* infos = xmalloc(sizeof(stbtt_fontinfo) * (size_t)fs->nfonts);
    for (int f = 0; f < fs->nfonts; f++) stbtt_InitFont(&infos[f], fs->data[f], stbtt_GetFontOffsetForIndex(fs->data[f], 0));
    int left = 0;
    for (int i = 0; i < count; i++) {
        fs->src[i] = -1;
        if (is_default_ignorable(cps[i])) continue;
        for (int f = 0; f < fs->nfixed && fs->src[i] < 0; f++) {
            if (stbtt_FindGlyphIndex(&infos[f], (int)cps[i])) fs->src[i] = f;
        }
        if (fs->src[i] < 0) left++;
    }
    int ncand = fs->nfonts - fs->nfixed;
    uint8_t* has = xcalloc((size_t)(count ? count : 1) * (size_t)(ncand ? ncand : 1), 1);
    for (int i = 0; i < count; i++) {
        if (fs->src[i] >= 0 || is_default_ignorable(cps[i])) continue;
        for (int k = 0; k < ncand; k++) has[(size_t)i * (size_t)ncand + (size_t)k] = stbtt_FindGlyphIndex(&infos[fs->nfixed + k], (int)cps[i]) != 0;
    }
    while (left > 0) {
        int best = -1, best_n = 0;
        for (int k = 0; k < ncand; k++) {
            int n = 0;
            for (int i = 0; i < count; i++) {
                if (fs->src[i] < 0 && has[(size_t)i * (size_t)ncand + (size_t)k]) n++;
            }
            if (n > best_n) {
                best = k;
                best_n = n;
            }
        }
        if (best < 0) break;
        for (int i = 0; i < count; i++) {
            if (fs->src[i] < 0 && has[(size_t)i * (size_t)ncand + (size_t)best]) fs->src[i] = fs->nfixed + best;
        }
        left -= best_n;
    }
    free(has);
    free(infos);
}

void fontset_free(FontSet* fs) {
    if (!fs) return;
    free(fs->data);
    free(fs->cps);
    free(fs->src);
    free(fs->freq);
    free(fs);
}

static int single_phase(uint32_t cp) {
    return (cp >= 0x1100 && cp <= 0x11FF) || (cp >= 0x2E80 && cp <= 0x2FDF) || (cp >= 0x3000 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA960 && cp <= 0xA97F) || (cp >= 0xAC00 && cp <= 0xD7FF) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x20000 && cp <= 0x3FFFF);
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

static int cmp_cell(const void* a, const void* b) {
    const Cell* x = *(const Cell* const*)a;
    const Cell* y = *(const Cell* const*)b;
    if (x->single != y->single) return x->single - y->single;
    if (x->single && x->freq != y->freq) return x->freq > y->freq ? -1 : 1;
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
    memset(dst, 0, (size_t)w * (size_t)h);
    for (int sy = 0; sy < h; sy++) {
        for (int sx = 0; sx < w; sx++) {
            int s = src[sy * w + sx];
            if (!s) continue;
            for (int k = 0; k < n; k++) {
                int x = sx - offs[k][0], y = sy - offs[k][1];
                if (x < 0 || y < 0 || x >= w || y >= h) continue;
                int v = (s * offs[k][2] + 128) >> 8;
                if (v > dst[y * w + x]) dst[y * w + x] = (uint8_t)v;
            }
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

int font_build(const FontSet* fs, int logical_size, int outline, Buf* tex, Buf* fnt) {
    stbtt_fontinfo* infos = xmalloc(sizeof(stbtt_fontinfo) * (size_t)fs->nfonts);
    for (int f = 0; f < fs->nfonts; f++) {
        if (!stbtt_InitFont(&infos[f], fs->data[f], stbtt_GetFontOffsetForIndex(fs->data[f], 0))) {
            free(infos);
            return 0;
        }
    }
    const stbtt_fontinfo* info = &infos[0];
    double px_size = logical_size * SCALE * 1.0;
    int big = py_round(px_size * SUPER);
    if (big < 1) big = 1;
    float sc = stbtt_ScaleForMappingEmToPixels(info, (float)big);
    int stroke = outline ? py_round(OUTLINE_LOGICAL * SCALE * 1.0 * SUPER / 2) : 0;
    if (outline && stroke < 1) stroke = 1;
    int asc_u, desc_u, gap_u;
    stbtt_GetFontVMetrics(info, &asc_u, &desc_u, &gap_u);
    int upem = (int)ttUSHORT(info->data + info->head + 18);
    int ascent = (int)ceil((double)asc_u * big / upem);
    int descent = (int)ceil((double)-desc_u * big / upem);
    int pad = stroke + EMBOLDEN + 2;
    int sw_total = stroke + EMBOLDEN;

    int n = fs->count;
    const uint32_t* cps = fs->cps;
    double* advs = xmalloc(sizeof(double) * (size_t)(n ? n : 1));
    Cell* cells = xcalloc((size_t)(n ? n : 1) * PHASES, sizeof(Cell));
    int ncell = 0;
    for (int i = 0; i < n; i++) {
        if (is_default_ignorable(cps[i])) {
            advs[i] = 0;
            continue;
        }
        const stbtt_fontinfo* fi = fs->src[i] >= 0 ? &infos[fs->src[i]] : info;
        int glyph = fs->src[i] >= 0 ? stbtt_FindGlyphIndex(fi, (int)cps[i]) : 0;
        float gsc = fi == info ? sc : stbtt_ScaleForMappingEmToPixels(fi, (float)big);
        int gupem = fi == info ? upem : (int)ttUSHORT(fi->data + fi->head + 18);
        int single = single_phase(cps[i]);
        double fit = 1.0;
        int drop = 0;
        int ta, td, tg;
        if (single && fi != info && stbtt_GetFontVMetricsOS2(fi, &ta, &td, &tg) && ta > td) {
            double box = (double)(ta - td) / gupem, line = (double)(asc_u - desc_u) / upem;
            if (box > line) fit = line / box;
            double centre = (double)(ta + td) / 2 / gupem * fit, line_centre = (double)(asc_u + desc_u) / 2 / upem;
            drop = py_round((centre - line_centre) * big);
            gsc = (float)(gsc * fit);
        }
        int aw, lsb;
        stbtt_GetGlyphHMetrics(fi, glyph, &aw, &lsb);
        double adv = nearbyint((double)aw * fit * big * 64.0 / gupem) / 64.0;
        advs[i] = adv / SUPER / SCALE;
        int ix0, iy0, ix1, iy1;
        int x0 = 0, x1 = py_round(adv), y0 = ascent, y1 = ascent;
        if (!stbtt_IsGlyphEmpty(fi, glyph)) {
            stbtt_GetGlyphBitmapBox(fi, glyph, gsc, gsc, &ix0, &iy0, &ix1, &iy1);
            if (ix1 > ix0 && iy1 > iy0) {
                if (ix0 < x0) x0 = ix0;
                if (ix1 > x1) x1 = ix1;
                if (ascent + drop + iy0 < y0) y0 = ascent + drop + iy0;
                if (ascent + drop + iy1 > y1) y1 = ascent + drop + iy1;
            }
        }
        x0 -= sw_total;
        y0 -= sw_total;
        x1 += sw_total;
        y1 += sw_total;
        if (x1 <= x0 || y1 <= y0) continue;
        int xs = floor_div(x0 - pad, SUPER) * SUPER;
        int ys = floor_div(y0 - pad, SUPER) * SUPER;
        for (int phase = 0; phase < (single ? 1 : PHASES); phase++) {
            double shift = phase * (double)SUPER / PHASES;
            int w = x1 + pad + SUPER - xs, h = y1 + pad - ys;
            w = (w + SUPER - 1) / SUPER * SUPER;
            h = (h + SUPER - 1) / SUPER * SUPER;
            uint8_t* cov = xcalloc((size_t)w * (size_t)h, 1);
            draw_glyph(fi, gsc, glyph, -xs + shift, -ys + ascent + drop, cov, w, h);
            uint8_t* fill = xmalloc((size_t)w * (size_t)h);
            uint8_t* outl = xmalloc((size_t)w * (size_t)h);
            dilate(cov, fill, w, h, EMBOLDEN);
            dilate(cov, outl, w, h, stroke + EMBOLDEN);
            int sw = w / SUPER, sh = h / SUPER;
            uint8_t* fsd = xmalloc((size_t)sw * (size_t)sh);
            uint8_t* osd = xmalloc((size_t)sw * (size_t)sh);
            box_down(fill, w, h, fsd);
            box_down(outl, w, h, osd);
            Cell* c = &cells[ncell];
            c->cp = cps[i];
            c->phase = phase;
            c->w = sw;
            c->h = sh;
            c->ox = xs / SUPER;
            c->oy = ys / SUPER;
            c->order = ncell;
            c->single = single;
            c->freq = fs->freq[i];
            c->px = xmalloc((size_t)sw * (size_t)sh * 4);
            for (int k = 0; k < sw * sh; k++) {
                int f = fsd[k];
                int o = outline ? osd[k] * OUTLINE_ALPHA / 255 : 0;
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
            free(fsd);
            free(osd);
        }
    }

    Cell** order = xmalloc(sizeof(Cell*) * (size_t)(ncell ? ncell : 1));
    for (int i = 0; i < ncell; i++) order[i] = &cells[i];
    qsort(order, (size_t)ncell, sizeof(Cell*), cmp_cell);
    int atlas_w = ATLAS_W;
    double area = 0;
    for (int i = 0; i < ncell; i++) area += (double)(cells[i].w + 1) * (cells[i].h + 1);
    while (atlas_w < 4096 && area * 1.25 / atlas_w > 30000) atlas_w *= 2;
    int* row_y = xmalloc(sizeof(int) * (size_t)(ncell + 1));
    int* row_h = xmalloc(sizeof(int) * (size_t)(ncell + 1));
    int nrows = 0;
    int x = 0, y = 0, cur_h = 0;
    for (int i = 0; i < ncell; i++) {
        Cell* c = order[i];
        if (x + c->w > atlas_w) {
            row_y[nrows] = y;
            row_h[nrows++] = cur_h;
            x = 0;
            y += cur_h + 1;
            cur_h = 0;
        }
        c->ax = x;
        c->ay = y;
        x += c->w + 1;
        if (c->h > cur_h) cur_h = c->h;
    }
    row_y[nrows] = y;
    row_h[nrows++] = cur_h;
    int atlas_h = y + cur_h + 1;
    int* page_y = xmalloc(sizeof(int) * (size_t)nrows);
    int npages = 0;
    for (int r = 0; r < nrows; r++) {
        if (npages == 0 || row_y[r] + row_h[r] + 1 - page_y[npages - 1] > PAGE_H) page_y[npages++] = row_y[r];
    }
    free(row_y);
    free(row_h);
    Image* atlas = img_new(atlas_w, atlas_h);
    memset(atlas->px, 0, (size_t)atlas_w * (size_t)atlas_h * 4);
    for (int i = 0; i < ncell; i++) {
        Cell* c = &cells[i];
        for (int yy = 0; yy < c->h; yy++) {
            memcpy(atlas->px + ((size_t)(c->ay + yy) * atlas_w + (size_t)c->ax) * 4, c->px + (size_t)yy * (size_t)c->w * 4, (size_t)c->w * 4);
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

    int ok = tex_encode_font(atlas, page_y, npages, tex);
    img_free(atlas);
    for (int i = 0; i < ncell; i++) free(cells[i].px);
    free(cells);
    free(order);
    free(page_y);
    free(advs);
    free(infos);
    return ok;
}
