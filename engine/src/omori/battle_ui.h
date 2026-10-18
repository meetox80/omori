#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../gfx2d/display.h"
#include "../rpg/action.h"
#include "../rpg/battle.h"
#include "../rpg/battler.h"
#include "../rpg/enemy.h"
#include "../rpg/scene.h"
#include "display_js.h"
#include "fx.h"
#include "menu_common.h"
#include "spriteset.h"
#include "sprites.h"
#include "window_selectable.h"

namespace rpg {
class FilterController;
}

namespace ui {

class WindowMessage;
class WindowScrollText;

namespace btl {

class SceneBattle;
class SpritesetBattle;
class WindowBattleLog;
class WindowBattleStatus;
class WindowChainSkillList;

struct Refs {
    SceneBattle* scene = nullptr;
    rpg::Scene* scene_node = nullptr;
    WindowBase* status_base = nullptr;
    SpritesetBattle* spriteset = nullptr;
    WindowBattleLog* log = nullptr;
    WindowBattleStatus* status = nullptr;
    WindowChainSkillList* chain = nullptr;
};

Refs& refs();

class ToneOverlay : public g2d::Filter {
public:
    const double* tone = nullptr;
    void apply(g2d::DisplayObject& obj, const g2d::RenderState& rs, const std::function<void(const g2d::RenderState&)>& draw) override;
};

using ToneOverlayPtr = std::shared_ptr<ToneOverlay>;

void set_filters(g2d::DisplayObject* obj, std::initializer_list<g2d::FilterPtr> list);
bool has_filters(g2d::DisplayObject* obj);

double frame_count();
double random_unit();
int random_int(int max);

class SpriteDamage : public g2d::Sprite {
public:
    SpriteDamage();
    void setup(rpg::GameBattler* target);
    void setup_critical_effect();
    double digit_width() const;
    double digit_height() const;
    void create_miss();
    void create_immune();
    void create_digits(int base_row, double value);
    g2d::SpritePtr create_child_sprite();
    void update() override;
    void update_child(g2d::Sprite* sprite);
    void update_flash();
    void update_opacity();
    bool is_playing() const { return duration_ > 0; }

private:
    int duration_ = 90;
    double flash_color_[4] = {0, 0, 0, 0};
    int flash_duration_ = 0;
    g2d::BitmapPtr damage_bitmap_;
};

class SpriteStateIcon : public g2d::Sprite {
public:
    SpriteStateIcon();
    void setup(rpg::GameBattler* battler) { battler_ = battler; }
    void update() override;
    void update_icon();
    void update_frame();
    void update_turn_and_counter();
    void update_mirror();
    void draw_state_turns(const Value& state);
    void draw_state_counter(const Value& state);
    void draw_buff_turns(int param_id);
    void draw_buff_rate(int param_id);
    std::string text_color(int n);

private:
    rpg::GameBattler* battler_ = nullptr;
    int icon_index_ = 0;
    int animation_count_ = 0;
    int animation_index_ = 0;
    std::shared_ptr<g2d::Sprite> turn_counter_sprite_;
};

class SpriteStateOverlay : public SpriteBase {
public:
    SpriteStateOverlay();
    void setup(rpg::GameBattler* battler) { battler_ = battler; }
    void update() override;
    void update_pattern();
    void update_frame();
    void update_mirror();

private:
    rpg::GameBattler* battler_ = nullptr;
    int overlay_index_ = 0;
    int animation_count_ = 0;
    int pattern_ = 0;
};

class SpriteWeapon : public SpriteBase {
public:
    SpriteWeapon();
    void setup(int weapon_image_id);
    void update() override;
    void update_pattern();
    void load_bitmap();
    void update_frame();
    bool is_playing() const { return weapon_image_id_ > 0; }

private:
    int weapon_image_id_ = 0;
    int animation_count_ = 0;
    int pattern_ = 0;
};

class SpriteEnemyBattlerStatus : public g2d::Sprite {
public:
    SpriteEnemyBattlerStatus();
    double min_width() const { return 120; }
    void refresh_bitmap(rpg::GameEnemy* battler);
    void set_cursor_position(int position);
    std::shared_ptr<g2d::Sprite> cursor_sprite;
};

struct MotionDef {
    int index = 0;
    bool loop = false;
};

const MotionDef* actor_motion(const std::string& name);

class SpriteBattler : public SpriteBase, public rpg::BattlerSpriteLink, public DisplayJsExtra {
public:
    SpriteBattler();
    ~SpriteBattler() override;
    virtual void init_members();
    virtual void set_battler(rpg::GameBattler* b);
    rpg::GameBattler* battler() const { return battler_; }
    void set_home(double hx, double hy);
    void update() override;
    void update_visibility() override;
    virtual void update_main();
    virtual void update_bitmap() {}
    virtual void update_frame() {}
    virtual void update_move();
    virtual void update_position();
    virtual void update_animation();
    virtual void update_damage_popup();
    virtual void update_selection_effect();
    virtual void setup_animation();
    virtual void setup_damage_popup();
    virtual double damage_offset_x() { return 0; }
    virtual double damage_offset_y() { return 0; }
    virtual void start_move(double tx, double ty, int duration);
    virtual void on_move_end() {}
    virtual bool is_effecting() { return false; }
    bool is_moving() const { return movement_duration_ > 0; }
    bool in_home_position() const { return offset_x_ == 0 && offset_y_ == 0; }
    void push_damage_sprite(const std::shared_ptr<SpriteDamage>& s);
    virtual void start_motion(const std::string& motion) { (void)motion; }
    virtual void force_motion(const std::string& motion) { (void)motion; }
    virtual void refresh_motion() {}
    virtual void setup_weapon_animation() {}
    virtual void set_transform(rpg::GameBattler* b) { (void)b; }
    bool is_popup_playing() const;
    void reset_float();
    void reset_opacity();
    void setup_float(double h, double dur);
    void setup_jump(double h, double dur);
    void setup_opacity_change(double target, double dur);
    void update_float();
    void update_state_sprites();
    virtual void update_weapon();
    void update_opacity();
    bool anti_opacity_change() const { return !opacity_changing_; }
    double get_float_height();
    double get_jump_height() const;
    bool is_floating() const { return float_dur_ > 0; }
    bool is_jumping() const { return jump_dur_ > 0; }
    bool is_changing_opacity() const { return opacity_dur_ > 0; }
    virtual double add_floating_height() { return 0; }
    void release_link();
    bool display_js_get(uint32_t key, js::Val& out) override;

    double link_x() const override { return x; }
    double link_y() const override { return y; }
    double link_bitmap_width() const override;
    double link_bitmap_height() const override;
    void link_force_motion(const std::string& motion) override { force_motion(motion); }
    void link_refresh_motion() override { refresh_motion(); }
    void link_setup_weapon_animation() override { setup_weapon_animation(); }
    void link_setup_opacity_change(double opacity, double duration) override { setup_opacity_change(opacity, duration); }
    double link_float_height() const override;
    double link_jump_height() const override { return get_jump_height(); }
    void link_transform(rpg::GameBattler* b) override;
    void link_start_move(double tx, double ty, double duration) override { start_move(tx, ty, (int)duration); }
    js::Val link_js() override;

    rpg::GameBattler* anim_battler() override { return battler_; }
    double anim_float_jump_rate() override { return get_float_height() + get_jump_height(); }
    double anim_texture_height() override { return height(); }

    std::shared_ptr<SpriteBase> main_sprite;
    std::shared_ptr<SpriteWeapon> weapon_sprite;
    std::shared_ptr<SpriteStateOverlay> state_sprite;
    std::shared_ptr<SpriteStateIcon> state_icon_sprite;
    std::shared_ptr<g2d::Sprite> shadow_sprite;
    std::vector<std::shared_ptr<SpriteDamage>> damages;
    double home_x() const { return home_x_; }
    double home_y() const { return home_y_; }

protected:
    rpg::GameBattler* battler_ = nullptr;
    double home_x_ = 0, home_y_ = 0;
    double offset_x_ = 0, offset_y_ = 0;
    double target_offset_x_ = 0, target_offset_y_ = 0;
    bool has_target_offset_ = false;
    int movement_duration_ = 0;
    int selection_effect_count_ = 0;
    bool post_initialized_ = false;
    double float_height_ = 0, float_target_ = 0, float_dur_ = 0, float_rate_ = 0;
    double jump_height_ = 0, jump_dur_ = 0, jump_full_ = 0;
    double opacity_target_ = 255, opacity_dur_ = 0, opacity_rate_ = 0;
    bool opacity_changing_ = false;
    fx::Horror horror_;
};

class SpriteActor : public SpriteBattler {
public:
    SpriteActor();
    void init_members() override;
    void set_battler(rpg::GameBattler* b) override;
    void move_to_start_position();
    void set_actor_home(int index);
    void update() override;
    void update_shadow();
    void update_main() override;
    void update_bitmap() override;
    void update_frame() override;
    void update_move() override;
    void update_motion();
    void update_motion_count();
    int motion_speed();
    void refresh_motion() override;
    void start_entry_motion();
    void start_motion(const std::string& motion) override;
    void force_motion(const std::string& motion) override;
    void setup_weapon_animation() override;
    void adjust_anchor();
    void setup_animation() override {}
    void setup_damage_popup() override {}
    double damage_offset_x() override { return -32; }
    void update_weapon() override;
    rpg::GameActor* actor() const { return actor_; }

private:
    rpg::GameActor* actor_ = nullptr;
    std::string battler_name_;
    const MotionDef* motion_ = nullptr;
    int motion_count_ = 0;
    int pattern_ = 0;
    std::string motion_name_;
    bool check_alive_status_ = false;
    void create_shadow_sprite();
    void create_weapon_sprite();
    void create_main_sprite();
    void create_state_sprite();
    const Value* current_sideview_motion();
    int sideview_frames();
    void update_sideview_frame();
    void update_sideview_motion_count();
};

class SpriteEnemy : public SpriteBattler {
public:
    explicit SpriteEnemy(std::shared_ptr<rpg::GameEnemy> enemy);
    void init_members() override;
    void set_battler(rpg::GameBattler* b) override;
    void update() override;
    void update_bitmap() override;
    void update_frame() override;
    void update_position() override;
    void update_state_sprite();
    void update_sv_state_sprite();
    void update_floating_state_sprite();
    void update_breathing();
    void update_sv_bitmap();
    void adjust_anchor();
    void update_scale();
    void update_sv_frame();
    void adjust_main_bitmap_settings(g2d::Bitmap* bitmap);
    void adjust_sv_shadow_settings();
    void update_motion();
    void setup_motion();
    void start_motion(const std::string& motion) override;
    void setup_weapon_animation() override;
    void update_motion_count();
    void refresh_motion() override;
    int motion_speed();
    int motion_frames();
    void update_selection_effect() override;
    bool is_sideview_collapse();
    void force_motion(const std::string& motion) override;
    void set_transform(rpg::GameBattler* b) override;
    void init_visibility();
    void setup_effect();
    void start_effect(const std::string& type);
    void update_effect();
    bool is_effecting() override { return has_effect_type_; }
    void revert_to_normal();
    void update_whiten();
    void update_blink();
    void update_appear();
    void update_disappear();
    void update_collapse();
    void update_boss_collapse();
    void update_instant_collapse();
    double damage_offset_x() override;
    double damage_offset_y() override;
    double add_floating_height() override;
    void update_status_sprite();
    void update_status_sprite_position();
    void shift_status_sprite_position(int sx, int sy);
    void load_bitmap(const std::string& name, int hue);
    rpg::GameEnemy* enemy() const { return enemy_; }
    std::shared_ptr<rpg::GameEnemy> enemy_ptr() const { return enemy_sp_; }

    bool appeared = false;
    int effect_duration = 0;
    std::shared_ptr<SpriteEnemyBattlerStatus> status_sprite;

private:
    std::shared_ptr<rpg::GameEnemy> enemy_sp_;
    rpg::GameEnemy* enemy_ = nullptr;
    std::string battler_name_;
    bool has_battler_name_ = false;
    int battler_hue_ = 0;
    std::string effect_type_;
    bool has_effect_type_ = false;
    double shake_ = 0;
    double sv_rand_ = 0;
    std::string sv_battler_name_;
    const MotionDef* motion_ = nullptr;
    int motion_count_ = 0;
    int pattern_ = 0;
    bool sv_battler_enabled_ = false;
    bool adjust_main_bitmap_settings_ = false;
    bool created_empty_main_sprite_ = false;
    std::string motion_name_;
    bool fall_on_death_ = false;
    bool has_fall_locked_pattern_ = false;
    int fall_locked_pattern_ = 0;
    std::vector<g2d::DisplayPtr> sv_children_;
    void init_sv_sprites();
    void create_shadow_sprite();
    void create_weapon_sprite();
    void create_main_sprite();
    void create_state_sprite();
    void start_appear();
    void start_disappear();
    void start_whiten();
    void start_blink();
    void start_collapse();
    void start_boss_collapse();
    void start_instant_collapse();
    bool is_sideview_battler() const;
    const Value& sideview_data() const;
    const Value* current_sideview_motion() const;
    void update_sideview_motion_count();
    void update_sideview_frame();
};

class SpriteImprovedBattleback : public g2d::TilingSprite {
public:
    SpriteImprovedBattleback(g2d::BitmapPtr b, int type);
    void setup(g2d::Sprite* battle_field, SpriteImprovedBattleback* sprite1);
    void update() override;
    void update_initial_location();
    void setup_scaling();
    void setup_location_x();
    void setup_location_y();
    void update_scroll();
    void reset_scroll_speeds();
    void set_scroll_speed_x(double v) { scroll_speed_x_ = v; }
    void set_scroll_speed_y(double v) { scroll_speed_y_ = v; }
    void change_bitmap(g2d::BitmapPtr b);
    void reset_fade_settings();
    void set_fade_out(double duration);
    void set_fade_in(double duration);
    void update_fade_out();
    void update_fade_in();
    void set_opacity_value(double value);

private:
    int type_ = 1;
    bool initial_location_setup_ = false;
    g2d::Sprite* battle_field_ = nullptr;
    SpriteImprovedBattleback* sprite1_ = nullptr;
    double scroll_speed_x_ = 0, scroll_speed_y_ = 0;
    int fade_out_duration_ = 0, fade_in_duration_ = 0;
};

class SpritesetBattle : public SpritesetBase, public rpg::battle::Spriteset, public DisplayJsExtra {
public:
    SpritesetBattle();
    ~SpritesetBattle() override;
    void create_lower_layer() override;
    void create_background();
    void create_battle_field();
    void create_battleback();
    void create_enemies();
    void create_actors();
    std::string battleback1_name();
    std::string battleback2_name();
    void update() override;
    void update_actors();
    void update_battleback();
    void update_z_coordinates();
    void update_position() override;
    std::vector<SpriteBattler*> battler_sprites();
    bool is_animation_playing();
    bool is_effecting();
    bool is_anyone_moving();
    bool is_anyone_floating();
    bool is_anyone_jumping();
    bool is_anyone_changing_opacity();
    void remove_enemies();
    void update_battleback_group_remove();
    void update_battleback_group_add();
    void update_battleback_change_to();
    void update_battleback_removal();
    void release_links();
    const js::Val* horror_filters_source() override;
    bool display_js_get(uint32_t key, js::Val& out) override;

    bool is_busy() override { return false; }
    bool is_popup_playing() override;
    void refresh_enemy_reinforcements() override;
    void change_battleback_to(int id, const std::string& folder, const std::string& file, int hue) override;
    void battleback_fade_in(int id, int duration) override;
    void battleback_fade_out(int id, int duration) override;
    void battleback_opacity(int id, int value) override;
    void reset_scroll_speeds(int id) override;
    void set_battleback_scroll_speed_x(int id, int speed) override;
    void set_battleback_scroll_speed_y(int id, int speed) override;
    void add_new_battleback(int id, const std::string& folder, const std::string& file, int hue, double opacity, int duration) override;
    void remove_battleback(int id) override;
    void set_picture_layer(const std::string& layer) override;
    js::Val js() override;

    std::shared_ptr<g2d::Sprite> background_sprite;
    std::shared_ptr<g2d::Sprite> battle_field;
    std::shared_ptr<SpriteImprovedBattleback> back1_sprite, back2_sprite;
    std::vector<std::shared_ptr<SpriteImprovedBattleback>> battleback_sprites;
    std::vector<g2d::BitmapPtr> loading_images;
    std::vector<int> battleback_to_remove;
    std::vector<std::shared_ptr<SpriteEnemy>> enemy_sprites;
    std::vector<std::shared_ptr<SpriteActor>> actor_sprites;
    rpg::Scene* scene = nullptr;

private:
    js::Val horror_source_;
};

}

rpg::SceneFactory battle_scene();

}
