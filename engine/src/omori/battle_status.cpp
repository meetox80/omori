#include <algorithm>
#include <cmath>

#include "../core/regex.h"
#include "../js/vm.h"
#include "../rpg/audio_manager.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "battle_windows.h"
#include "ui_util.h"

namespace ui {
namespace btl {

namespace {

rpg::AudioObj audio(const std::string& name, double volume, double pitch) {
    rpg::AudioObj a;
    a.valid = true;
    a.name = name;
    a.volume = volume;
    a.pitch = pitch;
    a.pan = 0;
    return a;
}

int animation_layer(const Value& animation) {
    re::Groups g;
    if (re::search("<Layer:\\s*(\\d+)>", "i", animation["name"].string(), &g)) return (int)rpg::js_number(g[1]);
    return 3;
}

}

void SpriteParticle::update() {
    g2d::Sprite::update();
    if (phase_index_ >= phases_.size()) return;
    Phase& p = phases_[phase_index_];
    p.duration--;
    x += p.x;
    y += p.y;
    rotation += p.rotation * (M_PI / 360);
    set_opacity(opacity() + p.opacity);
    scale_x += p.scale_x;
    scale_y += p.scale_y;
    if (p.shake) x = base_x + std::sin((frame_count() + angry_offset) * angry_speed) * 3;
    if (p.duration <= 0) phase_index_++;
}

SpriteBattleFaceStatusEmitter::SpriteBattleFaceStatusEmitter() {
    clear();
    deactivate();
}

void SpriteBattleFaceStatusEmitter::clear() {
    intensity_ = 1;
    intensity_variance_ = 0;
    has_generator_ = false;
    spawn_timer_ = 0;
    spawn_timer_variance_ = 0;
    spawn_timer_count_ = spawn_timer_;
    child_limit_ = 0;
}

void SpriteBattleFaceStatusEmitter::setup_generator(const std::string& type) {
    clear();
    if (rpg::to_lower(type) == "angrytest") {
        intensity_ = 1;
        intensity_variance_ = 0;
        has_generator_ = true;
        spawn_timer_ = 10;
        spawn_timer_variance_ = 0;
        spawn_timer_count_ = spawn_timer_;
        child_limit_ = 20;
    }
}

void SpriteBattleFaceStatusEmitter::generate_angry() {
    auto sprite = std::make_shared<SpriteParticle>(load_picture("StatusParticles"));
    sprite->anchor_x = 0.5;
    sprite->anchor_y = 0.5;
    sprite->x = (std::round(random_unit()) * 2 - 1) * random_int(45);
    sprite->y = 0;
    sprite->set_frame(30, 30, 30, 31);
    sprite->set_opacity(0);
    sprite->scale_x = 0;
    sprite->scale_y = 0;
    add_child(sprite);
    sprite->base_x = sprite->x;
    sprite->angry_offset = random_int(5);
    sprite->angry_speed = 1.5 + random_int(100) / 100.0;
    double ys = -(1 + random_int(1));
    std::vector<SpriteParticle::Phase> phases;
    SpriteParticle::Phase a;
    a.duration = 10;
    a.y = ys;
    a.opacity = 26;
    a.scale_x = a.scale_y = 0.1;
    a.shake = true;
    phases.push_back(a);
    SpriteParticle::Phase b;
    b.duration = 30;
    b.y = ys * 2;
    b.shake = true;
    phases.push_back(b);
    SpriteParticle::Phase c;
    c.duration = 10;
    c.y = ys;
    c.rotation = 30;
    c.opacity = -26;
    c.scale_x = c.scale_y = 0.1;
    c.shake = true;
    phases.push_back(c);
    sprite->setup(phases);
}

void SpriteBattleFaceStatusEmitter::update() {
    g2d::Sprite::update();
    if (!children.empty()) {
        std::vector<g2d::DisplayPtr> list = children;
        for (auto& c : list) {
            if (static_cast<SpriteParticle*>(c.get())->is_finished()) remove_child(c.get());
        }
    }
    if (!active_) return;
    if (children.size() >= child_limit_) return;
    spawn_timer_count_--;
    if (spawn_timer_count_ <= 0) spawn_timer_count_ = spawn_timer_ + random_int(spawn_timer_variance_);
    else return;
    if (has_generator_) {
        int n = intensity_ + random_int(intensity_variance_);
        for (int i = 0; i < n; i++) generate_angry();
    }
}

void SpriteBattleAnimation::update_position() {
    const Value* anim = animation();
    if (!anim) return;
    int position = (*anim)["position"].as_int();
    if (position == 3) {
        x = 320 + offset_x();
        y = 240 + offset_y();
    } else {
        x = home_x_ + home_w_ / 2;
        y = home_y_ + home_h_;
        if (position == 0) y -= home_h_;
        else if (position == 1) y -= home_h_ / 2;
    }
}

double SpriteBattleAnimation::offset_x() const {
    double v = x;
    for (const g2d::DisplayObject* n = this; n; n = n->parent) v -= n->x;
    return v;
}

double SpriteBattleAnimation::offset_y() const {
    double v = y;
    for (const g2d::DisplayObject* n = this; n; n = n->parent) v -= n->y;
    return v;
}

std::shared_ptr<SpriteBattleAnimation> SpriteBattleFaceAnimation::start_face_animation(const Value* animation, bool mirror, double delay) {
    auto s = std::make_shared<SpriteBattleAnimation>();
    s->setup(effect_target, animation, mirror, delay);
    add_child(s);
    animation_sprites_.push_back(s);
    return s;
}

SpriteACSBubble::SpriteACSBubble(int actor_index) : actor_index_(actor_index) {
    active_ = true;
    bubble_index_ = 0;
    target_opacity_ = 0;
    opacity_duration_ = 0;
    arrow_move_direction_ = 0;
    shake_ = false;
    arrow_sprite_ = std::make_shared<g2d::Sprite>(load_system("ACSArrows"));
    arrow_sprite_->anchor_x = 0.5;
    arrow_sprite_->anchor_y = 0.5;
    add_child(arrow_sprite_);
    apply_arrow_direction(2);
    set_bubble_index(0);
    update_position(0);
}

void SpriteACSBubble::activate_bubble() {
    active_ = true;
    fade_show(15);
}

void SpriteACSBubble::deactivate_bubble() {
    active_ = false;
    fade_opacity(180, 10);
}

void SpriteACSBubble::apply_arrow_direction(int direction) {
    g2d::BitmapPtr b = arrow_sprite_->bitmap();
    double w = b ? b->width() / 4.0 : 0;
    double h = b ? b->height() : 0;
    arrow_sprite_->anchor_x = 0.5;
    arrow_sprite_->anchor_y = 0.5;
    int slot = -1;
    switch (direction) {
    case 2:
        arrow_sprite_->set_frame(0, 0, w, h);
        arrow_move_direction_ = 0;
        slot = 0;
        break;
    case 4:
        arrow_sprite_->set_frame(2 * w, 0, w, h);
        arrow_move_direction_ = 1;
        slot = 1;
        break;
    case 6:
        arrow_sprite_->set_frame(1 * w, 0, w, h);
        arrow_move_direction_ = 1;
        slot = 2;
        break;
    case 8:
        arrow_sprite_->set_frame(3 * w, 0, w, h);
        arrow_move_direction_ = 0;
        slot = 3;
        break;
    }
    if (slot >= 0) {
        arrow_sprite_->x = arrow_positions_[slot][0];
        arrow_sprite_->y = arrow_positions_[slot][1];
    }
}

void SpriteACSBubble::set_arrow_direction(const std::string& direction) {
    arrow_key_ = direction;
    int d = 0;
    if (direction == "down" || direction == "2") d = 2;
    else if (direction == "left" || direction == "4") d = 4;
    else if (direction == "right" || direction == "6") d = 6;
    else if (direction == "up" || direction == "8") d = 8;
    apply_arrow_direction(d);
}

void SpriteACSBubble::set_bubble_index(int index) {
    g2d::BitmapPtr b = load_system("ACS_Bubble");
    double w = b->width() / 7.0, h = b->height() / 2.0;
    set_bitmap(b);
    set_frame((index % 7) * w, std::floor(index / 7) * h, w, h);
    bubble_index_ = index;
    auto set = [&](double a0, double a1, double b0, double b1, double c0, double c1, double d0, double d1) {
        arrow_positions_[0][0] = a0;
        arrow_positions_[0][1] = a1;
        arrow_positions_[1][0] = b0;
        arrow_positions_[1][1] = b1;
        arrow_positions_[2][0] = c0;
        arrow_positions_[2][1] = c1;
        arrow_positions_[3][0] = d0;
        arrow_positions_[3][1] = d1;
    };
    if (index >= 0 && index <= 3) set(70, 85, 15, 45, 120, 45, 70, 10);
    else if (index >= 4 && index <= 6) set(65, 95, 5, 55, 125, 55, 65, 10);
    else if (index >= 7 && index <= 9) set(60, 85, 5, 45, 115, 45, 60, 5);
    else if (index >= 10 && index <= 12) set(60, 90, 5, 50, 115, 50, 60, 15);
    if (arrow_key_.empty()) apply_arrow_direction(2);
    else set_arrow_direction(arrow_key_);
}

void SpriteACSBubble::update_position(int position) {
    static const double pos[4][3][2] = {{{110, 45}, {90, 110}, {5, 140}},
                                        {{-115, 45}, {-105, 110}, {-15, 140}},
                                        {{5, -80}, {95, -55}, {110, 10}},
                                        {{-5, -70}, {-95, -55}, {-105, 10}}};
    if (actor_index_ >= 0 && actor_index_ < 4) {
        if (position >= 0 && position < 3) {
            x = pos[actor_index_][position][0];
            y = pos[actor_index_][position][1];
        }
    } else {
        x = 0;
        y = 0;
    }
}

void SpriteACSBubble::fade_opacity(double op, int duration) {
    target_opacity_ = op;
    opacity_duration_ = duration;
}

void SpriteACSBubble::stop_shake() {
    shake_ = false;
    anchor_x = 0;
    anchor_y = 0;
    static const double zero[4] = {0, 0, 0, 0};
    set_blend_color(zero);
}

void SpriteACSBubble::update() {
    g2d::Sprite::update();
    update_arrow_direction();
    update_opacity();
    update_shake();
}

void SpriteACSBubble::update_arrow_direction() {
    if (!active_) return;
    double v = 0.5 + std::sin(frame_count() * 0.13) * 0.15;
    if (arrow_move_direction_ == 0) arrow_sprite_->anchor_y = v;
    else arrow_sprite_->anchor_x = v;
}

void SpriteACSBubble::update_opacity() {
    if (opacity_duration_ > 0) {
        double d = opacity_duration_;
        set_opacity((opacity() * (d - 1) + target_opacity_) / d);
        opacity_duration_--;
    }
    static const double on[4] = {0, 0, 0, 0};
    static const double off[4] = {-80, -80, -80, 255};
    arrow_sprite_->set_color_tone(active_ ? on : off);
}

void SpriteACSBubble::update_shake() {
    if (!shake_) return;
    double power = 0.01;
    anchor_x = random_unit() >= 0.5 ? power : -power;
    anchor_y = random_unit() >= 0.5 ? power : -power;
    double alpha = 64 + std::sin(frame_count() * 0.3) * 64;
    double c[4] = {255, 0, 0, alpha};
    set_blend_color(c);
}

void WindowOmoriBattleActorStatus::initialize(int index, FaceLayers* layers, double hx, double hy) {
    home_x = hx;
    home_y = hy;
    layers_ = layers;
    actor_index_ = index;
    hp_anim_ = Anim();
    mp_anim_ = Anim();
    selected = false;
    overlay_side_ = 0;
    overlay_delay_ = 15;
    acs_bubble_opacity_duration_ = 0;
    acs_bubble_opacity_ = 0;
    popup_count_ = 0;
    WindowBase::initialize(14, 100, window_width(), window_height());
    set_window_alpha(0);
    create_sprites();
    refresh();
}

rpg::GameActor* WindowOmoriBattleActorStatus::actor() { return rpg::game_party().member_at_status_index(actor_index_); }

void WindowOmoriBattleActorStatus::create_sprites() {
    FaceLayers& l = *layers_;
    status_back_sprite_ = std::make_shared<g2d::Sprite>(load_system("faceset_states"));
    status_back_sprite_->x = home_x + 7;
    status_back_sprite_->y = home_y + 17;
    set_status_back(0, false);
    l.status_back->add_child(status_back_sprite_);
    face_sprite = std::make_shared<menu::SpriteOmoMenuStatusFace>();
    face_sprite->x = home_x + (width() - 106) / 2;
    face_sprite->y = home_y + 15;
    l.face->add_child(face_sprite);
    face_sprite->set_actor(actor());
    face_sprite->mask_rect = std::make_shared<g2d::Rect>(7 - (width() - 106) / 2, 25 - 15, width() - 14, 92);
    polaroid_sprite_ = std::make_shared<g2d::Sprite>(load_system("player_box"));
    polaroid_sprite_->x = home_x;
    polaroid_sprite_->y = home_y;
    l.polaroid->add_child(polaroid_sprite_);
    hp_bar_sprite_ = std::make_shared<g2d::Sprite>(load_system("bar_gradients"));
    hp_bar_sprite_->x = home_x + 28;
    hp_bar_sprite_->y = home_y + 127;
    hp_bar_sprite_->set_frame(0, 0, 81, 12);
    l.polaroid->add_child(hp_bar_sprite_);
    mp_bar_sprite_ = std::make_shared<g2d::Sprite>(load_system("bar_gradients"));
    mp_bar_sprite_->x = home_x + 28;
    mp_bar_sprite_->y = home_y + 146;
    mp_bar_sprite_->set_frame(0, 19, 81, 12);
    l.polaroid->add_child(mp_bar_sprite_);
    state_sprite_ = std::make_shared<g2d::Sprite>(load_system("statelist"));
    state_sprite_->anchor_x = 0.5;
    state_sprite_->anchor_y = 0.5;
    state_sprite_->x = home_x + width() / 2;
    state_sprite_->y = home_y + 14;
    state_sprite_->set_frame(0, 0, 134, 24);
    l.polaroid->add_child(state_sprite_);
    selected_overlay_ = std::make_shared<g2d::Sprite>(load_system("target_selected"));
    selected_overlay_->x = home_x - 12;
    selected_overlay_->y = home_y - 3;
    selected_overlay_->set_opacity(0);
    l.polaroid->add_child(selected_overlay_);
    create_status_particle_emitters();
    create_acs_bubble_sprites();
    g2d::SpritePtr contents = contents_sprite();
    l.polaroid->add_child(contents);
    contents->x = home_x;
    contents->y = home_y;
    damage_container_ = std::make_shared<g2d::Sprite>();
    l.polaroid->add_child(damage_container_);
}

void WindowOmoriBattleActorStatus::create_status_particle_emitters() {
    FaceLayers& l = *layers_;
    std::shared_ptr<g2d::Sprite> list[3] = {l.status_back, l.face, l.polaroid};
    emitters_.clear();
    for (auto& layer : list) {
        auto s = std::make_shared<SpriteBattleFaceStatusEmitter>();
        s->x = home_x + width() / 2;
        s->y = home_y + 120;
        emitters_.push_back(s);
        layer->add_child(s);
    }
}

void WindowOmoriBattleActorStatus::create_acs_bubble_sprites() {
    acs_bubble_container = std::make_shared<g2d::Sprite>();
    acs_bubble_container->set_opacity(0);
    add_child(acs_bubble_container);
    acs_bubbles_.clear();
    for (int i = 0; i < 3; i++) {
        auto s = std::make_shared<SpriteACSBubble>(actor_index_);
        s->update_position(i);
        acs_bubbles_.push_back(s);
        acs_bubble_container->add_child(s);
    }
}

void WindowOmoriBattleActorStatus::refresh() {
    rpg::GameActor* a = actor();
    if (!a) return;
    set_status_back(a->status_back_index());
    set_status_header(a->status_list_index());
    face_sprite->set_anim_row(a->status_face_index());
    setup_status_particles(a->status_state_particles_data());
}

void WindowOmoriBattleActorStatus::draw_hp(double hp, double max_hp) {
    const Value& loc = menu::blue("Window_OmoriBattleActorStatus");
    c().font_size = loc["drawHP_contents_fontsize"].as_int();
    double yy = 112;
    const Value& pos = loc["drawHP_contents_clearRect_position"];
    double cy = menu::lang_num(pos[1], {{"y", js::Val::num(yy)}});
    c().clear_rect(pos[0].num(), cy, width(), 24);
    std::string max_text = " " + js::number_to_string(max_hp);
    double w = text_width(max_text) + 11;
    draw_text(js::number_to_string(hp), 0, yy, width() - w, "right");
    draw_text(max_text, 0, yy, width() - 10, "right");
    draw_text("/", 0, yy + 1, width() - w + 5, "right");
    reset_font_settings();
}

void WindowOmoriBattleActorStatus::draw_mp(double mp, double max_mp) {
    c().font_size = menu::blue("Window_OmoriBattleActorStatus")["drawMP_contents_fontsize"].as_int();
    double yy = 131;
    c().clear_rect(0, yy + 10, width(), 24);
    std::string max_text = " " + js::number_to_string(max_mp);
    double w = text_width(max_text) + 11;
    draw_text(js::number_to_string(mp), 0, yy, width() - w, "right");
    draw_text(max_text, 0, yy, width() - 10, "right");
    draw_text("/", 0, yy + 1, width() - w + 5, "right");
    reset_font_settings();
}

void WindowOmoriBattleActorStatus::update() {
    WindowBase::update();
    update_positions();
    update_status_back_fade();
    update_bars();
    update_animation();
    update_overlay();
    update_damage();
    update_acs_bubbles();
}

void WindowOmoriBattleActorStatus::update_positions() {
    status_back_sprite_->x = x + 7;
    status_back_sprite_->y = y + 17;
    face_sprite->x = x + (width() - 106) / 2;
    face_sprite->y = y + 15;
    polaroid_sprite_->x = x;
    polaroid_sprite_->y = y;
    hp_bar_sprite_->x = x + 28;
    hp_bar_sprite_->y = y + 127;
    mp_bar_sprite_->x = x + 28;
    mp_bar_sprite_->y = y + 146;
    state_sprite_->x = x + width() / 2;
    state_sprite_->y = y + 14;
    selected_overlay_->x = x - 12;
    selected_overlay_->y = y - 3;
    contents_sprite()->x = x;
    contents_sprite()->y = y;
    for (auto& e : emitters_) {
        e->x = x + width() / 2;
        e->y = y + 120;
    }
}

void WindowOmoriBattleActorStatus::set_status_header(int index) {
    const g2d::Rect& f = state_sprite_->frame();
    state_sprite_->set_frame(f.x, index * 24, f.width, f.height);
}

void WindowOmoriBattleActorStatus::set_status_back(int index, bool fade) {
    if (fade) {
        auto s = std::make_shared<g2d::Sprite>(load_system("faceset_states"));
        const g2d::Rect& f = status_back_sprite_->frame();
        s->set_frame(f.x, f.y, f.width, f.height);
        status_back_sprite_->add_child_at(s, 0);
    }
    double w = 100, h = 100;
    double sx = (index % 4) * h;
    double sy = std::floor(index / 4) * w;
    status_back_sprite_->set_frame(sx, sy, w, h);
}

void WindowOmoriBattleActorStatus::setup_status_particles(const std::map<std::string, std::string>& data) {
    static const char* names[] = {"behind", "front", "top"};
    for (size_t i = 0; i < 3 && i < emitters_.size(); i++) {
        auto it = data.find(names[i]);
        if (it != data.end() && !it->second.empty()) {
            emitters_[i]->setup_generator(it->second);
            emitters_[i]->activate();
        } else {
            emitters_[i]->clear();
            emitters_[i]->deactivate();
        }
    }
}

void WindowOmoriBattleActorStatus::start_face_animation(int layer, const Value* animation) {
    FaceLayers& l = *layers_;
    std::shared_ptr<SpriteBattleFaceAnimation> target = l.animation[4];
    switch (layer) {
    case 0: target = l.animation[0]; break;
    case 1: target = l.animation[1]; break;
    case 2: target = l.animation[2]; break;
    case 3: target = l.animation[4]; break;
    }
    auto s = target->start_face_animation(animation, false, 0);
    s->set_home_position(home_x, home_y);
}

void WindowOmoriBattleActorStatus::update_animation() {
    rpg::GameActor* a = actor();
    if (!a) return;
    while (a->is_animation_requested()) {
        rpg::GameBattler::Anim data = a->shift_animation();
        const Value& animation = data::animations()[data.id];
        if (!animation.is_obj()) continue;
        start_face_animation(animation_layer(animation), &animation);
    }
}

void WindowOmoriBattleActorStatus::update_status_back_fade() {
    if (status_back_sprite_->children.empty()) return;
    bool remove = false;
    for (auto& c : status_back_sprite_->children) {
        c->set_opacity(c->opacity() - 10);
        if (c->opacity() <= 0) remove = true;
    }
    if (remove) {
        std::vector<g2d::DisplayPtr> list = status_back_sprite_->children;
        for (auto& c : list) {
            if (c->opacity() <= 0) status_back_sprite_->remove_child(c.get());
        }
    }
}

void WindowOmoriBattleActorStatus::update_damage() {
    rpg::GameActor* a = actor();
    if (a) {
        if (a->is_damage_popup_requested()) {
            auto damage = std::make_shared<SpriteDamage>();
            damage->x = width() / 2;
            damage->y = height() - (40 + (20 * std::floor(a->damage_popup.size() / 3.0))) + (40 * popup_count_);
            damage->x += home_x;
            damage->y += home_y + 2;
            damage->setup(a);
            double hp_damage = a->damage_popup.empty() ? a->result().hp_damage : a->damage_popup.front().hp_damage;
            damage_container_->add_child(damage);
            damage_sprites_.push_back(damage);
            if (hp_damage > 0) {
                js::Val* sc = rpg::game_temp().expando.find(js::atom("_secondChance"));
                if (!(sc && sc->truthy()) || a->actor_id() != 1) face_sprite->set_anim_row(9);
            }
            popup_count_++;
        } else if (popup_count_ > 0) {
            popup_count_ = 0;
        }
    }
    std::vector<std::shared_ptr<SpriteDamage>> remove;
    for (auto& s : damage_sprites_) {
        if (!s->is_playing()) remove.push_back(s);
    }
    for (auto& s : remove) {
        auto it = std::find(damage_sprites_.begin(), damage_sprites_.end(), s);
        if (it != damage_sprites_.end()) damage_sprites_.erase(it);
        damage_container_->remove_child(s.get());
    }
}

void WindowOmoriBattleActorStatus::update_bars() {
    rpg::GameActor* a = actor();
    if (a) {
        if (a->hp() != hp_anim_.old) {
            hp_anim_.target = a->hp();
            hp_anim_.old = a->hp();
            hp_anim_.duration = (int)std::clamp(std::fabs(hp_anim_.current - hp_anim_.target), 0.0, 30.0);
        }
        if (a->mp() != mp_anim_.old) {
            mp_anim_.target = a->mp();
            mp_anim_.old = a->mp();
            mp_anim_.duration = (int)std::clamp(std::fabs(mp_anim_.current - mp_anim_.target), 0.0, 30.0);
        }
    }
    if (!a) return;
    if (hp_anim_.duration > 0) {
        Anim& an = hp_anim_;
        an.current = (an.current * (an.duration - 1) + an.target) / an.duration;
        an.duration--;
        draw_hp(std::round(an.current), a->mhp());
        double w = (an.current / a->mhp()) * 81;
        const g2d::Rect& f = hp_bar_sprite_->frame();
        hp_bar_sprite_->set_frame(f.x, f.y, w, f.height);
    }
    if (mp_anim_.duration > 0) {
        Anim& an = mp_anim_;
        an.current = (an.current * (an.duration - 1) + an.target) / an.duration;
        an.duration--;
        draw_mp(std::round(an.current), a->mmp());
        double w = (an.current / a->mmp()) * 81;
        const g2d::Rect& f = mp_bar_sprite_->frame();
        mp_bar_sprite_->set_frame(f.x, f.y, w, f.height);
    }
}

void WindowOmoriBattleActorStatus::update_overlay() {
    if (selected) {
        if (overlay_delay_ <= 0) {
            if (overlay_side_ == 0) {
                selected_overlay_->set_opacity(selected_overlay_->opacity() + 15);
                if (selected_overlay_->opacity() >= 255) {
                    overlay_side_ = 1;
                    overlay_delay_ = 40;
                }
            } else {
                selected_overlay_->set_opacity(selected_overlay_->opacity() - 15);
                if (selected_overlay_->opacity() <= 0) {
                    overlay_side_ = 0;
                    overlay_delay_ = 10;
                }
            }
        } else {
            overlay_delay_--;
        }
    } else if (selected_overlay_->opacity() > 0 || overlay_side_ == 1) {
        selected_overlay_->set_opacity(selected_overlay_->opacity() - 25);
        if (selected_overlay_->opacity() <= 0) {
            overlay_side_ = 0;
            overlay_delay_ = 0;
        }
    }
}

void WindowOmoriBattleActorStatus::show_acs_bubbles(int duration) {
    acs_bubble_opacity_duration_ = duration;
    acs_bubble_opacity_ = 255;
}

void WindowOmoriBattleActorStatus::hide_acs_bubbles(int duration) {
    acs_bubble_opacity_duration_ = duration;
    acs_bubble_opacity_ = 0;
}

void WindowOmoriBattleActorStatus::setup_acs_bubbles(const std::vector<std::pair<std::string, int>>& list) {
    rpg::GameActor* a = actor();
    for (size_t i = 0; i < acs_bubbles_.size(); i++) {
        auto& bubble = acs_bubbles_[i];
        bubble->update_position((int)i);
        if (i >= list.size()) continue;
        const Value& skill = data::skills()[list[i].second];
        const Value* icon = skill["meta"].find("ChainSkillIcon");
        int bubble_index = icon ? (int)rpg::js_number(data::js_string(*icon)) : 0;
        if (skill["meta"]["ChainSkillEnergyRelease"].truthy() && rpg::game_party().stress_energy_count >= 10) {
            bubble_index = rpg::game_party().size() == 1 ? 3 : 2;
            bubble->start_shake();
        } else {
            bubble->stop_shake();
        }
        bubble->set_bubble_index(bubble_index);
        bubble->set_arrow_direction(list[i].first);
        if (a && a->can_use(rpg::ItemRef::skill(list[i].second))) bubble->activate_bubble();
        else bubble->deactivate_bubble();
    }
}

void WindowOmoriBattleActorStatus::select_acs_bubble(const std::string& direction) {
    for (auto& bubble : acs_bubbles_) {
        if (bubble->arrow_key() == direction) {
            bubble->fade_opacity(255, 5);
        } else {
            bubble->deactivate_bubble();
            bubble->fade_opacity(0, 5);
        }
    }
}

void WindowOmoriBattleActorStatus::update_acs_bubbles() {
    if (acs_bubble_opacity_duration_ > 0) {
        double d = acs_bubble_opacity_duration_;
        acs_bubble_container->set_opacity((acs_bubble_container->opacity() * (d - 1) + acs_bubble_opacity_) / d);
        acs_bubble_opacity_duration_--;
    }
}

SpriteStressBar::SpriteStressBar() {
    lx_ = 0;
    ekg_row_ = 0;
    pending_ekg_row_ = -1;
    frame_count_ = 0;
    background_ = std::make_shared<g2d::Sprite>();
    add_child(background_);
    update_background_image();
    ekg_line_ = std::make_shared<g2d::Sprite>(g2d::Bitmap::create(290, 28));
    ekg_line_->x = 20;
    ekg_line_->y = 8;
    add_child(ekg_line_);
    ekg_line_bitmap_ = g2d::Bitmap::create(290, 28);
    ekg_line_new_bitmap_ = g2d::Bitmap::create(290, 28);
    ekg_text_ = std::make_shared<g2d::Sprite>(g2d::Bitmap::create(33, 33));
    ekg_text_->x = 320;
    ekg_text_->y = 7;
    ekg_text_->bitmap()->font_size = 28;
    add_child(ekg_text_);
    refresh_ekg_bitmap();
}

void SpriteStressBar::refresh_ekg_bitmap() {
    std::string name = "energy_stress_ekg_line";
    switch (rpg::game_variables().value(22).to_int()) {
    case 1: name = "energy_dw_line"; break;
    case 3:
    case 4: name = "energy_stress_ekg_line"; break;
    }
    g2d::BitmapPtr b = load_system(name);
    ekg_line_bitmap_->clear();
    ekg_line_bitmap_->blt(b.get(), 0, ekg_row_ * 28, b->width(), 28, 0, 0);
    if (pending_ekg_row_ >= 0) {
        ekg_line_new_bitmap_->clear();
        ekg_line_new_bitmap_->blt(b.get(), 0, pending_ekg_row_ * 28, b->width(), 28, 0, 0);
    }
}

void SpriteStressBar::draw_stress_count_value(double value) {
    g2d::Bitmap& b = *ekg_text_->bitmap();
    b.clear();
    int v = (int)std::clamp(value, 0.0, 10.0);
    std::string text = std::to_string(v);
    if (text.size() < 2) text = std::string(2 - text.size(), '0') + text;
    b.draw_text(text, 0, -4, b.width(), b.height(), "center");
}

void SpriteStressBar::update() {
    g2d::Sprite::update();
    rpg::GameParty& p = rpg::game_party();
    bool stressed = p.actor_is_affected_by_state(1, 20) || p.actor_is_affected_by_state(8, 20);
    int energy = (int)p.stress_energy_count;
    if (stressed) energy = 10;
    frame_count_ = (frame_count_ + 1) % 60;
    if (energy != ekg_row_) {
        if (energy == 10 && rpg::game_troop().troop_id == 451) pending_ekg_row_ = 11;
        else pending_ekg_row_ = energy;
        update_background_image();
        if (!stressed) draw_stress_count_value(energy);
    }
    auto& st = rpg::battle::st();
    int speed = st.has_stress_speed ? (int)st.stress_speed : 3;
    if (frame_count_ && speed && frame_count_ % speed == 0) {
        int world = rpg::game_variables().value(22).to_int();
        if (energy >= 10 && (world == 3 || world == 4) && stressed) draw_stress_count_value(random_int(11));
    }
    update_line(stressed);
}

void SpriteStressBar::update_line(bool stressed) {
    if (pending_ekg_row_ >= 0) {
        ekg_row_ = pending_ekg_row_;
        pending_ekg_row_ = -1;
        refresh_ekg_bitmap();
        if (!stressed) draw_stress_count_value(ekg_row_);
    }
    double space = 40;
    lx_ = (lx_ + 1) % (int)(ekg_line_bitmap_->width() + space);
    g2d::Bitmap& dst = *ekg_line_->bitmap();
    dst.blt(ekg_line_bitmap_.get(), lx_, 0, 1, ekg_line_bitmap_->height(), lx_, 0);
    dst.clear_rect(lx_ - space, 0, 1, ekg_line_bitmap_->height());
}

void SpriteStressBar::update_background_image() {
    std::string name = "StressBar_DreamWorld";
    int index = 0;
    int rows = 5;
    rpg::GameParty& p = rpg::game_party();
    double stress = p.stress_energy_count;
    if (p.actor_is_affected_by_state(1, 20) || p.actor_is_affected_by_state(8, 20)) stress = 10;
    switch (rpg::game_variables().value(22).to_int()) {
    case 1: index = (int)std::min(std::max(std::floor(stress / 2), 0.0), 4.0); break;
    case 3:
    case 4:
        name = "StressBar_BlackSpace";
        rows = 4;
        if (stress == 10) index = 2;
        else if (stress > 6 && stress < 10) index = 2;
        else if (stress > 3 && stress <= 6) index = 1;
        else index = 0;
        break;
    }
    g2d::BitmapPtr b = load_system(name);
    double h = b->height() / (double)rows;
    background_->set_bitmap(b);
    background_->set_frame(0, index * h, b->width(), h);
}

SpriteBattleLowHpOverlay::SpriteBattleLowHpOverlay() {
    set_bitmap(load_picture("battle_lowhealth"));
    actor_ = rpg::game_party().get_omori();
    set_opacity(102);
    opacity_change_ = 0;
    opacity_speed_ = 5;
    opacity_side_ = 0;
    active_opacity_ = -60;
    has_old_hp_ = false;
    hidden_ = false;
    beep_counter_ = 0;
    danger_rate_ = 0;
    hp_rate_ = 1;
}

void SpriteBattleLowHpOverlay::update() {
    g2d::Sprite::update();
    auto& st = rpg::battle::st();
    if (!st.show_low_hp_overlay) {
        hidden_ = true;
        set_opacity(0);
        return;
    }
    if (st.hide_low_hp_overlay) {
        if (opacity() > 0) set_opacity(opacity() - 10);
        return;
    }
    visible = !rpg::game_switches().value(93);
    if (actor_) {
        if (!has_old_hp_ || actor_->hp() != old_hp_) {
            double low = 0.2;
            hp_rate_ = actor_->hp_rate();
            double danger_max = actor_->mhp() * low;
            danger_rate_ = actor_->hp() / danger_max;
            old_hp_ = actor_->hp();
            has_old_hp_ = true;
            hidden_ = hp_rate_ > low;
            opacity_speed_ = 0;
            if (!hidden_) {
                opacity_speed_ = 4 + (5 - (5 * danger_rate_));
                if (actor_->hp() == 0) rpg::audio_manager::play_danger_bgs(audio("[sfx]battle_flatline", 0, 100));
                else rpg::audio_manager::play_danger_bgs(audio("boss_something_heartbeat", 40, 100 + (50 - (50 * danger_rate_))));
            } else {
                rpg::audio_manager::fade_out_danger_bgs(1);
            }
        }
    }
    if (hidden_ && active_opacity_ > -60) active_opacity_ = std::max(active_opacity_ - 5, -60.0);
    else if (!hidden_ && active_opacity_ < 80) active_opacity_ = std::min(active_opacity_ + 5, 80.0);
    if (opacity_side_ == 0) {
        double mx = 60;
        opacity_change_ = std::min(opacity_change_ + opacity_speed_, mx);
        if (opacity_change_ == mx) opacity_side_ = 1;
    } else {
        double mn = -60;
        opacity_change_ = std::max(opacity_change_ - opacity_speed_, mn);
        if (opacity_change_ == mn) opacity_side_ = 0;
    }
    set_opacity(active_opacity_ + opacity_change_);
    if (!hidden_ && actor_ && actor_->hp() > 0 && hp_rate_ <= 0.1) {
        beep_counter_--;
        if (beep_counter_ <= 0) {
            rpg::audio_manager::play_se(audio("[sfx]battle_ekg_beep", 50, 100));
            beep_counter_ = 100 - (20 - (20 * danger_rate_));
        }
    }
}

SpriteBattleIntroContainer::SpriteBattleIntroContainer(const std::string& type) : type_(type) {
    if (!type_.empty() && rpg::to_lower(type_) == "finalbattle") setup_final_battle_animation();
}

void SpriteBattleIntroContainer::setup_final_battle_animation() {
    g2d::BitmapPtr b = load_picture("omori_prebattle");
    SceneBattle* scene = refs().scene;
    if (scene) scene->hide_all_menu_elements(1);
    animation_sprite_ = std::make_shared<g2d::Sprite>();
    add_child(animation_sprite_);
    if (refs().spriteset && !refs().spriteset->enemy_sprites.empty()) battler_sprite_ = refs().spriteset->enemy_sprites[0];
    if (battler_sprite_) {
        if (battler_sprite_->battler()) battler_sprite_->battler()->hide();
        battler_sprite_->visible = false;
    }
    small_delay_ = 15;
    animation_sprite_->set_bitmap(b);
    double w = b->width() / 6.0, h = b->height() / 4.0;
    animation_sprite_->set_frame(0, 0, w, h);
    animation_sprite_->x = 67;
    animation_sprite_->y = 480 - h;
    index_ = 0;
    frames_ = 21;
    count_ = 10;
    delay_ = 10;
    has_animation_data_ = true;
}

void SpriteBattleIntroContainer::update() {
    g2d::Sprite::update();
    if (!type_.empty() && rpg::to_lower(type_) == "finalbattle") update_final_battle_animation();
}

void SpriteBattleIntroContainer::update_final_battle_animation() {
    if (!animation_sprite_ || !animation_sprite_->bitmap() || !has_animation_data_) return;
    SceneBattle* scene = refs().scene;
    if (!scene || !scene->gp_white) return;
    if (small_delay_ > 0) {
        small_delay_--;
        return;
    }
    if (scene->gp_white->parent) {
        if (scene->gp_white->alpha > 0) {
            scene->gp_white->alpha -= 0.05;
            return;
        }
        double t[4] = {0, 0, 0, 0};
        rpg::game_screen().start_tint(t, 1);
        scene->remove_child(scene->gp_white.get());
    }
    count_--;
    if (count_ <= 0) {
        count_ = delay_;
        if (index_ < frames_) {
            index_++;
        } else {
            auto keep = shared_from_this();
            visible = false;
            if (parent) parent->remove_child(this);
            if (battler_sprite_ && !battler_sprite_->appeared) {
                if (battler_sprite_->battler()) battler_sprite_->battler()->appear();
                battler_sprite_->effect_duration = 0;
                battler_sprite_->appeared = true;
                battler_sprite_->set_opacity(255);
            }
            rpg::battle::set_battle_intro(js::Val::null());
            scene->show_all_menu_elements();
        }
        double w = animation_sprite_->width(), h = animation_sprite_->height();
        animation_sprite_->set_frame((index_ % 6) * w, std::floor(index_ / 6) * h, w, h);
    }
}

}
}
