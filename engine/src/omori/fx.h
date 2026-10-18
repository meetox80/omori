#pragma once
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../gfx2d/display.h"
#include "../js/value.h"

namespace rpg {
class FilterController;
}

namespace ui {
namespace fx {

class Horror {
public:
    Horror();
    ~Horror();
    void sync(g2d::Sprite& sprite, const js::Val* source, bool glitch_special);
    void update(g2d::Sprite& sprite);
    void create_noise(g2d::Sprite& sprite);
    void remove_noise(g2d::Sprite& sprite);
    void set_noise_rate(double value);
    void set_noise_animated(bool value);
    void create_glitch(g2d::Sprite& sprite);
    void remove_glitch(g2d::Sprite& sprite);
    void set_glitch_slices(double value);
    void set_glitch_offset(double value);
    void set_glitch_animated(bool value);
    void set_glitch_frequency(double value);
    void set_glitch_strength(double value);
    void create_tv(g2d::Sprite& sprite);
    void remove_tv(g2d::Sprite& sprite);
    void set_tv_line_thickness(double value);
    void set_tv_corner_size(double value);
    void set_tv_animated(bool value);
    void set_tv_speed(double value);
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

struct TkmTargets {
    g2d::DisplayObject* scene = nullptr;
    g2d::DisplayObject* spriteset = nullptr;
    g2d::DisplayObject* tilemap = nullptr;
    g2d::DisplayObject* base_sprite = nullptr;
    std::vector<g2d::DisplayObject*> tile_layers;
    g2d::DisplayObject* parallax = nullptr;
    std::vector<std::pair<g2d::DisplayObject*, int>> character_sprites;
    g2d::Container* picture_container = nullptr;
};

class TkmFilters {
public:
    TkmFilters();
    ~TkmFilters();
    void update(const TkmTargets& targets, bool battle);
    void remove(rpg::FilterController* controller, const TkmTargets& targets);
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

class ScreenEffects {
public:
    ScreenEffects();
    ~ScreenEffects();
    void create(g2d::Sprite& spriteset);
    void update();
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}
}
