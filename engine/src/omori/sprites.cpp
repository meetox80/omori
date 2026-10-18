#include "sprites.h"

#include <algorithm>
#include <cmath>

#include "../rpg/audio_manager.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/params.h"
#include "ui_util.h"

namespace ui {

namespace {

bool g_checker1 = false;
bool g_checker2 = false;

std::string aries_param(const char* key, const char* def) { return data::plugin_param("Aries001_AnimationScreenEffects", key, def); }

}

SpriteBase::SpriteBase() { effect_target = this; }

SpriteBase::SpriteBase(g2d::BitmapPtr b) : g2d::Sprite(std::move(b)) { effect_target = this; }

void SpriteBase::update() {
    g2d::Sprite::update();
    update_visibility();
    update_animation_sprites();
}

void SpriteBase::hide_sprite() {
    hiding_ = true;
    update_visibility();
}

void SpriteBase::show_sprite() {
    hiding_ = false;
    update_visibility();
}

void SpriteBase::update_visibility() { visible = !hiding_; }

void SpriteBase::update_animation_sprites() {
    if (animation_sprites_.empty()) return;
    auto sprites = animation_sprites_;
    animation_sprites_.clear();
    for (auto& s : sprites) {
        if (s->is_playing()) animation_sprites_.push_back(s);
        else s->remove();
    }
}

void SpriteBase::start_animation(const Value* animation, bool mirror, double delay) {
    auto s = std::make_shared<SpriteAnimation>();
    s->setup(effect_target, animation, mirror, delay);
    if (parent) parent->add_child(s);
    animation_sprites_.push_back(s);
}

SpriteAnimation::SpriteAnimation() { z = 8; }

void SpriteAnimation::begin_frame_checks() {
    g_checker1 = false;
    g_checker2 = false;
}

void SpriteAnimation::setup(g2d::Sprite* target, const Value* animation, bool mirror, double delay) {
    target_ = target;
    animation_ = animation && animation->is_obj() ? animation : nullptr;
    mirror_ = mirror;
    delay_ = delay;
    if (animation_) {
        remove();
        setup_rate();
        setup_duration();
        load_bitmaps();
        create_sprites();
    }
}

void SpriteAnimation::remove() {
    if (!parent) return;
    long idx = parent->index_of(this);
    if (idx < 0) return;
    auto keep = shared_from_this();
    parent->remove_child(this);
    static const double zero[4] = {0, 0, 0, 0};
    target_blend(zero);
    target_show();
}

void SpriteAnimation::setup_rate() { rate_ = (int)yparam_num("AnimationRate", 4); }

void SpriteAnimation::setup_duration() { duration_ = (int)(*animation_)["frames"].size() * rate_ + 1; }

void SpriteAnimation::load_bitmaps() {
    bitmap1_ = load_animation((*animation_)["animation1Name"].string());
    bitmap2_ = load_animation((*animation_)["animation2Name"].string());
}

void SpriteAnimation::create_sprites() {
    int position = (*animation_)["position"].as_int();
    if (!g_checker2) {
        create_cell_sprites();
        if (position == 3) g_checker2 = true;
        create_screen_flash_sprite();
    }
    if (g_checker1) {
        duplicated_ = true;
    } else {
        duplicated_ = false;
        if (position == 3) g_checker1 = true;
    }
}

void SpriteAnimation::create_cell_sprites() {
    cell_sprites_.clear();
    for (int i = 0; i < 16; i++) {
        auto s = std::make_shared<g2d::Sprite>();
        s->anchor_x = 0.5;
        s->anchor_y = 0.5;
        cell_sprites_.push_back(s);
        add_child(s);
    }
}

void SpriteAnimation::create_screen_flash_sprite() {
    screen_flash_sprite_ = std::make_shared<g2d::ScreenSprite>();
    add_child(screen_flash_sprite_);
}

void SpriteAnimation::update() {
    g2d::Sprite::update();
    update_main();
    update_flash();
    update_screen_flash();
    update_hiding();
    g_checker1 = false;
    g_checker2 = false;
}

void SpriteAnimation::update_flash() {
    if (flash_duration_ > 0) {
        int d = flash_duration_--;
        flash_color_[3] *= (double)(d - 1) / d;
        target_blend(flash_color_);
    }
}

void SpriteAnimation::update_screen_flash() {
    if (screen_flash_duration_ > 0) {
        int d = screen_flash_duration_--;
        if (screen_flash_sprite_) {
            screen_flash_sprite_->x = -absolute_x();
            screen_flash_sprite_->y = -absolute_y();
            screen_flash_sprite_->set_opacity(screen_flash_sprite_->opacity() * (double)(d - 1) / d);
            screen_flash_sprite_->visible = screen_flash_duration_ > 0;
        }
    }
}

double SpriteAnimation::absolute_x() const {
    double v = 0;
    for (const g2d::DisplayObject* o = this; o; o = o->parent) v += o->x;
    return v;
}

double SpriteAnimation::absolute_y() const {
    double v = 0;
    for (const g2d::DisplayObject* o = this; o; o = o->parent) v += o->y;
    return v;
}

void SpriteAnimation::update_hiding() {
    if (hiding_duration_ > 0) {
        hiding_duration_--;
        if (hiding_duration_ == 0) target_show();
    }
}

void SpriteAnimation::update_main() {
    if (!is_playing() || !is_ready()) return;
    if (delay_ > 0) {
        delay_--;
        return;
    }
    duration_--;
    update_position();
    if (duration_ % rate_ == 0) update_frame();
}

void SpriteAnimation::update_position() {
    int position = (*animation_)["position"].as_int();
    if (position == 3) {
        if (parent) {
            x = parent->layout_w / 2;
            y = parent->layout_h / 2;
        }
    } else {
        g2d::Container* tp = target_->parent;
        g2d::Container* grand = tp ? tp->parent : nullptr;
        x = target_->x;
        y = target_->y;
        if (parent == grand && tp) {
            x += tp->x;
            y += tp->y;
        }
        if (position == 0) y -= target_->height();
        else if (position == 1) y -= target_->height() / 2;
    }
    if (position == 0 || position == 1) update_battler_position();
    update_sve_position();
}

void SpriteAnimation::update_battler_position() {
    SpriteBase* b = dynamic_cast<SpriteBase*>(target_);
    if (!b || !b->anim_battler()) {
        b = target_->parent ? dynamic_cast<SpriteBase*>(target_->parent) : nullptr;
        if (!b || !b->anim_battler()) return;
    }
    g2d::BitmapPtr bmp = b->bitmap();
    if (!bmp || bmp->height() <= 0) return;
    y -= b->anim_float_jump_rate() * bmp->height();
}

void SpriteAnimation::update_sve_position() {
    SpriteBase* p = target_->parent ? dynamic_cast<SpriteBase*>(target_->parent) : nullptr;
    if (!p) return;
    rpg::GameBattler* battler = p->anim_battler();
    if (!battler) return;
    int position = (*animation_)["position"].as_int();
    if (position == 3) return;
    if (!battler->is_enemy()) return;
    if (position == 0) y -= p->anim_texture_height();
    else if (position == 1) y -= p->anim_texture_height() / 2;
}

void SpriteAnimation::update_frame() {
    if (duration_ <= 0) return;
    int fi = current_frame_index();
    const Value& frames = (*animation_)["frames"];
    update_all_cell_sprites(frames[fi]);
    const Value& timings = (*animation_)["timings"];
    for (size_t i = 0; i < timings.size(); ++i) {
        if (timings[i]["frame"].as_int() == fi) process_timing_data(timings[i]);
    }
}

int SpriteAnimation::current_frame_index() const {
    int n = (int)(*animation_)["frames"].size();
    return n - (int)std::floor((double)(duration_ + rate_ - 1) / rate_);
}

void SpriteAnimation::update_all_cell_sprites(const Value& frame) {
    for (size_t i = 0; i < cell_sprites_.size(); i++) {
        if (i < frame.size()) update_cell_sprite(cell_sprites_[i].get(), frame[i]);
        else cell_sprites_[i]->visible = false;
    }
}

void SpriteAnimation::update_cell_sprite(g2d::Sprite* s, const Value& cell) {
    int pattern = cell[0].as_int();
    if (pattern < 0) {
        s->visible = false;
        return;
    }
    double sx = (pattern % 5) * 192;
    double sy = std::floor((pattern % 100) / 5) * 192;
    s->set_bitmap(pattern < 100 ? bitmap1_ : bitmap2_);
    s->set_frame(sx, sy, 192, 192);
    s->x = cell[1].num();
    s->y = cell[2].num();
    if (mirror_) s->x *= -1;
    s->rotation = cell[4].num() * M_PI / 180;
    s->scale_x = cell[3].num() / 100;
    bool flip = cell[5].truthy();
    if ((flip && !mirror_) || (!flip && mirror_)) s->scale_x *= -1;
    s->scale_y = cell[3].num() / 100;
    s->set_opacity(cell[6].num());
    s->blend_mode = cell[7].as_int();
    s->visible = true;
}

void SpriteAnimation::process_timing_data(const Value& timing) {
    const Value& se = timing["se"];
    if (se.is_obj()) {
        std::string name = rpg::to_upper(se["name"].string());
        const Value& fc = timing["flashColor"];
        double dur = timing["flashDuration"].num();
        rpg::GameScreen& scr = rpg::game_screen();
        if (name == rpg::to_upper(aries_param("Shake SE", "ft_doShake"))) {
            scr.start_shake(fc[0].num() * 0.03529411764705882352941176470588, fc[1].num() * 0.03529411764705882352941176470588, (int)dur);
            return;
        }
        if (name == rpg::to_upper(aries_param("Blur SE", "SSFX_Blur"))) {
            scr.start_blur(fc[0].num() * 0.01960784313725490196078431372549, (int)dur);
            return;
        }
        if (name == rpg::to_upper(aries_param("ZoomBlur SE", "SSFX_ZoomBlur"))) {
            scr.start_zblur(fc[0].num() * 0.0039215686274509803921568627451, (int)dur, fc[1].num() * 0.0039215686274509803921568627451, target_->x, target_->y);
            return;
        }
        if (name == rpg::to_upper(aries_param("Glow SE", "SSFX_Glow"))) {
            scr.start_glow(fc[0].num() * 0.0039215686274509803921568627451, fc[1].num() * 0.0039215686274509803921568627451,
                           fc[2].num() * 0.0039215686274509803921568627451, (int)dur);
            return;
        }
        if (name == rpg::to_upper(aries_param("Wave SE", "SSFX_Wave"))) {
            scr.start_wave(fc[0].num() * 0.0039215686274509803921568627451, fc[1].num() * 0.0039215686274509803921568627451, (int)dur, target_->x, target_->y);
            return;
        }
        if (name == rpg::to_upper(aries_param("Distort SE", "SSFX_Distort"))) {
            scr.start_distortion(fc[0].num() * 0.008, fc[1].num(), fc[2].num() * 0.039215686274509803921568627451, (int)dur);
            return;
        }
    }
    base_process_timing_data(timing);
}

void SpriteAnimation::base_process_timing_data(const Value& timing) {
    int duration = timing["flashDuration"].as_int() * rate_;
    switch (timing["flashScope"].as_int()) {
    case 1: start_flash(timing["flashColor"], duration); break;
    case 2: start_screen_flash(timing["flashColor"], duration); break;
    case 3: start_hiding(duration); break;
    }
    if (!duplicated_ && timing["se"].is_obj()) rpg::audio_manager::play_se(rpg::AudioObj::from(timing["se"]));
}

void SpriteAnimation::start_flash(const Value& color, int duration) {
    for (int i = 0; i < 4; i++) flash_color_[i] = color[i].num();
    flash_duration_ = duration;
}

void SpriteAnimation::start_screen_flash(const Value& color, int duration) {
    screen_flash_duration_ = duration;
    if (screen_flash_sprite_) {
        screen_flash_sprite_->set_color(color[0].as_int(), color[1].as_int(), color[2].as_int());
        screen_flash_sprite_->set_opacity(color[3].num());
    }
}

void SpriteAnimation::start_hiding(int duration) {
    hiding_duration_ = duration;
    target_hide();
}

void SpriteAnimation::target_show() {
    if (!target_) return;
    if (SpriteBase* b = dynamic_cast<SpriteBase*>(target_)) b->show_sprite();
    else target_->show();
}

void SpriteAnimation::target_hide() {
    if (!target_) return;
    if (SpriteBase* b = dynamic_cast<SpriteBase*>(target_)) b->hide_sprite();
    else target_->hide();
}

void SpriteAnimation::target_blend(const double c[4]) {
    if (target_) target_->set_blend_color(c);
}

}
