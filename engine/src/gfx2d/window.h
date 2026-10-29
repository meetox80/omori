#pragma once
#include "display.h"

namespace g2d {

class Window;

class WindowSkinPart : public Sprite {
public:
    WindowSkinPart(Window* owner, bool back) : owner_(owner), back_(back) {}
    void render(const RenderState& rs) override;

private:
    Window* owner_;
    bool back_;
};

class Window : public Container {
public:
    Window();
    bool active = true;
    bool down_arrow_visible = false;
    bool up_arrow_visible = false;
    bool pause = false;
    double origin_x = 0, origin_y = 0;

    BitmapPtr windowskin() const { return windowskin_; }
    void set_windowskin(BitmapPtr b);
    BitmapPtr contents() const { return contents_sprite_->bitmap(); }
    void set_contents(BitmapPtr b) { contents_sprite_->set_bitmap(std::move(b)); }
    double width() const { return width_; }
    double height() const { return height_; }
    void set_width(double w) {
        width_ = std::ceil(w);
        refresh_all_parts();
    }
    void set_height(double h) {
        height_ = std::ceil(h);
        refresh_all_parts();
    }
    int padding() const { return padding_; }
    void set_padding(int p) {
        padding_ = p;
        refresh_all_parts();
    }
    int margin() const { return margin_; }
    void set_margin(int m) {
        margin_ = m;
        refresh_all_parts();
    }
    double window_opacity() const { return sprite_container_->alpha * 255.0; }
    void set_window_opacity(double v) { sprite_container_->alpha = std::clamp(v, 0.0, 255.0) / 255.0; }
    double back_opacity() const { return back_sprite_->alpha * 255.0; }
    void set_back_opacity(double v) { back_sprite_->alpha = std::clamp(v, 0.0, 255.0) / 255.0; }
    double contents_opacity() const { return contents_sprite_->alpha * 255.0; }
    void set_contents_opacity(double v) { contents_sprite_->alpha = std::clamp(v, 0.0, 255.0) / 255.0; }
    int openness() const { return openness_; }
    void set_openness(int v);
    bool is_open() const { return openness_ >= 255; }
    bool is_closed() const { return openness_ <= 0; }
    void move(double nx, double ny, double w, double h);
    void set_cursor_rect(double cx, double cy, double cw, double ch);
    const Rect& cursor_rect() const { return cursor_rect_; }
    void set_tone(int r, int g, int b);
    void add_child_to_back(DisplayPtr child);
    void update() override;
    void render(const RenderState& rs) override;
    int animation_count = 0;
    int openness_type = 0;

    SpritePtr back_sprite() const { return back_sprite_; }
    SpritePtr frame_sprite() const { return frame_sprite_; }
    SpritePtr cursor_sprite() const { return cursor_sprite_; }
    SpritePtr contents_sprite() const { return contents_sprite_; }
    SpritePtr down_arrow_sprite() const { return down_arrow_; }
    SpritePtr up_arrow_sprite() const { return up_arrow_; }
    SpritePtr pause_sign_sprite() const { return pause_sign_; }
    std::shared_ptr<Container> sprite_container() const { return sprite_container_; }

protected:
    friend class WindowSkinPart;
    BitmapPtr windowskin_;
    bool back_baked_ = false;
    bool frame_baked_ = false;
    double width_ = 0, height_ = 0;
    Rect cursor_rect_;
    int openness_ = 255;
    int padding_ = 18;
    int margin_ = 4;
    int tone_[3] = {0, 0, 0};
    std::shared_ptr<Container> sprite_container_;
    SpritePtr back_sprite_, cursor_sprite_, frame_sprite_, contents_sprite_, down_arrow_, up_arrow_, pause_sign_;
    void refresh_all_parts();
    void refresh_back();
    void refresh_frame();
    void bake_back();
    void bake_frame();
    bool direct_frame_possible() const;
    void draw_back_direct(const RenderState& rs);
    void draw_frame_direct(const RenderState& rs);
    void refresh_cursor();
    void refresh_contents();
    virtual void refresh_arrows();
    virtual void refresh_pause_sign();
    void update_cursor();
    void update_contents();
    void update_arrows();
    virtual void update_pause_sign();
    virtual void update_transform_hook();
};

class WindowLayer : public Container {
public:
    double width = 0, height = 0;
    void move(double nx, double ny, double w, double h) {
        x = nx;
        y = ny;
        width = w;
        height = h;
    }
    void render(const RenderState& rs) override;
};

}
