#include "fx_core.h"

#include <algorithm>
#include <cmath>

namespace ui {
namespace fx {
namespace core {

using gfx::Blend;
using gfx::Surface;

static const uint32_t BLACK = 0xFF000000u;
static const uint32_t WHITE = 0xFFFFFFFFu;

Box intersect(const Box& a, const Box& b) {
    Box r;
    r.x0 = std::max(a.x0, b.x0);
    r.y0 = std::max(a.y0, b.y0);
    r.x1 = std::min(a.x1, b.x1);
    r.y1 = std::min(a.y1, b.y1);
    if (r.x1 < r.x0) r.x1 = r.x0;
    if (r.y1 < r.y0) r.y1 = r.y0;
    return r;
}

Box expand(const Box& b, int n) {
    Box r = b;
    r.x0 -= n;
    r.y0 -= n;
    r.x1 += n;
    r.y1 += n;
    return r;
}

Box game_box() {
    Box b;
    b.x0 = (int)std::lround(phys_x(0));
    b.y0 = (int)std::lround(phys_y(0));
    b.x1 = (int)std::lround(phys_x(SCREEN_LW));
    b.y1 = (int)std::lround(phys_y(SCREEN_LH));
    return b;
}

static int to8(real v) { return std::clamp((int)std::lround(v * 255.0), 0, 255); }

uint32_t gray(real v) {
    int c = to8(v);
    return gfx::rgba(c, c, c, 255);
}

uint32_t level(real v) {
    int c = to8(v);
    return gfx::rgba(c, c, c, c);
}

uint32_t rgb_level(real r, real g, real b) { return gfx::rgba(to8(r), to8(g), to8(b), 255); }

static void add_point(const g2d::Mat& m, real x, real y, real& x0, real& y0, real& x1, real& y1) {
    real wx, wy;
    m.apply(x, y, wx, wy);
    x0 = std::min(x0, wx);
    y0 = std::min(y0, wy);
    x1 = std::max(x1, wx);
    y1 = std::max(y1, wy);
}

static void add_rect(const g2d::Mat& m, real lx, real ly, real w, real h, real& x0, real& y0, real& x1, real& y1, bool& any) {
    add_point(m, lx, ly, x0, y0, x1, y1);
    add_point(m, lx + w, ly, x0, y0, x1, y1);
    add_point(m, lx, ly + h, x0, y0, x1, y1);
    add_point(m, lx + w, ly + h, x0, y0, x1, y1);
    any = true;
}

static void bounds_rec(g2d::DisplayObject& o, const g2d::Mat& m, bool self_renderable, real& x0, real& y0, real& x1, real& y1, bool& any,
                       int depth) {
    if (depth > 64) return;
    if (auto* sp = dynamic_cast<g2d::Sprite*>(&o)) {
        const g2d::Rect& rf = sp->real_frame();
        if (self_renderable && sp->bitmap() && rf.width > 0 && rf.height > 0)
            add_rect(m, -sp->anchor_x * rf.width, -sp->anchor_y * rf.height, rf.width, rf.height, x0, y0, x1, y1, any);
    } else if (auto* ts = dynamic_cast<g2d::TilingSprite*>(&o)) {
        if (self_renderable && ts->width > 0 && ts->height > 0) add_rect(m, 0, 0, ts->width, ts->height, x0, y0, x1, y1, any);
    } else if (dynamic_cast<g2d::ScreenSprite*>(&o)) {
        if (self_renderable) {
            x0 = std::min(x0, 0.0);
            y0 = std::min(y0, 0.0);
            x1 = std::max(x1, SCREEN_LW);
            y1 = std::max(y1, SCREEN_LH);
            any = true;
        }
    } else if (!o.is_container()) {
        if (self_renderable) {
            x0 = std::min(x0, 0.0);
            y0 = std::min(y0, 0.0);
            x1 = std::max(x1, SCREEN_LW);
            y1 = std::max(y1, SCREEN_LH);
            any = true;
        }
        return;
    }
    auto* c = dynamic_cast<g2d::Container*>(&o);
    if (!c) return;
    for (auto& ch : c->children) {
        if (!ch || !ch->visible || ch->alpha <= 0) continue;
        if (!ch->renderable && !ch->is_container()) continue;
        g2d::Mat cm = m.mul(ch->local_matrix());
        real cx0 = 1e30, cy0 = 1e30, cx1 = -1e30, cy1 = -1e30;
        bool cany = false;
        bounds_rec(*ch, cm, ch->renderable, cx0, cy0, cx1, cy1, cany, depth + 1);
        if (!cany) continue;
        if (ch->mask_rect) {
            real mx0 = 1e30, my0 = 1e30, mx1 = -1e30, my1 = -1e30;
            add_point(cm, ch->mask_rect->x, ch->mask_rect->y, mx0, my0, mx1, my1);
            add_point(cm, ch->mask_rect->x + ch->mask_rect->width, ch->mask_rect->y + ch->mask_rect->height, mx0, my0, mx1, my1);
            cx0 = std::max(cx0, mx0);
            cy0 = std::max(cy0, my0);
            cx1 = std::min(cx1, mx1);
            cy1 = std::min(cy1, my1);
            if (cx1 <= cx0 || cy1 <= cy0) continue;
        }
        x0 = std::min(x0, cx0);
        y0 = std::min(y0, cy0);
        x1 = std::max(x1, cx1);
        y1 = std::max(y1, cy1);
        any = true;
    }
}

bool logical_bounds(g2d::DisplayObject& obj, const g2d::Mat& world, g2d::Rect& out) {
    real x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
    bool any = false;
    bounds_rec(obj, world, true, x0, y0, x1, y1, any, 0);
    if (!any) return false;
    out = g2d::Rect(x0, y0, x1 - x0, y1 - y0);
    return true;
}

Box clip_box(const g2d::RenderState& rs) {
    Box b;
    if (rs.clip) {
        b.x0 = rs.cx0;
        b.y0 = rs.cy0;
        b.x1 = rs.cx1;
        b.y1 = rs.cy1;
    } else {
        b.x0 = 0;
        b.y0 = 0;
        b.x1 = gfx::surface_width(gfx::current_target());
        b.y1 = gfx::surface_height(gfx::current_target());
    }
    return b;
}

bool make_frame(g2d::DisplayObject& obj, const g2d::RenderState& rs, bool full_area, real padding, Frame& out) {
    real fx, fy, fw, fh;
    if (full_area) {
        fx = 0;
        fy = 0;
        fw = SCREEN_LW;
        fh = SCREEN_LH;
    } else {
        g2d::Rect r;
        if (!logical_bounds(obj, rs.world, r)) return false;
        fx = (real)(int64_t)r.x;
        fy = (real)(int64_t)r.y;
        fw = std::ceil(r.width);
        fh = std::ceil(r.height);
    }
    fx -= padding;
    fy -= padding;
    fw += padding * 2;
    fh += padding * 2;
    real ex = std::max(fx, 0.0), ey = std::max(fy, 0.0);
    real ex1 = std::min(fx + fw, SCREEN_LW), ey1 = std::min(fy + fh, SCREEN_LH);
    if (ex1 <= ex || ey1 <= ey) return false;
    out.x = ex;
    out.y = ey;
    out.w = ex1 - ex;
    out.h = ey1 - ey;
    Box b;
    b.x0 = (int)std::lround(phys_x(out.x));
    b.y0 = (int)std::lround(phys_y(out.y));
    b.x1 = (int)std::lround(phys_x(out.x + out.w));
    b.y1 = (int)std::lround(phys_y(out.y + out.h));
    out.box = intersect(intersect(b, clip_box(rs)), game_box());
    return !out.box.empty();
}

void scissor(const Box& b) { gfx::set_scissor(b.x0, b.y0, b.w(), b.h()); }

void restore_scissor(const g2d::RenderState& rs) {
    if (rs.clip) gfx::set_scissor(rs.cx0, rs.cy0, rs.cx1 - rs.cx0, rs.cy1 - rs.cy0);
    else gfx::reset_scissor();
}

g2d::RenderState with_clip(const g2d::RenderState& rs, const Box& b) {
    g2d::RenderState r = rs;
    r.clip = true;
    r.cx0 = b.x0;
    r.cy0 = b.y0;
    r.cx1 = b.x1;
    r.cy1 = b.y1;
    return r;
}

void blit(gfx::Texture* t, real u0, real v0, real u1, real v1, real x0, real y0, real x1, real y1, uint32_t color, Blend blend, bool smooth,
          real strip) {
    if (!t || x1 <= x0 || y1 <= y0) return;
    real du = (u1 - u0) / (x1 - x0);
    for (real sx = x0; sx < x1; sx += strip) {
        real ex = std::min(x1, sx + strip);
        gfx::sprite(t, (float)(u0 + (sx - x0) * du), (float)v0, (float)(u0 + (ex - x0) * du), (float)v1, (float)sx, (float)y0, (float)ex, (float)y1, color,
                    blend, smooth);
    }
}

void blit_box(gfx::Texture* t, const Box& b, uint32_t color, Blend blend) {
    blit(t, b.x0, b.y0, b.x1, b.y1, b.x0, b.y0, b.x1, b.y1, color, blend, false);
}

void fill(const Box& b, uint32_t color, Blend blend) {
    if (b.empty()) return;
    gfx::fill((float)b.x0, (float)b.y0, (float)b.x1, (float)b.y1, color, blend);
}

void scale_box(const Box& b, real factor) {
    if (factor < 0) factor = 0;
    if (std::fabs(factor - 1.0) < 1.0 / 512.0) return;
    if (factor < 1.0) {
        fill(b, gray(factor), Blend::MulSrc);
        return;
    }
    while (factor > 2.0) {
        fill(b, WHITE, Blend::MulAdd);
        factor *= 0.5;
    }
    fill(b, gray(factor - 1.0), Blend::MulAdd);
}

void lut_box(gfx::Texture* src, int channel, const uint32_t* lut, const Box& sb, const Box& db, Blend blend) {
    if (db.empty()) return;
    gfx::sprite_lut(src, channel, lut, (float)sb.x0, (float)sb.y0, (float)sb.x1, (float)sb.y1, (float)db.x0, (float)db.y0, (float)db.x1, (float)db.y1, blend);
}

void Mesh::build(const Box& b, real cell) {
    cols = std::max(1, (int)std::ceil(b.w() / cell));
    rows = std::max(1, (int)std::ceil(b.h() / cell));
    v.assign((size_t)(cols + 1) * (rows + 1), gfx::Vertex{0, 0, WHITE, 0, 0});
    const float sx = (float)b.w() / (float)cols, sy = (float)b.h() / (float)rows;
    for (int r = 0; r <= rows; r++) {
        float y = r == rows ? (float)b.y1 : (float)b.y0 + sy * (float)r;
        for (int c = 0; c <= cols; c++) {
            gfx::Vertex& p = at(c, r);
            p.x = c == cols ? (float)b.x1 : (float)b.x0 + sx * (float)c;
            p.y = y;
            p.u = p.x;
            p.v = p.y;
        }
    }
}

void draw_mesh(gfx::Texture* tex, const Mesh& m, Blend blend, bool smooth) {
    if (m.v.empty()) return;
    gfx::draw_grid(tex, m.v.data(), m.cols, m.rows, blend, smooth);
}

static std::vector<gfx::Stencil> g_mask_stack;
static gfx::Stencil g_mask_mode = gfx::Stencil::Off;

void mask_begin_write(const Box& b) {
    g_mask_stack.push_back(g_mask_mode);
    gfx::stencil_clear(b.x0, b.y0, b.x1, b.y1);
    g_mask_mode = gfx::Stencil::Write;
    gfx::set_stencil(g_mask_mode);
}

void mask_end_write() {
    g_mask_mode = g_mask_stack.empty() ? gfx::Stencil::Off : g_mask_stack.back();
    if (!g_mask_stack.empty()) g_mask_stack.pop_back();
    gfx::set_stencil(g_mask_mode);
}

void mask_begin_test() {
    g_mask_stack.push_back(g_mask_mode);
    g_mask_mode = gfx::Stencil::Test;
    gfx::set_stencil(g_mask_mode);
}

void mask_end_test() { mask_end_write(); }

struct NoiseTextures {
    gfx::Texture* pos = nullptr;
    gfx::Texture* neg = nullptr;
};

static NoiseTextures& noise_textures() {
    static NoiseTextures t;
    if (t.pos) return t;
    const int N = 256;
    uint8_t* pp = (uint8_t*)gfx::alloc(N * N);
    uint8_t* np = (uint8_t*)gfx::alloc(N * N);
    uint32_t* pal_p = (uint32_t*)gfx::alloc(1024);
    uint32_t* pal_n = (uint32_t*)gfx::alloc(1024);
    for (int i = 0; i < 256; i++) {
        pal_p[i] = gfx::rgba(i, i, i, 255);
        pal_n[i] = gfx::rgba(i, i, i, 255);
    }
    uint32_t s = 0x9E3779B9u;
    for (int i = 0; i < N * N; i++) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        real r = (s >> 8) / 16777216.0;
        real d = r - 0.5;
        pp[i] = (uint8_t)std::clamp((int)std::lround(std::max(d, 0.0) * 2.0 * 255.0), 0, 255);
        np[i] = (uint8_t)std::clamp((int)std::lround(std::max(-d, 0.0) * 2.0 * 255.0), 0, 255);
    }
    t.pos = gfx::texture_create(N, N, N, N, gfx::Format::T8, false, pp, pal_p);
    t.neg = gfx::texture_create(N, N, N, N, gfx::Format::T8, false, np, pal_n);
    return t;
}

void draw_noise(const Box& b, real amount, real seed, real cell, real anchor_x, real anchor_y) {
    if (b.empty() || !(amount > 0) || !(cell > 0)) return;
    NoiseTextures& nt = noise_textures();
    real sd = std::isfinite(seed) ? seed : 0.0;
    real ox = std::floor((sd * 4093.0 - std::floor(sd * 4093.0)) * 256.0);
    real oy = std::floor((sd * 7919.0 - std::floor(sd * 7919.0)) * 256.0);
    real u0 = (b.x0 - anchor_x) / cell + ox, u1 = (b.x1 - anchor_x) / cell + ox;
    real v0 = (b.y0 - anchor_y) / cell + oy, v1 = (b.y1 - anchor_y) / cell + oy;
    gfx::set_wrap(true);
    real rem = amount * 0.5;
    while (rem > 1e-4) {
        real k = std::min(1.0, rem);
        blit(nt.pos, u0, v0, u1, v1, b.x0, b.y0, b.x1, b.y1, gray(k), Blend::Add, false, 32.0);
        blit(nt.neg, u0, v0, u1, v1, b.x0, b.y0, b.x1, b.y1, gray(k), Blend::Subtract, false, 32.0);
        rem -= k;
    }
    gfx::set_wrap(false);
}

static int g_isolation = 0;

IsolationLock::IsolationLock() {
    if (g_isolation == 0 && gfx::current_target() == Surface::Screen) {
        ok_ = true;
        g_isolation++;
    }
}

IsolationLock::~IsolationLock() {
    if (ok_) g_isolation--;
}

void layers_full(const Frame& f, const g2d::RenderState& rs, const DrawFn& draw) {
    const Box& b = f.box;
    g2d::RenderState r2 = with_clip(rs, b);
    gfx::set_target(Surface::Grab);
    scissor(b);
    fill(b, WHITE, Blend::Copy);
    draw(r2);
    gfx::set_target(Surface::Wide);
    scissor(b);
    fill(b, WHITE, Blend::Copy);
    blit_box(gfx::surface_texture(Surface::Grab), b, WHITE, Blend::Subtract);
    gfx::set_target(Surface::Grab);
    scissor(b);
    fill(b, BLACK, Blend::Copy);
    draw(r2);
    gfx::set_target(Surface::Wide);
    scissor(b);
    blit_box(gfx::surface_texture(Surface::Grab), b, WHITE, Blend::Add);
    gfx::set_target(Surface::Screen);
    restore_scissor(rs);
}

Box half_box(const Box& b) {
    Box h;
    h.x0 = std::max(1, (int)std::floor(half_x(b.x0)));
    h.y0 = std::max(1, (int)std::floor(half_y(b.y0)));
    h.x1 = std::min(gfx::HALF_W - 1, (int)std::ceil(half_x(b.x1)));
    h.y1 = std::min(gfx::HALF_H - 1, (int)std::ceil(half_y(b.y1)));
    if (h.x1 < h.x0) h.x1 = h.x0;
    if (h.y1 < h.y0) h.y1 = h.y0;
    return h;
}

Box phys_box_of_half(const Box& hb) {
    Box b;
    b.x0 = (int)std::lround(phys_of_half_x(hb.x0));
    b.y0 = (int)std::lround(phys_of_half_y(hb.y0));
    b.x1 = (int)std::lround(phys_of_half_x(hb.x1));
    b.y1 = (int)std::lround(phys_of_half_y(hb.y1));
    return b;
}

void half_ring(Surface s, const Box& hb, uint32_t color) {
    if (gfx::current_target() != s) gfx::set_target(s);
    Box e = expand(hb, 1);
    gfx::reset_scissor();
    fill(Box{e.x0, e.y0, e.x1, hb.y0}, color, Blend::Copy);
    fill(Box{e.x0, hb.y1, e.x1, e.y1}, color, Blend::Copy);
    fill(Box{e.x0, hb.y0, hb.x0, hb.y1}, color, Blend::Copy);
    fill(Box{hb.x1, hb.y0, e.x1, hb.y1}, color, Blend::Copy);
}

Box downsample(gfx::Texture* src, const Box& b, Surface dst, uint32_t ring) {
    Box hb = half_box(b);
    Box pb = phys_box_of_half(hb);
    gfx::set_target(dst);
    gfx::reset_scissor();
    blit(src, pb.x0, pb.y0, pb.x1, pb.y1, hb.x0, hb.y0, hb.x1, hb.y1, WHITE, Blend::Copy, true, 32.0);
    half_ring(dst, hb, ring);
    return hb;
}

void upsample(Surface src, const Box& hb, const Box& b, Blend blend, uint32_t color) {
    (void)hb;
    blit(gfx::surface_texture(src), half_x(b.x0), half_y(b.y0), half_x(b.x1), half_y(b.y1), b.x0, b.y0, b.x1, b.y1, color, blend, true);
}

Box layers_half(const Frame& f, const g2d::RenderState& rs, const DrawFn& draw) {
    const Box& b = f.box;
    Box hb = half_box(b);
    Box eb = intersect(phys_box_of_half(expand(hb, 1)), Box{0, 0, gfx::SCREEN_W, gfx::SCREEN_H});
    Box pb = phys_box_of_half(hb);
    g2d::RenderState r2 = with_clip(rs, b);
    gfx::Texture* grab = gfx::surface_texture(Surface::Grab);
    gfx::set_target(Surface::Grab);
    scissor(eb);
    fill(eb, WHITE, Blend::Copy);
    scissor(b);
    draw(r2);
    gfx::set_target(Surface::Half1);
    gfx::reset_scissor();
    fill(hb, WHITE, Blend::Copy);
    blit(grab, pb.x0, pb.y0, pb.x1, pb.y1, hb.x0, hb.y0, hb.x1, hb.y1, WHITE, Blend::Subtract, true, 32.0);
    gfx::set_target(Surface::Grab);
    scissor(eb);
    fill(eb, BLACK, Blend::Copy);
    scissor(b);
    draw(r2);
    gfx::set_target(Surface::Half0);
    gfx::reset_scissor();
    blit(grab, pb.x0, pb.y0, pb.x1, pb.y1, hb.x0, hb.y0, hb.x1, hb.y1, WHITE, Blend::Copy, true, 32.0);
    half_ring(Surface::Half0, hb, BLACK);
    gfx::set_target(Surface::Half1);
    gfx::reset_scissor();
    blit(grab, pb.x0, pb.y0, pb.x1, pb.y1, hb.x0, hb.y0, hb.x1, hb.y1, WHITE, Blend::Add, true, 32.0);
    half_ring(Surface::Half1, hb, BLACK);
    gfx::set_target(Surface::Screen);
    restore_scissor(rs);
    return hb;
}

void composite_full_mesh(const Mesh& m) {
    draw_mesh(gfx::surface_texture(Surface::Wide), m, Blend::MulSub, true);
    draw_mesh(gfx::surface_texture(Surface::Grab), m, Blend::Add, true);
}

void composite_half(const Box& hb, const Box& b) {
    upsample(Surface::Half1, hb, b, Blend::MulSub, WHITE);
    upsample(Surface::Half0, hb, b, Blend::Add, WHITE);
}

std::vector<Tap> gaussian_taps(real sigma, bool horizontal) {
    std::vector<Tap> taps;
    if (!(sigma > 0.05)) {
        taps.push_back(Tap{0, 0, 1});
        return taps;
    }
    int K = std::max(1, (int)std::ceil(sigma * 3.0));
    std::vector<real> w((size_t)K + 2, 0.0);
    real total = 0;
    for (int k = 0; k <= K; k++) {
        w[(size_t)k] = std::exp(-(real)k * k / (2.0 * sigma * sigma));
        total += k == 0 ? w[0] : 2.0 * w[(size_t)k];
    }
    for (auto& x : w) x /= total;
    taps.push_back(Tap{0, 0, w[0]});
    for (int k = 1; k <= K; k += 2) {
        real a = w[(size_t)k], c = k + 1 <= K ? w[(size_t)k + 1] : 0.0;
        real s = a + c;
        if (s <= 0) continue;
        real off = (k * a + (k + 1) * c) / s;
        taps.push_back(horizontal ? Tap{off, 0, s} : Tap{0, off, s});
        taps.push_back(horizontal ? Tap{-off, 0, s} : Tap{0, -off, s});
    }
    return taps;
}

void draw_taps(gfx::Texture* src, const Box& dst, const Box& valid, const std::vector<Tap>& taps, real tex_scale) {
    fill(dst, BLACK, Blend::Copy);
    std::vector<int> c(taps.size());
    int sum = 0;
    size_t best = 0;
    for (size_t i = 0; i < taps.size(); i++) {
        c[i] = std::clamp((int)std::lround(taps[i].w * 255.0), 0, 255);
        sum += c[i];
        if (taps[i].w > taps[best].w) best = i;
    }
    if (!taps.empty()) c[best] = std::clamp(c[best] + 255 - sum, 0, 255);
    for (size_t i = 0; i < taps.size(); i++) {
        if (c[i] <= 0) continue;
        real dx = taps[i].dx, dy = taps[i].dy;
        real x0 = std::max<real>(dst.x0, valid.x0 - dx), x1 = std::min<real>(dst.x1, valid.x1 - dx);
        real y0 = std::max<real>(dst.y0, valid.y0 - dy), y1 = std::min<real>(dst.y1, valid.y1 - dy);
        if (x1 <= x0 || y1 <= y0) continue;
        bool frac = std::fabs(dx - std::round(dx)) > 1e-3 || std::fabs(dy - std::round(dy)) > 1e-3;
        blit(src, (x0 + dx) * tex_scale, (y0 + dy) * tex_scale, (x1 + dx) * tex_scale, (y1 + dy) * tex_scale, x0, y0, x1, y1,
             gfx::rgba(c[i], c[i], c[i], 255), Blend::Add, frac);
    }
}

void gaussian_half(Surface buf, Surface tmp, const Box& hb, real sigma_x, real sigma_y) {
    std::vector<Tap> tx = gaussian_taps(sigma_x, true), ty = gaussian_taps(sigma_y, false);
    gfx::set_target(tmp);
    gfx::reset_scissor();
    draw_taps(gfx::surface_texture(buf), hb, hb, tx, 1.0);
    half_ring(tmp, hb, BLACK);
    gfx::set_target(buf);
    gfx::reset_scissor();
    draw_taps(gfx::surface_texture(tmp), hb, hb, ty, 1.0);
    half_ring(buf, hb, BLACK);
}

void gaussian_screen(const Box& b, real sigma_x, real sigma_y) {
    std::vector<Tap> tx = gaussian_taps(sigma_x, true), ty = gaussian_taps(sigma_y, false);
    if (tx.size() > 1) draw_taps(gfx::grab_screen(), b, b, tx, 1.0);
    if (ty.size() > 1) draw_taps(gfx::grab_screen(), b, b, ty, 1.0);
}

Surface kawase_half(Surface a, Surface b, const Box& hb, const std::vector<real>& offsets) {
    Surface cur = a, other = b;
    for (real o : offsets) {
        std::vector<Tap> taps = {Tap{-o, o, 0.25}, Tap{o, o, 0.25}, Tap{o, -o, 0.25}, Tap{-o, -o, 0.25}};
        gfx::set_target(other);
        gfx::reset_scissor();
        draw_taps(gfx::surface_texture(cur), hb, hb, taps, 1.0);
        half_ring(other, hb, BLACK);
        std::swap(cur, other);
    }
    return cur;
}

real pixi_blur_sigma(real blur, real quality) {
    static const real K15[8] = {0.197448, 0.174697, 0.120999, 0.065602, 0.02784, 0.009246, 0.002403, 0.000489};
    real var = 0;
    for (int i = 1; i < 8; i++) var += 2.0 * K15[i] * i * i;
    real q = std::max(1.0, std::round(quality));
    real step = blur / q;
    return std::sqrt(q) * std::fabs(step) * std::sqrt(var);
}

}
}
}
