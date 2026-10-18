#include "../rpg/perf.h"
#include <cmath>
#include <memory>
#include <vector>

#include "../rpg/game.h"
#include "../rpg/map.h"
#include "map_effects.h"
#include "ui_util.h"

namespace ui {

namespace {

class SpriteFootprint : public g2d::Sprite {
public:
    explicit SpriteFootprint(std::shared_ptr<rpg::GameFootprint> footprint) : footprint_(std::move(footprint)) {
        set_bitmap(load_character(footprint_->graphics_name));
        finished_ = false;
        map_x_ = footprint_->x;
        map_y_ = footprint_->y;
        set_opacity(footprint_->opacity);
        setup_bitmap();
    }

    const std::shared_ptr<rpg::GameFootprint>& footprint() const { return footprint_; }
    bool is_finished() const { return finished_; }

    double screen_x() const {
        rpg::GameMap& map = rpg::game_map();
        return std::floor(map.adjust_x(map_x_) * map.tile_width() + 0.5);
    }

    double screen_y() const {
        rpg::GameMap& map = rpg::game_map();
        return std::floor(map.adjust_y(map_y_) * map.tile_height() + 0.5);
    }

    void update() override {
        g2d::Sprite::update();
        if (footprint_) {
            footprint_->update();
            set_opacity(footprint_->opacity);
            if (footprint_->is_finished()) finished_ = true;
        } else {
            finished_ = true;
        }
    }

private:
    std::shared_ptr<rpg::GameFootprint> footprint_;
    bool finished_ = false;
    double map_x_ = 0, map_y_ = 0;

    void setup_bitmap() {
        const rpg::GameFootprint& f = *footprint_;
        double last_direction = f.last_direction;
        double direction = f.direction;
        double d_index = (direction / 2) - 1;
        double width = bitmap_->width() / (double)(f.columns + 2);
        double height = bitmap_->height() / (double)f.rows;
        double sy = d_index * height;
        double sx = (f.frame % 3) * width;
        if (f.direction != f.last_direction) {
            int index = 0;
            bool skip = false;
            switch (f.last_direction) {
            case 2:
                switch (f.direction) {
                case 4: index = 1; break;
                case 6: index = 0; break;
                case 2:
                case 8: skip = true; break;
                }
                break;
            case 4:
                switch (f.direction) {
                case 2: index = 0; break;
                case 8: index = 1; break;
                case 4:
                case 6: skip = true; break;
                }
                break;
            case 6:
                switch (f.direction) {
                case 2: index = 0; break;
                case 8: index = 1; break;
                case 4:
                case 6: skip = true; break;
                }
                break;
            case 8:
                switch (f.direction) {
                case 4: index = 1; break;
                case 6: index = 0; break;
                case 2:
                case 8: skip = true; break;
                }
                break;
            }
            if (!skip) {
                sx = (f.columns * width) + (index * width);
                sy = ((last_direction / 2) - 1) * height;
            }
        }
        set_frame(sx, sy, width, height);
    }
};

}

struct FootprintsContainer::Impl {
    std::vector<std::shared_ptr<SpriteFootprint>> sprites;
};

FootprintsContainer::FootprintsContainer() : impl_(new Impl()) {
    std::vector<std::shared_ptr<rpg::GameFootprint>> footprints = rpg::game_map().footprints_;
    for (auto& f : footprints) {
        if (f && !f->is_finished()) add_footprint(f);
    }
}

FootprintsContainer::~FootprintsContainer() = default;

void FootprintsContainer::add_footprint(std::shared_ptr<rpg::GameFootprint> footprint) {
    if (!footprint) return;
    auto sprite = std::make_shared<SpriteFootprint>(std::move(footprint));
    impl_->sprites.push_back(sprite);
    add_child(sprite);
}

static void remove_footprint_sprite(FootprintsContainer& self, std::vector<std::shared_ptr<SpriteFootprint>>& sprites, size_t index) {
    std::shared_ptr<SpriteFootprint> sprite = sprites[index];
    rpg::game_map().remove_footprint(sprite->footprint().get());
    sprites.erase(sprites.begin() + (long)index);
    self.remove_child(sprite.get());
}

void FootprintsContainer::update() {
    rpg::perf::Scope ps(rpg::perf::Footprints);
    auto& sprites = impl_->sprites;
    if (sprites.empty()) return;
    for (size_t i = 0; i < sprites.size(); i++) {
        std::shared_ptr<SpriteFootprint> sprite = sprites[i];
        sprite->x = sprite->screen_x();
        sprite->y = sprite->screen_y();
        sprite->update();
        if (sprite->is_finished()) remove_footprint_sprite(*this, sprites, i);
    }
}

void FootprintsContainer::remove_all() {
    auto& sprites = impl_->sprites;
    if (sprites.empty()) return;
    for (size_t i = 0; i < sprites.size(); i++) remove_footprint_sprite(*this, sprites, i);
}

}
