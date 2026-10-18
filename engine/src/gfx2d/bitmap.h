#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/image.h"
#include "color.h"

namespace g2d {

constexpr float S = images::SCALE;

struct Canvas {
    int lw = 0, lh = 0;
    int pw = 0, ph = 0;
    int stride = 0;
    uint32_t* px = nullptr;
    Image img;
    bool dirty = true;
    Canvas(int w, int h);
    ~Canvas();
    void touch();
    uint32_t* row(int y) { return px + (size_t)y * stride; }
};

void release_decoded_sources();

class Bitmap;
using BitmapPtr = std::shared_ptr<Bitmap>;

class Bitmap {
public:
    static BitmapPtr create(int w, int h);
    static BitmapPtr load(const std::string& key);
    static BitmapPtr empty();

    std::string font_face = "GameFont";
    int font_size = 28;
    bool font_italic = false;
    bool font_bold = false;
    std::string text_color = "#ffffff";
    int paint_opacity = 255;
    bool smooth = false;
    std::string outline_color = "rgba(0, 0, 0, 0.5)";
    int outline_width = 4;
    int version = 0;
    bool text_blocked = false;

    int width() const;
    int height() const;
    bool is_ready() const { return true; }
    bool is_error() const { return missing_; }
    const std::string& key() const { return key_; }
    Image* image();
    float fx();
    float fy();

    void resize(int w, int h);
    void clear();
    void clear_rect(double x, double y, double w, double h);
    void fill_all(const std::string& color);
    void fill_rect(double x, double y, double w, double h, const std::string& color);
    void stroke_rect(double x, double y, double w, double h, const std::string& color);
    void gradient_fill_rect(double x, double y, double w, double h, const std::string& c1, const std::string& c2, bool vertical);
    void draw_circle(double x, double y, double r, const std::string& color);
    void blt(Bitmap* src, double sx, double sy, double sw, double sh, double dx, double dy, double dw = -1, double dh = -1);
    void draw_text(const std::string& text, double x, double y, double max_width, double line_height, const std::string& align);
    int measure_text_width(const std::string& text);
    std::string get_pixel(int x, int y);
    int get_alpha_pixel(int x, int y);
    void adjust_tone(int r, int g, int b);
    void rotate_hue(int offset);
    void blur();
    uint32_t pixel_premul(int px, int py);
    void fill_rows(const std::function<void(int y, uint32_t* row, int w)>& fill);
    void write_pixels(const uint32_t* src, int pw, int ph);
    void touch();

    Bitmap() = default;
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;

private:
    std::string key_;
    images::CacheHandle image_handle_;
    int lw_ = 0, lh_ = 0;
    bool missing_ = false;
    std::unique_ptr<Canvas> canvas_;
    void ensure_canvas();
    void fill_physical(int x0, int y0, int x1, int y1, uint32_t premul, bool copy);
};

}
