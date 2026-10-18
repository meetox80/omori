#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../js/bind.h"
#include "battler.h"

namespace rpg {

class GameUnit : public js::JsObject {
public:
    bool in_battle_ = false;
    bool in_battle() const { return in_battle_; }
    virtual std::vector<GameBattler*> unit_members() = 0;
    virtual std::vector<GameBattler*> unit_all_members() { return unit_members(); }
    std::vector<GameBattler*> alive_members();
    std::vector<GameBattler*> dead_members();
    std::vector<GameBattler*> movable_members();
    std::vector<GameBattler*> all_alive_members();
    double agility();
    void clear_actions();
    void clear_results();
    bool is_all_dead();
    void refresh_members();
    std::vector<int> aura_state_type_ids(const std::string& type);
    void process_state_eval(const std::string& type);
    GameBattler* random_target();
    GameBattler* random_dead_target();
    GameBattler* smooth_target(int index);
    GameBattler* smooth_dead_target(int index);
    GameBattler* substitute_battler();
    void select(GameBattler* active);
    void request_motion_refresh();
    int lowest_index_member();
    std::vector<GameBattler*> taunt_members();
    std::vector<GameBattler*> physical_taunt_members();
    std::vector<GameBattler*> magical_taunt_members();
    std::vector<GameBattler*> certain_taunt_members();
};

using ItemCounts = std::map<int, double>;

struct WorldItems {
    ItemCounts items, weapons, armors;
};

struct LastItemData {
    int id = 0;
    int type = 0;
    double amount = 0;
};

class GameActors : public js::JsObject {
public:
    std::map<int, std::unique_ptr<GameActor>> data;
    GameActor* actor(int id);
    js::HostClass* js_class() const override;
};

class GameParty : public GameUnit {
public:
    GameParty();
    double gold_ = 0;
    double steps_ = 0;
    GameItem last_item;
    int menu_actor_id = 0;
    int target_actor_id = 0;
    std::vector<int> actors;
    ItemCounts items_, weapons_, armors_;
    int active_world = -1;
    ItemCounts& items_c();
    ItemCounts& weapons_c();
    ItemCounts& armors_c();
    const ItemCounts& items_c() const;
    const ItemCounts& weapons_c() const;
    const ItemCounts& armors_c() const;
    std::vector<int> scanned_enemy_list;
    std::vector<int> black_letters;
    int black_letter_index = 0;
    LastItemData last_item_data;
    double stress_energy_count = 0;
    std::map<int, WorldItems> world_items;
    int last_world_item_index = 0;
    js::Val quest_list;
    js::Val quest_stand_by_message;
    js::Val album_pictures;
    js::Val album_picture_positions;
    std::vector<int> defeated_enemies;

    std::vector<GameBattler*> unit_members() override;
    std::vector<GameBattler*> unit_all_members() override;
    void init_all_items();
    bool exists() const { return !actors.empty(); }
    int size() { return (int)members().size(); }
    bool is_empty() { return size() == 0; }
    std::vector<GameActor*> members();
    std::vector<GameActor*> all_members();
    std::vector<GameActor*> battle_members();
    int max_battle_members() const { return 4; }
    GameActor* leader();
    GameActor* battle_member(int index);
    GameActor* first_member();
    void revive_battle_members();
    std::vector<ItemRef> items() const;
    std::vector<ItemRef> weapons() const;
    std::vector<ItemRef> armors() const;
    std::vector<ItemRef> equip_items() const;
    std::vector<ItemRef> all_items() const;
    ItemCounts* item_container(const ItemRef& item);
    const ItemCounts* item_container(const ItemRef& item) const;
    void setup_starting_members();
    std::string name();
    int highest_level();
    int lowest_level();
    double average_level();
    void add_actor(int id);
    void remove_actor(int id);
    double gold() const { return gold_; }
    void gain_gold(double amount);
    void lose_gold(double amount) { gain_gold(-amount); }
    double max_gold() const;
    double steps() const { return steps_; }
    void increase_steps() { steps_++; }
    double num_items(const ItemRef& item) const;
    double max_items(const ItemRef& item) const;
    bool has_max_items(const ItemRef& item) const { return num_items(item) >= max_items(item); }
    bool has_item(const ItemRef& item, bool include_equip);
    bool is_any_member_equipped(const ItemRef& item);
    void gain_item(const ItemRef& item, double amount, bool include_equip = false);
    void discard_members_equip(const ItemRef& item, int amount);
    void lose_item(const ItemRef& item, double amount, bool include_equip = false) { gain_item(item, -amount, include_equip); }
    void consume_item(const ItemRef& item);
    bool can_use(const ItemRef& item);
    bool can_input();
    bool is_all_dead();
    void on_player_walk();
    GameActor* menu_actor();
    void set_menu_actor(GameActor* a);
    void make_menu_actor_next();
    void make_menu_actor_previous();
    GameActor* target_actor();
    void set_target_actor(GameActor* a);
    ItemRef last_item_object() const { return last_item.object(); }
    void set_last_item(const ItemRef& item) { last_item.set_object(item); }
    void swap_order(int i1, int i2);
    bool party_ability(int id);
    bool has_encounter_half() { return party_ability(0); }
    bool has_encounter_none() { return party_ability(1); }
    bool has_cancel_surprise() { return party_ability(2); }
    bool has_raise_preemptive() { return party_ability(3); }
    bool has_gold_double() { return party_ability(4); }
    bool has_drop_item_double() { return party_ability(5); }
    void remove_battle_states();
    bool has_key_items(bool hidden = false);
    bool has_consumable_items(bool hidden = false);
    bool has_toy_items(bool hidden = false);
    bool has_valid_pocket_items();
    bool is_in_faraway() const;
    void init_world_items_container();
    void change_world_item_container(int world);
    ItemRef last_gained_item() const;
    void sort_members_for_tag(int leader_id);
    void add_scanned_enemy(int id);
    GameActor* member_at_status_index(int index);
    bool actor_is_affected_by_state(int actor_id, int state_id);
    GameActor* get_omori();
    void set_quest_stand_by_message(const js::Val& m) { quest_stand_by_message = m; }
    js::Val add_quest(const std::string& id, const js::Val& data);
    void remove_quest(const std::string& id);
    bool has_quest(const std::string& id) const;
    std::vector<js::Val> completed_quest_list(int world) const;
    std::vector<js::Val> incomplete_quest_list(int world) const;
    void set_quest_complete_state(const std::string& id, bool state);
    void set_quest_message_index(const std::string& id, const js::Val& index);
    void create_picture_album(const std::string& album);
    void randomize_album_picture_positions(const std::string& album, const js::Val& pictures, bool force);
    int album_size(const std::string& album);
    void add_picture_to_album(const std::string& album, int index, const js::Val& id);
    void remove_picture_to_album(const std::string& album, int index);
    js::Val get_album_picture_at_index(const std::string& album, int index) const;
    js::Val album_has_picture(const std::string& album, const js::Val& index) const;
    void add_defeated_enemy(int id);
    std::string last_black_letter() const;
    void on_battle_start();
    void on_battle_end();
    void make_actions();
    void create_actions();
    void on_turn_start();
    void update_tick();
    void perform_victory();
    void perform_escape();
    void perform_escape_success();
    void request_motion_refresh_all();
    double battle_num_items(const ItemRef& item);
    bool ai_element_rate_known(GameBattler* target, int element_id) const { (void)target, (void)element_id; return true; }
    ItemRef last_black_letter_item() const;
    double rate_preemptive(double troop_agi);
    double rate_surprise(double troop_agi);
    js::HostClass* js_class() const override;
};

class GameEnemy;
class GameInterpreter;

class GameTroop : public GameUnit {
public:
    GameTroop();
    ~GameTroop() override;
    std::unique_ptr<GameInterpreter> interpreter;
    int troop_id = 0;
    std::map<int, bool> event_flags;
    std::vector<std::shared_ptr<GameEnemy>> enemies;
    std::vector<std::shared_ptr<GameEnemy>> new_enemies;
    int turn_count_ = 0;
    std::map<std::string, int> names_count;
    std::vector<int> ignore_pages;
    std::map<int, std::vector<int>> ai_known_element_rates;

    std::vector<GameBattler*> unit_members() override;
    std::vector<GameEnemy*> members() const;
    bool is_event_running() const;
    void update_interpreter();
    int turn_count() const { return turn_count_; }
    void clear();
    const Value& troop() const;
    void setup(int troop_id);
    void make_unique_names();
    std::vector<std::string> enemy_names();
    bool meets_conditions(const Value& page);
    bool base_meets_conditions(const Value& page);
    void setup_battle_event();
    void increase_turn();
    double exp_total();
    double gold_total();
    double gold_rate() const;
    std::vector<ItemRef> make_drop_items();
    void update_ai_patterns();
    bool ai_element_rate_known(GameBattler* target, int element_id);
    void ai_register_element_rate(GameBattler* target, int element_id);
    void clear_reinforcements() { new_enemies.clear(); }
    void add_reinforcement_member(int troop_id, int member_id, const Value& member);
    void add_enemy_reinforcement(int troop_id, int member_id);
    void add_troop_reinforcements(int troop_id);
    void remove_enemy_reinforcement(int troop_id, int member_id);
    void remove_troop_reinforcements(int troop_id);
    bool is_enemy_reinforcement_added(int troop_id, int member_id, bool needs_alive);
    bool is_troop_reinforcement_added(int troop_id, bool needs_alive);
    void setup_common_event(int id);
    void ignore_page(int page);
    void stop_ignore_page(int page);
    bool is_all_dead();
    bool is_white_transition_troop() const { return troop_id == 426; }
    bool is_humphrey_battle() const;
    bool wait_for_emotion() const;
    bool are_summon_troops() const;
    void on_battle_start();
    void on_battle_end();
    void make_actions();
    void create_actions();
    void on_turn_start();
    void update_tick();
    js::HostClass* js_class() const override;
};

bool is_key_item(const Value& item);
bool is_consumable_item(const Value& item);
bool is_toy_item(const Value& item);
int current_world_index();

}
