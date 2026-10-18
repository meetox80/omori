#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "battler.h"

namespace rpg {

class GameAction;

struct StealableItem {
    std::string type;
    int id = 0;
    std::string rate;
    bool is_stolen = false;
    bool is_drop = false;
};

class GameEnemy : public GameBattler {
public:
    GameEnemy(int enemy_id, double x, double y);
    void init_members() override;
    int enemy_id_ = 0;
    std::string letter;
    bool plural = false;
    double screen_x_ = 0, screen_y_ = 0;
    bool has_stealables = false;
    std::vector<StealableItem> stealables;
    int troop_id = 0;
    int troop_member_id = 0;
    bool has_troop_member_id = false;
    bool is_state_transforming = false;
    bool has_death_turn = false;
    double death_turn_ = 0;
    bool struck_created = false;
    std::map<int, double> struck_skills, struck_stype, struck_items, struck_states, struck_elements;
    int last_struck_id = 0;
    bool last_struck_skill = false;
    int last_struck_actor = 0;
    bool has_sv_weapon = false;
    int sv_weapon_image_id = 0;
    std::string sv_attack_motion;
    bool has_sv_attack_animation = false;
    int sv_attack_animation_id = 0;
    bool has_sv_battler_name = false;
    js::Val sv_battler_name;
    std::string sv_idle_motion;

    void setup(int enemy_id, double x, double y);
    bool is_enemy() const override { return true; }
    GameUnit& friends_unit() const override;
    GameUnit& opponents_unit() const override;
    int index() const override;
    bool is_battle_member() const { return index() >= 0; }
    int enemy_id() const { return enemy_id_; }
    const Value& enemy() const;
    std::vector<const Value*> trait_objects() override;
    double param_base(int id) const override;
    double exp() const { return enemy()["exp"].num(); }
    double gold();
    std::vector<ItemRef> make_drop_items();
    std::vector<ItemRef> base_make_drop_items();
    double drop_item_rate() const;
    ItemRef item_object(int kind, int id) const;
    bool is_sprite_visible() const { return true; }
    double screen_x() const { return screen_x_; }
    double screen_y() const { return screen_y_; }
    std::string battler_name() const;
    int battler_hue() const { return enemy()["battlerHue"].as_int(); }
    std::string original_name() const { return enemy()["name"].string(); }
    std::string name() const override { return original_name() + (plural ? letter : ""); }
    void set_letter(const std::string& l) { letter = l; }
    void set_plural(bool p) { plural = p; }
    void perform_action_start(GameAction* action) override;
    void perform_action(GameAction* action) override;
    void perform_damage() override;
    void perform_collapse() override;
    void transform(int enemy_id);
    bool meets_condition(const Value& action);
    bool meets_turn_condition(double p1, double p2);
    bool is_action_valid(const Value& action);
    const Value* select_action(const std::vector<const Value*>& list, double rating_zero);
    void select_all_actions(std::vector<const Value*> list);
    void base_make_actions();
    void make_actions() override;
    std::vector<ItemRef> skills() const;
    int attack_animation_id();
    int base_attack_animation_id() const { return enemy()["attackAnimationId"].as_int(); }
    int attack_animation_id1() { return attack_animation_id(); }
    int attack_animation_id2() { return attack_animation_id(); }
    int reflect_animation_id() override;
    bool sprite_can_move() override;
    void make_counter_skills() override;
    double counter_total() override;
    double target_counter_rate() override;
    double target_counter_flat() override;
    double target_counter_rate_eval(double rate, GameBattler* target, const ItemRef& item) override;
    bool force_evade_counter() override;
    bool force_hit_counter() override;
    bool ai_consider_taunt() const;
    void set_ai_pattern() override;
    double ai_level() const override { return enemy()["aiLevel"].num(); }
    double max_buff_limit(int id) override;
    double max_debuff_limit(int id) override;
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
    bool taunt_physical() override;
    bool taunt_magical() override;
    bool taunt_certain() override;
    bool null_taunt_physical() override;
    bool null_taunt_magical() override;
    bool null_taunt_certain() override;
    bool ignore_taunt_physical() override;
    bool ignore_taunt_magical() override;
    bool ignore_taunt_certain() override;
    std::vector<int> passive_states_raw_impl() override;
    std::vector<int> aura_state_ids() override;
    void create_stealable_items();
    std::vector<StealableItem>& stealable_items();
    bool all_items_stolen(const ItemRef& skill);
    double steal_resist() const { return enemy()["stealResist"].num(); }
    std::vector<ItemRef> make_conditional_drop_items();
    void mark_death_turn();
    void mark_struck_state(int id);
    double death_turn() const { return has_death_turn ? death_turn_ : 0; }
    void create_times_struck() { struck_created = true; }
    ItemRef last_struck_action() const;
    void mark_struck_actions(const ItemRef& item, GameBattler* subject, GameAction* action);
    GameBattler* killer();
    void mark_last_struck_actor(GameBattler* subject);
    void mark_struck_elements(const ItemRef& item, GameBattler* subject);
    double times_struck(const std::map<int, double>& m, int id) const;
    void clear_sv_attributes();
    void setup_sv_attributes();
    js::Val sv_battler_name_value();
    bool has_sv_battler();
    int weapon_image_id();
    std::string attack_motion() override;
    std::string idle_motion() override;
    std::string damage_motion() const { return enemy()["sideviewDmgMotion"].string(); }
    std::string evade_motion() const { return enemy()["sideviewEvadeMotion"].string(); }
    std::string escape_motion() const { return enemy()["sideviewEscMotion"].string(); }
    std::string guard_motion() override { return enemy()["sideviewGuardMotion"].string(); }
    std::string abnormal_motion() override { return enemy()["sideviewAbnMotion"].string(); }
    std::string sleep_motion() override { return enemy()["sideviewSleepMotion"].string(); }
    std::string dying_motion() override { return enemy()["sideviewDyingMotion"].string(); }
    std::string dead_motion() override { return enemy()["sideviewDeadMotion"].string(); }
    double anchor_x() override;
    double anchor_y() override;
    js::Val sideview_width() const;
    js::Val sideview_height() const;
    double sprite_scale_x() const;
    double sprite_scale_y() const { return enemy()["spriteScaleY"].num(); }
    void perform_attack() override;
    void perform_evasion() override;
    void perform_magic_evasion() override;
    void perform_counter() override;
    void perform_escape();
    bool is_breathing() { return !is_dead() && enemy()["sideviewBreathing"].truthy(); }
    bool is_floating() {
        if (is_dead() && !enemy()["sideviewFloatDeath"].truthy()) return false;
        return enemy()["sideviewFloating"].truthy();
    }
    bool is_hide_sv_weapon();
    void add_state(int id) override;
    void remove_state(int id) override;
    void refresh_emotion_state_transform();
    int base_id() const;
    bool is_scanned() const;
    double battle_status_position() const;
    double battle_status_cursor_position() const;
    double battle_status_x_offset() const;
    double battle_status_y_offset() const;
    void apply_battle_position_offsets();
    double battle_damage_x_offset() const;
    double battle_damage_y_offset() const;
    bool can_scan() const { return enemy()["meta"].find("ScanImmunity") == nullptr; }
    void die() override;
    void appear() override;
    bool has_skill_id(int id) const;
    void on_battle_start() override;
    void on_add_state_extra(int id, bool addable) override;
    js::Val gauge(int n) const;
    js::Val gauge_icon(int n) const;
    js::HostClass* js_class() const override;
};

}
