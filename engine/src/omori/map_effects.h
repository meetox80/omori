#pragma once
#include <memory>
#include <string>

#include "../gfx2d/display.h"
#include "window_base.h"

namespace rpg {
class GameFootprint;
}

namespace ui {

class WeatherLayer : public g2d::Container {
public:
    WeatherLayer();
    ~WeatherLayer() override;
    std::string type = "none";
    double power = 0;
    double origin_x = 0, origin_y = 0;
    void update() override;
    void render(const g2d::RenderState& rs) override;
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

class MapFogContainer : public g2d::Container {
public:
    MapFogContainer();
    ~MapFogContainer() override;
    void update() override;
    void add_fog(const std::string& name);
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

class FootprintsContainer : public g2d::Container {
public:
    FootprintsContainer();
    ~FootprintsContainer() override;
    void update() override;
    void add_footprint(std::shared_ptr<rpg::GameFootprint> footprint);
    void remove_all();
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

std::shared_ptr<WindowBase> create_map_name_window();
void map_name_window_open(WindowBase* w);
void map_name_window_close(WindowBase* w);

}
