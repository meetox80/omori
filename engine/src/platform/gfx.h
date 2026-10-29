#pragma once
#include <cstddef>
#include <cstdint>

#include "profile.h"

namespace gfx {

constexpr int SCREEN_W = profile::SCREEN_W;
constexpr int SCREEN_H = profile::SCREEN_H;

enum class Format : uint8_t { RGBA8888 = 0, RGBA4444 = 1, RGB565 = 2, RGBA5551 = 3, T8 = 4 };
enum class Blend : uint8_t { Normal = 0, Add = 1, Multiply = 2, Screen = 3, Subtract = 4, Copy = 5, MulSrc = 6, MulAdd = 7, Max = 8, Min = 9, MulSub = 10 };
enum class Surface : uint8_t { Screen = 0, Grab = 1, Half0 = 2, Half1 = 3, Half2 = 4, Wide = 5, Bottom = 6 };
enum class Stencil : uint8_t { Off = 0, Write = 1, Test = 2 };

constexpr int HALF_W = profile::HALF_W;
constexpr int HALF_H = profile::HALF_H;

struct Vertex {
    float u, v;
    uint32_t color;
    float x, y;
};

struct Texture;

struct BorderRect {
    int x0, y0, x1, y1;
};

struct BorderArt {
    const uint8_t* levels;
    int w, h, ox, oy;
    uint32_t back, line;
};

inline uint32_t rgba(int r, int g, int b, int a = 255) {
    return (uint32_t)(r & 255) | ((uint32_t)(g & 255) << 8) | ((uint32_t)(b & 255) << 16) | ((uint32_t)(a & 255) << 24);
}

inline uint32_t premul(int r, int g, int b, int a) {
    return rgba(r * a / 255, g * a / 255, b * a / 255, a);
}

inline size_t tiled_index(int x, int y, int tw, int th) {
    int row = th - 1 - y;
    int m = 0;
    for (int i = 0; i < 3; i++) m |= ((x >> i) & 1) << (2 * i) | ((row >> i) & 1) << (2 * i + 1);
    return ((size_t)(row >> 3) * (size_t)(tw >> 3) + (size_t)(x >> 3)) * 64 + (size_t)m;
}

inline uint32_t stored_texel(const uint8_t* raw, int x, int y, int stride, int rows) {
    const uint8_t* p;
    switch (profile::TEX_LAYOUT) {
        case profile::TexLayout::PicaTiled:
            p = raw + tiled_index(x, y, stride, rows) * 4;
            return rgba(p[3], p[2], p[1], p[0]);
        case profile::TexLayout::GxTiled: {
            const uint8_t* tile = raw + ((size_t)(y >> 2) * (size_t)(stride >> 2) + (size_t)(x >> 2)) * 64;
            size_t at = (size_t)((y & 3) * 4 + (x & 3)) * 2;
            return rgba(tile[at + 1], tile[32 + at], tile[32 + at + 1], tile[at]);
        }
        case profile::TexLayout::Argb:
            p = raw + ((size_t)y * stride + x) * 4;
            return rgba(p[1], p[2], p[3], p[0]);
        case profile::TexLayout::GsStraight: {
            p = raw + ((size_t)y * stride + x) * 4;
            int a = p[3] >= 128 ? 255 : (p[3] * 255 + 64) / 128;
            return rgba(p[0] * a / 255, p[1] * a / 255, p[2] * a / 255, a);
        }
        case profile::TexLayout::PspSwizzle: {
            size_t bw = (size_t)stride * 4, xb = (size_t)x * 4;
            p = raw + ((size_t)(y / 8) * (bw / 16) + xb / 16) * 128 + (size_t)(y % 8) * 16 + xb % 16;
            break;
        }
        default: p = raw + ((size_t)y * stride + x) * 4; break;
    }
    return rgba(p[0], p[1], p[2], p[3]);
}

void* alloc(size_t bytes);
void release(void* p);

Texture* texture_create(int w, int h, int stride, int rows, Format fmt, bool swizzled, void* pixels, uint32_t* palette);
Texture* texture_wrap(int w, int h, int stride, uint32_t* pixels);
void texture_touch(Texture* t);
bool texture_read(const Texture* t, int x, int y, uint32_t* out);
void texture_read_rect(const Texture* t, int x, int y, int w, int h, uint32_t* out, size_t stride);
void texture_destroy(Texture* t);
size_t texture_bytes(const Texture* t);
int texture_width(const Texture* t);
int texture_height(const Texture* t);

void init();
void shutdown();
void begin_frame(uint32_t clear_color);
void end_frame();
int list_peak_bytes();
size_t arena_free_bytes();
void* arena_try_alloc(size_t bytes);
double take_batch_count();
void present();
bool reclaim_memory();
void set_evict_handler(bool (*fn)());
void border_canvas(float& x0, float& y0, float& x1, float& y1);
bool set_border(const BorderArt* art, const BorderRect* bars, int nbars, const BorderRect* frame, int nframe);
void set_border_scroll(float x);
const uint32_t* framebuffer();
bool capture(uint32_t* out, int out_stride);
bool capture_row(int y, int x, int w, uint32_t* out);

void set_scissor(int x, int y, int w, int h);
void reset_scissor();

void sprite(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, Blend blend, bool smooth);
void draw_quad(Texture* tex, const float uv[8], const float xy[8], uint32_t color, Blend blend, bool smooth);
void fill(float x0, float y0, float x1, float y1, uint32_t color, Blend blend);
void sprite_flat(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t rgb, int alpha, Blend blend,
                 bool smooth);
void quad_flat(Texture* tex, const float uv[8], const float xy[8], uint32_t rgb, int alpha, Blend blend, bool smooth);
void desaturate(float amount);
Texture* grab_screen();

void set_target(Surface s);
Surface current_target();
Texture* surface_texture(Surface s);
int surface_width(Surface s);
int surface_height(Surface s);
void draw_triangles(Texture* tex, const Vertex* v, int count, Blend blend, bool smooth);
void draw_grid(Texture* tex, const Vertex* v, int cols, int rows, Blend blend, bool smooth);
void sprite_lut(Texture* tex, int channel, const uint32_t* lut, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, Blend blend);
void set_color_mask(uint32_t protect_rgb);
void set_wrap(bool repeat);
void set_alpha_weight(bool on);
void stencil_clear(int x0, int y0, int x1, int y1);
void set_stencil(Stencil mode);

}
