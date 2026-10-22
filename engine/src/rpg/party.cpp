#include "party.h"

#include <algorithm>
#include <cmath>

#include "../core/regex.h"
#include "../js/dataview.h"
#include "../js/random.h"
#include "../js/vm.h"
#include "action.h"
#include "battle.h"
#include "data.h"
#include "game.h"
#include "params.h"

namespace rpg {

namespace {

bool contains_int(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

js::Vm& V() { return js::vm(); }

js::Val get(const js::Val& o, const char* k) { return V().get(o, js::atom(k)); }

void put(const js::Val& o, const char* k, const js::Val& v) { V().put(o, js::atom(k), v); }

}

bool is_key_item(const Value& item) { return &item == &data::items()[item["id"].as_int()] && item["itypeId"].as_int() == 2; }

static bool has_toy_item_tag(const Value& item) { return re::test("<Is Toy>", item["note"].string()); }

bool is_consumable_item(const Value& item) {
    return &item == &data::items()[item["id"].as_int()] && item["itypeId"].as_int() == 1 && !has_toy_item_tag(item);
}

bool is_toy_item(const Value& item) { return &item == &data::items()[item["id"].as_int()] && item["itypeId"].as_int() == 1 && has_toy_item_tag(item); }

int current_world_index() {
    double v = game_variables().number(22);
    return v > 0 ? (int)v : 1;
}

std::vector<GameBattler*> GameUnit::dead_members() {
    std::vector<GameBattler*> out;
    for (GameBattler* m : unit_members()) {
        if (m->is_dead()) out.push_back(m);
    }
    return out;
}

std::vector<GameBattler*> GameUnit::movable_members() {
    std::vector<GameBattler*> out;
    for (GameBattler* m : unit_members()) {
        if (m->can_move()) out.push_back(m);
    }
    return out;
}

std::vector<GameBattler*> GameUnit::all_alive_members() {
    std::vector<GameBattler*> out;
    for (GameBattler* m : unit_members()) {
        if (m->is_alive()) out.push_back(m);
    }
    return out;
}

double GameUnit::agility() {
    auto m = unit_members();
    if (m.empty()) return 1;
    double sum = 0;
    for (GameBattler* b : m) sum += b->agi();
    return sum / m.size();
}

void GameUnit::clear_actions() {
    for (GameBattler* m : unit_members()) m->clear_actions();
}

void GameUnit::clear_results() {
    for (GameBattler* m : unit_members()) m->clear_result();
}

bool GameUnit::is_all_dead() {
    game_temp().check_all_alive_members = true;
    auto alive = alive_members();
    size_t length = alive.size();
    size_t count = 0;
    for (size_t i = 0; i < length; ++i) {
        auto cur = alive_members();
        GameBattler* member = i < cur.size() ? cur[i] : nullptr;
        if (member && member->is_group_defeat_affected()) count++;
    }
    if (count >= length) {
        game_temp().check_all_alive_members = false;
        return true;
    }
    bool value = alive_members().empty();
    game_temp().check_all_alive_members = false;
    return value;
}

void GameUnit::refresh_members() {
    for (GameBattler* m : unit_all_members()) {
        if (m) m->refresh();
    }
}

std::vector<int> GameUnit::aura_state_type_ids(const std::string& type) {
    std::vector<int> arr;
    for (GameBattler* m : all_alive_members()) {
        if (!m) continue;
        auto ids = m->get_aura_state_type_id(type);
        arr.insert(arr.end(), ids.begin(), ids.end());
    }
    return arr;
}

void GameUnit::process_state_eval(const std::string& type) {
    auto all = unit_all_members();
    for (size_t i = 0; i < all.size(); ++i) {
        GameBattler* member = all[i];
        if (!member) return;
        member->refresh();
        for (const Value* s : member->states()) {
            if (s) member->custom_effect_eval((*s)["id"].as_int(), type);
        }
    }
}

GameBattler* GameUnit::random_target() {
    auto alive = alive_members();
    double sum = 0;
    for (GameBattler* m : alive) sum += m->tgr();
    double r = rng::next() * sum;
    GameBattler* target = nullptr;
    for (GameBattler* m : alive) {
        r -= m->tgr();
        if (r <= 0 && !target) target = m;
    }
    return target;
}

GameBattler* GameUnit::random_dead_target() {
    auto dead = dead_members();
    if (dead.empty()) return nullptr;
    return dead[(size_t)std::floor(rng::next() * dead.size())];
}

GameBattler* GameUnit::smooth_target(int index) {
    if (index < 0) index = 0;
    auto m = unit_members();
    GameBattler* member = index < (int)m.size() ? m[(size_t)index] : nullptr;
    if (member && member->is_alive()) return member;
    auto alive = alive_members();
    return alive.empty() ? nullptr : alive[0];
}

GameBattler* GameUnit::smooth_dead_target(int index) {
    if (index < 0) index = 0;
    auto m = unit_members();
    GameBattler* member = index < (int)m.size() ? m[(size_t)index] : nullptr;
    if (member && member->is_dead()) return member;
    auto dead = dead_members();
    return dead.empty() ? nullptr : dead[0];
}

GameBattler* GameUnit::substitute_battler() {
    for (GameBattler* m : unit_members()) {
        if (m->is_substitute()) return m;
    }
    return nullptr;
}

void GameUnit::select(GameBattler* active) {
    for (GameBattler* m : unit_members()) m->selected = (m == active);
}

void GameUnit::request_motion_refresh() {
    for (GameBattler* m : unit_members()) {
        if (m) m->request_motion_refresh();
    }
}

int GameUnit::lowest_index_member() {
    std::vector<int> idx;
    for (GameBattler* m : alive_members()) idx.push_back(m->index());
    std::sort(idx.begin(), idx.end(), [](int a, int b) { return std::to_string(a) < std::to_string(b); });
    return idx.empty() ? -1 : idx[0];
}

GameActor* GameActors::actor(int id) {
    if (!data::actors()[id].is_obj()) return nullptr;
    auto it = data.find(id);
    if (it == data.end()) it = data.emplace(id, std::unique_ptr<GameActor>(new GameActor(id))).first;
    return it->second.get();
}

GameParty::GameParty() {
    init_all_items();
    quest_list = js::make_array();
    quest_stand_by_message = js::Val::null();
    album_pictures = js::make_object();
    album_picture_positions = js::make_object();
    init_world_items_container();
}

void GameParty::init_all_items() {
    active_world = -1;
    items_.clear();
    weapons_.clear();
    armors_.clear();
}

ItemCounts& GameParty::items_c() { return active_world >= 0 ? world_items[active_world].items : items_; }
ItemCounts& GameParty::weapons_c() { return active_world >= 0 ? world_items[active_world].weapons : weapons_; }
ItemCounts& GameParty::armors_c() { return active_world >= 0 ? world_items[active_world].armors : armors_; }
const ItemCounts& GameParty::items_c() const { return active_world >= 0 ? world_items.at(active_world).items : items_; }
const ItemCounts& GameParty::weapons_c() const { return active_world >= 0 ? world_items.at(active_world).weapons : weapons_; }
const ItemCounts& GameParty::armors_c() const { return active_world >= 0 ? world_items.at(active_world).armors : armors_; }

std::vector<GameBattler*> GameParty::unit_members() {
    std::vector<GameBattler*> out;
    for (GameActor* a : members()) out.push_back(a);
    return out;
}

std::vector<GameBattler*> GameParty::unit_all_members() { return unit_members(); }

std::vector<GameActor*> GameParty::members() { return in_battle() ? battle_members() : all_members(); }

std::vector<GameActor*> GameParty::all_members() {
    std::vector<GameActor*> out;
    bool faraway = is_in_faraway();
    for (int id : actors) {
        GameActor* a = game_actors().actor(id);
        if (faraway && a && a->faraway_actor_id) a = game_actors().actor(a->faraway_actor_id);
        out.push_back(a);
    }
    return out;
}

std::vector<GameActor*> GameParty::battle_members() {
    std::vector<GameActor*> all = all_members();
    std::vector<GameActor*> out;
    for (size_t i = 0; i < all.size() && i < (size_t)max_battle_members(); i++) {
        if (all[i] && all[i]->is_appeared()) out.push_back(all[i]);
    }
    return out;
}

GameActor* GameParty::leader() { return battle_member(0); }

GameActor* GameParty::first_member() {
    if (in_battle()) return battle_member(0);
    if (actors.empty()) return nullptr;
    GameActor* a = game_actors().actor(actors[0]);
    if (is_in_faraway() && a && a->faraway_actor_id) a = game_actors().actor(a->faraway_actor_id);
    return a;
}

GameActor* GameParty::battle_member(int index) {
    if (index < 0) return nullptr;
    bool faraway = is_in_faraway();
    int found = 0;
    size_t limit = std::min(actors.size(), (size_t)max_battle_members());
    for (size_t i = 0; i < limit; i++) {
        GameActor* a = game_actors().actor(actors[i]);
        if (faraway && a && a->faraway_actor_id) a = game_actors().actor(a->faraway_actor_id);
        if (a && a->is_appeared()) {
            if (found == index) return a;
            found++;
        }
    }
    return nullptr;
}

void GameParty::revive_battle_members() {
    for (GameActor* a : battle_members()) {
        if (a->is_dead()) a->set_hp(1);
    }
}

static std::vector<ItemRef> list_of(const ItemCounts& c, ItemRef (*mk)(int)) {
    std::vector<ItemRef> out;
    for (auto& kv : c) out.push_back(mk(kv.first));
    return out;
}

std::vector<ItemRef> GameParty::items() const { return list_of(items_c(), &ItemRef::item); }
std::vector<ItemRef> GameParty::weapons() const { return list_of(weapons_c(), &ItemRef::weapon); }
std::vector<ItemRef> GameParty::armors() const { return list_of(armors_c(), &ItemRef::armor); }

std::vector<ItemRef> GameParty::equip_items() const {
    std::vector<ItemRef> out = weapons();
    for (auto& a : armors()) out.push_back(a);
    return out;
}

std::vector<ItemRef> GameParty::all_items() const {
    std::vector<ItemRef> out = items();
    for (auto& a : equip_items()) out.push_back(a);
    return out;
}

ItemCounts* GameParty::item_container(const ItemRef& item) {
    if (!item.valid()) return nullptr;
    if (item.is_item()) return &items_c();
    if (item.is_weapon()) return &weapons_c();
    if (item.is_armor()) return &armors_c();
    return nullptr;
}

const ItemCounts* GameParty::item_container(const ItemRef& item) const {
    if (!item.valid()) return nullptr;
    if (item.is_item()) return &items_c();
    if (item.is_weapon()) return &weapons_c();
    if (item.is_armor()) return &armors_c();
    return nullptr;
}

void GameParty::setup_starting_members() {
    actors.clear();
    const Value& pm = data::system()["partyMembers"];
    for (size_t i = 0; i < pm.size(); i++) {
        int id = pm[i].as_int();
        if (game_actors().actor(id)) actors.push_back(id);
    }
}

std::string GameParty::name() {
    auto b = battle_members();
    if (b.empty()) return "";
    if (b.size() == 1) return leader()->name();
    return js_format(data::system()["terms"]["messages"]["partyName"].string(), {leader()->name()});
}

int GameParty::highest_level() {
    auto m = members();
    if (m.empty()) return (int)-INFINITY;
    int v = m[0]->level;
    for (GameActor* a : m) v = std::max(v, a->level);
    return v;
}

int GameParty::lowest_level() {
    auto m = members();
    if (m.empty()) return (int)INFINITY;
    int v = m[0]->level;
    for (GameActor* a : m) v = std::min(v, a->level);
    return v;
}

double GameParty::average_level() {
    auto m = members();
    double level = 0;
    for (GameActor* a : m) {
        if (a) level += a->level;
    }
    return level / m.size();
}

void GameParty::add_actor(int id) {
    if (!contains_int(actors, id)) {
        actors.push_back(id);
        game_player().refresh();
        game_map().request_refresh();
    }
}

void GameParty::remove_actor(int id) {
    auto it = std::find(actors.begin(), actors.end(), id);
    if (it != actors.end()) {
        actors.erase(it);
        game_player().refresh();
        game_map().request_refresh();
    }
}

double GameParty::max_gold() const { return yanfly_eval_num("MaxGold", 99999999); }

void GameParty::gain_gold(double amount) { gold_ = std::min(std::max(gold_ + amount, 0.0), max_gold()); }

double GameParty::num_items(const ItemRef& item) const {
    const ItemCounts* c = item_container(item);
    if (!c) return 0;
    auto it = c->find(item.id);
    return it == c->end() ? 0 : it->second;
}

double GameParty::max_items(const ItemRef& item) const {
    if (!item.valid()) return 1;
    return item.data()["maxItem"].num();
}

bool GameParty::has_item(const ItemRef& item, bool include_equip) {
    if (num_items(item) > 0) return true;
    if (include_equip && is_any_member_equipped(item)) return true;
    return false;
}

bool GameParty::is_any_member_equipped(const ItemRef& item) {
    for (GameActor* a : members()) {
        for (auto& e : a->equips()) {
            if (e.valid() == item.valid() && e == item) return true;
        }
    }
    return false;
}

void GameParty::gain_item(const ItemRef& item, double amount, bool include_equip) {
    if (item.valid()) {
        const Value& d = item.data();
        bool has_etype = d.find("etypeId") != nullptr;
        bool has_atype = d.find("atypeId") != nullptr;
        last_item_data.type = 0;
        last_item_data.id = item.id;
        last_item_data.amount = amount;
        if (!has_atype && has_etype) last_item_data.type = 1;
        else if (has_atype && has_etype) last_item_data.type = 2;
    }
    ItemCounts* container = item_container(item);
    if (container) {
        double last = num_items(item);
        double nn = last + amount;
        double v = std::min(std::max(nn, 0.0), max_items(item));
        if (v == 0) container->erase(item.id);
        else (*container)[item.id] = v;
        if (include_equip && nn < 0) discard_members_equip(item, (int)-nn);
        game_map().request_refresh();
    }
    if (item.valid() && amount > 0) {
        const char* type = item.is_item() ? "item" : item.is_weapon() ? "weapon" : item.is_armor() ? "armor" : "";
        game_system().add_to_gacha_book(type, item.id);
    }
    if (item.valid() && item.id == 13 && num_items(item) >= 99) game_system().unlock_achievement("SWEETHEART_I_MEAN_TOFU");
}

void GameParty::discard_members_equip(const ItemRef& item, int amount) {
    int n = amount;
    for (GameActor* a : members()) {
        while (n > 0 && a->is_equipped(item)) {
            a->discard_equip(item);
            n--;
        }
    }
}

void GameParty::consume_item(const ItemRef& item) {
    if (item.is_item() && item.valid() && item.data()["consumable"].truthy()) lose_item(item, 1);
}

bool GameParty::can_use(const ItemRef& item) {
    for (GameActor* a : members()) {
        if (a->can_use(item)) return true;
    }
    return false;
}

bool GameParty::can_input() {
    for (GameActor* a : members()) {
        if (a->can_input()) return true;
    }
    return false;
}

bool GameParty::is_all_dead() {
    if (GameUnit::is_all_dead()) return in_battle() || !is_empty();
    return false;
}

void GameParty::on_player_walk() {
    auto group = members();
    for (GameActor* a : group) {
        if (a) a->on_player_walk();
    }
}

GameActor* GameParty::menu_actor() {
    GameActor* a = game_actors().actor(menu_actor_id);
    auto m = members();
    if (std::find(m.begin(), m.end(), a) == m.end()) a = m.empty() ? nullptr : m[0];
    return a;
}

void GameParty::set_menu_actor(GameActor* a) {
    if (a) menu_actor_id = a->actor_id();
}

void GameParty::make_menu_actor_next() {
    auto m = members();
    if (m.empty()) return;
    auto it = std::find(m.begin(), m.end(), menu_actor());
    if (it != m.end()) set_menu_actor(m[(size_t)((it - m.begin()) + 1) % m.size()]);
    else set_menu_actor(m[0]);
}

void GameParty::make_menu_actor_previous() {
    auto m = members();
    if (m.empty()) return;
    auto it = std::find(m.begin(), m.end(), menu_actor());
    if (it != m.end()) set_menu_actor(m[(size_t)((it - m.begin()) + m.size() - 1) % m.size()]);
    else set_menu_actor(m[0]);
}

GameActor* GameParty::target_actor() {
    GameActor* a = game_actors().actor(target_actor_id);
    auto m = members();
    if (std::find(m.begin(), m.end(), a) == m.end()) a = m.empty() ? nullptr : m[0];
    return a;
}

void GameParty::set_target_actor(GameActor* a) {
    if (a) target_actor_id = a->actor_id();
}

void GameParty::swap_order(int i1, int i2) {
    if (i1 < 0 || i2 < 0) return;
    size_t n = (size_t)std::max(i1, i2) + 1;
    if (actors.size() < n) return;
    std::swap(actors[(size_t)i1], actors[(size_t)i2]);
    game_player().refresh();
}

bool GameParty::party_ability(int id) {
    for (GameActor* a : battle_members()) {
        if (a->party_ability(id)) return true;
    }
    return false;
}

double GameParty::rate_preemptive(double troop_agi) {
    double rate = agility() >= troop_agi ? 0.05 : 0.03;
    if (has_raise_preemptive()) rate *= 4;
    return rate;
}

double GameParty::rate_surprise(double troop_agi) {
    double rate = agility() >= troop_agi ? 0.03 : 0.05;
    if (has_cancel_surprise()) rate = 0;
    return rate;
}

void GameParty::remove_battle_states() {
    for (GameActor* a : members()) a->remove_battle_states();
}

static bool hidden_in_menu(const Value& item) { return item["meta"]["HideInMenu"].truthy(); }

bool GameParty::has_key_items(bool hidden) {
    for (auto& r : items()) {
        const Value& it = r.data();
        if (!hidden && hidden_in_menu(it)) continue;
        if (is_key_item(it)) return true;
    }
    return false;
}

bool GameParty::has_consumable_items(bool hidden) {
    for (auto& r : items()) {
        const Value& it = r.data();
        if (!hidden && hidden_in_menu(it)) continue;
        if (is_consumable_item(it)) return true;
    }
    return false;
}

bool GameParty::has_toy_items(bool hidden) {
    for (auto& r : items()) {
        const Value& it = r.data();
        if (!hidden && hidden_in_menu(it)) continue;
        if (is_toy_item(it)) return true;
    }
    return false;
}

bool GameParty::has_valid_pocket_items() {
    for (auto& r : items()) {
        const Value& it = r.data();
        if (hidden_in_menu(it)) continue;
        if (is_key_item(it) || is_consumable_item(it) || is_toy_item(it)) return true;
    }
    return false;
}

bool GameParty::is_in_faraway() const { return game_switches().value(7); }

void GameParty::init_world_items_container() {
    world_items.clear();
    last_world_item_index = current_world_index();
    for (int i = 0; i < 5; i++) world_items[i] = WorldItems();
}

void GameParty::change_world_item_container(int world) {
    WorldItems& last = world_items[last_world_item_index];
    WorldItems& next = world_items[world];
    if (active_world != last_world_item_index) {
        for (auto& kv : items_c()) last.items[kv.first] = kv.second;
        for (auto& kv : weapons_c()) last.weapons[kv.first] = kv.second;
        for (auto& kv : armors_c()) last.armors[kv.first] = kv.second;
    }
    (void)next;
    active_world = world;
    last_world_item_index = world;
    struct C {
        ItemCounts* c;
        const Value* d;
    } cs[3] = {{&items_c(), &data::items()}, {&weapons_c(), &data::weapons()}, {&armors_c(), &data::armors()}};
    for (auto& c : cs) {
        for (auto it = c.c->begin(); it != c.c->end();) {
            const Value& item = (*c.d)[it->first];
            std::string wi = actor_meta(item, "WorldIndex");
            if (item.is_obj() && !wi.empty() && js_number(wi) != world) it = c.c->erase(it);
            else ++it;
        }
    }
}

ItemRef GameParty::last_gained_item() const {
    switch (last_item_data.type) {
    case 0: return ItemRef::item(last_item_data.id);
    case 1: return ItemRef::weapon(last_item_data.id);
    case 2: return ItemRef::armor(last_item_data.id);
    default: return ItemRef();
    }
}

void GameParty::sort_members_for_tag(int leader_id) {
    if (game_variables().number(22) == 2) {
        actors = {8, 10, 9, 11};
        auto it = std::find(actors.begin(), actors.end(), leader_id);
        if (it != actors.end()) {
            actors.erase(it);
            actors.insert(actors.begin(), leader_id);
        }
    } else {
        std::vector<int> leader;
        auto it = std::find(actors.begin(), actors.end(), leader_id);
        if (it != actors.end()) {
            leader.push_back(*it);
            actors.erase(it);
        } else if (!actors.empty()) {
            leader.push_back(actors.back());
            actors.pop_back();
        }
        std::sort(actors.begin(), actors.end());
        leader.insert(leader.end(), actors.begin(), actors.end());
        actors = leader;
    }
    game_player().refresh();
}

void GameParty::add_scanned_enemy(int id) {
    if (!contains_int(scanned_enemy_list, id)) scanned_enemy_list.push_back(id);
}

GameActor* GameParty::member_at_status_index(int index) {
    for (GameActor* a : battle_members()) {
        if (a->battle_status_index() == index) return a;
    }
    return nullptr;
}

bool GameParty::actor_is_affected_by_state(int actor_id, int state_id) {
    for (GameActor* a : members()) {
        if (a && a->actor_id() == actor_id && a->is_state_affected(state_id)) return true;
    }
    return false;
}

GameActor* GameParty::get_omori() {
    for (GameActor* a : battle_members()) {
        if (a->actor_id() == 1 || a->actor_id() == 8) return a;
    }
    return nullptr;
}

js::Val GameParty::add_quest(const std::string& id, const js::Val& extra) {
    if (has_quest(id)) return js::Val::boolean(false);
    js::Val quest = js::make_object();
    put(quest, "id", js::Val::str(id));
    put(quest, "messageIndex", js::Val::num(0));
    const Value& q = data::quests()["quests"][id.c_str()];
    const Value* w = q.find("world");
    put(quest, "world", w ? js::value_to_js(*w) : js::Val());
    put(quest, "complete", js::Val::boolean(false));
    if (extra.is_obj()) {
        std::vector<uint32_t> keys;
        js::ordered_keys(extra.o, keys);
        for (uint32_t k : keys) V().put(quest, k, V().get(extra, k));
    }
    quest_list.o->items.push_back(quest);
    return quest;
}

void GameParty::remove_quest(const std::string& id) {
    auto& items = quest_list.o->items;
    items.erase(std::remove_if(items.begin(), items.end(), [&](const js::Val& q) { return js::strict_equals(get(q, "id"), js::Val::str(id)); }),
                items.end());
}

bool GameParty::has_quest(const std::string& id) const {
    for (auto& q : quest_list.o->items) {
        if (js::strict_equals(get(q, "id"), js::Val::str(id))) return true;
    }
    return false;
}

std::vector<js::Val> GameParty::completed_quest_list(int world) const {
    std::vector<js::Val> out;
    for (auto& q : quest_list.o->items) {
        if (get(q, "complete").truthy() && js::strict_equals(get(q, "world"), js::Val::num(world))) out.push_back(q);
    }
    return out;
}

std::vector<js::Val> GameParty::incomplete_quest_list(int world) const {
    std::vector<js::Val> out;
    for (auto& q : quest_list.o->items) {
        if (!get(q, "complete").truthy() && js::strict_equals(get(q, "world"), js::Val::num(world))) out.push_back(q);
    }
    return out;
}

void GameParty::set_quest_complete_state(const std::string& id, bool state) {
    for (auto& q : quest_list.o->items) {
        if (js::strict_equals(get(q, "id"), js::Val::str(id))) put(q, "complete", js::Val::boolean(state));
    }
}

void GameParty::set_quest_message_index(const std::string& id, const js::Val& index) {
    for (auto& q : quest_list.o->items) {
        if (js::strict_equals(get(q, "id"), js::Val::str(id))) {
            put(q, "messageIndex", index);
            return;
        }
    }
}

void GameParty::create_picture_album(const std::string& album) {
    if (get(album_pictures, album.c_str()).is_undef()) put(album_pictures, album.c_str(), js::make_array());
}

void GameParty::randomize_album_picture_positions(const std::string& album, const js::Val& pictures, bool force) {
    if (!get(album_picture_positions, album.c_str()).is_undef() && !force) return;
    std::vector<js::Val> list = pictures.is_array() ? pictures.o->items : std::vector<js::Val>();
    for (size_t i = list.size(); i-- > 1;) {
        size_t r = (size_t)std::floor(rng::next() * (double)(i + 1));
        std::swap(list[i], list[r]);
    }
    put(album_picture_positions, album.c_str(), js::make_array(list));
}

int GameParty::album_size(const std::string& album) {
    create_picture_album(album);
    js::Val a = get(album_pictures, album.c_str());
    int n = 0;
    for (auto& v : a.o->items) n += v.truthy() ? 1 : 0;
    return n;
}

void GameParty::add_picture_to_album(const std::string& album, int index, const js::Val& id) {
    create_picture_album(album);
    js::Val a = get(album_pictures, album.c_str());
    V().put(a, js::Val::num(index), id);
}

void GameParty::remove_picture_to_album(const std::string& album, int index) {
    create_picture_album(album);
    js::Val a = get(album_pictures, album.c_str());
    V().put(a, js::Val::num(index), js::Val());
}

js::Val GameParty::get_album_picture_at_index(const std::string& album, int index) const {
    js::Val a = get(album_pictures, album.c_str());
    if (a.is_undef()) return js::Val();
    return V().get(a, js::Val::num(index));
}

js::Val GameParty::album_has_picture(const std::string& album, const js::Val& index) const {
    js::Val a = get(album_pictures, album.c_str());
    if (a.is_undef()) return js::Val();
    for (auto& v : a.o->items) {
        if (js::strict_equals(v, index)) return js::Val::boolean(true);
    }
    return js::Val::boolean(false);
}

void GameParty::add_defeated_enemy(int id) {
    if (!contains_int(defeated_enemies, id)) defeated_enemies.push_back(id);
    const Value& info = data::lang_entry("Bestiary.Information");
    bool all = info.is_obj();
    for (const Member& m : info.members()) {
        if (!contains_int(defeated_enemies, (int)js_number(m.key))) {
            all = false;
            break;
        }
    }
    if (all) game_system().unlock_achievement("FOES_FILED");
}

std::string GameParty::last_black_letter() const {
    if (black_letters.empty()) return " ";
    const Value& item = data::items()[black_letters.back()];
    return to_upper(trim(actor_meta(item, "Blackletter")));
}

ItemRef GameParty::last_black_letter_item() const {
    if (black_letters.empty()) return ItemRef();
    return ItemRef::item(black_letters.back());
}

}

namespace rpg {

static std::vector<GameBattler*> base_alive_members(GameUnit& unit) {
    std::vector<GameBattler*> out;
    for (GameBattler* m : unit.unit_members()) {
        if (m->is_alive()) out.push_back(m);
    }
    return out;
}

std::vector<GameBattler*> GameUnit::alive_members() {
    if (in_battle_ && game_temp().taunt) return taunt_members();
    return base_alive_members(*this);
}

std::vector<GameBattler*> GameUnit::physical_taunt_members() {
    std::vector<GameBattler*> out;
    for (GameBattler* m : unit_members()) {
        if (m->is_alive() && m->taunt_physical()) out.push_back(m);
    }
    return out;
}

std::vector<GameBattler*> GameUnit::magical_taunt_members() {
    std::vector<GameBattler*> out;
    for (GameBattler* m : unit_members()) {
        if (m->is_alive() && m->taunt_magical()) out.push_back(m);
    }
    return out;
}

std::vector<GameBattler*> GameUnit::certain_taunt_members() {
    std::vector<GameBattler*> out;
    for (GameBattler* m : unit_members()) {
        if (m->is_alive() && m->taunt_certain()) out.push_back(m);
    }
    return out;
}

std::vector<GameBattler*> GameUnit::taunt_members() {
    GameAction* action = game_temp().taunt_action;
    std::shared_ptr<GameAction> inputting;
    if (!action) {
        inputting = battle::inputting_action();
        action = inputting.get();
    }
    if (!action) return base_alive_members(*this);
    if (!action->is_tauntable()) return base_alive_members(*this);
    if (action->is_physical()) {
        auto v = physical_taunt_members();
        if (!v.empty()) return v;
    }
    if (action->is_magical()) {
        auto v = magical_taunt_members();
        if (!v.empty()) return v;
    }
    if (action->is_certain_hit()) {
        auto v = certain_taunt_members();
        if (!v.empty()) return v;
    }
    return base_alive_members(*this);
}

void GameParty::on_battle_start() {
    for (GameBattler* m : unit_members()) m->on_battle_start();
    in_battle_ = true;
    process_state_eval("battle");
}

void GameParty::on_battle_end() {
    in_battle_ = false;
    for (GameBattler* m : unit_members()) m->on_battle_end();
}

void GameParty::make_actions() {
    for (GameBattler* m : unit_members()) m->make_actions();
}

void GameParty::create_actions() {
    auto m = unit_members();
    for (size_t i = 0; i < m.size(); ++i) {
        if (m[i]) m[i]->create_actions();
    }
}

void GameParty::on_turn_start() {
    auto m = unit_members();
    for (size_t i = 0; i < m.size(); ++i) {
        if (m[i]) {
            m[i]->on_turn_start();
            m[i]->refresh();
        }
    }
}

void GameParty::update_tick() {
    for (GameBattler* m : unit_members()) {
        if (m) m->update_tick();
    }
}

void GameParty::perform_victory() {
    for (GameActor* a : members()) a->perform_victory();
}

void GameParty::perform_escape() {
    for (GameActor* a : members()) a->perform_escape();
}

void GameParty::perform_escape_success() {
    auto m = members();
    for (size_t i = 0; i < m.size(); ++i) {
        if (m[i]) m[i]->perform_escape_success();
    }
}

void GameParty::request_motion_refresh_all() {
    for (GameActor* a : members()) a->request_motion_refresh();
    game_troop().request_motion_refresh();
}

double GameParty::battle_num_items(const ItemRef& item) {
    const ItemCounts* container = item_container(item);
    double total = 0;
    if (container) {
        auto it = container->find(item.id);
        if (it != container->end()) total = it->second;
    }
    GameActor* actor = battle::actor();
    std::shared_ptr<GameAction> current = actor ? actor->current_action() : nullptr;
    for (GameActor* member : all_members()) {
        for (auto& action : member->actions) {
            if (action == current) continue;
            if (action->is_item() && action->item_.item_id == item.id) total--;
        }
    }
    return std::max(0.0, total);
}

}
