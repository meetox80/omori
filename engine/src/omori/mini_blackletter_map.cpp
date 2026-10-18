#include <memory>
#include <string>
#include <vector>

#include "../js/random.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "scenes.h"
#include "ui_util.h"
#include "window_base.h"

namespace ui {

namespace {

class WindowOmoBlackLetterMapName : public WindowBase {
public:
    void setup(const std::string& n) {
        name_ = n;
        WindowBase::initialize(0, 0, 300, 38);
        refresh();
    }
    int standard_padding() override { return 4; }
    void refresh() {
        c().clear();
        double tw = text_width(name_);
        set_width(tw + (name_ == "???" ? 24 : padding() * 2));
        c().font_size = 22;
        draw_text(name_, 0, -7, contents_width(), "center");
    }

private:
    std::string name_;
};

struct MapArea {
    const char* name;
    double name_x, name_y;
    double rx, ry, rw, rh;
    double px, py;
    int black_switch_id, name_switch_id, black_switch50_id;
};

class SpriteOmoBlackLetterMap : public g2d::Sprite {
public:
    SpriteOmoBlackLetterMap() {
        create_background_sprite();
        create_overlay_sprites();
    }

    void create_background_sprite() {
        background_sprite_ = std::make_shared<g2d::Sprite>(load_system("blackletter_bg"));
        add_child(background_sprite_);
    }

    void create_overlay_sprites() {
        g2d::BitmapPtr bitmap = g2d::Bitmap::create(640, 480);
        g2d::BitmapPtr bg = load_atlas("blackLetter_map_atlas");
        g2d::BitmapPtr bg50 = load_atlas("blackLetter_map_50_atlas");
        static const MapArea map_data[] = {
            {"PYREFLY FOREST", 80, 195, 0, 0, 193, 139, 111, 103, 23, 30, 900},
            {"PINWHEEL FOREST", 440, 240, 350, 0, 99, 107, 471, 128, 24, 31, 901},
            {"SPROUT MOLE TOWN", 25, 340, 450, 0, 94, 80, 54, 267, 25, 32, 902},
            {"VAST FOREST", 250, 300, 0, 124, 640, 201, -2, 143, 26, 33, 903},
            {"DEEP WELL", 450, 355, 0, 326, 418, 113, 119, 366, 27, 34, 904},
            {"ORANGE OASIS", 20, 55, 545, 0, 122, 102, 31, 85, 28, 35, 905},
            {"OTHERWORLD", 450, 75, 419, 326, 140, 209, 390, 21, 29, 36, 906},
        };
        name_windows_container_ = std::make_shared<g2d::Sprite>();
        rpg::GameSwitches& sw = rpg::game_switches();
        for (const MapArea& d : map_data) {
            rng::next();
            if (sw.value(d.black_switch50_id)) {
                bitmap->blt(bg.get(), d.rx, d.ry, d.rw, d.rh, d.px, d.py);
            } else if (!sw.value(d.black_switch_id)) {
                bitmap->blt(bg50.get(), d.rx, d.ry, d.rw, d.rh, d.px, d.py);
            }
            std::string n = sw.value(d.name_switch_id) ? std::string(d.name) : std::string("???");
            auto win = std::make_shared<WindowOmoBlackLetterMapName>();
            win->setup(n);
            win->x = d.name_x;
            win->y = d.name_y;
            name_windows_.push_back(win);
            name_windows_container_->add_child(win);
        }
        black_overlay_ = std::make_shared<g2d::Sprite>(bitmap);
        add_child(black_overlay_);
        add_child(name_windows_container_);
        create_text_counter_sprite();
    }

    void create_text_counter_sprite() {
        g2d::BitmapPtr bg = load_atlas("blackLetter_map_atlas");
        g2d::BitmapPtr bitmap = g2d::Bitmap::create(200, 40);
        bitmap->blt(bg.get(), 450, 81, 39, 37, 5, 10);
        bitmap->text_color = "#000000";
        bitmap->outline_color = "rgba(255, 255, 255, 1)";
        bitmap->outline_width = 3;
        std::string text = rpg::js_format("%1/%2", {to_js_string(rpg::game_variables().value(19)), "26"});
        bitmap->draw_text(text, 48, 0, 70, 55, "left");
        text_counter_sprite_ = std::make_shared<g2d::Sprite>(bitmap);
        text_counter_sprite_->y = 480 - 50;
        add_child(text_counter_sprite_);
    }

private:
    std::shared_ptr<g2d::Sprite> background_sprite_, name_windows_container_, black_overlay_, text_counter_sprite_;
    std::vector<std::shared_ptr<WindowOmoBlackLetterMapName>> name_windows_;
};

class SceneOmoriBlackLetterMap : public rpg::Scene {
public:
    const char* name() const override { return "Scene_OmoriBlackLetterMap"; }

    void create() override {
        rpg::Scene::create();
        map_sprite_ = std::make_shared<SpriteOmoBlackLetterMap>();
        add_child(map_sprite_);
    }

    void start() override {
        rpg::Scene::start();
        start_fade_in(slow_fade_speed(), false);
    }

    void update() override {
        rpg::Scene::update();
        if (input::is_triggered(input::CANCEL)) {
            if (!is_busy()) pop_scene();
        }
    }

    void terminate() override {
        rpg::Scene::terminate();
        remove_children();
    }

private:
    std::shared_ptr<SpriteOmoBlackLetterMap> map_sprite_;
};

}

rpg::SceneFactory black_letter_map_scene() { return rpg::scene_of<SceneOmoriBlackLetterMap>("Scene_OmoriBlackLetterMap"); }

static SceneRegistrar g_black_letter_map_registrar("Scene_OmoriBlackLetterMap", black_letter_map_scene());

}
