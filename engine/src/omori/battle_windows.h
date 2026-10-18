#pragma once
#include <array>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "battle_ui.h"

namespace ui {
namespace btl {

class WindowScrollingTextSource : public WindowBase {
public:
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 0; }
    int standard_font_size() override;
    double calc_text_height(const TextState& ts, bool all) override { return WindowBase::calc_text_height(ts, all) - 5; }
    void process_escape_character(const std::string& code, TextState& ts) override;
    TextState draw_line(const std::string& text, double x, double y);
};

class SpriteScrollingText : public g2d::Sprite {
public:
    SpriteScrollingText();
    void clear();
    double line_width() const { return width(); }
    std::shared_ptr<g2d::Sprite> add_line(const std::string& text);
    void update() override;
    void update_lines();

    std::shared_ptr<g2d::Sprite> line_sprites_container;
    std::vector<std::shared_ptr<g2d::Sprite>> list;
    std::shared_ptr<WindowScrollingTextSource> source;
};

class WindowBattleLog : public WindowSelectable, public rpg::battle::BattleLog, public DisplayJsExtra {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }
    double window_width() const { return 360; }
    double window_height() const { return 93; }
    void update() override;
    bool update_wait();
    bool update_wait_count();
    bool update_wait_mode();
    bool update_input_wait();
    void set_wait_mode(const std::string& mode) { wait_mode_ = mode; }
    void call_next_method();
    void push_fn(const std::string& name, std::function<void()> fn);
    void call_named(const std::string& name, std::vector<js::Val>& args);
    void wait() { wait_count_ = 16; }
    void push_base_line() { base_line_stack_.push_back((int)lines_.size()); }
    void pop_base_line();
    void add_instant_text(const std::string& text);
    void show_chain_skill_list(rpg::GameBattler* subject, const rpg::ItemRef& skill);
    void hide_chain_skill_list();
    void start_chain_skill_input_wait();
    void update_chain_skill_input();
    void display_critical(rpg::GameBattler* target);
    void display_damage(rpg::GameBattler* target);
    void display_miss(rpg::GameBattler* target);
    void display_evasion(rpg::GameBattler* target);
    void display_hp_damage(rpg::GameBattler* target);
    void display_mp_damage(rpg::GameBattler* target);
    void display_tp_damage(rpg::GameBattler* target);
    void display_affected_status(rpg::GameBattler* target);
    void display_removed_states(rpg::GameBattler* target);
    void display_changed_buffs(rpg::GameBattler* target);
    void display_buffs(rpg::GameBattler* target, const std::vector<int>& buffs, const std::string& fmt);
    void display_failure(rpg::GameBattler* target);
    std::string make_hp_damage_text(rpg::GameBattler* target);
    std::string make_mp_damage_text(rpg::GameBattler* target);
    std::string make_tp_damage_text(rpg::GameBattler* target);
    std::string make_custom_action_text(rpg::GameBattler* subject, rpg::GameBattler* target, const rpg::ItemRef& item);
    void push_add_text(const std::string& text);
    void push_wait();
    void push_battler_call(const std::string& name, rpg::GameBattler* b, void (*fn)(rpg::GameBattler*));

    void push(const std::string& method, std::vector<js::Val> args = {}) override;
    std::string last_method() const override;
    bool is_busy() override;
    void start_turn() override;
    void start_action(rpg::GameBattler* subject, rpg::GameAction* action, const std::vector<rpg::GameBattler*>& targets) override;
    void end_action(rpg::GameBattler* subject) override;
    void display_action_results(rpg::GameBattler* subject, rpg::GameBattler* target) override;
    void display_counter(rpg::GameBattler* target) override;
    void display_reflection(rpg::GameBattler* target) override;
    void display_substitute(rpg::GameBattler* substitute, rpg::GameBattler* target) override;
    void display_auto_affected_status(rpg::GameBattler* target) override;
    void display_current_state(rpg::GameBattler* subject) override;
    void display_regeneration(rpg::GameBattler* subject) override;
    void display_action(rpg::GameBattler* subject, const rpg::ItemRef& item) override;
    void display_added_states(rpg::GameBattler* target) override;
    void display_custom_action_text(rpg::GameBattler* subject, rpg::GameBattler* target, const rpg::ItemRef& item) override;
    void clear() override;
    void add_text(const std::string& text) override;
    void push_line(const std::string& text) override { lines_.push_back(text); }
    int& wait_count() override { return wait_count_; }
    void show_normal_animation(const std::vector<rpg::GameBattler*>& targets, int id, bool mirror) override;
    void show_attack_animation(rpg::GameBattler* subject, const std::vector<rpg::GameBattler*>& targets) override;
    void show_actor_atk_ani_mirror(rpg::GameBattler* subject, const std::vector<rpg::GameBattler*>& targets) override;
    void show_animation(rpg::GameBattler* subject, const std::vector<rpg::GameBattler*>& targets, int id) override;
    void perform_action(rpg::GameBattler* subject, rpg::GameAction* action) override;
    void perform_action_end(rpg::GameBattler* subject) override;
    void perform_action_start(rpg::GameBattler* subject, rpg::GameAction* action) override;
    void wait_for_animation() override { set_wait_mode("animation"); }
    void wait_for_effect() override { set_wait_mode("effect"); }
    void wait_for_movement() override { set_wait_mode("movement"); }
    void wait_for_new_line() override;
    void wait_for_popups() override { set_wait_mode("popups"); }
    void wait_for_float() override { set_wait_mode("float"); }
    void wait_for_jump() override { set_wait_mode("jump"); }
    void wait_for_opacity() override { set_wait_mode("opacity"); }
    void wait_for_camera() override { set_wait_mode("camera"); }
    void wait_for_zoom() override { set_wait_mode("zoom"); }
    void set_visible(bool v) override { g2d::DisplayObject::visible = v; }
    bool visible() const override { return g2d::Window::visible; }
    js::Val js() override;
    bool display_js_get(uint32_t key, js::Val& out) override;

    std::shared_ptr<SpriteScrollingText> scroll_text_sprite;

private:
    struct Method {
        std::string name;
        std::function<void()> fn;
    };
    std::vector<std::string> lines_;
    std::deque<Method> methods_;
    int wait_count_ = 0;
    std::string wait_mode_;
    std::vector<int> base_line_stack_;
    bool waiting_for_input_ = false;
    int active_chain_skill_input_counter_ = 0;
    int active_chain_skill_ = 0;
    bool multi_hit_flag_ = false;
    int action_icon_ = 0;
    std::shared_ptr<g2d::Sprite> back_sprite_;
};

class SpriteParticle : public g2d::Sprite {
public:
    struct Phase {
        int duration = 0;
        double x = 0, y = 0, rotation = 0, opacity = 0, scale_x = 0, scale_y = 0;
        bool shake = false;
    };
    explicit SpriteParticle(g2d::BitmapPtr b) : g2d::Sprite(std::move(b)) {}
    void setup(const std::vector<Phase>& phases) { phases_ = phases; }
    bool is_finished() const { return phase_index_ >= phases_.size(); }
    void update() override;

    double base_x = 0, angry_offset = 0, angry_speed = 0;

private:
    size_t phase_index_ = 0;
    std::vector<Phase> phases_;
};

class SpriteBattleFaceStatusEmitter : public g2d::Sprite {
public:
    SpriteBattleFaceStatusEmitter();
    void activate() { active_ = true; }
    void deactivate() { active_ = false; }
    void clear();
    void setup_generator(const std::string& type);
    void generate_angry();
    void update() override;

private:
    bool active_ = false;
    int intensity_ = 1, intensity_variance_ = 0;
    bool has_generator_ = false;
    int spawn_timer_ = 0, spawn_timer_variance_ = 0, spawn_timer_count_ = 0;
    size_t child_limit_ = 0;
};

class SpriteBattleAnimation : public SpriteAnimation {
public:
    void set_home_position(double hx, double hy) {
        home_x_ = hx;
        home_y_ = hy;
    }

private:
    double home_x_ = 100, home_y_ = 0, home_w_ = 114, home_h_ = 164;
    void update_position() override;
    double offset_x() const;
    double offset_y() const;
};

class SpriteBattleFaceAnimation : public SpriteBase {
public:
    std::shared_ptr<SpriteBattleAnimation> start_face_animation(const Value* animation, bool mirror, double delay);
};

struct FaceLayers {
    std::shared_ptr<g2d::Sprite> container;
    std::shared_ptr<g2d::Sprite> behind, status_back, face, polaroid, front;
    std::shared_ptr<SpriteBattleFaceAnimation> animation[5];
};

class SpriteACSBubble : public g2d::Sprite {
public:
    explicit SpriteACSBubble(int actor_index);
    void activate_bubble();
    void deactivate_bubble();
    void set_arrow_direction(const std::string& direction);
    void set_bubble_index(int index);
    void update_position(int position);
    void fade_opacity(double opacity, int duration);
    void fade_show(int duration = 15) { fade_opacity(255, duration); }
    void start_shake() { shake_ = true; }
    void stop_shake();
    void update() override;
    void update_arrow_direction();
    void update_opacity();
    void update_shake();
    const std::string& arrow_key() const { return arrow_key_; }

private:
    bool active_ = true;
    int actor_index_ = 0;
    int bubble_index_ = 0;
    double target_opacity_ = 0;
    int opacity_duration_ = 0;
    std::string arrow_key_;
    int arrow_move_direction_ = 0;
    void apply_arrow_direction(int direction);
    double arrow_positions_[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    bool shake_ = false;
    std::shared_ptr<g2d::Sprite> arrow_sprite_;
};

class WindowOmoriBattleActorStatus : public WindowBase {
public:
    void initialize(int index, FaceLayers* layers, double x, double y);
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 0; }
    double window_width() const { return 114; }
    double window_height() const { return 164; }
    rpg::GameActor* actor();
    void create_sprites();
    void create_status_particle_emitters();
    void create_acs_bubble_sprites();
    void refresh();
    void draw_hp(double hp, double max_hp);
    void draw_mp(double mp, double max_mp);
    void update() override;
    void update_positions();
    void set_status_header(int index);
    void set_status_back(int index, bool fade = true);
    void setup_status_particles(const std::map<std::string, std::string>& data);
    void start_face_animation(int layer, const Value* animation);
    void update_animation();
    void update_status_back_fade();
    void update_damage();
    void update_bars();
    void update_overlay();
    void show_acs_bubbles(int duration = 15);
    void hide_acs_bubbles(int duration = 15);
    void setup_acs_bubbles(const std::vector<std::pair<std::string, int>>& list);
    void select_acs_bubble(const std::string& direction);
    void update_acs_bubbles();

    double home_x = 0, home_y = 0;
    bool selected = false;
    std::shared_ptr<menu::SpriteOmoMenuStatusFace> face_sprite;
    std::shared_ptr<g2d::Sprite> acs_bubble_container;

private:
    struct Anim {
        double current = -2, target = 0, old = -1;
        int duration = 0;
    };
    FaceLayers* layers_ = nullptr;
    int actor_index_ = 0;
    Anim hp_anim_, mp_anim_;
    int overlay_side_ = 0, overlay_delay_ = 15;
    int acs_bubble_opacity_duration_ = 0;
    double acs_bubble_opacity_ = 0;
    int popup_count_ = 0;
    std::shared_ptr<g2d::Sprite> status_back_sprite_, polaroid_sprite_, hp_bar_sprite_, mp_bar_sprite_, state_sprite_, selected_overlay_, damage_container_;
    std::vector<std::shared_ptr<SpriteBattleFaceStatusEmitter>> emitters_;
    std::vector<std::shared_ptr<SpriteACSBubble>> acs_bubbles_;
    std::vector<std::shared_ptr<SpriteDamage>> damage_sprites_;
};

class WindowBattleStatus : public WindowSelectable, public DisplayJsExtra {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }
    int max_cols() override { return 2; }
    int max_items() override;
    void refresh() override;
    void update() override;
    void update_cursor() override;
    void update_selected_face_windows();
    int find_next_index_in_direction(const std::string& direction, int index);
    void update_index_from_direction(const std::string& direction);
    void cursor_up(bool wrap) override { update_index_from_direction("up"); }
    void cursor_down(bool wrap) override { update_index_from_direction("down"); }
    void cursor_left(bool wrap) override { update_index_from_direction("left"); }
    void cursor_right(bool wrap) override { update_index_from_direction("right"); }
    void update_status_requests();
    void show_actor_chain_skill_bubbles(rpg::GameActor* actor, const std::vector<std::pair<std::string, int>>& list);
    void select_actor_chain_skill_bubble(rpg::GameActor* actor, const std::string& direction);
    bool display_js_get(uint32_t key, js::Val& out) override;

    std::vector<std::shared_ptr<WindowOmoriBattleActorStatus>>* face_windows = nullptr;
};

class WindowBattleActor : public WindowBattleStatus {
public:
    void initialize(double x, double y);
    using WindowBattleStatus::initialize;
    void show() override;
    void hide() override;
    void select(int index) override;
    rpg::GameActor* actor();
    void auto_select();
    void update_cursor() override;
    void auto_select_first_dead_actor();
    void update() override;
    void cursor_pageup() override {}
    void cursor_pagedown() override {}

    std::function<void()> group_target_swap;

private:
    bool select_dead_ = false;
};

class WindowBattleEnemy : public WindowSelectable {
public:
    void initialize(double x, double y);
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }
    int max_cols() override { return 2; }
    int max_items() override { return (int)enemies_.size(); }
    rpg::GameEnemy* enemy();
    int enemy_index();
    void draw_item(int index) override;
    void show() override;
    void hide() override;
    void refresh() override;
    void select(int index) override;
    void auto_select();
    void update() override;

    std::function<void()> group_target_swap;

private:
    std::vector<rpg::GameEnemy*> enemies_;
};

class WindowActorCommand : public WindowCommand {
public:
    void initialize();
    using WindowCommand::initialize;
    double window_width() override { return 360; }
    int num_visible_rows() override { return 4; }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int max_cols() override { return command_max_cols_; }
    int standard_padding() override { return 0; }
    double item_height() override { return 59 - 17; }
    double item_width() override { return 178; }
    double custom_cursor_rect_x_offset() override { return custom_cursor_x_offset_; }
    double custom_cursor_rect_y_offset() override { return 0; }
    int spacing() override { return 4; }
    void make_command_list() override;
    void make_custom_actor_command_list();
    void add_actor_custom_command(const js::Val& obj);
    void add_attack_command();
    void add_skill_commands();
    void add_guard_command();
    void add_item_command();
    void draw_item(int index) override { (void)index; }
    bool is_current_item_enabled() override;
    void create_command_sprites();
    void setup(rpg::GameActor* actor);
    void process_ok() override;
    void select_last();

    std::vector<std::shared_ptr<g2d::Sprite>> command_sprites;
    std::shared_ptr<g2d::Sprite> custom_cursor_container() const { return custom_cursor_container_; }

private:
    double custom_cursor_x_offset_ = 0;
    int command_max_cols_ = 2;
    rpg::GameActor* actor_ = nullptr;
};

class SpriteBattleBSRedHand : public g2d::Sprite {
public:
    explicit SpriteBattleBSRedHand(int side);
    void update() override;

private:
    int side_ = 0;
    double move_speed_ = 0;
    int phase_ = 0;
    int frame_ = 0, count_ = 0, delay_ = 7;
};

class WindowPartyCommand : public WindowCommand {
public:
    void initialize();
    using WindowCommand::initialize;
    double window_width() override { return 360; }
    int num_visible_rows() override { return 4; }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int max_cols() override { return 1; }
    int standard_padding() override { return 0; }
    double item_height() override { return 39; }
    void play_ok_sound() override;
    ItemRect item_rect(int index) override;
    void make_command_list() override;
    void draw_item(int index) override { (void)index; }
    void create_command_sprites();
    void process_ok() override;
    void start_escape_block_effect();
    void setup();

    std::vector<std::shared_ptr<g2d::Sprite>> command_sprites;
    std::shared_ptr<g2d::Sprite> escape_block_container;
    std::shared_ptr<g2d::Sprite> custom_cursor_container() const { return custom_cursor_container_; }
};

class WindowItemListBack : public WindowBase {
public:
    void initialize(double w, double h);
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 4; }
    int standard_font_size() override { return 20; }
    void set_item(const rpg::ItemRef& item, rpg::GameActor* actor);
};

class WindowBattleSkill : public WindowSelectable {
public:
    void initialize(double x, double y, double w, double h) override;
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    double line_height() override { return 22; }
    int spacing() override { return 0; }
    int standard_padding() override { return 10; }
    double custom_cursor_rect_x_offset() override { return -28; }
    int max_cols() override;
    int max_items() override { return (int)data_.size(); }
    rpg::ItemRef item();
    bool is_current_item_enabled() override;
    bool includes(const rpg::ItemRef& item) { return item.valid(); }
    bool is_enabled(const rpg::ItemRef& item);
    void make_item_list();
    void select_last();
    void set_actor(rpg::GameActor* actor);
    void set_stype_id(int id);
    ItemRect item_rect(int index) override;
    void draw_item(int index) override;
    void update_help() override;
    void refresh() override;
    void show() override;
    void hide() override;

private:
    rpg::GameActor* actor_ = nullptr;
    int stype_id_ = 0;
    std::vector<rpg::ItemRef> data_;
    std::shared_ptr<WindowItemListBack> back_window_;
};

class WindowBattleItem : public menu::WindowItemList {
public:
    void initialize(double x, double y, double w, double h) override;
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    bool includes(const rpg::ItemRef& item) override;
    double line_height() override { return 24; }
    int spacing() override { return 0; }
    int standard_padding() override { return 10; }
    double custom_cursor_rect_x_offset() override { return -24; }
    ItemRect item_rect(int index) override;
    void draw_item(int index) override;
    void update_help() override;
    void cursor_down(bool wrap) override;
    void show() override;
    void hide() override;

private:
    std::shared_ptr<WindowItemListBack> back_window_;
};

class WindowChainSkillList : public WindowBase {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    void setup(rpg::GameBattler* battler, const rpg::ItemRef& skill);
    void show() override;
    void set_chain_input(const std::string& value);
    void refresh();
    void draw_background_color();
    void draw_chain_skill_title();
    void draw_chain_skills();
    std::string input_text_settings(const std::string& input, const Value& skill);

    rpg::GameBattler* battler = nullptr;
    std::vector<std::pair<std::string, int>> chain_skills;
    std::string chain_input;
    bool has_chain_input = false;

private:
    rpg::ItemRef skill_;
    bool enabled_ = true;
};

std::vector<std::pair<std::string, int>> chain_skill_list(const rpg::ItemRef& skill);

class SpriteStressBar : public g2d::Sprite {
public:
    SpriteStressBar();
    void refresh_ekg_bitmap();
    void draw_stress_count_value(double value);
    void update() override;
    void update_line(bool stressed);
    void update_background_image();

private:
    int lx_ = 0;
    int ekg_row_ = 0;
    int pending_ekg_row_ = -1;
    int frame_count_ = 0;
    std::shared_ptr<g2d::Sprite> background_, ekg_line_, ekg_text_;
    g2d::BitmapPtr ekg_line_bitmap_, ekg_line_new_bitmap_;
};

class SpriteBattleLowHpOverlay : public g2d::Sprite {
public:
    SpriteBattleLowHpOverlay();
    void update() override;

private:
    rpg::GameActor* actor_ = nullptr;
    double opacity_change_ = 0, opacity_speed_ = 5, active_opacity_ = -60;
    int opacity_side_ = 0;
    double old_hp_ = 0;
    bool has_old_hp_ = false;
    bool hidden_ = false;
    double beep_counter_ = 0;
    double danger_rate_ = 0, hp_rate_ = 1;
};

class SpriteBattleIntroContainer : public g2d::Sprite {
public:
    explicit SpriteBattleIntroContainer(const std::string& type);
    void setup_final_battle_animation();
    void update() override;
    void update_final_battle_animation();

private:
    std::string type_;
    std::shared_ptr<g2d::Sprite> animation_sprite_;
    std::shared_ptr<SpriteEnemy> battler_sprite_;
    int small_delay_ = 0;
    int index_ = 0, frames_ = 21, count_ = 10, delay_ = 10;
    bool has_animation_data_ = false;
};

class SceneBattle : public rpg::Scene, public rpg::battle::BattleScene {
public:
    SceneBattle();
    ~SceneBattle() override;
    const char* name() const override { return "Scene_Battle"; }
    void create() override;
    void start() override;
    void update() override;
    void stop() override;
    void terminate() override;
    bool js_get(uint32_t key, js::Val& out) override;
    bool js_set(uint32_t key, const js::Val& v) override;
    void remove_filter_controller(rpg::FilterController* f) override;

    void refresh_status() override;
    void set_window_layer_hidden(bool hidden) override;
    void start_fade_out(int duration, bool white) override;
    void start_fade_in(int duration, bool white) override;
    void raise_fade_sprite() override;

    void fade_in(int duration, bool white);
    void fade_out(int duration, bool white);
    bool needs_slow_fade_out();
    void update_battle_process();
    bool is_any_input_window_active();
    void change_input_window();
    void create_display_objects();
    void create_all_windows();
    void create_log_window();
    void create_status_window();
    void create_face_windows();
    void create_party_command_window();
    void create_actor_command_window();
    void create_help_window();
    void create_skill_window();
    void create_item_window();
    void create_actor_window();
    void create_enemy_window();
    void create_chain_skill_window();
    void create_stress_bar();
    void create_battle_intro_container();
    void add_tone_filter_compatibility();
    void start_party_command_selection();
    void base_start_party_command_selection();
    void command_fight();
    void command_escape();
    void start_actor_command_selection();
    void command_attack();
    void command_skill();
    void command_guard();
    void command_item(const std::string& category);
    void command_actor_command_action();
    void select_next_command();
    void select_previous_command();
    void select_actor_selection();
    void on_actor_ok();
    void on_actor_cancel();
    void select_enemy_selection();
    void on_enemy_ok();
    void on_enemy_cancel();
    void on_skill_ok();
    void on_skill_cancel();
    void on_item_ok();
    void on_item_cancel();
    void on_select_action();
    void end_command_selection();
    void group_target_swap(int type);
    void push_party_message();
    void add_actor_action_prompt_log_text();
    void add_log_command_message(const std::string& text, bool clear = true, bool instant = true);
    void hide_all_menu_elements(int duration = 15);
    void show_all_menu_elements();
    void show_party_command();
    void hide_party_command();
    void show_item_window();
    void hide_item_window();
    void show_skill_window();
    void hide_skill_window();
    void update_main_tone_filter();
    bool needs_main_tone_update();
    void set_start_tint();
    bool needs_gp_white();
    void create_gp_white();
    void update_gp_white();
    void default_start();
    void update_tkm_filters();
    void update_retry_fade();
    void move_y(g2d::DisplayObject* obj, double to, int duration, menu::Easing easing);
    void move_x(g2d::DisplayObject* obj, double to, int duration, menu::Easing easing);

    std::shared_ptr<SpritesetBattle> spriteset;
    std::shared_ptr<WindowBattleLog> log_window;
    std::shared_ptr<WindowBattleStatus> status_window;
    std::shared_ptr<WindowPartyCommand> party_command_window;
    std::shared_ptr<WindowActorCommand> actor_command_window;
    std::shared_ptr<menu::WindowOmoMenuHelp> help_window;
    std::shared_ptr<WindowBattleSkill> skill_window;
    std::shared_ptr<WindowBattleItem> item_window;
    std::shared_ptr<WindowBattleActor> actor_window;
    std::shared_ptr<WindowBattleEnemy> enemy_window;
    std::shared_ptr<WindowChainSkillList> chain_window;
    std::shared_ptr<WindowMessage> message_window;
    std::shared_ptr<WindowScrollText> scroll_text_window;
    std::shared_ptr<g2d::Sprite> face_windows_container;
    FaceLayers layers;
    std::vector<std::shared_ptr<WindowOmoriBattleActorStatus>> face_windows;
    std::shared_ptr<SpriteStressBar> stress_bar;
    std::shared_ptr<SpriteBattleLowHpOverlay> low_hp_overlay;
    std::shared_ptr<SpriteBattleIntroContainer> battle_intro_container;
    std::shared_ptr<g2d::ScreenSprite> gp_white;
    menu::ObjectMovement move;

private:
    ToneOverlayPtr tone_filter_;
    ToneOverlayPtr main_status_tone_filter_;
    bool using_tone_filter_ = false;
    double main_status_tone_[4] = {0, 0, 0, 0};
    int retry_fade_in_anim_ = 0;
    int current_retry_fade_in_anim_ = -1;
    fx::TkmFilters tkm_;
    fx::TkmTargets tkm_targets();
    std::vector<g2d::DisplayObject*> message_layer_children_;
    g2d::DisplayPtr scroll_text_layer_;
};

}
}
