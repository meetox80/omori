#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fx_core.h"

namespace ui {
namespace fx {

enum class Scope : uint8_t { Screen, Object };

class FxFilter : public g2d::Filter {
public:
    Scope scope = Scope::Object;
    bool full_area = false;
    void apply(g2d::DisplayObject& obj, const g2d::RenderState& rs, const std::function<void(const g2d::RenderState&)>& draw) override;

protected:
    virtual bool identity() const { return false; }
    virtual real padding() const { return 0; }
    virtual void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) = 0;
    void color_pass(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw, const std::function<void()>& passes);
    void warp_pass(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw, core::Mesh& mesh);
};

using FxPtr = std::shared_ptr<FxFilter>;

class GlitchFx : public FxFilter {
public:
    GlitchFx();
    real offset = 100;
    real seed = 0;
    real direction = 0;
    int fill_mode = 0;
    bool average = false;
    real min_size = 8;
    int sample_size = 512;
    real red[2] = {0, 0}, green[2] = {0, 0}, blue[2] = {0, 0};
    void set_slices(real v);
    real slices() const { return slices_value_; }
    void refresh();

protected:
    bool identity() const override;
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    real slices_value_ = 0;
    int count_ = 0;
    std::vector<float> sizes_, offsets_;
    std::array<int16_t, 512> table_{};
    bool any_ = false;
    void randomize_sizes();
    void shuffle();
    void randomize_offsets();
    void redraw();
    struct Band {
        int y0, y1;
        real d;
    };
    void bands(const core::Frame& f, std::vector<Band>& out) const;
};

class NoiseFx : public FxFilter {
public:
    real noise = 0.5;
    real seed = 0;

protected:
    bool identity() const override { return !(noise > 0); }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;
};

class CrtFx : public FxFilter {
public:
    CrtFx();
    ~CrtFx() override;
    real curvature = 1, line_width = 1, line_contrast = 0.25;
    bool vertical_line = false;
    real noise = 0, noise_size = 1;
    real vignetting = 0.3, vignetting_alpha = 1, vignetting_blur = 0.3;
    real seed = 0, time = 0;

protected:
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    gfx::Texture* line_pos_ = nullptr;
    gfx::Texture* line_neg_ = nullptr;
    gfx::Texture* segment_ = nullptr;
    gfx::Texture* segment_v_ = nullptr;
    gfx::Texture* vignette_ = nullptr;
    real built_contrast_ = -1;
    real built_vig_[3] = {-1, -1, -1};
    void passes(const core::Frame& f);
};

class BlurFx : public FxFilter {
public:
    real blur = 8;
    real quality = 4;

protected:
    bool identity() const override { return !(blur != 0); }
    real padding() const override { return std::abs(blur) * 2; }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;
};

class BloomFx : public FxFilter {
public:
    real threshold = 0.5;
    real bloom_scale = 1;
    real brightness = 1;
    real blur = 8;
    real quality = 4;
    real pixel_size = 1;

protected:
    bool identity() const override;
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    void bloom(const core::Box& b, real scale);
};

class ZoomBlurFx : public FxFilter {
public:
    real strength = 0.1;
    real center[2] = {0, 0};
    real inner_radius = 0;
    real radius = -1;

protected:
    bool identity() const override { return !(strength != 0); }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    void taps(const core::Frame& f, gfx::Texture* src, const core::Box& b, bool half);
};

class ShockwaveFx : public FxFilter {
public:
    real center[2] = {0, 0};
    real amplitude = 30, wavelength = 160, brightness = 1, speed = 500, radius = -1, time = 0;

protected:
    bool identity() const override;
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;
};

class DisplacementMap {
public:
    bool load(const std::string& name);
    bool ready() const { return !rg_.empty(); }
    void sample(float lx, float ly, float& r, float& g) const;

private:
    std::string name_;
    int n_ = 0;
    float gx_scale_ = 1, gy_scale_ = 1;
    std::vector<uint8_t> rg_;
};

class DisplacementFx : public FxFilter {
public:
    explicit DisplacementFx(std::shared_ptr<DisplacementMap> map);
    real mask_x = 0, mask_y = 0;
    real scale_x = 20, scale_y = 20;

protected:
    bool identity() const override { return !map_ || !map_->ready(); }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    std::shared_ptr<DisplacementMap> map_;
};

class TwistFx : public FxFilter {
public:
    real offset[2] = {0, 0};
    real radius = 200, angle = 4;

protected:
    bool identity() const override { return !(angle != 0) || !(radius > 0); }
    real padding() const override { return 20; }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;
};

class BulgePinchFx : public FxFilter {
public:
    real center[2] = {0.5, 0.5};
    real radius = 100, strength = 1;

protected:
    bool identity() const override { return !(strength != 0) || !(radius > 0); }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;
};

class RgbSplitFx : public FxFilter {
public:
    real red[2] = {-10, 0}, green[2] = {0, 10}, blue[2] = {0, 0};

protected:
    bool identity() const override;
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;
};

class PixelateFx : public FxFilter {
public:
    real size[2] = {10, 10};

protected:
    bool identity() const override { return size[0] <= 1 && size[1] <= 1; }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    void cells(gfx::Texture* src, const core::Box& b, gfx::Blend blend);
};

class AdjustmentFx : public FxFilter {
public:
    real gamma = 1, saturation = 1, contrast = 1, brightness = 1, red = 1, green = 1, blue = 1, alpha = 1;

protected:
    bool identity() const override;
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    void passes(const core::Box& b);
};

class MotionBlurFx : public FxFilter {
public:
    real velocity[2] = {0, 0};
    int kernel_size = 5;
    real offset = 0;

protected:
    bool identity() const override { return (velocity[0] == 0 && velocity[1] == 0) || kernel_size <= 1; }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    std::vector<core::Tap> tap_list(real scale) const;
};

class RadialBlurFx : public FxFilter {
public:
    real angle = 0;
    real center[2] = {0, 0};
    int kernel_size = 5;
    real radius = -1;

protected:
    bool identity() const override { return angle == 0 || kernel_size <= 1; }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    void taps(const core::Frame& f, gfx::Texture* src, const core::Box& b, bool half);
};

class GodrayFx : public FxFilter {
public:
    GodrayFx();
    ~GodrayFx() override;
    real angle = 30, gain = 0.5, lacunarity = 2.5, strength = 1, time = 0;
    bool parallel = true;
    real center[2] = {0, 0};

protected:
    bool identity() const override { return !(strength > 0); }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    gfx::Texture* strip_ = nullptr;
    uint32_t* strip_px_ = nullptr;
    void mist(const core::Frame& f);
};

class ReflectionFx : public FxFilter {
public:
    bool mirror = true;
    real boundary = 0.5;
    real amplitude[2] = {0, 20}, wave_length[2] = {30, 100}, alpha[2] = {1, 1};
    real time = 0;

protected:
    bool identity() const override { return boundary >= 1; }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;
};

class GlowFx : public FxFilter {
public:
    real distance = 10, outer_strength = 4, inner_strength = 0;
    uint32_t color = 0xFFFFFF;

protected:
    bool identity() const override { return scope == Scope::Screen || (outer_strength <= 0 && inner_strength <= 0); }
    real padding() const override { return 0; }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;
};

class EmbossFx : public FxFilter {
public:
    real strength = 5;

protected:
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;
};

class CrossHatchFx : public FxFilter {
protected:
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;
};

class DotFx : public FxFilter {
public:
    DotFx();
    ~DotFx() override;
    real scale = 1, angle = 5;

protected:
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    gfx::Texture* pos_ = nullptr;
    gfx::Texture* neg_ = nullptr;
};

class AsciiFx : public FxFilter {
public:
    AsciiFx();
    ~AsciiFx() override;
    real size = 8;

protected:
    bool identity() const override { return !(size > 0); }
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    std::array<gfx::Texture*, 8> glyphs_{};
};

class OldFilmFx : public FxFilter {
public:
    OldFilmFx();
    ~OldFilmFx() override;
    real sepia = 0.3, noise = 0.3, noise_size = 1, scratch = 0.5, scratch_density = 0.3, scratch_width = 1;
    real vignetting = 0.3, vignetting_alpha = 1, vignetting_blur = 0.3;
    real seed = 0;

protected:
    void render(const core::Frame& f, const g2d::RenderState& rs, const core::DrawFn& draw) override;

private:
    gfx::Texture* column_ = nullptr;
    uint32_t* column_px_ = nullptr;
    gfx::Texture* vignette_ = nullptr;
    real built_vig_[4] = {-1, -1, -1, -1};
    void passes(const core::Frame& f);
};

gfx::Texture* make_texture(int w, int h, const std::vector<uint32_t>& px);

}
}
