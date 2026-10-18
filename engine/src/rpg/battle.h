#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "../js/value.h"
#include "action.h"
#include "state.h"

namespace rpg {

class GameBattler;
class GameActor;

namespace battle {

struct ActSeq {
    std::string name;
    std::vector<std::string> args;
};

struct BattleLog {
    virtual ~BattleLog() = default;
    virtual void push(const std::string& method, std::vector<js::Val> args = {}) = 0;
    virtual std::string last_method() const = 0;
    virtual bool is_busy() = 0;
    virtual void start_turn() = 0;
    virtual void start_action(GameBattler* subject, GameAction* action, const std::vector<GameBattler*>& targets) = 0;
    virtual void end_action(GameBattler* subject) = 0;
    virtual void display_action_results(GameBattler* subject, GameBattler* target) = 0;
    virtual void display_counter(GameBattler* target) = 0;
    virtual void display_reflection(GameBattler* target) = 0;
    virtual void display_substitute(GameBattler* substitute, GameBattler* target) = 0;
    virtual void display_auto_affected_status(GameBattler* target) = 0;
    virtual void display_current_state(GameBattler* subject) = 0;
    virtual void display_regeneration(GameBattler* subject) = 0;
    virtual void display_action(GameBattler* subject, const ItemRef& item) = 0;
    virtual void display_added_states(GameBattler* target) = 0;
    virtual void display_custom_action_text(GameBattler* subject, GameBattler* target, const ItemRef& item) = 0;
    virtual void clear() = 0;
    virtual void add_text(const std::string& text) = 0;
    virtual void push_line(const std::string& text) = 0;
    virtual int& wait_count() = 0;
    virtual void show_normal_animation(const std::vector<GameBattler*>& targets, int id, bool mirror) = 0;
    virtual void show_attack_animation(GameBattler* subject, const std::vector<GameBattler*>& targets) = 0;
    virtual void show_actor_atk_ani_mirror(GameBattler* subject, const std::vector<GameBattler*>& targets) = 0;
    virtual void show_animation(GameBattler* subject, const std::vector<GameBattler*>& targets, int id) = 0;
    virtual void perform_action(GameBattler* subject, GameAction* action) = 0;
    virtual void perform_action_end(GameBattler* subject) = 0;
    virtual void perform_action_start(GameBattler* subject, GameAction* action) = 0;
    virtual void wait_for_animation() = 0;
    virtual void wait_for_effect() = 0;
    virtual void wait_for_movement() = 0;
    virtual void wait_for_new_line() = 0;
    virtual void wait_for_popups() = 0;
    virtual void wait_for_float() = 0;
    virtual void wait_for_jump() = 0;
    virtual void wait_for_opacity() = 0;
    virtual void wait_for_camera() = 0;
    virtual void wait_for_zoom() = 0;
    virtual void set_visible(bool v) = 0;
    virtual bool visible() const = 0;
    virtual js::Val js() = 0;
};

struct Spriteset {
    virtual ~Spriteset() = default;
    virtual bool is_busy() = 0;
    virtual bool is_popup_playing() = 0;
    virtual void refresh_enemy_reinforcements() = 0;
    virtual void change_battleback_to(int id, const std::string& folder, const std::string& file, int hue) = 0;
    virtual void battleback_fade_in(int id, int duration) = 0;
    virtual void battleback_fade_out(int id, int duration) = 0;
    virtual void battleback_opacity(int id, int value) = 0;
    virtual void reset_scroll_speeds(int id) = 0;
    virtual void set_battleback_scroll_speed_x(int id, int speed) = 0;
    virtual void set_battleback_scroll_speed_y(int id, int speed) = 0;
    virtual void add_new_battleback(int id, const std::string& folder, const std::string& file, int hue, double opacity, int duration) = 0;
    virtual void remove_battleback(int id) = 0;
    virtual void set_picture_layer(const std::string& layer) = 0;
    virtual js::Val js() = 0;
};

struct BattleScene {
    virtual ~BattleScene() = default;
    virtual void refresh_status() = 0;
    virtual void set_window_layer_hidden(bool hidden) = 0;
    virtual void start_fade_out(int duration, bool white) = 0;
    virtual void start_fade_in(int duration, bool white) = 0;
    virtual void raise_fade_sprite() = 0;
};

struct PreForce {
    BattlerRef subject;
    bool has_subject = false;
    std::shared_ptr<GameAction> action;
    std::vector<ActSeq> action_list;
    std::vector<BattlerRef> targets, all_targets, ind_targets;
    std::vector<std::string> phase_steps;
    std::string return_phase;
    std::string phase;
    std::vector<bool> condition_flags, true_flags;
};

struct State {
    std::string phase = "init";
    bool phase_null = false;
    bool can_escape = false;
    bool can_lose = false;
    bool battle_test = false;
    std::function<void(int)> event_callback;
    bool preemptive = false;
    bool surprise = false;
    int actor_index = -1;
    BattlerRef action_forced_battler;
    bool has_action_forced_battler = false;
    AudioObj map_bgm, map_bgs;
    bool has_map_bgm = false, has_map_bgs = false;
    std::vector<BattlerRef> action_battlers;
    BattlerRef subject;
    bool has_subject = false;
    std::shared_ptr<GameAction> action;
    std::vector<BattlerRef> targets;
    BattlerRef target;
    bool has_target = false;
    BattleLog* log = nullptr;
    Spriteset* spriteset = nullptr;
    BattleScene* scene = nullptr;
    double escape_ratio = 0;
    double escape_fail_boost = 0;
    bool escaped = false;
    double rewards_gold = 0, rewards_exp = 0;
    std::vector<ItemRef> rewards_items;
    bool turn_forced = false;
    bool force_selection = false;
    bool all_selection = false;
    bool victory_phase = false;
    std::vector<PreForce> force_action_queue;
    std::vector<std::shared_ptr<GameAction>> counter_queue;
    int counter_sequence = 0;
    BattlerRef counter_original_subject;
    bool has_counter_original_subject = false;
    std::shared_ptr<GameAction> counter_original_action;
    bool countering = false;
    bool has_counter_skill = false;
    bool counter_skill_null = false;
    ItemRef counter_skill;
    int actor_command_index = 0;
    std::string picture_display_layer = "front";
    bool hide_low_hp_overlay = false;
    bool show_low_hp_overlay = true;
    bool enter_end_phase = false;
    std::vector<BattlerRef> performed_battlers;
    bool process_turn = false;
    std::vector<BattlerRef> all_targets;
    std::vector<BattlerRef> individual_targets;
    std::vector<std::string> phase_steps;
    std::string return_phase;
    std::vector<ActSeq> action_list;
    ActSeq act_seq;
    std::vector<bool> condition_flags, true_flags;
    bool processing_forced_action = false;
    std::string pre_force_phase;
    double camera_x = 320, camera_y = 240, camera_offset_x = 0, camera_offset_y = 0;
    std::vector<BattlerRef> camera_focus_group;
    std::string camera_focus_pos_x = "BASE", camera_focus_pos_y = "BASE";
    bool camera_clamp = true;
    AudioObj memorized_bgm, memorized_bgs;
    bool has_memorized_bgm = false, has_memorized_bgs = false;
    js::Val battle_intro;
    bool escape_animation_played = false;
    bool escape_by_command = false;
    double stress_speed = 1;
    bool has_stress_speed = false;
    bool suspend_events = false;
    bool show_encounter_effect = true;
    bool show_damage = true;
    bool battle_retried = false;
    int final_battle_phase = 0;
    bool has_retry_setup = false;
    int retry_troop = 0;
    bool retry_can_escape = false, retry_can_lose = false;
    std::vector<uint8_t> retry_data;
    bool waiting_for_retry_video = false;
    bool bypass_move_to_start_location = false;
    bool is_final_battle = false;
    js::Expando expando;
};

State& st();
void setup(int troop_id, bool can_escape, bool can_lose);
void init_members();
void set_event_callback(std::function<void(int)> cb);
void on_encounter();
double rate_preemptive();
double rate_surprise();
void save_bgm_and_bgs();
void play_battle_bgm();
void play_victory_me();
void play_defeat_me();
void replay_bgm_and_bgs();
void make_escape_ratio();
void update();
bool update_event();
bool update_event_main();
bool is_busy();
bool is_inputting();
bool is_in_turn();
bool is_turn_end();
bool is_aborting();
bool is_battle_end();
bool can_escape();
bool can_lose();
bool is_escaped();
GameActor* actor();
void clear_actor();
void change_actor(int new_index, const std::string& last_state);
void start_battle();
void start_input();
std::shared_ptr<GameAction> inputting_action();
void select_next_command();
void select_previous_command();
void refresh_status();
void start_turn();
void update_turn();
void process_turn();
void end_turn();
bool is_forced_turn();
void update_turn_end();
GameBattler* get_next_subject();
std::vector<GameBattler*> all_battle_members();
void make_action_orders();
void start_action();
void update_action();
void end_action();
void invoke_action(GameBattler* subject, GameBattler* target);
void invoke_normal_action(GameBattler* subject, GameBattler* target);
void invoke_counter_attack(GameBattler* subject, GameBattler* target);
void invoke_magic_reflection(GameBattler* subject, GameBattler* target);
GameBattler* apply_substitute(GameBattler* target);
bool check_substitute(GameBattler* target);
bool is_action_forced();
void force_action(GameBattler* battler);
void process_forced_action();
void abort();
bool check_battle_end();
bool check_abort();
void process_victory();
bool process_escape();
void process_abort();
void process_defeat();
void end_battle(int result);
void update_battle_end();
void make_rewards();
void display_victory_message();
void display_defeat_message();
void display_escape_success_message();
void display_escape_failure_message();
void display_rewards();
void gain_rewards();
void gain_exp();
void gain_gold();
void gain_drop_items();
bool is_dtb();
bool is_turn_based();
bool is_tick_based();
double tick_rate();
void force_selection();
bool is_force_selection();
void reset_selection();
void start_all_selection();
bool is_all_selection();
void stop_all_selection();
bool time_based_states();
bool time_based_buffs();
void create_actions();
void clear_inputting_action();
void refresh_all_members();
void queue_force_action(GameBattler* user, int skill_id, const js::Val& target, bool clear_result);
void add_text(const std::string& text, int wait);
void clear_results();
void set_targets(const std::vector<GameBattler*>& targets);
void update_phase();
void update_action_list();
void update_action_target_list();
bool process_action_sequence_check(const std::string& name, const std::vector<std::string>& args);
bool process_action_sequence(const std::string& name, const std::vector<std::string>& args);
std::vector<GameBattler*> make_action_targets(const std::string& name);
bool is_countering();
GameBattler* subject();
std::vector<GameBattler*> targets();
std::vector<GameBattler*> all_targets();
void push_action_list_wait(int frames);
void log_push_line(const std::string& text);
void refresh_enemy_reinforcements();
void alter_battleback(const std::string& line);
bool is_battle_intro_active();
void set_battle_intro(const js::Val& name);
void make_battle_retry_data();
void process_retry();
std::vector<int> get_actor_input_order();
void set_picture_display_layer(const std::string& layer);
void start_fade_out(int duration, bool white);
void start_fade_in(int duration, bool white);
void queue_force_script_call(const std::string& code);
void force_custom_battle_log(GameBattler* target);
bool is_battle_same_of_map_bgm();
double camera_x();
double camera_y();
double target_pos_x(const std::vector<GameBattler*>& group, const std::string& position);
double target_pos_y(const std::vector<GameBattler*>& group, const std::string& position);
void reset_camera(int duration);
void action_reset_zoom(int duration);
void register_scene(BattleLog* log, Spriteset* spriteset, BattleScene* scene);
void unregister_scene();
js::Val js_object();

}

}
