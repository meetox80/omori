#pragma once
#include <cstddef>
#include <cstdint>

namespace gfx {

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 272;

enum class Format : uint8_t { RGBA8888 = 0, RGBA4444 = 1, RGB565 = 2, RGBA5551 = 3, T8 = 4 };
enum class Blend : uint8_t { Normal = 0, Add = 1, Multiply = 2, Screen = 3, Subtract = 4, Copy = 5, MulSrc = 6, MulAdd = 7, Max = 8, Min = 9, MulSub = 10 };
enum class Surface : uint8_t { Screen = 0, Grab = 1, Half0 = 2, Half1 = 3, Half2 = 4, Wide = 5 };
enum class Stencil : uint8_t { Off = 0, Write = 1, Test = 2 };

constexpr int HALF_W = 240;
constexpr int HALF_H = 138;

struct Vertex {
    float u, v;
    uint32_t color;
    float x, y;
};

struct Texture;

inline uint32_t rgba(int r, int g, int b, int a = 255) {
    return (uint32_t)(r & 255) | ((uint32_t)(g & 255) << 8) | ((uint32_t)(b & 255) << 16) | ((uint32_t)(a & 255) << 24);
}

inline uint32_t premul(int r, int g, int b, int a) {
    return rgba(r * a / 255, g * a / 255, b * a / 255, a);
}

void* alloc(size_t bytes);
void release(void* p);

Texture* texture_create(int w, int h, int stride, int rows, Format fmt, bool swizzled, void* pixels, uint32_t* palette);
Texture* texture_wrap(int w, int h, int stride, uint32_t* pixels);
void texture_touch(Texture* t);
bool texture_read(const Texture* t, int x, int y, uint32_t* out);
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
void stencil_clear(int x0, int y0, int x1, int y1);
void set_stencil(Stencil mode);

}
