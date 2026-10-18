#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../rpg/config.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "../rpg/save.h"
#include "../rpg/system.h"
#include "boot.h"
#include "scenes.h"
#include "sprites.h"
#include "ui_util.h"

namespace ui {

void setup_new_game() {
    rpg::create_game_objects();
    rpg::game_switches().set_value(5008, true);
    rpg::save::select_for_new_game();
    rpg::game_party().setup_starting_members();
    const Value& sys = data::system();
    rpg::game_player().reserve_transfer(sys["startMapId"].as_int(), sys["startX"].num(), sys["startY"].num(), 0, 2);
    rpg::set_frame_count(0);
    rpg::playtime_manager::clear();
}

namespace {

const char* const kSplashPlugin = "GALV_AnimatedSplashScreens";

bool g_splashed = false;

struct SplashImage {
    std::string image;
    double timer = 0;
    double fade = 0;
    double anim = 0;
};

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t from = 0;
    while (true) {
        size_t at = s.find(sep, from);
        if (at == std::string::npos) {
            out.push_back(s.substr(from));
            break;
        }
        out.push_back(s.substr(from, at - from));
        from = at + 1;
    }
    return out;
}

const std::vector<SplashImage>& splash_images() {
    static std::vector<SplashImage> list;
    static bool built = false;
    if (built) return list;
    built = true;
    for (const std::string& entry : split(data::plugin_param(kSplashPlugin, "Splash Images"), '|')) {
        std::vector<std::string> parts = split(entry, ',');
        SplashImage img;
        img.image = parts[0];
        img.timer = parts.size() > 1 ? rpg::js_number(parts[1]) : std::nan("");
        img.fade = parts.size() > 2 ? rpg::js_number(parts[2]) : std::nan("");
        img.anim = parts.size() > 3 ? rpg::js_number(parts[3]) : std::nan("");
        list.push_back(img);
    }
    return list;
}

std::string splash_background() { return data::plugin_param(kSplashPlugin, "Splash Background"); }

std::string splash_skip() { return rpg::to_lower(data::plugin_param(kSplashPlugin, "Splash Skip")); }

void galv_goto(rpg::SceneFactory next) {
    if (!g_splashed) {
        if (rpg::save::exists(rpg::save::kCrashSlot)) {
            g_splashed = true;
            rpg::scene_manager::goto_scene(std::move(next));
            return;
        }
        next = splash_scene();
    }
    rpg::scene_manager::goto_scene(std::move(next));
}

class SceneBoot : public rpg::Scene {
public:
    const char* name() const override { return "Scene_Boot"; }

    void create() override {
        rpg::Scene::create();
        rpg::config_load();
    }

    void start() override {
        rpg::Scene::start();
        if (data::system()["startMapId"].as_int() == 0) {
            rpg::script_error("Scene_Boot", "Player's starting position is not set");
            rpg::scene_manager::exit_game();
            return;
        }
        setup_new_game();
        galv_goto(title_scene());
        if (rpg::save::exists(rpg::save::kCrashSlot)) {
            if (rpg::save::load_game(rpg::save::kCrashSlot)) {
                fade_out_all();
                if (rpg::game_system().version_id != data::system()["versionId"].as_int()) {
                    rpg::GamePlayer& player = rpg::game_player();
                    player.reserve_transfer(rpg::game_map().map_id_, player.x_, player.y_, 0, 2);
                    player.request_map_reload();
                }
                double frames = rpg::game_system().frames_on_save;
                if (frames != 0 && !std::isnan(frames)) rpg::set_frame_count((uint64_t)frames);
                galv_goto(map_scene());
                rpg::save::remove(rpg::save::kCrashSlot);
            }
        }
    }

    void terminate() override {
        rpg::Scene::terminate();
        remove_children();
    }
};

class SpriteSplashImage : public SpriteBase {
public:
    explicit SpriteSplashImage(int index) : index_(index) {
        set_bitmap(load_system(splash_images()[(size_t)index].image));
        set_opacity(0);
    }

    void do_anim() { anim_id_ = splash_images()[(size_t)index_].anim; }

    void update() override {
        SpriteBase::update();
        update_animation();
    }

    void update_animation() {
        setup_animation();
        if (!is_animation_playing()) animation_playing_ = false;
    }

    void setup_animation() {
        if (anim_id_ > 0) {
            const Value& animation = data::animations()[(size_t)anim_id_];
            start_animation(&animation, false, 0);
            anim_id_ = 0;
            animation_playing_ = true;
        }
    }

private:
    int index_ = 0;
    double anim_id_ = 0;
    bool animation_playing_ = false;
};

class SceneSplashScreens : public rpg::Scene {
public:
    const char* name() const override { return "Scene_SplashScreens"; }

    void create() override {
        rpg::Scene::create();
        set_vars();
        create_background();
        create_splashes();
    }

    void start() override {
        rpg::Scene::start();
        rpg::scene_manager::clear_stack();
        center_sprite(*back_sprite_);
        start_fade_in(fade_speed(), false);
    }

    void update() override {
        rpg::Scene::update();
        update_splash();
    }

    void terminate() override {
        rpg::Scene::terminate();
        remove_children();
    }

private:
    int splash_index_ = 0;
    double ticker_ = 0;
    bool fade_in_ = true;
    std::shared_ptr<g2d::Sprite> back_sprite_;
    std::vector<std::shared_ptr<SpriteSplashImage>> sprites_;

    void set_vars() {
        splash_index_ = 0;
        ticker_ = 0;
        fade_in_ = true;
    }

    void create_background() {
        std::string bg = splash_background();
        if (!bg.empty() && bg[0] == '#') {
            back_sprite_ = std::make_shared<g2d::Sprite>();
            g2d::BitmapPtr bitmap = g2d::Bitmap::create(640, 480);
            bitmap->fill_rect(0, 0, 640, 480, bg);
            back_sprite_->set_bitmap(bitmap);
        } else {
            back_sprite_ = std::make_shared<g2d::Sprite>(load_system(bg));
            center_sprite(*back_sprite_);
        }
        add_child(back_sprite_);
    }

    void create_splashes() {
        sprites_.clear();
        const std::vector<SplashImage>& imgs = splash_images();
        for (size_t i = 0; i < imgs.size(); i++) {
            auto sprite = std::make_shared<SpriteSplashImage>((int)i);
            center_sprite(*sprite);
            sprites_.push_back(sprite);
            add_child(sprite);
        }
    }

    static void center_sprite(g2d::Sprite& sprite) {
        sprite.x = 640 / 2.0;
        sprite.y = 480 / 2.0;
        sprite.anchor_x = 0.5;
        sprite.anchor_y = 0.5;
    }

    SpriteSplashImage* sprite() { return (size_t)splash_index_ < sprites_.size() ? sprites_[(size_t)splash_index_].get() : nullptr; }

    const SplashImage& splash() { return splash_images()[(size_t)splash_index_]; }

    bool has_sprite(int index) { return index >= 0 && (size_t)index < sprites_.size() && sprites_[(size_t)index]; }

    void update_splash() {
        if (fade_in_) {
            sprite()->set_opacity(sprite()->opacity() + splash().fade);
            if (sprite()->opacity() >= 255) {
                fade_in_ = false;
                sprite()->do_anim();
            }
        } else {
            if (ticker_ < splash().timer) {
                ticker_++;
            } else {
                sprite()->set_opacity(sprite()->opacity() - splash().fade);
                if (sprite()->opacity() <= 0) {
                    if (!has_sprite(splash_index_ + 1)) {
                        end_splashes();
                        return;
                    }
                    splash_index_++;
                    fade_in_ = true;
                    ticker_ = 0;
                }
            }
        }
        if (input::is_triggered(input::OK)) {
            std::string skip = splash_skip();
            if (skip == "one") {
                ticker_ = splash().timer;
                fade_in_ = false;
            } else if (skip == "all") {
                ticker_ = splash().timer;
                fade_in_ = false;
                if ((size_t)(splash_index_ + 1) < sprites_.size()) sprites_[(size_t)splash_index_ + 1] = nullptr;
            }
        }
    }

    void end_splashes() {
        g_splashed = true;
        rpg::scene_manager::goto_scene(title_scene());
    }
};

}

rpg::SceneFactory boot_scene() { return rpg::scene_of<SceneBoot>("Scene_Boot"); }

rpg::SceneFactory splash_scene() { return rpg::scene_of<SceneSplashScreens>("Scene_SplashScreens"); }

static SceneRegistrar g_boot_registrar("Scene_Boot", boot_scene());
static SceneRegistrar g_splash_registrar("Scene_SplashScreens", splash_scene());

}
