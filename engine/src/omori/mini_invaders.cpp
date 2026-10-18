#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../js/random.h"
#include "../rpg/audio_manager.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/party.h"
#include "../rpg/screen.h"
#include "../rpg/system.h"
#include "scenes.h"
#include "sprites.h"
#include "ui_util.h"
#include "window_base.h"

namespace ui {

namespace {

class SceneSpaceInvader;

bool scene_paused(SceneSpaceInvader* s);

class InvaderShot : public SpriteBase {
public:
    InvaderShot(SceneSpaceInvader* scene, double sx, double sy, double spd, int dir, double ang, g2d::BitmapPtr bmp)
        : scene_(scene), angle(ang), speed(spd), direction(dir) {
        x = sx;
        y = sy;
        set_bitmap(std::move(bmp));
    }
    void update() override {
        SpriteBase::update();
        if (scene_paused(scene_)) return;
        x += direction * speed;
        if (angle != 0) y += angle * (speed / 2);
    }
    SceneSpaceInvader* scene_;
    double angle = 0;
    double speed = 0;
    int direction = 1;
};

using ShotPtr = std::shared_ptr<InvaderShot>;

ShotPtr make_shot(SceneSpaceInvader* s, double x, double y, double speed, int direction, double angle);

class InvaderObject : public SpriteBase {
public:
    InvaderObject(SceneSpaceInvader* scene, const std::string& t, double px = 0, double py = 0, int idx = 0) : scene_(scene), type(t) {
        if (type == "player") setup_player();
        else if (type == "powerUp") setup_power_up();
        else setup_enemy(px, py, idx);
    }

    void setup_player() {
        type = "player";
        index = 0;
        current_hp = 3;
        speed = 3;
        shoot_delay = 0;
        shoot_speed = 20;
        bullet_speed = 6;
        set_bitmap(load_picture("INVADER-Ship"));
        x = 176;
        y = (480 - bitmap()->height()) / 2.0 + 78;
        has_power_up = false;
    }

    void setup_power_up() {
        type = "powerUp";
        index = 0;
        current_hp = 1;
        speed = rng::next() * (2 - 0.4) + 0.4;
        shoot_delay = 0;
        shoot_speed = 20;
        bullet_speed = 0;
        set_bitmap(load_picture("INVADER-PowerUp"));
        x = 480;
        y = 140 + 80;
        has_power_up = false;
    }

    void setup_enemy(double px, double py, int idx) {
        index = idx;
        x = px;
        y = py;
        movement_timing = 0;
        bullet_speed = 1;
        shoot_delay = 0;
        shoot_speed = 35;
        struct Stat {
            const char* name;
            int hp, speed, points, bullet, shoot;
        };
        static const Stat stats[] = {{"Lv1", 1, 60, 5, 1, 60},     {"Lv2", 1, 50, 20, 2, 45},    {"Lv3", 2, 30, 50, 3, 30},
                                     {"Lv4", 2, 40, 75, 2, 45},    {"Lv5", 2, 25, 100, 3, 35},   {"Lv6", 2, 15, 125, 4, 20},
                                     {"Lv7", 2, 50, 125, 4, 50},   {"Lv8", 2, 40, 150, 3, 20},   {"Lv9", 2, 20, 175, 3, 20},
                                     {"Lv10", 2, 10, 250, 3, 20},  {"Special", 1, 10, 500, 5, 20}};
        for (const Stat& s : stats) {
            if (type != s.name) continue;
            hp = s.hp;
            current_hp = hp;
            speed = s.speed;
            points = s.points;
            set_bitmap(load_picture("INVADER-Enemy"));
            bullet_speed = s.bullet;
            shoot_speed = s.shoot;
        }
    }

    void update() override {
        if (scene_paused(scene_)) return;
        if (type == "player") {
            if (input::is_pressed(input::UP) && !input::is_pressed(input::DOWN)) {
                y -= speed;
                if (y < 140) y = 140;
            } else if (input::is_pressed(input::DOWN) && !input::is_pressed(input::UP)) {
                y += speed;
                if (y > 340) y = 340;
            }
            if (has_power_up) {
                power_up_timer--;
                if (power_up_timer <= 0) has_power_up = false;
            }
        } else if (type == "powerUp") {
            x -= speed;
        } else {
            if (movement_timing <= 0) {
                movement_timing = speed;
                if (move_left) {
                    x -= 24;
                    move_left = false;
                } else {
                    if (moving_down) y += 12;
                    else y -= 12;
                }
            }
            movement_timing--;
        }
    }

    ShotPtr shoot(double angle = 0) {
        int direction = type == "player" ? 1 : -1;
        double offset = type == "player" ? 18 : 20;
        return make_shot(scene_, x + 12 * direction, y + offset, bullet_speed, direction, angle);
    }

    SceneSpaceInvader* scene_;
    std::string type;
    bool dead = false;
    bool moving_down = false;
    bool move_left = false;
    bool has_power_up = false;
    int index = 0;
    int hp = 0;
    int current_hp = 0;
    double speed = 0;
    double movement_timing = 0;
    int shoot_delay = 0;
    int shoot_speed = 0;
    double bullet_speed = 0;
    int points = 0;
    int power_up_timer = 0;
};

using ObjectPtr = std::shared_ptr<InvaderObject>;

class InvaderBarricade : public SpriteBase {
public:
    InvaderBarricade(double px, double py) {
        x = px;
        y = py;
        g2d::BitmapPtr b = g2d::Bitmap::create(32, 64);
        b->fill_rect(0, 0, 32, 64, "#5000bb");
        set_bitmap(b);
    }

    void take_damage(double lx, double ly) {
        double cx = std::floor(lx / 2) * 2;
        double cy = std::floor(ly / 2) * 2;
        int shapes[5] = {0, 1, 2, 3, 4};
        int cur = 5;
        while (cur != 0) {
            int r = (int)std::floor(rng::next() * cur);
            cur -= 1;
            int t = shapes[cur];
            shapes[cur] = shapes[r];
            shapes[r] = t;
        }
        g2d::Bitmap& b = *bitmap();
        switch (shapes[0]) {
        case 0:
            b.clear_rect(cx - 2, cy - 2, 4, 4);
            b.clear_rect(cx + 2, cy - 4, 2, 4);
            b.clear_rect(cx + 4, cy, 2, 2);
            b.clear_rect(cx + 2, cy + 2, 2, 2);
            b.clear_rect(cx - 4, cy + 2, 2, 2);
            b.clear_rect(cx - 6, cy, 2, 2);
            b.clear_rect(cx - 4, cy - 4, 2, 2);
            b.clear_rect(cx - 2, cy - 6, 2, 2);
            break;
        case 1:
            b.clear_rect(cx - 3, cy - 2, 4, 4);
            b.clear_rect(cx + 2, cy - 4, 2, 4);
            b.clear_rect(cx + 4, cy, 2, 2);
            b.clear_rect(cx + 1, cy + 2, 4, 5);
            b.clear_rect(cx - 4, cy + 2, 2, 2);
            b.clear_rect(cx - 6, cy, 2, 2);
            b.clear_rect(cx - 4, cy - 4, 2, 2);
            b.clear_rect(cx - 2, cy - 6, 3, 3);
            break;
        case 2:
            b.clear_rect(cx - 4, cy - 3, 2, 2);
            b.clear_rect(cx + 2, cy - 4, 1, 1);
            b.clear_rect(cx + 4, cy, 2, 2);
            b.clear_rect(cx + 2, cy + 2, 2, 2);
            b.clear_rect(cx - 4, cy + 2, 4, 4);
            b.clear_rect(cx - 6, cy, 2, 2);
            b.clear_rect(cx - 4, cy - 4, 2, 2);
            b.clear_rect(cx - 2, cy - 6, 2, 2);
            break;
        case 3:
            b.clear_rect(cx - 2, cy, 5, 5);
            b.clear_rect(cx + 2, cy - 2, 2, 2);
            b.clear_rect(cx + 4, cy, 2, 2);
            b.clear_rect(cx + 2, cy + 2, 2, 2);
            b.clear_rect(cx - 3, cy + 3, 2, 2);
            b.clear_rect(cx - 5, cy + 5, 2, 2);
            b.clear_rect(cx - 4, cy - 4, 3, 3);
            b.clear_rect(cx - 2, cy - 6, 1, 1);
            break;
        case 4:
            b.clear_rect(cx - 2, cy - 2, 2, 2);
            b.clear_rect(cx + 2, cy - 4, 3, 2);
            b.clear_rect(cx + 4, cy, 4, 6);
            b.clear_rect(cx + 2, cy + 2, 1, 1);
            b.clear_rect(cx - 4, cy + 2, 2, 2);
            b.clear_rect(cx - 6, cy, 2, 2);
            b.clear_rect(cx - 4, cy - 4, 2, 2);
            b.clear_rect(cx - 2, cy - 6, 2, 2);
            break;
        }
    }

    bool check_if_damaged(double bx, double by) {
        double lx = bx - x;
        double ly = by - y;
        if (bitmap()->get_alpha_pixel((int)lx, (int)ly) != 0) {
            take_damage(lx, ly);
            return true;
        }
        return false;
    }
};

using BarricadePtr = std::shared_ptr<InvaderBarricade>;

class WindowSpaceInvaderUI : public WindowBase {
public:
    void initialize() {
        WindowBase::initialize((640 - window_width()) / 2, (480 - window_height()) / 2, window_width(), window_height());
        set_window_opacity(0);
        score = 0;
        lives = 2;
        refresh();
    }
    using WindowBase::initialize;
    double window_width() { return 320; }
    double window_height() { return 288; }
    int standard_padding() override { return 0; }

    void refresh() {
        g2d::Bitmap& b = c();
        b.clear();
        g2d::BitmapPtr text = load_picture("INVADER-Text");
        b.blt(text.get(), 0, 0, text->width(), text->height(), 12, 12);
        g2d::BitmapPtr line = load_picture("INVADER-UI-Line");
        b.blt(line.get(), 0, 0, line->width(), line->height(), 6, 32);
        for (int i = 0; i < lives - 1; i++) {
            g2d::BitmapPtr ship = load_picture("INVADER-Ship");
            b.blt(ship.get(), 0, 0, ship->width(), ship->height(), (window_width() - 70) + (i * 32), 6);
        }
        std::string s = std::to_string(score);
        for (size_t i = 0; i < s.size(); i++) get_number(s[i] - '0', 70 + ((double)i * 12), 12);
    }

    void get_number(int n, double dx, double dy) {
        g2d::BitmapPtr bmp = load_picture("INVADER-UI-Numbers");
        double pw = bmp->width() / 10.0;
        double ph = bmp->height();
        double sx = n % 10 * pw;
        double sy = std::floor(n / 10.0) * ph;
        c().blt(bmp.get(), sx, sy, pw, ph, dx, dy);
    }

    int score = 0;
    int lives = 2;
};

struct Hit {
    InvaderBarricade* block = nullptr;
    InvaderObject* object = nullptr;
    bool any() const { return block || object; }
};

void play(const char* name) { play_se(name, 100, 100, 0); }

g2d::SpritePtr picture_sprite(const char* name, double px, double py) {
    auto s = std::make_shared<g2d::Sprite>(load_picture(name));
    s->x = px;
    s->y = py;
    return s;
}

class SceneSpaceInvader : public rpg::Scene {
public:
    SceneSpaceInvader() {
        rpg::AudioObj bgm;
        bgm.valid = true;
        bgm.name = "minigame_space";
        bgm.volume = 100;
        bgm.pitch = 100;
        bgm.pan = 0;
        rpg::audio_manager::play_bgm(bgm);
        player_shot_bitmap_ = g2d::Bitmap::create(7, 2);
        player_shot_bitmap_->fill_rect(0, 0, 7, 2, "#ffffff");
        enemy_shot_bitmap_ = g2d::Bitmap::create(7, 2);
        enemy_shot_bitmap_->fill_rect(0, 0, 7, 2, "#dc3aff");
    }

    const char* name() const override { return "Scene_SpaceInvader"; }

    void create() override {
        rpg::Scene::create();
        create_background();
        start_fade_in(60, false);
        ui_window_ = WindowBase::make<WindowSpaceInvaderUI>();
        add_child(ui_window_);
        ui_window_->visible = false;
        player_ = std::make_shared<InvaderObject>(this, "player");
        add_child(player_);
    }

    void create_background() {
        main_image_ = picture_sprite("INVADER-Background", 160, 96);
        add_child(main_image_);
        parallax_stars_ = std::make_shared<g2d::TilingSprite>(load_picture("INVADER-Parallax"));
        parallax_stars_->move(160, 96, 320, 288);
        add_child(parallax_stars_);
    }

    void remove(const g2d::DisplayPtr& p) {
        if (p) remove_child(p.get());
    }

    ShotPtr add_shot(double sx, double sy, double speed, int direction, double angle) {
        auto shot = std::make_shared<InvaderShot>(this, sx, sy, speed, direction, angle, direction == 1 ? player_shot_bitmap_ : enemy_shot_bitmap_);
        add_child(shot);
        return shot;
    }

    void start_game() {
        title_showing_ = false;
        get_into_position_ = false;
        logo_.reset();
        starting_game_ = false;
        game_paused = false;
        game_in_progress_ = true;
        ui_window_->visible = true;
        setup_enemies(1);
        setup_barricades(3);
        update_ui();
    }

    void setup_barricades(int amount) {
        for (int i = 0; i < amount; i++) {
            double y_pos = 148;
            if (amount == 2) y_pos += 42;
            if (amount == 1) y_pos += 78;
            auto b = std::make_shared<InvaderBarricade>(176 + 48, y_pos + (i * 78));
            add_child(b);
            barricades_.push_back(b);
        }
    }

    void clear_enemies() {
        if (!enemies_.empty()) {
            for (auto& e : enemies_) remove(e);
            enemies_.clear();
        }
    }

    void setup_enemies(int level) {
        clear_enemies();
        remove_result_img();
        int max_enemies = 0, rows = 0;
        double start_y = 240;
        if (level <= 3) {
            max_enemies = 10;
            rows = 2;
        } else if (level <= 6) {
            max_enemies = 15;
            rows = 3;
        } else {
            max_enemies = 20;
            rows = 4;
            start_y = 160;
        }
        std::string type = "Lv" + std::to_string(level);
        for (int i = 0; i < max_enemies; i++) {
            auto e = std::make_shared<InvaderObject>(this, type, 320 + (i % 5 * 32), start_y + (i % rows * 40), i);
            add_child(e);
            enemies_.push_back(e);
        }
    }

    void update_ui() {
        ui_window_->lives = lives_;
        ui_window_->score = score_;
        ui_window_->refresh();
    }

    void add_boundary_sprite() {
        auto key = picture_sprite("INVADER-Boundary", 0, 0);
        if (press_key_is_boundary_) {
            if (!press_key_stack_) press_key_stack_ = press_key_;
            else remove(press_key_);
        }
        press_key_ = key;
        press_key_is_boundary_ = true;
        add_child(key);
    }

    void update_title() {
        if (!title_showing_) return;
        if (input::is_triggered(input::CANCEL)) {
            start_fade_out(30);
            rpg::game_party().gold_ += 1;
            exit_fade_ = 30;
        }
        if (!logo_) {
            player_->visible = true;
            player_->y = (480 - player_->bitmap()->height()) / 2.0 + 78;
            logo_ = picture_sprite("INVADER-Title", 160 + 12, 96 + 64);
            add_child(logo_);
            press_key_ = picture_sprite("INVADER-Prompt", 160 + 12, 96 + 64);
            press_key_is_boundary_ = false;
            add_child(press_key_);
        }
        if (input::is_triggered(input::OK)) {
            if (flicker_ != 0 || get_into_position_) return;
            flicker_ = 40;
            play("mini_csb_start");
        }
        if (flicker_ > 0) {
            if (flicker_ == 45 || flicker_ == 40 || flicker_ == 35 || flicker_ == 30 || flicker_ == 25) press_key_->visible = !press_key_->visible;
            flicker_--;
            if (flicker_ <= 0) {
                remove(press_key_);
                get_into_position_ = true;
                remove(logo_);
            }
        }
        if (get_into_position_) {
            add_boundary_sprite();
            player_->x += 4;
            if (player_->x > (ui_window_->width() + ui_window_->x)) {
                player_->x = ui_window_->x - player_->width();
                starting_game_ = true;
            }
            if (player_->x >= 176 && starting_game_) start_game();
        }
    }

    void fire_player() {
        if (player_->has_power_up) {
            bullets_.push_back(player_->shoot(-1));
            bullets_.push_back(player_->shoot());
            bullets_.push_back(player_->shoot(1));
        } else {
            bullets_.push_back(player_->shoot());
        }
        player_->shoot_delay = player_->shoot_speed;
        play("mini_csb_laser");
    }

    void update_player_shoot() {
        if (input::is_pressed(input::OK) && player_->shoot_delay <= 0) fire_player();
        else if (input::is_triggered(input::OK)) fire_player();
        if (player_->shoot_delay > 0) player_->shoot_delay--;
    }

    void give_power_up() {
        player_->has_power_up = true;
        player_->power_up_timer = 300;
        play("mini_csb_start");
    }

    double ui_left() { return ui_window_->x; }
    double ui_right() { return ui_window_->width() + ui_window_->x; }
    double ui_top() { return ui_window_->y; }
    double ui_bottom() { return ui_window_->height() + ui_window_->y; }

    void update_power_up_movement() {
        if (!power_) {
            int rando = (int)std::floor(rng::next() * 1000);
            if (rando > 996 && !player_->has_power_up && power_spawn_delay_ <= 0) {
                power_ = std::make_shared<InvaderObject>(this, "powerUp");
                power_->y = std::floor(rng::next() * (340 - 140 + 1)) + 140;
                add_child(power_);
                power_spawn_delay_ = power_spawn_speed_;
            }
            return;
        }
        if (power_->x < ui_left() || power_->x > ui_right() || power_->y < ui_top() || power_->y > ui_bottom()) {
            remove(power_);
            power_.reset();
            return;
        }
        InvaderObject& p = *player_;
        InvaderObject& w = *power_;
        if ((w.x > p.x && w.x < (p.x + p.width()) && w.y > p.y && w.y < (p.y + p.height())) ||
            (p.x > w.x && p.x < (w.x + w.width()) && p.y > w.y && p.y < (w.y + w.width()))) {
            give_power_up();
            remove(power_);
            power_.reset();
            update_ui();
            return;
        }
        if (!player_->has_power_up) {
            if (power_spawn_delay_ > 0) power_spawn_delay_--;
        }
    }

    void update_enemy_movement() {
        for (size_t i = 0; i < enemies_.size(); i++) {
            if (!enemies_[i]) continue;
            if (enemies_[i]->dead) continue;
            if (enemies_[i]->y <= 140 && !moving_down_) {
                for (auto& e : enemies_) {
                    e->moving_down = true;
                    e->move_left = true;
                }
                moving_down_ = true;
            } else if (enemies_[i]->y >= 340 && moving_down_) {
                for (auto& e : enemies_) {
                    e->moving_down = false;
                    e->move_left = true;
                }
                moving_down_ = false;
            }
            if (!player_) continue;
            if (player_->dead) break;
            if (enemies_[i]->x < player_->x) {
                player_die();
                update_ui();
            }
            InvaderObject& p = *player_;
            if (player_->dead) break;
            InvaderObject& e = *enemies_[i];
            if (e.x > p.x && e.x < p.x + p.width() && e.y > p.y && e.y < p.y + p.height()) {
                player_die();
                update_ui();
            }
        }
        for (size_t i = 0; i < enemies_.size(); i++) {
            if (enemies_[i]->dead) continue;
            int rando = (int)std::floor(rng::next() * 1000);
            if (rando > 996 && enemies_[i]->shoot_delay <= 0) {
                bullets_.push_back(enemies_[i]->shoot());
                enemies_[i]->shoot_delay = enemies_[i]->shoot_speed;
            }
            if (enemies_[i]->shoot_delay > 0) enemies_[i]->shoot_delay--;
        }
    }

    void show_result(const char* name) {
        result_img_ = picture_sprite(name, 160 + 12, 96 + 64);
        add_child(result_img_);
    }

    void player_die() {
        play("mini_csb_death");
        static const double color[4] = {255, 255, 255, 255};
        rpg::game_screen().start_flash(color, 60);
        for (auto& b : bullets_) remove(b);
        bullets_.clear();
        remove(power_);
        power_.reset();
        player_->visible = false;
        player_->dead = true;
        player_->has_power_up = false;
        game_paused = true;
        lives_--;
        if (lives_ > 0) {
            game_paused = true;
            results_open_ = true;
            result_timer_ = 60;
            show_result("INVADER-Dead");
        } else {
            rpg::audio_manager::stop_bgm();
            clear_enemies();
            for (auto& b : barricades_) remove(b);
            for (auto& b : bullets_) remove(b);
            bullets_.clear();
            barricades_.clear();
            remove(power_);
            power_.reset();
            game_over_ = true;
            game_paused = true;
            results_open_ = true;
            result_timer_ = 60;
            show_result("INVADER-GameOver");
        }
    }

    void erase_bullet(int i) {
        if (i >= 0 && i < (int)bullets_.size()) {
            remove(bullets_[(size_t)i]);
            bullets_.erase(bullets_.begin() + i);
        }
    }

    void update_all_bullets() {
        for (int i = 0; i < (int)bullets_.size(); i++) {
            bullets_[(size_t)i]->update();
            InvaderShot& b = *bullets_[(size_t)i];
            if (b.x < ui_left() || b.x > ui_right() || b.y < ui_top() || b.y > ui_bottom()) erase_bullet(i);
            InvaderShot* cur = i < (int)bullets_.size() ? bullets_[(size_t)i].get() : nullptr;
            Hit hit = check_collide(cur);
            if (!hit.any()) continue;
            if (hit.object) {
                if (hit.object->type == "player") {
                    player_die();
                    erase_bullet(i);
                    i--;
                    update_ui();
                } else {
                    play("mini_csb_enemy_ship_dead");
                    score_ += hit.object->points;
                    hit.object->dead = true;
                    hit.object->visible = false;
                    erase_bullet(i);
                    i--;
                    update_ui();
                }
            } else {
                if (hit.block->check_if_damaged(cur->x, cur->y)) {
                    erase_bullet(i);
                    i--;
                }
            }
            if (!result_img_ && !results_open_) check_stage_complete();
        }
    }

    void update() override {
        rpg::Scene::update();
        parallax_stars_->origin_y += 1;
        if (exit_fade_ > 0) {
            exit_fade_--;
            if (logo_) logo_->set_opacity(logo_->opacity() - 12);
            if (main_image_) main_image_->set_opacity(main_image_->opacity() - 12);
            if (press_key_) press_key_->set_opacity(press_key_->opacity() - 12);
            if (ui_window_) ui_window_->set_window_alpha(ui_window_->opacity_value() - 12);
            if (player_) player_->set_opacity(player_->opacity() - 12);
            if (exit_fade_ <= 0) {
                remove(logo_);
                remove(player_);
                remove(ui_window_);
                remove(main_image_);
                remove(parallax_stars_);
                remove(press_key_);
                pop_scene();
                rpg::game_system().replay_bgm();
            }
            return;
        }
        if (result_timer_ > 0) {
            result_timer_--;
            return;
        }
        update_title();
        if (results_open_) {
            if (input::is_triggered(input::OK)) {
                if (game_over_) {
                    start_fade_out(12);
                    go_to_title();
                } else if (game_complete_) {
                    start_fade_out(12);
                    go_to_title();
                    game_complete_ = false;
                } else if (next_stage_) {
                    remove_result_img();
                    go_to_next_stage();
                } else if (player_->dead) {
                    respawn_player();
                }
            }
            return;
        }
        if (game_paused) return;
        update_player_shoot();
        update_enemy_movement();
        update_power_up_movement();
        update_all_bullets();
    }

    void remove_result_img() {
        if (result_img_) {
            results_open_ = false;
            result_img_->visible = false;
            remove(result_img_);
            result_img_.reset();
        }
    }

    Hit check_collide(InvaderShot* bullet) {
        Hit h;
        if (!bullet) return h;
        for (auto& block : barricades_) {
            if (bullet->x > block->x && bullet->x < block->x + block->width() && bullet->y > block->y && bullet->y < block->y + block->height()) {
                h.block = block.get();
                return h;
            }
        }
        if (bullet->direction == -1) {
            InvaderObject& p = *player_;
            if (bullet->x > p.x && bullet->x < p.x + p.width() && bullet->y > p.y && bullet->y < p.y + p.height()) h.object = &p;
        } else {
            for (auto& ep : enemies_) {
                if (!ep || ep->dead) continue;
                InvaderObject& e = *ep;
                if (bullet->x > e.x && bullet->x < e.x + e.width() && bullet->y > e.y && bullet->y < e.y + e.height()) {
                    h.object = &e;
                    return h;
                }
            }
        }
        return h;
    }

    void check_stage_complete() {
        bool all_dead = false;
        for (auto& e : enemies_) {
            if (e->dead) {
                all_dead = true;
                continue;
            }
            all_dead = false;
            break;
        }
        if (all_dead) {
            play("mini_csb_crisis_averted");
            game_paused = true;
            results_open_ = true;
            result_timer_ = 60;
            show_result("INVADER-Complete");
            next_stage_ = true;
        }
    }

    void go_to_title() {
        clear_enemies();
        for (auto& b : barricades_) remove(b);
        for (auto& b : bullets_) remove(b);
        bullets_.clear();
        barricades_.clear();
        remove(power_);
        power_.reset();
        game_in_progress_ = false;
        game_paused = true;
        rpg::game_variables().set_value(641, js::Val::num(score_));
        score_ = 0;
        lives_ = 3;
        level_ = 1;
        ui_window_->visible = false;
        remove_result_img();
        game_over_ = false;
        moving_down_ = false;
        start_fade_out(30);
        exit_fade_ = 30;
    }

    void go_to_next_stage() { go_to_stage(level_ + 1); }

    void go_to_stage(int stage) {
        clear_enemies();
        for (auto& b : bullets_) remove(b);
        bullets_.clear();
        for (auto& b : barricades_) remove(b);
        remove(power_);
        power_.reset();
        barricades_.clear();
        next_stage_ = false;
        game_in_progress_ = true;
        game_paused = false;
        remove_result_img();
        level_ = stage;
        moving_down_ = false;
        if (level_ > 10) {
            game_paused = true;
            results_open_ = true;
            result_timer_ = 60;
            show_result("INVADER-YouWin");
            game_complete_ = true;
        } else {
            setup_enemies(level_);
            if (level_ <= 3) setup_barricades(3);
            else if (level_ <= 7) setup_barricades(2);
            else setup_barricades(1);
        }
    }

    void respawn_player() {
        game_in_progress_ = true;
        game_paused = false;
        remove_result_img();
        game_over_ = false;
        player_->y = (480 - player_->bitmap()->height()) / 2.0;
        player_->visible = true;
        player_->dead = false;
        moving_down_ = false;
        clear_enemies();
        setup_enemies(level_);
    }

    bool game_paused = true;

private:
    int exit_fade_ = 0;
    bool title_showing_ = true;
    int lives_ = 3;
    std::vector<ObjectPtr> enemies_;
    std::vector<BarricadePtr> barricades_;
    ObjectPtr power_;
    bool moving_down_ = false;
    std::vector<ShotPtr> bullets_;
    int score_ = 0;
    int level_ = 1;
    bool game_in_progress_ = false;
    bool results_open_ = false;
    bool starting_game_ = false;
    int power_spawn_delay_ = 0;
    int power_spawn_speed_ = 20;
    int result_timer_ = 0;
    int flicker_ = 0;
    bool get_into_position_ = false;
    bool game_over_ = false;
    bool game_complete_ = false;
    bool next_stage_ = false;
    bool press_key_is_boundary_ = false;
    g2d::SpritePtr main_image_;
    std::shared_ptr<g2d::TilingSprite> parallax_stars_;
    std::shared_ptr<WindowSpaceInvaderUI> ui_window_;
    ObjectPtr player_;
    g2d::SpritePtr logo_;
    g2d::SpritePtr press_key_;
    g2d::SpritePtr press_key_stack_;
    g2d::SpritePtr result_img_;
    g2d::BitmapPtr player_shot_bitmap_;
    g2d::BitmapPtr enemy_shot_bitmap_;
};

bool scene_paused(SceneSpaceInvader* s) { return s->game_paused; }

ShotPtr make_shot(SceneSpaceInvader* s, double x, double y, double speed, int direction, double angle) {
    return s->add_shot(x, y, speed, direction, angle);
}

}

rpg::SceneFactory space_invader_scene() { return rpg::scene_of<SceneSpaceInvader>("Scene_SpaceInvader"); }

static SceneRegistrar space_invader_registrar("Scene_SpaceInvader", space_invader_scene());

}
