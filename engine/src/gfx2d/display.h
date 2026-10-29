#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../js/value.h"
#include "../platform/gfx.h"
#include "bitmap.h"

namespace g2d {

struct Rect {
    double x = 0, y = 0, width = 0, height = 0;
    Rect() = default;
    Rect(double x_, double y_, double w_, double h_) : x(x_), y(y_), width(w_), height(h_) {}
};

struct Mat {
    float a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    Mat mul(const Mat& o) const {
        Mat r;
        r.a = a * o.a + c * o.b;
        r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d;
        r.d = b * o.c + d * o.d;
        r.tx = a * o.tx + c * o.ty + tx;
        r.ty = b * o.tx + d * o.ty + ty;
        return r;
    }
    void apply(double x, double y, double& ox, double& oy) const {
        float fx = (float)x, fy = (float)y;
        ox = a * fx + c * fy + tx;
        oy = b * fx + d * fy + ty;
    }
    void apply(float x, float y, float& ox, float& oy) const {
        ox = a * x + c * y + tx;
        oy = b * x + d * y + ty;
    }
    bool axis_aligned() const { return std::fabs(b) < 1e-9f && std::fabs(c) < 1e-9f; }
};

class Container;
class DisplayObject;
struct RenderState;

class Filter {
public:
    virtual ~Filter() = default;
    bool enabled = true;
    virtual void apply(DisplayObject& obj, const RenderState& rs, const std::function<void(const RenderState&)>& draw) = 0;
};

using FilterPtr = std::shared_ptr<Filter>;

struct RenderState {
    Mat world;
    double alpha = 1;
    bool clip = false;
    int cx0 = 0, cy0 = 0, cx1 = gfx::SCREEN_W, cy1 = gfx::SCREEN_H;
};

class DisplayObject : public std::enable_shared_from_this<DisplayObject> {
public:
    virtual ~DisplayObject();
    double x = 0, y = 0;
    double scale_x = 1, scale_y = 1;
    double rotation = 0;
    double pivot_x = 0, pivot_y = 0;
    double skew_x = 0, skew_y = 0;
    double alpha = 1;
    bool visible = true;
    bool renderable = true;
    double z = 0;
    double priority = 0;
    int sprite_id = 0;
    Container* parent = nullptr;
    std::shared_ptr<Rect> mask_rect;
    js::Expando expando;
    std::string tag;
    std::vector<FilterPtr> filters;

    DisplayObject();
    Mat local_matrix() const;
    Mat world_matrix() const;
    virtual void update() {}
    virtual void render(const RenderState& rs) {}
    virtual bool is_container() const { return false; }
    void set_opacity(double v) { alpha = std::clamp<double>(v, 0, 255) / 255.0; }
    double opacity() const { return alpha * 255.0; }
    void remove_from_parent();
};

using DisplayPtr = std::shared_ptr<DisplayObject>;

struct Iteration;

class Container : public DisplayObject {
public:
    ~Container() override;
    std::vector<DisplayPtr> children;
    double layout_w = 640, layout_h = 480;
    bool sort_children = false;
    bool update_children_in_update = true;
    bool is_container() const override { return true; }
    DisplayPtr add_child(DisplayPtr c);
    DisplayPtr add_child_at(DisplayPtr c, size_t index);
    void remove_child(DisplayObject* c);
    DisplayPtr remove_child_at(size_t index);
    void remove_children();
    long index_of(DisplayObject* c) const;
    void set_child_index(DisplayObject* c, size_t index);
    void update() override;
    void render(const RenderState& rs) override;
    void render_children(const RenderState& rs, bool (*pick)(const DisplayObject&) = nullptr);
    std::function<bool(const DisplayPtr&, const DisplayPtr&)> comparator;

private:
    friend struct Iteration;
    Iteration* iteration_ = nullptr;
    void retire(const DisplayPtr& c);
};

class Sprite : public Container {
public:
    Sprite();
    explicit Sprite(BitmapPtr b);
    double anchor_x = 0, anchor_y = 0;
    int blend_mode = 0;
    bool is_picture = false;

    BitmapPtr bitmap() const { return bitmap_; }
    void set_bitmap(BitmapPtr b);
    void set_frame(double x, double y, double w, double h);
    const Rect& frame() const { return frame_; }
    const Rect& real_frame() const { return real_frame_; }
    double width() const { return frame_.width; }
    double height() const { return frame_.height; }
    void set_width(double w);
    void set_height(double h);
    void move(double nx, double ny) {
        x = nx;
        y = ny;
    }
    void show() {
        visible = true;
        hidden_ = false;
    }
    void hide() {
        visible = false;
        hidden_ = true;
    }
    bool hidden() const { return hidden_; }
    void set_blend_color(const double c[4]);
    void set_color_tone(const double t[4]);
    const double* blend_color() const { return blend_color_; }
    const double* color_tone() const { return color_tone_; }
    void refresh();
    void render(const RenderState& rs) override;
    void render_self(const RenderState& rs);
    bool has_tint() const;

protected:
    BitmapPtr bitmap_;
    Rect frame_;
    Rect real_frame_;
    bool refresh_frame_ = false;
    bool hidden_ = false;
    double blend_color_[4] = {0, 0, 0, 0};
    double color_tone_[4] = {0, 0, 0, 0};
    BitmapPtr tinted_;
    int tint_version_ = -1;
    bool tint_dirty_ = true;
    void build_tint();
};

using SpritePtr = std::shared_ptr<Sprite>;

class TilingSprite : public Container {
public:
    explicit TilingSprite(BitmapPtr b = nullptr);
    BitmapPtr bitmap;
    double origin_x = 0, origin_y = 0;
    double width = 0, height = 0;
    int blend_mode = 0;
    Rect frame;
    bool frame_set = false;
    void move(double nx, double ny, double w, double h) {
        x = nx;
        y = ny;
        width = w;
        height = h;
    }
    void set_frame(double fx, double fy, double fw, double fh) {
        frame = Rect(fx, fy, fw, fh);
        frame_set = true;
    }
    void render(const RenderState& rs) override;
};

class ScreenSprite : public Container {
public:
    ScreenSprite() { alpha = 0; }
    int red = 0, green = 0, blue = 0;
    int blend_mode = 0;
    void set_black() { set_color(0, 0, 0); }
    void set_white() { set_color(255, 255, 255); }
    void set_color(int r, int g, int b) {
        red = r;
        green = g;
        blue = b;
    }
    void render(const RenderState& rs) override;
};

void render_object(DisplayObject& obj, const RenderState& rs);
int take_render_count();
void screen_point(const Mat& m, double lx, double ly, float& sx, float& sy);
void draw_image(Image* img, double sx, double sy, double sw, double sh, const Mat& m, double alpha, int blend, bool smooth, const RenderState& rs);
void draw_flat(Image* img, double sx, double sy, double sw, double sh, const Mat& m, uint32_t rgb, double alpha, int blend, bool smooth,
               const RenderState& rs);
void draw_fill(double x, double y, double w, double h, const Mat& m, int r, int g, int b, double alpha, int blend, const RenderState& rs);
gfx::Blend blend_of(int mode);
int next_sprite_id();

}
