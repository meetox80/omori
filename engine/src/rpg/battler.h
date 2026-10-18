#pragma once
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "../js/bind.h"
#include "character.h"
#include "data.h"

namespace rpg {

class GameUnit;
class GameAction;
class GameBattler;

struct BattlerSpriteLink {
    virtual ~BattlerSpriteLink() = default;
    virtual double link_x() const = 0;
    virtual double link_y() const = 0;
    virtual double link_bitmap_width() const = 0;
    virtual double link_bitmap_height() const = 0;
    virtual void link_force_motion(const std::string& motion) = 0;
    virtual void link_refresh_motion() = 0;
    virtual void link_setup_weapon_animation() = 0;
    virtual void link_setup_opacity_change(double opacity, double duration) = 0;
    virtual double link_float_height() const = 0;
    virtual double link_jump_height() const = 0;
    virtual void link_transform(GameBattler* battler) = 0;
    virtual void link_start_move(double x, double y, double duration) = 0;
    virtual js::Val link_js() = 0;
};

struct ItemRef {
    enum Kind : uint8_t { None, Skill, Item, Weapon, Armor };
    Kind kind = None;
    int id = 0;
    static ItemRef skill(int id) { return ItemRef{Skill, id}; }
    static ItemRef item(int id) { return ItemRef{Item, id}; }
    static ItemRef weapon(int id) { return ItemRef{Weapon, id}; }
    static ItemRef armor(int id) { return ItemRef{Armor, id}; }
    static ItemRef from_data(const Value* v);
    static ItemRef from_js(const js::Val& v);
    const Value& data() const;
    bool valid() const { return data().is_obj(); }
    bool is_skill() const { return kind == Skill; }
    bool is_item() const { return kind == Item; }
    bool is_weapon() const { return kind == Weapon; }
    bool is_armor() const { return kind == Armor; }
    bool is_equip() const { return kind == Weapon || kind == Armor; }
    js::Val to_js() const;
    bool operator==(const ItemRef& o) const { return kind == o.kind && id == o.id && (kind == None || valid() == o.valid()); }
    bool operator!=(const ItemRef& o) const { return !(*this == o); }
    const char* class_name() const;
};

struct GameItem {
    std::string data_class;
    int item_id = 0;
    ItemRef object() const;
    void set_object(const ItemRef& r);
    void set_equip(bool is_weapon, int id) {
        data_class = is_weapon ? "weapon" : "armor";
        item_id = id;
    }
    bool is_null() const { return data_class.empty(); }
};

class GameActionResult : public js::JsObject {
public:
    GameActionResult() { clear(); }
    bool used = false, missed = false, evaded = false, physical = false, drain = false, critical = false, success = false, hp_affected = false;
    double hp_damage = 0, mp_damage = 0, tp_damage = 0;
    std::vector<int> added_states, removed_states, added_buffs, added_debuffs, removed_buffs;
    bool element_strong = false, element_weak = false;
    void clear();
    bool is_status_affected() const;
    bool is_hit() const { return used && !missed && !evaded; }
    bool is_state_added(int id) const;
    void push_added_state(int id);
    bool is_state_removed(int id) const;
    void push_removed_state(int id);
    bool is_buff_added(int id) const;
    void push_added_buff(int id);
    bool is_debuff_added(int id) const;
    void push_added_debuff(int id);
    bool is_buff_removed(int id) const;
    void push_removed_buff(int id);
    js::HostClass* js_class() const override;
};

enum TraitCode {
    TRAIT_ELEMENT_RATE = 11,
    TRAIT_DEBUFF_RATE = 12,
    TRAIT_STATE_RATE = 13,
    TRAIT_STATE_RESIST = 14,
    TRAIT_PARAM = 21,
    TRAIT_XPARAM = 22,
    TRAIT_SPARAM = 23,
    TRAIT_ATTACK_ELEMENT = 31,
    TRAIT_ATTACK_STATE = 32,
    TRAIT_ATTACK_SPEED = 33,
    TRAIT_ATTACK_TIMES = 34,
    TRAIT_STYPE_ADD = 41,
    TRAIT_STYPE_SEAL = 42,
    TRAIT_SKILL_ADD = 43,
    TRAIT_SKILL_SEAL = 44,
    TRAIT_EQUIP_WTYPE = 51,
    TRAIT_EQUIP_ATYPE = 52,
    TRAIT_EQUIP_LOCK = 53,
    TRAIT_EQUIP_SEAL = 54,
    TRAIT_SLOT_TYPE = 55,
    TRAIT_ACTION_PLUS = 61,
    TRAIT_SPECIAL_FLAG = 62,
    TRAIT_COLLAPSE_TYPE = 63,
    TRAIT_PARTY_ABILITY = 64
};

class GameBattlerBase : public js::JsObject {
public:
    GameBattlerBase();
    ~GameBattlerBase() override = default;
    virtual void init_members();

    double hp_ = 1, mp_ = 0, tp_ = 0;
    bool hidden = false;
    bool has_dmg_capped = false, dmg_capped_cache = false;
    bool has_maximum_damage = false, has_maximum_healing = false;
    double maximum_damage_cache = 0, maximum_healing_cache = 0;
    std::vector<int> check_aura_state_condition;
    double param_plus_[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<int> states_;
    std::map<int, double> state_turns;
    double buffs_[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    double buff_turns[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::map<int, js::Val> state_counter;
    std::map<int, std::pair<int, int>> state_origin;
    bool has_cache_buff_rate = false;
    std::map<int, double> cache_buff_rate;
    bool has_passive_raw = false;
    std::vector<int> passive_raw;
    std::vector<int> check_passive;
    std::vector<int> check_aura;
    bool status_refresh_requested = false;
    bool immortal_state = false;
    js::Val horror_filters;

    virtual bool is_actor() const { return false; }
    virtual bool is_enemy() const { return false; }
    virtual GameUnit& friends_unit() const = 0;
    virtual GameUnit& opponents_unit() const = 0;
    virtual int index() const = 0;

    void clear_param_plus();
    virtual void clear_states();
    void base_clear_states();
    virtual void erase_state(int id);
    bool is_state_affected(int id);
    bool is_death_state_affected() { return is_state_affected(death_state_id()); }
    int death_state_id() const { return 1; }
    virtual void reset_state_counts(int id);
    bool is_state_expired(int id) const;
    void update_state_turns() { update_state_turn_timing(2); }
    void update_state_turn_timing(int timing);
    void clear_buffs();
    void erase_buff(int id);
    int buff_length() const { return 8; }
    double buff(int id) const { return id >= 0 && id < 8 ? buffs_[id] : 0; }
    bool is_buff_affected(int id) const { return buff(id) > 0; }
    bool is_debuff_affected(int id) const { return buff(id) < 0; }
    bool is_buff_or_debuff_affected(int id) const { return buff(id) != 0; }
    bool is_max_buff_affected(int id);
    bool is_max_debuff_affected(int id);
    void increase_buff(int id);
    void decrease_buff(int id);
    void overwrite_buff_turns(int id, double turns);
    bool is_buff_expired(int id) const { return id >= 0 && id < 8 && buff_turns[id] == 0; }
    void update_buff_turns();
    virtual void die();
    void revive();
    std::vector<const Value*> states();
    std::vector<int> state_icons();
    std::vector<int> buff_icons();
    int buff_icon_index(double level, int param_id) const;
    std::vector<int> all_icons();
    virtual std::vector<const Value*> trait_objects();
    void for_each_trait(int code, const std::function<void(const Value& t)>& fn);
    double traits_pi(int code, int id);
    double traits_sum(int code, int id);
    double traits_sum_all(int code);
    std::vector<int> traits_set(int code);
    virtual double param_base(int id) const { (void)id; return 0; }
    virtual double param_plus(int id);
    double param_min(int id) const { return id == 1 ? 0 : 1; }
    virtual double param_max(int id) const;
    double param_rate(int id) { return traits_pi(TRAIT_PARAM, id); }
    double param_buff_rate(int id);
    double param(int id);
    double xparam(int id) { return traits_sum(TRAIT_XPARAM, id); }
    double sparam(int id) { return traits_pi(TRAIT_SPARAM, id); }
    double element_rate(int id) { return traits_pi(TRAIT_ELEMENT_RATE, id); }
    double debuff_rate(int id) { return traits_pi(TRAIT_DEBUFF_RATE, id); }
    double state_rate(int id) { return traits_pi(TRAIT_STATE_RATE, id); }
    std::vector<int> state_resist_set() { return traits_set(TRAIT_STATE_RESIST); }
    bool is_state_resist(int id);
    virtual std::vector<int> attack_elements() { return traits_set(TRAIT_ATTACK_ELEMENT); }
    std::vector<int> attack_states() { return traits_set(TRAIT_ATTACK_STATE); }
    double attack_states_rate(int id) { return traits_sum(TRAIT_ATTACK_STATE, id); }
    double attack_speed() { return traits_sum_all(TRAIT_ATTACK_SPEED); }
    double attack_times_add();
    std::vector<int> added_skill_types() { return traits_set(TRAIT_STYPE_ADD); }
    bool is_skill_type_sealed(int id);
    std::vector<int> added_skills() { return traits_set(TRAIT_SKILL_ADD); }
    bool is_skill_sealed(int id);
    bool is_equip_wtype_ok(int id);
    bool is_equip_atype_ok(int id);
    bool is_equip_type_locked(int id);
    bool is_equip_type_sealed(int id);
    int slot_type();
    bool is_dual_wield() { return slot_type() == 1; }
    std::vector<double> action_plus_set();
    bool special_flag(int id);
    int collapse_type();
    bool party_ability(int id);
    bool is_auto_battle() { return special_flag(0); }
    bool is_guard() { return special_flag(1) && can_move(); }
    bool is_substitute() { return special_flag(2) && can_move(); }
    bool is_preserve_tp() { return special_flag(3); }
    void add_param(int id, double value);
    void set_hp(double hp);
    void set_mp(double mp);
    void set_tp(double tp);
    double max_tp() const { return 100; }
    virtual void refresh();
    void base_refresh();
    void reset_dmg_temp_values() {
        has_dmg_capped = false;
        has_maximum_damage = false;
        has_maximum_healing = false;
    }
    std::vector<const Value*> all_traits();
    bool is_status_refresh_requested() const { return status_refresh_requested; }
    void complete_status_refresh_request() { status_refresh_requested = false; }
    void update_state_ticks();
    bool is_bypass_update_turns();
    void update_state_action_start();
    void update_state_action_end() { update_state_turn_timing(1); }
    void update_state_turn_start() { update_state_turn_timing(4); }
    void update_state_turn_end() { update_state_turn_timing(2); }
    void update_buff_ticks();
    double timed_tick() const;
    virtual js::Val gauge(int n) const;
    virtual js::Val gauge_icon(int n) const;
    void clear_state_counters() { state_counter.clear(); }
    void clamp_state_counter(int id, double mn, double mx);
    bool aura_state_condition_eval(const Value& state);
    virtual void recover_all();
    double hp_rate() { return hp() / mhp(); }
    double mp_rate() { return mmp() > 0 ? mp() / mmp() : 0; }
    double tp_rate() { return tp() / max_tp(); }
    void hide() { hidden = true; }
    virtual void appear() { hidden = false; }
    bool is_hidden() const { return hidden; }
    bool is_appeared() const { return !hidden; }
    bool is_dead() { return is_appeared() && is_death_state_affected(); }
    bool is_alive() { return is_appeared() && !is_death_state_affected(); }
    bool is_dying() { return is_alive() && hp_ < mhp() / 4; }
    bool is_restricted() { return is_appeared() && restriction() > 0; }
    bool can_input() { return is_appeared() && !is_restricted() && !is_auto_battle(); }
    bool can_move() { return is_appeared() && restriction() < 4; }
    bool is_confused();
    int confusion_level() { return is_confused() ? restriction() : 0; }
    void sort_states();
    int restriction();
    virtual void add_new_state(int id);
    virtual void on_restrict() {}
    std::string most_important_state_text();
    int state_motion_index();
    int state_overlay_index();
    virtual bool is_skill_wtype_ok(const Value& skill) { (void)skill; return true; }
    double skill_mp_cost(const ItemRef& skill);
    double skill_tp_cost(const ItemRef& skill);
    double skill_hp_cost(const ItemRef& skill);
    virtual bool can_pay_skill_cost(const ItemRef& skill);
    bool base_can_pay_skill_cost(const ItemRef& skill);
    virtual void pay_skill_cost(const ItemRef& skill);
    void base_pay_skill_cost(const ItemRef& skill);
    bool is_occasion_ok(const Value& item);
    virtual bool meets_usable_item_conditions(const ItemRef& item);
    bool meets_skill_conditions(const ItemRef& skill);
    bool meets_item_conditions(const ItemRef& item);
    bool can_use(const ItemRef& item);
    bool can_equip(const ItemRef& item);
    bool can_equip_weapon(const Value& item);
    bool can_equip_armor(const Value& item);
    int attack_skill_id() const { return 1; }
    int guard_skill_id() const { return 2; }
    bool can_attack() { return can_use(ItemRef::skill(attack_skill_id())); }
    bool can_guard() { return can_use(ItemRef::skill(guard_skill_id())); }
    bool map_regen_update_check(const char* type);
    void request_status_refresh() { status_refresh_requested = true; }
    bool is_immortal() const { return immortal_state; }
    bool no_hidden_skill_conditions_met(const ItemRef& skill);
    bool meets_custom_show_eval(const ItemRef& skill);
    bool meets_skill_conditions_eval(const ItemRef& skill);
    bool can_pay_skill_hp_cost(const ItemRef& skill);
    void pay_skill_hp_cost(const ItemRef& skill);
    void pay_skill_eval_cost(const ItemRef& skill);
    GameBattlerBase* state_origin_battler(int id);
    void clear_state_origin(int id) { state_origin.erase(id); }
    void clear_enemy_state_origins();
    double state_turns_of(int id) const;
    void set_state_turns(int id, double turns) { state_turns[id] = std::floor(turns); }
    double buff_turns_of(int id) const { return id >= 0 && id < 8 ? buff_turns[id] : 0; }
    void set_buff_turns(int id, double turns) {
        if (id >= 0 && id < 8) buff_turns[id] = std::floor(turns);
    }
    virtual double max_buff_limit(int id);
    virtual double max_debuff_limit(int id);
    void init_state_counter() { state_counter.clear(); }
    void set_state_counter(int id, const js::Val& v);
    void add_state_counter(int id, double v);
    void remove_state_counter(int id) { state_counter.erase(id); }
    js::Val get_state_counter(int id) const;
    bool is_custom_clear_states() const;
    void retain_custom_clear_states(const std::vector<int>& states, const std::map<int, double>& turns);
    std::vector<int> passive_states_raw();
    virtual std::vector<int> passive_states_raw_impl();
    std::vector<const Value*> passive_states();
    std::vector<int> get_passive_state_data(const Value* obj);
    bool meet_passive_state_condition(int id);
    bool passive_state_conditions(const Value& state);
    bool passive_state_condition_eval(const Value& state);
    bool passive_condition_case_eval(const Value& state, const Value& data);
    void sort_passive_states(std::vector<const Value*>& states);
    bool is_passive_state_affected(int id);
    bool check_aura_alive() { return hp() > 0 && is_alive(); }
    bool check_aura_dead() { return hp() <= 0 || is_dead(); }
    std::vector<int> valid_aura_state_ids();
    virtual std::vector<int> aura_state_ids();
    std::vector<int> get_aura_state_type_id(const std::string& type);
    bool meet_aura_state_condition(int id);
    void update_auras(int id);
    int total_buffs() const;
    int total_debuffs() const;
    bool is_chain_skill_restrict(const ItemRef& skill);
    bool active_chain_enabled = false;
    void clear_horror_effects() { horror::clear(horror_filters); }
    void create_horror_effect(const std::string& t) { horror::create(horror_filters, t); }
    void remove_horror_effect(const std::string& t) { horror::remove(horror_filters, t); }
    void set_horror_effect_to_value(const std::string& t, const std::string& k, const js::Val& v) { horror::set_value(horror_filters, t, k, v); }
    virtual bool is_learned_skill(int id) const { (void)id; return false; }
    virtual bool taunt_physical();
    virtual bool taunt_magical();
    virtual bool taunt_certain();
    virtual bool null_taunt_physical();
    virtual bool null_taunt_magical();
    virtual bool null_taunt_certain();
    virtual bool ignore_taunt_physical();
    virtual bool ignore_taunt_magical();
    virtual bool ignore_taunt_certain();
    bool states_flag(const char* key);

    double hp() const { return hp_; }
    double mp() const { return mp_; }
    double tp() const { return tp_; }
    double mhp() { return param(0); }
    double mmp() { return param(1); }
    double atk() { return param(2); }
    double def() { return param(3); }
    double mat() { return param(4); }
    double mdf() { return param(5); }
    double agi() { return param(6); }
    double luk() { return param(7); }
    double hit() { return xparam(0); }
    double eva() { return xparam(1); }
    double cri() { return xparam(2); }
    double cev() { return xparam(3); }
    double mev() { return xparam(4); }
    double mrf() { return xparam(5); }
    double cnt() { return xparam(6); }
    double hrg() { return xparam(7); }
    double mrg() { return xparam(8); }
    double trg() { return xparam(9); }
    double tgr() { return sparam(0); }
    double grd() { return sparam(1); }
    double rec() { return sparam(2); }
    double pha() { return sparam(3); }
    double mcr() { return sparam(4); }
    double tcr() { return sparam(5); }
    double pdr() { return sparam(6); }
    double mdr() { return sparam(7); }
    double fdr() { return sparam(8); }
    double exr() { return sparam(9); }

    js::Val eval_note_code(const Value& ast, const char* result_var, const js::Val& init, std::initializer_list<std::pair<const char*, js::Val>> extra, const char* where);
};

class GameBattler : public GameBattlerBase {
public:
    GameBattler();
    void init_members() override;
    std::vector<std::shared_ptr<GameAction>> actions;
    double speed_ = 0;
    std::unique_ptr<GameActionResult> result_;
    std::string action_state;
    int last_target_index = 0;
    struct Anim {
        int id = 0;
        bool mirror = false;
        double delay = 0;
    };
    std::vector<Anim> animations;
    std::vector<GameActionResult> damage_popup;
    std::string effect_type;
    std::string motion_type;
    int weapon_image_id = 0;
    bool motion_refresh = false;
    bool selected = false;
    std::vector<int> free_state_turn;
    int self_turn_count = 0;
    int counters = 0;
    bool flinched = false;
    js::Val no_effect_message;
    js::Val no_state_message;
    bool has_group_defeat_cache = false;
    bool group_defeat_cache = false;
    bool has_counter_total_cache = false;
    double counter_total_cache = 0;

    GameActionResult& result() { return *result_; }
    void clear_result() { result_->clear(); }
    void refresh() override;
    virtual void add_state(int id);
    void base_add_state(int id);
    bool is_state_addable(int id);
    bool is_state_restrict(int id);
    void on_restrict() override;
    virtual void remove_state(int id);
    void add_buff(int id, double turns);
    void add_debuff(int id, double turns);
    void remove_buff(int id);
    void remove_battle_states();
    void remove_all_buffs();
    void remove_states_auto(int timing);
    void remove_buffs_auto();
    void remove_states_by_damage();
    void clear_actions() { actions.clear(); }
    void consume_item(const ItemRef& item);
    void gain_hp(double v);
    void gain_mp(double v);
    void gain_tp(double v);
    void gain_silent_tp(double v) { set_tp(tp() + v); }
    void clear_tp() { set_tp(0); }
    void set_action_state(const std::string& s);
    bool is_undecided() const { return action_state == "undecided"; }
    bool is_inputting() const { return action_state == "inputting"; }
    bool is_waiting() const { return action_state == "waiting"; }
    bool is_acting() const { return action_state == "acting"; }
    void custom_effect_eval(int state_id, const std::string& type);
    void add_state_effects(int id);
    void remove_state_effects(int id);
    void leave_state_effects(int id);
    void remove_state_category_all(const std::string& category);
    void remove_state_category(const std::string& category, int times);
    bool is_state_category_affected(const std::string& category);
    int get_state_category_affected_count(const std::string& category);
    bool is_group_defeat_affected();
    bool is_emotion_affected(const std::string& emotion);
    bool is_any_emotion_affected();
    bool is_emotional_state(int id);
    bool is_state_state(int id);
    void set_state_origin(int id, GameBattlerBase* battler);
    void request_motion(const std::string& m) { motion_type = m; }
    void request_motion_refresh() { motion_refresh = true; }
    void request_effect(const std::string& e) { effect_type = e; }
    void start_animation(int id, bool mirror, double delay);
    void start_damage_popup();
    virtual void on_add_state_extra(int id, bool addable) { (void)id, (void)addable; }
    virtual void perform_collapse();
    virtual void on_turn_end();
    void force_action(int skill_id, int target_index);

    BattlerSpriteLink* sprite_link = nullptr;
    std::vector<int> counter_skills_;
    bool has_counter_skills = false;
    std::vector<std::shared_ptr<GameAction>> original_pre_counter_actions;
    bool has_original_pre_counter_actions = false;
    bool collapse_sound_played = false;
    std::vector<Anim> animation_queue;
    virtual std::string name() const = 0;
    void clear_animations() { animations.clear(); }
    void clear_damage_popup() { damage_popup.clear(); }
    void clear_weapon_animation() { weapon_image_id = 0; }
    void clear_effect() { effect_type.clear(); }
    void clear_motion() {
        motion_type.clear();
        motion_refresh = false;
    }
    void select() { selected = true; }
    void deselect() { selected = false; }
    bool is_animation_requested() const { return !animations.empty(); }
    bool is_damage_popup_requested() const { return !damage_popup.empty(); }
    bool is_effect_requested() const { return !effect_type.empty(); }
    bool is_motion_requested() const { return !motion_type.empty(); }
    bool is_weapon_animation_requested() const { return weapon_image_id > 0; }
    bool is_motion_refresh_requested() const { return motion_refresh; }
    bool is_selected();
    Anim shift_animation();
    GameActionResult shift_damage_popup();
    void start_weapon_animation(int id);
    std::shared_ptr<GameAction> action(int index) const;
    void set_action(int index, std::shared_ptr<GameAction> action);
    int num_actions() const { return (int)actions.size(); }
    int make_action_times();
    virtual void make_actions();
    void base_make_actions();
    void make_speed();
    double speed() const { return speed_; }
    std::shared_ptr<GameAction> current_action() const { return actions.empty() ? nullptr : actions.front(); }
    void remove_current_action() {
        if (!actions.empty()) actions.erase(actions.begin());
    }
    void set_last_target(GameBattler* target);
    void use_item(const ItemRef& item);
    void init_tp();
    void charge_tp_by_damage(double rate);
    void regenerate_hp();
    double max_slip_damage();
    void regenerate_mp();
    void regenerate_tp();
    void regenerate_all();
    virtual void on_battle_start();
    void on_all_actions_end();
    virtual void on_battle_end();
    void on_damage(double value);
    void escape();
    bool is_chanting() const;
    bool is_guard_waiting() const;
    virtual void perform_action_start(GameAction* action);
    virtual void perform_action(GameAction* action) { (void)action; }
    void perform_action_end();
    virtual void perform_damage();
    void perform_miss();
    void perform_recovery();
    virtual void perform_evasion();
    virtual void perform_magic_evasion();
    virtual void perform_counter();
    void perform_reflection();
    void perform_substitute(GameBattler* target);
    void add_immortal() { immortal_state = true; }
    void remove_immortal();
    void perform_result_effects();
    void perform_flinch() {}
    void force_motion(const std::string& motion);
    virtual int reflect_animation_id();
    virtual bool sprite_can_move() { return false; }
    double sprite_pos_x() const;
    double sprite_pos_y() const;
    virtual double sprite_width();
    virtual double sprite_height();
    virtual std::string attack_motion() { return "thrust"; }
    virtual void perform_attack() {}
    void force_motion_refresh();
    void on_turn_start();
    void update_tick();
    void increase_self_turn_count() { self_turn_count += 1; }
    double turn_count() const;
    void create_actions();
    virtual std::string idle_motion() { return "walk"; }
    virtual std::string dead_motion() { return "dead"; }
    virtual std::string sleep_motion() { return "sleep"; }
    virtual std::string chant_motion() { return "chant"; }
    virtual std::string guard_motion() { return "guard"; }
    virtual std::string abnormal_motion() { return "abnormal"; }
    virtual std::string dying_motion() { return "dying"; }
    virtual std::string wait_motion() { return "wait"; }
    void sprite_opacity(double opacity, double duration);
    double camera_pos_x() const { return sprite_pos_x(); }
    double camera_pos_y();
    std::vector<ItemRef> counter_skills() const;
    virtual void make_counter_skills();
    void base_make_counter_skills();
    void extend_counter_skills_eval(const Value& obj);
    double get_counter_total_eval(const Value& obj);
    virtual double counter_total();
    double base_counter_total();
    void pay_counter(double value) { counters += (int)(value ? value : 1); }
    bool can_counter();
    virtual double target_counter_rate();
    virtual double target_counter_flat();
    virtual double target_counter_rate_eval(double rate, GameBattler* target, const ItemRef& item);
    double get_target_cnt_rate_eval(const Value* obj, double rate, GameBattler* target, const ItemRef& item);
    virtual bool force_evade_counter();
    virtual bool force_hit_counter();
    void force_evade_popup();
    void set_counter_action(std::shared_ptr<GameAction> action);
    void remove_counter_action();
    virtual void set_ai_pattern() { base_make_actions(); }
    bool has_state(int id);
    bool not_state(int id) { return !is_state_affected(id); }
    virtual double ai_level() const;
    void on_turn_start_state_effects();
    void on_turn_end_state_effects();
    bool meet_turn_end_state_effects_conditions();
    void on_regenerate_state_effects();
    void regenerate_state_effects(int state_id);
    void on_action_start_state_effects();
    void on_action_end_state_effects();
    void remove_state_category_effect(const Value& obj, GameBattler* user);
    double remove_state_category_eval(const Value& obj, const std::string& category, GameBattler* user, double value);
    virtual bool is_damage_capped();
    virtual double maximum_damage();
    virtual double maximum_healing();
    virtual double critical_multiplier_bonus();
    virtual double flat_critical_bonus();
    virtual double certain_crit_rate_bonus();
    virtual double physical_crit_rate_bonus();
    virtual double magical_crit_rate_bonus();
    double states_sum(const char* key) const;
    void life_steal(double damage, const std::string& type, GameBattler* target, double rate, double flat, bool mp);
    virtual double get_life_steal_rate(const std::string& type, GameBattler* target);
    virtual double get_life_steal_flat(const std::string& type, GameBattler* target);
    double life_steal_eval(const Value* ast, GameBattler* target, const char* var, const char* where);
    virtual bool is_life_steal_state(const std::string& type) const;
    double steal_rate_bonus(int index) const;
    double steal_rate_bonus_type(const std::string& type) const;
    void after_steal_eval(GameBattler* target, const ItemRef& skill, const ItemRef& item);
    void after_steal_success_eval(GameBattler* target, const ItemRef& skill, const ItemRef& item);
    std::string get_state_emotion();
    void enable_active_chain() {
        if (is_actor()) active_chain_enabled = true;
    }
    void disable_active_chain() {
        if (is_actor()) active_chain_enabled = false;
    }
    void add_active_skill_chain(int skill_id);
    bool is_emotion_addable(const std::string& type);
    virtual double anchor_x() { return 0.5; }
    virtual double anchor_y() { return 1; }
    double element_rate_undefined() { return traits_pi(TRAIT_ELEMENT_RATE, -1000000); }
    const std::vector<const Value*> state_objects_for(const Value* obj) const;
};

class GameActor : public GameBattler {
public:
    explicit GameActor(int actor_id);
    void init_members() override;
    int actor_id_ = 0;
    std::string name_, nickname_, profile_;
    int class_id = 0;
    int level = 0;
    std::string character_name_;
    int character_index_ = 0;
    std::string face_name_;
    int face_index_ = 0;
    std::string battler_name_;
    std::map<int, double> exp_;
    std::vector<int> skills_;
    std::vector<GameItem> equips_;
    int action_input_index = 0;
    GameItem last_menu_skill, last_battle_skill;
    std::string last_command_symbol;
    std::map<int, double> state_steps;
    MovementGraphics character_movement_graphics;
    std::vector<int> equipped_skills;
    std::vector<int> locked_equipment_slots;
    std::vector<int> disabled_battle_commands;
    int faraway_actor_id = 0;
    bool use_victory_face = false;
    std::string battle_face_name;
    std::vector<bool> locked_equipped_skills;
    std::vector<int> locked_skills;
    js::Val battle_commands_list;
    bool has_anchor_x = false, has_anchor_y = false;
    double anchor_x_cache = 0, anchor_y_cache = 0;
    int disable_dashing_cache = -1;
    bool played_collapse_sound = false;
    js::Val level_up_data;

    void setup(int actor_id);
    int actor_id() const { return actor_id_; }
    const Value& actor() const;
    std::string name() const override;
    void set_name(const std::string& n) { name_ = n; }
    const std::string& nickname() const { return nickname_; }
    const std::string& profile() const { return profile_; }
    const std::string& character_name() const { return character_name_; }
    int character_index() const { return character_index_; }
    const std::string& face_name() const { return face_name_; }
    int face_index() const { return face_index_; }
    std::string battler_name() const { return battler_name_; }
    void clear_states() override;
    void erase_state(int id) override;
    void reset_state_counts(int id) override;
    void init_images();
    double exp_for_level(int lvl) const;
    void init_exp();
    double current_exp() const;
    double current_level_exp() const { return exp_for_level(level); }
    double next_level_exp() const { return exp_for_level(level + 1); }
    double next_required_exp() const { return next_level_exp() - current_exp(); }
    int max_level() const;
    bool is_max_level() const;
    void init_skills();
    void init_equips(const Value& equips);
    std::vector<int> equip_slots();
    std::vector<ItemRef> equips();
    std::vector<ItemRef> weapons();
    std::vector<ItemRef> armors();
    bool has_weapon(const ItemRef& w);
    bool has_armor(const ItemRef& a);
    bool is_equip_change_ok(int slot);
    void change_equip(int slot, const ItemRef& item);
    void force_change_equip(int slot, const ItemRef& item);
    bool trade_item_with_party(const ItemRef& new_item, const ItemRef& old_item);
    void change_equip_by_id(int etype, int item_id);
    bool is_equipped(const ItemRef& item);
    void discard_equip(const ItemRef& item);
    void release_unequippable_items(bool forcing);
    void clear_equipments();
    void optimize_equipments();
    ItemRef best_equip_item(int slot);
    bool is_skill_wtype_ok(const Value& skill) override;
    bool is_wtype_equipped(int wtype);
    void refresh() override;
    bool is_actor() const override { return true; }
    GameUnit& friends_unit() const override;
    GameUnit& opponents_unit() const override;
    int index() const override;
    bool is_battle_member() const;
    const Value& current_class() const;
    std::vector<ItemRef> skills();
    std::vector<ItemRef> usable_skills();
    std::vector<const Value*> trait_objects() override;
    std::vector<int> attack_elements() override;
    bool has_no_weapons() { return weapons().empty(); }
    double param_max(int id) const override;
    double param_base(int id) const override;
    double param_plus(int id) override;
    double eval_param_plus(const Value& item, int id);
    int attack_animation_id1();
    int attack_animation_id2();
    void change_exp(double exp, bool show);
    void level_up();
    void level_down() { level--; }
    std::vector<ItemRef> find_new_skills(const std::vector<ItemRef>& last);
    void display_level_up(const std::vector<ItemRef>& new_skills);
    void gain_exp(double exp);
    double final_exp_rate();
    void change_level(int level, bool show);
    void learn_skill(int id);
    void forget_skill(int id);
    bool is_learned_skill(int id) const override;
    bool has_skill(int id);
    void change_class(int class_id, bool keep_exp);
    void set_character_image(const std::string& n, int i) {
        character_name_ = n;
        character_index_ = i;
    }
    void set_face_image(const std::string& n, int i) {
        face_name_ = n;
        face_index_ = i;
    }
    void set_battler_image(const std::string& n) { battler_name_ = n; }
    void on_player_walk();
    void update_state_steps(const Value& state);
    void show_added_states();
    void show_removed_states();
    void turn_end_on_map();
    void check_floor_effect();
    void execute_floor_damage();
    void perform_map_damage();
    bool meets_usable_item_conditions(const ItemRef& item) override;
    void init_battle_commands_list();
    bool can_pay_skill_cost(const ItemRef& skill) override;
    void pay_skill_cost(const ItemRef& skill) override;
    std::vector<ItemRef> skills_list();
    bool is_equip_skill_slot_locked(int slot) const;
    bool can_equip_skill(const ItemRef& skill) const;
    std::vector<ItemRef> equipable_skills();
    void equip_skill(int index, int skill_id);
    void unequip_skill(int index, bool learn);
    std::string battle_status_face_name() const;
    std::string menu_status_face_name() const;
    int status_face_index();
    int status_back_index();
    int status_list_index();
    int battle_status_index() const;
    bool is_dash_disabled();
    std::vector<int> passive_states_raw_impl() override;
    double max_buff_limit(int id) override;
    double max_debuff_limit(int id) override;
    bool taunt_physical() override;
    bool taunt_magical() override;
    bool taunt_certain() override;
    bool null_taunt_physical() override;
    bool null_taunt_magical() override;
    bool null_taunt_certain() override;
    bool ignore_taunt_physical() override;
    bool ignore_taunt_magical() override;
    bool ignore_taunt_certain() override;
    bool owner_flag(const char* key);
    void set_movement_graphics(const js::Val& idle, const js::Val& walking, const js::Val& running);
    void set_movement_graphic_data(const std::string& name, const js::Val& data);
    void clear_movement_graphics() { character_movement_graphics = MovementGraphics(); }
    std::string face_save_load() const;
    bool is_class(int id) const { return data::classes()[id].is_obj() && class_id == id; }
    bool is_sprite_visible() const;
    void perform_action(GameAction* action) override;
    void perform_attack() override;
    void perform_damage() override;
    void perform_evasion() override;
    void perform_magic_evasion() override;
    void perform_counter() override;
    void perform_victory();
    void perform_escape();
    void perform_escape_success();
    std::vector<std::shared_ptr<GameAction>> make_action_list();
    void make_auto_battle_actions();
    void make_confusion_actions();
    void make_actions() override;
    std::shared_ptr<GameAction> inputting_action() const { return action(action_input_index); }
    bool select_next_command();
    bool select_previous_command();
    void set_last_menu_skill(const ItemRef& s) { last_menu_skill.set_object(s); }
    void set_last_battle_skill(const ItemRef& s) { last_battle_skill.set_object(s); }
    void set_last_command_symbol(const std::string& s) { last_command_symbol = s; }
    bool test_escape(const ItemRef& item) const;
    int reflect_animation_id() override;
    bool sprite_can_move() override;
    double sprite_width() override { return 1; }
    double sprite_height() override { return 1; }
    std::string attack_motion() override;
    bool has_custom_battle_commands() const;
    bool can_show_item_battle_command(const ItemRef& item);
    js::Val gauge(int n) const override;
    js::Val gauge_icon(int n) const override;
    void make_counter_skills() override;
    double target_counter_rate() override;
    double target_counter_flat() override;
    double target_counter_rate_eval(double rate, GameBattler* target, const ItemRef& item) override;
    bool force_evade_counter() override;
    bool force_hit_counter() override;
    bool is_damage_capped() override;
    double maximum_damage() override;
    double maximum_healing() override;
    double critical_multiplier_bonus() override;
    double flat_critical_bonus() override;
    double certain_crit_rate_bonus() override;
    double physical_crit_rate_bonus() override;
    double magical_crit_rate_bonus() override;
    double get_life_steal_rate(const std::string& type, GameBattler* target) override;
    double get_life_steal_flat(const std::string& type, GameBattler* target) override;
    bool is_life_steal_state(const std::string& type) const override;
    std::vector<int> aura_state_ids() override;
    double steal_rate_bonus_actor(int index) const;
    void on_battle_start() override;
    void on_battle_end() override;
    bool is_equipment_slot_locked(int slot) const;
    void lock_equipment_slots(const std::vector<int>& slots);
    void unlock_equipment_slots(const std::vector<int>& slots);
    bool is_battle_command_disabled(int index) const;
    void disable_battle_commands(const std::vector<int>& ids);
    void enable_battle_commands(const std::vector<int>& ids);
    void lock_equip_skill(const std::vector<int>& ids);
    void unlock_equip_skill(const std::vector<int>& ids);
    std::map<std::string, std::string> status_state_particles_data();
    int face_save_load_index() const { return 0; }
    double anchor_x() override;
    double anchor_y() override;
    js::HostClass* js_class() const override;
};

std::string actor_meta(const Value& obj, const char* key);

}
