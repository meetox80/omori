#pragma once
#include <array>
#include <cstdint>
#include <map>
#include <memory>

#include "../gfx2d/display.h"
#include "../js/value.h"

namespace ui {

class DisplayJsExtra {
public:
    virtual ~DisplayJsExtra() = default;
    virtual bool display_js_get(uint32_t key, js::Val& out) {
        (void)key, (void)out;
        return false;
    }
    virtual bool display_js_set(uint32_t key, const js::Val& v) {
        (void)key, (void)v;
        return false;
    }
};

js::Val display_js(g2d::DisplayObject* obj);
void release_display_proxies();

class ColorMatrixFilter : public g2d::Filter {
public:
    std::array<double, 20> matrix{};
    ColorMatrixFilter();
    void set_grayscale(double scale);
    void set_matrix(const std::array<double, 20>& m);
    void apply(g2d::DisplayObject& obj, const g2d::RenderState& rs, const std::function<void(const g2d::RenderState&)>& draw) override;

private:
    struct Cached {
        std::weak_ptr<g2d::Bitmap> source;
        int version = -1;
        g2d::BitmapPtr result;
    };
    std::map<g2d::Bitmap*, Cached> cache_;
    g2d::BitmapPtr transformed(const g2d::BitmapPtr& src);
};

g2d::FilterPtr filter_from_js(const js::Val& v);
void set_display_filters_js(g2d::DisplayObject* obj, const js::Val& list);

}
