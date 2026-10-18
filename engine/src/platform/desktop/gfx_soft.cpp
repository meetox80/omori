#include "../gfx.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace gfx {

struct Texture {
    int w = 0;
    int h = 0;
    size_t bytes = 0;
    std::vector<uint32_t> px;
    const uint32_t* ext = nullptr;
    int ext_stride = 0;
    uint32_t at(int x, int y) const { return ext ? ext[(size_t)y * ext_stride + x] : px[(size_t)y * w + x]; }
};

struct Plane {
    std::vector<uint32_t> px;
    std::vector<uint8_t> st;
    int w = 0, h = 0, stride = 0;
    bool rgb565 = false;
    Texture tex;
};

static Plane g_planes[6];
static Plane* g_cur = nullptr;
static Surface g_target = Surface::Screen;
static int g_sx0 = 0, g_sy0 = 0, g_sx1 = SCREEN_W, g_sy1 = SCREEN_H;
static uint32_t g_protect = 0;
static bool g_wrap = false;
static Stencil g_stencil = Stencil::Off;

static const int DITHER[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};

void* alloc(size_t bytes) {
    void* p = nullptr;
    if (posix_memalign(&p, 64, bytes ? bytes : 16) != 0) return nullptr;
    return p;
}

void release(void* p) { free(p); }

static int bpp_of(Format f) {
    switch (f) {
        case Format::RGBA8888: return 4;
        case Format::T8: return 1;
        default: return 2;
    }
}

static uint32_t expand(Format f, const uint8_t* p, const uint32_t* pal) {
    switch (f) {
        case Format::RGBA8888: return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        case Format::T8: return pal ? pal[p[0]] : 0;
        case Format::RGBA4444: {
            uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
            int r = (v & 15) * 17, g = ((v >> 4) & 15) * 17, b = ((v >> 8) & 15) * 17, a = ((v >> 12) & 15) * 17;
            return rgba(r, g, b, a);
        }
        case Format::RGB565: {
            uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
            int r = (v & 31) * 255 / 31, g = ((v >> 5) & 63) * 255 / 63, b = ((v >> 11) & 31) * 255 / 31;
            return rgba(r, g, b, 255);
        }
        case Format::RGBA5551: {
            uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
            int r = (v & 31) * 255 / 31, g = ((v >> 5) & 31) * 255 / 31, b = ((v >> 10) & 31) * 255 / 31, a = (v >> 15) ? 255 : 0;
            return rgba(r, g, b, a);
        }
    }
    return 0;
}

Texture* texture_create(int w, int h, int stride, int rows, Format fmt, bool swizzled, void* pixels, uint32_t* palette) {
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    int bpp = bpp_of(fmt);
    int bw = stride * bpp;
    t->bytes = (size_t)bw * rows + (palette ? 1024 : 0);
    t->px.resize((size_t)w * h);
    const uint8_t* src = (const uint8_t*)pixels;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int xb = x * bpp;
            size_t off;
            if (swizzled) {
                off = ((size_t)(y / 8) * (bw / 16) + (xb / 16)) * 128 + (y % 8) * 16 + (xb % 16);
            } else {
                off = (size_t)y * bw + xb;
            }
            t->px[(size_t)y * w + x] = expand(fmt, src + off, palette);
        }
    }
    release(pixels);
    release(palette);
    return t;
}

Texture* texture_wrap(int w, int h, int stride, uint32_t* pixels) {
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    t->ext = pixels;
    t->ext_stride = stride;
    t->bytes = 0;
    return t;
}

void texture_touch(Texture*) {}

bool texture_read(const Texture* t, int x, int y, uint32_t* out) {
    if (!t || x < 0 || y < 0 || x >= t->w || y >= t->h) return false;
    *out = t->at(x, y);
    return true;
}

void texture_destroy(Texture* t) { delete t; }
size_t texture_bytes(const Texture* t) { return t ? t->bytes : 0; }
int texture_width(const Texture* t) { return t ? t->w : 0; }
int texture_height(const Texture* t) { return t ? t->h : 0; }

static void init_plane(Surface s, int w, int h, int stride, bool rgb565) {
    Plane& p = g_planes[(int)s];
    p.w = w;
    p.h = h;
    p.stride = stride;
    p.rgb565 = rgb565;
    p.px.assign((size_t)stride * h, 0xFF000000u);
    p.st.assign((size_t)stride * h, 0);
    p.tex.w = w;
    p.tex.h = h;
    p.tex.ext = p.px.data();
    p.tex.ext_stride = stride;
    p.tex.bytes = 0;
}

void init() {
    init_plane(Surface::Screen, SCREEN_W, SCREEN_H, SCREEN_W, false);
    init_plane(Surface::Grab, SCREEN_W, SCREEN_H, 512, false);
    init_plane(Surface::Half0, HALF_W, HALF_H, 256, false);
    init_plane(Surface::Half1, HALF_W, HALF_H, 256, false);
    init_plane(Surface::Half2, HALF_W, HALF_H, 256, false);
    init_plane(Surface::Wide, SCREEN_W, SCREEN_H, 512, true);
    g_cur = &g_planes[0];
    g_target = Surface::Screen;
}

void shutdown() {
    for (auto& p : g_planes) {
        p.px.clear();
        p.st.clear();
    }
}

void begin_frame(uint32_t clear_color) {
    set_target(Surface::Screen);
    std::fill(g_cur->px.begin(), g_cur->px.end(), clear_color | 0xFF000000u);
    g_protect = 0;
    g_wrap = false;
    g_stencil = Stencil::Off;
    reset_scissor();
}

void end_frame() {}

int list_peak_bytes() { return 0; }

size_t arena_free_bytes() { return 0; }

void* arena_try_alloc(size_t) { return nullptr; }

double take_batch_count() { return 0; }

void present() {}

bool reclaim_memory() { return true; }

const uint32_t* framebuffer() { return g_planes[0].px.data(); }

bool capture_row(int y, int x, int w, uint32_t* out) {
    const Plane& p = g_planes[0];
    if (y < 0 || y >= SCREEN_H || x < 0 || x + w > SCREEN_W) return false;
    std::memcpy(out, &p.px[(size_t)y * p.stride + x], (size_t)w * 4);
    return true;
}

bool capture(uint32_t* out, int out_stride) {
    const Plane& p = g_planes[0];
    for (int y = 0; y < SCREEN_H; y++) std::memcpy(out + (size_t)y * out_stride, &p.px[(size_t)y * p.stride], SCREEN_W * 4);
    return true;
}

void set_scissor(int x, int y, int w, int h) {
    g_sx0 = std::max(0, x);
    g_sy0 = std::max(0, y);
    g_sx1 = std::min(g_cur->w, x + w);
    g_sy1 = std::min(g_cur->h, y + h);
}

void reset_scissor() {
    g_sx0 = 0;
    g_sy0 = 0;
    g_sx1 = g_cur->w;
    g_sy1 = g_cur->h;
}

void set_target(Surface s) {
    g_target = s;
    g_cur = &g_planes[(int)s];
    reset_scissor();
}

Surface current_target() { return g_target; }

Texture* surface_texture(Surface s) { return &g_planes[(int)s].tex; }
int surface_width(Surface s) { return g_planes[(int)s].w; }
int surface_height(Surface s) { return g_planes[(int)s].h; }

void set_color_mask(uint32_t protect_rgb) { g_protect = protect_rgb & 0xFFFFFFu; }
void set_wrap(bool repeat) { g_wrap = repeat; }
void set_stencil(Stencil mode) { g_stencil = mode; }

void stencil_clear(int x0, int y0, int x1, int y1) {
    Plane& p = *g_cur;
    if (p.rgb565) return;
    x0 = std::max(x0, g_sx0);
    y0 = std::max(y0, g_sy0);
    x1 = std::min(x1, g_sx1);
    y1 = std::min(y1, g_sy1);
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) p.st[(size_t)y * p.stride + x] = 0;
    }
}

static inline int wrap_coord(int c, int n) {
    c %= n;
    return c < 0 ? c + n : c;
}

static inline uint32_t texel(const Texture* t, int x, int y) {
    if (g_wrap) {
        x = wrap_coord(x, t->w);
        y = wrap_coord(y, t->h);
    } else {
        x = std::clamp(x, 0, t->w - 1);
        y = std::clamp(y, 0, t->h - 1);
    }
    return t->at(x, y);
}

static inline uint32_t sample(const Texture* t, float u, float v, bool smooth) {
    if (!smooth) return texel(t, (int)std::floor(u), (int)std::floor(v));
    float fu = u - 0.5f, fv = v - 0.5f;
    int x0 = (int)std::floor(fu), y0 = (int)std::floor(fv);
    float ax = fu - x0, ay = fv - y0;
    uint32_t c00 = texel(t, x0, y0), c10 = texel(t, x0 + 1, y0);
    uint32_t c01 = texel(t, x0, y0 + 1), c11 = texel(t, x0 + 1, y0 + 1);
    int wx = (int)(ax * 256.0f), wy = (int)(ay * 256.0f);
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        int a = (c00 >> s) & 255, b = (c10 >> s) & 255, c = (c01 >> s) & 255, d = (c11 >> s) & 255;
        int top = a * (256 - wx) + b * wx;
        int bot = c * (256 - wx) + d * wx;
        int v2 = (top * (256 - wy) + bot * wy) >> 16;
        out |= (uint32_t)std::clamp(v2, 0, 255) << s;
    }
    return out;
}

static inline uint32_t modulate(uint32_t c, uint32_t m) {
    if (m == 0xFFFFFFFFu) return c;
    uint32_t out = 0;
    for (int s = 0; s < 32; s += 8) {
        int a = (c >> s) & 255, b = (m >> s) & 255;
        out |= (uint32_t)((a * b + 127) / 255) << s;
    }
    return out;
}

static inline void put(int x, int y, uint32_t s, Blend mode) {
    Plane& p = *g_cur;
    size_t idx = (size_t)y * p.stride + x;
    int sa = s >> 24;
    if (g_stencil == Stencil::Test && (p.rgb565 || p.st[idx] != 255)) return;
    if (g_stencil == Stencil::Write && sa == 0) return;
    uint32_t& d = p.px[idx];
    if (mode == Blend::Normal && sa == 0 && (s & 0xFFFFFF) == 0) {
        if (g_stencil == Stencil::Write && !p.rgb565) p.st[idx] = 255;
        return;
    }
    uint32_t out = 0;
    for (int sh = 0; sh < 24; sh += 8) {
        int sc = (s >> sh) & 255, dc = (d >> sh) & 255, r;
        switch (mode) {
            case Blend::Normal: r = sc + dc * (255 - sa) / 255; break;
            case Blend::Add: r = sc + dc; break;
            case Blend::Multiply: r = sc * dc / 255 + dc * (255 - sa) / 255; break;
            case Blend::Screen: r = sc + dc * (255 - sc) / 255; break;
            case Blend::Subtract: r = dc - sc; break;
            case Blend::MulSrc: r = (dc * sc + 127) / 255; break;
            case Blend::MulAdd: r = dc + (dc * sc + 127) / 255; break;
            case Blend::MulSub: r = dc - (dc * sc + 127) / 255; break;
            case Blend::Max: r = std::max(sc, dc); break;
            case Blend::Min: r = std::min(sc, dc); break;
            default: r = sc; break;
        }
        r = std::clamp(r, 0, 255);
        if (p.rgb565) {
            int bits = sh == 8 ? 6 : 5;
            int q = std::clamp(r + DITHER[y & 3][x & 3], 0, 255) >> (8 - bits);
            r = bits == 6 ? ((q << 2) | (q >> 4)) : ((q << 3) | (q >> 2));
        }
        out |= (uint32_t)r << sh;
    }
    if (g_protect) out = (out & ~g_protect) | (d & g_protect);
    d = (out & 0xFFFFFFu) | 0xFF000000u;
    if (g_stencil == Stencil::Write && !p.rgb565) p.st[idx] = 255;
}

void sprite(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    int ix0 = std::max(g_sx0, (int)std::ceil(x0 - 0.5f));
    int iy0 = std::max(g_sy0, (int)std::ceil(y0 - 0.5f));
    int ix1 = std::min(g_sx1, (int)std::ceil(x1 - 0.5f));
    int iy1 = std::min(g_sy1, (int)std::ceil(y1 - 0.5f));
    if (ix0 >= ix1 || iy0 >= iy1) return;
    float du = (u1 - u0) / (x1 - x0), dv = (v1 - v0) / (y1 - y0);
    for (int y = iy0; y < iy1; y++) {
        float v = v0 + (y + 0.5f - y0) * dv;
        for (int x = ix0; x < ix1; x++) {
            float u = u0 + (x + 0.5f - x0) * du;
            put(x, y, modulate(sample(tex, u, v, smooth), color), blend);
        }
    }
}

static inline uint32_t flat_of(uint32_t texel_value, uint32_t rgb, int alpha) {
    int a = (int)(texel_value >> 24) * alpha / 255;
    int r = (int)(rgb & 255) * a / 255, g = (int)((rgb >> 8) & 255) * a / 255, b = (int)((rgb >> 16) & 255) * a / 255;
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

void sprite_flat(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t rgb, int alpha, Blend blend,
                 bool smooth) {
    if (!tex || alpha <= 0) return;
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    int ix0 = std::max(g_sx0, (int)std::ceil(x0 - 0.5f));
    int iy0 = std::max(g_sy0, (int)std::ceil(y0 - 0.5f));
    int ix1 = std::min(g_sx1, (int)std::ceil(x1 - 0.5f));
    int iy1 = std::min(g_sy1, (int)std::ceil(y1 - 0.5f));
    if (ix0 >= ix1 || iy0 >= iy1) return;
    float du = (u1 - u0) / (x1 - x0), dv = (v1 - v0) / (y1 - y0);
    for (int y = iy0; y < iy1; y++) {
        float v = v0 + (y + 0.5f - y0) * dv;
        for (int x = ix0; x < ix1; x++) {
            float u = u0 + (x + 0.5f - x0) * du;
            put(x, y, flat_of(sample(tex, u, v, smooth), rgb, alpha), blend);
        }
    }
}

template <class F>
static void raster_quad(const float uv[8], const float xy[8], F shade) {
    float ox = xy[0], oy = xy[1];
    float ax = xy[2] - ox, ay = xy[3] - oy;
    float bx = xy[6] - ox, by = xy[7] - oy;
    float det = ax * by - ay * bx;
    if (std::fabs(det) < 1e-6f) return;
    float minx = std::min(std::min(xy[0], xy[2]), std::min(xy[4], xy[6]));
    float maxx = std::max(std::max(xy[0], xy[2]), std::max(xy[4], xy[6]));
    float miny = std::min(std::min(xy[1], xy[3]), std::min(xy[5], xy[7]));
    float maxy = std::max(std::max(xy[1], xy[3]), std::max(xy[5], xy[7]));
    int ix0 = std::max(g_sx0, (int)std::floor(minx)), ix1 = std::min(g_sx1, (int)std::ceil(maxx));
    int iy0 = std::max(g_sy0, (int)std::floor(miny)), iy1 = std::min(g_sy1, (int)std::ceil(maxy));
    float uo = uv[0], vo = uv[1];
    float ua = uv[2] - uo, va = uv[3] - vo;
    float ub = uv[6] - uo, vb = uv[7] - vo;
    for (int y = iy0; y < iy1; y++) {
        for (int x = ix0; x < ix1; x++) {
            float px = x + 0.5f - ox, py = y + 0.5f - oy;
            float s = (px * by - py * bx) / det;
            float t = (ax * py - ay * px) / det;
            if (s < 0 || s >= 1 || t < 0 || t >= 1) continue;
            shade(x, y, uo + s * ua + t * ub, vo + s * va + t * vb);
        }
    }
}

void quad_flat(Texture* tex, const float uv[8], const float xy[8], uint32_t rgb, int alpha, Blend blend, bool smooth) {
    if (!tex || alpha <= 0) return;
    raster_quad(uv, xy, [&](int x, int y, float u, float v) { put(x, y, flat_of(sample(tex, u, v, smooth), rgb, alpha), blend); });
}

void draw_quad(Texture* tex, const float uv[8], const float xy[8], uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    raster_quad(uv, xy, [&](int x, int y, float u, float v) { put(x, y, modulate(sample(tex, u, v, smooth), color), blend); });
}

void fill(float x0, float y0, float x1, float y1, uint32_t color, Blend blend) {
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    int ix0 = std::max(g_sx0, (int)std::ceil(x0 - 0.5f));
    int iy0 = std::max(g_sy0, (int)std::ceil(y0 - 0.5f));
    int ix1 = std::min(g_sx1, (int)std::ceil(x1 - 0.5f));
    int iy1 = std::min(g_sy1, (int)std::ceil(y1 - 0.5f));
    for (int y = iy0; y < iy1; y++) {
        for (int x = ix0; x < ix1; x++) put(x, y, color, blend);
    }
}

static inline float edge(float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); }

static inline bool top_left(float ax, float ay, float bx, float by) { return (ay == by && bx < ax) || by < ay; }

static void raster_triangle(Texture* tex, const Vertex& a0, const Vertex& b0, const Vertex& c0, Blend blend, bool smooth) {
    const Vertex* a = &a0;
    const Vertex* b = &b0;
    const Vertex* c = &c0;
    float area = edge(a->x, a->y, b->x, b->y, c->x, c->y);
    if (std::fabs(area) < 1e-9f) return;
    if (area < 0) {
        std::swap(b, c);
        area = -area;
    }
    float minx = std::min(a->x, std::min(b->x, c->x)), maxx = std::max(a->x, std::max(b->x, c->x));
    float miny = std::min(a->y, std::min(b->y, c->y)), maxy = std::max(a->y, std::max(b->y, c->y));
    int ix0 = std::max(g_sx0, (int)std::floor(minx)), ix1 = std::min(g_sx1, (int)std::ceil(maxx) + 1);
    int iy0 = std::max(g_sy0, (int)std::floor(miny)), iy1 = std::min(g_sy1, (int)std::ceil(maxy) + 1);
    bool tl0 = top_left(b->x, b->y, c->x, c->y), tl1 = top_left(c->x, c->y, a->x, a->y), tl2 = top_left(a->x, a->y, b->x, b->y);
    float ca[4], cb[4], cc[4];
    for (int k = 0; k < 4; k++) {
        ca[k] = (float)((a->color >> (k * 8)) & 255);
        cb[k] = (float)((b->color >> (k * 8)) & 255);
        cc[k] = (float)((c->color >> (k * 8)) & 255);
    }
    for (int y = iy0; y < iy1; y++) {
        float py = y + 0.5f;
        for (int x = ix0; x < ix1; x++) {
            float px = x + 0.5f;
            float w0 = edge(b->x, b->y, c->x, c->y, px, py);
            float w1 = edge(c->x, c->y, a->x, a->y, px, py);
            float w2 = edge(a->x, a->y, b->x, b->y, px, py);
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            if ((w0 == 0 && !tl0) || (w1 == 0 && !tl1) || (w2 == 0 && !tl2)) continue;
            float l0 = w0 / area, l1 = w1 / area, l2 = w2 / area;
            uint32_t col = 0;
            for (int k = 0; k < 4; k++) {
                int v = (int)std::lround(ca[k] * l0 + cb[k] * l1 + cc[k] * l2);
                col |= (uint32_t)std::clamp(v, 0, 255) << (k * 8);
            }
            if (tex) {
                float u = a->u * l0 + b->u * l1 + c->u * l2;
                float v = a->v * l0 + b->v * l1 + c->v * l2;
                put(x, y, modulate(sample(tex, u, v, smooth), col), blend);
            } else {
                put(x, y, col, blend);
            }
        }
    }
}

void draw_triangles(Texture* tex, const Vertex* v, int count, Blend blend, bool smooth) {
    for (int i = 0; i + 2 < count; i += 3) raster_triangle(tex, v[i], v[i + 1], v[i + 2], blend, smooth);
}

void draw_grid(Texture* tex, const Vertex* v, int cols, int rows, Blend blend, bool smooth) {
    int stride = cols + 1;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            const Vertex& p00 = v[(size_t)r * stride + c];
            const Vertex& p10 = v[(size_t)r * stride + c + 1];
            const Vertex& p01 = v[(size_t)(r + 1) * stride + c];
            const Vertex& p11 = v[(size_t)(r + 1) * stride + c + 1];
            raster_triangle(tex, p00, p01, p10, blend, smooth);
            raster_triangle(tex, p10, p01, p11, blend, smooth);
        }
    }
}

void sprite_lut(Texture* tex, int channel, const uint32_t* lut, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, Blend blend) {
    if (!tex || !lut) return;
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    int ix0 = std::max(g_sx0, (int)std::ceil(x0 - 0.5f));
    int iy0 = std::max(g_sy0, (int)std::ceil(y0 - 0.5f));
    int ix1 = std::min(g_sx1, (int)std::ceil(x1 - 0.5f));
    int iy1 = std::min(g_sy1, (int)std::ceil(y1 - 0.5f));
    if (ix0 >= ix1 || iy0 >= iy1) return;
    float du = (u1 - u0) / (x1 - x0), dv = (v1 - v0) / (y1 - y0);
    int shift = std::clamp(channel, 0, 3) * 8;
    for (int y = iy0; y < iy1; y++) {
        float v = v0 + (y + 0.5f - y0) * dv;
        for (int x = ix0; x < ix1; x++) {
            float u = u0 + (x + 0.5f - x0) * du;
            uint32_t t = texel(tex, (int)std::floor(u), (int)std::floor(v));
            put(x, y, lut[(t >> shift) & 255], blend);
        }
    }
}

void desaturate(float amount) {
    if (amount <= 0) return;
    if (amount > 1) amount = 1;
    int k = (int)(amount * 256.0f + 0.5f);
    for (uint32_t& px : g_planes[0].px) {
        int r = px & 255, g = (px >> 8) & 255, b = (px >> 16) & 255;
        int avg = (r + g + b) / 3;
        r = r + (((avg - r) * k) >> 8);
        g = g + (((avg - g) * k) >> 8);
        b = b + (((avg - b) * k) >> 8);
        px = (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | 0xFF000000u;
    }
}

Texture* grab_screen() {
    Plane& src = g_planes[0];
    Plane& dst = g_planes[(int)Surface::Grab];
    for (int y = 0; y < SCREEN_H; y++) std::memcpy(&dst.px[(size_t)y * dst.stride], &src.px[(size_t)y * src.stride], SCREEN_W * 4);
    return &dst.tex;
}

}
