#include "display.h"

#include <algorithm>
#include <cmath>
#include <deque>

namespace g2d {

static int g_sprite_counter = 0;

int next_sprite_id() { return g_sprite_counter++; }

DisplayObject::DisplayObject() : sprite_id(next_sprite_id()) {}

DisplayObject::~DisplayObject() {}

Mat DisplayObject::local_matrix() const {
    Mat m;
    if (rotation == 0 && skew_x == 0 && skew_y == 0) {
        m.a = scale_x;
        m.b = 0;
        m.c = 0;
        m.d = scale_y;
        m.tx = x - pivot_x * m.a;
        m.ty = y - pivot_y * m.d;
        return m;
    }
    double cr = std::cos(rotation + skew_y), sr = std::sin(rotation + skew_y);
    double cs = -std::sin(rotation - skew_x), ss = std::cos(rotation - skew_x);
    m.a = cr * scale_x;
    m.b = sr * scale_x;
    m.c = cs * scale_y;
    m.d = ss * scale_y;
    m.tx = x - (pivot_x * m.a + pivot_y * m.c);
    m.ty = y - (pivot_x * m.b + pivot_y * m.d);
    return m;
}

Mat DisplayObject::world_matrix() const {
    Mat m = local_matrix();
    for (Container* p = parent; p; p = p->parent) m = p->local_matrix().mul(m);
    return m;
}

void DisplayObject::remove_from_parent() {
    if (parent) parent->remove_child(this);
}

DisplayPtr Container::add_child(DisplayPtr c) {
    if (!c) return c;
    if (c->parent) c->parent->remove_child(c.get());
    c->parent = this;
    children.push_back(c);
    return c;
}

DisplayPtr Container::add_child_at(DisplayPtr c, size_t index) {
    if (!c) return c;
    if (c->parent) c->parent->remove_child(c.get());
    c->parent = this;
    if (index > children.size()) index = children.size();
    children.insert(children.begin() + (long)index, c);
    return c;
}

void Container::remove_child(DisplayObject* c) {
    for (size_t i = 0; i < children.size(); i++) {
        if (children[i].get() == c) {
            children[i]->parent = nullptr;
            children.erase(children.begin() + (long)i);
            return;
        }
    }
}

DisplayPtr Container::remove_child_at(size_t index) {
    if (index >= children.size()) return nullptr;
    DisplayPtr c = children[index];
    c->parent = nullptr;
    children.erase(children.begin() + (long)index);
    return c;
}

void Container::remove_children() {
    for (auto& c : children) c->parent = nullptr;
    children.clear();
}

long Container::index_of(DisplayObject* c) const {
    for (size_t i = 0; i < children.size(); i++) {
        if (children[i].get() == c) return (long)i;
    }
    return -1;
}

void Container::set_child_index(DisplayObject* c, size_t index) {
    long cur = index_of(c);
    if (cur < 0) return;
    DisplayPtr keep = children[(size_t)cur];
    children.erase(children.begin() + cur);
    if (index > children.size()) index = children.size();
    children.insert(children.begin() + (long)index, keep);
}

namespace {

std::deque<std::vector<DisplayPtr>> g_scratch;
size_t g_scratch_depth = 0;

struct Snapshot {
    std::vector<DisplayPtr>& items;
    explicit Snapshot(const std::vector<DisplayPtr>& src) : items(acquire()) { items.assign(src.begin(), src.end()); }
    ~Snapshot() {
        items.clear();
        g_scratch_depth--;
    }
    static std::vector<DisplayPtr>& acquire() {
        if (g_scratch_depth >= g_scratch.size()) g_scratch.emplace_back();
        return g_scratch[g_scratch_depth++];
    }
};

}

void Container::update() {
    if (!update_children_in_update) return;
    Snapshot snap(children);
    for (auto& c : snap.items) c->update();
}

void Container::render_children(const RenderState& rs) {
    if (comparator && children.size() > 1 && !std::is_sorted(children.begin(), children.end(), comparator))
        std::stable_sort(children.begin(), children.end(), comparator);
    Snapshot snap(children);
    std::vector<DisplayPtr>& snapshot = snap.items;
    for (auto& c : snapshot) {
        if (!c->visible || c->alpha <= 0) continue;
        RenderState cs = rs;
        cs.world = rs.world.mul(c->local_matrix());
        cs.alpha = rs.alpha * c->alpha;
        if (c->mask_rect) {
            float x0, y0, x1, y1;
            screen_point(cs.world, c->mask_rect->x, c->mask_rect->y, x0, y0);
            screen_point(cs.world, c->mask_rect->x + c->mask_rect->width, c->mask_rect->y + c->mask_rect->height, x1, y1);
            int ix0 = (int)std::lround(std::min(x0, x1)), iy0 = (int)std::lround(std::min(y0, y1));
            int ix1 = (int)std::lround(std::max(x0, x1)), iy1 = (int)std::lround(std::max(y0, y1));
            cs.clip = true;
            cs.cx0 = std::max(rs.cx0, ix0);
            cs.cy0 = std::max(rs.cy0, iy0);
            cs.cx1 = std::min(rs.cx1, ix1);
            cs.cy1 = std::min(rs.cy1, iy1);
            if (cs.cx1 <= cs.cx0 || cs.cy1 <= cs.cy0) continue;
            gfx::set_scissor(cs.cx0, cs.cy0, cs.cx1 - cs.cx0, cs.cy1 - cs.cy0);
            if (c->renderable) render_object(*c, cs);
            if (rs.clip) gfx::set_scissor(rs.cx0, rs.cy0, rs.cx1 - rs.cx0, rs.cy1 - rs.cy0);
            else gfx::reset_scissor();
        } else if (c->renderable || c->is_container()) {
            render_object(*c, cs);
        }
    }
}

void Container::render(const RenderState& rs) { render_children(rs); }

static void render_filtered(DisplayObject& obj, const RenderState& rs, size_t n) {
    while (n > 0 && !(obj.filters[n - 1] && obj.filters[n - 1]->enabled)) n--;
    if (n == 0) {
        obj.render(rs);
        return;
    }
    FilterPtr f = obj.filters[n - 1];
    f->apply(obj, rs, [&obj, n](const RenderState& s) { render_filtered(obj, s, n - 1); });
}

static int g_rendered = 0;

int take_render_count() {
    int n = g_rendered;
    g_rendered = 0;
    return n;
}

void render_object(DisplayObject& obj, const RenderState& rs) {
    g_rendered++;
    if (obj.filters.empty()) {
        obj.render(rs);
        return;
    }
    render_filtered(obj, rs, obj.filters.size());
}

void screen_point(const Mat& m, double lx, double ly, float& sx, float& sy) {
    float wx, wy;
    m.apply((float)lx, (float)ly, wx, wy);
    sx = wx * (float)S + (float)images::OFFSET_X;
    sy = wy * (float)S + (float)images::OFFSET_Y;
}

gfx::Blend blend_of(int mode) {
    switch (mode) {
    case 1: return gfx::Blend::Add;
    case 2: return gfx::Blend::Multiply;
    case 3: return gfx::Blend::Screen;
    default: return gfx::Blend::Normal;
    }
}

static inline bool near_int(float v) { return std::fabs(v - std::round(v)) < 0.02f; }

static void draw_chunks(Image* img, double sx, double sy, double sw, double sh, const Mat& m, uint32_t color, bool flat, uint32_t rgb, int a8,
                        gfx::Blend blend, bool smooth) {
    if (!img || sw <= 0 || sh <= 0) return;
    float tx0 = (float)(sx * img->fx), ty0 = (float)(sy * img->fy);
    float tx1 = (float)((sx + sw) * img->fx), ty1 = (float)((sy + sh) * img->fy);
    if (m.axis_aligned()) {
        float x0, y0, x1, y1;
        screen_point(m, 0, 0, x0, y0);
        screen_point(m, sw, sh, x1, y1);
        float pw = std::fabs(x1 - x0), ph = std::fabs(y1 - y0);
        float tw = tx1 - tx0, th = ty1 - ty0;
        bool unscaled = std::fabs(pw - tw) < 0.05f && std::fabs(ph - th) < 0.05f;
        bool sm = smooth;
        if (unscaled) {
            float rx = std::round(x0), ry = std::round(y0);
            x1 += rx - x0;
            y1 += ry - y0;
            x0 = rx;
            y0 = ry;
            sm = !(near_int(tx0) && near_int(ty0));
        }
        float kx = (x1 - x0) / (tx1 - tx0), ky = (y1 - y0) / (ty1 - ty0);
        for (size_t ci = 0; ci < img->chunks.size(); ci++) {
            const ImageChunk& c = img->chunks[ci];
            float cx0 = std::max(tx0, (float)c.x), cy0 = std::max(ty0, (float)c.y);
            float cx1 = std::min(tx1, (float)(c.x + c.w)), cy1 = std::min(ty1, (float)(c.y + c.h));
            if (cx1 <= cx0 || cy1 <= cy0) continue;
            gfx::Texture* ctex = images::chunk_texture(img, ci);
            if (!ctex) continue;
            float dx0 = x0 + (cx0 - tx0) * kx, dx1 = x0 + (cx1 - tx0) * kx;
            float dy0 = y0 + (cy0 - ty0) * ky, dy1 = y0 + (cy1 - ty0) * ky;
            if (flat) gfx::sprite_flat(ctex, cx0 - c.x, cy0 - c.y, cx1 - c.x, cy1 - c.y, dx0, dy0, dx1, dy1, rgb, a8, blend, sm);
            else gfx::sprite(ctex, cx0 - c.x, cy0 - c.y, cx1 - c.x, cy1 - c.y, dx0, dy0, dx1, dy1, color, blend, sm);
        }
        return;
    }
    for (size_t ci = 0; ci < img->chunks.size(); ci++) {
        const ImageChunk& ch = img->chunks[ci];
        float cx0 = std::max(tx0, (float)ch.x), cy0 = std::max(ty0, (float)ch.y);
        float cx1 = std::min(tx1, (float)(ch.x + ch.w)), cy1 = std::min(ty1, (float)(ch.y + ch.h));
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        gfx::Texture* ctex = images::chunk_texture(img, ci);
        if (!ctex) continue;
        float lx[4] = {cx0, cx1, cx1, cx0}, ly[4] = {cy0, cy0, cy1, cy1};
        float xy[8], uv[8];
        for (int k = 0; k < 4; k++) {
            double ox = (lx[k] - tx0) / img->fx, oy = (ly[k] - ty0) / img->fy;
            screen_point(m, ox, oy, xy[k * 2], xy[k * 2 + 1]);
            uv[k * 2] = lx[k] - ch.x;
            uv[k * 2 + 1] = ly[k] - ch.y;
        }
        if (flat) gfx::quad_flat(ctex, uv, xy, rgb, a8, blend, true);
        else gfx::draw_quad(ctex, uv, xy, color, blend, true);
    }
}

void draw_image(Image* img, double sx, double sy, double sw, double sh, const Mat& m, double alpha, int blend, bool smooth, const RenderState& rs) {
    (void)rs;
    int a = std::clamp((int)std::lround(alpha * 255.0), 0, 255);
    if (a <= 0) return;
    uint32_t color = gfx::rgba(a, a, a, a);
    draw_chunks(img, sx, sy, sw, sh, m, color, false, 0, 0, blend_of(blend), smooth);
}

void draw_flat(Image* img, double sx, double sy, double sw, double sh, const Mat& m, uint32_t rgb, double alpha, int blend, bool smooth,
               const RenderState& rs) {
    (void)rs;
    int a = std::clamp((int)std::lround(alpha * 255.0), 0, 255);
    if (a <= 0) return;
    draw_chunks(img, sx, sy, sw, sh, m, 0, true, rgb, a, blend_of(blend), smooth);
}

void draw_fill(double x, double y, double w, double h, const Mat& m, int r, int g, int b, double alpha, int blend, const RenderState& rs) {
    (void)rs;
    int a = std::clamp((int)std::lround(alpha * 255.0), 0, 255);
    if (a <= 0) return;
    float x0, y0, x1, y1;
    screen_point(m, x, y, x0, y0);
    screen_point(m, x + w, y + h, x1, y1);
    gfx::fill(std::round(std::min(x0, x1)), std::round(std::min(y0, y1)), std::round(std::max(x0, x1)), std::round(std::max(y0, y1)),
              gfx::premul(r, g, b, a), blend_of(blend));
}

Sprite::Sprite() {}

Sprite::Sprite(BitmapPtr b) { set_bitmap(std::move(b)); }

void Sprite::set_bitmap(BitmapPtr b) {
    if (bitmap_ == b) return;
    bitmap_ = std::move(b);
    if (bitmap_) {
        refresh_frame_ = false;
        frame_.width = bitmap_->width();
        frame_.height = bitmap_->height();
    }
    tint_dirty_ = true;
    refresh();
}

void Sprite::set_frame(double fx, double fy, double fw, double fh) {
    refresh_frame_ = false;
    if (fx != frame_.x || fy != frame_.y || fw != frame_.width || fh != frame_.height) {
        frame_ = Rect(fx, fy, fw, fh);
        tint_dirty_ = true;
        refresh();
    }
}

void Sprite::set_width(double w) {
    frame_.width = w;
    tint_dirty_ = true;
    refresh();
}

void Sprite::set_height(double h) {
    frame_.height = h;
    tint_dirty_ = true;
    refresh();
}

void Sprite::refresh() {
    double fxv = std::floor(frame_.x), fyv = std::floor(frame_.y);
    double fw = std::floor(frame_.width), fh = std::floor(frame_.height);
    double bw = bitmap_ ? bitmap_->width() : 0, bh = bitmap_ ? bitmap_->height() : 0;
    double rx = std::clamp(fxv, 0.0, bw), ry = std::clamp(fyv, 0.0, bh);
    double rw = std::clamp(fw - rx + fxv, 0.0, bw - rx), rh = std::clamp(fh - ry + fyv, 0.0, bh - ry);
    real_frame_ = Rect(rx, ry, rw, rh);
    pivot_x = fxv - rx;
    pivot_y = fyv - ry;
}

void Sprite::set_blend_color(const double c[4]) {
    if (std::equal(c, c + 4, blend_color_)) return;
    std::copy(c, c + 4, blend_color_);
    tint_dirty_ = true;
    refresh();
}

void Sprite::set_color_tone(const double t[4]) {
    if (std::equal(t, t + 4, color_tone_)) return;
    std::copy(t, t + 4, color_tone_);
    tint_dirty_ = true;
    refresh();
}

bool Sprite::has_tint() const { return color_tone_[0] || color_tone_[1] || color_tone_[2] || color_tone_[3] || blend_color_[3] > 0; }

static inline int clamp8(double v) { return std::clamp((int)std::lround(v), 0, 255); }

void Sprite::build_tint() {
    int rw = (int)real_frame_.width, rh = (int)real_frame_.height;
    if (rw <= 0 || rh <= 0 || !bitmap_) {
        tinted_.reset();
        return;
    }
    if (!tinted_ || tinted_->width() != rw || tinted_->height() != rh) tinted_ = Bitmap::create(rw, rh);
    tinted_->clear();
    tinted_->blt(bitmap_.get(), real_frame_.x, real_frame_.y, rw, rh, 0, 0);
    Image* img = tinted_->image();
    if (!img) return;
    double gray = std::max(0.0, color_tone_[3]) / 255.0;
    double r1 = std::max(0.0, color_tone_[0]), g1 = std::max(0.0, color_tone_[1]), b1 = std::max(0.0, color_tone_[2]);
    double r2 = std::max(0.0, -color_tone_[0]), g2 = std::max(0.0, -color_tone_[1]), b2 = std::max(0.0, -color_tone_[2]);
    double r3 = std::max(0.0, blend_color_[0]), g3 = std::max(0.0, blend_color_[1]), b3 = std::max(0.0, blend_color_[2]);
    double a3 = std::max(0.0, blend_color_[3]) / 255.0;
    int pw = img->w, ph = img->h;
    std::vector<uint32_t> out((size_t)pw * ph);
    for (int y = 0; y < ph; y++) {
        for (int x = 0; x < pw; x++) {
            uint32_t p = tinted_->pixel_premul(x, y);
            double A = (p >> 24) / 255.0;
            double pr = (p & 255), pg = ((p >> 8) & 255), pb = ((p >> 16) & 255);
            double cr = A > 0 ? pr / A : 0, cg = A > 0 ? pg / A : 0, cb = A > 0 ? pb / A : 0;
            double L = 0.3 * cr + 0.59 * cg + 0.11 * cb;
            double rr = (1 - gray) * cr * A + (1 - A) * 255 * gray + gray * A * L;
            double rg = (1 - gray) * cg * A + (1 - A) * 255 * gray + gray * A * L;
            double rb = (1 - gray) * cb * A + (1 - A) * 255 * gray + gray * A * L;
            rr = std::min(255.0, rr + r1);
            rg = std::min(255.0, rg + g1);
            rb = std::min(255.0, rb + b1);
            rr = std::max(0.0, rr - r2);
            rg = std::max(0.0, rg - g2);
            rb = std::max(0.0, rb - b2);
            rr = r3 * a3 + rr * (1 - a3);
            rg = g3 * a3 + rg * (1 - a3);
            rb = b3 * a3 + rb * (1 - a3);
            out[(size_t)y * pw + x] = (uint32_t)clamp8(rr * A) | ((uint32_t)clamp8(rg * A) << 8) | ((uint32_t)clamp8(rb * A) << 16) |
                                      ((uint32_t)clamp8(A * 255) << 24);
        }
    }
    tinted_->write_pixels(out.data(), pw, ph);
}

void Sprite::render(const RenderState& rs) {
    if (renderable) render_self(rs);
    render_children(rs);
}

void Sprite::render_self(const RenderState& rs) {
    if (!bitmap_) return;
    if (real_frame_.width <= 0 || real_frame_.height <= 0) return;
    Mat m = rs.world;
    Mat off;
    off.tx = -anchor_x * real_frame_.width;
    off.ty = -anchor_y * real_frame_.height;
    m = m.mul(off);
    if (has_tint()) {
        int ver = bitmap_->version;
        if (tint_dirty_ || ver != tint_version_) {
            build_tint();
            tint_version_ = ver;
            tint_dirty_ = false;
        }
        if (!tinted_) return;
        tinted_->touch();
        draw_image(tinted_->image(), 0, 0, real_frame_.width, real_frame_.height, m, rs.alpha, blend_mode, bitmap_->smooth, rs);
        return;
    }
    bitmap_->touch();
    draw_image(bitmap_->image(), real_frame_.x, real_frame_.y, real_frame_.width, real_frame_.height, m, rs.alpha, blend_mode, bitmap_->smooth, rs);
}

TilingSprite::TilingSprite(BitmapPtr b) : bitmap(std::move(b)) {}

void TilingSprite::render(const RenderState& rs) {
    if (renderable && bitmap && width > 0 && height > 0) {
        Image* img = bitmap->image();
        bitmap->touch();
        double fx0 = frame_set ? frame.x : 0, fy0 = frame_set ? frame.y : 0;
        double fw = frame_set ? frame.width : bitmap->width(), fh = frame_set ? frame.height : bitmap->height();
        if (img && fw > 0 && fh > 0) {
            double ox = std::fmod(origin_x, fw), oy = std::fmod(origin_y, fh);
            if (ox < 0) ox += fw;
            if (oy < 0) oy += fh;
            const Mat& w = rs.world;
            if (w.axis_aligned() && std::fabs(std::fabs(w.a) * S - img->fx) < 0.01f && std::fabs(std::fabs(w.d) * S - img->fy) < 0.01f) {
                ox = std::round(ox * img->fx) / img->fx;
                oy = std::round(oy * img->fy) / img->fy;
            }
            for (double ty = -oy; ty < height; ty += fh) {
                for (double tx = -ox; tx < width; tx += fw) {
                    double cx0 = std::max(tx, 0.0), cy0 = std::max(ty, 0.0);
                    double cx1 = std::min(tx + fw, width), cy1 = std::min(ty + fh, height);
                    if (cx1 <= cx0 || cy1 <= cy0) continue;
                    Mat m = rs.world;
                    Mat t;
                    t.tx = cx0;
                    t.ty = cy0;
                    m = m.mul(t);
                    draw_image(img, fx0 + (cx0 - tx), fy0 + (cy0 - ty), cx1 - cx0, cy1 - cy0, m, rs.alpha, blend_mode, bitmap->smooth, rs);
                }
            }
        }
    }
    render_children(rs);
}

void ScreenSprite::render(const RenderState& rs) {
    if (renderable && rs.alpha > 0) {
        int a = std::clamp((int)std::lround(rs.alpha * 255.0), 0, 255);
        gfx::fill(0, 0, gfx::SCREEN_W, gfx::SCREEN_H, gfx::premul(red, green, blue, a), blend_of(blend_mode));
    }
    render_children(rs);
}

}
