#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../js/random.h"
#include "../rpg/audio_manager.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/system.h"
#include "scenes.h"
#include "sprites.h"
#include "ui_util.h"
#include "window_selectable.h"

namespace ui {

namespace {

void play_rock_se(const char* name) { play_se(name, 100, 100, 0); }

void play_rock_bgm(const std::string& name) {
    rpg::AudioObj bgm;
    bgm.name = name;
    bgm.volume = 100;
    bgm.pitch = 100;
    bgm.pan = 0;
    bgm.valid = true;
    rpg::audio_manager::play_bgm(bgm);
}

class Rock : public SpriteBase {
public:
    explicit Rock(const std::string& n) {
        name = "ROCK-" + n;
        set_bitmap(load_picture("ROCK-" + n));
        hp = 6;
        frequency = {33, 33, 33};
        options = {"rock", "paper", "scissors"};
        set_sprite(false);
        setup_rock(n);
    }

    void setup_rock(const std::string& n) {
        if (n == "Jash") {
            hp = 3;
            frequency = {0, 0, 0};
        } else if (n == "TomatoGirl") {
            hp = 3;
            frequency = {60, 40, 0};
        } else if (n == "RoseLad") {
            hp = 3;
            frequency = {0, 60, 40};
        } else if (n == "TheMuncherKing") {
            hp = 3;
            frequency = {40, 0, 60};
        } else if (n == "PapaChip") {
            hp = 3;
            frequency = {50, 25, 25};
        } else if (n == "Bunny") {
            hp = 3;
            frequency = {25, 50, 25};
        } else if (n == "SaucyK") {
            hp = 6;
            frequency = {0, 0, 100};
        } else if (n == "Pluto") {
            hp = 6;
            frequency = {100, 0, 0};
        } else if (n == "LilG") {
            hp = 4;
            frequency = {33, 33, 33};
        } else if (n == "P'terry") {
            hp = 5;
            frequency = {25, 25, 50};
        } else if (n == "CoolSNake") {
            hp = 5;
            frequency = {25, 50, 25};
        } else if (n == "Ocotaco") {
            hp = 5;
            frequency = {50, 25, 25};
        } else if (n == "OneWingAsh") {
            hp = 9;
            frequency = {33, 33, 33};
        }
    }

    void set_sprite(bool attacking) {
        double pw = 128, ph = 128;
        if (attacking) set_frame(1 * pw, 0, pw, ph);
        else set_frame(0 * pw, 0, pw, ph);
    }

    void flash() { flashing = 40; }

    void update() override {
        if (flashing > 0) {
            flashing--;
            if (flashing % 5 == 0) visible = !visible;
            if (flashing <= 0) visible = true;
        }
    }

    std::string name;
    int hp = 6;
    std::vector<int> frequency;
    std::vector<std::string> options;
    int flashing = 0;
    int current_combo = 1;
};

class RockResult : public SpriteBase {
public:
    RockResult(double px, double py) {
        visible = false;
        set_bitmap(load_picture("ROCK-Animation"));
        x = px;
        y = py;
    }

    void set_sprite(const std::string& result, int id) {
        double pw = 64, ph = 64;
        if (result.empty()) return;
        double sx = id * pw;
        double sy = 0;
        if (result == "defend") sy = 0;
        if (result == "hit") sy = 1 * ph;
        if (result == "draw") sy = 2 * ph;
        set_frame(sx, sy, pw, ph);
        visible = true;
    }
};

class ScenePetRocks;
int pet_rocks_option_pressed(ScenePetRocks* s);

class WindowPetRockOptions : public WindowHorzCommand {
public:
    explicit WindowPetRockOptions(ScenePetRocks* scene) : scene_(scene) {}

    void initialize(double x, double y) override {
        WindowHorzCommand::initialize(x, y);
        set_window_alpha(0);
    }

    void make_command_list() override {
        add_command("Rock", "rock", true);
        add_command("Paper", "paper", true);
        add_command("Scissors", "scissors", true);
    }

    double window_width() override { return 330; }
    double window_height() override { return 100; }
    int max_cols() override { return 3; }

    void draw_item(int index) override {
        ItemRect rect = item_rect_for_text(index);
        c().clear_rect(rect.x, rect.y, rect.width + 12, rect.height + 12);
        std::string addon;
        if (pet_rocks_option_pressed(scene_) && current_symbol() == command_symbol(index)) addon = "Pressed";
        else if (current_symbol() == command_symbol(index)) addon = "Selected";
        g2d::BitmapPtr b = load_picture("ROCK-Button" + addon);
        c().blt(b.get(), 0, 0, b->width(), b->height(), rect.x, rect.y);
    }

    void select(int index) override {
        index_ = index;
        stay_count = 0;
        ensure_cursor_visible();
        update_cursor();
        call_update_help();
        refresh();
    }

protected:
    void update_transform_hook() override { cursor_sprite()->visible = false; }

private:
    ScenePetRocks* scene_;
};

class ScenePetRocks : public rpg::Scene {
public:
    const char* name() const override { return "Scene_PetRocks"; }

    void create() override {
        start_fade_in(fade_speed(), false);
        rpg::Scene::create();
        main_image_ = std::make_shared<g2d::Sprite>(load_picture("ROCK-Background"));
        add_child(main_image_);
        start_game();
    }

    void terminate() override {
        rpg::Scene::terminate();
        remove_children();
    }

    void start_game() {
        play_rock_bgm("minigame_jash_title");
        player_ = std::make_shared<Rock>("Jash");
        add_child(player_);
        player_->x = 160;
        player_->y = 176;
        player_->current_combo = 1;
        opponent_ = std::make_shared<Rock>(to_js_string(rpg::game_variables().value(824)));
        add_child(opponent_);
        opponent_->x = 640 - 160 - 128;
        opponent_->y = 176;
        opponent_->current_combo = 1;
        options_ = std::shared_ptr<WindowPetRockOptions>(new WindowPetRockOptions(this));
        options_->initialize(160, 375);
        options_->set_handler("rock", [this] { on_choice("rock"); });
        options_->set_handler("paper", [this] { on_choice("paper"); });
        options_->set_handler("scissors", [this] { on_choice("scissors"); });
        add_child(options_);
        for (int i = 0; i < player_->hp; i++) {
            auto s = std::make_shared<g2d::Sprite>(load_picture("ROCK-Heart"));
            s->x = 174 + (i % 3 * 32);
            s->y = 106;
            player_hearts_.push_back(s);
            add_child(s);
        }
        std::reverse(player_hearts_.begin(), player_hearts_.end());
        for (int i = 0; i < opponent_->hp; i++) {
            auto s = std::make_shared<g2d::Sprite>(load_picture("ROCK-Heart"));
            s->x = 382 + (i % 3 * 32);
            if (i > 5) s->y = 106 + 28 + 28;
            else if (i > 2) s->y = 106 + 28;
            else s->y = 106;
            opponent_hearts_.push_back(s);
            add_child(s);
        }
        std::reverse(opponent_hearts_.begin(), opponent_hearts_.end());
        combo_counter_ = std::make_shared<g2d::Sprite>(load_picture("ROCK-ComboCounter"));
        combo_counter_->x = 320 - 37;
        combo_counter_->y = 92;
        add_child(combo_counter_);
        combo_number_ = std::make_shared<g2d::Sprite>(load_picture("ROCK-Combo1"));
        combo_number_->x = 320;
        combo_number_->y = 114;
        add_child(combo_number_);
    }

    void on_choice(const std::string& choice) {
        if (!on_start_) return;
        play_ok();
        options_->deactivate();
        option_pressed = 20;
        options_->redraw_current_item();
        player_choice_ = choice;
        opponent_choice_ = get_opponent_choice(opponent_.get());
        player_icon_ = std::make_shared<g2d::Sprite>(load_picture("ROCK-OptionsSheet"));
        player_icon_->x = 320 - 41;
        player_icon_->y = 240 - 48;
        opponent_icon_ = std::make_shared<g2d::Sprite>(load_picture("ROCK-OptionsSheet"));
        opponent_icon_->x = 320 + 41;
        opponent_icon_->y = 240;
        sfx_delay_ = 240;
    }

    void display_decision() {
        double sx = 0, ox = 0;
        if (player_choice_ == "rock") {
            sx = 2 * 82;
            if (opponent_choice_ == "rock") {
                rock_result_ = {"draw", "draw"};
                ox = 2 * 82;
            } else if (opponent_choice_ == "paper") {
                rock_result_ = {"hit", "defend"};
                ox = 1 * 82;
            } else {
                rock_result_ = {"defend", "hit"};
                ox = 0 * 82;
            }
        }
        if (player_choice_ == "paper") {
            sx = 1 * 82;
            if (opponent_choice_ == "paper") {
                rock_result_ = {"draw", "draw"};
                ox = 1 * 82;
            } else if (opponent_choice_ == "scissors") {
                rock_result_ = {"hit", "defend"};
                ox = 0 * 82;
            } else {
                rock_result_ = {"defend", "hit"};
                ox = 2 * 82;
            }
        }
        if (player_choice_ == "scissors") {
            sx = 0 * 82;
            if (opponent_choice_ == "scissors") {
                rock_result_ = {"draw", "draw"};
                ox = 0 * 82;
            } else if (opponent_choice_ == "rock") {
                rock_result_ = {"hit", "defend"};
                ox = 2 * 82;
            } else {
                rock_result_ = {"defend", "hit"};
                ox = 1 * 82;
            }
        }
        player_icon_->set_frame(sx, 0, 82, 44);
        add_child(player_icon_);
        opponent_icon_->set_frame(ox, 0, 82, 44);
        add_child(opponent_icon_);
        player_icon_->visible = true;
        opponent_icon_->visible = true;
        results_showing_ = 75;
    }

    void display_results() {
        if (rock_result_[0] == "hit") {
            play_rock_se("mini_coj_hurt");
            opponent_->set_sprite(true);
        } else if (rock_result_[0] == "defend") {
            play_rock_se("mini_coj_hit");
            player_->set_sprite(true);
        } else {
            play_rock_se("mini_coj_draw");
        }
        displaying_animations_ = true;
        timer_ = 15;
        result_animation_ = std::make_shared<RockResult>(160 + 32, 174 + 32);
        add_child(result_animation_);
        result_animation_opp_ = std::make_shared<RockResult>(640 - 160 - 128 + 32, 174 + 32);
        add_child(result_animation_opp_);
    }

    std::string get_opponent_choice(Rock* opponent) {
        int random_num = (int)std::floor(rng::next() * 100);
        int weight = 0;
        for (size_t i = 0; i < opponent->frequency.size(); i++) {
            weight += opponent->frequency[i];
            if (random_num < weight) {
                opponent_choice_ = opponent->options[i];
                return opponent->options[i];
            }
        }
        return std::string();
    }

    void update() override {
        rpg::Scene::update();
        if (title_showing_) {
            if (!title_sprite_) {
                title_sprite_ = std::make_shared<g2d::Sprite>(load_picture("Sequence/ROCK-Intro_01"));
                add_child(title_sprite_);
                title_animation_timer_ = 300;
                title_sprite_id_ = 1;
                options_->deactivate();
            }
            if (title_sprite_) {
                if (title_animation_timer_ % (title_sprite_id_ >= 6 ? 10 : 40) == 0) {
                    title_sprite_id_ += 1;
                    if (title_sprite_id_ > 7) title_sprite_id_ = 6;
                    std::string id = title_sprite_id_ > 9 ? std::to_string(title_sprite_id_) : "0" + std::to_string(title_sprite_id_);
                    title_sprite_->set_bitmap(load_picture("Sequence/ROCK-Intro_" + id));
                }
                title_animation_timer_--;
                if (title_animation_timer_ <= 0) {
                    title_showing_ = false;
                    remove_child(title_sprite_.get());
                    options_->activate();
                    static const char* towns_people[] = {"ROCK-TomatoGirl", "ROCK-RoseLad", "ROCK-TheMuncherKing", "ROCK-PapaChip",
                                                         "ROCK-Bunny",      "ROCK-SaucyK",  "ROCK-Pluto"};
                    static const char* elite[] = {"ROCK-LilG", "ROCK-P'terry", "ROCK-CoolSNake", "ROCK-Ocotaco"};
                    std::string bgm;
                    bool found = false;
                    for (const char* n : towns_people) {
                        if (opponent_->name == n) found = true;
                    }
                    if (found) {
                        bgm = "minigame_jash1";
                    } else {
                        for (const char* n : elite) {
                            if (opponent_->name == n) found = true;
                        }
                        if (found) bgm = "minigame_jash2";
                        else if (opponent_->name == "ROCK-OneWingAsh") bgm = "minigame_jash3";
                    }
                    play_rock_bgm(bgm);
                    on_start_ = true;
                }
            }
            return;
        }
        if (sfx_delay_ > 0) {
            sfx_delay_--;
            if (sfx_delay_ == 180) play_rock_se("mini_coj_windup");
            if (sfx_delay_ == 120) play_rock_se("mini_coj_windup");
            if (sfx_delay_ == 60) play_rock_se("mini_coj_windup");
            if (sfx_delay_ <= 0) display_decision();
        }
        if (results_showing_ > 0) {
            results_showing_--;
            if (results_showing_ <= 0) display_results();
        }
        if (option_pressed > 0) {
            option_pressed--;
            if (option_pressed <= 0) options_->redraw_current_item();
        }
        if (displaying_animations_ || timer_ > 0) {
            timer_--;
            if (timer_ > 10 && timer_ < 15) {
                if (rock_result_[0] == "hit") player_->flash();
                else if (rock_result_[0] == "defend") opponent_->flash();
                result_animation_->set_sprite(rock_result_[0], 0);
                result_animation_opp_->set_sprite(rock_result_[1], 0);
            }
            if (timer_ > 5 && timer_ < 10) {
                result_animation_->set_sprite(rock_result_[0], 1);
                result_animation_opp_->set_sprite(rock_result_[1], 1);
            }
            if (timer_ < 5) {
                result_animation_->set_sprite(rock_result_[0], 2);
                result_animation_opp_->set_sprite(rock_result_[1], 2);
            }
            if (timer_ <= 0) {
                if (rock_result_[0] == "draw") {
                    opponent_->current_combo = 1;
                    player_->current_combo = 1;
                }
                if (rock_result_[0] == "defend") {
                    int damage = 1 * player_->current_combo;
                    remove_hearts(false, damage);
                    opponent_->hp -= damage;
                    opponent_->current_combo = 1;
                    player_->current_combo += 1;
                    if (player_->current_combo > 3) player_->current_combo = 3;
                }
                if (rock_result_[0] == "hit") {
                    int damage = 1 * opponent_->current_combo;
                    remove_hearts(true, damage);
                    player_->hp -= damage;
                    player_->current_combo = 1;
                    opponent_->current_combo += 1;
                    if (opponent_->current_combo > 3) opponent_->current_combo = 3;
                }
                displaying_animations_ = false;
                remove_child(result_animation_.get());
                remove_child(result_animation_opp_.get());
                player_->set_sprite(false);
                opponent_->set_sprite(false);
                remove_child(player_icon_.get());
                remove_child(opponent_icon_.get());
                next_turn_delay_ = 80;
                check_win_conditions();
            }
        }
        if (next_turn_delay_ > 0) {
            next_turn_delay_--;
            if (next_turn_delay_ <= 0) {
                if (game_end_timer_ <= 0) {
                    options_->activate();
                    int combo = player_->current_combo > 0 ? player_->current_combo : 1;
                    combo_number_->set_bitmap(load_picture("ROCK-Combo" + std::to_string(combo)));
                }
            }
        }
        if (game_end_timer_ > 0) {
            game_end_timer_--;
            if (game_end_timer_ == 120) {
                if (to_js_string(rpg::game_variables().value(825)) == "opponent") play_rock_se("mini_coj_lose");
                else play_rock_se("mini_coj_win");
            }
            if (game_end_timer_ <= 0) {
                remove_children();
                pop_scene();
                rpg::audio_manager::stop_bgm();
            }
            return;
        }
    }

    void remove_hearts(bool player, int damage) {
        std::vector<std::shared_ptr<g2d::Sprite>>& hearts = player ? player_hearts_ : opponent_hearts_;
        if (hearts.empty()) return;
        for (int i = 0; i < damage && i < (int)hearts.size(); i++) {
            if (hearts[(size_t)i]->alpha < 1) break;
            hearts[(size_t)i]->alpha -= 0.4;
        }
        for (int i = 0; i < damage; i++) std::rotate(hearts.begin(), hearts.begin() + 1, hearts.end());
    }

    void check_win_conditions() {
        if (player_->hp <= 0) {
            player_->alpha = 0.3;
            rpg::game_variables().set_value(825, js::Val::str("opponent"));
            game_end_timer_ = 180;
            options_->deactivate();
        } else if (opponent_->hp <= 0) {
            opponent_->alpha = 0.3;
            rpg::game_variables().set_value(825, js::Val::str("player"));
            game_end_timer_ = 180;
            options_->deactivate();
        }
    }

    int option_pressed = 0;

private:
    bool title_showing_ = true;
    int game_end_timer_ = 0;
    bool on_start_ = false;
    std::shared_ptr<g2d::Sprite> main_image_, title_sprite_, combo_counter_, combo_number_, player_icon_, opponent_icon_;
    std::shared_ptr<Rock> player_, opponent_;
    std::shared_ptr<WindowPetRockOptions> options_;
    std::vector<std::shared_ptr<g2d::Sprite>> player_hearts_, opponent_hearts_;
    std::shared_ptr<RockResult> result_animation_, result_animation_opp_;
    std::string player_choice_, opponent_choice_;
    std::vector<std::string> rock_result_ = {"", ""};
    int title_animation_timer_ = 0;
    int title_sprite_id_ = 1;
    int sfx_delay_ = 0;
    int results_showing_ = 0;
    bool displaying_animations_ = false;
    int timer_ = 0;
    int next_turn_delay_ = 0;
};

int pet_rocks_option_pressed(ScenePetRocks* s) { return s->option_pressed; }

}

rpg::SceneFactory pet_rocks_scene() { return rpg::scene_of<ScenePetRocks>("Scene_PetRocks"); }

static SceneRegistrar g_pet_rocks_registrar("Scene_PetRocks", pet_rocks_scene());

}
