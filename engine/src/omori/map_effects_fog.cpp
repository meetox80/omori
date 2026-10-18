#include "../rpg/perf.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "../js/vm.h"
#include "../rpg/game.h"
#include "../rpg/map.h"
#include "map_effects.h"
#include "ui_util.h"

namespace ui {

namespace {

class SpriteMapFog : public g2d::TilingSprite {
public:
    explicit SpriteMapFog(const std::string& id) : id_(id) {}
    const std::string& id() const { return id_; }

    bool update_fog() {
        std::shared_ptr<rpg::GameMapFog> data = rpg::game_map().get_map_fog(id_);
        if (!data || !data->active) return false;
        if (!bitmap || bitmap_name_ != data->name) {
            bitmap = load_folder("overlays", data->name);
            bitmap->smooth = true;
            move(0, 0, data->width, data->height);
            bitmap_name_ = data->name;
        }
        set_opacity(data->opacity);
        blend_mode = data->blend_mode;
        scale_x = data->scale_x;
        scale_y = data->scale_y;
        visible = data->visible;
        if (!data->visible && data->deactivate_on_invisible) data->active = false;
        double bw = bitmap->width(), bh = bitmap->height();
        if (bw > 0) {
            js::Vm& vm = js::vm();
            double ox = data->origin_x, oy = data->origin_y;
            double mx = data->move_px, my = data->move_py;
            if (data->origin_js.is_obj()) {
                ox = vm.get(data->origin_js, js::atom("x")).to_num();
                oy = vm.get(data->origin_js, js::atom("y")).to_num();
            }
            if (data->move_js.is_obj()) {
                mx = vm.get(data->move_js, js::atom("x")).to_num();
                my = vm.get(data->move_js, js::atom("y")).to_num();
            }
            ox = std::fmod(ox + mx, bw);
            oy = std::fmod(oy + my, bh);
            data->origin_x = ox;
            data->origin_y = oy;
            data->move_px = mx;
            data->move_py = my;
            if (data->origin_js.is_obj()) {
                vm.put(data->origin_js, js::atom("x"), js::Val::num(ox));
                vm.put(data->origin_js, js::atom("y"), js::Val::num(oy));
            }
            double sx = ox, sy = oy;
            if (data->map_bind) {
                rpg::GameMap& map = rpg::game_map();
                sx += map.display_x() * map.tile_width();
                sy += map.display_y() * map.tile_height();
            }
            origin_x = -std::floor(-sx + 0.5);
            origin_y = -std::floor(-sy + 0.5);
        }
        return true;
    }

private:
    std::string id_;
    std::string bitmap_name_;
};

}

struct MapFogContainer::Impl {
    std::vector<std::shared_ptr<SpriteMapFog>> sprites;
};

MapFogContainer::MapFogContainer() : impl_(new Impl()) {
    std::vector<std::pair<std::string, std::shared_ptr<rpg::GameMapFog>>> fogs(rpg::game_map().map_fogs.begin(), rpg::game_map().map_fogs.end());
    std::stable_sort(fogs.begin(), fogs.end(), [](const auto& a, const auto& b) {
        double pa = a.second ? a.second->priority : 0, pb = b.second ? b.second->priority : 0;
        return pa < pb;
    });
    for (auto& kv : fogs) add_fog(kv.first);
}

MapFogContainer::~MapFogContainer() = default;

void MapFogContainer::add_fog(const std::string& name) {
    for (auto& s : impl_->sprites) {
        if (s->id() == name) return;
    }
    if (!rpg::game_map().get_map_fog(name)) return;
    auto sprite = std::make_shared<SpriteMapFog>(name);
    if (!sprite->update_fog()) return;
    impl_->sprites.push_back(sprite);
    add_child(sprite);
}

void MapFogContainer::update() {
    rpg::perf::Scope ps(rpg::perf::Fog);
    auto& sprites = impl_->sprites;
    for (size_t i = 0; i < sprites.size(); i++) {
        std::shared_ptr<SpriteMapFog> sprite = sprites[i];
        sprite->g2d::TilingSprite::update();
        if (!sprite->update_fog()) {
            sprites.erase(sprites.begin() + (long)i);
            remove_child(sprite.get());
        }
    }
}

}
