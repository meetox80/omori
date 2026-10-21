#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#include "../core/image.h"
#include "../gfx2d/display.h"
#include "../platform/gfx.h"

namespace ui {
namespace fx {

#if defined(PSP) || defined(__3DS__) || defined(__PS2__)
using real = float;
#else
using real = double;
#endif

namespace core {

using DrawFn = std::function<void(const g2d::RenderState&)>;

constexpr real SCREEN_LW = 640.0;
constexpr real SCREEN_LH = 480.0;
constexpr real PI = 3.14159265358979323846;

struct Box {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
    int w() const { return x1 - x0; }
    int h() const { return y1 - y0; }
};

Box intersect(const Box& a, const Box& b);
Box expand(const Box& b, int n);
Box game_box();

inline real phys_x(real lx) { return lx * images::SCALE + images::OFFSET_X; }
inline real phys_y(real ly) { return ly * images::SCALE + images::OFFSET_Y; }
inline real logic_x(real px) { return (px - images::OFFSET_X) / images::SCALE; }
inline real logic_y(real py) { return (py - images::OFFSET_Y) / images::SCALE; }
inline real half_x(real px) { return (px - images::OFFSET_X) * 0.5 + 1.0; }
inline real half_y(real py) { return (py - images::OFFSET_Y) * 0.5 + 1.0; }
inline real phys_of_half_x(real hx) { return (hx - 1.0) * 2.0 + images::OFFSET_X; }
inline real phys_of_half_y(real hy) { return (hy - 1.0) * 2.0 + images::OFFSET_Y; }

uint32_t gray(real v);
uint32_t level(real v);
uint32_t rgb_level(real r, real g, real b);

struct Frame {
    real x = 0, y = 0, w = SCREEN_LW, h = SCREEN_LH;
    Box box;
    real fx(real px) const { return logic_x(px) - x; }
    real fy(real py) const { return logic_y(py) - y; }
    real px(real lx) const { return phys_x(lx + x); }
    real py(real ly) const { return phys_y(ly + y); }
};

bool logical_bounds(g2d::DisplayObject& obj, const g2d::Mat& world, g2d::Rect& out);
bool make_frame(g2d::DisplayObject& obj, const g2d::RenderState& rs, bool full_area, real padding, Frame& out);
Box clip_box(const g2d::RenderState& rs);
void scissor(const Box& b);
void restore_scissor(const g2d::RenderState& rs);
g2d::RenderState with_clip(const g2d::RenderState& rs, const Box& b);

void blit(gfx::Texture* t, real u0, real v0, real u1, real v1, real x0, real y0, real x1, real y1, uint32_t color, gfx::Blend blend,
          bool smooth, real strip = 64.0);
void blit_box(gfx::Texture* t, const Box& b, uint32_t color, gfx::Blend blend);
void fill(const Box& b, uint32_t color, gfx::Blend blend);
void scale_box(const Box& b, real factor);
void lut_box(gfx::Texture* src, int channel, const uint32_t* lut, const Box& sb, const Box& db, gfx::Blend blend);

struct Mesh {
    int cols = 0, rows = 0;
    std::vector<gfx::Vertex> v;
    void build(const Box& b, real cell);
    gfx::Vertex& at(int c, int r) { return v[(size_t)r * (cols + 1) + c]; }
};

void draw_mesh(gfx::Texture* tex, const Mesh& m, gfx::Blend blend, bool smooth);

void mask_begin_write(const Box& b);
void mask_end_write();
void mask_begin_test();
void mask_end_test();

void draw_noise(const Box& b, real amount, real seed, real cell, real anchor_x, real anchor_y);

class IsolationLock {
public:
    IsolationLock();
    ~IsolationLock();
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

void layers_full(const Frame& f, const g2d::RenderState& rs, const DrawFn& draw);
Box layers_half(const Frame& f, const g2d::RenderState& rs, const DrawFn& draw);
void composite_full_mesh(const Mesh& m);
void composite_half(const Box& hb, const Box& b);

Box half_box(const Box& b);
Box phys_box_of_half(const Box& hb);
void half_ring(gfx::Surface s, const Box& hb, uint32_t color);
Box downsample(gfx::Texture* src, const Box& b, gfx::Surface dst, uint32_t ring);
void upsample(gfx::Surface src, const Box& hb, const Box& b, gfx::Blend blend, uint32_t color);

struct Tap {
    real dx = 0, dy = 0, w = 0;
};

std::vector<Tap> gaussian_taps(real sigma, bool horizontal);
void draw_taps(gfx::Texture* src, const Box& dst, const Box& valid, const std::vector<Tap>& taps, real tex_scale);
void gaussian_half(gfx::Surface buf, gfx::Surface tmp, const Box& hb, real sigma_x, real sigma_y);
void gaussian_screen(const Box& b, real sigma_x, real sigma_y);
gfx::Surface kawase_half(gfx::Surface a, gfx::Surface b, const Box& hb, const std::vector<real>& offsets);
real pixi_blur_sigma(real blur, real quality);

}
}
}
