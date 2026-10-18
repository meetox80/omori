#include <algorithm>
#include <cmath>
#include <cstring>

#include "fx_filters.h"

namespace ui {
namespace fx {

using core::Box;
using core::Frame;
using core::Mesh;
using gfx::Blend;
using gfx::Surface;

static const uint32_t BLACK = 0xFF000000u;
static const uint32_t WHITE = 0xFFFFFFFFu;
static const real S = images::SCALE;
static const uint32_t PROTECT[3] = {0x00FFFF00u, 0x00FF00FFu, 0x0000FFFFu};

static void destroy_texture(gfx::Texture*& t) {
    if (t) gfx::texture_destroy(t);
    t = nullptr;
}

static gfx::Texture* dyn_texture(int w, int h, uint32_t*& px) {
    px = (uint32_t*)gfx::alloc((size_t)w * h * 4);
    std::memset(px, 0, (size_t)w * h * 4);
    return gfx::texture_wrap(w, h, w, px);
}

static void dyn_destroy(gfx::Texture*& t, uint32_t*& px) {
    destroy_texture(t);
    if (px) gfx::release(px);
    px = nullptr;
}

static int next_pow2(real v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

static real clamp01(real v) { return std::clamp(v, 0.0, 1.0); }

static void fill_lut_third(uint32_t* lut, real div) {
    for (int v = 0; v < 256; v++) lut[v] = core::gray(v / 255.0 / div);
}

void TwistFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    Mesh m;
    m.build(b, 8.0);
    for (auto& p : m.v) {
        real lx = core::logic_x(p.x), ly = core::logic_y(p.y);
        real cx = lx - offset[0], cy = ly - offset[1];
        real dist = std::sqrt(cx * cx + cy * cy);
        if (dist < radius) {
            real ratio = (radius - dist) / radius;
            real a = ratio * ratio * angle;
            real s = std::sin(a), c = std::cos(a);
            real nx = cx * c - cy * s, ny = cx * s + cy * c;
            cx = nx;
            cy = ny;
        }
        p.u = (float)std::clamp(core::phys_x(offset[0] + cx), b.x0 + 0.5, b.x1 - 0.5);
        p.v = (float)std::clamp(core::phys_y(offset[1] + cy), b.y0 + 0.5, b.y1 - 0.5);
    }
    warp_pass(f, rs, draw, m);
}

static real smoothstep(real e0, real e1, real x) {
    real t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void BulgePinchFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    Mesh m;
    m.build(b, 8.0);
    real potw = next_pow2(f.w), poth = next_pow2(f.h);
    real cxp = center[0] * f.w, cyp = center[1] * f.h;
    for (auto& p : m.v) {
        real x = f.fx(p.x) - cxp, y = f.fy(p.y) - cyp;
        real d = std::sqrt(x * x + y * y);
        if (d < radius && d > 0) {
            real percent = d / radius;
            real k;
            if (strength > 0) {
                real mixv = strength * 0.75;
                k = 1.0 + (smoothstep(0.0, radius / d, percent) - 1.0) * mixv;
            } else {
                real target = std::pow(percent, 1.0 + strength * 0.75) * radius / d;
                k = 1.0 + (target - 1.0) * (1.0 - percent);
            }
            x *= k;
            y *= k;
        }
        real sx = x + cxp, sy = y + cyp;
        real qx = std::clamp(sx, 0.0, f.w - 1), qy = std::clamp(sy, 0.0, f.h - 1);
        real factor = 1.0;
        if (qx != sx || qy != sy) {
            real ex = (sx - qx) / potw, ey = (sy - qy) / poth;
            factor = std::max(0.0, 1.0 - std::sqrt(ex * ex + ey * ey));
        }
        p.u = (float)std::clamp(f.px(qx), b.x0 + 0.5, b.x1 - 0.5);
        p.v = (float)std::clamp(f.py(qy), b.y0 + 0.5, b.y1 - 0.5);
        p.color = core::level(factor);
    }
    warp_pass(f, rs, draw, m);
}

bool RgbSplitFx::identity() const { return !(red[0] || red[1] || green[0] || green[1] || blue[0] || blue[1]); }

void RgbSplitFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    const real* ch[3] = {red, green, blue};
    gfx::Texture* src;
    Blend blend;
    core::IsolationLock lock;
    if (scope == Scope::Screen) {
        draw(rs);
        src = gfx::grab_screen();
        core::scissor(b);
        core::fill(b, BLACK, Blend::Copy);
        blend = Blend::Copy;
    } else {
        if (!lock.ok()) {
            draw(rs);
            return;
        }
        core::layers_full(f, rs, draw);
        core::scissor(b);
        core::blit_box(gfx::surface_texture(Surface::Wide), b, WHITE, Blend::MulSub);
        src = gfx::surface_texture(Surface::Grab);
        blend = Blend::Add;
    }
    for (int c = 0; c < 3; c++) {
        real ox = ch[c][0], oy = ch[c][1];
        real x0 = std::max<real>(b.x0, f.px(-ox)), x1 = std::min<real>(b.x1, f.px(f.w - ox));
        real y0 = std::max<real>(b.y0, f.py(-oy)), y1 = std::min<real>(b.y1, f.py(f.h - oy));
        if (x1 <= x0 || y1 <= y0) continue;
        gfx::set_color_mask(PROTECT[c]);
        real sx = ox * S, sy = oy * S;
        bool frac = std::fabs(sx - std::round(sx)) > 1e-3 || std::fabs(sy - std::round(sy)) > 1e-3;
        core::blit(src, x0 + sx, y0 + sy, x1 + sx, y1 + sy, x0, y0, x1, y1, WHITE, blend, frac);
    }
    gfx::set_color_mask(0);
    core::restore_scissor(rs);
}

void PixelateFx::cells(gfx::Texture* src, const Box& b, Blend blend) {
    real sx = std::max(size[0], 1e-3), sy = std::max(size[1], 1e-3);
    real lx0 = core::logic_x(b.x0), lx1 = core::logic_x(b.x1);
    real ly0 = core::logic_y(b.y0), ly1 = core::logic_y(b.y1);
    int i0 = (int)std::floor(lx0 / sx), i1 = (int)std::ceil(lx1 / sx);
    int j0 = (int)std::floor(ly0 / sy), j1 = (int)std::ceil(ly1 / sy);
    const int TW = gfx::HALF_W - 2, TH = gfx::HALF_H - 2;
    real cw = sx * S, chh = sy * S;
    for (int tj = j0; tj < j1; tj += TH) {
        for (int ti = i0; ti < i1; ti += TW) {
            int nx = std::min(TW, i1 - ti), ny = std::min(TH, j1 - tj);
            real u0 = core::phys_x(ti * sx) - 0.5 * cw, v0 = core::phys_y(tj * sy) - 0.5 * chh;
            gfx::set_target(Surface::Half0);
            gfx::reset_scissor();
            core::blit(src, u0, v0, u0 + nx * cw, v0 + ny * chh, 1, 1, 1 + nx, 1 + ny, WHITE, Blend::Copy, true);
            gfx::set_target(Surface::Screen);
            core::scissor(b);
            core::blit(gfx::surface_texture(Surface::Half0), 1, 1, 1 + nx, 1 + ny, core::phys_x(ti * sx), core::phys_y(tj * sy),
                       core::phys_x((ti + nx) * sx), core::phys_y((tj + ny) * sy), WHITE, blend, false);
        }
    }
}

void PixelateFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    if (scope == Scope::Screen) {
        draw(rs);
        gfx::Texture* g = gfx::grab_screen();
        cells(g, b, Blend::Copy);
        core::restore_scissor(rs);
        return;
    }
    core::IsolationLock lock;
    if (!lock.ok()) {
        draw(rs);
        return;
    }
    core::layers_full(f, rs, draw);
    cells(gfx::surface_texture(Surface::Wide), b, Blend::MulSub);
    cells(gfx::surface_texture(Surface::Grab), b, Blend::Add);
    core::restore_scissor(rs);
}

bool AdjustmentFx::identity() const {
    return gamma == 1 && saturation == 1 && contrast == 1 && brightness == 1 && red == 1 && green == 1 && blue == 1 && alpha == 1;
}

void AdjustmentFx::passes(const Box& b) {
    gfx::Texture* g = gfx::grab_screen();
    real ig = 1.0 / std::max(gamma, 1e-4);
    real K[3] = {red * brightness * alpha, green * brightness * alpha, blue * brightness * alpha};
    uint32_t lut[256];
    auto G = [&](int v) { return std::pow(v / 255.0, ig); };
    if (std::fabs(saturation - 1.0) < 1e-6) {
        core::fill(b, BLACK, Blend::Copy);
        for (int c = 0; c < 3; c++) {
            for (int v = 0; v < 256; v++) {
                real o = clamp01((0.5 + contrast * (G(v) - 0.5)) * K[c]);
                int q = (int)std::lround(o * 255.0);
                lut[v] = gfx::rgba(c == 0 ? q : 0, c == 1 ? q : 0, c == 2 ? q : 0, 255);
            }
            core::lut_box(g, c, lut, b, b, Blend::Add);
        }
        return;
    }
    const real w[3] = {0.2125, 0.7154, 0.0721};
    real M[3][3], cst[3];
    for (int k = 0; k < 3; k++) {
        for (int c = 0; c < 3; c++) M[k][c] = K[k] * contrast * (saturation * (k == c ? 1.0 : 0.0) + (1.0 - saturation) * w[c]);
        cst[k] = K[k] * 0.5 * (1.0 - contrast);
    }
    core::fill(b, core::rgb_level(clamp01(0.5 + cst[0] * 0.5), clamp01(0.5 + cst[1] * 0.5), clamp01(0.5 + cst[2] * 0.5)), Blend::Copy);
    for (int sign = 0; sign < 2; sign++) {
        for (int c = 0; c < 3; c++) {
            bool any = false;
            for (int v = 0; v < 256; v++) {
                real o[3];
                for (int k = 0; k < 3; k++) {
                    real t = M[k][c] * G(v) * 0.5;
                    o[k] = sign == 0 ? std::max(t, 0.0) : std::max(-t, 0.0);
                    any = any || o[k] > 0;
                }
                lut[v] = core::rgb_level(clamp01(o[0]), clamp01(o[1]), clamp01(o[2]));
            }
            if (any) core::lut_box(g, c, lut, b, b, sign == 0 ? Blend::Add : Blend::Subtract);
        }
    }
    gfx::Texture* g2 = gfx::grab_screen();
    core::fill(b, BLACK, Blend::Copy);
    for (int c = 0; c < 3; c++) {
        for (int v = 0; v < 256; v++) {
            int q = (int)std::lround(clamp01(2.0 * (v / 255.0) - 1.0) * 255.0);
            lut[v] = gfx::rgba(c == 0 ? q : 0, c == 1 ? q : 0, c == 2 ? q : 0, 255);
        }
        core::lut_box(g2, c, lut, b, b, Blend::Add);
    }
}

void AdjustmentFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    color_pass(f, rs, draw, [&]() { passes(f.box); });
}

std::vector<core::Tap> MotionBlurFx::tap_list(real scale) const {
    std::vector<core::Tap> taps;
    int ks = kernel_size;
    int k = ks - 1;
    real vlen = std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1]);
    real off = -offset / vlen - 0.5;
    taps.push_back(core::Tap{0, 0, 1.0 / ks});
    for (int i = 0; i < k; i++) {
        real t = (real)i / k + off;
        taps.push_back(core::Tap{velocity[0] * t * scale, velocity[1] * t * scale, 1.0 / ks});
    }
    return taps;
}

void MotionBlurFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    if (scope == Scope::Screen) {
        draw(rs);
        gfx::Texture* g = gfx::grab_screen();
        core::scissor(b);
        core::draw_taps(g, b, b, tap_list(S), 1.0);
        core::restore_scissor(rs);
        return;
    }
    core::IsolationLock lock;
    if (!lock.ok()) {
        draw(rs);
        return;
    }
    Box hb = core::layers_half(f, rs, draw);
    std::vector<core::Tap> taps = tap_list(S * 0.5);
    gfx::set_target(Surface::Half2);
    gfx::reset_scissor();
    core::draw_taps(gfx::surface_texture(Surface::Half1), hb, hb, taps, 1.0);
    core::half_ring(Surface::Half2, hb, BLACK);
    gfx::set_target(Surface::Half1);
    gfx::reset_scissor();
    core::draw_taps(gfx::surface_texture(Surface::Half0), hb, hb, taps, 1.0);
    core::half_ring(Surface::Half1, hb, BLACK);
    gfx::set_target(Surface::Screen);
    core::scissor(b);
    core::upsample(Surface::Half2, hb, b, Blend::MulSub, WHITE);
    core::upsample(Surface::Half1, hb, b, Blend::Add, WHITE);
    core::restore_scissor(rs);
}

void RadialBlurFx::taps(const Frame& f, gfx::Texture* src, const Box& b, bool half) {
    int ks = kernel_size;
    int k = ks - 1;
    real radian = angle * core::PI / 180.0;
    real gradient = radius * 0.3;
    real rad = radius - gradient * 0.5;
    Mesh m;
    m.build(b, half ? 8.0 : 16.0);
    size_t nv = m.v.size();
    std::vector<real> step(nv), fx(nv), fy(nv);
    for (size_t i = 0; i < nv; i++) {
        real px = half ? core::phys_of_half_x(m.v[i].x) : m.v[i].x;
        real py = half ? core::phys_of_half_y(m.v[i].y) : m.v[i].y;
        fx[i] = f.fx(px) - center[0];
        fy[i] = f.fy(py) - center[1];
        real dist = std::sqrt(fx[i] * fx[i] + fy[i] * fy[i]);
        real st = radian;
        if (rad >= 0 && dist > rad) {
            real sc = 1.0 - std::fabs((dist - rad) / gradient);
            st = sc <= 0 ? 0.0 : st * sc;
        }
        step[i] = st / k;
    }
    std::vector<int> c((size_t)ks, 255 / ks);
    c[0] += 255 - (255 / ks) * ks;
    core::fill(b, BLACK, Blend::Copy);
    for (int j = 0; j < ks; j++) {
        for (size_t i = 0; i < nv; i++) {
            real a = j * step[i];
            real co = std::cos(a), si = std::sin(a);
            real rx = co * fx[i] + si * fy[i], ry = -si * fx[i] + co * fy[i];
            real qx = f.px(rx + center[0]), qy = f.py(ry + center[1]);
            if (half) {
                qx = core::half_x(qx);
                qy = core::half_y(qy);
            }
            m.v[i].u = (float)std::clamp(qx, b.x0 + 0.5, b.x1 - 0.5);
            m.v[i].v = (float)std::clamp(qy, b.y0 + 0.5, b.y1 - 0.5);
            m.v[i].color = gfx::rgba(c[(size_t)j], c[(size_t)j], c[(size_t)j], c[(size_t)j]);
        }
        core::draw_mesh(src, m, Blend::Add, true);
    }
}

void RadialBlurFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    if (scope == Scope::Screen) {
        draw(rs);
        gfx::Texture* g = gfx::grab_screen();
        core::scissor(b);
        taps(f, g, b, false);
        core::restore_scissor(rs);
        return;
    }
    core::IsolationLock lock;
    if (!lock.ok()) {
        draw(rs);
        return;
    }
    Box hb = core::layers_half(f, rs, draw);
    gfx::set_target(Surface::Half2);
    gfx::reset_scissor();
    taps(f, gfx::surface_texture(Surface::Half1), hb, true);
    core::half_ring(Surface::Half2, hb, BLACK);
    gfx::set_target(Surface::Half1);
    gfx::reset_scissor();
    taps(f, gfx::surface_texture(Surface::Half0), hb, true);
    core::half_ring(Surface::Half1, hb, BLACK);
    gfx::set_target(Surface::Screen);
    core::scissor(b);
    core::upsample(Surface::Half2, hb, b, Blend::MulSub, WHITE);
    core::upsample(Surface::Half1, hb, b, Blend::Add, WHITE);
    core::restore_scissor(rs);
}

namespace {

struct V3 {
    real x, y, z;
};
struct V4 {
    real x, y, z, w;
};

real gmod(real x, real y) { return x - y * std::floor(x / y); }
real fract(real x) { return x - std::floor(x); }
real mod289(real x) { return x - std::floor(x * (1.0 / 289.0)) * 289.0; }
real permute(real x) { return mod289(((x * 34.0) + 1.0) * x); }
real inv_sqrt(real r) { return 1.79284291400159 - 0.85373472095314 * r; }
real fade(real t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
real mixd(real a, real b, real t) { return a + (b - a) * t; }
real dot3(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

real pnoise(const V3& P, const V3& rep) {
    V3 Pi0{gmod(std::floor(P.x), rep.x), gmod(std::floor(P.y), rep.y), gmod(std::floor(P.z), rep.z)};
    V3 Pi1{gmod(Pi0.x + 1.0, rep.x), gmod(Pi0.y + 1.0, rep.y), gmod(Pi0.z + 1.0, rep.z)};
    Pi0 = V3{mod289(Pi0.x), mod289(Pi0.y), mod289(Pi0.z)};
    Pi1 = V3{mod289(Pi1.x), mod289(Pi1.y), mod289(Pi1.z)};
    V3 Pf0{fract(P.x), fract(P.y), fract(P.z)};
    V3 Pf1{Pf0.x - 1.0, Pf0.y - 1.0, Pf0.z - 1.0};
    real ix[4] = {Pi0.x, Pi1.x, Pi0.x, Pi1.x};
    real iy[4] = {Pi0.y, Pi0.y, Pi1.y, Pi1.y};
    real gx[2][4], gy[2][4], gz[2][4];
    for (int s = 0; s < 2; s++) {
        real iz = s == 0 ? Pi0.z : Pi1.z;
        for (int k = 0; k < 4; k++) {
            real ixy = permute(permute(ix[k]) + iy[k]);
            real v = permute(ixy + iz);
            real x = v * (1.0 / 7.0);
            real y = fract(std::floor(x) * (1.0 / 7.0)) - 0.5;
            x = fract(x);
            real z = 0.5 - std::fabs(x) - std::fabs(y);
            real sz = z <= 0.0 ? 1.0 : 0.0;
            x -= sz * ((x < 0.0 ? 0.0 : 1.0) - 0.5);
            y -= sz * ((y < 0.0 ? 0.0 : 1.0) - 0.5);
            gx[s][k] = x;
            gy[s][k] = y;
            gz[s][k] = z;
        }
    }
    V3 g[2][4];
    for (int s = 0; s < 2; s++) {
        for (int k = 0; k < 4; k++) g[s][k] = V3{gx[s][k], gy[s][k], gz[s][k]};
        int order[4] = {0, 2, 1, 3};
        for (int k = 0; k < 4; k++) {
            V3& q = g[s][order[k]];
            real n = inv_sqrt(dot3(q, q));
            q = V3{q.x * n, q.y * n, q.z * n};
        }
    }
    real n000 = dot3(g[0][0], Pf0);
    real n100 = dot3(g[0][1], V3{Pf1.x, Pf0.y, Pf0.z});
    real n010 = dot3(g[0][2], V3{Pf0.x, Pf1.y, Pf0.z});
    real n110 = dot3(g[0][3], V3{Pf1.x, Pf1.y, Pf0.z});
    real n001 = dot3(g[1][0], V3{Pf0.x, Pf0.y, Pf1.z});
    real n101 = dot3(g[1][1], V3{Pf1.x, Pf0.y, Pf1.z});
    real n011 = dot3(g[1][2], V3{Pf0.x, Pf1.y, Pf1.z});
    real n111 = dot3(g[1][3], Pf1);
    real fx = fade(Pf0.x), fy = fade(Pf0.y), fz = fade(Pf0.z);
    real z0 = mixd(n000, n001, fz), z1 = mixd(n100, n101, fz), z2 = mixd(n010, n011, fz), z3 = mixd(n110, n111, fz);
    real y0 = mixd(z0, z2, fy), y1 = mixd(z1, z3, fy);
    return 2.2 * mixd(y0, y1, fx);
}

real turb(const V3& P, const V3& rep, real lacunarity, real gain) {
    real sum = 0, sc = 1, total = 1;
    for (int i = 0; i < 6; i++) {
        sum += total * pnoise(V3{P.x * sc, P.y * sc, P.z * sc}, rep);
        sc *= lacunarity;
        total *= gain;
    }
    return std::fabs(sum);
}

}

GodrayFx::GodrayFx() {}

GodrayFx::~GodrayFx() { dyn_destroy(strip_, strip_px_); }

void GodrayFx::mist(const Frame& f) {
    const Box& b = f.box;
    const int N = 256;
    if (!strip_) strip_ = dyn_texture(N, 2, strip_px_);
    real aspect = f.h / f.w;
    real lcx = std::cos(angle * core::PI / 180.0), lsy = std::sin(angle * core::PI / 180.0);
    auto dval = [&](real cx, real cy) {
        if (parallel) return lcx * cx + lsy * cy * aspect;
        real dx = cx - center[0] / f.w, dy = (cy - center[1] / f.h) * aspect;
        real dis = std::sqrt(dx * dx + dy * dy) + 0.00001;
        return dy / dis;
    };
    real dmin = -1, dmax = 1;
    if (parallel) {
        real c[4] = {dval(0, 0), dval(1, 0), dval(0, 1), dval(1, 1)};
        dmin = *std::min_element(c, c + 4);
        dmax = *std::max_element(c, c + 4);
        if (dmax - dmin < 1e-6) dmax = dmin + 1e-6;
    }
    std::vector<real> vals(N);
    real fmax = 1e-6;
    for (int i = 0; i < N; i++) {
        real d = dmin + (i + 0.5) / N * (dmax - dmin);
        V3 p{d + time * 0.05, d, (62.1 + time) * 0.05};
        real n = turb(p, V3{480.0, 320.0, 480.0}, lacunarity, gain) * 0.7;
        vals[(size_t)i] = n;
        fmax = std::max(fmax, n);
    }
    for (int i = 0; i < N; i++) {
        uint32_t g = core::gray(vals[(size_t)i] / fmax);
        strip_px_[i] = g;
        strip_px_[N + i] = g;
    }
    gfx::texture_touch(strip_);
    Mesh m;
    m.build(b, parallel ? 16.0 : 8.0);
    std::vector<real> amt(m.v.size());
    real top = 0;
    for (size_t i = 0; i < m.v.size(); i++) {
        gfx::Vertex& p = m.v[i];
        real cx = f.fx(p.x) / f.w, cy = f.fy(p.y) / f.h;
        p.u = (float)((dval(cx, cy) - dmin) / (dmax - dmin) * N);
        p.v = 1.0f;
        amt[i] = std::max(0.0, (1.0 - cy) * strength * fmax);
        top = std::max(top, amt[i]);
    }
    for (int pass = 0; pass < (int)std::ceil(top - 1e-6); pass++) {
        for (size_t i = 0; i < m.v.size(); i++) m.v[i].color = core::gray(std::clamp(amt[i] - pass, 0.0, 1.0));
        core::draw_mesh(strip_, m, Blend::Add, true);
    }
}

void GodrayFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    if (scope == Scope::Screen) {
        draw(rs);
        core::scissor(b);
        mist(f);
        core::restore_scissor(rs);
        return;
    }
    core::IsolationLock lock;
    if (!lock.ok()) {
        draw(rs);
        return;
    }
    core::layers_full(f, rs, draw);
    gfx::set_target(Surface::Wide);
    core::scissor(b);
    core::fill(b, core::gray(std::min(1.0, strength)), Blend::Add);
    gfx::set_target(Surface::Screen);
    core::scissor(b);
    core::blit_box(gfx::surface_texture(Surface::Wide), b, WHITE, Blend::MulSub);
    core::blit_box(gfx::surface_texture(Surface::Grab), b, WHITE, Blend::Add);
    mist(f);
    core::restore_scissor(rs);
}

void ReflectionFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    int ystart = std::max(b.y0, (int)std::ceil(f.py(boundary * f.h) - 0.5));
    if (ystart >= b.y1) {
        draw(rs);
        return;
    }
    Box below{b.x0, ystart, b.x1, b.y1};
    Mesh m;
    m.cols = 4;
    m.rows = std::max(1, (below.h() + 1) / 2);
    m.v.assign((size_t)(m.cols + 1) * (m.rows + 1), gfx::Vertex{0, 0, WHITE, 0, 0});
    for (int r = 0; r <= m.rows; r++) {
        real y = below.y0 + (real)below.h() * r / m.rows;
        real fy = f.fy(y);
        real cy = fy / f.h;
        real k = (cy - boundary) / (1.0 - boundary + 0.0001);
        real vpx = 2.0 * boundary * f.h - fy;
        real ysrc = mirror ? vpx : fy;
        real amp = (amplitude[1] - amplitude[0]) * k + amplitude[0];
        real wl = (wave_length[1] - wave_length[0]) * k + wave_length[0];
        real al = (alpha[1] - alpha[0]) * k + alpha[0];
        real shift = wl != 0 ? std::cos(vpx * 6.28 / wl - time) * amp : 0.0;
        for (int c = 0; c <= m.cols; c++) {
            gfx::Vertex& p = m.at(c, r);
            p.x = (float)(below.x0 + (real)below.w() * c / m.cols);
            p.y = (float)y;
            real xsrc = std::clamp(f.fx(p.x) + shift, 0.0, f.w - 1);
            p.u = (float)std::clamp(f.px(xsrc), b.x0 + 0.5, b.x1 - 0.5);
            p.v = (float)std::clamp(f.py(std::clamp(ysrc, 0.0, f.h - 1)), b.y0 + 0.5, b.y1 - 0.5);
            p.color = core::level(clamp01(al));
        }
    }
    if (scope == Scope::Screen) {
        draw(rs);
        gfx::Texture* g = gfx::grab_screen();
        core::scissor(below);
        core::draw_mesh(g, m, Blend::Copy, true);
        core::restore_scissor(rs);
        return;
    }
    core::IsolationLock lock;
    if (!lock.ok()) {
        draw(rs);
        return;
    }
    core::layers_full(f, rs, draw);
    Mesh above;
    if (ystart > b.y0) {
        above.build(Box{b.x0, b.y0, b.x1, ystart}, 1e9);
        core::scissor(Box{b.x0, b.y0, b.x1, ystart});
        core::composite_full_mesh(above);
    }
    core::scissor(below);
    core::composite_full_mesh(m);
    core::restore_scissor(rs);
}

void GlowFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    core::IsolationLock lock;
    if (!lock.ok()) {
        draw(rs);
        return;
    }
    Box hb = core::layers_half(f, rs, draw);
    real num = 0, den = 0;
    for (real r = 1.0; r <= distance; r += 1.0) {
        num += (distance - r) * r * r;
        den += distance - r;
    }
    real sigma = den > 0 ? std::sqrt(num / den * 0.5) : 0.0;
    real sh = sigma * S * 0.5;
    Box ring = core::expand(hb, 1);
    gfx::set_target(Surface::Half2);
    gfx::reset_scissor();
    core::blit_box(gfx::surface_texture(Surface::Half1), ring, WHITE, Blend::Copy);
    core::gaussian_half(Surface::Half2, Surface::Grab, hb, sh, sh);
    real gc[3] = {((color >> 16) & 255) / 255.0, ((color >> 8) & 255) / 255.0, (color & 255) / 255.0};
    auto add_colored = [&](Surface src, real strength) {
        real rem = strength;
        while (rem > 1e-4) {
            real k = std::min(1.0, rem);
            core::upsample(src, hb, b, Blend::Add, core::rgb_level(gc[0] * k, gc[1] * k, gc[2] * k));
            rem -= k;
        }
    };
    gfx::set_target(Surface::Screen);
    core::scissor(b);
    core::upsample(Surface::Half1, hb, b, Blend::MulSub, WHITE);
    if (outer_strength > 0) {
        gfx::set_target(Surface::Grab);
        gfx::reset_scissor();
        core::blit_box(gfx::surface_texture(Surface::Half2), ring, WHITE, Blend::Copy);
        core::scale_box(hb, outer_strength);
        gfx::set_target(Surface::Screen);
        core::scissor(b);
        core::upsample(Surface::Grab, hb, b, Blend::MulSub, WHITE);
        gfx::set_target(Surface::Grab);
        gfx::reset_scissor();
        core::blit_box(gfx::surface_texture(Surface::Half2), ring, WHITE, Blend::Copy);
        core::blit_box(gfx::surface_texture(Surface::Half1), hb, WHITE, Blend::MulSub);
        gfx::set_target(Surface::Screen);
        core::scissor(b);
        add_colored(Surface::Grab, outer_strength);
    }
    if (inner_strength > 0) {
        gfx::set_target(Surface::Half1);
        gfx::reset_scissor();
        core::blit_box(gfx::surface_texture(Surface::Half2), hb, WHITE, Blend::MulSub);
        gfx::set_target(Surface::Screen);
        core::scissor(b);
        add_colored(Surface::Half1, inner_strength);
        uint32_t lut[256];
        for (int v = 0; v < 256; v++) lut[v] = core::gray(clamp01(1.0 - inner_strength + inner_strength * v / 255.0));
        gfx::set_target(Surface::Grab);
        gfx::reset_scissor();
        core::lut_box(gfx::surface_texture(Surface::Half2), 0, lut, hb, hb, Blend::Copy);
        gfx::set_target(Surface::Half0);
        gfx::reset_scissor();
        core::blit_box(gfx::surface_texture(Surface::Grab), hb, WHITE, Blend::MulSrc);
    }
    gfx::set_target(Surface::Screen);
    core::scissor(b);
    core::upsample(Surface::Half0, hb, b, Blend::Add, WHITE);
    core::restore_scissor(rs);
}

void EmbossFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    color_pass(f, rs, draw, [&]() {
        const Box& b = f.box;
        gfx::Texture* g = gfx::grab_screen();
        uint32_t lut[256];
        fill_lut_third(lut, 6.0);
        core::fill(b, core::gray(0.5), Blend::Copy);
        Box plus{b.x0, b.y0, b.x1 - 1, b.y1 - 1}, minus{b.x0 + 1, b.y0 + 1, b.x1, b.y1};
        Box splus{b.x0 + 1, b.y0 + 1, b.x1, b.y1}, sminus{b.x0, b.y0, b.x1 - 1, b.y1 - 1};
        for (int c = 0; c < 3; c++) core::lut_box(g, c, lut, splus, plus, Blend::Add);
        for (int c = 0; c < 3; c++) core::lut_box(g, c, lut, sminus, minus, Blend::Subtract);
        gfx::Texture* g2 = gfx::grab_screen();
        for (int v = 0; v < 256; v++) lut[v] = core::gray(clamp01(0.5 + 2.0 * strength * (v / 255.0 - 0.5)));
        core::lut_box(g2, 0, lut, b, b, Blend::Copy);
    });
}

static void hatch_lines(const Frame& f, int sign, real shift) {
    real h = 1.0 / S;
    std::vector<gfx::Vertex> v;
    real span = f.w + f.h + 20;
    for (real c = -span; c <= span; c += 10.0) {
        real cc = c + shift;
        real x0 = -f.h - 10, x1 = f.w + 10;
        auto P = [&](real x, real off) {
            real y = sign > 0 ? (cc + off - x) : (x - cc - off);
            return gfx::Vertex{0, 0, BLACK, (float)f.px(x), (float)f.py(y)};
        };
        gfx::Vertex a = P(x0, -h), b = P(x1, -h), cq = P(x1, h), d = P(x0, h);
        v.push_back(a);
        v.push_back(b);
        v.push_back(cq);
        v.push_back(a);
        v.push_back(cq);
        v.push_back(d);
    }
    gfx::draw_triangles(nullptr, v.data(), (int)v.size(), Blend::Copy, false);
}

void CrossHatchFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    gfx::Texture* src;
    core::IsolationLock lock;
    if (scope == Scope::Screen) {
        draw(rs);
        src = gfx::grab_screen();
    } else {
        if (!lock.ok()) {
            draw(rs);
            return;
        }
        gfx::set_target(Surface::Grab);
        core::scissor(b);
        core::fill(b, BLACK, Blend::Copy);
        draw(core::with_clip(rs, b));
        gfx::set_target(Surface::Screen);
        src = gfx::surface_texture(Surface::Grab);
    }
    core::scissor(b);
    uint32_t lut[256];
    for (int v = 0; v < 256; v++) lut[v] = core::gray((v / 255.0) * (v / 255.0) / 3.0);
    core::fill(b, BLACK, Blend::Copy);
    for (int c = 0; c < 3; c++) core::lut_box(src, c, lut, b, b, Blend::Add);
    gfx::Texture* q = gfx::grab_screen();
    core::fill(b, WHITE, Blend::Copy);
    const real thr[4] = {1.0, 0.75, 0.5, 0.3};
    const int sg[4] = {1, -1, 1, -1};
    const real sh[4] = {0, 0, 5, 5};
    for (int l = 0; l < 4; l++) {
        real t = thr[l] * thr[l] / 3.0;
        for (int v = 0; v < 256; v++) lut[v] = (v / 255.0 < t) ? WHITE : 0u;
        core::mask_begin_write(b);
        gfx::set_color_mask(0xFFFFFF);
        core::lut_box(q, 0, lut, b, b, Blend::Copy);
        gfx::set_color_mask(0);
        core::mask_end_write();
        core::mask_begin_test();
        hatch_lines(f, sg[l], sh[l]);
        core::mask_end_test();
    }
    core::restore_scissor(rs);
}

DotFx::DotFx() {
    const int N = 64;
    std::vector<uint32_t> p((size_t)N * N), n((size_t)N * N);
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++) {
            real v = std::sin(2.0 * core::PI * (i + 0.5) / N) * std::sin(2.0 * core::PI * (j + 0.5) / N) * 0.4;
            p[(size_t)j * N + i] = core::gray(std::max(v, 0.0));
            n[(size_t)j * N + i] = core::gray(std::max(-v, 0.0));
        }
    }
    pos_ = make_texture(N, N, p);
    neg_ = make_texture(N, N, n);
}

DotFx::~DotFx() {
    destroy_texture(pos_);
    destroy_texture(neg_);
}

void DotFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    color_pass(f, rs, draw, [&]() {
        const Box& b = f.box;
        const int N = 64;
        gfx::Texture* g = gfx::grab_screen();
        uint32_t lut[256];
        fill_lut_third(lut, 3.0);
        core::fill(b, BLACK, Blend::Copy);
        for (int c = 0; c < 3; c++) core::lut_box(g, c, lut, b, b, Blend::Add);
        Mesh m;
        m.build(b, 1e9);
        real s = std::sin(angle), c = std::cos(angle);
        for (auto& p : m.v) {
            real tx = f.fx(p.x), ty = f.fy(p.y);
            real px = (c * tx - s * ty) * scale, py = (s * tx + c * ty) * scale;
            p.u = (float)(px / (2.0 * core::PI) * N);
            p.v = (float)(py / (2.0 * core::PI) * N);
        }
        gfx::set_wrap(true);
        core::draw_mesh(pos_, m, Blend::Add, true);
        core::draw_mesh(neg_, m, Blend::Subtract, true);
        gfx::set_wrap(false);
        gfx::Texture* g2 = gfx::grab_screen();
        for (int v = 0; v < 256; v++) lut[v] = core::gray(clamp01(10.0 * (v / 255.0 - 0.5)));
        core::lut_box(g2, 0, lut, b, b, Blend::Copy);
    });
}

static const real ASCII_N[8] = {65536.0, 65600.0, 332772.0, 15255086.0, 23385164.0, 15252014.0, 13199452.0, 11512810.0};
static const real ASCII_T[9] = {-1.0, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 2.0};

AsciiFx::AsciiFx() {
    const int G = 32;
    for (int l = 0; l < 8; l++) {
        std::vector<uint32_t> px((size_t)G * G);
        for (int j = 0; j < G; j++) {
            for (int i = 0; i < G; i++) {
                real tx = (i + 0.5) / G, ty = (j + 0.5) / G;
                int qx = (int)std::floor((2.0 * tx - 1.0) * 4.0 + 2.5), qy = (int)std::floor((2.0 * ty - 1.0) * -4.0 + 2.5);
                bool on = false;
                if (qx >= 0 && qx <= 4 && qy >= 0 && qy <= 4) on = ((int64_t)std::floor(ASCII_N[l] / std::exp2(qx + 5.0 * qy)) % 2) == 1;
                px[(size_t)j * G + i] = on ? WHITE : BLACK;
            }
        }
        glyphs_[(size_t)l] = make_texture(G, G, px);
    }
}

AsciiFx::~AsciiFx() {
    for (auto& g : glyphs_) destroy_texture(g);
}

void AsciiFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    draw(rs);
    gfx::Texture* src = gfx::grab_screen();
    const int G = 32;
    real sz = size, cw = sz * S;
    real lx0 = core::logic_x(b.x0), lx1 = core::logic_x(b.x1), ly0 = core::logic_y(b.y0), ly1 = core::logic_y(b.y1);
    int i0 = (int)std::floor(lx0 / sz), i1 = (int)std::ceil(lx1 / sz), j0 = (int)std::floor(ly0 / sz), j1 = (int)std::ceil(ly1 / sz);
    const int TW = gfx::HALF_W - 2, TH = gfx::HALF_H - 2;
    uint32_t avg[256], lvl[256];
    fill_lut_third(avg, 3.0);
    for (int tj = j0; tj < j1; tj += TH) {
        for (int ti = i0; ti < i1; ti += TW) {
            int nx = std::min(TW, i1 - ti), ny = std::min(TH, j1 - tj);
            Box hb{1, 1, 1 + nx, 1 + ny};
            real u0 = core::phys_x(ti * sz) - 0.5 * cw, v0 = core::phys_y(tj * sz) - 0.5 * cw;
            gfx::set_target(Surface::Half0);
            gfx::reset_scissor();
            core::blit(src, u0, v0, u0 + nx * cw, v0 + ny * cw, 1, 1, 1 + nx, 1 + ny, WHITE, Blend::Copy, true);
            gfx::set_target(Surface::Half1);
            gfx::reset_scissor();
            core::fill(hb, BLACK, Blend::Copy);
            for (int c = 0; c < 3; c++) core::lut_box(gfx::surface_texture(Surface::Half0), c, avg, hb, hb, Blend::Add);
            gfx::set_target(Surface::Screen);
            Box tile{(int)std::floor(core::phys_x(ti * sz)), (int)std::floor(core::phys_y(tj * sz)), (int)std::ceil(core::phys_x((ti + nx) * sz)),
                     (int)std::ceil(core::phys_y((tj + ny) * sz))};
            tile = core::intersect(tile, b);
            if (tile.empty()) continue;
            core::scissor(tile);
            real dx0 = core::phys_x(ti * sz), dy0 = core::phys_y(tj * sz), dx1 = core::phys_x((ti + nx) * sz), dy1 = core::phys_y((tj + ny) * sz);
            core::blit(gfx::surface_texture(Surface::Half0), 1, 1, 1 + nx, 1 + ny, dx0, dy0, dx1, dy1, WHITE, Blend::Copy, false);
            for (int l = 0; l < 8; l++) {
                for (int v = 0; v < 256; v++) {
                    real gv = v / 255.0;
                    lvl[v] = (gv > ASCII_T[l] && gv <= ASCII_T[l + 1]) ? WHITE : 0u;
                }
                core::mask_begin_write(tile);
                gfx::set_color_mask(0xFFFFFF);
                gfx::sprite_lut(gfx::surface_texture(Surface::Half1), 0, lvl, 1, 1, (float)(1 + nx), (float)(1 + ny), (float)dx0, (float)dy0, (float)dx1,
                                (float)dy1, Blend::Copy);
                gfx::set_color_mask(0);
                core::mask_end_write();
                core::mask_begin_test();
                gfx::set_wrap(true);
                core::blit(glyphs_[(size_t)l], core::logic_x(tile.x0) / sz * G, core::logic_y(tile.y0) / sz * G, core::logic_x(tile.x1) / sz * G,
                           core::logic_y(tile.y1) / sz * G, tile.x0, tile.y0, tile.x1, tile.y1, WHITE, Blend::MulSrc, false, 32.0);
                gfx::set_wrap(false);
                core::mask_end_test();
            }
        }
    }
    core::restore_scissor(rs);
}

OldFilmFx::OldFilmFx() {}

OldFilmFx::~OldFilmFx() {
    dyn_destroy(column_, column_px_);
    destroy_texture(vignette_);
}

void OldFilmFx::passes(const Frame& f) {
    const Box& b = f.box;
    uint32_t lut[256];
    if (sepia > 0) {
        gfx::Texture* g = gfx::grab_screen();
        fill_lut_third(lut, 3.0);
        core::fill(b, BLACK, Blend::Copy);
        for (int c = 0; c < 3; c++) core::lut_box(g, c, lut, b, b, Blend::Add);
        gfx::Texture* g2 = gfx::grab_screen();
        const real SEP[3] = {112.0 / 255.0, 66.0 / 255.0, 20.0 / 255.0};
        for (int v = 0; v < 256; v++) {
            real gr = v / 255.0, o[3];
            for (int c = 0; c < 3; c++) {
                real ov = gr <= 0.5 ? 2.0 * SEP[c] * gr : 1.0 - 2.0 * (1.0 - gr) * (1.0 - SEP[c]);
                o[c] = clamp01(gr + sepia * (ov - gr));
            }
            lut[v] = core::rgb_level(o[0], o[1], o[2]);
        }
        core::lut_box(g2, 0, lut, b, b, Blend::Copy);
    }
    if (vignetting > 0) {
        const int N = 64;
        real aspect = f.h / f.w;
        if (!vignette_ || built_vig_[0] != vignetting || built_vig_[1] != vignetting_alpha || built_vig_[2] != vignetting_blur || built_vig_[3] != aspect) {
            destroy_texture(vignette_);
            std::vector<uint32_t> px((size_t)N * N);
            const real SQ2 = 1.414213;
            real outer = SQ2 - vignetting * SQ2;
            for (int j = 0; j < N; j++) {
                for (int i = 0; i < N; i++) {
                    real dx = 0.5 - (i + 0.5) / N, dy = (0.5 - (j + 0.5) / N) * aspect;
                    real darker = std::clamp((outer - std::sqrt(dx * dx + dy * dy) * SQ2) / (0.00001 + vignetting_blur * SQ2), 0.0, 1.0);
                    px[(size_t)j * N + i] = core::gray(darker + (1.0 - darker) * (1.0 - vignetting_alpha));
                }
            }
            vignette_ = make_texture(N, N, px);
            built_vig_[0] = vignetting;
            built_vig_[1] = vignetting_alpha;
            built_vig_[2] = vignetting_blur;
            built_vig_[3] = aspect;
        }
        core::blit(vignette_, f.fx(b.x0) / f.w * N, f.fy(b.y0) / f.h * N, f.fx(b.x1) / f.w * N, f.fy(b.y1) / f.h * N, b.x0, b.y0, b.x1, b.y1, WHITE,
                   Blend::MulSrc, true);
    }
    if (scratch_density > seed && scratch != 0) {
        const int N = 512;
        if (!column_) column_ = dyn_texture(N, 2, column_px_);
        real phase = seed * 256.0;
        real s = gmod(std::floor(phase), 2.0);
        real dist = 1.0 / scratch_density;
        real ccx = seed * dist, ccy = std::fabs(s - seed * dist);
        real rad = seed * 0.6 + 0.4;
        real period = scratch_density * 10.0;
        real kk = 2.0 * period;
        real dw = scratch_width / f.w * (0.75 + seed);
        real dh = dw * kk;
        real sgn = scratch > 0 ? 1.0 : -1.0;
        bool brighten = scratch > 0;
        for (int i = 0; i < N; i++) {
            real acc = 0;
            for (int ss = 0; ss < 4; ss++) {
                real px = b.x0 + i + (ss + 0.5) / 4.0;
                real cx = f.fx(px) / f.w;
                real xx = cx * period + phase;
                real aa = std::fabs(gmod(xx, 0.5) * 4.0);
                real bb = gmod(std::floor(xx / 0.5), 2.0);
                real yy = (1.0 - bb) * aa + bb * (2.0 - aa);
                real tine = yy - (2.0 - dh);
                real mult = 1.0;
                if (tine > 0.0) {
                    tine = s * tine / period + scratch + 0.1;
                    mult = std::clamp(tine + 1.0, 0.5 + sgn * 0.5, 1.5 + sgn * 0.5);
                }
                acc += mult;
            }
            real m = acc / 4.0;
            uint32_t g = core::gray(brighten ? clamp01(m - 1.0) : clamp01(m));
            column_px_[i] = g;
            column_px_[N + i] = g;
        }
        gfx::texture_touch(column_);
        const int SEG = 48;
        std::vector<gfx::Vertex> v;
        real ox = f.px(ccx * f.w), oy = f.py(ccy * f.h);
        auto vert = [&](real nx, real ny) {
            real x = f.px(nx * f.w), y = f.py(ny * f.h);
            return gfx::Vertex{(float)(x - b.x0), 1.0f, WHITE, (float)x, (float)y};
        };
        for (int k = 0; k < SEG; k++) {
            real a0 = 2.0 * core::PI * k / SEG, a1 = 2.0 * core::PI * (k + 1) / SEG;
            v.push_back(gfx::Vertex{(float)(ox - b.x0), 1.0f, WHITE, (float)ox, (float)oy});
            v.push_back(vert(ccx + std::cos(a0) * rad, ccy + std::sin(a0) * rad));
            v.push_back(vert(ccx + std::cos(a1) * rad, ccy + std::sin(a1) * rad));
        }
        gfx::draw_triangles(column_, v.data(), (int)v.size(), brighten ? Blend::MulAdd : Blend::MulSrc, false);
    }
    if (noise > 0 && noise_size > 0) core::draw_noise(b, noise, seed, noise_size * S, f.px(0), f.py(0));
}

void OldFilmFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    color_pass(f, rs, draw, [&]() { passes(f); });
}

}
}
