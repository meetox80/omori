#include <algorithm>
#include <cmath>
#include <utility>

#include "../js/random.h"
#include "../rpg/audio_manager.h"
#include "../rpg/character.h"
#include "../rpg/data.h"
#include "../rpg/enemy.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "menu_common.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {
namespace menu {

namespace {

const Value& bestiary_information() { return lang_pinned("Bestiary", "Information"); }

const Value& bestiary_entry(int enemy_id) {
    std::string key = std::to_string(enemy_id);
    const Value* v = bestiary_information().find(key.c_str());
    return v ? *v : Value::null_value();
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : text) {
        if (ch == '\r' || ch == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    out.push_back(cur);
    return out;
}

bool is_auto(const Value& v) { return v.is_str() && v.string() == "auto"; }

}

class SpriteOmoBestiaryEnemy : public g2d::Sprite, public rpg::BattlerSpriteLink {
public:
    explicit SpriteOmoBestiaryEnemy(rpg::GameEnemy* enemy) {
        init_members();
        set_battler(enemy);
    }

    ~SpriteOmoBestiaryEnemy() override {
        if (enemy_ && enemy_->sprite_link == this) enemy_->sprite_link = nullptr;
    }

    void init_members() {
        anchor_x = 0.5;
        anchor_y = 1;
        enemy_ = nullptr;
        home_x_ = home_y_ = 0;
        offset_x_ = offset_y_ = 0;
        has_target_offset_ = false;
        movement_duration_ = 0;
        opacity_target_ = 255;
        opacity_dur_ = 0;
        opacity_rate_ = 0;
        opacity_changing_ = false;
        appeared_ = false;
        has_battler_name_ = false;
        battler_name_.clear();
        battler_hue_ = 0;
        init_sv_sprites();
        motion_name_.clear();
    }

    void init_sv_sprites() {
        sv_rand_ = rng::next() * 10000;
        sv_battler_name_.clear();
        motion_index_ = -1;
        motion_count_ = 0;
        pattern_ = 0;
        sv_enabled_ = false;
        create_shadow_sprite();
        create_main_sprite();
    }

    void create_shadow_sprite() {
        shadow_ = std::make_shared<g2d::Sprite>(load_system("Shadow2"));
        shadow_->anchor_x = 0.5;
        shadow_->anchor_y = 0.5;
        shadow_->y = -2;
        add_child(shadow_);
        shadow_->set_opacity(0);
    }

    void create_main_sprite() {
        main_ = std::make_shared<g2d::Sprite>();
        main_->anchor_x = 0.5;
        main_->anchor_y = 1;
        add_child(main_);
    }

    void set_transform(rpg::GameEnemy* battler) {
        shadow_->set_opacity(0);
        main_->set_opacity(0);
        if (!battler->sv_battler_name_value().is_undef()) {
            create_shadow_sprite();
            create_main_sprite();
            shadow_->set_opacity(255);
            main_->set_opacity(255);
        }
    }

    void set_battler(rpg::GameEnemy* battler) {
        sv_enabled_ = false;
        init_sv_sprites();
        if (enemy_ && enemy_ != battler && enemy_->sprite_link == this) enemy_->sprite_link = nullptr;
        enemy_ = battler;
        if (battler) battler->sprite_link = this;
        set_home(battler->screen_x(), battler->screen_y());
        set_sv_battler();
    }

    void set_sv_battler() {
        if (!enemy_) return;
        if (enemy_->sv_battler_name_value().is_undef()) return;
        adjust_main_bitmap_settings_ = false;
        sv_enabled_ = true;
    }

    void set_home(double x, double y) {
        home_x_ = x;
        home_y_ = y;
        update_position();
    }

    const Value& sideview() const { return enemy_->enemy()["_sideviewBattler"]; }

    bool is_sideview_battler() const {
        const Value& sv = sideview();
        return sv.is_obj() && !sv["filename"].string().empty() && !sv["default"].truthy();
    }

    const Value* fallback_motion() const {
        if (!is_sideview_battler()) return nullptr;
        const Value& motions = sideview()["motions"];
        const Value* other = motions.find("other");
        if (other && other->truthy()) return other;
        const Value* walk = motions.find("walk");
        return walk && walk->truthy() ? walk : nullptr;
    }

    const Value* sideview_motion(const std::string& name) const {
        if (name.empty()) return nullptr;
        if (!is_sideview_battler()) return nullptr;
        const Value* m = sideview()["motions"].find(name.c_str());
        if (!m || !m->truthy()) return fallback_motion();
        return m;
    }

    double sideview_frames(const std::string& name) const {
        const Value* m = sideview_motion(name);
        if (m && (*m)["frames"].truthy()) return (*m)["frames"].num();
        return sideview()["frames"].num();
    }

    double sideview_speed(const std::string& name) const {
        const Value* m = sideview_motion(name);
        if (m && (*m)["speed"].truthy()) return (*m)["speed"].num();
        return sideview()["speed"].num();
    }

    const Value* current_motion() const {
        const Value* other = sideview_motion("other");
        if (!other) return sideview_motion("walk");
        return other;
    }

    void start_motion(const std::string& motion_type) {
        if (is_sideview_battler()) {
            if (motion_name_ != motion_type) {
                motion_name_ = motion_type;
                motion_count_ = 0;
                pattern_ = 0;
            }
            return;
        }
        if (!sv_enabled_) return;
        int idx = sv_motion_index(motion_type);
        if (motion_index_ == idx) return;
        motion_index_ = idx;
        motion_count_ = 0;
        pattern_ = 0;
    }

    void force_motion(const std::string& motion_type) {
        if (is_sideview_battler()) {
            motion_name_ = motion_type;
            motion_count_ = 0;
            pattern_ = 0;
            return;
        }
        if (!sv_enabled_) return;
        motion_index_ = sv_motion_index(motion_type);
        motion_count_ = 0;
        pattern_ = 0;
    }

    static int sv_motion_index(const std::string& name) {
        static const char* names[] = {"walk",  "wait",   "chant", "guard",    "damage", "evade",  "thrust", "swing", "missile",
                                      "skill", "spell",  "item",  "escape",   "victory", "dying", "abnormal", "sleep", "dead"};
        for (int i = 0; i < 18; i++) {
            if (name == names[i]) return i;
        }
        return -1;
    }

    static bool sv_motion_loop(int index) { return index == 0 || index == 1 || index == 2 || index == 3 || index == 15 || index == 16 || index == 17; }

    void refresh_motion() {
        if (!sv_enabled_) return;
        rpg::GameEnemy* enemy = enemy_;
        if (!enemy) return;
        int state_motion = enemy->state_motion_index();
        if (enemy->is_inputting() || enemy->is_acting()) start_motion("walk");
        else if (state_motion == 3) start_motion(enemy->dead_motion());
        else if (state_motion == 2) start_motion(enemy->sleep_motion());
        else if (enemy->is_guard() || enemy->is_guard_waiting()) start_motion(enemy->guard_motion());
        else if (state_motion == 1) start_motion(enemy->abnormal_motion());
        else if (enemy->is_dying()) start_motion(enemy->dying_motion());
        else start_motion(enemy->idle_motion());
    }

    void update() override {
        g2d::Sprite::update();
        visible = true;
        if (enemy_) {
            update_bitmap();
            update_frame();
            update_move();
            update_position();
            update_float();
            update_opacity();
            if (sv_enabled_) update_motion();
            update_breathing();
        }
        update_mask();
    }

    void update_bitmap() {
        std::string name = enemy_->battler_name();
        int hue = enemy_->battler_hue();
        if (!has_battler_name_ || battler_name_ != name || battler_hue_ != hue) {
            has_battler_name_ = true;
            battler_name_ = name;
            battler_hue_ = hue;
            if (!sv_enabled_) set_bitmap(load_enemy(name));
            appeared_ = enemy_->is_alive();
            if (!appeared_) set_opacity(0);
        }
        if (!sv_enabled_) update_scale();
        update_sv_bitmap();
        adjust_anchor();
    }

    void update_sv_bitmap() {
        js::Val v = enemy_->sv_battler_name_value();
        std::string name = v.is_undef() ? std::string() : v.to_str();
        if (sv_enabled_ && sv_battler_name_ != name) {
            created_dummy_main_ = false;
            sv_battler_name_ = name;
            main_->set_bitmap(load_folder("sv_actors", name));
            adjust_anchor();
            refresh_motion();
            update_scale();
        } else if (sv_battler_name_.empty()) {
            sv_enabled_ = false;
            if (created_dummy_main_) return;
            created_dummy_main_ = true;
            main_ = std::make_shared<g2d::Sprite>();
            main_->anchor_x = 0.5;
            main_->anchor_y = 1;
        }
    }

    void adjust_anchor() {
        if (!main_) return;
        main_->anchor_x = enemy_->enemy()["sideviewAnchorX"].num();
        main_->anchor_y = enemy_->enemy()["sideviewAnchorY"].num();
    }

    void update_scale() {
        scale_x = enemy_->sprite_scale_x();
        scale_y = enemy_->sprite_scale_y();
    }

    void update_frame() {
        if (is_sideview_battler()) {
            update_sideview_frame();
            return;
        }
        if (sv_enabled_) {
            update_sv_frame();
            return;
        }
        if (bitmap()) set_frame(0, 0, bitmap()->width(), bitmap()->height());
    }

    void update_sideview_frame() {
        g2d::BitmapPtr bmp = main_->bitmap();
        const Value* motion = current_motion();
        if (!bmp || bmp->width() <= 0 || !motion) return;
        const Value& sizes = sideview()["sizes"];
        double cw = sizes[0].num(), ch = sizes[1].num();
        double cx = pattern_;
        double cy = (*motion)["index"].num();
        main_->set_frame(cx * cw, cy * ch, cw, ch);
        adjust_main_bitmap_settings(bmp.get());
        adjust_sv_shadow_settings();
    }

    void update_sv_frame() {
        g2d::BitmapPtr bmp = main_->bitmap();
        if (!bmp || bmp->width() <= 0) return;
        int motion_index = motion_index_ >= 0 ? motion_index_ : 0;
        int pattern = pattern_ < 3 ? pattern_ : 1;
        double cw = bmp->width() / 9.0;
        double ch = bmp->height() / 6.0;
        double cx = std::floor(motion_index / 6.0) * 3 + pattern;
        double cy = motion_index % 6;
        main_->set_frame(cx * cw, cy * ch, cw, ch);
        adjust_main_bitmap_settings(bmp.get());
        adjust_sv_shadow_settings();
    }

    void adjust_main_bitmap_settings(g2d::Bitmap* bmp) {
        if (adjust_main_bitmap_settings_) return;
        adjust_main_bitmap_settings_ = true;
        const Value& svw_v = enemy_->enemy()["sideviewWidth"];
        const Value& svh_v = enemy_->enemy()["sideviewHeight"];
        double svw = is_auto(svw_v) ? bmp->width() / 9.0 : svw_v.num();
        double svh = is_auto(svh_v) ? bmp->height() / 6.0 : svh_v.num();
        own_width_ = std::floor(std::abs(svw * enemy_->sprite_scale_x()));
        own_height_ = std::floor(std::abs(svh * enemy_->sprite_scale_y()));
        has_own_size_ = true;
        set_bitmap(nullptr);
    }

    void adjust_sv_shadow_settings() {
        const Value& e = enemy_->enemy();
        if (e["sideviewShadowShow"].truthy()) shadow_->set_opacity(255);
        const Value& sx = e["sideviewShadowScaleX"];
        const Value& sy = e["sideviewShadowScaleY"];
        double w = main_->bitmap() ? main_->bitmap()->width() : 0;
        shadow_->scale_x = is_auto(sx) ? w / 9 / 64 : sx.num();
        shadow_->scale_y = is_auto(sy) ? w / 9 / 64 : sy.num();
    }

    void update_move() {
        if (movement_duration_ > 0) {
            double d = movement_duration_;
            offset_x_ = (offset_x_ * (d - 1) + target_offset_x_) / d;
            offset_y_ = (offset_y_ * (d - 1) + target_offset_y_) / d;
            movement_duration_--;
        }
    }

    void update_position() {
        x = home_x_ + offset_x_;
        y = home_y_ + offset_y_;
    }

    double sprite_height() {
        const Value& svh = enemy_->enemy()["sideviewHeight"];
        if (!enemy_->sv_battler_name_value().is_undef() && !is_auto(svh)) return std::floor(svh.num());
        if (has_own_size_) return std::floor(own_height_);
        if (bitmap()) return std::floor((double)bitmap()->height());
        return 1;
    }

    double add_floating_height() {
        double value = 0;
        if (enemy_ && enemy_->is_floating()) {
            double c = (double)rpg::frame_count() + sv_rand_;
            double s = enemy_->enemy()["sideviewFloatSpeed"].num();
            double rate = enemy_->enemy()["sideviewFloatRate"].num();
            value += std::cos(c / s) * rate - rate;
            double height = enemy_->enemy()["sideviewFloatHeight"].num();
            value -= height / sprite_height();
        }
        return value;
    }

    void update_float() {
        double base_y = enemy_->anchor_y();
        double height = 0 - add_floating_height();
        if (main_ && main_->bitmap()) {
            double mh = main_->height();
            double rate = mh != 0 ? sprite_height() / mh : 0;
            main_->anchor_y = base_y + (height == 0 ? 0 : height * rate);
        } else {
            anchor_y = base_y + height;
        }
    }

    void update_opacity() {
        if (!opacity_changing_) return;
        opacity_dur_--;
        if (opacity_dur_ <= 0) {
            if (opacity() != opacity_target_) set_opacity(opacity_target_);
            opacity_changing_ = false;
        } else {
            double target = opacity_target_;
            double rate = opacity_rate_;
            if (opacity() >= target) set_opacity(std::max(target, opacity() - rate));
            else set_opacity(std::min(target, opacity() + rate));
        }
    }

    void update_motion() {
        if (!sv_enabled_) return;
        if (enemy_->is_motion_requested()) {
            start_motion(enemy_->motion_type);
            enemy_->clear_motion();
        }
        if (is_sideview_battler() && !sideview()["weapon"].truthy()) enemy_->clear_weapon_animation();
        if (enemy_->is_motion_refresh_requested()) {
            refresh_motion();
            enemy_->clear_motion();
        }
        update_motion_count();
    }

    void update_motion_count() {
        if (is_sideview_battler()) {
            const Value* motion = current_motion();
            double speed = sideview_speed(motion_name_);
            double frames = sideview_frames(motion_name_);
            if (motion && ++motion_count_ >= speed) {
                if ((*motion)["loop"].truthy()) {
                    int f = (int)frames;
                    pattern_ = f > 0 ? (pattern_ + 1) % f : 0;
                } else if (pattern_ < frames - 1) {
                    pattern_++;
                } else {
                    refresh_motion();
                }
                motion_count_ = 0;
            }
            return;
        }
        if (!sv_enabled_) return;
        double speed = enemy_->enemy()["sideviewFrameSpeed"].num();
        if (speed == 0) speed = 12;
        if (motion_index_ >= 0 && ++motion_count_ >= speed) {
            if (sv_motion_loop(motion_index_)) pattern_ = (pattern_ + 1) % 4;
            else if (pattern_ < 2) pattern_++;
            else start_motion(enemy_->idle_motion());
            motion_count_ = 0;
        }
    }

    void update_breathing() {
        double sx = 0, sy = 0;
        if (enemy_->is_breathing()) {
            const Value& e = enemy_->enemy();
            double c = (double)rpg::frame_count() + sv_rand_;
            double s = e["sideviewBreathSpeed"].num();
            double rate_x = e["sideviewBreathXRate"].num();
            double rate_y = e["sideviewBreathYRate"].num();
            if (e["sideviewLinkBreathing"].truthy()) s /= enemy_->hp_rate();
            sx = std::cos(c / s) * rate_x;
            sy = std::cos(c / s) * rate_y;
        }
        double mirror = scale_x > 0 ? 1 : -1;
        scale_x = enemy_->sprite_scale_x() + sx;
        scale_x = std::abs(scale_x) * mirror;
        scale_y = enemy_->sprite_scale_y() + sy;
    }

    void set_cover(const g2d::Rect& r) {
        cover_ = r;
        has_cover_ = true;
        update_mask();
    }

    void update_mask() {
        if (!has_cover_) return;
        double sx = scale_x == 0 ? 1e-9 : scale_x;
        double sy = scale_y == 0 ? 1e-9 : scale_y;
        double x0 = (cover_.x - x) / sx, y0 = (cover_.y - y) / sy;
        double x1 = (cover_.x + cover_.width - x) / sx, y1 = (cover_.y + cover_.height - y) / sy;
        if (!mask_rect) mask_rect = std::make_shared<g2d::Rect>();
        *mask_rect = g2d::Rect(std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0));
    }

    double link_x() const override { return x; }
    double link_y() const override { return y; }
    double link_bitmap_width() const override { return has_own_size_ ? own_width_ : (bitmap_ ? bitmap_->width() : 0); }
    double link_bitmap_height() const override { return has_own_size_ ? own_height_ : (bitmap_ ? bitmap_->height() : 0); }
    void link_force_motion(const std::string& motion) override { force_motion(motion); }
    void link_refresh_motion() override { refresh_motion(); }
    void link_setup_weapon_animation() override {
        if (is_sideview_battler() && !sideview()["weapon"].truthy()) enemy_->clear_weapon_animation();
    }
    void link_setup_opacity_change(double target, double duration) override {
        duration = std::max(1.0, duration);
        opacity_target_ = target;
        opacity_dur_ = duration;
        opacity_rate_ = std::abs(opacity() - target) / duration;
        opacity_changing_ = true;
    }
    double link_float_height() const override { return 0; }
    double link_jump_height() const override { return 0; }
    void link_transform(rpg::GameBattler* battler) override {
        rpg::GameEnemy* e = static_cast<rpg::GameEnemy*>(battler);
        set_transform(e);
        set_battler(e);
    }
    void link_start_move(double tx, double ty, double duration) override {
        if (enemy_ && !enemy_->sprite_can_move()) return;
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
    js::Val link_js() override { return js::Val::null(); }

private:
    rpg::GameEnemy* enemy_ = nullptr;
    std::shared_ptr<g2d::Sprite> shadow_;
    std::shared_ptr<g2d::Sprite> main_;
    double home_x_ = 0, home_y_ = 0, offset_x_ = 0, offset_y_ = 0;
    bool has_target_offset_ = false;
    double target_offset_x_ = 0, target_offset_y_ = 0;
    double movement_duration_ = 0;
    double opacity_target_ = 255, opacity_dur_ = 0, opacity_rate_ = 0;
    bool opacity_changing_ = false;
    bool appeared_ = false;
    bool has_battler_name_ = false;
    std::string battler_name_;
    int battler_hue_ = 0;
    double sv_rand_ = 0;
    std::string sv_battler_name_;
    int motion_index_ = -1;
    int motion_count_ = 0;
    int pattern_ = 0;
    bool sv_enabled_ = false;
    bool created_dummy_main_ = false;
    bool adjust_main_bitmap_settings_ = false;
    std::string motion_name_;
    bool has_own_size_ = false;
    double own_width_ = 0, own_height_ = 0;
    bool has_cover_ = false;
    g2d::Rect cover_;
};

class SpriteOmoBestiaryCharacter : public g2d::Sprite {
public:
    explicit SpriteOmoBestiaryCharacter(rpg::GameCharacter* character) : character_(character) {
        anchor_x = 0.5;
        anchor_y = 1;
    }

    void update() override {
        g2d::Sprite::update();
        visible = !character_->is_transparent();
        update_bitmap();
        update_frame();
        update_other();
        update_bobble();
    }

    void update_bitmap() {
        if (!has_image_ || character_name_ != character_->character_name() || character_index_ != character_->character_index()) {
            has_image_ = true;
            character_name_ = character_->character_name();
            character_index_ = character_->character_index();
            set_bitmap(load_character(character_name_));
            is_big_ = is_big_character(character_name_);
        }
    }

    double pattern_width() {
        if (character_->has_custom_frame_w) return character_->custom_frame_w;
        double pw = is_big_ ? bitmap()->width() / 3.0 : bitmap()->width() / 12.0;
        if (character_->is_emf) return (pw * 3) / character_->emf.frame_num;
        return pw;
    }

    double pattern_height() {
        if (character_->has_custom_frame_h) return character_->custom_frame_h;
        return is_big_ ? bitmap()->height() / 4.0 : bitmap()->height() / 8.0;
    }

    double block_x() {
        if (character_->has_custom_frame_x) return character_->custom_frame_x;
        if (is_big_) return 0;
        return character_->character_index() % 4 * 3;
    }

    double block_y() {
        if (character_->has_custom_frame_y) return character_->custom_frame_y;
        if (is_big_) return 0;
        return std::floor(character_->character_index() / 4.0) * 4;
    }

    double pattern_x() {
        if (character_->has_custom_frame_x) return 0;
        return character_->pattern();
    }

    double pattern_y() {
        if (character_->has_custom_frame_y) return 0;
        return (character_->direction() - 2) / 2.0;
    }

    void update_frame() {
        if (!bitmap()) return;
        if (character_->is_single_frame()) {
            set_frame(0, 0, bitmap()->width(), bitmap()->height());
            return;
        }
        double pw = pattern_width();
        double ph = pattern_height();
        double sx = (block_x() + pattern_x()) * pw;
        double sy = (block_y() + pattern_y()) * ph;
        set_frame(sx, sy, pw, ph);
    }

    void update_other() {
        set_opacity(character_->opacity());
        blend_mode = character_->blend_mode();
    }

    void update_bobble() {
        if (character_->has_bobble) {
            const rpg::Bobble& b = character_->bobble;
            anchor_y = b.base + (std::sin(rpg::frame_count() * b.speed) * b.amplitude);
        } else {
            anchor_y = 1;
        }
    }

private:
    rpg::GameCharacter* character_;
    bool has_image_ = false;
    std::string character_name_;
    int character_index_ = 0;
    bool is_big_ = false;
};

class WindowOmoBestiaryEnemy : public WindowBase {
public:
    void initialize(rpg::GameEnemy* enemy) {
        WindowBase::initialize(0, 0, 640 / 2, 480);
        enemy_ = enemy;
        create_cover_mask();
        create_background_sprite();
        create_enemy_sprite();
    }
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 5; }

    void create_cover_mask() {
        double padding = standard_padding();
        cover_ = g2d::Rect(padding, padding, width() - (padding * 2), height() - (padding * 2));
    }

    void create_background_sprite() {
        background_sprite = std::make_shared<g2d::Sprite>(load_battleback1("battleback_vf_default"));
        background_sprite->mask_rect = std::make_shared<g2d::Rect>();
        update_background_mask();
        add_child(background_sprite);
    }

    void create_enemy_sprite() {
        enemy_sprite = std::make_shared<SpriteOmoBestiaryEnemy>(enemy_);
        enemy_sprite->set_cover(cover_);
        add_child(enemy_sprite);
    }

    void update_background_mask() {
        *background_sprite->mask_rect = g2d::Rect(cover_.x - background_sprite->x, cover_.y - background_sprite->y, cover_.width, cover_.height);
    }

    void set_background(const std::string* name, double x = 0, double y = 0) {
        if (name && !name->empty()) {
            background_sprite->set_bitmap(load_battleback1(*name));
            background_sprite->x = x;
            background_sprite->y = y;
        } else {
            background_sprite->set_bitmap(nullptr);
        }
        update_background_mask();
        op_delay_ = 6;
    }

    void clear_opacity() {
        enemy_sprite->set_opacity(0);
        background_sprite->set_opacity(0);
    }

    void update_be_opacity() {
        if (enemy_sprite->opacity() >= 255 && background_sprite->opacity() >= 255) return;
        enemy_sprite->set_opacity(std::min(enemy_sprite->opacity() + 8, 255.0));
        background_sprite->set_opacity(std::min(background_sprite->opacity() + 8, 255.0));
    }

    void update() override {
        WindowBase::update();
        if (--op_delay_ > 0) return;
        update_be_opacity();
    }

    std::shared_ptr<g2d::Sprite> background_sprite;
    std::shared_ptr<SpriteOmoBestiaryEnemy> enemy_sprite;

private:
    rpg::GameEnemy* enemy_ = nullptr;
    g2d::Rect cover_;
    int op_delay_ = 0;
};

class WindowOmoBestiaryEnemyName : public WindowBase {
public:
    void initialize() { WindowBase::initialize(0, 0, 640 / 2, 48); }
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 4; }
    void draw_name(const std::string& name) {
        c().clear();
        c().draw_text(name, 15, -5, c().width() - 30, c().height(), "left");
    }
};

class WindowOmoBestiaryEnemyText : public WindowBase {
public:
    void initialize() {
        WindowBase::initialize(0, 0, 640 / 2, 480 - 48);
        create_character();
    }
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }

    void create_character() {
        enemy_character = std::make_shared<rpg::GameCharacter>();
        enemy_character->set_image("", 0);
        enemy_character->set_walk_anime(true);
        enemy_character->set_step_anime(true);
        character_sprite = std::make_shared<SpriteOmoBestiaryCharacter>(enemy_character.get());
        add_child(character_sprite);
    }

    void draw_information(const std::string& information) {
        c().clear();
        std::vector<std::string> lines = split_lines(information);
        for (size_t i = 0; i < lines.size(); i++) draw_text(lines[i], 0, -10 + (i * 24.0), c().width());
    }

    void draw_lines(const std::vector<std::string>& lines) {
        c().clear();
        for (size_t i = 0; i < lines.size(); i++) draw_text(lines[i], 0, -10 + (i * 28.0), c().width());
    }

    void update() override {
        WindowBase::update();
        update_character();
    }

    void update_character() {
        enemy_character->update();
        character_sprite->x = width() - (character_sprite->frame().width / 2) - 10;
        character_sprite->y = height() - 10;
    }

    std::shared_ptr<rpg::GameCharacter> enemy_character;
    std::shared_ptr<SpriteOmoBestiaryCharacter> character_sprite;
};

class WindowOmoBestiaryEnemyList : public WindowCommand {
public:
    void initialize() {
        sorted_.clear();
        const Value& info = bestiary_information();
        for (const Member* m = info.begin_members(); m != info.end_members(); ++m) sorted_.push_back({(int)js::string_to_number(m->key), &m->v});
        std::stable_sort(sorted_.begin(), sorted_.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::stable_sort(sorted_.begin(), sorted_.end(), [](const auto& a, const auto& b) {
            const Value* la = (*a.second).find("listIndex");
            const Value* lb = (*b.second).find("listIndex");
            double ia = la ? la->num() : a.first;
            double ib = lb ? lb->num() : b.first;
            return ia < ib;
        });
        WindowCommand::initialize(0.0, 0.0);
    }
    void initialize(double x, double y) override { WindowCommand::initialize(x, y); }
    void initialize(double x, double y, double w, double h) override { WindowCommand::initialize(x, y, w, h); }
    double window_width() override { return 640 / 2.0; }
    double window_height() override { return 480 - 48; }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    double custom_cursor_rect_x_offset() override { return 14; }
    double custom_cursor_rect_y_offset() override { return 0; }
    double custom_cursor_rect_text_x_offset() override { return 30; }
    double custom_cursor_rect_text_y_offset() override { return -7; }
    double custom_cursor_rect_text_width_offset() override { return 0; }
    std::string custom_cursor_rect_bitmap_name() override { return "cursor_menu"; }

    int enemy_id(int index) {
        if (index < 0 || index >= (int)list_.size()) return 0;
        return list_[(size_t)index].ext.to_int();
    }
    int enemy_id() { return enemy_id(index()); }

    void select_next_enemy() {
        int idx = index() + 1;
        if (idx >= max_items()) idx = 0;
        for (int i = idx; i < max_items(); i++) {
            if (list_[(size_t)i].ext.to_int() != 0) {
                play_se("SE_turn_page", 90, 100, 0);
                select(i);
                break;
            }
        }
    }

    void select_previous_enemy() {
        int idx = (index() - 1) < 0 ? max_items() - 1 : index() - 1;
        for (int i = idx; i >= 0; i--) {
            if (list_[(size_t)i].ext.to_int() != 0) {
                play_se("SE_turn_page", 90, 100, 0);
                select(i);
                break;
            }
        }
    }

    void make_command_list() override {
        const std::vector<int>& defeated = rpg::game_party().defeated_enemies;
        for (const auto& entry : sorted_) {
            int idx = entry.first;
            if (std::find(defeated.begin(), defeated.end(), idx) != defeated.end()) {
                add_command(enemy_name(*entry.second), "ok", true, js::Val::num(idx));
            } else {
                add_command("------------------------------", "nothing", false, js::Val::num(0));
            }
        }
    }

    static std::string enemy_name(const Value& obj) {
        const Value* names = obj.find("conditionalNames");
        if (names && names->truthy()) {
            for (size_t i = 0; i < names->size(); i++) {
                const Value& d = (*names)[i];
                const Value& switches = d["switches"];
                bool ok = true;
                for (size_t k = 0; k < switches.size(); k++) {
                    const Value& arr = switches[k];
                    const Value& want = arr[1];
                    bool cur = rpg::game_switches().value(arr[0].as_int());
                    if (!(want.t == Value::Bool && want.b == cur)) {
                        ok = false;
                        break;
                    }
                }
                if (ok) return data::js_string(d["name"]);
            }
        }
        return data::js_string(obj["name"]);
    }

    void draw_item(int index) override {
        ItemRect r = item_rect_for_text(index);
        std::string align = item_text_align();
        reset_text_color();
        change_paint_opacity(true);
        draw_text(command_name(index), r.x, r.y, r.width - r.x, align);
    }

    void select(int index) override {
        WindowCommand::select(index);
        if (active && on_cursor_change) on_cursor_change();
    }

    std::function<void()> on_cursor_change;

protected:
    void update_transform_hook() override {
        double w = width(), h = height();
        double p = 28, q = p / 2;
        down_arrow_sprite()->move(w - q, h - q);
        up_arrow_sprite()->move(w - q, q);
    }

private:
    std::vector<std::pair<int, const Value*>> sorted_;
};

class SceneOmoriBestiary : public SceneBaseEX {
public:
    SceneOmoriBestiary() { enemy_ = std::make_shared<rpg::GameEnemy>(1, 0, 0); }

    ~SceneOmoriBestiary() override {
        remove_children();
        enemy_window_.reset();
        if (enemy_) enemy_->sprite_link = nullptr;
    }

    const char* name() const override { return "Scene_OmoriBestiary"; }

    void start() override {
        SceneBaseEX::start();
        start_fade_in(slow_fade_speed(), false);
    }

    void create() override {
        SceneBaseEX::create();
        create_enemy_window();
        create_enemy_name_window();
        create_enemy_list_window();
        enemy_text_window_ = WindowBase::make<WindowOmoBestiaryEnemyText>();
        enemy_text_window_->y = 480 - enemy_text_window_->height();
        enemy_text_window_->x = 640 - enemy_text_window_->width();
        enemy_text_window_->visible = false;
        add_child(enemy_text_window_);
        on_list_change_update();
    }

    void create_enemy_window() {
        enemy_window_ = std::shared_ptr<WindowOmoBestiaryEnemy>(new WindowOmoBestiaryEnemy());
        enemy_window_->initialize(enemy_.get());
        add_child(enemy_window_);
    }

    void create_enemy_name_window() {
        enemy_name_window_ = WindowBase::make<WindowOmoBestiaryEnemyName>();
        enemy_name_window_->x = 640 - enemy_name_window_->width();
        add_child(enemy_name_window_);
    }

    void create_enemy_list_window() {
        enemy_list_window_ = WindowBase::make<WindowOmoBestiaryEnemyList>();
        enemy_list_window_->x = 640 - enemy_list_window_->width();
        enemy_list_window_->y = 480 - enemy_list_window_->height();
        enemy_list_window_->set_handler("ok", [this] { on_enemy_list_ok(); });
        enemy_list_window_->set_handler("cancel", [this] { pop_scene(); });
        enemy_list_window_->on_cursor_change = [this] { on_list_change_update(); };
        add_child(enemy_list_window_);
    }

    void update() override {
        SceneBaseEX::update();
        if (enemy_text_window_->visible) {
            if (input::is_triggered(input::CANCEL)) {
                play_cancel();
                enemy_list_window_->on_cursor_change = nullptr;
                enemy_list_window_->activate();
                enemy_text_window_->visible = false;
                enemy_list_window_->on_cursor_change = [this] { on_list_change_update(); };
                return;
            }
            if (input::is_triggered(input::LEFT)) {
                enemy_list_window_->select_previous_enemy();
                on_list_change_update();
                on_enemy_list_ok();
            }
            if (input::is_triggered(input::RIGHT)) {
                enemy_list_window_->select_next_enemy();
                on_list_change_update();
                on_enemy_list_ok();
            }
        }
    }

    void on_list_change_update() {
        int enemy_id = enemy_list_window_->enemy_id();
        SpriteOmoBestiaryEnemy* enemy_sprite = enemy_window_->enemy_sprite.get();
        if (enemy_id > 0) {
            enemy_window_->clear_opacity();
            enemy_sprite->remove_children();
            enemy_->transform(enemy_id);
            const Value& data = bestiary_entry(enemy_id);
            const Value& background = data["background"];
            enemy_name_window_->draw_name(WindowOmoBestiaryEnemyList::enemy_name(data));
            enemy_sprite->set_home(data["position"]["x"].num(), data["position"]["y"].num());
            enemy_sprite->visible = true;
            enemy_sprite->start_motion("other");
            enemy_sprite->update();
            std::string bg_name = background["name"].is_null() ? std::string() : data::js_string(background["name"]);
            enemy_window_->set_background(&bg_name, background["x"].num(), background["y"].num());
        } else {
            enemy_sprite->set_home(-640, -480);
            enemy_name_window_->draw_name(data::js_string(lang_pinned("Bestiary", "EmptyEnemyName")));
            enemy_window_->set_background(nullptr);
        }
    }

    void on_enemy_list_ok() {
        int enemy_id = enemy_list_window_->enemy_id();
        const Value& data = bestiary_entry(enemy_id);
        enemy_text_window_->visible = true;
        std::vector<std::string> lines = split_lines(data::js_string(data["text"]));
        const Value* conditional = data.find("conditionalText");
        if (conditional && conditional->truthy()) {
            for (size_t i = 0; i < conditional->size(); i++) {
                const Value& text_data = (*conditional)[i];
                const Value& ids = text_data["switchIds"];
                bool all = true;
                for (size_t k = 0; k < ids.size(); k++) {
                    if (!rpg::game_switches().value(ids[k].as_int())) {
                        all = false;
                        break;
                    }
                }
                if (!all) continue;
                long len = (long)lines.size();
                long start = 0;
                const Value* line = text_data.find("line");
                if (line && line->is_null()) {
                    start = len;
                } else if (line) {
                    long n = (long)line->num();
                    start = n < 0 ? std::max(len + n, 0L) : std::min(n, len);
                }
                std::vector<std::string> extra = split_lines(data::js_string(text_data["text"]));
                lines.insert(lines.begin() + start, extra.begin(), extra.end());
            }
        }
        enemy_text_window_->draw_lines(lines);
        rpg::GameCharacter* character = enemy_text_window_->enemy_character.get();
        auto& sprite = enemy_text_window_->character_sprite;
        const Value* ch = data.find("character");
        if (ch && ch->truthy()) character->set_image(data::js_string((*ch)["name"]), (*ch)["index"].as_int());
        else character->set_image("", 0);
        sprite->update();
        enemy_text_window_->update_character();
        enemy_text_window_->character_sprite->update();
    }

private:
    std::shared_ptr<rpg::GameEnemy> enemy_;
    std::shared_ptr<WindowOmoBestiaryEnemy> enemy_window_;
    std::shared_ptr<WindowOmoBestiaryEnemyName> enemy_name_window_;
    std::shared_ptr<WindowOmoBestiaryEnemyList> enemy_list_window_;
    std::shared_ptr<WindowOmoBestiaryEnemyText> enemy_text_window_;
};

}

rpg::SceneFactory bestiary_scene() {
    return rpg::SceneFactory{"Scene_OmoriBestiary", [] { return std::unique_ptr<rpg::Scene>(new menu::SceneOmoriBestiary()); }};
}

static SceneRegistrar r_scene_omori_bestiary("Scene_OmoriBestiary", bestiary_scene());

}
