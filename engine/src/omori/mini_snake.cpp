#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../js/random.h"
#include "../rpg/audio_manager.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/party.h"
#include "../rpg/system.h"
#include "scenes.h"
#include "sprites.h"
#include "ui_util.h"
#include "window_base.h"

namespace ui {

namespace {

class SceneSnake;
class Snake;

bool snake_scene_paused(SceneSnake* s);
bool snake_scene_results_open(SceneSnake* s);
void snake_scene_add_score(SceneSnake* s, int v);
void snake_scene_update_ui(SceneSnake* s);

void play_snake_se(const char* name) { play_se(name, 100, 100, 0); }

class SnakePart : public SpriteBase {
public:
    SnakePart(int id_, double px, double py, Snake* owner, SceneSnake* scene) : id(id_), owner_(owner), scene_(scene) {
        x = px;
        y = py;
        previous_x = px;
        previous_y = py;
        visible = false;
        set_bitmap(load_picture("SNAKE-Body"));
    }
    void update() override {
        SpriteBase::update();
        update_character_frame();
    }
    void update_character_frame();

    int id = 0;
    int direction = 2;
    int previous_dir = 0;
    double previous_x = 0, previous_y = 0;
    Snake* owner_;
    SceneSnake* scene_;
};

using PartPtr = std::shared_ptr<SnakePart>;

class Snake : public SpriteBase {
public:
    Snake(int len, SceneSnake* scene) : length(len), scene_(scene) {
        x = 0;
        y = 0;
        set_width(640);
        set_height(480);
    }

    void setup_player() {
        for (int i = 0; i < length; i++) {
            auto part = std::make_shared<SnakePart>(i, (640.0 - 32) / 2, (480.0 - 32) / 2, this, scene_);
            body.push_back(part);
            add_child(part);
        }
    }

    void update() override {
        SpriteBase::update();
        if (flashing > 0) {
            flashing--;
            if (flashing % 8 == 0) visible = !visible;
            if (flashing <= 0) {
                visible = false;
                set_opacity(0);
            }
            return;
        }
        if (snake_scene_paused(scene_)) return;
        if (speed > 0) {
            if (!dir_switched) {
                if (input::is_pressed(input::UP) && direction != 2) {
                    direction = 8;
                    dir_switched = true;
                } else if (input::is_pressed(input::DOWN) && direction != 8) {
                    direction = 2;
                    dir_switched = true;
                } else if (input::is_pressed(input::LEFT) && direction != 6) {
                    direction = 4;
                    dir_switched = true;
                } else if (input::is_pressed(input::RIGHT) && direction != 4) {
                    direction = 6;
                    dir_switched = true;
                }
            }
        }
        if (movement_timing <= 0) {
            movement_timing = speed;
            for (size_t i = 0; i < body.size(); i++) {
                save_position(body[i].get());
                if (i == 0) {
                    if (direction == 2) {
                        body[i]->direction = direction;
                        body[i]->y += 32;
                    } else if (direction == 8) {
                        body[i]->direction = direction;
                        body[i]->y -= 32;
                    } else if (direction == 4) {
                        body[i]->direction = direction;
                        body[i]->x -= 32;
                    } else if (direction == 6) {
                        body[i]->direction = direction;
                        body[i]->x += 32;
                    }
                    dir_switched = false;
                } else {
                    body[i]->x = body[i - 1]->previous_x;
                    body[i]->y = body[i - 1]->previous_y;
                    body[i]->direction = body[i - 1]->previous_dir;
                }
                body[i]->update_character_frame();
                moves += 1;
                if (moves == 4) fully_expanded = true;
                play_snake_se("mini_sme_move");
            }
        }
        movement_timing--;
    }

    void save_position(SnakePart* p) {
        p->previous_dir = p->direction;
        p->previous_x = p->x;
        p->previous_y = p->y;
    }

    void eat() {
        play_snake_se("mini_sme_eat");
        SnakePart* last = body.back().get();
        auto part = std::make_shared<SnakePart>(last->id + 1, last->x, last->y, this, scene_);
        part->update_character_frame();
        body.push_back(part);
        add_child(part);
        length += 1;
        snake_scene_add_score(scene_, 10);
        if (length % 4 == 0) {
            if (speed >= 20) speed -= 5;
            else if (speed >= 10) speed -= 2;
            else if (speed >= 5) speed -= 1;
            snake_scene_add_score(scene_, 55);
        }
        snake_scene_update_ui(scene_);
    }

    void flash() { flashing = 80; }

    int length = 1;
    int speed = 0;
    int movement_timing = 0;
    std::vector<PartPtr> body;
    bool dead = false;
    int direction = 8;
    bool dir_switched = false;
    bool fully_expanded = false;
    int moves = 0;
    int flashing = 0;
    SceneSnake* scene_;
};

void SnakePart::update_character_frame() {
    if (!owner_) return;
    double pw = 32, ph = 32;
    double sx = 0;
    double sy = ((direction - 2) / 2.0) * ph;
    if (id == 0) sx = 0;
    else if (id == owner_->length - 1) sx = 2 * pw;
    else sx = 1 * pw;
    set_frame(sx, sy, pw, ph);
    if (snake_scene_results_open(scene_)) {
        if (!visible) visible = true;
    }
}

class WindowSnakeUI : public WindowBase {
public:
    void setup() {
        double ww = window_width(), wh = window_height();
        WindowBase::initialize((640 - ww) / 2, (480 - wh) / 2, ww, wh);
        set_window_alpha(0);
        score = 0;
        lives = 2;
        refresh();
    }
    double window_width() { return 320; }
    double window_height() { return 288; }
    int standard_padding() override { return 0; }

    void refresh() {
        c().clear();
        g2d::BitmapPtr text = load_picture("SNAKE-ScoreText");
        c().blt(text.get(), 0, 0, text->width(), text->height(), 22, 6);
        g2d::BitmapPtr line = load_picture("SNAKE-Lines");
        c().blt(line.get(), 0, 0, line->width(), line->height(), 12, 32);
        g2d::BitmapPtr life = load_picture("SNAKE-Life");
        c().blt(life.get(), 0, 0, life->width(), life->height(), 234, 2);
        std::string lv = "0" + number_text(lives);
        for (size_t i = 0; i < lv.size(); i++) get_number(lv[i] - '0', 260 + (double)i * 19, 8);
        std::string sc = number_text(score);
        for (size_t i = 0; i < sc.size(); i++) get_number(sc[i] - '0', 120 + (double)i * 19, 8);
    }

    void get_number(int index, double dx, double dy) {
        g2d::BitmapPtr b = load_picture("SNAKE-Numbers");
        double pw = b->width() / 10.0;
        double ph = b->height();
        if (index < 0 || index > 9) return;
        double sx = (index % 10) * pw;
        double sy = std::floor(index / 10.0) * ph;
        c().blt(b.get(), sx, sy, pw, ph, dx, dy);
    }

    int score = 0;
    int lives = 2;
};

class SceneSnake : public rpg::Scene {
public:
    const char* name() const override { return "Scene_Snake"; }

    void create() override {
        rpg::Scene::create();
        create_background();
        ui_window_ = std::make_shared<WindowSnakeUI>();
        ui_window_->setup();
        add_child(ui_window_);
        ui_window_->visible = false;
    }

    void create_background() {
        main_image_ = std::make_shared<g2d::Sprite>(load_picture("SNAKE-Background"));
        main_image_->x = 160;
        main_image_->y = 96;
        add_child(main_image_);
    }

    void terminate() override {
        rpg::Scene::terminate();
        remove_children();
    }

    void start_game() {
        player_ = std::make_shared<Snake>(1, this);
        add_child(player_);
        player_->setup_player();
        create_food();
        title_showing_ = false;
        logo_.reset();
        remove_sprite(boundary_);
        boundary_.reset();
        remove_sprite(up_snake_);
        remove_sprite(down_snake_);
        remove_sprite(left_snake_);
        remove_sprite(right_snake_);
        starting_game_ = false;
        game_paused = false;
        game_in_progress_ = true;
        ui_window_->visible = true;
        player_->speed = 30;
        update_ui();
    }

    void update_ui() {
        ui_window_->lives = lives_;
        ui_window_->score = score;
        ui_window_->refresh();
    }

    void update() override {
        rpg::Scene::update();
        if (exit_fade_ > 0) {
            exit_fade_--;
            fade_sprite_by(logo_.get());
            fade_sprite_by(main_image_.get());
            fade_sprite_by(press_key_.get());
            if (ui_window_) ui_window_->set_window_alpha(ui_window_->opacity_value() - 12);
            fade_sprite_by(player_.get());
            fade_sprite_by(up_snake_.get());
            fade_sprite_by(down_snake_.get());
            fade_sprite_by(left_snake_.get());
            fade_sprite_by(right_snake_.get());
            if (exit_fade_ <= 0) {
                remove_sprite(logo_);
                remove_sprite(main_image_);
                remove_sprite(press_key_);
                if (result_img_) remove_sprite(result_img_);
                remove_sprite(player_);
                if (ui_window_) remove_child(ui_window_.get());
                remove_sprite(up_snake_);
                remove_sprite(down_snake_);
                remove_sprite(left_snake_);
                remove_sprite(right_snake_);
                remove_sprite(boundary_);
                pop_scene();
                rpg::game_system().replay_bgm();
            }
            return;
        }
        if (result_timer_ > 0) {
            result_timer_--;
            return;
        }
        if (title_showing_) {
            if (input::is_triggered(input::CANCEL)) {
                rpg::game_party().gold_ += 1;
                exit_fade_ = 30;
                return;
            }
            if (!logo_) create_title();
            if (player_ && player_->flashing) return;
            if (right_snakes_) {
                right_snake_->x += 1;
                if (right_snake_->x > 480) {
                    right_snakes_ = false;
                    left_snake_->x = 600 * 2;
                    left_snake_->y = 356;
                    left_snakes_ = true;
                }
            } else if (left_snakes_) {
                left_snake_->x -= 1;
                if (left_snake_->x < 124) {
                    left_snakes_ = false;
                    up_snake_->x = 164;
                    up_snake_->y = 480 * 2;
                    up_snakes_ = true;
                }
            } else if (up_snakes_) {
                up_snake_->y -= 1;
                if (up_snake_->y < -14) {
                    up_snakes_ = false;
                    down_snake_->x = 454;
                    down_snake_->y = 100 - 538 - 4;
                    down_snakes_ = true;
                }
            } else if (down_snakes_) {
                down_snake_->y += 1;
                if (down_snake_->y > 538) {
                    down_snakes_ = false;
                    right_snake_->x = -538;
                    right_snake_->y = 100;
                    right_snakes_ = true;
                }
            }
            if (input::is_triggered(input::OK)) {
                if (flicker_ > 0) return;
                play_snake_se("mini_sme_start");
                flicker_ = 40;
            }
            if (flicker_ > 0) {
                if (flicker_ == 45 || flicker_ == 40 || flicker_ == 35 || flicker_ == 30 || flicker_ == 25) press_key_->visible = !press_key_->visible;
                flicker_--;
                if (flicker_ <= 0) {
                    remove_sprite(press_key_);
                    remove_sprite(logo_);
                    start_game();
                }
            }
        }
        if (results_open) {
            if (input::is_triggered(input::OK)) {
                if (game_over_ || game_complete_) {
                    game_complete_ = false;
                    go_to_title();
                } else if (player_->dead) {
                    respawn_player();
                }
            }
            return;
        }
        if (player_ && check_collide()) {
            player_->eat();
            create_food();
        }
        bool out_of_bounds = player_ && !player_->body.empty() &&
                             (player_->body[0]->x < 192 || player_->body[0]->x > ui_window_->width() + 96 || player_->body[0]->y < 144 ||
                              player_->body[0]->y > ui_window_->height() + 32);
        if (out_of_bounds || (collide_self() && player_->fully_expanded && player_->body.size() > 2)) {
            result_img_ = std::make_shared<g2d::Sprite>(load_picture("SNAKE-Lose"));
            result_img_->x = 227;
            result_img_->y = 160;
            add_child(result_img_);
            play_snake_se("mini_sme_die");
            player_->flash();
            lives_ -= 1;
            update_ui();
            player_->dead = true;
            game_paused = true;
            results_open = true;
            result_timer_ = 120;
        }
        if (game_paused) return;
        check_stage_complete();
    }

    void create_title() {
        logo_ = std::make_shared<g2d::Sprite>(load_picture("SNAKE-Title"));
        logo_->x = 227;
        logo_->y = 96 + 64;
        add_child(logo_);
        press_key_ = std::make_shared<g2d::Sprite>(load_picture("SNAKE-Prompt"));
        press_key_->x = 214;
        press_key_->y = 326;
        add_child(press_key_);
        left_snake_ = std::make_shared<g2d::Sprite>(load_picture("SNAKE-TitleLoop"));
        left_snake_->x = 600 * 2;
        left_snake_->y = 356;
        add_child(left_snake_);
        right_snake_ = std::make_shared<g2d::Sprite>(load_picture("SNAKE-TitleLoop"));
        right_snake_->x = -400;
        right_snake_->y = 100;
        add_child(right_snake_);
        up_snake_ = std::make_shared<g2d::Sprite>(load_picture("SNAKE-TitleVertLoop"));
        up_snake_->x = 164;
        up_snake_->y = 480 * 2;
        add_child(up_snake_);
        down_snake_ = std::make_shared<g2d::Sprite>(load_picture("SNAKE-TitleVertLoop"));
        down_snake_->x = 640 - 160 - 32 - 4;
        down_snake_->y = 100 - 538 - 4;
        add_child(down_snake_);
        boundary_ = std::make_shared<g2d::Sprite>(load_picture("INVADER-Boundary"));
        boundary_->x = 0;
        boundary_->y = 0;
        add_child(boundary_);
        right_snakes_ = true;
        left_snakes_ = false;
        up_snakes_ = false;
        down_snakes_ = false;
    }

    void create_food() {
        if (food_) remove_child(food_.get());
        food_ = std::make_shared<SpriteBase>(load_picture("SNAKE-Food"));
        get_food_pos();
        while (any_touch_food()) get_food_pos();
        add_child(food_);
    }

    void get_food_pos() {
        double min_x = 192;
        double max_x = ui_window_->width() + 64;
        double min_y = 144;
        double max_y = ui_window_->height() + 32;
        food_->x = get_random_int(min_x, max_x);
        food_->y = get_random_int(min_y, max_y);
        if (food_->x > 0) food_->x = std::ceil(food_->x / 32.0) * 32;
        else if (food_->x < 0) food_->x = std::floor(food_->x / 32.0) * 32;
        else food_->x = 32;
        food_->x += 16;
        if (food_->y > 0) food_->y = std::ceil(food_->y / 32.0) * 32;
        else if (food_->y < 0) food_->y = std::floor(food_->y / 32.0) * 32;
        else food_->y = 32;
    }

    double get_random_int(double mn, double mx) {
        mn = std::ceil(mn);
        mx = std::floor(mx);
        return std::floor(rng::next() * (mx - mn + 1)) + mn;
    }

    bool any_touch_food() {
        if (!player_) return false;
        for (auto& part : player_->body) {
            if (food_->x == part->x && food_->y == part->y) return true;
        }
        return false;
    }

    bool check_collide() {
        if (!food_) return false;
        SnakePart* head = player_->body[0].get();
        return head->x == food_->x && head->y == food_->y;
    }

    bool collide_self() {
        if (!player_) return false;
        for (size_t i = 1; i < player_->body.size(); i++) {
            SnakePart* head = player_->body[0].get();
            SnakePart* part = player_->body[i].get();
            if (head->x == part->x && head->y == part->y) return true;
        }
        return false;
    }

    void check_stage_complete() {
        if (player_ && player_->length >= 56) {
            result_img_ = std::make_shared<g2d::Sprite>(load_picture("SNAKE-Win"));
            result_img_->x = 227;
            result_img_->y = 160;
            add_child(result_img_);
            game_paused = true;
            results_open = true;
            result_timer_ = 120;
            game_complete_ = true;
        }
    }

    void go_to_title() {
        rpg::game_variables().set_number(621, score);
        exit_fade_ = 30;
        if (food_) remove_child(food_.get());
        remove_sprite(player_);
        player_->body.clear();
        food_.reset();
        game_in_progress_ = false;
        game_paused = true;
        score = 0;
        lives_ = 3;
        ui_window_->visible = false;
        results_open = false;
        if (result_img_) {
            result_img_->visible = false;
            remove_child(result_img_.get());
            result_img_.reset();
        }
        game_over_ = false;
    }

    void respawn_player() {
        if (lives_ <= 0) {
            go_to_title();
            return;
        }
        game_in_progress_ = true;
        game_paused = false;
        results_open = false;
        if (result_img_) {
            result_img_->visible = false;
            remove_child(result_img_.get());
            result_img_.reset();
        }
        game_over_ = false;
        for (auto& part : player_->body) {
            player_->moves = 0;
            player_->fully_expanded = false;
            part->x = (640.0 - 32) / 2;
            part->y = (480.0 - 32) / 2;
        }
        player_->visible = true;
        player_->set_opacity(255);
        player_->dead = false;
    }

    template <class T>
    void remove_sprite(const std::shared_ptr<T>& s) {
        if (s) remove_child(s.get());
    }

    static void fade_sprite_by(g2d::Sprite* s) {
        if (s) s->set_opacity(s->opacity() - 12);
    }

    bool game_paused = true;
    bool results_open = false;
    int score = 0;

private:
    int exit_fade_ = 0;
    bool title_showing_ = true;
    int lives_ = 3;
    bool game_in_progress_ = false;
    bool starting_game_ = false;
    bool game_over_ = false;
    bool game_complete_ = false;
    int result_timer_ = 0;
    int flicker_ = 0;
    bool right_snakes_ = false, left_snakes_ = false, up_snakes_ = false, down_snakes_ = false;
    std::shared_ptr<WindowSnakeUI> ui_window_;
    std::shared_ptr<g2d::Sprite> main_image_, logo_, press_key_, left_snake_, right_snake_, up_snake_, down_snake_, boundary_, result_img_;
    std::shared_ptr<Snake> player_;
    std::shared_ptr<SpriteBase> food_;
};

bool snake_scene_paused(SceneSnake* s) { return s->game_paused; }
bool snake_scene_results_open(SceneSnake* s) { return s->results_open; }
void snake_scene_add_score(SceneSnake* s, int v) { s->score += v; }
void snake_scene_update_ui(SceneSnake* s) { s->update_ui(); }

}

rpg::SceneFactory snake_scene() { return rpg::scene_of<SceneSnake>("Scene_Snake"); }

static SceneRegistrar g_snake_registrar("Scene_Snake", snake_scene());

}
