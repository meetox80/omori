#include <algorithm>
#include <cmath>

#include "../core/image.h"
#include "../js/compile.h"
#include "../js/random.h"
#include "../js/vm.h"
#include "../rpg/audio_manager.h"
#include "../rpg/data.h"
#include "../rpg/evalctx.h"
#include "../rpg/game.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "../rpg/scene.h"
#include "battle_ui.h"
#include "ui_util.h"
#include "window_base.h"

namespace ui {
namespace btl {

Refs& refs() {
    static Refs r;
    return r;
}

double frame_count() { return (double)rpg::scene_manager::frame_count(); }
double random_unit() { return rng::next(); }
int random_int(int max) { return (int)std::floor(rng::next() * max); }

namespace {

bool sprite_visible(rpg::GameBattler* b) {
    if (!b) return false;
    if (b->is_actor()) return static_cast<rpg::GameActor*>(b)->is_sprite_visible();
    return static_cast<rpg::GameEnemy*>(b)->is_sprite_visible();
}

g2d::BitmapPtr load_hued(const std::string& folder, const std::string& name, int hue, bool smooth) {
    g2d::BitmapPtr b = load_folder(folder, name);
    if (hue && b && b->width() > 0) {
        g2d::BitmapPtr h = g2d::Bitmap::create(b->width(), b->height());
        h->blt(b.get(), 0, 0, b->width(), b->height(), 0, 0);
        h->rotate_hue(hue);
        b = h;
    }
    if (b) b->smooth = smooth;
    return b;
}

g2d::BitmapPtr load_path_hued(const std::string& path, int hue) {
    std::string p = path;
    if (p.size() > 4 && p.compare(0, 4, "img/") == 0) p = p.substr(4);
    while (!p.empty() && p.back() == '/') p.pop_back();
    size_t slash = p.rfind('/');
    std::string folder = slash == std::string::npos ? p : p.substr(0, slash);
    std::string name = slash == std::string::npos ? std::string() : p.substr(slash + 1);
    return load_hued(folder, name, hue, true);
}

bool side_view() { return data::system()["optSideView"].truthy(); }

double icon_size() { return yparam_num("IconWidth", 32); }

std::string group_text(double n) { return group_digits(n); }

}

SpriteDamage::SpriteDamage() {
    duration_ = (int)yparam_num("BECPopupDur", 90);
    damage_bitmap_ = load_system("Damage");
}

void SpriteDamage::setup(rpg::GameBattler* target) {
    rpg::GameActionResult result = target->shift_damage_popup();
    if (result.missed || result.evaded) create_miss();
    else if (result.hp_affected) create_digits(0, result.hp_damage);
    else if (target->is_alive() && result.mp_damage != 0) create_digits(2, result.mp_damage);
    if (result.critical) setup_critical_effect();
}

void SpriteDamage::setup_critical_effect() {
    std::string text = yparam_str("BECCritPopup");
    double c[4] = {0, 0, 0, 0};
    size_t pos = 0;
    for (int i = 0; i < 4 && pos <= text.size(); i++) {
        size_t comma = text.find(',', pos);
        std::string part = text.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        c[i] = rpg::js_number(rpg::trim(part));
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    for (int i = 0; i < 4; i++) flash_color_[i] = c[i];
    flash_duration_ = (int)yparam_num("BECCritDur", 60);
}

double SpriteDamage::digit_width() const { return damage_bitmap_ ? damage_bitmap_->width() / 10.0 : 0; }
double SpriteDamage::digit_height() const { return damage_bitmap_ ? damage_bitmap_->height() / 7.0 : 0; }

void SpriteDamage::create_miss() {
    double h = digit_height();
    g2d::SpritePtr s = create_child_sprite();
    s->set_frame(0, 4 * h, 48, h);
}

void SpriteDamage::create_immune() {
    double h = digit_height();
    g2d::SpritePtr s = create_child_sprite();
    s->set_frame(0, 5 * h, 82, h);
}

void SpriteDamage::create_digits(int base_row, double value) {
    std::string str = js::number_to_string(std::fabs(value));
    int row = base_row + (value < 0 ? 1 : 0);
    double w = digit_width(), h = digit_height();
    for (size_t i = 0; i < str.size(); i++) {
        g2d::SpritePtr s = create_child_sprite();
        int n = str[i] >= '0' && str[i] <= '9' ? str[i] - '0' : 0;
        s->set_frame(n * w, row * h, w, h);
        s->x = (i - (str.size() - 1) / 2.0) * (w - 8);
    }
}

g2d::SpritePtr SpriteDamage::create_child_sprite() {
    auto s = std::make_shared<g2d::Sprite>(damage_bitmap_);
    s->anchor_x = 0.5;
    s->anchor_y = 1;
    s->y = -50;
    s->set_opacity(0);
    add_child(s);
    return s;
}

void SpriteDamage::update() {
    g2d::Sprite::update();
    if (duration_ > 0) {
        bool prev_done = true;
        std::vector<g2d::DisplayPtr> list = children;
        for (auto& c : list) {
            g2d::Sprite* s = static_cast<g2d::Sprite*>(c.get());
            if (prev_done) update_child(s);
            prev_done = s->y >= -20;
        }
        if (prev_done) duration_--;
    }
    update_flash();
    update_opacity();
    if (!rpg::battle::st().show_damage) set_opacity(0);
}

void SpriteDamage::update_child(g2d::Sprite* s) {
    s->y = std::min(s->y + 6, 0.0);
    if (duration_ > 10) s->set_opacity(s->opacity() + 20);
    s->set_blend_color(flash_color_);
}

void SpriteDamage::update_flash() {
    if (flash_duration_ > 0) {
        int d = flash_duration_--;
        flash_color_[3] *= (double)(d - 1) / d;
    }
}

void SpriteDamage::update_opacity() {
    if (duration_ < 10) set_opacity(255.0 * duration_ / 10);
}

SpriteStateIcon::SpriteStateIcon() {
    anchor_x = 0.5;
    anchor_y = 0.5;
    turn_counter_sprite_ = std::make_shared<g2d::Sprite>();
    add_child(turn_counter_sprite_);
    turn_counter_sprite_->anchor_x = 0.5;
    turn_counter_sprite_->anchor_y = 0.5;
    turn_counter_sprite_->set_bitmap(g2d::Bitmap::create((int)icon_size(), (int)yparam_num("IconHeight", 32)));
    set_bitmap(load_system("IconSet"));
    set_frame(0, 0, 0, 0);
}

void SpriteStateIcon::update() {
    g2d::Sprite::update();
    animation_count_++;
    if (animation_count_ >= 40) {
        update_icon();
        update_frame();
        animation_count_ = 0;
    }
    update_mirror();
}

void SpriteStateIcon::update_icon() {
    std::vector<int> icons;
    if (battler_ && battler_->is_alive()) icons = battler_->all_icons();
    if (!icons.empty()) {
        animation_index_++;
        if (animation_index_ >= (int)icons.size()) animation_index_ = 0;
        icon_index_ = icons[(size_t)animation_index_];
    } else {
        animation_index_ = 0;
        icon_index_ = 0;
    }
}

void SpriteStateIcon::update_frame() {
    if (!yparam_bool("BSCShowEnemyIcon")) return;
    double pw = icon_size(), ph = yparam_num("IconHeight", 32);
    double sx = icon_index_ % 16 * pw;
    double sy = std::floor(icon_index_ / 16) * ph;
    set_frame(sx, sy, pw, ph);
    if (turn_counter_sprite_) update_turn_and_counter();
}

void SpriteStateIcon::update_mirror() {
    if (!parent) return;
    if (parent->scale_x < 0) scale_x = -1 * std::fabs(scale_x);
    if (parent->scale_x > 0) scale_x = std::fabs(scale_x);
}

std::string SpriteStateIcon::text_color(int n) { return refs().status_base ? refs().status_base->text_color(n) : std::string("#ffffff"); }

void SpriteStateIcon::update_turn_and_counter() {
    turn_counter_sprite_->bitmap()->clear();
    if (!battler_) return;
    std::vector<const Value*> states;
    std::vector<int> buffs;
    std::vector<int> order;
    for (const Value* s : battler_->states()) {
        if (s && (*s)["iconIndex"].num() > 0) {
            states.push_back(s);
            order.push_back(-(int)states.size());
        }
    }
    for (int i = 0; i < 8; i++) {
        if (battler_->buff(i)) order.push_back(i);
    }
    if (order.empty()) return;
    if (animation_index_ < 0 || animation_index_ >= (int)order.size()) return;
    int entry = order[(size_t)animation_index_];
    if (entry >= 0) {
        if (yparam_bool("BSCEnemyBTurn")) {
            draw_buff_turns(entry);
            if (yparam_bool("BSCShowBuffRate")) draw_buff_rate(entry);
        }
    } else {
        const Value& state = *states[(size_t)(-entry - 1)];
        if (yparam_bool("BSCEnemyTurn")) draw_state_turns(state);
        if (yparam_bool("BSCEnemyCounter")) draw_state_counter(state);
    }
}

void SpriteStateIcon::draw_state_turns(const Value& state) {
    if (!state["showTurns"].truthy()) return;
    if (state["autoRemovalTiming"].num() <= 0) return;
    double turns = battler_->state_turns_of(state["id"].as_int());
    if (std::isnan(turns)) return;
    std::string text = group_text(std::ceil(turns));
    double wx = state["turnBufferX"].num();
    double wy = state["turnBufferY"].num() - 2;
    double ww = icon_size();
    double wh = yparam_num("LineHeight", 36);
    g2d::Bitmap& c = *turn_counter_sprite_->bitmap();
    c.font_size = state["turnFontSize"].as_int();
    c.text_color = text_color(state["turnColor"].as_int());
    c.draw_text(text, wx, wy, ww, wh, state["turnAlign"].string());
}

void SpriteStateIcon::draw_state_counter(const Value& state) {
    js::Val value = battler_->get_state_counter(state["id"].as_int());
    if (value.is_undef()) return;
    const Value& settings = state["stateCounterSettings"];
    std::string text = value.is_num() ? group_text(value.n) : value.to_str();
    double wx = settings["bufferX"].num();
    double wy = settings["bufferY"].num() - 2;
    double ww = icon_size();
    double wh = yparam_num("LineHeight", 36);
    g2d::Bitmap& c = *turn_counter_sprite_->bitmap();
    c.font_size = settings["size"].as_int();
    c.text_color = text_color(settings["color"].as_int());
    c.draw_text(text, wx, wy, ww, wh, settings["align"].string());
}

void SpriteStateIcon::draw_buff_turns(int param_id) {
    if (!yparam_bool("BSCShowTurns")) return;
    std::string text = group_text(std::ceil(battler_->buff_turns_of(param_id)));
    double wx = yparam_num("BSCTurnBufferX");
    double wy = yparam_num("BSCTurnBufferY") - 2;
    double ww = icon_size();
    double wh = yparam_num("LineHeight", 36);
    g2d::Bitmap& c = *turn_counter_sprite_->bitmap();
    c.font_size = (int)yparam_num("BSCFontSize", 16);
    if (battler_->is_buff_affected(param_id)) c.text_color = text_color((int)yparam_num("BSCBuffColor"));
    else c.text_color = text_color((int)yparam_num("BSCDebuffColor"));
    c.draw_text(text, wx, wy, ww, wh, yparam_str("BSCTurnAlign"));
}

void SpriteStateIcon::draw_buff_rate(int param_id) {
    if (!yparam_bool("BSCShowTurns")) return;
    double value = battler_->param_buff_rate(param_id);
    std::string text = js::number_to_string(std::floor(value * 100)) + "%";
    double bx = yparam_num("BSCCounterBufferX", 0);
    double by = yparam_num("BSCCounterBufferY", 8);
    double wx = bx;
    double wy = (by ? by : 8) - 2;
    double ww = icon_size();
    double wh = yparam_num("LineHeight", 36);
    g2d::Bitmap& c = *turn_counter_sprite_->bitmap();
    c.font_size = (int)(yparam_num("BSCFontSize", 16) * 0.75);
    c.text_color = text_color(0);
    c.draw_text(text, wx, wy, ww, wh, "center");
}

SpriteStateOverlay::SpriteStateOverlay() {
    anchor_x = 0.5;
    anchor_y = 1;
    set_bitmap(load_system("States"));
    set_frame(0, 0, 0, 0);
}

void SpriteStateOverlay::update() {
    SpriteBase::update();
    animation_count_++;
    if (animation_count_ >= 8) {
        update_pattern();
        update_frame();
        animation_count_ = 0;
    }
    update_mirror();
}

void SpriteStateOverlay::update_pattern() {
    pattern_ = (pattern_ + 1) % 8;
    if (battler_) overlay_index_ = battler_->state_overlay_index();
}

void SpriteStateOverlay::update_frame() {
    if (overlay_index_ > 0) set_frame(pattern_ * 96, (overlay_index_ - 1) * 96, 96, 96);
    else set_frame(0, 0, 0, 0);
}

void SpriteStateOverlay::update_mirror() {
    if (!parent) return;
    if (parent->scale_x < 0) scale_x = -1 * std::fabs(scale_x);
    if (parent->scale_x > 0) scale_x = std::fabs(scale_x);
}

SpriteWeapon::SpriteWeapon() {
    anchor_x = 0.5;
    anchor_y = 1;
    x = -16;
}

void SpriteWeapon::setup(int id) {
    weapon_image_id_ = id;
    animation_count_ = 0;
    pattern_ = 0;
    load_bitmap();
    update_frame();
    animation_count_ -= 1;
}

void SpriteWeapon::update() {
    SpriteBase::update();
    animation_count_++;
    if (animation_count_ >= 12) {
        update_pattern();
        update_frame();
        animation_count_ = 0;
    }
}

void SpriteWeapon::update_pattern() {
    pattern_++;
    if (pattern_ >= 3) weapon_image_id_ = 0;
}

void SpriteWeapon::load_bitmap() {
    int page = (int)std::floor((weapon_image_id_ - 1) / 12.0) + 1;
    if (page >= 1) set_bitmap(load_system("Weapons" + std::to_string(page)));
    else set_bitmap(load_system(""));
}

void SpriteWeapon::update_frame() {
    if (weapon_image_id_ > 0) {
        int index = (weapon_image_id_ - 1) % 12;
        double sx = (std::floor(index / 6) * 3 + pattern_) * 96;
        double sy = (index % 6) * 64;
        set_frame(sx, sy, 96, 64);
    } else {
        set_frame(0, 0, 0, 0);
    }
}

SpriteEnemyBattlerStatus::SpriteEnemyBattlerStatus() {
    set_bitmap(g2d::Bitmap::create(1, 1));
    g2d::BitmapPtr arrows = load_system("ACSArrows");
    cursor_sprite = std::make_shared<g2d::Sprite>(arrows);
    cursor_sprite->set_frame(0, 0, arrows->width() / 4.0, arrows->height());
    add_child(cursor_sprite);
    set_cursor_position(0);
}

void SpriteEnemyBattlerStatus::refresh_bitmap(rpg::GameEnemy* battler) {
    if (!battler) return;
    bitmap()->font_size = 24;
    std::string name = battler->name();
    double name_width = bitmap()->measure_text_width(name);
    set_bitmap(g2d::Bitmap::create((int)std::max(min_width(), name_width + 20), 57));
    g2d::Bitmap& b = *bitmap();
    b.font_size = 24;
    b.fill_all("rgba(255, 0, 0, 1)");
    g2d::BitmapPtr back = load_system("enemy_box");
    double w = width(), h = height();
    b.blt(back.get(), 0, 0, 7, back->height(), 0, 0);
    b.blt(back.get(), 7, 0, 1, back->height(), 7, 0, w - 14);
    b.blt(back.get(), 8, 0, 7, back->height(), w - 7, 0);
    b.draw_text(name, 0, 0, w, 27, "center");
    g2d::BitmapPtr bar = load_system("enemy_box_gradients");
    g2d::BitmapPtr icon = load_system("hp_icon");
    double sx = (w - (bar->width() + icon->width())) / 2;
    double bar_height = bar->height() / 2.0;
    b.blt(icon.get(), 0, 0, icon->width(), icon->height(), sx, h - (icon->height() + 7));
    sx += icon->width();
    b.blt(bar.get(), 0, bar_height, bar->width(), bar_height, sx, h - (bar_height + 9));
    double rate_w = battler->hp_rate() * bar->width();
    if (rate_w > 0) b.blt(bar.get(), 0, 0, rate_w, bar_height, sx, h - (bar_height + 9));
    if (battler->is_scanned()) {
        std::string text = js::number_to_string(battler->hp()) + "/" + js::number_to_string(battler->mhp());
        b.draw_text(text, sx, h - 29, bar->width() - 3, 20, "right");
    }
    set_cursor_position((int)battler->battle_status_cursor_position());
}

void SpriteEnemyBattlerStatus::set_cursor_position(int position) {
    double fx = 0;
    double cw = cursor_sprite->width(), ch = cursor_sprite->height();
    switch (position) {
    case 0:
        fx = 3 * cw;
        cursor_sprite->x = (width() - cw) / 2;
        cursor_sprite->y = -ch + 4;
        break;
    case 1:
        fx = 2 * cw;
        cursor_sprite->x = -cw + 4;
        cursor_sprite->y = (height() - ch) / 2;
        break;
    case 2:
        fx = 1 * cw;
        cursor_sprite->x = width() - 5;
        cursor_sprite->y = (height() - ch) / 2;
        break;
    case 3:
        fx = 0;
        cursor_sprite->x = (width() - cw) / 2;
        cursor_sprite->y = height() - 4;
        break;
    }
    const g2d::Rect& f = cursor_sprite->frame();
    cursor_sprite->set_frame(fx, f.y, f.width, f.height);
}

const MotionDef* actor_motion(const std::string& name) {
    static const std::pair<const char*, MotionDef> table[] = {
        {"walk", {0, true}},     {"wait", {1, true}},    {"chant", {2, true}},  {"guard", {3, true}},     {"damage", {4, false}},
        {"evade", {5, false}},   {"thrust", {6, false}}, {"swing", {7, false}}, {"missile", {8, false}},   {"skill", {9, false}},
        {"spell", {10, false}},  {"item", {11, false}},  {"escape", {12, true}}, {"victory", {13, true}},  {"dying", {14, true}},
        {"abnormal", {15, true}}, {"sleep", {16, true}}, {"dead", {17, true}}};
    for (auto& e : table) {
        if (name == e.first) return &e.second;
    }
    return nullptr;
}

SpriteBattler::SpriteBattler() {}

SpriteBattler::~SpriteBattler() { release_link(); }

void SpriteBattler::release_link() {
    if (battler_ && battler_->sprite_link == this) battler_->sprite_link = nullptr;
}

void SpriteBattler::init_members() {
    anchor_x = 0.5;
    anchor_y = 1;
    battler_ = nullptr;
    damages.clear();
    home_x_ = home_y_ = offset_x_ = offset_y_ = 0;
    has_target_offset_ = false;
    movement_duration_ = 0;
    selection_effect_count_ = 0;
    anchor_x = 0.5;
    anchor_y = 1.0;
    z = 1;
    reset_float();
    setup_jump(0, 0);
    reset_opacity();
}

void SpriteBattler::set_battler(rpg::GameBattler* b) {
    if (battler_ != b) release_link();
    battler_ = b;
    if (b) b->sprite_link = this;
    if (!b) return;
    const Value& d = b->is_enemy() ? static_cast<rpg::GameEnemy*>(b)->enemy() : static_cast<rpg::GameActor*>(b)->actor();
    const Value* zi = d["meta"].find("ZIndex");
    if (zi && zi->truthy()) z = rpg::js_parse_int(rpg::trim(data::js_string(*zi)));
}

void SpriteBattler::set_home(double hx, double hy) {
    home_x_ = hx;
    home_y_ = hy;
    update_position();
}

void SpriteBattler::update() {
    horror_.sync(*this, battler_ ? &battler_->horror_filters : nullptr, true);
    g2d::Sprite::update();
    horror_.update(*this);
    update_visibility();
    update_animation_sprites();
    if (battler_) {
        update_main();
        update_animation();
        update_damage_popup();
        update_selection_effect();
    } else {
        set_bitmap(nullptr);
    }
    post_initialized_ = true;
    if (battler_) {
        update_float();
        update_state_sprites();
        update_weapon();
        update_opacity();
    }
}

void SpriteBattler::update_visibility() {
    SpriteBase::update_visibility();
    if (!battler_ || !sprite_visible(battler_)) visible = false;
}

void SpriteBattler::update_main() {
    if (sprite_visible(battler_)) {
        update_bitmap();
        update_frame();
    }
    update_move();
    update_position();
}

void SpriteBattler::update_move() {
    if (movement_duration_ > 0) {
        double d = movement_duration_;
        offset_x_ = (offset_x_ * (d - 1) + target_offset_x_) / d;
        offset_y_ = (offset_y_ * (d - 1) + target_offset_y_) / d;
        movement_duration_--;
        if (movement_duration_ == 0) on_move_end();
    }
}

void SpriteBattler::update_position() {
    x = home_x_ + offset_x_;
    y = home_y_ + offset_y_;
}

void SpriteBattler::update_animation() { setup_animation(); }

void SpriteBattler::setup_animation() {
    while (battler_->is_animation_requested()) {
        rpg::GameBattler::Anim data = battler_->shift_animation();
        const Value& animation = data::animations()[data.id];
        if (!animation.is_obj()) continue;
        double delay = animation["position"].as_int() == 3 ? 0 : data.delay;
        start_animation(&animation, data.mirror, delay);
        for (auto& s : animation_sprites_) s->visible = sprite_visible(battler_);
    }
}

void SpriteBattler::setup_damage_popup() {
    if (battler_->is_damage_popup_requested()) {
        if (sprite_visible(battler_)) {
            auto sprite = std::make_shared<SpriteDamage>();
            sprite->x = x + damage_offset_x();
            sprite->y = y + damage_offset_y();
            sprite->setup(battler_);
            push_damage_sprite(sprite);
            if (refs().scene_node) refs().scene_node->add_child(sprite);
        }
    } else {
        battler_->clear_damage_popup();
    }
}

void SpriteBattler::update_damage_popup() {
    setup_damage_popup();
    if (!damages.empty()) {
        std::vector<std::shared_ptr<SpriteDamage>> list = damages;
        for (auto& d : list) d->update();
        if (!damages.front()->is_playing()) {
            if (damages.front()->parent) damages.front()->parent->remove_child(damages.front().get());
            damages.erase(damages.begin());
        }
    }
}

void SpriteBattler::push_damage_sprite(const std::shared_ptr<SpriteDamage>& sprite) {
    double buffer = yparam_num("BECPopupOverlap", 0.9);
    if (yparam_bool("BECNewPopBottom")) {
        damages.push_back(sprite);
        for (auto& spr : damages) {
            for (auto& c : spr->children) static_cast<g2d::Sprite*>(c.get())->anchor_y += buffer;
        }
    } else {
        damages.push_back(sprite);
        buffer *= damages.size();
        for (auto& c : sprite->children) static_cast<g2d::Sprite*>(c.get())->anchor_y += buffer;
    }
}

void SpriteBattler::update_selection_effect() {
    g2d::Sprite* target = effect_target ? effect_target : this;
    static const double on[4] = {255, 255, 255, 64};
    static const double off[4] = {0, 0, 0, 0};
    if (battler_->is_selected()) {
        selection_effect_count_++;
        target->set_blend_color(selection_effect_count_ % 30 < 15 ? on : off);
    } else if (selection_effect_count_ > 0) {
        selection_effect_count_ = 0;
        target->set_blend_color(off);
    }
}

void SpriteBattler::start_move(double tx, double ty, int duration) {
    if (battler_ && !battler_->sprite_can_move()) return;
    if (!has_target_offset_ || target_offset_x_ != tx || target_offset_y_ != ty) {
        has_target_offset_ = true;
        target_offset_x_ = tx;
        target_offset_y_ = ty;
        movement_duration_ = duration;
        if (duration == 0) {
            offset_x_ = tx;
            offset_y_ = ty;
        }
    }
}

bool SpriteBattler::is_popup_playing() const {
    if (!damages.empty()) return damages.front()->is_playing();
    return false;
}

void SpriteBattler::reset_float() {
    float_height_ = 0;
    float_target_ = 0;
    float_dur_ = 0;
    float_rate_ = 0;
}

void SpriteBattler::reset_opacity() {
    opacity_target_ = 255;
    opacity_dur_ = 0;
    opacity_rate_ = 0;
    opacity_changing_ = false;
}

void SpriteBattler::setup_float(double h, double dur) {
    dur = std::max(1.0, dur);
    float_target_ = h;
    float_dur_ = dur;
    float_rate_ = std::fabs(float_height_ - h) / dur;
}

void SpriteBattler::setup_jump(double h, double dur) {
    jump_height_ = h;
    jump_dur_ = dur;
    jump_full_ = dur;
}

void SpriteBattler::setup_opacity_change(double target, double dur) {
    dur = std::max(1.0, dur);
    opacity_target_ = target;
    opacity_dur_ = dur;
    opacity_rate_ = std::fabs(opacity() - target) / dur;
    opacity_changing_ = true;
}

void SpriteBattler::update_float() {
    if (!battler_) return;
    if (float_dur_ > 0) float_dur_--;
    if (jump_dur_ > 0) jump_dur_--;
    double base_y = battler_->anchor_y();
    double fh = get_float_height();
    double jh = get_jump_height();
    double h = fh + jh;
    if (main_sprite && main_sprite->bitmap()) {
        double rate = battler_->sprite_height() / main_sprite->height();
        double v = base_y + h * rate;
        if (std::isfinite(v)) {
            main_sprite->anchor_y = v;
            if (weapon_sprite) weapon_sprite->anchor_y = main_sprite->anchor_y;
        }
    } else {
        anchor_y = base_y + h;
    }
}

void SpriteBattler::update_state_sprites() {
    double icon_h = yparam_num("IconHeight", 32);
    if (state_icon_sprite) {
        double h = battler_->sprite_height() * -1;
        h -= icon_h;
        h /= scale_y;
        state_icon_sprite->y = h;
    }
    if (state_sprite) state_sprite->y = (battler_->sprite_height() - 64 * scale_y) * -1;
    double rate = 0;
    rate += get_float_height();
    rate += get_jump_height();
    if (battler_->is_enemy() && static_cast<rpg::GameEnemy*>(battler_)->is_floating()) rate += add_floating_height();
    double h = battler_->sprite_height();
    if (state_icon_sprite) state_icon_sprite->y += std::ceil(rate * -h);
    if (state_sprite) state_sprite->y += std::ceil(rate * -h);
}

void SpriteBattler::update_weapon() {}

void SpriteBattler::update_opacity() {
    if (anti_opacity_change()) return;
    opacity_dur_--;
    if (opacity_dur_ <= 0) {
        if (opacity() != opacity_target_) set_opacity(opacity_target_);
        opacity_changing_ = false;
    } else {
        double target = opacity_target_, rate = opacity_rate_;
        if (opacity() >= target) set_opacity(std::max(target, opacity() - rate));
        else set_opacity(std::min(target, opacity() + rate));
    }
}

double SpriteBattler::get_float_height() {
    if (float_dur_ <= 0) {
        float_height_ = float_target_;
    } else {
        if (float_height_ >= float_target_) float_height_ = std::max(float_target_, float_height_ - float_rate_);
        else float_height_ = std::min(float_target_, float_height_ + float_rate_);
    }
    return float_height_ - add_floating_height();
}

double SpriteBattler::get_jump_height() const {
    if (jump_dur_ <= 0) return 0;
    double xx = jump_full_ - jump_dur_;
    double hh = jump_full_ / 2;
    double kk = jump_height_;
    double a = -kk / std::pow(hh, 2);
    return a * std::pow(xx - hh, 2) + kk;
}

double SpriteBattler::link_bitmap_width() const {
    g2d::BitmapPtr b = bitmap();
    return b ? b->width() : 1;
}

double SpriteBattler::link_bitmap_height() const {
    g2d::BitmapPtr b = bitmap();
    return b ? b->height() : 1;
}

double SpriteBattler::link_float_height() const { return const_cast<SpriteBattler*>(this)->get_float_height(); }

void SpriteBattler::link_transform(rpg::GameBattler* b) {
    set_transform(b);
    set_battler(b);
}

js::Val SpriteBattler::link_js() { return display_js(this); }

bool SpriteBattler::display_js_get(uint32_t key, js::Val& out) {
    const std::string& n = js::atom_name(key);
    if (n == "_battler" || n == "_enemy" || n == "_actor") {
        out = battler_ ? battler_->to_js() : js::Val::null();
    } else if (n == "_mainSprite") {
        out = display_js(main_sprite.get());
    } else if (n == "_homeX") {
        out = js::Val::num(home_x_);
    } else if (n == "_homeY") {
        out = js::Val::num(home_y_);
    } else if (n == "_offsetX") {
        out = js::Val::num(offset_x_);
    } else if (n == "_offsetY") {
        out = js::Val::num(offset_y_);
    } else if (n == "_stateIconSprite") {
        out = display_js(state_icon_sprite.get());
    } else if (n == "_stateSprite") {
        out = display_js(state_sprite.get());
    } else if (n == "_weaponSprite") {
        out = display_js(weapon_sprite.get());
    } else if (n == "_shadowSprite") {
        out = display_js(shadow_sprite.get());
    } else if (n == "_appeared") {
        if (auto* e = dynamic_cast<SpriteEnemy*>(this)) out = js::Val::boolean(e->appeared);
        else return false;
    } else if (n == "_statusSprite") {
        if (auto* e = dynamic_cast<SpriteEnemy*>(this)) out = display_js(e->status_sprite.get());
        else return false;
    } else if (n == "isEffecting") {
        std::weak_ptr<g2d::DisplayObject> w = weak_from_this();
        out = js::make_native([w](js::Vm&, const js::Val&, std::vector<js::Val>&) {
            auto o = w.lock();
            return js::Val::boolean(o && static_cast<SpriteBattler*>(o.get())->is_effecting());
        });
    } else {
        return false;
    }
    return true;
}

SpriteActor::SpriteActor() {
    init_members();
    set_battler(nullptr);
    move_to_start_position();
}

void SpriteActor::init_members() {
    SpriteBattler::init_members();
    battler_name_.clear();
    motion_ = nullptr;
    motion_count_ = 0;
    pattern_ = 0;
    create_shadow_sprite();
    create_weapon_sprite();
    create_main_sprite();
    create_state_sprite();
    motion_name_.clear();
}

void SpriteActor::create_main_sprite() {
    main_sprite = std::make_shared<SpriteBase>();
    main_sprite->anchor_x = 0.5;
    main_sprite->anchor_y = 1;
    add_child(main_sprite);
    effect_target = main_sprite.get();
}

void SpriteActor::create_shadow_sprite() {
    shadow_sprite = std::make_shared<g2d::Sprite>(load_system("Shadow2"));
    shadow_sprite->anchor_x = 0.5;
    shadow_sprite->anchor_y = 0.5;
    shadow_sprite->y = -2;
    add_child(shadow_sprite);
}

void SpriteActor::create_weapon_sprite() {
    weapon_sprite = std::make_shared<SpriteWeapon>();
    add_child(weapon_sprite);
}

void SpriteActor::create_state_sprite() {
    state_sprite = std::make_shared<SpriteStateOverlay>();
    add_child(state_sprite);
}

void SpriteActor::set_battler(rpg::GameBattler* b) {
    SpriteBattler::set_battler(b);
    rpg::GameActor* a = static_cast<rpg::GameActor*>(b);
    bool changed = a != actor_;
    if (changed) {
        actor_ = a;
        if (a) set_actor_home(a->index());
        start_entry_motion();
        state_sprite->setup(a);
    }
}

void SpriteActor::move_to_start_position() {
    if (rpg::battle::st().bypass_move_to_start_location) return;
    if (side_view() && check_alive_status_) start_move(300, 0, 0);
}

void SpriteActor::set_actor_home(int index) {
    double screen_width = 640, screen_height = 480;
    double max_size = rpg::game_party().max_battle_members();
    double party_size = (double)rpg::game_party().battle_members().size();
    double status_height = 0;
    {
        auto prog = js::compile_cached(yparam_str("BECCommandRows"));
        if (prog) {
            rpg::JsEval e{js::Val()};
            if (e.run(prog.get(), "BECCommandRows")) status_height = e.completion().to_num();
        }
    }
    status_height *= yparam_num("LineHeight", 36);
    status_height += yparam_num("WindowPadding", 18) * 2;
    bool side = side_view();
    auto eval = [&](const char* key, const char* where) {
        auto prog = js::compile_cached(yparam_str(key));
        if (!prog) return 0.0;
        rpg::JsEval e{js::Val()};
        e.num("screenWidth", screen_width).num("screenHeight", screen_height).num("maxSize", max_size).num("partySize", party_size);
        e.num("statusHeight", status_height).num("index", index);
        if (!e.run(prog.get(), where)) return 0.0;
        return e.completion().to_num();
    };
    double hx = side ? eval("BECHomePosX", "SIDE VIEW HOME X FORMULA ERROR") : eval("BECFrontPosX", "FRONT VIEW HOME X FORMULA ERROR");
    double hy = side ? eval("BECHomePosY", "SIDE VIEW HOME Y FORMULA ERROR") : eval("BECFrontPosY", "FRONT VIEW HOME Y FORMULA ERROR");
    check_alive_status_ = false;
    auto members = rpg::game_party().battle_members();
    if (index >= 0 && index < (int)members.size() && members[(size_t)index]->is_alive()) check_alive_status_ = true;
    set_home(hx, hy);
    move_to_start_position();
}

void SpriteActor::update() {
    SpriteBattler::update();
    update_shadow();
    if (actor_) update_motion();
}

void SpriteActor::update_shadow() {
    if (!yparam_bool("BECShowShadows")) {
        shadow_sprite->visible = false;
        return;
    }
    shadow_sprite->visible = actor_ != nullptr;
}

void SpriteActor::update_main() {
    SpriteBattler::update_main();
}

void SpriteActor::update_bitmap() {
    std::string name = actor_->battler_name();
    bool need = battler_name_ != name;
    if (need) {
        battler_name_ = name;
        const Value& sv = actor_->actor()["_sideviewBattler"];
        std::string file = name;
        if (sv.is_obj() && !sv["filename"].string().empty() && !sv["default"].truthy()) file = sv["filename"].string();
        main_sprite->set_bitmap(load_folder("sv_actors", file));
        adjust_anchor();
    }
}

const Value* SpriteActor::current_sideview_motion() {
    if (motion_name_.empty()) return nullptr;
    const Value& sv = actor_->actor()["_sideviewBattler"];
    const Value& motions = sv["motions"];
    const Value* m = motions.find(motion_name_.c_str());
    if (m && m->is_obj()) return m;
    const Value* other = motions.find("other");
    if (other && other->truthy()) return other;
    const Value* walk = motions.find("walk");
    return walk && walk->is_obj() ? walk : nullptr;
}

static bool actor_sideview(rpg::GameActor* a) {
    const Value& sv = a->actor()["_sideviewBattler"];
    return sv.is_obj() && !sv["filename"].string().empty() && !sv["default"].truthy();
}

int SpriteActor::sideview_frames() {
    const Value& sv = actor_->actor()["_sideviewBattler"];
    const Value* m = current_sideview_motion();
    if (m && (*m)["frames"].truthy()) return (*m)["frames"].as_int();
    return sv["frames"].as_int();
}

void SpriteActor::update_frame() {
    if (actor_sideview(actor_)) {
        update_sideview_frame();
        return;
    }
    g2d::BitmapPtr b = main_sprite->bitmap();
    if (b) {
        int motion_index = motion_ ? motion_->index : 0;
        int pattern = pattern_ < 3 ? pattern_ : 1;
        double cw = b->width() / 9.0, ch = b->height() / 6.0;
        double cx = std::floor(motion_index / 6) * 3 + pattern;
        double cy = motion_index % 6;
        main_sprite->set_frame(cx * cw, cy * ch, cw, ch);
    }
    if (!main_sprite->bitmap()) return;
    if (main_sprite->bitmap()->width() > 0 && !bitmap()) {
        set_bitmap(g2d::Bitmap::create((int)(main_sprite->bitmap()->width() / 9.0), (int)(main_sprite->bitmap()->height() / 6.0)));
    }
}

void SpriteActor::update_sideview_frame() {
    g2d::BitmapPtr b = main_sprite->bitmap();
    const Value* motion = current_sideview_motion();
    const Value& sizes = actor_->actor()["_sideviewBattler"]["sizes"];
    if (b && motion) {
        double cw = sizes[0].num(), ch = sizes[1].num();
        main_sprite->set_frame(pattern_ * cw, (*motion)["index"].num() * ch, cw, ch);
    }
}

void SpriteActor::update_move() {
    SpriteBattler::update_move();
}

void SpriteActor::update_motion() { update_motion_count(); }

int SpriteActor::motion_speed() {
    if (actor_sideview(actor_)) {
        const Value* m = current_sideview_motion();
        if (m && (*m)["speed"].truthy()) return (*m)["speed"].as_int();
        return actor_->actor()["_sideviewBattler"]["speed"].as_int();
    }
    return 12;
}

void SpriteActor::update_motion_count() {
    if (actor_sideview(actor_)) {
        update_sideview_motion_count();
        return;
    }
    if (motion_ && ++motion_count_ >= motion_speed()) {
        if (motion_->loop) pattern_ = (pattern_ + 1) % 4;
        else if (pattern_ < 2) pattern_++;
        else refresh_motion();
        motion_count_ = 0;
    }
}

void SpriteActor::update_sideview_motion_count() {
    const Value* motion = current_sideview_motion();
    int speed = motion_speed(), frames = sideview_frames();
    if (motion && ++motion_count_ >= speed) {
        if ((*motion)["loop"].truthy()) pattern_ = frames ? (pattern_ + 1) % frames : 0;
        else if (pattern_ < frames - 1) pattern_++;
        else refresh_motion();
        motion_count_ = 0;
    }
}

void SpriteActor::refresh_motion() {
    rpg::GameActor* a = actor_;
    if (!a) return;
    if (motion_ == actor_motion("guard") && !rpg::battle::is_inputting()) return;
    int state_motion = a->state_motion_index();
    if (a->is_inputting() || a->is_acting()) start_motion(a->idle_motion());
    else if (state_motion == 3) start_motion(a->dead_motion());
    else if (state_motion == 2) start_motion(a->sleep_motion());
    else if (a->is_chanting()) start_motion(a->chant_motion());
    else if (a->is_guard() || a->is_guard_waiting()) start_motion(a->guard_motion());
    else if (state_motion == 1) start_motion(a->abnormal_motion());
    else if (a->is_dying()) start_motion(a->dying_motion());
    else if (a->is_undecided()) start_motion(a->idle_motion());
    else start_motion(a->wait_motion());
}

void SpriteActor::start_entry_motion() {
    if (actor_ && actor_->can_move()) {
        start_motion("walk");
        start_move(0, 0, 30);
    } else if (!is_moving()) {
        refresh_motion();
        start_move(0, 0, 0);
    }
}

void SpriteActor::start_motion(const std::string& motion) {
    if (actor_ && actor_sideview(actor_)) {
        if (motion_name_ != motion) {
            motion_name_ = motion;
            motion_count_ = 0;
            pattern_ = 0;
        }
        return;
    }
    const MotionDef* m = actor_motion(motion);
    if (motion_ != m) {
        motion_ = m;
        motion_count_ = 0;
        pattern_ = 0;
    }
}

void SpriteActor::force_motion(const std::string& motion) {
    if (actor_ && actor_sideview(actor_)) {
        motion_name_ = motion;
        motion_count_ = 0;
        pattern_ = 0;
        return;
    }
    motion_ = actor_motion(motion);
    motion_count_ = 0;
    pattern_ = 0;
}

void SpriteActor::setup_weapon_animation() {
    if (!actor_) return;
    bool use = !actor_sideview(actor_) || actor_->actor()["_sideviewBattler"]["weapon"].truthy();
    if (use) {
        if (actor_->is_weapon_animation_requested()) {
            weapon_sprite->setup(actor_->weapon_image_id);
            actor_->clear_weapon_animation();
        }
        return;
    }
    actor_->clear_weapon_animation();
}

void SpriteActor::adjust_anchor() {
    if (!main_sprite) return;
    main_sprite->anchor_x = actor_->anchor_x();
    main_sprite->anchor_y = actor_->anchor_y();
}

void SpriteActor::update_weapon() {
    if (!battler_ || !battler_->is_actor()) return;
    weapon_sprite->anchor_y = main_sprite->anchor_y;
}

SpriteEnemy::SpriteEnemy(std::shared_ptr<rpg::GameEnemy> enemy) : enemy_sp_(std::move(enemy)) {
    init_members();
    set_battler(enemy_sp_.get());
}

void SpriteEnemy::init_members() {
    SpriteBattler::init_members();
    enemy_ = nullptr;
    appeared = false;
    battler_name_.clear();
    has_battler_name_ = false;
    battler_hue_ = 0;
    has_effect_type_ = false;
    effect_type_.clear();
    effect_duration = 0;
    shake_ = 0;
    state_icon_sprite = std::make_shared<SpriteStateIcon>();
    add_child(state_icon_sprite);
    init_sv_sprites();
    motion_name_.clear();
    status_sprite = std::make_shared<SpriteEnemyBattlerStatus>();
    status_sprite->z = 2;
    fall_on_death_ = false;
    has_fall_locked_pattern_ = false;
}

void SpriteEnemy::init_sv_sprites() {
    for (auto& c : sv_children_) remove_child(c.get());
    sv_children_.clear();
    sv_rand_ = random_unit() * 10000;
    sv_battler_name_.clear();
    motion_ = nullptr;
    motion_count_ = 0;
    pattern_ = 0;
    sv_battler_enabled_ = false;
    create_shadow_sprite();
    create_weapon_sprite();
    create_main_sprite();
    create_state_sprite();
    effect_target = this;
}

void SpriteEnemy::create_shadow_sprite() {
    if (shadow_sprite) remove_child(shadow_sprite.get());
    g2d::BitmapPtr b = load_system("Shadow2");
    if (yparam_bool("SVESmoothing")) b->smooth = true;
    shadow_sprite = std::make_shared<g2d::Sprite>(b);
    shadow_sprite->anchor_x = 0.5;
    shadow_sprite->anchor_y = 0.5;
    shadow_sprite->y = -2;
    add_child(shadow_sprite);
    shadow_sprite->set_opacity(0);
    sv_children_.push_back(shadow_sprite);
}

void SpriteEnemy::create_weapon_sprite() {
    if (weapon_sprite) remove_child(weapon_sprite.get());
    weapon_sprite = std::make_shared<SpriteWeapon>();
    add_child(weapon_sprite);
    sv_children_.push_back(weapon_sprite);
}

void SpriteEnemy::create_main_sprite() {
    if (main_sprite) remove_child(main_sprite.get());
    main_sprite = std::make_shared<SpriteBase>();
    main_sprite->anchor_x = 0.5;
    main_sprite->anchor_y = 1;
    add_child(main_sprite);
    effect_target = main_sprite.get();
    sv_children_.push_back(main_sprite);
}

void SpriteEnemy::create_state_sprite() {
    if (state_sprite) remove_child(state_sprite.get());
    state_sprite = std::make_shared<SpriteStateOverlay>();
    add_child(state_sprite);
    sv_children_.push_back(state_sprite);
}

void SpriteEnemy::set_transform(rpg::GameBattler* b) {
    shadow_sprite->set_opacity(0);
    weapon_sprite->set_opacity(0);
    main_sprite->set_opacity(0);
    state_sprite->set_opacity(0);
    rpg::GameEnemy* e = static_cast<rpg::GameEnemy*>(b);
    if (e->sv_battler_name_value().truthy()) {
        create_shadow_sprite();
        create_weapon_sprite();
        create_main_sprite();
        create_state_sprite();
        shadow_sprite->set_opacity(255);
        weapon_sprite->set_opacity(255);
        main_sprite->set_opacity(255);
        state_sprite->set_opacity(255);
    }
}

void SpriteEnemy::set_battler(rpg::GameBattler* b) {
    sv_battler_enabled_ = false;
    init_sv_sprites();
    SpriteBattler::set_battler(b);
    enemy_ = static_cast<rpg::GameEnemy*>(b);
    if (enemy_) {
        for (auto& e : rpg::game_troop().enemies) {
            if (e.get() == enemy_) enemy_sp_ = e;
        }
        for (auto& e : rpg::game_troop().new_enemies) {
            if (e.get() == enemy_) enemy_sp_ = e;
        }
    }
    set_home(enemy_->screen_x(), enemy_->screen_y());
    state_icon_sprite->setup(enemy_);
    js::Val* ay = enemy_->expando.find(js::atom("_alteredScreenY"));
    if (!ay || !ay->truthy()) {
        home_y_ += std::floor((480.0 - 624) / 2);
        enemy_->screen_y_ = home_y_;
        enemy_->expando.set(js::atom("_alteredScreenY"), js::Val::boolean(true));
    }
    if (!side_view()) {
        js::Val* ax = enemy_->expando.find(js::atom("_alteredScreenX"));
        if (!ax || !ax->truthy()) {
            home_x_ += (640.0 - 816) / 2;
            enemy_->screen_x_ = home_x_;
            enemy_->expando.set(js::atom("_alteredScreenX"), js::Val::boolean(true));
        }
    }
    if (!enemy_->sv_battler_name_value().is_undef()) {
        adjust_main_bitmap_settings_ = false;
        sv_battler_enabled_ = true;
        if (state_sprite) state_sprite->setup(enemy_);
    }
    fall_on_death_ = enemy_->enemy()["meta"]["FallOnDeath"].truthy();
    has_fall_locked_pattern_ = false;
}

void SpriteEnemy::update() {
    SpriteBattler::update();
    if (enemy_) {
        update_effect();
        update_state_sprite();
    }
    if (sv_battler_enabled_) update_motion();
    update_breathing();
    update_status_sprite();
}

bool SpriteEnemy::is_sideview_battler() const {
    const Value& sv = enemy_->enemy()["_sideviewBattler"];
    return sv.is_obj() && !sv["filename"].string().empty() && !sv["default"].truthy();
}

const Value& SpriteEnemy::sideview_data() const { return enemy_->enemy()["_sideviewBattler"]; }

const Value* SpriteEnemy::current_sideview_motion() const {
    if (motion_name_.empty() || !is_sideview_battler()) return nullptr;
    const Value& motions = sideview_data()["motions"];
    const Value* m = motions.find(motion_name_.c_str());
    if (m && m->truthy()) return m;
    const Value* other = motions.find("other");
    if (other && other->truthy()) return other;
    const Value* walk = motions.find("walk");
    return walk && walk->truthy() ? walk : nullptr;
}

void SpriteEnemy::load_bitmap(const std::string& name, int hue) {
    if (side_view()) set_bitmap(load_hued("sv_enemies", name, hue, false));
    else set_bitmap(load_hued("enemies", name, hue, false));
}

void SpriteEnemy::update_bitmap() {
    std::string name = enemy_->battler_name();
    int hue = enemy_->battler_hue();
    if (!has_battler_name_ || battler_name_ != name || battler_hue_ != hue) {
        has_battler_name_ = true;
        battler_name_ = name;
        battler_hue_ = hue;
        load_bitmap(name, hue);
        init_visibility();
    }
    if (!sv_battler_enabled_) update_scale();
    update_sv_bitmap();
    adjust_anchor();
}

void SpriteEnemy::update_sv_bitmap() {
    js::Val nv = enemy_->sv_battler_name_value();
    std::string name = nv.is_undef() ? std::string() : nv.to_str();
    if (sv_battler_enabled_ && sv_battler_name_ != name) {
        created_empty_main_sprite_ = false;
        sv_battler_name_ = name;
        g2d::BitmapPtr b = load_folder("sv_actors", name);
        if (yparam_bool("SVESmoothing")) b->smooth = true;
        main_sprite->set_bitmap(b);
        adjust_anchor();
        refresh_motion();
        update_scale();
    } else if (sv_battler_name_.empty()) {
        sv_battler_enabled_ = false;
        if (created_empty_main_sprite_) return;
        created_empty_main_sprite_ = true;
        main_sprite = std::make_shared<SpriteBase>();
        main_sprite->anchor_x = 0.5;
        main_sprite->anchor_y = 1;
    }
}

void SpriteEnemy::adjust_anchor() {
    if (!main_sprite) return;
    main_sprite->anchor_x = enemy_->enemy()["sideviewAnchorX"].num();
    main_sprite->anchor_y = enemy_->enemy()["sideviewAnchorY"].num();
}

void SpriteEnemy::update_scale() {
    scale_x = enemy_->sprite_scale_x();
    scale_y = enemy_->sprite_scale_y();
    if (state_icon_sprite) {
        double safe = 1.0 / 100000;
        state_icon_sprite->scale_x = 1 / std::max(safe, std::fabs(scale_x));
        state_icon_sprite->scale_y = 1 / std::max(safe, std::fabs(scale_y));
    }
}

void SpriteEnemy::update_frame() {
    if (is_sideview_battler()) {
        update_sideview_frame();
        return;
    }
    if (sv_battler_enabled_) {
        update_sv_frame();
        return;
    }
    g2d::BitmapPtr b = bitmap();
    if (!b) return;
    double fh = b->height();
    if (effect_type_ == "bossCollapse" && has_effect_type_) fh = effect_duration;
    set_frame(0, 0, b->width(), fh);
}

void SpriteEnemy::update_sideview_frame() {
    g2d::BitmapPtr b = main_sprite->bitmap();
    const Value* motion = current_sideview_motion();
    const Value& sizes = sideview_data()["sizes"];
    if (!b || b->width() <= 0) return;
    effect_target = main_sprite.get();
    if (!motion) return;
    double cw = sizes[0].num(), ch = sizes[1].num();
    double cdh = 0;
    if (has_effect_type_ && effect_type_ == "bossCollapse") cdh = ch - effect_duration;
    main_sprite->set_frame(pattern_ * cw, (*motion)["index"].num() * ch, cw, ch - cdh);
    adjust_main_bitmap_settings(b.get());
    adjust_sv_shadow_settings();
}

void SpriteEnemy::update_sv_frame() {
    g2d::BitmapPtr b = main_sprite->bitmap();
    if (!b || b->width() <= 0) return;
    effect_target = main_sprite.get();
    int motion_index = motion_ ? motion_->index : 0;
    int pattern = pattern_ < 3 ? pattern_ : 1;
    double cw = b->width() / 9.0, ch = b->height() / 6.0;
    double cx = std::floor(motion_index / 6) * 3 + pattern;
    double cy = motion_index % 6;
    double cdh = 0;
    if (has_effect_type_ && effect_type_ == "bossCollapse") cdh = ch - effect_duration;
    main_sprite->set_frame(cx * cw, cy * ch, cw, ch - cdh);
    adjust_main_bitmap_settings(b.get());
    adjust_sv_shadow_settings();
}

void SpriteEnemy::adjust_main_bitmap_settings(g2d::Bitmap* b) {
    if (adjust_main_bitmap_settings_) return;
    adjust_main_bitmap_settings_ = true;
    js::Val w = enemy_->sideview_width(), h = enemy_->sideview_height();
    double svw = (w.is_str() && w.to_str() == "auto") ? b->width() / 9.0 : w.to_num();
    double svh = (h.is_str() && h.to_str() == "auto") ? b->height() / 6.0 : h.to_num();
    svw = std::floor(std::fabs(svw * enemy_->sprite_scale_x()));
    svh = std::floor(std::fabs(svh * enemy_->sprite_scale_y()));
    set_bitmap(g2d::Bitmap::create((int)svw, (int)svh));
}

void SpriteEnemy::adjust_sv_shadow_settings() {
    const Value& e = enemy_->enemy();
    if (e["sideviewShadowShow"].truthy()) shadow_sprite->set_opacity(255);
    const Value& sx = e["sideviewShadowScaleX"];
    const Value& sy = e["sideviewShadowScaleY"];
    g2d::BitmapPtr mb = main_sprite->bitmap();
    double mw = mb ? mb->width() : 0;
    shadow_sprite->scale_x = (sx.is_str() && sx.string() == "auto") ? mw / 9 / 64 : sx.num();
    shadow_sprite->scale_y = (sy.is_str() && sy.string() == "auto") ? mw / 9 / 64 : sy.num();
}

void SpriteEnemy::update_position() {
    SpriteBattler::update_position();
    x += shake_;
}

void SpriteEnemy::update_state_sprite() {
    if (enemy_->has_sv_battler()) {
        update_sv_state_sprite();
    } else {
        g2d::BitmapPtr b = bitmap();
        double bh = b ? b->height() : 0;
        state_icon_sprite->y = -std::round((bh + 40) * 0.9);
        if (state_icon_sprite->y < 20 - y) state_icon_sprite->y = 20 - y;
    }
    update_floating_state_sprite();
}

void SpriteEnemy::update_sv_state_sprite() {
    if (!state_sprite) return;
    state_sprite->visible = enemy_->enemy()["sideviewStateOverlay"].truthy();
}

void SpriteEnemy::update_floating_state_sprite() {
    if (enemy_ && enemy_->is_floating()) {
        double rate = add_floating_height();
        double h = enemy_->sprite_height();
        state_icon_sprite->y += std::ceil(rate * h);
        state_sprite->y += std::ceil(rate * h);
    }
}

double SpriteEnemy::add_floating_height() {
    double value = 0;
    if (enemy_ && enemy_->is_floating()) {
        const Value& e = enemy_->enemy();
        double c = frame_count() + sv_rand_;
        double s = e["sideviewFloatSpeed"].num();
        double rate = e["sideviewFloatRate"].num();
        value += std::cos(c / s) * rate - rate;
        double height = e["sideviewFloatHeight"].num();
        value -= height / enemy_->sprite_height();
    }
    return value;
}

void SpriteEnemy::update_breathing() {
    if (!enemy_) return;
    double sx = 0, sy = 0;
    if (enemy_->is_breathing()) {
        const Value& e = enemy_->enemy();
        double c = frame_count() + sv_rand_;
        double s = e["sideviewBreathSpeed"].num();
        double rx = e["sideviewBreathXRate"].num();
        double ry = e["sideviewBreathYRate"].num();
        if (e["sideviewLinkBreathing"].truthy()) s /= enemy_->hp_rate();
        sx = std::cos(c / s) * rx;
        sy = std::cos(c / s) * ry;
    }
    double mirror = scale_x > 0 ? 1 : -1;
    scale_x = enemy_->sprite_scale_x() + sx;
    scale_x = std::fabs(scale_x) * mirror;
    scale_y = enemy_->sprite_scale_y() + sy;
}

void SpriteEnemy::update_motion() {
    if (!sv_battler_enabled_) return;
    setup_motion();
    setup_weapon_animation();
    if (enemy_->is_motion_refresh_requested()) {
        rpg::GameEnemy* a = enemy_;
        bool skip = motion_ == actor_motion("guard") && !rpg::battle::is_inputting();
        if (!skip) {
            int state_motion = a->state_motion_index();
            if (a->is_inputting() || a->is_acting()) start_motion(a->idle_motion());
            else if (state_motion == 3) start_motion(a->dead_motion());
            else if (state_motion == 2) start_motion(a->sleep_motion());
            else if (a->is_chanting()) start_motion(a->chant_motion());
            else if (a->is_guard() || a->is_guard_waiting()) start_motion(a->guard_motion());
            else if (state_motion == 1) start_motion(a->abnormal_motion());
            else if (a->is_dying()) start_motion(a->dying_motion());
            else if (a->is_undecided()) start_motion(a->idle_motion());
            else start_motion(a->wait_motion());
        }
        enemy_->clear_motion();
    }
    update_motion_count();
}

void SpriteEnemy::setup_motion() {
    if (!sv_battler_enabled_) return;
    if (!enemy_->is_motion_requested()) return;
    start_motion(enemy_->motion_type);
    enemy_->clear_motion();
}

void SpriteEnemy::start_motion(const std::string& motion) {
    if (is_sideview_battler()) {
        if (motion_name_ != motion) {
            motion_name_ = motion;
            motion_count_ = 0;
            pattern_ = 0;
        }
        return;
    }
    if (!sv_battler_enabled_) return;
    const MotionDef* m = actor_motion(motion);
    if (motion_ == m) return;
    motion_ = m;
    motion_count_ = 0;
    pattern_ = 0;
}

void SpriteEnemy::setup_weapon_animation() {
    bool use = !is_sideview_battler() || sideview_data()["weapon"].truthy();
    if (use) {
        if (!sv_battler_enabled_) return;
        if (!static_cast<rpg::GameBattler*>(enemy_)->is_weapon_animation_requested()) return;
        weapon_sprite->setup(enemy_->weapon_image_id());
        enemy_->clear_weapon_animation();
        return;
    }
    enemy_->clear_weapon_animation();
}

int SpriteEnemy::motion_speed() {
    if (is_sideview_battler()) {
        const Value* m = current_sideview_motion();
        if (m && (*m)["speed"].truthy()) return (*m)["speed"].as_int();
        return sideview_data()["speed"].as_int();
    }
    return 12;
}

int SpriteEnemy::motion_frames() {
    if (is_sideview_battler()) {
        const Value* m = current_sideview_motion();
        if (m && (*m)["frames"].truthy()) return (*m)["frames"].as_int();
        return sideview_data()["frames"].as_int();
    }
    return 3;
}

void SpriteEnemy::update_motion_count() {
    if (is_sideview_battler()) {
        update_sideview_motion_count();
        return;
    }
    if (!sv_battler_enabled_) return;
    if (motion_ && ++motion_count_ >= motion_speed()) {
        if (motion_->loop) pattern_ = (pattern_ + 1) % 4;
        else if (pattern_ < 2) pattern_++;
        else if (pattern_ >= 2) start_motion(enemy_->idle_motion());
        else refresh_motion();
        motion_count_ = 0;
    }
}

void SpriteEnemy::update_sideview_motion_count() {
    const Value* motion = current_sideview_motion();
    int speed = motion_speed(), frames = motion_frames();
    if (motion && ++motion_count_ >= speed) {
        if ((*motion)["loop"].truthy()) pattern_ = frames ? (pattern_ + 1) % frames : 0;
        else if (pattern_ < frames - 1) pattern_++;
        else refresh_motion();
        motion_count_ = 0;
    }
}

void SpriteEnemy::refresh_motion() {
    if (!sv_battler_enabled_) return;
    rpg::GameEnemy* e = enemy_;
    if (!e) return;
    if (motion_ == actor_motion("guard") && !rpg::battle::is_inputting()) return;
    int state_motion = e->state_motion_index();
    if (e->is_inputting() || e->is_acting()) start_motion("walk");
    else if (state_motion == 3) start_motion(e->dead_motion());
    else if (state_motion == 2) start_motion(e->sleep_motion());
    else if (e->is_guard() || e->is_guard_waiting()) start_motion(e->guard_motion());
    else if (state_motion == 1) start_motion(e->abnormal_motion());
    else if (e->is_dying()) start_motion(e->dying_motion());
    else start_motion(e->idle_motion());
}

void SpriteEnemy::force_motion(const std::string& motion) {
    if (is_sideview_battler()) {
        motion_name_ = motion;
        motion_count_ = 0;
        pattern_ = 0;
        return;
    }
    motion_ = actor_motion(motion);
    motion_count_ = 0;
    pattern_ = 0;
}

void SpriteEnemy::update_selection_effect() {
    if (rpg::party_in_battle()) {
        static const int unblinking[] = {336, 344, 351, 353, 358, 365, 367, 368, 372, 373, 377, 437, 441, 442,
                                         443, 444, 445, 451, 564, 565, 566, 570, 571, 575, 576, 592, 891};
        int troop = rpg::game_troop().troop_id;
        for (int t : unblinking) {
            if (t == troop) return;
        }
    }
    if (!sv_battler_enabled_) {
        if (battler_->is_selected()) start_effect("whiten");
        return;
    }
    static const double on[4] = {255, 255, 255, 64};
    static const double off[4] = {0, 0, 0, 0};
    if (battler_->is_selected()) {
        selection_effect_count_++;
        main_sprite->set_blend_color(selection_effect_count_ % 30 < 15 ? on : off);
    } else if (selection_effect_count_ > 0) {
        selection_effect_count_ = 0;
        main_sprite->set_blend_color(off);
    }
}

bool SpriteEnemy::is_sideview_collapse() {
    if (!sv_battler_enabled_) return true;
    return enemy_->enemy()["sideviewCollapse"].truthy();
}

void SpriteEnemy::init_visibility() {
    appeared = enemy_->is_alive();
    if (!appeared) set_opacity(0);
}

void SpriteEnemy::setup_effect() {
    if (appeared && enemy_->is_effect_requested()) {
        start_effect(enemy_->effect_type);
        enemy_->clear_effect();
    }
    if (!appeared && enemy_->is_alive()) start_effect("appear");
    else if (appeared && enemy_->is_hidden()) start_effect("disappear");
}

void SpriteEnemy::start_effect(const std::string& type) {
    effect_type_ = type;
    has_effect_type_ = true;
    if (type == "appear") start_appear();
    else if (type == "disappear") start_disappear();
    else if (type == "whiten") start_whiten();
    else if (type == "blink") start_blink();
    else if (type == "collapse") start_collapse();
    else if (type == "bossCollapse") start_boss_collapse();
    else if (type == "instantCollapse") start_instant_collapse();
    revert_to_normal();
}

void SpriteEnemy::start_appear() {
    effect_duration = 16;
    appeared = true;
}

void SpriteEnemy::start_disappear() {
    effect_duration = 32;
    appeared = false;
}

void SpriteEnemy::start_whiten() { effect_duration = 16; }
void SpriteEnemy::start_blink() { effect_duration = 20; }

void SpriteEnemy::start_collapse() {
    effect_duration = 32;
    appeared = false;
}

void SpriteEnemy::start_boss_collapse() {
    if (sv_battler_enabled_) effect_duration = (int)std::ceil(main_sprite->height() * scale_y);
    else effect_duration = bitmap() ? bitmap()->height() : 0;
    appeared = false;
}

void SpriteEnemy::start_instant_collapse() {
    effect_duration = 16;
    appeared = false;
}

void SpriteEnemy::update_effect() {
    setup_effect();
    if (effect_duration > 0) {
        effect_duration--;
        if (effect_type_ == "whiten") update_whiten();
        else if (effect_type_ == "blink") update_blink();
        else if (effect_type_ == "appear") update_appear();
        else if (effect_type_ == "disappear") update_disappear();
        else if (effect_type_ == "collapse") update_collapse();
        else if (effect_type_ == "bossCollapse") update_boss_collapse();
        else if (effect_type_ == "instantCollapse") update_instant_collapse();
        if (effect_duration == 0) {
            has_effect_type_ = false;
            effect_type_.clear();
        }
    }
}

void SpriteEnemy::revert_to_normal() {
    shake_ = 0;
    blend_mode = 0;
    set_opacity(255);
    static const double zero[4] = {0, 0, 0, 0};
    set_blend_color(zero);
}

void SpriteEnemy::update_whiten() {
    double c[4] = {255, 255, 255, 128.0 - (16 - effect_duration) * 10};
    set_blend_color(c);
}

void SpriteEnemy::update_blink() { set_opacity(effect_duration % 10 < 5 ? 255 : 0); }
void SpriteEnemy::update_appear() { set_opacity((16 - effect_duration) * 16); }
void SpriteEnemy::update_disappear() { set_opacity(256 - (32 - effect_duration) * 10); }

void SpriteEnemy::update_collapse() {
    if (fall_on_death_) {
        if (has_fall_locked_pattern_) pattern_ = fall_locked_pattern_;
        if (pattern_ >= motion_frames() - 2 || has_fall_locked_pattern_) {
            double target_y = 480 + main_sprite->height();
            offset_y_ += std::fabs(target_y - home_y_) / 32;
            if (!has_fall_locked_pattern_) {
                has_fall_locked_pattern_ = true;
                fall_locked_pattern_ = pattern_;
            }
        } else {
            effect_duration = 32;
        }
        return;
    }
    if (!is_sideview_collapse()) return;
    double d = effect_duration;
    if (sv_battler_enabled_) {
        main_sprite->blend_mode = 1;
        static const double c[4] = {255, 128, 128, 128};
        main_sprite->set_blend_color(c);
        set_opacity(opacity() * d / (d + 1));
    } else {
        blend_mode = 1;
        static const double c[4] = {255, 128, 128, 128};
        set_blend_color(c);
        set_opacity(opacity() * d / (d + 1));
    }
}

void SpriteEnemy::update_boss_collapse() {
    if (!is_sideview_collapse()) return;
    double d = effect_duration;
    shake_ = effect_duration % 2 * 4 - 2;
    if (sv_battler_enabled_) {
        main_sprite->blend_mode = 1;
        double c[4] = {255, 255, 255, 255 - opacity()};
        main_sprite->set_blend_color(c);
        set_opacity(opacity() * d / (d + 1));
    } else {
        blend_mode = 1;
        set_opacity(opacity() * d / (d + 1));
        double c[4] = {255, 255, 255, 255 - opacity()};
        set_blend_color(c);
    }
    if (effect_duration % 20 == 19) rpg::audio_manager::play_system_sound(13);
}

void SpriteEnemy::update_instant_collapse() {
    if (!is_sideview_collapse()) return;
    set_opacity(0);
}

double SpriteEnemy::damage_offset_x() { return enemy_ ? enemy_->battle_damage_x_offset() : 0; }
double SpriteEnemy::damage_offset_y() { return enemy_ ? enemy_->battle_damage_y_offset() : -8; }

void SpriteEnemy::update_status_sprite() {
    if (!enemy_) return;
    bool selected = enemy_->is_selected();
    if (selected && !status_sprite->visible) {
        status_sprite->refresh_bitmap(enemy_);
        update_status_sprite_position();
    }
    status_sprite->visible = selected;
}

void SpriteEnemy::update_status_sprite_position() {
    status_sprite->x = -(status_sprite->width() / 2);
    status_sprite->y = -((height() + status_sprite->height()) / 2);
    int position = (int)enemy_->battle_status_position();
    static const int shifts[9][2] = {{-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {0, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};
    if (position >= 0 && position < 9) shift_status_sprite_position(shifts[position][0], shifts[position][1]);
    status_sprite->x += enemy_->battle_status_x_offset();
    status_sprite->y += enemy_->battle_status_y_offset();
    g2d::Mat m = world_matrix();
    double gx, gy;
    m.apply(status_sprite->x, status_sprite->y, gx, gy);
    status_sprite->x = gx;
    status_sprite->y = gy;
    if (parent && !status_sprite->parent && refs().scene_node) {
        refs().scene_node->add_child(status_sprite);
        if (enemy_->enemy()["meta"]["sprite mirrored"].truthy()) status_sprite->scale_x = -1;
    }
}

void SpriteEnemy::shift_status_sprite_position(int sx, int sy) {
    if (sx == -1) status_sprite->x -= width() / 2;
    else if (sx == 1) status_sprite->x += width() / 2;
    if (sy == -1) status_sprite->y -= height() / 2;
    else if (sy == 1) status_sprite->y += height() / 2;
}

SpriteImprovedBattleback::SpriteImprovedBattleback(g2d::BitmapPtr b, int type) : g2d::TilingSprite(std::move(b)), type_(type ? type : 1) {
    reset_scroll_speeds();
    reset_fade_settings();
}

void SpriteImprovedBattleback::setup(g2d::Sprite* bf, SpriteImprovedBattleback* sp1) {
    initial_location_setup_ = false;
    battle_field_ = bf;
    sprite1_ = sp1;
    double margin = rpg::js_number(data::plugin_param("YEP_ImprovedBattlebacks", "Battleback Margin", "32"));
    double mx = -bf->x - margin;
    double w = 640 + margin * 2, h = 480 + margin * 2;
    move(mx, 0, w, h);
    if (sprite1_ != this) sprite1_->setup(bf, sp1);
}

void SpriteImprovedBattleback::update() {
    g2d::TilingSprite::update();
    update_initial_location();
    if (!initial_location_setup_) return;
    update_scroll();
    update_fade_out();
    update_fade_in();
}

void SpriteImprovedBattleback::update_initial_location() {
    if (initial_location_setup_) return;
    if (!battle_field_ || !sprite1_) return;
    if (!sprite1_->bitmap || sprite1_->bitmap->width() <= 0) return;
    if (!bitmap || bitmap->width() <= 0) return;
    initial_location_setup_ = true;
    std::string scale = data::plugin_param("YEP_ImprovedBattlebacks", "Scale Battlebacks", "true");
    if (scale == "true") {
        setup_scaling();
    } else {
        setup_location_x();
        setup_location_y();
    }
}

void SpriteImprovedBattleback::setup_scaling() {
    double rx = 640.0 / bitmap->width();
    double ry = 480.0 / bitmap->height();
    if (rx > 1.0) {
        scale_x = rx;
        x = 0;
    } else {
        setup_location_x();
    }
    if (ry > 1.0) {
        scale_y = ry;
        y = 0;
    } else {
        setup_location_y();
    }
}

void SpriteImprovedBattleback::setup_location_x() { origin_x = sprite1_->x + (bitmap->width() - battle_field_->width()) / 2; }

void SpriteImprovedBattleback::setup_location_y() {
    if (!side_view()) return;
    origin_y = sprite1_->y + bitmap->height() - battle_field_->height();
}

void SpriteImprovedBattleback::update_scroll() {
    origin_x += scroll_speed_x_;
    origin_y += scroll_speed_y_;
}

void SpriteImprovedBattleback::reset_scroll_speeds() {
    scroll_speed_x_ = 0;
    scroll_speed_y_ = 0;
}

void SpriteImprovedBattleback::change_bitmap(g2d::BitmapPtr b) {
    if (!b || b->width() <= 0) return;
    bitmap = std::move(b);
    initial_location_setup_ = false;
}

void SpriteImprovedBattleback::reset_fade_settings() {
    fade_out_duration_ = 0;
    fade_in_duration_ = 0;
}

void SpriteImprovedBattleback::set_fade_out(double duration) { fade_out_duration_ = (int)std::round(duration); }
void SpriteImprovedBattleback::set_fade_in(double duration) { fade_in_duration_ = (int)std::round(duration); }

void SpriteImprovedBattleback::update_fade_out() {
    if (fade_out_duration_ <= 0) return;
    double d = fade_out_duration_;
    set_opacity(opacity() * (d - 1) / d);
    --fade_out_duration_;
}

void SpriteImprovedBattleback::update_fade_in() {
    if (fade_in_duration_ <= 0) return;
    double d = fade_in_duration_;
    set_opacity((opacity() * (d - 1) + 255) / d);
    --fade_in_duration_;
}

void SpriteImprovedBattleback::set_opacity_value(double value) {
    reset_fade_settings();
    set_opacity(std::round(value));
}

SpritesetBattle::SpritesetBattle() {}

SpritesetBattle::~SpritesetBattle() { release_links(); }

void SpritesetBattle::release_links() {
    for (auto& s : enemy_sprites) {
        if (s) s->release_link();
    }
    for (auto& s : actor_sprites) {
        if (s) s->release_link();
    }
}

const js::Val* SpritesetBattle::horror_filters_source() {
    horror_source_ = rpg::game_system().horror_filters();
    return &horror_source_;
}

void SpritesetBattle::create_lower_layer() {
    SpritesetBase::create_lower_layer();
    create_background();
    create_battle_field();
    create_battleback();
    create_enemies();
    create_actors();
}

void SpritesetBattle::create_background() {
    background_sprite = std::make_shared<g2d::Sprite>(rpg::scene_manager::background_bitmap());
    base_sprite->add_child(background_sprite);
}

void SpritesetBattle::create_battle_field() {
    battle_field = std::make_shared<g2d::Sprite>();
    battle_field->set_frame(0, 0, 640, 480);
    battle_field->x = 0;
    battle_field->y = 0;
    base_sprite->add_child(battle_field);
}

std::string SpritesetBattle::battleback1_name() {
    if (rpg::battle::st().battle_test) return data::system()["battleback1Name"].string();
    return rpg::game_map().battleback1_name();
}

std::string SpritesetBattle::battleback2_name() {
    if (rpg::battle::st().battle_test) return data::system()["battleback2Name"].string();
    return rpg::game_map().battleback2_name();
}

void SpritesetBattle::create_battleback() {
    loading_images.assign(1, nullptr);
    battleback_sprites.assign(1, nullptr);
    battleback_to_remove.clear();
    back1_sprite = std::make_shared<SpriteImprovedBattleback>(load_battleback1(battleback1_name()), 1);
    battleback_sprites.push_back(back1_sprite);
    back2_sprite = std::make_shared<SpriteImprovedBattleback>(load_battleback2(battleback2_name()), 2);
    battleback_sprites.push_back(back2_sprite);
    loading_images.resize(battleback_sprites.size());
    back1_sprite->setup(battle_field.get(), back1_sprite.get());
    back2_sprite->setup(battle_field.get(), back1_sprite.get());
    battle_field->add_child(back1_sprite);
    battle_field->add_child(back2_sprite);
}

void SpritesetBattle::create_enemies() {
    std::vector<std::shared_ptr<SpriteEnemy>> sprites;
    for (rpg::GameEnemy* e : rpg::game_troop().members()) {
        std::shared_ptr<rpg::GameEnemy> sp;
        for (auto& x : rpg::game_troop().enemies) {
            if (x.get() == e) sp = x;
        }
        for (auto& x : rpg::game_troop().new_enemies) {
            if (x.get() == e) sp = x;
        }
        if (!sp) continue;
        sprites.push_back(std::make_shared<SpriteEnemy>(sp));
    }
    std::stable_sort(sprites.begin(), sprites.end(), [](const std::shared_ptr<SpriteEnemy>& a, const std::shared_ptr<SpriteEnemy>& b) {
        if (a->y != b->y) return a->y < b->y;
        return b->sprite_id < a->sprite_id;
    });
    for (auto& s : sprites) battle_field->add_child(s);
    enemy_sprites = sprites;
}

void SpritesetBattle::create_actors() {
    actor_sprites.clear();
    for (int i = 0; i < rpg::game_party().max_battle_members(); i++) {
        auto s = std::make_shared<SpriteActor>();
        actor_sprites.push_back(s);
        battle_field->add_child(s);
    }
}

void SpritesetBattle::update() {
    SpritesetBase::update();
    update_actors();
    update_battleback();
    update_z_coordinates();
}

void SpritesetBattle::update_actors() {
    auto members = rpg::game_party().battle_members();
    for (size_t i = 0; i < actor_sprites.size(); i++) actor_sprites[i]->set_battler(i < members.size() ? members[i] : nullptr);
}

void SpritesetBattle::update_battleback() {
    update_battleback_change_to();
    update_battleback_removal();
}

void SpritesetBattle::update_z_coordinates() {
    update_battleback_group_remove();
    std::stable_sort(battle_field->children.begin(), battle_field->children.end(), [](const g2d::DisplayPtr& a, const g2d::DisplayPtr& b) {
        if (a->z < b->z) return true;
        if (a->z > b->z) return false;
        return a->y < b->y;
    });
    update_battleback_group_add();
}

void SpritesetBattle::update_position() {
    rpg::GameScreen& s = rpg::game_screen();
    double zoom = s.zoom_scale;
    bool clamp = rpg::battle::st().camera_clamp;
    scale_x = zoom;
    scale_y = zoom;
    double sx = -1 * s.zoom_x * zoom + 320;
    double sy = -1 * s.zoom_y * zoom + 240;
    if (clamp && zoom >= 1.0) {
        double cx1 = -640 * zoom + 640;
        double cy2 = -480 * zoom + 480;
        x = std::round(std::min(std::max(sx, cx1), 0.0));
        y = std::round(std::min(std::max(sy, cy2), 0.0));
    } else if (clamp && zoom < 1.0) {
        x = std::round((640 - 640 * zoom) / 2);
        y = std::round((480 - 480 * zoom) / 2);
    } else {
        x = std::round(sx);
        y = std::round(sy);
    }
    x += std::round(s.shake);
}

std::vector<SpriteBattler*> SpritesetBattle::battler_sprites() {
    std::vector<SpriteBattler*> out;
    for (auto& s : enemy_sprites) {
        if (s && s->battler()) out.push_back(s.get());
    }
    for (auto& s : actor_sprites) {
        if (s && s->battler()) out.push_back(s.get());
    }
    return out;
}

bool SpritesetBattle::is_animation_playing() {
    for (SpriteBattler* s : battler_sprites()) {
        if (s->is_animation_playing()) return true;
    }
    return false;
}

bool SpritesetBattle::is_effecting() {
    for (SpriteBattler* s : battler_sprites()) {
        if (s->is_effecting()) return true;
    }
    return false;
}

bool SpritesetBattle::is_anyone_moving() {
    for (SpriteBattler* s : battler_sprites()) {
        if (s->is_moving()) return true;
    }
    return false;
}

bool SpritesetBattle::is_anyone_floating() {
    for (SpriteBattler* s : battler_sprites()) {
        if (s->is_floating()) return true;
    }
    return false;
}

bool SpritesetBattle::is_anyone_jumping() {
    for (SpriteBattler* s : battler_sprites()) {
        if (s->is_jumping()) return true;
    }
    return false;
}

bool SpritesetBattle::is_anyone_changing_opacity() {
    for (SpriteBattler* s : battler_sprites()) {
        if (s->is_changing_opacity()) return true;
    }
    return false;
}

bool SpritesetBattle::is_popup_playing() {
    for (SpriteBattler* s : battler_sprites()) {
        if (s->is_popup_playing()) return true;
    }
    return false;
}

void SpritesetBattle::remove_enemies() {
    for (auto& s : enemy_sprites) battle_field->remove_child(s.get());
}

void SpritesetBattle::refresh_enemy_reinforcements() {
    remove_enemies();
    create_enemies();
}

void SpritesetBattle::update_battleback_group_remove() {
    for (auto& s : battleback_sprites) {
        if (s) battle_field->remove_child(s.get());
    }
}

void SpritesetBattle::update_battleback_group_add() {
    for (size_t i = battleback_sprites.size(); i > 0; --i) {
        if (i < battleback_sprites.size() && battleback_sprites[i]) battle_field->add_child_at(battleback_sprites[i], 0);
    }
}

void SpritesetBattle::update_battleback_change_to() {
    if (loading_images.size() < battleback_sprites.size()) loading_images.resize(battleback_sprites.size());
    for (size_t i = 0; i < battleback_sprites.size(); i++) {
        if (!battleback_sprites[i] || !loading_images[i]) continue;
        if (loading_images[i]->width() <= 0) return;
    }
    for (size_t i = 0; i < battleback_sprites.size(); i++) {
        if (!battleback_sprites[i] || !loading_images[i]) continue;
        battleback_sprites[i]->change_bitmap(loading_images[i]);
        loading_images[i] = nullptr;
    }
}

void SpritesetBattle::update_battleback_removal() {
    size_t length = battleback_to_remove.size();
    std::vector<size_t> to_remove;
    for (size_t i = 0; i < length; ++i) {
        int id = battleback_to_remove[i];
        std::shared_ptr<SpriteImprovedBattleback> bb = id >= 0 && id < (int)battleback_sprites.size() ? battleback_sprites[(size_t)id] : nullptr;
        if (bb && bb->opacity() > 0) continue;
        if (id >= 0 && id < (int)battleback_sprites.size()) {
            battleback_sprites.erase(battleback_sprites.begin() + id);
            if (id < (int)loading_images.size()) loading_images.erase(loading_images.begin() + id);
        }
        if (!(id >= 0 && id < (int)battleback_sprites.size() && battleback_sprites[(size_t)id])) to_remove.push_back(i);
    }
    for (size_t id : to_remove) {
        bool present = id < battleback_sprites.size() && battleback_sprites[id];
        if (present) continue;
        auto it = std::find(battleback_to_remove.begin(), battleback_to_remove.end(), (int)id);
        if (it != battleback_to_remove.end()) battleback_to_remove.erase(it);
        else if (!battleback_to_remove.empty()) battleback_to_remove.pop_back();
    }
}

void SpritesetBattle::change_battleback_to(int id, const std::string& folder, const std::string& file, int hue) {
    if (id < 0 || id >= (int)battleback_sprites.size() || !battleback_sprites[(size_t)id]) return;
    if (loading_images.size() < battleback_sprites.size()) loading_images.resize(battleback_sprites.size());
    loading_images[(size_t)id] = load_path_hued(folder + file, hue);
}

void SpritesetBattle::battleback_fade_in(int id, int duration) {
    if (id < 0 || id >= (int)battleback_sprites.size() || !battleback_sprites[(size_t)id]) return;
    battleback_sprites[(size_t)id]->set_fade_in(duration);
}

void SpritesetBattle::battleback_fade_out(int id, int duration) {
    if (id < 0 || id >= (int)battleback_sprites.size() || !battleback_sprites[(size_t)id]) return;
    battleback_sprites[(size_t)id]->set_fade_out(duration);
}

void SpritesetBattle::battleback_opacity(int id, int value) {
    if (id < 0 || id >= (int)battleback_sprites.size() || !battleback_sprites[(size_t)id]) return;
    battleback_sprites[(size_t)id]->set_opacity_value(value);
}

void SpritesetBattle::reset_scroll_speeds(int id) {
    if (id < 0 || id >= (int)battleback_sprites.size() || !battleback_sprites[(size_t)id]) return;
    battleback_sprites[(size_t)id]->reset_scroll_speeds();
}

void SpritesetBattle::set_battleback_scroll_speed_x(int id, int speed) {
    if (id < 0 || id >= (int)battleback_sprites.size() || !battleback_sprites[(size_t)id]) return;
    battleback_sprites[(size_t)id]->set_scroll_speed_x(speed);
}

void SpritesetBattle::set_battleback_scroll_speed_y(int id, int speed) {
    if (id < 0 || id >= (int)battleback_sprites.size() || !battleback_sprites[(size_t)id]) return;
    battleback_sprites[(size_t)id]->set_scroll_speed_y(speed);
}

void SpritesetBattle::add_new_battleback(int id, const std::string& folder, const std::string& file, int hue, double op, int duration) {
    if (id <= 2) return;
    if (id < (int)battleback_sprites.size() && battleback_sprites[(size_t)id]) return;
    auto bb = std::make_shared<SpriteImprovedBattleback>(load_path_hued(folder + file, hue), 3);
    bb->setup(battle_field.get(), back1_sprite.get());
    bb->set_opacity_value(op);
    bb->set_fade_in(duration);
    if ((int)battleback_sprites.size() <= id) battleback_sprites.resize((size_t)id + 1);
    battleback_sprites[(size_t)id] = bb;
    if (loading_images.size() < battleback_sprites.size()) loading_images.resize(battleback_sprites.size());
    update_battleback_group_remove();
    update_battleback_group_add();
}

void SpritesetBattle::remove_battleback(int id) {
    if (id <= 2) return;
    if (id >= (int)battleback_sprites.size() || !battleback_sprites[(size_t)id]) return;
    battleback_sprites[(size_t)id]->set_fade_out(20);
    battleback_to_remove.push_back(id);
}

void SpritesetBattle::set_picture_layer(const std::string& layer) {
    if (!picture_container) return;
    if (layer == "top") {
        if (scene) scene->add_child(picture_container);
    } else if (layer == "front") {
        if (picture_container->parent) picture_container->parent->remove_child(picture_container.get());
        size_t index = 0, counted = 0;
        while (index < children.size() && counted < 2) {
            if (children[index].get() != videos_container.get()) counted++;
            index++;
        }
        add_child_at(picture_container, index);
    } else if (layer == "back") {
        battle_field->add_child_at(picture_container, 0);
    }
}

js::Val SpritesetBattle::js() { return display_js(this); }

bool SpritesetBattle::display_js_get(uint32_t key, js::Val& out) {
    const std::string& n = js::atom_name(key);
    if (n == "_battlebackSprites") {
        std::vector<js::Val> items;
        for (auto& s : battleback_sprites) items.push_back(s ? display_js(s.get()) : js::Val());
        out = js::make_array(std::move(items));
    } else if (n == "_enemySprites") {
        std::vector<js::Val> items;
        for (auto& s : enemy_sprites) items.push_back(display_js(s.get()));
        out = js::make_array(std::move(items));
    } else if (n == "_actorSprites") {
        std::vector<js::Val> items;
        for (auto& s : actor_sprites) items.push_back(display_js(s.get()));
        out = js::make_array(std::move(items));
    } else if (n == "_pictureContainer") {
        out = display_js(picture_container.get());
    } else if (n == "_baseSprite") {
        out = display_js(base_sprite.get());
    } else if (n == "_battleField") {
        out = display_js(battle_field.get());
    } else if (n == "_back1Sprite") {
        out = display_js(back1_sprite.get());
    } else if (n == "_back2Sprite") {
        out = display_js(back2_sprite.get());
    } else if (n == "_backgroundSprite") {
        out = display_js(background_sprite.get());
    } else if (n == "_timerSprite") {
        out = display_js(timer_sprite.get());
    } else if (n == "_blackScreen") {
        out = display_js(black_screen.get());
    } else if (n == "_flashSprite") {
        out = display_js(flash_sprite.get());
    } else if (n == "_fadeSprite") {
        out = display_js(fade_sprite.get());
    } else {
        return false;
    }
    return true;
}

namespace {

void flat_chunks(Image* img, double sx, double sy, double sw, double sh, const g2d::Mat& m, uint32_t rgb, int a8, gfx::Blend blend, bool smooth) {
    if (!img || sw <= 0 || sh <= 0 || a8 <= 0) return;
    float tx0 = (float)(sx * img->fx), ty0 = (float)(sy * img->fy);
    float tx1 = (float)((sx + sw) * img->fx), ty1 = (float)((sy + sh) * img->fy);
    if (m.axis_aligned()) {
        float x0, y0, x1, y1;
        g2d::screen_point(m, 0, 0, x0, y0);
        g2d::screen_point(m, sw, sh, x1, y1);
        float pw = std::fabs(x1 - x0), ph = std::fabs(y1 - y0);
        float tw = tx1 - tx0, th = ty1 - ty0;
        bool unscaled = std::fabs(pw - tw) < 0.05f && std::fabs(ph - th) < 0.05f;
        if (unscaled) {
            float rx = std::round(x0), ry = std::round(y0);
            x1 += rx - x0;
            y1 += ry - y0;
            x0 = rx;
            y0 = ry;
        }
        float kx = (x1 - x0) / (tx1 - tx0), ky = (y1 - y0) / (ty1 - ty0);
        for (size_t ci = 0; ci < img->chunks.size(); ci++) {
            const ImageChunk& c = img->chunks[ci];
            float cx0 = std::max(tx0, (float)c.x), cy0 = std::max(ty0, (float)c.y);
            float cx1 = std::min(tx1, (float)(c.x + c.w)), cy1 = std::min(ty1, (float)(c.y + c.h));
            if (cx1 <= cx0 || cy1 <= cy0) continue;
            gfx::Texture* ctex = images::chunk_texture(img, ci);
            if (!ctex) continue;
            float dx0 = x0 + (cx0 - tx0) * kx, dx1 = x0 + (cx1 - tx0) * kx;
            float dy0 = y0 + (cy0 - ty0) * ky, dy1 = y0 + (cy1 - ty0) * ky;
            gfx::sprite_flat(ctex, cx0 - c.x, cy0 - c.y, cx1 - c.x, cy1 - c.y, dx0, dy0, dx1, dy1, rgb, a8, blend, smooth);
        }
        return;
    }
    for (size_t ci = 0; ci < img->chunks.size(); ci++) {
        const ImageChunk& ch = img->chunks[ci];
        float cx0 = std::max(tx0, (float)ch.x), cy0 = std::max(ty0, (float)ch.y);
        float cx1 = std::min(tx1, (float)(ch.x + ch.w)), cy1 = std::min(ty1, (float)(ch.y + ch.h));
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        gfx::Texture* ctex = images::chunk_texture(img, ci);
        if (!ctex) continue;
        float lx[4] = {cx0, cx1, cx1, cx0}, ly[4] = {cy0, cy0, cy1, cy1};
        float xy[8], uv[8];
        for (int k = 0; k < 4; k++) {
            double ox = (lx[k] - tx0) / img->fx, oy = (ly[k] - ty0) / img->fy;
            g2d::screen_point(m, ox, oy, xy[k * 2], xy[k * 2 + 1]);
            uv[k * 2] = lx[k] - ch.x;
            uv[k * 2 + 1] = ly[k] - ch.y;
        }
        gfx::quad_flat(ctex, uv, xy, rgb, a8, blend, true);
    }
}

void overlay_self(g2d::DisplayObject& obj, const g2d::RenderState& rs, uint32_t rgb, gfx::Blend blend) {
    g2d::Sprite* s = dynamic_cast<g2d::Sprite*>(&obj);
    if (!s || !s->renderable) return;
    g2d::BitmapPtr b = s->bitmap();
    if (!b) return;
    const g2d::Rect& f = s->frame();
    double fx = std::floor(f.x), fy = std::floor(f.y), fw = std::floor(f.width), fh = std::floor(f.height);
    double bw = b->width(), bh = b->height();
    double rx = std::clamp(fx, 0.0, bw), ry = std::clamp(fy, 0.0, bh);
    double rw = std::clamp(fw - rx + fx, 0.0, bw - rx), rh = std::clamp(fh - ry + fy, 0.0, bh - ry);
    if (rw <= 0 || rh <= 0) return;
    g2d::Mat off;
    off.tx = -s->anchor_x * rw;
    off.ty = -s->anchor_y * rh;
    g2d::Mat m = rs.world.mul(off);
    int a8 = std::clamp((int)std::lround(rs.alpha * 255.0), 0, 255);
    b->touch();
    flat_chunks(b->image(), rx, ry, rw, rh, m, rgb, a8, blend, b->smooth);
}

void overlay_tree(g2d::DisplayObject& obj, const g2d::RenderState& rs, uint32_t rgb, gfx::Blend blend) {
    overlay_self(obj, rs, rgb, blend);
    g2d::Container* c = dynamic_cast<g2d::Container*>(&obj);
    if (!c) return;
    std::vector<g2d::DisplayPtr> snapshot = c->children;
    for (auto& ch : snapshot) {
        if (!ch->visible || ch->alpha <= 0) continue;
        g2d::RenderState cs = rs;
        cs.world = rs.world.mul(ch->local_matrix());
        cs.alpha = rs.alpha * ch->alpha;
        if (ch->mask_rect) {
            float x0, y0, x1, y1;
            g2d::screen_point(cs.world, ch->mask_rect->x, ch->mask_rect->y, x0, y0);
            g2d::screen_point(cs.world, ch->mask_rect->x + ch->mask_rect->width, ch->mask_rect->y + ch->mask_rect->height, x1, y1);
            int ix0 = (int)std::lround(std::min(x0, x1)), iy0 = (int)std::lround(std::min(y0, y1));
            int ix1 = (int)std::lround(std::max(x0, x1)), iy1 = (int)std::lround(std::max(y0, y1));
            cs.clip = true;
            cs.cx0 = std::max(rs.cx0, ix0);
            cs.cy0 = std::max(rs.cy0, iy0);
            cs.cx1 = std::min(rs.cx1, ix1);
            cs.cy1 = std::min(rs.cy1, iy1);
            if (cs.cx1 <= cs.cx0 || cs.cy1 <= cs.cy0) continue;
            gfx::set_scissor(cs.cx0, cs.cy0, cs.cx1 - cs.cx0, cs.cy1 - cs.cy0);
            overlay_tree(*ch, cs, rgb, blend);
            if (rs.clip) gfx::set_scissor(rs.cx0, rs.cy0, rs.cx1 - rs.cx0, rs.cy1 - rs.cy0);
            else gfx::reset_scissor();
        } else {
            overlay_tree(*ch, cs, rgb, blend);
        }
    }
}

}

void ToneOverlay::apply(g2d::DisplayObject& obj, const g2d::RenderState& rs, const std::function<void(const g2d::RenderState&)>& draw) {
    draw(rs);
    if (!tone) return;
    int add[3], sub[3];
    bool any_add = false, any_sub = false;
    for (int i = 0; i < 3; i++) {
        int v = (int)std::lround(std::clamp(tone[i], -255.0, 255.0));
        add[i] = v > 0 ? v : 0;
        sub[i] = v < 0 ? -v : 0;
        any_add = any_add || add[i];
        any_sub = any_sub || sub[i];
    }
    if (any_add) overlay_tree(obj, rs, gfx::rgba(add[0], add[1], add[2], 0) & 0xFFFFFFu, gfx::Blend::Add);
    if (any_sub) overlay_tree(obj, rs, gfx::rgba(sub[0], sub[1], sub[2], 0) & 0xFFFFFFu, gfx::Blend::Subtract);
}

void set_filters(g2d::DisplayObject* obj, std::initializer_list<g2d::FilterPtr> list) {
    if (!obj) return;
    obj->filters.assign(list.begin(), list.end());
}

bool has_filters(g2d::DisplayObject* obj) { return obj && !obj->filters.empty(); }

}
}

namespace rpg {

void battler_sprite_position(int index, double& x, double& y) {
    x = y = 0;
    ui::btl::SpritesetBattle* s = ui::btl::refs().spriteset;
    if (!s) return;
    ui::btl::SpriteBattler* sprite = nullptr;
    if (index < 0) {
        size_t i = (size_t)(-index - 1);
        if (i < s->enemy_sprites.size()) sprite = s->enemy_sprites[i].get();
    } else if (index > 0) {
        size_t i = (size_t)(index - 1);
        if (i < s->actor_sprites.size()) sprite = s->actor_sprites[i].get();
    }
    if (!sprite) return;
    x = sprite->x;
    y = sprite->y;
}

}
