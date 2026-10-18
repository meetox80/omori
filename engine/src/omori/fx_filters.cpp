#include "fx_filters.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <typeinfo>

#include "../js/random.h"
#include "../platform/platform.h"
#include "../rpg/perf.h"

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

gfx::Texture* make_texture(int w, int h, const std::vector<uint32_t>& px) {
    uint32_t* mem = (uint32_t*)gfx::alloc((size_t)w * h * 4);
    std::memcpy(mem, px.data(), (size_t)w * h * 4);
    return gfx::texture_create(w, h, w, h, gfx::Format::RGBA8888, false, mem, nullptr);
}

static void destroy_texture(gfx::Texture*& t) {
    if (t) gfx::texture_destroy(t);
    t = nullptr;
}

static int next_pow2(real v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

void FxFilter::apply(g2d::DisplayObject& obj, const g2d::RenderState& rs, const std::function<void(const g2d::RenderState&)>& draw) {
    if (identity()) {
        draw(rs);
        return;
    }
    Frame f;
    if (!core::make_frame(obj, rs, full_area || scope == Scope::Screen, padding(), f)) {
        draw(rs);
        return;
    }
    if (!rpg::perf::enabled()) {
        render(f, rs, draw);
        return;
    }
    struct Stat {
        uint64_t us = 0;
        int calls = 0;
    };
    static std::map<const char*, Stat> stats;
    uint64_t inner = 0;
    uint64_t t0 = plat::time_us();
    render(f, rs, [&](const g2d::RenderState& r) {
        uint64_t d0 = plat::time_us();
        draw(r);
        inner += plat::time_us() - d0;
    });
    Stat& st = stats[typeid(*this).name()];
    st.us += plat::time_us() - t0 - inner;
    if (++st.calls == 300) {
        plat::log("perf fx %s: %.2f ms", typeid(*this).name(), st.us / 300000.0);
        st = Stat();
    }
}

void FxFilter::color_pass(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw, const std::function<void()>& passes) {
    if (scope == Scope::Screen) {
        draw(rs);
        core::scissor(f.box);
        passes();
        core::restore_scissor(rs);
        return;
    }
    core::scissor(f.box);
    core::mask_begin_write(f.box);
    core::restore_scissor(rs);
    draw(rs);
    core::mask_end_write();
    core::mask_begin_test();
    core::scissor(f.box);
    passes();
    core::mask_end_test();
    core::restore_scissor(rs);
}

void FxFilter::warp_pass(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw, Mesh& mesh) {
    if (scope == Scope::Screen) {
        draw(rs);
        gfx::Texture* g = gfx::grab_screen();
        core::scissor(f.box);
        core::draw_mesh(g, mesh, Blend::Copy, true);
        core::restore_scissor(rs);
        return;
    }
    core::IsolationLock lock;
    if (!lock.ok()) {
        draw(rs);
        return;
    }
    core::layers_full(f, rs, draw);
    core::scissor(f.box);
    core::composite_full_mesh(mesh);
    core::restore_scissor(rs);
}

GlitchFx::GlitchFx() { set_slices(5); }

void GlitchFx::set_slices(real v) {
    if (slices_value_ == v) return;
    slices_value_ = v;
    count_ = (std::isfinite(v) && v > 0) ? (int)std::floor(v) : 0;
    sizes_.assign((size_t)count_, 0.0f);
    offsets_.assign((size_t)count_, 0.0f);
    refresh();
}

void GlitchFx::refresh() {
    randomize_sizes();
    randomize_offsets();
    redraw();
}

void GlitchFx::randomize_sizes() {
    int t = count_ - 1;
    real n = sample_size;
    real r = std::min(min_size / n, 0.9 / count_);
    if (average) {
        real i = 1;
        for (int l = 0; l < t; l++) {
            real s = i / (count_ - l);
            real a = std::max<real>(s * (1 - 0.6 * rng::next()), r);
            sizes_[(size_t)l] = (float)a;
            i -= a;
        }
        if (t >= 0) sizes_[(size_t)t] = (float)i;
    } else {
        real u = 1, c = std::sqrt(1.0 / count_);
        for (int k = 0; k < t; k++) {
            real h = std::max<real>(c * u * rng::next(), r);
            sizes_[(size_t)k] = (float)h;
            u -= h;
        }
        if (t >= 0) sizes_[(size_t)t] = (float)u;
    }
    shuffle();
}

void GlitchFx::shuffle() {
    for (int t = count_ - 1; t > 0; t--) {
        int n = (int)(rng::next() * t);
        std::swap(sizes_[(size_t)t], sizes_[(size_t)n]);
    }
}

void GlitchFx::randomize_offsets() {
    for (int e = 0; e < count_; e++) {
        real a = rng::next();
        real b = rng::next();
        offsets_[(size_t)e] = (float)(a * (b < 0.5 ? -1 : 1));
    }
}

static int32_t to_int32(double v) {
    if (!std::isfinite(v)) return 0;
    double t = std::trunc(v);
    double m = std::fmod(t, 4294967296.0);
    if (m < 0) m += 4294967296.0;
    if (m >= 2147483648.0) m -= 4294967296.0;
    return (int32_t)m;
}

void GlitchFx::redraw() {
    table_.fill(0);
    real o = 0;
    real t = sample_size;
    for (int i = 0; i < count_; i++) {
        int e = (int)std::floor(256.0 * (real)offsets_[(size_t)i]);
        real l = (real)sizes_[(size_t)i] * t;
        int s = std::min(255, std::max(e, 0)), a = std::min(255, std::max(-e, 0));
        int y = to_int32(o), h = to_int32(l + 1);
        int y0 = h >= 0 ? y : y + h, y1 = h >= 0 ? y + h : y;
        y0 = std::max(y0, 0);
        y1 = std::min(y1, 512);
        for (int r = y0; r < y1; r++) table_[(size_t)r] = (int16_t)(s - a);
        o += l;
    }
    any_ = false;
    for (int16_t v : table_) any_ = any_ || v != 0;
}

bool GlitchFx::identity() const {
    bool rgb = red[0] || red[1] || green[0] || green[1] || blue[0] || blue[1];
    return !rgb && (!any_ || offset == 0 || !std::isfinite(offset));
}

void GlitchFx::bands(const Frame& f, std::vector<Band>& out) const {
    out.clear();
    const Box& b = f.box;
    for (int py = b.y0; py < b.y1; py++) {
        real ny = f.fy(py + 0.5) / f.h;
        if (ny > 1.0) ny = 2.0 - ny;
        if (ny < 0.0) ny = -ny;
        int row = std::clamp((int)std::floor(ny * 512.0), 0, 511);
        real d = table_[(size_t)row] / 255.0 * offset;
        if (!std::isfinite(d)) d = 0;
        if (!out.empty() && out.back().d == d && out.back().y1 == py) out.back().y1 = py + 1;
        else out.push_back(Band{py, py + 1, d});
    }
}

void GlitchFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    std::vector<Band> bs;
    bands(f, bs);
    real ch[3][2] = {{red[0] * (1 - seed * 0.4), red[1] * (1 - seed * 0.4)},
                       {green[0] * (1 - seed * 0.3), green[1] * (1 - seed * 0.3)},
                       {blue[0] * (1 - seed * 0.2), blue[1] * (1 - seed * 0.2)}};
    bool split = !(ch[0][0] == ch[1][0] && ch[0][0] == ch[2][0] && ch[0][1] == ch[1][1] && ch[0][1] == ch[2][1]);
    bool moved = split || ch[0][0] != 0 || ch[0][1] != 0;
    for (const Band& bd : bs) moved = moved || bd.d != 0;
    if (!moved) {
        draw(rs);
        return;
    }
    static const uint32_t PROTECT[3] = {0x00FFFF00u, 0x00FF00FFu, 0x0000FFFFu};
    const Box& b = f.box;
    int passes = split ? 3 : 1;
    gfx::Texture* grab = nullptr;
    if (scope == Scope::Screen) {
        draw(rs);
        grab = gfx::grab_screen();
        core::scissor(b);
        core::fill(b, BLACK, Blend::Copy);
    }
    for (const Band& bd : bs) {
        for (int c = 0; c < passes; c++) {
            real ox = ch[c][0], oy = ch[c][1];
            real lx0 = std::max(-bd.d, -bd.d - ox), lx1 = std::min(f.w - bd.d, f.w - bd.d - ox);
            real ly0 = -oy, ly1 = f.h - oy;
            real x0 = std::max<real>(b.x0, f.px(lx0)), x1 = std::min<real>(b.x1, f.px(lx1));
            real y0 = std::max<real>(bd.y0, f.py(ly0)), y1 = std::min<real>(bd.y1, f.py(ly1));
            if (x1 <= x0 || y1 <= y0) continue;
            if (split) gfx::set_color_mask(PROTECT[c]);
            real sx = (bd.d + ox) * S, sy = oy * S;
            if (scope == Scope::Screen) {
                bool frac = std::fabs(sx - std::round(sx)) > 1e-3 || std::fabs(sy - std::round(sy)) > 1e-3;
                core::blit(grab, x0 + sx, y0 + sy, x1 + sx, y1 + sy, x0, y0, x1, y1, WHITE, Blend::Copy, frac);
            } else {
                Box clip{(int)std::lround(x0), (int)std::lround(y0), (int)std::lround(x1), (int)std::lround(y1)};
                clip = core::intersect(clip, b);
                if (clip.empty()) continue;
                g2d::RenderState r2 = core::with_clip(rs, clip);
                r2.world.tx -= bd.d + ox;
                r2.world.ty -= oy;
                core::scissor(clip);
                draw(r2);
            }
        }
    }
    if (split) gfx::set_color_mask(0);
    core::restore_scissor(rs);
}

void NoiseFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    color_pass(f, rs, draw, [&]() { core::draw_noise(f.box, noise, seed, S, f.px(0), f.py(0)); });
}

CrtFx::CrtFx() {}

CrtFx::~CrtFx() {
    destroy_texture(line_pos_);
    destroy_texture(line_neg_);
    destroy_texture(segment_);
    destroy_texture(segment_v_);
    destroy_texture(vignette_);
}

void CrtFx::passes(const Frame& f) {
    const Box& b = f.box;
    real W = f.w, H = f.h;
    if (noise > 0 && noise_size > 0) core::draw_noise(b, noise, seed, noise_size * S, f.px(0), f.py(0));
    if (line_width > 0) {
        const int N = 64;
        if (!line_pos_ || built_contrast_ != line_contrast) {
            destroy_texture(line_pos_);
            destroy_texture(line_neg_);
            std::vector<uint32_t> p((size_t)N * 8), n((size_t)N * 8);
            for (int i = 0; i < N; i++) {
                real c = std::cos(2.0 * core::PI * (i + 0.5) / N) * 0.5 * line_contrast;
                for (int r = 0; r < 8; r++) {
                    p[(size_t)r * N + i] = core::gray(std::max(c, 0.0));
                    n[(size_t)r * N + i] = core::gray(1.0 + std::min(c, 0.0));
                }
            }
            line_pos_ = make_texture(N, 8, p);
            line_neg_ = make_texture(N, 8, n);
            built_contrast_ = line_contrast;
        }
        if (!segment_) {
            std::vector<uint32_t> s(8 * 8), t(8 * 8);
            for (int r = 0; r < 8; r++) {
                for (int i = 0; i < 8; i++) {
                    s[(size_t)r * 8 + i] = core::gray(0.99 + ((r % 4) + 1) * 0.015 - 1.0);
                    t[(size_t)r * 8 + i] = core::gray(0.99 + ((i % 4) + 1) * 0.015 - 1.0);
                }
            }
            segment_ = make_texture(8, 8, s);
            segment_v_ = make_texture(8, 8, t);
        }
        Mesh m;
        m.build(b, 8.0);
        real c = curvature > 0 ? curvature : 1.0;
        real lw = std::min(1.0, 2.0 / line_width);
        for (int r = 0; r <= m.rows; r++) {
            for (int q = 0; q <= m.cols; q++) {
                gfx::Vertex& vx = m.at(q, r);
                real cx = f.fx(vx.x) / W, cy = f.fy(vx.y) / H;
                real dx = cx - 0.5, dy = cy - 0.5;
                real k = curvature > 0 ? (std::sqrt(dx * dx * dx * dx + dy * dy * dy * dy) * 0.25 * c * c + 0.935 * c) : 1.0;
                real v = (vertical_line ? dx * k * W : dy * k * H) * lw / c;
                real phase = v * 1.2 - time;
                vx.u = (float)(phase / (2.0 * core::PI) * N);
                vx.v = 4.0f;
            }
        }
        gfx::set_wrap(true);
        core::draw_mesh(line_pos_, m, Blend::MulAdd, true);
        core::draw_mesh(line_neg_, m, Blend::MulSrc, true);
        if (vertical_line) core::blit(segment_v_, f.fx(b.x0), 0, f.fx(b.x1), 8, b.x0, b.y0, b.x1, b.y1, WHITE, Blend::MulAdd, false);
        else core::blit(segment_, 0, f.fy(b.y0), 8, f.fy(b.y1), b.x0, b.y0, b.x1, b.y1, WHITE, Blend::MulAdd, false);
        gfx::set_wrap(false);
    }
    if (vignetting > 0) {
        const int N = 64;
        if (!vignette_ || built_vig_[0] != vignetting || built_vig_[1] != vignetting_alpha || built_vig_[2] != vignetting_blur) {
            destroy_texture(vignette_);
            std::vector<uint32_t> px((size_t)N * N);
            const real SQ2 = 1.414213;
            real outer = SQ2 - vignetting * SQ2;
            for (int j = 0; j < N; j++) {
                for (int i = 0; i < N; i++) {
                    real dx = (i + 0.5) / N - 0.5, dy = (j + 0.5) / N - 0.5;
                    real darker = std::clamp((outer - std::sqrt(dx * dx + dy * dy) * SQ2) / (0.00001 + vignetting_blur * SQ2), 0.0, 1.0);
                    px[(size_t)j * N + i] = core::gray(darker + (1.0 - darker) * (1.0 - vignetting_alpha));
                }
            }
            vignette_ = make_texture(N, N, px);
            built_vig_[0] = vignetting;
            built_vig_[1] = vignetting_alpha;
            built_vig_[2] = vignetting_blur;
        }
        core::blit(vignette_, f.fx(b.x0) / W * N, f.fy(b.y0) / H * N, f.fx(b.x1) / W * N, f.fy(b.y1) / H * N, b.x0, b.y0, b.x1, b.y1, WHITE,
                   Blend::MulSrc, true);
    }
}

void CrtFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    color_pass(f, rs, draw, [&]() { passes(f); });
}

void BlurFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    real sp = core::pixi_blur_sigma(blur, quality) * S;
    if (scope == Scope::Screen) {
        draw(rs);
        core::scissor(b);
        if (sp < 1.5) {
            core::gaussian_screen(b, sp, sp);
        } else {
            gfx::Texture* g = gfx::grab_screen();
            Box hb = core::downsample(g, b, Surface::Half0, BLACK);
            core::gaussian_half(Surface::Half0, Surface::Half1, hb, sp * 0.5, sp * 0.5);
            gfx::set_target(Surface::Screen);
            core::scissor(b);
            core::upsample(Surface::Half0, hb, b, Blend::Copy, WHITE);
        }
        core::restore_scissor(rs);
        return;
    }
    core::IsolationLock lock;
    if (!lock.ok()) {
        draw(rs);
        return;
    }
    Box hb = core::layers_half(f, rs, draw);
    core::gaussian_half(Surface::Half0, Surface::Half2, hb, sp * 0.5, sp * 0.5);
    core::gaussian_half(Surface::Half1, Surface::Half2, hb, sp * 0.5, sp * 0.5);
    gfx::set_target(Surface::Screen);
    core::scissor(b);
    core::composite_half(hb, b);
    core::restore_scissor(rs);
}

bool BloomFx::identity() const { return bloom_scale == 0 && brightness == 1; }

void BloomFx::bloom(const Box& b, real scale) {
    if (!(scale > 0)) return;
    gfx::Texture* src = gfx::surface_texture(Surface::Grab);
    Box hb = core::downsample(src, b, Surface::Half0, BLACK);
    uint32_t lut_lo[256], lut_hi[256], lut_id[256], lut_thr[256];
    for (int v = 0; v < 256; v++) {
        int lo = v / 2, hi = (v + 1) / 2;
        lut_lo[v] = gfx::rgba(lo, lo, lo, 255);
        lut_hi[v] = gfx::rgba(hi, hi, hi, 255);
        lut_id[v] = gfx::rgba(v, v, v, 255);
        lut_thr[v] = (v / 255.0 > threshold) ? WHITE : BLACK;
    }
    gfx::set_target(Surface::Half1);
    gfx::reset_scissor();
    core::fill(hb, BLACK, Blend::Copy);
    for (int c = 0; c < 3; c++) core::lut_box(gfx::surface_texture(Surface::Half0), c, lut_lo, hb, hb, Blend::Max);
    gfx::set_target(Surface::Half2);
    gfx::reset_scissor();
    core::fill(hb, WHITE, Blend::Copy);
    for (int c = 0; c < 3; c++) core::lut_box(gfx::surface_texture(Surface::Half0), c, lut_id, hb, hb, Blend::Min);
    gfx::set_target(Surface::Half1);
    gfx::reset_scissor();
    core::lut_box(gfx::surface_texture(Surface::Half2), 0, lut_hi, hb, hb, Blend::Add);
    gfx::set_target(Surface::Half2);
    gfx::reset_scissor();
    core::lut_box(gfx::surface_texture(Surface::Half1), 0, lut_thr, hb, hb, Blend::Copy);
    gfx::set_target(Surface::Half0);
    gfx::reset_scissor();
    core::blit_box(gfx::surface_texture(Surface::Half2), hb, WHITE, Blend::MulSrc);
    core::half_ring(Surface::Half0, hb, BLACK);
    real q = std::max(1.0, std::round(quality));
    std::vector<real> kernels;
    kernels.push_back(blur);
    if (blur > 0) {
        real r = blur, o = blur / q;
        for (int i = 1; i < (int)q; i++) {
            r -= o;
            kernels.push_back(r);
        }
    }
    std::vector<real> offsets;
    for (real k : kernels) offsets.push_back((k + 0.5) * pixel_size * S * 0.5);
    Surface res = core::kawase_half(Surface::Half0, Surface::Half1, hb, offsets);
    gfx::set_target(res);
    gfx::reset_scissor();
    core::scale_box(hb, scale);
    gfx::set_target(Surface::Screen);
    core::scissor(b);
    core::upsample(res, hb, b, Blend::Add, WHITE);
}

void BloomFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    if (scope == Scope::Screen) {
        draw(rs);
        gfx::grab_screen();
        core::scissor(b);
        core::scale_box(b, brightness);
        bloom(b, bloom_scale);
        core::restore_scissor(rs);
        return;
    }
    core::IsolationLock lock;
    if (!lock.ok()) {
        draw(rs);
        return;
    }
    core::layers_full(f, rs, draw);
    core::scissor(b);
    core::blit_box(gfx::surface_texture(Surface::Wide), b, WHITE, Blend::MulSub);
    real rem = std::max(0.0, brightness);
    while (rem > 1e-4) {
        real k = std::min(1.0, rem);
        core::blit_box(gfx::surface_texture(Surface::Grab), b, core::gray(k), Blend::Add);
        rem -= k;
    }
    bloom(b, bloom_scale);
    core::restore_scissor(rs);
}

void ZoomBlurFx::taps(const Frame& f, gfx::Texture* src, const Box& b, bool half) {
    const int N = 10;
    Mesh m;
    m.build(b, half ? 8.0 : 16.0);
    size_t nv = m.v.size();
    std::vector<float> ux((size_t)N * nv), uy((size_t)N * nv);
    std::vector<uint8_t> wc((size_t)N * nv);
    real minGradient = inner_radius * 0.3;
    real inner = inner_radius + minGradient * 0.5;
    real gradient = radius * 0.3;
    real outer = radius - gradient * 0.5;
    for (size_t i = 0; i < nv; i++) {
        const gfx::Vertex& p = m.v[i];
        real px = half ? core::phys_of_half_x(p.x) : p.x;
        real py = half ? core::phys_of_half_y(p.y) : p.y;
        real fx = f.fx(px), fy = f.fy(py);
        real dx = center[0] - fx, dy = center[1] - fy;
        real dist = std::sqrt(dx * dx + dy * dy);
        real st = strength;
        real limit = 32.0;
        real delta = 0, gap = 0;
        if (dist < inner) {
            delta = inner - dist;
            gap = minGradient;
        } else if (outer >= 0 && dist > outer) {
            delta = dist - outer;
            gap = gradient;
        }
        if (delta > 0) {
            real d2 = gap > 0 ? (gap - delta) / gap : 0.0;
            limit *= d2;
            st *= d2;
        }
        real range = limit < 1.0 ? 0.0 : std::min(1.0, (std::floor(limit) + 1.5) / 32.0);
        real w[N] = {};
        real total = 0;
        for (int t = 0; t < N; t++) {
            real pc = (t + 0.5) / N * range;
            w[t] = range > 0 ? 4.0 * (pc - pc * pc) : 1.0;
            total += w[t];
        }
        int sum = 0, best = 0;
        for (int t = 0; t < N; t++) {
            int c = std::clamp((int)std::lround(w[t] / total * 255.0), 0, 255);
            wc[(size_t)t * nv + i] = (uint8_t)c;
            sum += c;
            if (w[t] > w[best]) best = t;
        }
        wc[(size_t)best * nv + i] = (uint8_t)std::clamp(wc[(size_t)best * nv + i] + 255 - sum, 0, 255);
        for (int t = 0; t < N; t++) {
            real pc = (t + 0.5) / N * range;
            real sx = fx + dx * st * pc, sy = fy + dy * st * pc;
            real qx = f.px(sx), qy = f.py(sy);
            if (half) {
                qx = core::half_x(qx);
                qy = core::half_y(qy);
            }
            ux[(size_t)t * nv + i] = (float)std::clamp(qx, b.x0 + 0.5, b.x1 - 0.5);
            uy[(size_t)t * nv + i] = (float)std::clamp(qy, b.y0 + 0.5, b.y1 - 0.5);
        }
    }
    core::fill(b, BLACK, Blend::Copy);
    for (int t = 0; t < N; t++) {
        for (size_t i = 0; i < nv; i++) {
            gfx::Vertex& p = m.v[i];
            p.u = ux[(size_t)t * nv + i];
            p.v = uy[(size_t)t * nv + i];
            int c = wc[(size_t)t * nv + i];
            p.color = gfx::rgba(c, c, c, c);
        }
        core::draw_mesh(src, m, Blend::Add, true);
    }
}

void ZoomBlurFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
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

bool ShockwaveFx::identity() const {
    if (radius > 0 && time * speed > radius) return true;
    return amplitude == 0 && brightness == 1;
}

void ShockwaveFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    Mesh m, bright;
    m.build(b, 8.0);
    bright = m;
    real half_wl = wavelength * 0.5;
    real cur = time * speed;
    real fade = radius > 0 ? 1.0 - std::pow(cur / radius, 2.0) : 1.0;
    real potw = next_pow2(f.w), poth = next_pow2(f.h);
    bool any_bright = false;
    for (size_t i = 0; i < m.v.size(); i++) {
        gfx::Vertex& p = m.v[i];
        real fx = f.fx(p.x), fy = f.fy(p.y);
        real dx = fx - center[0], dy = fy - center[1];
        real dist = std::sqrt(dx * dx + dy * dy);
        real mult = 1.0;
        real sx = fx, sy = fy, factor = 1.0;
        if (!(dist <= 0 || dist < cur - half_wl || dist > cur + half_wl)) {
            real diff = (dist - cur) / half_wl;
            real pp = 1.0 - diff * diff;
            real pd = 1.25 * std::sin(diff * core::PI) * pp * (amplitude * fade);
            sx = fx + dx / dist * pd;
            sy = fy + dy / dist * pd;
            real cx = std::clamp(sx, 0.0, f.w - 1), cy = std::clamp(sy, 0.0, f.h - 1);
            if (cx != sx || cy != sy) {
                real ex = (sx - cx) / potw, ey = (sy - cy) / poth;
                factor = std::max(0.0, 1.0 - std::sqrt(ex * ex + ey * ey));
            }
            sx = cx;
            sy = cy;
            mult = 1.0 + (brightness - 1.0) * pp * fade;
        }
        p.u = (float)std::clamp(f.px(sx), b.x0 + 0.5, b.x1 - 0.5);
        p.v = (float)std::clamp(f.py(sy), b.y0 + 0.5, b.y1 - 0.5);
        p.color = core::level(factor);
        gfx::Vertex& q = bright.v[i];
        q.color = brightness >= 1 ? core::gray(mult - 1.0) : core::gray(mult);
        if (mult != 1.0) any_bright = true;
        if (scope == Scope::Object) {
            q.x = p.u;
            q.y = p.v;
        }
    }
    Blend bb = brightness >= 1 ? Blend::MulAdd : Blend::MulSrc;
    if (scope == Scope::Screen) {
        draw(rs);
        gfx::Texture* g = gfx::grab_screen();
        core::scissor(b);
        core::draw_mesh(g, m, Blend::Copy, true);
        if (any_bright) core::draw_mesh(nullptr, bright, bb, false);
        core::restore_scissor(rs);
        return;
    }
    core::IsolationLock lock;
    if (!lock.ok()) {
        draw(rs);
        return;
    }
    core::layers_full(f, rs, draw);
    if (any_bright) {
        gfx::set_target(Surface::Grab);
        core::scissor(b);
        core::draw_mesh(nullptr, bright, bb, false);
        gfx::set_target(Surface::Screen);
    }
    core::scissor(b);
    core::composite_full_mesh(m);
    core::restore_scissor(rs);
}

bool DisplacementMap::load(const std::string& name) {
    if (name == name_ && ready()) return true;
    name_ = name;
    rg_.clear();
    Image* img = images::get("img/pictures", name);
    if (!img || img->w <= 0 || img->h <= 0 || img->ow <= 0 || img->oh <= 0) return false;
    n_ = 256;
    gx_scale_ = (float)n_ / (float)img->ow;
    gy_scale_ = (float)n_ / (float)img->oh;
    rg_.assign((size_t)n_ * n_ * 2, 128);
    for (int j = 0; j < n_; j++) {
        for (int i = 0; i < n_; i++) {
            int px = std::clamp((int)((i + 0.5) / n_ * img->w), 0, img->w - 1);
            int py = std::clamp((int)((j + 0.5) / n_ * img->h), 0, img->h - 1);
            uint32_t c = 0;
            if (!images::read_pixel(img, px, py, &c)) {
                rg_.clear();
                return false;
            }
            rg_[((size_t)j * n_ + i) * 2] = (uint8_t)(c & 255);
            rg_[((size_t)j * n_ + i) * 2 + 1] = (uint8_t)((c >> 8) & 255);
        }
    }
    return true;
}

void DisplacementMap::sample(float lx, float ly, float& r, float& g) const {
    float gx = lx * gx_scale_ - 0.5f, gy = ly * gy_scale_ - 0.5f;
    int x0 = (int)gx, y0 = (int)gy;
    if ((float)x0 > gx) x0--;
    if ((float)y0 > gy) y0--;
    float ax = gx - (float)x0, ay = gy - (float)y0;
    int mask = n_ - 1;
    int xa = x0 & mask, xb = (x0 + 1) & mask;
    const uint8_t* ra = &rg_[(size_t)(y0 & mask) * n_ * 2];
    const uint8_t* rb = &rg_[(size_t)((y0 + 1) & mask) * n_ * 2];
    for (int k = 0; k < 2; k++) {
        float top = ra[xa * 2 + k] * (1 - ax) + ra[xb * 2 + k] * ax;
        float bot = rb[xa * 2 + k] * (1 - ax) + rb[xb * 2 + k] * ax;
        (k == 0 ? r : g) = (top * (1 - ay) + bot * ay) * (1.0f / 255.0f);
    }
}

DisplacementFx::DisplacementFx(std::shared_ptr<DisplacementMap> map) : map_(std::move(map)) {}

void DisplacementFx::render(const Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) {
    const Box& b = f.box;
    Mesh m;
    m.build(b, 8.0);
    const float inv = 1.0f / images::SCALE;
    const float mx = (float)mask_x, my = (float)mask_y, kx = (float)scale_x, ky = (float)scale_y;
    const float u0 = b.x0 + 0.5f, u1 = b.x1 - 0.5f, v0 = b.y0 + 0.5f, v1 = b.y1 - 0.5f;
    for (auto& p : m.v) {
        float lx = (p.x - images::OFFSET_X) * inv, ly = (p.y - images::OFFSET_Y) * inv;
        float r = 0.5f, g = 0.5f;
        map_->sample(lx - mx, ly - my, r, g);
        float sx = lx + (r - 0.5f) * kx, sy = ly + (g - 0.5f) * ky;
        p.u = std::clamp(sx * images::SCALE + images::OFFSET_X, u0, u1);
        p.v = std::clamp(sy * images::SCALE + images::OFFSET_Y, v0, v1);
    }
    warp_pass(f, rs, draw, m);
}

}
}
