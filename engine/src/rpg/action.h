#pragma once
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "../js/bind.h"
#include "battler.h"

namespace rpg {

class GameUnit;
class GameEnemy;

struct BattlerRef {
    int actor_id = 0;
    int enemy_index = -1;
    static BattlerRef of(const GameBattlerBase* b);
    GameBattler* get() const;
    bool valid() const { return actor_id > 0 || enemy_index >= 0; }
};

struct StealableItem;

class GameAction : public js::JsObject {
public:
    enum { EFFECT_RECOVER_HP = 11, EFFECT_RECOVER_MP = 12, EFFECT_GAIN_TP = 13, EFFECT_ADD_STATE = 21, EFFECT_REMOVE_STATE = 22,
           EFFECT_ADD_BUFF = 31, EFFECT_ADD_DEBUFF = 32, EFFECT_REMOVE_BUFF = 33, EFFECT_REMOVE_DEBUFF = 34, EFFECT_SPECIAL = 41,
           EFFECT_GROW = 42, EFFECT_LEARN_SKILL = 43, EFFECT_COMMON_EVENT = 44 };
    enum { HITTYPE_CERTAIN = 0, HITTYPE_PHYSICAL = 1, HITTYPE_MAGICAL = 2 };

    GameAction(GameBattler* subject, bool forcing = false);
    GameAction(const GameAction& o);
    std::shared_ptr<GameAction> clone() const { return std::make_shared<GameAction>(*this); }

    int subject_actor_id = 0;
    int subject_enemy_index = -1;
    bool forcing = false;
    GameItem item_;
    int target_index = -1;
    bool is_counter_ = false;
    bool is_force_miss = false;
    int target_group = -1;
    bool has_reflection_target = false;
    BattlerRef reflection_target;
    int snatch_target_index = 0;
    bool active_chain_skill = false;

    void clear();
    void set_subject(GameBattler* subject);
    GameBattler* subject() const;
    GameUnit& friends_unit() const;
    GameUnit& opponents_unit() const;
    void set_enemy_action(const Value* action);
    void set_attack();
    void set_guard();
    void set_skill(int id);
    void set_item(int id);
    void set_item_object(const ItemRef& r) { item_.set_object(r); }
    void set_target(int index) { target_index = index; }
    ItemRef item() const { return item_.object(); }
    const Value& item_data() const { return item().data(); }
    bool is_skill() const { return item_.data_class == "skill"; }
    bool is_item() const { return item_.data_class == "item"; }
    int num_repeats() const;
    bool check_item_scope(std::initializer_list<int> list) const;
    bool is_for_opponent() const;
    bool is_for_friend() const;
    bool is_for_dead_friend() const { return check_item_scope({9, 10}); }
    bool is_for_user() const { return check_item_scope({11}); }
    bool is_for_one() const { return check_item_scope({1, 3, 7, 9, 11}); }
    bool is_for_random() const { return check_item_scope({3, 4, 5, 6}); }
    bool is_for_all() const { return check_item_scope({2, 8, 10}); }
    bool needs_selection() const;
    int num_targets() const { return is_for_random() ? item_data()["scope"].as_int() - 2 : 0; }
    bool check_damage_type(std::initializer_list<int> list) const;
    bool is_hp_effect() const { return check_damage_type({1, 3, 5}); }
    bool is_mp_effect() const { return check_damage_type({2, 4, 6}); }
    bool is_damage() const { return check_damage_type({1, 2}); }
    bool is_recover() const { return check_damage_type({3, 4}); }
    bool is_drain() const { return check_damage_type({5, 6}); }
    bool is_hp_recover() const { return check_damage_type({3}); }
    bool is_mp_recover() const { return check_damage_type({4}); }
    bool is_certain_hit() const { return item().valid() && item_data()["hitType"].as_int() == HITTYPE_CERTAIN; }
    bool is_physical() const { return item().valid() && item_data()["hitType"].as_int() == HITTYPE_PHYSICAL; }
    bool is_magical() const { return item().valid() && item_data()["hitType"].as_int() == HITTYPE_MAGICAL; }
    bool is_attack() const;
    bool is_guard() const;
    bool is_magic_skill() const;
    void decide_random_target();
    void set_confusion() { set_attack(); }
    void prepare();
    bool is_valid();
    double speed();
    std::vector<GameBattler*> make_targets();
    std::vector<GameBattler*> base_make_targets();
    std::vector<GameBattler*> repeat_targets(const std::vector<GameBattler*>& targets) const;
    GameBattler* confusion_target();
    std::vector<GameBattler*> targets_for_opponents();
    std::vector<GameBattler*> targets_for_friends();
    double evaluate();
    std::vector<GameBattler*> item_target_candidates();
    double evaluate_with_target(GameBattler* target);
    bool test_apply(GameBattler* target);
    bool has_item_any_valid_effects(GameBattler* target);
    bool test_item_effect(GameBattler* target, const Value& effect);
    double item_cnt(GameBattler* target);
    double item_mrf(GameBattler* target) const;
    double item_hit(GameBattler* target) const;
    double item_eva(GameBattler* target) const;
    double item_cri(GameBattler* target);
    void apply(GameBattler* target);
    void apply_core(GameBattler* target);
    double make_damage_value(GameBattler* target, bool critical);
    double eval_damage_formula(GameBattler* target);
    double calc_element_rate(GameBattler* target);
    double elements_max_rate(GameBattler* target, const std::vector<int>& elements);
    double apply_critical(double value) const { return value; }
    double apply_variance(double damage, double variance) const;
    double apply_guard(double damage, GameBattler* target) const;
    void execute_damage(GameBattler* target, double value);
    void execute_hp_damage(GameBattler* target, double value);
    void execute_mp_damage(GameBattler* target, double value);
    void gain_drained_hp(double value);
    void gain_drained_mp(double value);
    void apply_item_effect(GameBattler* target, const Value& effect);
    void item_effect_recover_hp(GameBattler* target, const Value& effect);
    void item_effect_recover_mp(GameBattler* target, const Value& effect);
    void item_effect_gain_tp(GameBattler* target, const Value& effect);
    void item_effect_add_state(GameBattler* target, const Value& effect);
    void item_effect_add_attack_state(GameBattler* target, const Value& effect);
    void item_effect_add_normal_state(GameBattler* target, const Value& effect);
    void item_effect_remove_state(GameBattler* target, const Value& effect);
    void item_effect_add_buff(GameBattler* target, const Value& effect);
    void item_effect_add_debuff(GameBattler* target, const Value& effect);
    void item_effect_remove_buff(GameBattler* target, const Value& effect);
    void item_effect_remove_debuff(GameBattler* target, const Value& effect);
    void item_effect_special(GameBattler* target, const Value& effect);
    void item_effect_grow(GameBattler* target, const Value& effect);
    void item_effect_learn_skill(GameBattler* target, const Value& effect);
    void make_success(GameBattler* target);
    void apply_item_user_effect(GameBattler* target);
    double luk_effect_rate(GameBattler* target) const;
    void apply_global();
    void apply_before_eval(GameBattler* target);
    void apply_after_eval(GameBattler* target);
    double apply_pre_damage_eval(GameBattler* target, double value);
    void apply_post_damage_eval(GameBattler* target, double value);
    double custom_counter_rate_eval(double rate, GameBattler* target);
    void set_counter() { is_counter_ = true; }
    bool is_counter() const { return is_counter_; }
    void ai_register_element_rate(GameBattler* target);
    void apply_modify_buff_turns(GameBattler* target);
    double apply_buff_turns_eval(double turn, int param_id, GameBattler* target, bool debuff);
    void apply_modify_debuff_turns(GameBattler* target);
    void apply_modify_state_turns(GameBattler* target);
    double apply_state_turns_eval(double turn, int state_id, GameBattler* target);
    js::Val custom_effect_eval(GameBattler* target, int state_id, const char* type, GameBattler* side, const js::Val& value);
    void on_apply_state_effects(GameBattler* target);
    void on_select_state_effects(GameBattler* target);
    void on_deselect_state_effects(GameBattler* target);
    void off_apply_state_effects(GameBattler* target);
    double on_pre_damage_state_effects(GameBattler* target, double value);
    double on_react_state_effects(GameBattler* target, double value);
    double on_respond_state_effects(GameBattler* target, double value);
    double on_post_damage_state_effects(GameBattler* target, double value);
    void apply_state_category_removal_effect(GameBattler* target);
    bool modify_critical(bool critical, double base, GameBattler* target);
    double apply_damage_rate(double value) const;
    double apply_heal_rate(double value, GameBattler* target) const;
    double apply_critical_rate(double value, double base, GameBattler* target);
    double apply_item_critical_mult(double value, double base, GameBattler* target);
    double apply_flat_critical(double value, double base, GameBattler* target);
    double apply_minimum_damage(double value, double base, GameBattler* target);
    bool is_damage_capped() const;
    double apply_item_critical_rate(GameBattler* target);
    double apply_item_flat_crit(double value, double base, GameBattler* target);
    bool can_life_steal_hp(double damage, GameBattler* target) const;
    bool can_life_steal_mp(double damage, GameBattler* target) const;
    void perform_life_steal(double damage, GameBattler* target, double value);
    double get_life_steal_rate(GameBattler* target, double value, const std::string& type);
    double get_life_steal_flat(GameBattler* target, double value, const std::string& type);
    double life_steal_eval(const Value* ast, GameBattler* target, double value, const char* var, const char* where);
    bool is_tauntable() const;
    std::vector<GameBattler*> get_taunt_target(std::vector<GameBattler*> targets);
    void apply_item_steal_effect(GameBattler* target);
    void apply_item_snatch_effect(GameBattler* target);
    bool can_process_steal(GameBattler* target) const;
    bool can_process_snatch(GameBattler* target) const;
    bool match_steal_type(GameBattler* target, const StealableItem* s) const;
    double get_stealable_rate(GameBattler* target, const StealableItem* s);
    double steal_bonus_formula(GameBattler* target);
    double custom_steal_rate_eval(GameBattler* target, double rate);
    void get_stealable_item(GameBattler* target, StealableItem* s);
    void after_steal_effect(GameBattler* target, const ItemRef& item);
    void make_battle_engine_core_steal_wait();
    void display_steal_text(const std::string& text);
    void play_steal_sound(const Value& sound);
    void display_steal_failure(GameBattler* target);
    void display_steal_empty(GameBattler* target);
    void set_active_chain_skill(int skill_id);
    void set_target_group(int g) { target_group = g; }
    bool can_change_target_groups() const;
    bool not_target_user() const;
    bool is_for_everybody() const;
    void init_eval(class JsEval& e, GameBattler* target) const;
    js::HostClass* js_class() const override;
};

using ActionPtr = std::shared_ptr<GameAction>;

}
