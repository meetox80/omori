#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../rpg/audio_manager.h"
#include "../rpg/battle.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/party.h"
#include "../rpg/system.h"
#include "menu_common.h"
#include "scenes.h"
#include "ui_util.h"
#include "window_selectable.h"

namespace ui {

namespace {

struct GameoverState {
    bool in_battle = false;
    bool final_battle = false;
    double final_battle_phase = 0;
};

GameoverState capture_state() {
    GameoverState s;
    s.in_battle = rpg::party_in_battle();
    s.final_battle = s.in_battle ? rpg::game_troop().troop_id == 891 : false;
    s.final_battle_phase = rpg::game_variables().value(1220).to_num();
    rpg::battle::st().is_final_battle = s.final_battle;
    return s;
}

std::vector<std::string> utf8_chars(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;
        out.push_back(s.substr(i, len));
        i += len;
    }
    return out;
}

int clamp_paint(double v) { return (int)std::clamp(v, 0.0, 255.0); }

class WindowRetryQuestion : public WindowBase {
public:
    int standard_padding() override { return 4; }

    void setup(const std::string& text) {
        c().font_size = 26;
        x = 0;
        y = 380 - 48;
        draw_opacity = 0;
        set_window_alpha(0);
        text_to_draw = text;
        chars_ = utf8_chars(text);
        text_drawn.clear();
        text_index = -1;
        text_delay = 100;
        draw_opacity = 255;
    }

    bool is_text_complete() const { return text_drawn == text_to_draw; }

    void update() override {}

    void update_phase(int anim_phase) {
        if (anim_phase == 2 || anim_phase == 3) {
            if (is_text_complete()) return;
            if (text_delay > 0) {
                text_delay--;
                return;
            }
            c().clear();
            text_index = std::min(text_index + 1, (int)chars_.size());
            if (text_index < (int)chars_.size()) text_drawn += chars_[(size_t)text_index];
            c().paint_opacity = clamp_paint(draw_opacity);
            c().draw_text(text_drawn, 0, -4, c().width(), c().height(), "center");
            rpg::audio_manager::play_se(rpg::AudioObj::from_js(rpg::game_system().get_message_sound()));
            text_delay = 4;
        } else if (anim_phase == 4) {
            c().clear();
            draw_opacity -= 4;
            c().paint_opacity = clamp_paint(draw_opacity);
            c().draw_text(text_drawn, 0, -4, c().width(), c().height(), "center");
        }
    }

    double draw_opacity = 0;
    std::string text_to_draw;
    std::string text_drawn;
    int text_index = -1;
    int text_delay = 100;

private:
    std::vector<std::string> chars_;
};

class WindowRetryChoice : public WindowBase {
public:
    int standard_padding() override { return 4; }

    void update() override {}

    void update_phase(int anim_phase, WindowRetryQuestion* question) {
        if (anim_phase == 2) {
            if (!question->is_text_complete()) return;
            if (draw_opacity < 255) {
                c().clear();
                draw_opacity += 2;
                set_window_alpha(window_opacity() + 2);
                c().paint_opacity = clamp_paint(draw_opacity);
                c().draw_text(text_to_draw, 0, -4, c().width(), c().height(), "center");
            }
        } else if (anim_phase == 4) {
            c().clear();
            draw_opacity -= 4;
            set_window_alpha(window_opacity() - 4);
            c().paint_opacity = clamp_paint(draw_opacity);
            c().draw_text(text_to_draw, 0, -4, c().width(), c().height(), "center");
        }
    }

    double draw_opacity = 0;
    std::string text_to_draw;
};

class SceneGameover : public rpg::Scene {
public:
    explicit SceneGameover(const GameoverState& state) {
        is_final_battle_ = state.final_battle;
        is_in_battle_ = state.in_battle;
        final_battle_phase_ = state.final_battle_phase;
    }

    const char* name() const override { return "Scene_Gameover"; }

    void create() override {
        rpg::Scene::create();
        play_gameover_music();
        create_background();
        create_retry_windows();
    }

    void start() override {
        rpg::Scene::start();
        start_fade_in(slow_fade_speed(), false);
        if (yparam_bool("ScaleGameOver")) rescale_background();
    }

    void update() override {
        rpg::Scene::update();
        update_transition_animation();
        update_text_position();
        update_retry_input();
    }

    void stop() override {
        rpg::Scene::stop();
        fade_out_all();
    }

    void terminate() override {
        rpg::Scene::terminate();
        rpg::audio_manager::stop_all();
        remove_children();
    }

private:
    struct TextMoveSettings {
        int delay = 20;
        int direction = 0;
        bool active = true;
        bool waiter = false;
    } text_mode_settings_;
    struct AnimData {
        int phase = 0;
        int delay = 0;
        bool active = true;
    } anim_data_;
    struct InputData {
        int index = 0;
        int max = 2;
        bool active = false;
    } input_data_;

    bool is_final_battle_ = false;
    bool is_in_battle_ = false;
    double final_battle_phase_ = 0;
    bool text_has_made_delay_ = false;
    double text_base_y_ = 0;

    std::shared_ptr<g2d::Sprite> back_sprite_;
    std::shared_ptr<g2d::Sprite> text_sprite_;
    std::shared_ptr<g2d::Sprite> omori_sprite_;
    std::shared_ptr<WindowRetryQuestion> retry_question_;
    std::vector<std::shared_ptr<WindowRetryChoice>> retry_windows_;
    std::shared_ptr<SpriteWindowCustomCursor> retry_cursor_sprite_;

    bool final_phase_reached() const { return is_final_battle_ && final_battle_phase_ >= 5; }

    void play_gameover_music() {
        rpg::audio_manager::stop_bgm();
        rpg::audio_manager::stop_bgs();
        rpg::audio_manager::play_me(rpg::AudioObj::from(data::system()["gameoverMe"]));
    }

    void create_background() {
        back_sprite_ = std::make_shared<g2d::Sprite>();
        g2d::BitmapPtr back = g2d::Bitmap::create(640, 480);
        back->fill_all("black");
        back_sprite_->set_bitmap(back);
        add_child(back_sprite_);
        g2d::BitmapPtr bitmap = load_system("GameOverText");
        text_sprite_ = std::make_shared<g2d::Sprite>(bitmap);
        text_sprite_->x = (640 - bitmap->width()) / 2.0;
        text_base_y_ = std::ceil((480 - bitmap->height()) / 2.5);
        text_sprite_->y = text_base_y_;
        add_child(text_sprite_);
        bitmap = load_picture("name_input");
        omori_sprite_ = std::make_shared<g2d::Sprite>(bitmap);
        omori_sprite_->x = (640 - bitmap->width()) / 2.0;
        omori_sprite_->y = ((480 - omori_sprite_->width()) / 2) + 74;
        omori_sprite_->set_opacity(0);
        add_child(omori_sprite_);
    }

    void create_retry_windows() {
        retry_question_ = WindowBase::make<WindowRetryQuestion>(0.0, 0.0, 640.0, 32.0);
        const Value& fixes = menu::blue("Yin_Omori_Fixes");
        retry_question_->setup(data::js_string(final_phase_reached() ? fixes["continue"] : fixes["retry"]));
        add_child(retry_question_);
        retry_windows_.clear();
        std::vector<std::string> text = {data::js_string(menu::lang_pinned("XX_SYSTEM", "message_10")["text"]),
                                         data::js_string(menu::lang_pinned("XX_SYSTEM", "message_11")["text"])};
        for (size_t i = 0; i < text.size(); i++) {
            auto win = WindowBase::make<WindowRetryChoice>(0.0, 0.0, 0.0, 0.0);
            double width = win->c().measure_text_width(text[i]) + win->standard_padding() * 4;
            win->initialize(0, 0, width, 32);
            win->c().font_size = 26;
            win->x = std::floor(640 / 2.6) + (double)i * 100;
            win->y = 380;
            win->draw_opacity = 0;
            win->set_window_alpha(0);
            win->text_to_draw = text[i];
            retry_windows_.push_back(win);
            add_child(win);
        }
        input_data_.max = (int)text.size();
        retry_cursor_sprite_ = std::make_shared<SpriteWindowCustomCursor>(-1, "cursor_menu");
        retry_cursor_sprite_->set_opacity(0);
        add_child(retry_cursor_sprite_);
    }

    void rescale_background() {
        g2d::BitmapPtr bitmap = back_sprite_->bitmap();
        double ratio_x = 640.0 / bitmap->width();
        double ratio_y = 480.0 / bitmap->height();
        if (ratio_x > 1.0) back_sprite_->scale_x = ratio_x;
        if (ratio_y > 1.0) back_sprite_->scale_y = ratio_y;
        back_sprite_->x = 640 / 2.0;
        back_sprite_->y = 480 / 2.0;
        back_sprite_->anchor_x = 0.5;
        back_sprite_->anchor_y = 0.5;
    }

    void remove_added_children() {
        remove_child(back_sprite_.get());
        remove_child(text_sprite_.get());
        remove_child(retry_cursor_sprite_.get());
    }

    void base_update_transition_animation() {
        AnimData& anim = anim_data_;
        if (!anim.active) return;
        if (anim.delay > 0) {
            anim.delay--;
            return;
        }
        switch (anim.phase) {
            case 0:
                anim.phase = 1;
                anim.delay = 80;
                if (final_phase_reached()) {
                    play_me("xx_gameover_piano");
                    anim.delay = 500;
                } else if (!is_final_battle_) {
                    play_me("xx_gameover");
                }
                break;
            case 1:
                if (final_phase_reached()) {
                    double speed = 1;
                    if (!text_mode_settings_.waiter) {
                        text_mode_settings_.waiter = true;
                        anim.delay = 60;
                        return;
                    }
                    if (text_sprite_->opacity() > 0) {
                        text_sprite_->set_opacity(std::max(text_sprite_->opacity() - speed, 0.0));
                        back_sprite_->set_opacity(back_sprite_->opacity() - 16);
                    } else {
                        if (!text_has_made_delay_) {
                            anim.delay = 250;
                            text_has_made_delay_ = true;
                            return;
                        }
                        omori_sprite_->set_opacity(omori_sprite_->opacity() + 10);
                    }
                    if (omori_sprite_->opacity() >= 255) anim.phase = 2;
                } else {
                    if (!is_in_battle_) {
                        anim.delay = 400;
                        anim.phase = 7;
                    } else {
                        anim.phase = 2;
                    }
                }
                break;
            case 2:
                retry_windows_[0]->update_phase(anim.phase, retry_question_.get());
                retry_windows_[1]->update_phase(anim.phase, retry_question_.get());
                if (retry_windows_[0]->window_opacity() >= 255) {
                    retry_cursor_sprite_->set_opacity(255);
                    update_retry_input_cursor_position(0);
                    anim.phase = 3;
                    input_data_.active = true;
                }
                break;
            case 4:
                retry_windows_[0]->update_phase(anim.phase, retry_question_.get());
                retry_windows_[1]->update_phase(anim.phase, retry_question_.get());
                retry_cursor_sprite_->set_opacity(retry_cursor_sprite_->opacity() - 4);
                text_sprite_->set_opacity(text_sprite_->opacity() - 4);
                back_sprite_->set_opacity(back_sprite_->opacity() - 4);
                if (final_phase_reached()) omori_sprite_->set_opacity(omori_sprite_->opacity() - 4);
                if (back_sprite_->opacity() <= 0) {
                    anim.phase = 5;
                    remove_added_children();
                    rpg::battle::process_retry();
                }
                break;
            case 7:
                text_sprite_->set_opacity(text_sprite_->opacity() - 2);
                back_sprite_->set_opacity(back_sprite_->opacity() - 2);
                if (back_sprite_->opacity() <= 0) anim.phase = 8;
                break;
            case 8:
                if (fade_duration_ <= 0) {
                    visible = false;
                    if (is_final_battle_) {
                        std::vector<rpg::GameActor*> members = rpg::game_party().members();
                        if (!members.empty()) members[0]->recover_all();
                        remove_added_children();
                        rpg::scene_manager::goto_scene(map_scene());
                        anim.phase = -1;
                    } else {
                        remove_added_children();
                        rpg::scene_manager::goto_scene(title_scene());
                        anim.phase = -1;
                    }
                }
                break;
        }
    }

    void update_transition_animation() {
        base_update_transition_animation();
        switch (anim_data_.phase) {
            case 2:
            case 3:
                retry_question_->update_phase(anim_data_.phase);
                break;
            case 4:
                retry_question_->update_phase(anim_data_.phase);
                break;
        }
    }

    static void play_me(const char* name) {
        rpg::AudioObj a;
        a.valid = true;
        a.name = name;
        a.volume = 100;
        a.pitch = 100;
        a.pan = 0;
        rpg::audio_manager::play_me(a);
    }

    void update_text_position() {
        TextMoveSettings& anim = text_mode_settings_;
        if (!anim.active) return;
        anim.delay--;
        if (anim.delay <= 0) {
            anim.delay = 30;
            double base_y = text_base_y_;
            double spacing = 3;
            if (anim.direction == 0) {
                double bottom_y = base_y + (spacing * 2);
                text_sprite_->y = std::min(text_sprite_->y + spacing, bottom_y);
                if (text_sprite_->y >= bottom_y) anim.direction = 1;
            } else {
                text_sprite_->y = std::max(text_sprite_->y - spacing, base_y);
                if (text_sprite_->y <= base_y) anim.direction = 0;
            }
        }
    }

    void update_retry_input() {
        InputData& in = input_data_;
        if (!in.active) return;
        if (input::is_triggered(input::OK)) {
            in.active = false;
            play_load();
            AnimData& anim = anim_data_;
            if (in.index == 0) {
                if (anim.phase == 3) anim.phase = 4;
            } else {
                anim.phase = 8;
                fade_out_all();
                anim.delay = 70;
            }
            return;
        }
        if (input::is_repeated(input::LEFT)) {
            play_cursor();
            in.index = std::abs((in.index - 1) % in.max);
            update_retry_input_cursor_position(in.index);
            return;
        }
        if (input::is_repeated(input::RIGHT)) {
            play_cursor();
            in.index = (in.index + 1) % in.max;
            update_retry_input_cursor_position(in.index);
            return;
        }
    }

    void update_retry_input_cursor_position(int index) {
        if (index < 0 || (size_t)index >= retry_windows_.size()) return;
        WindowRetryChoice* win = retry_windows_[(size_t)index].get();
        retry_cursor_sprite_->x = win->x - 35;
        retry_cursor_sprite_->y = win->y + (win->height() / 2);
    }
};

}

rpg::SceneFactory gameover_scene() {
    GameoverState state = capture_state();
    return rpg::SceneFactory{"Scene_Gameover", [state]() -> std::unique_ptr<rpg::Scene> { return std::unique_ptr<rpg::Scene>(new SceneGameover(state)); }};
}

static SceneRegistrar g_gameover_registrar("Scene_Gameover", rpg::SceneFactory{"Scene_Gameover", []() -> std::unique_ptr<rpg::Scene> {
                                                                                   return std::unique_ptr<rpg::Scene>(new SceneGameover(capture_state()));
                                                                               }});

}
