#include <algorithm>
#include <cmath>

#include "../js/random.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "map_extras_parts.h"
#include "ui_util.h"

namespace ui {

namespace {

int qte_param(const char* key) {
    std::string v = data::plugin_param("Omori Map QTE", key);
    double n = rpg::js_number(v.empty() ? "1" : v);
    return std::isnan(n) ? 0 : (int)n;
}

int qte_switch_id() { return qte_param("Active QTE Switch ID"); }

int qte_result_variable_id() { return qte_param("QTE Result Variable ID"); }

void finish_qte(double result) {
    rpg::game_switches().set_value(qte_switch_id(), false);
    rpg::game_variables().set_value(qte_result_variable_id(), js::Val::num(result));
}

int random_int(int max) { return (int)std::floor(max * rng::next()); }

}

void WindowOmoriMapQTESmash::initialize() {
    segment_ = 0;
    press_count_ = 0;
    timer_ = 300;
    max_timer_ = 300;
    segment_presses_ = {5};
    alt_state_ = 0;
    alt_delay_ = 0;
    close_delay_ = 30;
    WindowBase::initialize(14, 14, window_width(), window_height());
    create_bar_sprite();
    create_parallax_back();
    deactivate();
    set_openness(0);
}

void WindowOmoriMapQTESmash::update_transform_hook() {
    WindowBase::update_transform_hook();
    if (!smash_bar_sprite_) return;
    double scale = openness() / 255.0;
    smash_bar_sprite_->scale_y = scale;
    smash_bar_sprite_->y = 5 + (height() / 2 * (1 - openness() / 255.0));
    parallax_back_->y = 4 + (height() / 2 * (1 - openness() / 255.0));
    parallax_back_->scale_y = scale;
    time_bar_sprite_->y = (height() - 7) + (height() / 2 * (1 - openness() / 255.0));
    time_bar_sprite_->scale_y = scale;
}

void WindowOmoriMapQTESmash::create_parallax_back() {
    parallax_back_ = std::make_shared<g2d::TilingSprite>();
    parallax_back_->bitmap = load_system("QTE Smash Background");
    parallax_back_->move(5, 5, width() - 10, 32);
    parallax_back_->set_opacity(180);
    add_child_to_back(parallax_back_);
}

void WindowOmoriMapQTESmash::create_bar_sprite() {
    g2d::BitmapPtr bitmap = g2d::Bitmap::create((int)(width() - 8), (int)(height() - 37));
    bitmap->fill_all("rgba(255, 0, 0, 1)");
    smash_bar_sprite_ = std::make_shared<g2d::Sprite>(bitmap);
    smash_bar_sprite_->set_width(0);
    smash_bar_sprite_->set_opacity(180);
    smash_bar_sprite_->x = 5;
    smash_bar_sprite_->y = 5;
    add_child_to_back(smash_bar_sprite_);
    time_bar_sprite_ = std::make_shared<g2d::Sprite>();
    time_bar_sprite_->x = 5;
    time_bar_sprite_->y = height() - 7;
    add_child(time_bar_sprite_);
}

void WindowOmoriMapQTESmash::setup(const js::Val& segments, const js::Val& time, const js::Val& width) {
    segment_ = 0;
    press_count_ = 0;
    timer_ = time.is_undef() ? 10 : time.to_num();
    max_timer_ = timer_;
    segment_presses_.clear();
    if (segments.is_undef()) {
        segment_presses_.push_back(5);
    } else if (segments.is_array()) {
        for (const js::Val& v : segments.o->items) segment_presses_.push_back(v.to_num());
    }
    close_delay_ = 61;
    alt_state_ = 0;
    alt_delay_ = 0;
    const double clear[4] = {0, 0, 0, 0};
    smash_bar_sprite_->set_blend_color(clear);
    refresh();
    update_smash_bar();
    update_time_bar();
}

void WindowOmoriMapQTESmash::refresh() {
    g2d::BitmapPtr bitmap = g2d::Bitmap::create((int)(width() - 10), (int)(height() - 15));
    bitmap->fill_all("rgba(255, 0, 0, 1)");
    smash_bar_sprite_->set_bitmap(bitmap);
    create_contents();
    bitmap = g2d::Bitmap::create((int)(width() - 10), 2);
    bitmap->fill_all("rgba(255, 255, 0, 1)");
    time_bar_sprite_->set_bitmap(bitmap);
    size_t segments = segment_presses_.size();
    segment_width_ = width() / (double)segments;
    c().fill_rect(4, height() - 11, width() - 8, 2, "rgba(0, 0, 0, 1)");
    c().fill_rect(4, height() - 10, width() - 8, 2, "rgba(255, 255, 255, 1)");
    for (size_t i = 1; i < segments; i++) {
        double x = i * segment_width_;
        c().fill_rect(x - 1, 4, 3, height() - 14, "rgba(0, 0, 0, 1)");
        c().fill_rect(x, 4, 1, height() - 14, "rgba(255, 255, 255, 1)");
    }
    x = (640 - width()) / 2;
    y = (480 - height()) / 2;
}

void WindowOmoriMapQTESmash::finish() {
    finish_qte(segment_);
    close();
    deactivate();
}

void WindowOmoriMapQTESmash::update() {
    WindowBase::update();
    if (!is_open() || !active) return;
    double rate = segment_ / std::max((double)segment_presses_.size() - 1, 1.0);
    double speed = std::max(rate * 5, 1.5);
    parallax_back_->origin_x -= speed;
    if (timer_ <= 0) {
        finish();
        return;
    }
    if (timer_ > 0) {
        timer_--;
        update_time_bar();
    }
    if (segment_ < (int)segment_presses_.size()) {
        if (input::is_triggered(input::OK)) {
            press_count_++;
            if (press_count_ >= segment_presses_[(size_t)segment_]) {
                segment_++;
                press_count_ = 0;
                update_smash_bar();
            }
        }
    } else {
        alt_delay_--;
        if (alt_delay_ <= 0) {
            if (alt_state_ == 0) {
                const double white[4] = {255, 255, 255, 255};
                smash_bar_sprite_->set_blend_color(white);
            } else {
                const double clear[4] = {0, 0, 0, 0};
                smash_bar_sprite_->set_blend_color(clear);
            }
            alt_state_ = (alt_state_ + 1) % 2;
            alt_delay_ = 10;
        }
        if (close_delay_ > 0) close_delay_--;
        if (timer_ <= 0 || close_delay_ <= 0) finish();
    }
}

void WindowOmoriMapQTESmash::update_smash_bar() {
    double w = segment_ * segment_width_;
    smash_bar_sprite_->set_width(w - 3);
}

void WindowOmoriMapQTESmash::update_time_bar() {
    double w = time_bar_sprite_->bitmap() ? time_bar_sprite_->bitmap()->width() : 0;
    double rate = timer_ / max_timer_;
    time_bar_sprite_->set_width(rate * w);
}

void WindowOmoriStopTheArrow::initialize() {
    arrow_index_ = 0;
    arrow_delay_ = 10;
    arrow_delay_count_ = 0;
    arrow_direction_ = 0;
    facing_direction_ = "up";
    WindowBase::initialize(14, 200, window_width(), window_height());
    create_background_sprite();
    create_arrow_sprites();
    x = (640 - width()) / 2;
    y = (480 - height()) / 2;
    set_openness(0);
    deactivate();
}

void WindowOmoriStopTheArrow::update_transform_hook() {
    WindowBase::update_transform_hook();
    if (!arrow_container_) return;
    double scale = openness() / 255.0;
    if (facing_direction_ == "up" || facing_direction_ == "down") {
        arrow_container_->scale_x = 1;
        arrow_container_->scale_y = scale;
    } else {
        arrow_container_->scale_x = scale;
        arrow_container_->scale_y = 1;
    }
}

void WindowOmoriStopTheArrow::create_background_sprite() {
    arrow_container_ = std::make_shared<g2d::Container>();
    arrow_container_->pivot_x = 43;
    arrow_container_->pivot_y = 36.5;
    arrow_container_->x = width() / 2;
    arrow_container_->y = height() / 2;
    add_child_to_back(arrow_container_);
    background_sprite_ = std::make_shared<g2d::Sprite>(load_system("QTE Arrow"));
    background_sprite_->set_frame(0, 0, 86, background_sprite_->height());
    arrow_container_->add_child(background_sprite_);
}

void WindowOmoriStopTheArrow::create_arrow_sprites() {
    arrow_sprites_.clear();
    for (int i = 0; i < 3; i++) {
        auto sprite = std::make_shared<g2d::Sprite>(load_system("QTE Arrow"));
        arrow_sprites_.push_back(sprite);
        arrow_container_->add_child(sprite);
    }
    g2d::Sprite* sprite = arrow_sprites_[0].get();
    sprite->set_frame(86, 0, 39, sprite->bitmap()->height());
    sprite->x = 7 - 5;
    sprite->y = 17 - 5;
    sprite = arrow_sprites_[1].get();
    sprite->set_frame(125, 0, 36, sprite->bitmap()->height());
    sprite->x = 35 - 5;
    sprite->y = 12 - 5;
    sprite = arrow_sprites_[2].get();
    sprite->set_frame(163, 0, 36, sprite->bitmap()->height());
    sprite->x = 63 - 5;
    sprite->y = 17 - 5;
    update_arrows_visibility();
}

void WindowOmoriStopTheArrow::setup(const js::Val& direction, const js::Val& delay) {
    std::string dir = direction.is_undef() ? std::string("up") : direction.to_str();
    std::string lower = rpg::to_lower(dir);
    if (lower == "up") {
        arrow_container_->rotation = (0 * 90) * M_PI / 180;
        set_width(96);
        set_height(83);
    } else if (lower == "left") {
        arrow_container_->rotation = (3 * 90) * M_PI / 180;
        set_width(96 - 14);
        set_height(83 + 12);
    } else if (lower == "right") {
        arrow_container_->rotation = (1 * 90) * M_PI / 180;
        set_width(96 - 14);
        set_height(83 + 12);
    } else if (lower == "down") {
        set_width(96);
        set_height(83);
        arrow_container_->rotation = (2 * 90) * M_PI / 180;
    }
    facing_direction_ = dir;
    arrow_index_ = random_int((int)arrow_sprites_.size());
    arrow_delay_ = delay.is_undef() ? 10 : delay.to_num();
    arrow_delay_count_ = 0;
    arrow_direction_ = 0;
    arrow_container_->x = width() / 2;
    arrow_container_->y = height() / 2;
    update_arrows_visibility();
}

void WindowOmoriStopTheArrow::update() {
    WindowBase::update();
    if (active && is_open()) {
        if (input::is_triggered(input::OK)) {
            finish();
            return;
        }
        arrow_delay_count_++;
        if (arrow_delay_count_ >= arrow_delay_) {
            arrow_delay_count_ = 0;
            if (arrow_direction_ == 0) {
                if (arrow_index_ >= (int)arrow_sprites_.size() - 1) {
                    arrow_index_--;
                    play_se("sys_cursor1", 90, 100, 0);
                    arrow_direction_ = 1;
                } else {
                    play_se("SYS_cursor", 90, 100, 0);
                    arrow_index_++;
                }
            } else {
                if (arrow_index_ <= 0) {
                    play_se("sys_cursor1", 90, 100, 0);
                    arrow_index_++;
                    arrow_direction_ = 0;
                } else {
                    play_se("SYS_cursor", 90, 100, 0);
                    arrow_index_--;
                }
            }
            update_arrows_visibility();
        }
    }
}

void WindowOmoriStopTheArrow::update_arrows_visibility() {
    for (size_t i = 0; i < arrow_sprites_.size(); i++) arrow_sprites_[i]->visible = arrow_index_ == (int)i;
}

void WindowOmoriStopTheArrow::finish() {
    finish_qte(arrow_index_);
    close();
    deactivate();
}

void WindowOmoriHitTheMark::initialize() {
    speed_ = 3;
    speed_is_number_ = true;
    direction_ = 0;
    target_index_ = 0;
    target_width_ = 3 + (speed_ * 2);
    target_x_ = 0;
    WindowBase::initialize(14, 0, window_width(), window_height());
    create_background_sprite();
    create_pointer();
    x = (640 - width()) / 2;
    y = (480 - height()) / 2;
    refresh();
    deactivate();
    set_openness(0);
}

void WindowOmoriHitTheMark::update_transform_hook() {
    WindowBase::update_transform_hook();
    if (background_sprite_) background_sprite_->scale_y = openness() / 255.0;
}

void WindowOmoriHitTheMark::create_background_sprite() {
    background_sprite_ = std::make_shared<g2d::Sprite>(load_system("QTE_OMORI_ATLAS"));
    background_sprite_->set_frame(0, 0, 206, 56);
    background_sprite_->x = 5;
    background_sprite_->y = 33;
    background_sprite_->anchor_y = 0.5;
    add_child_to_back(background_sprite_);
}

void WindowOmoriHitTheMark::create_pointer() {
    pointer_ = std::make_shared<g2d::Sprite>(load_system("ACSArrows"));
    pointer_->set_frame(3 * 32, 0, 32, 29);
    pointer_->x = 32;
    pointer_->anchor_x = 0.5;
    pointer_->anchor_y = 0.5;
    pointer_->y = height() + 5;
    pointer_->visible = false;
    add_child(pointer_);
}

void WindowOmoriHitTheMark::setup(const js::Val& speed, int index) {
    speed_is_number_ = speed.is_num();
    speed_ = speed.to_num();
    direction_ = 0;
    target_index_ = index;
    stop_ = false;
    refresh();
}

void WindowOmoriHitTheMark::refresh() {
    c().clear();
    g2d::BitmapPtr bitmap = load_system("QTE_OMORI_ATLAS");
    int index = target_index_;
    double tx = 11 + (index * 17) - index;
    c().paint_opacity = 160;
    if (speed_is_number_ && speed_ == 2) {
        target_width_ = 9;
        tx -= 4;
        c().blt(bitmap.get(), 209, 0, target_width_, bitmap->height(), tx, 0);
    } else if (speed_is_number_ && speed_ == 4) {
        tx -= 7;
        target_width_ = 15;
        c().blt(bitmap.get(), 225, 0, target_width_, bitmap->height(), tx, 0);
    }
    c().paint_opacity = 255;
    target_x_ = tx;
}

void WindowOmoriHitTheMark::finish() {
    double padding = 4;
    double start_x = target_x_ - padding;
    double end_x = target_x_ + target_width_ + padding;
    bool in_area = start_x < pointer_->x && pointer_->x < end_x;
    finish_qte(in_area ? 1 : 0);
    close();
    deactivate();
    pointer_->visible = false;
}

void WindowOmoriHitTheMark::update() {
    WindowBase::update();
    if (stop_ || !active) return;
    pointer_->visible = true;
    if (input::is_triggered(input::OK)) {
        stop_ = true;
        finish();
        return;
    }
    double speed = speed_;
    double left = 11;
    double right = width() - 11;
    if (direction_ == 0) {
        if (pointer_->x <= left) direction_ = 1;
        else pointer_->x = std::max(pointer_->x - speed, left);
    } else {
        if (pointer_->x >= right) direction_ = 0;
        else pointer_->x = std::min(pointer_->x + speed, right);
    }
}

}
