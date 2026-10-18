#include "window.h"

#include <cmath>

namespace g2d {

Window::Window() {
    sprite_container_ = std::make_shared<Container>();
    back_sprite_ = std::make_shared<WindowSkinPart>(this, true);
    cursor_sprite_ = std::make_shared<Sprite>();
    frame_sprite_ = std::make_shared<WindowSkinPart>(this, false);
    contents_sprite_ = std::make_shared<Sprite>();
    down_arrow_ = std::make_shared<Sprite>();
    up_arrow_ = std::make_shared<Sprite>();
    pause_sign_ = std::make_shared<Sprite>();
    back_sprite_->alpha = 192.0 / 255.0;
    add_child(sprite_container_);
    sprite_container_->add_child(back_sprite_);
    sprite_container_->add_child(frame_sprite_);
    add_child(cursor_sprite_);
    add_child(contents_sprite_);
    add_child(down_arrow_);
    add_child(up_arrow_);
    add_child(pause_sign_);
}

void Window::set_windowskin(BitmapPtr b) {
    if (windowskin_ == b) return;
    windowskin_ = std::move(b);
    refresh_all_parts();
}

void Window::set_openness(int v) {
    if (openness_ == v) return;
    openness_ = std::clamp(v, 0, 255);
    double r = openness_ / 255.0;
    if (openness_type == 0 || openness_type == 2) {
        sprite_container_->scale_y = r;
        sprite_container_->y = height_ / 2 * (1 - r);
    }
    if (openness_type == 1 || openness_type == 2) {
        sprite_container_->x = width_ / 2 * (1 - r);
        sprite_container_->scale_x = r;
    }
}

void Window::move(double nx, double ny, double w, double h) {
    x = nx;
    y = ny;
    w = std::ceil(w);
    h = std::ceil(h);
    if (width_ != w || height_ != h) {
        width_ = w;
        height_ = h;
        refresh_all_parts();
    }
}

void Window::set_cursor_rect(double cx, double cy, double cw, double ch) {
    cx = std::floor(cx);
    cy = std::floor(cy);
    cw = std::floor(cw);
    ch = std::floor(ch);
    if (cursor_rect_.x != cx || cursor_rect_.y != cy || cursor_rect_.width != cw || cursor_rect_.height != ch) {
        cursor_rect_ = Rect(cx, cy, cw, ch);
        refresh_cursor();
    }
}

void Window::set_tone(int r, int g, int b) {
    if (r != tone_[0] || g != tone_[1] || b != tone_[2]) {
        tone_[0] = r;
        tone_[1] = g;
        tone_[2] = b;
        refresh_back();
    }
}

void Window::add_child_to_back(DisplayPtr child) {
    long idx = index_of(sprite_container_.get());
    add_child_at(std::move(child), (size_t)(idx + 1));
}

void Window::update() {
    if (active) animation_count++;
    Container::update();
}

void Window::refresh_all_parts() {
    refresh_back();
    refresh_frame();
    refresh_cursor();
    refresh_contents();
    refresh_arrows();
    refresh_pause_sign();
}

void Window::refresh_back() {
    double m = margin_;
    back_sprite_->move(m, m);
    back_sprite_->set_bitmap(nullptr);
    back_baked_ = false;
    if (tone_[0] || tone_[1] || tone_[2]) bake_back();
}

void Window::bake_back() {
    double m = margin_;
    double w = width_ - m * 2, h = height_ - m * 2;
    BitmapPtr bmp = Bitmap::create((int)std::max(0.0, w), (int)std::max(0.0, h));
    back_sprite_->set_bitmap(bmp);
    back_sprite_->set_frame(0, 0, w, h);
    back_baked_ = true;
    if (w > 0 && h > 0 && windowskin_) {
        double p = 96;
        bmp->blt(windowskin_.get(), 0, 0, p, p, 0, 0, w, h);
        for (double yy = 0; yy < h; yy += p) {
            for (double xx = 0; xx < w; xx += p) bmp->blt(windowskin_.get(), 0, p, p, p, xx, yy, p, p);
        }
        bmp->adjust_tone(tone_[0], tone_[1], tone_[2]);
    }
}

void Window::refresh_frame() {
    frame_sprite_->set_bitmap(nullptr);
    frame_baked_ = false;
    if (!direct_frame_possible()) bake_frame();
}

bool Window::direct_frame_possible() const { return width_ >= 48 && height_ >= 48; }

void Window::bake_frame() {
    double w = width_, h = height_;
    double m = 24;
    BitmapPtr bmp = Bitmap::create((int)std::max(0.0, w), (int)std::max(0.0, h));
    frame_sprite_->set_bitmap(bmp);
    frame_sprite_->set_frame(0, 0, w, h);
    frame_baked_ = true;
    if (w > 0 && h > 0 && windowskin_) {
        Bitmap* skin = windowskin_.get();
        double p = 96, q = 96;
        double owid = p - m * 2, nwid = w - m * 2, ohei = p - m * 2, nhei = h - m * 2;
        int hrep = (int)std::floor(nwid / owid), vrep = (int)std::floor(nhei / ohei);
        double hrem = std::fmod(nwid, owid), vrem = std::fmod(nhei, ohei);
        for (int i = 0; i < hrep; i++) bmp->blt(skin, p + m, 0, owid, m, m + i * owid, 0, owid, m);
        bmp->blt(skin, p + m, 0, hrem, m, m + owid * hrep, 0, hrem, m);
        for (int i = 0; i < hrep; i++) bmp->blt(skin, p + m, q - m, owid, m, m + i * owid, h - m, owid, m);
        bmp->blt(skin, p + m, q - m, hrem, m, m + owid * hrep, h - m, hrem, m);
        for (int i = 0; i < vrep; i++) bmp->blt(skin, p, m, m, ohei, 0, m + i * ohei, m, ohei);
        bmp->blt(skin, p, m, m, vrem, 0, m + vrep * ohei, m, vrem);
        for (int i = 0; i < vrep; i++) bmp->blt(skin, p + q - m, m, m, ohei, w - m, m + i * ohei, m, ohei);
        bmp->blt(skin, p + q - m, m, m, vrem, w - m, m + vrep * ohei, m, vrem);
        bmp->blt(skin, p + 0, 0 + 0, m, m, 0, 0, m, m);
        bmp->blt(skin, p + q - m, 0 + 0, m, m, w - m, 0, m, m);
        bmp->blt(skin, p + 0, 0 + q - m, m, m, 0, h - m, m, m);
        bmp->blt(skin, p + q - m, 0 + q - m, m, m, w - m, h - m, m, m);
    }
}

static void draw_piece(Image* img, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh, bool smooth,
                       const RenderState& rs, int blend) {
    if (!(sw > 0 && sh > 0 && dw > 0 && dh > 0)) return;
    Mat t;
    t.a = (float)(dw / sw);
    t.d = (float)(dh / sh);
    t.tx = (float)dx;
    t.ty = (float)dy;
    draw_image(img, sx, sy, sw, sh, rs.world.mul(t), rs.alpha, blend, smooth, rs);
}

void Window::draw_back_direct(const RenderState& rs) {
    double m = margin_;
    double w = width_ - m * 2, h = height_ - m * 2;
    if (!(w > 0 && h > 0) || !windowskin_) return;
    Image* img = windowskin_->image();
    if (!img) return;
    windowskin_->touch();
    int blend = back_sprite_->blend_mode;
    double p = 96;
    draw_piece(img, 0, 0, p, p, 0, 0, w, h, true, rs, blend);
    for (double yy = 0; yy < h; yy += p) {
        for (double xx = 0; xx < w; xx += p) {
            double cw = std::min(p, w - xx), ch = std::min(p, h - yy);
            draw_piece(img, 0, p, cw, ch, xx, yy, cw, ch, windowskin_->smooth, rs, blend);
        }
    }
}

void Window::draw_frame_direct(const RenderState& rs) {
    double w = width_, h = height_;
    if (!(w > 0 && h > 0) || !windowskin_) return;
    Image* img = windowskin_->image();
    if (!img) return;
    windowskin_->touch();
    int blend = frame_sprite_->blend_mode;
    bool sm = windowskin_->smooth;
    double m = 24, p = 96, q = 96;
    double owid = p - m * 2, nwid = w - m * 2, ohei = p - m * 2, nhei = h - m * 2;
    int hrep = (int)std::floor(nwid / owid), vrep = (int)std::floor(nhei / ohei);
    double hrem = std::fmod(nwid, owid), vrem = std::fmod(nhei, ohei);
    for (int i = 0; i < hrep; i++) draw_piece(img, p + m, 0, owid, m, m + i * owid, 0, owid, m, sm, rs, blend);
    draw_piece(img, p + m, 0, hrem, m, m + owid * hrep, 0, hrem, m, sm, rs, blend);
    for (int i = 0; i < hrep; i++) draw_piece(img, p + m, q - m, owid, m, m + i * owid, h - m, owid, m, sm, rs, blend);
    draw_piece(img, p + m, q - m, hrem, m, m + owid * hrep, h - m, hrem, m, sm, rs, blend);
    for (int i = 0; i < vrep; i++) draw_piece(img, p, m, m, ohei, 0, m + i * ohei, m, ohei, sm, rs, blend);
    draw_piece(img, p, m, m, vrem, 0, m + vrep * ohei, m, vrem, sm, rs, blend);
    for (int i = 0; i < vrep; i++) draw_piece(img, p + q - m, m, m, ohei, w - m, m + i * ohei, m, ohei, sm, rs, blend);
    draw_piece(img, p + q - m, m, m, vrem, w - m, m + vrep * ohei, m, vrem, sm, rs, blend);
    draw_piece(img, p + 0, 0 + 0, m, m, 0, 0, m, m, sm, rs, blend);
    draw_piece(img, p + q - m, 0 + 0, m, m, w - m, 0, m, m, sm, rs, blend);
    draw_piece(img, p + 0, 0 + q - m, m, m, 0, h - m, m, m, sm, rs, blend);
    draw_piece(img, p + q - m, 0 + q - m, m, m, w - m, h - m, m, m, sm, rs, blend);
}

void WindowSkinPart::render(const RenderState& rs) {
    const Mat& w = rs.world;
    if (renderable && std::fabs(w.a * w.d - w.b * w.c) > 1e-6f) {
        if (back_) {
            bool direct = !(owner_->tone_[0] || owner_->tone_[1] || owner_->tone_[2]) && rs.alpha >= 1.0;
            if (direct) {
                owner_->draw_back_direct(rs);
            } else {
                if (!owner_->back_baked_) owner_->bake_back();
                render_self(rs);
            }
        } else if (owner_->frame_baked_) {
            render_self(rs);
        } else {
            owner_->draw_frame_direct(rs);
        }
    }
    render_children(rs);
}

void Window::refresh_cursor() {
    double pad = padding_;
    double cx = cursor_rect_.x + pad - origin_x, cy = cursor_rect_.y + pad - origin_y;
    double w = cursor_rect_.width, h = cursor_rect_.height;
    double m = 4;
    double x2 = std::max(cx, pad), y2 = std::max(cy, pad);
    double ox = cx - x2, oy = cy - y2;
    double w2 = std::min(w, width_ - pad - x2), h2 = std::min(h, height_ - pad - y2);
    BitmapPtr bmp = Bitmap::create((int)std::max(0.0, w2), (int)std::max(0.0, h2));
    cursor_sprite_->set_bitmap(bmp);
    cursor_sprite_->set_frame(0, 0, w2, h2);
    cursor_sprite_->move(x2, y2);
    if (w > 0 && h > 0 && windowskin_) {
        Bitmap* skin = windowskin_.get();
        double p = 96, q = 48;
        bmp->blt(skin, p + m, p + m, q - m * 2, q - m * 2, ox + m, oy + m, w - m * 2, h - m * 2);
        bmp->blt(skin, p + m, p + 0, q - m * 2, m, ox + m, oy + 0, w - m * 2, m);
        bmp->blt(skin, p + m, p + q - m, q - m * 2, m, ox + m, oy + h - m, w - m * 2, m);
        bmp->blt(skin, p + 0, p + m, m, q - m * 2, ox + 0, oy + m, m, h - m * 2);
        bmp->blt(skin, p + q - m, p + m, m, q - m * 2, ox + w - m, oy + m, m, h - m * 2);
        bmp->blt(skin, p + 0, p + 0, m, m, ox + 0, oy + 0, m, m);
        bmp->blt(skin, p + q - m, p + 0, m, m, ox + w - m, oy + 0, m, m);
        bmp->blt(skin, p + 0, p + q - m, m, m, ox + 0, oy + h - m, m, m);
        bmp->blt(skin, p + q - m, p + q - m, m, m, ox + w - m, oy + h - m, m, m);
    }
}

void Window::refresh_contents() { contents_sprite_->move(padding_, padding_); }

void Window::refresh_arrows() {
    double w = width_, h = height_;
    double p = 24, q = p / 2;
    double sx = 96 + p, sy = 0 + p;
    down_arrow_->set_bitmap(windowskin_);
    down_arrow_->anchor_x = 0.5;
    down_arrow_->anchor_y = 0.5;
    down_arrow_->set_frame(sx + q, sy + q + p, p, q);
    down_arrow_->move(w / 2, h - q);
    up_arrow_->set_bitmap(windowskin_);
    up_arrow_->anchor_x = 0.5;
    up_arrow_->anchor_y = 0.5;
    up_arrow_->set_frame(sx + q, sy, p, q);
    up_arrow_->move(w / 2, q);
}

void Window::refresh_pause_sign() {
    double sx = 144, sy = 96, p = 24;
    pause_sign_->set_bitmap(windowskin_);
    pause_sign_->anchor_x = 0.5;
    pause_sign_->anchor_y = 1;
    pause_sign_->move(width_ / 2, height_);
    pause_sign_->set_frame(sx, sy, p, p);
    pause_sign_->alpha = 0;
}

void Window::update_cursor() {
    int blink = animation_count % 40;
    double op = contents_opacity();
    if (active) {
        if (blink < 20) op -= blink * 8;
        else op -= (40 - blink) * 8;
    }
    cursor_sprite_->alpha = op / 255.0;
    cursor_sprite_->visible = is_open();
}

void Window::update_contents() {
    double w = width_ - padding_ * 2, h = height_ - padding_ * 2;
    if (w > 0 && h > 0) {
        contents_sprite_->set_frame(origin_x, origin_y, w, h);
        contents_sprite_->visible = is_open();
    } else {
        contents_sprite_->visible = false;
    }
}

void Window::update_arrows() {
    down_arrow_->visible = is_open() && down_arrow_visible;
    up_arrow_->visible = is_open() && up_arrow_visible;
}

void Window::update_pause_sign() {
    int xx = (animation_count / 16) % 2;
    int yy = (animation_count / 16 / 2) % 2;
    double sx = 144, sy = 96, p = 24;
    if (!pause) pause_sign_->alpha = 0;
    else if (pause_sign_->alpha < 1) pause_sign_->alpha = std::min(pause_sign_->alpha + 0.1, 1.0);
    pause_sign_->set_frame(sx + xx * p, sy + yy * p, p, p);
    pause_sign_->visible = is_open();
}

void Window::update_transform_hook() {}

void Window::render(const RenderState& rs) {
    update_cursor();
    update_arrows();
    update_pause_sign();
    update_contents();
    update_transform_hook();
    render_children(rs);
}

}
