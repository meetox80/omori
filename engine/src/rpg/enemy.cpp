#include "enemy.h"

#include <algorithm>
#include <cmath>

#include "../core/regex.h"
#include "../js/compile.h"
#include "../js/dataview.h"
#include "../js/random.h"
#include "action.h"
#include "audio_manager.h"
#include "battle.h"
#include "data.h"
#include "evalctx.h"
#include "game.h"
#include "interpreter.h"
#include "params.h"
#include "party.h"

namespace rpg {

namespace {

bool contains_int(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

js::Val jsv(GameBattler* b) { return b ? b->to_js() : js::Val(); }

bool run_condition(const std::string& code, JsEval& e, const char* where, bool* threw = nullptr) {
    auto prog = js::compile_cached(code);
    if (!prog) {
        script_error(where, "could not compile: " + code);
        if (threw) *threw = true;
        return false;
    }
    bool ok = e.run(prog.get(), where);
    if (threw) *threw = !ok;
    return ok && e.completion().truthy();
}

int ref_id(const char* table, const std::string& name) { return data::root()["yanfly"][table][to_upper(name).c_str()].as_int(); }

}

GameEnemy::GameEnemy(int enemy_id, double x, double y) {
    GameEnemy::init_members();
    horror::clear(horror_filters);
    int loops = 100;
    while (data::enemies()[enemy_id]["swapEnemies"].size() > 0) {
        const Value& pool = data::enemies()[enemy_id]["swapEnemies"];
        enemy_id = pool[(size_t)std::floor(rng::next() * pool.size())].as_int();
        if (--loops <= 0) break;
    }
    setup(enemy_id, x, y);
}

void GameEnemy::init_members() {
    GameBattler::init_members();
    enemy_id_ = 0;
    letter.clear();
    plural = false;
    screen_x_ = 0;
    screen_y_ = 0;
}

void GameEnemy::setup(int enemy_id, double x, double y) {
    enemy_id_ = enemy_id;
    screen_x_ = x;
    screen_y_ = y;
    recover_all();
    create_stealable_items();
    troop_id = game_troop().troop()["id"].as_int();
    is_state_transforming = false;
    apply_battle_position_offsets();
}

GameUnit& GameEnemy::friends_unit() const { return game_troop(); }
GameUnit& GameEnemy::opponents_unit() const { return game_party(); }

int GameEnemy::index() const {
    auto m = game_troop().members();
    auto it = std::find(m.begin(), m.end(), this);
    return it == m.end() ? -1 : (int)(it - m.begin());
}

const Value& GameEnemy::enemy() const { return data::enemies()[enemy_id_]; }

std::vector<const Value*> GameEnemy::trait_objects() {
    std::vector<const Value*> objs = GameBattlerBase::trait_objects();
    objs.push_back(&enemy());
    return objs;
}

double GameEnemy::param_base(int id) const { return enemy()["params"][id].num(); }

double GameEnemy::gold() {
    double g = enemy()["gold"].num();
    for (auto& st : stealable_items()) {
        if (st.type != "gold") continue;
        if (!st.is_stolen) continue;
        if (!st.is_drop) continue;
        st.is_drop = false;
        g = 0;
    }
    return g;
}

double GameEnemy::drop_item_rate() const { return game_party().has_drop_item_double() ? 2 : 1; }

ItemRef GameEnemy::item_object(int kind, int id) const {
    if (kind == 1) return ItemRef::item(id);
    if (kind == 2) return ItemRef::weapon(id);
    if (kind == 3) return ItemRef::armor(id);
    return ItemRef();
}

std::vector<ItemRef> GameEnemy::base_make_drop_items() {
    std::vector<ItemRef> r;
    const Value& di = enemy()["dropItems"];
    for (size_t i = 0; i < di.size(); i++) {
        int kind = di[i]["kind"].as_int();
        if (kind > 0 && rng::next() * di[i]["denominator"].num() < drop_item_rate()) r.push_back(item_object(kind, di[i]["dataId"].as_int()));
    }
    return r;
}

std::vector<ItemRef> GameEnemy::make_drop_items() {
    std::vector<ItemRef> drops = base_make_drop_items();
    for (auto& st : stealable_items()) {
        if (st.type == "gold") continue;
        if (!st.is_stolen || !st.is_drop) continue;
        st.is_drop = false;
        ItemRef item = st.type == "item" ? ItemRef::item(st.id) : st.type == "weapons" ? ItemRef::weapon(st.id) : ItemRef::armor(st.id);
        auto it = std::find(drops.begin(), drops.end(), item);
        if (it != drops.end()) drops.erase(it);
        else if (!drops.empty()) drops.pop_back();
    }
    for (auto& d : make_conditional_drop_items()) drops.push_back(d);
    return drops;
}

std::string GameEnemy::battler_name() const {
    const Value& sv = enemy()["_sideviewBattler"];
    if (sv.is_obj() && sv["default"].t == Value::Bool && !sv["default"].b && !sv["filename"].string().empty()) return sv["filename"].string();
    return enemy()["battlerName"].string();
}

void GameEnemy::perform_action_start(GameAction* action) {
    GameBattler::perform_action_start(action);
    if (!sprite_can_move()) request_effect("whiten");
}

void GameEnemy::perform_action(GameAction* action) {
    if (!has_sv_battler()) return;
    if (action->is_attack()) perform_attack();
    else if (action->is_guard()) request_motion("guard");
    else if (action->is_magic_skill()) request_motion("spell");
    else if (action->is_skill()) request_motion("skill");
    else if (action->is_item()) request_motion("item");
}

void GameEnemy::perform_damage() {
    if (!has_sv_battler()) {
        request_effect("blink");
        audio_manager::play_system_sound(10);
        return;
    }
    if (is_sprite_visible()) request_motion(damage_motion());
    else game_screen().start_shake(5, 5, 10);
}

void GameEnemy::perform_collapse() {
    switch (collapse_type()) {
    case 0:
        request_effect("collapse");
        audio_manager::play_system_sound(11);
        break;
    case 1:
        request_effect("bossCollapse");
        audio_manager::play_system_sound(12);
        break;
    case 2: request_effect("instantCollapse"); break;
    }
}

void GameEnemy::transform(int id) {
    clear_sv_attributes();
    std::string name = original_name();
    enemy_id_ = id;
    if (original_name() != name) {
        letter.clear();
        plural = false;
    }
    refresh();
    if (num_actions() > 0) make_actions();
    if (sprite_link) sprite_link->link_transform(this);
}

bool GameEnemy::meets_turn_condition(double p1, double p2) {
    double n = turn_count();
    if (p2 == 0) return n == p1;
    return n > 0 && n >= p1 && std::fmod(n, p2) == std::fmod(p1, p2);
}

bool GameEnemy::meets_condition(const Value& action) {
    double p1 = action["conditionParam1"].num(), p2 = action["conditionParam2"].num();
    switch (action["conditionType"].as_int()) {
    case 1: return meets_turn_condition(p1, p2);
    case 2: return hp_rate() >= p1 && hp_rate() <= p2;
    case 3: return mp_rate() >= p1 && mp_rate() <= p2;
    case 4: return is_state_affected((int)p1);
    case 5: return game_party().highest_level() >= p1;
    case 6: return game_switches().value((int)p1);
    default: return true;
    }
}

bool GameEnemy::is_action_valid(const Value& action) { return meets_condition(action) && can_use(ItemRef::skill(action["skillId"].as_int())); }

const Value* GameEnemy::select_action(const std::vector<const Value*>& list, double rating_zero) {
    double sum = 0;
    for (const Value* a : list) sum += (*a)["rating"].num() - rating_zero;
    if (sum > 0) {
        double value = std::floor(sum * rng::next());
        for (const Value* a : list) {
            value -= (*a)["rating"].num() - rating_zero;
            if (value < 0) return a;
        }
        return nullptr;
    }
    return nullptr;
}

void GameEnemy::select_all_actions(std::vector<const Value*> list) {
    double rating_max = -INFINITY;
    for (const Value* a : list) rating_max = std::max(rating_max, (*a)["rating"].num());
    double rating_zero = rating_max - 3;
    list.erase(std::remove_if(list.begin(), list.end(), [&](const Value* a) { return (*a)["rating"].num() <= rating_zero; }), list.end());
    for (int i = 0; i < num_actions(); i++) action(i)->set_enemy_action(select_action(list, rating_zero));
}

void GameEnemy::base_make_actions() {
    GameBattler::base_make_actions();
    if (num_actions() > 0) {
        std::vector<const Value*> list;
        const Value& acts = enemy()["actions"];
        for (size_t i = 0; i < acts.size(); i++) {
            if (is_action_valid(acts[i])) list.push_back(&acts[i]);
        }
        if (!list.empty()) select_all_actions(list);
    }
    set_action_state("waiting");
}

void GameEnemy::make_actions() {
    if (enemy()["aiPattern"].size() > 0) {
        set_ai_pattern();
        set_action_state("waiting");
    } else {
        base_make_actions();
    }
}

static std::vector<ItemRef> pattern_skills(std::vector<ItemRef> array, const Value& patterns) {
    for (size_t i = 0; i < patterns.size(); ++i) {
        std::string line = patterns[i].string();
        re::Groups g;
        int skill_id;
        if (re::search("[ ]*(.*):[ ](?:SKILL)[ ](\\d+),[ ](.*)", "i", line, &g)) skill_id = g.num(2);
        else if (re::search("[ ]*(.*):[ ](?:SKILL)[ ](\\d+)", "i", line, &g)) skill_id = g.num(2);
        else if (re::search("[ ]*(.*):[ ](.*),[ ](.*)", "i", line, &g)) skill_id = ref_id("SkillIdRef", g[2]);
        else if (re::search("[ ]*(.*):[ ](.*)", "i", line, &g)) skill_id = ref_id("SkillIdRef", g[2]);
        else continue;
        if (data::skills()[skill_id].is_obj()) array.push_back(ItemRef::skill(skill_id));
    }
    return array;
}

std::vector<ItemRef> GameEnemy::skills() const {
    std::vector<ItemRef> s;
    const Value& acts = enemy()["actions"];
    for (size_t i = 0; i < acts.size(); i++) {
        int id = acts[i]["skillId"].as_int();
        if (data::skills()[id].is_obj()) s.push_back(ItemRef::skill(id));
    }
    return pattern_skills(s, enemy()["aiPattern"]);
}

int GameEnemy::attack_animation_id() {
    if (has_sv_battler() && !is_hide_sv_weapon()) {
        if (has_sv_attack_animation) return sv_attack_animation_id;
        setup_sv_attributes();
        return sv_attack_animation_id;
    }
    return base_attack_animation_id();
}

int GameEnemy::reflect_animation_id() {
    if (enemy()["reflectAnimationId"].num() > 0) return enemy()["reflectAnimationId"].as_int();
    return GameBattler::reflect_animation_id();
}

bool GameEnemy::sprite_can_move() {
    if (enemy()["spriteCannotMove"].truthy()) return false;
    return GameBattler::sprite_can_move();
}

void GameEnemy::make_counter_skills() {
    base_make_counter_skills();
    const Value* obj = nullptr;
    if (enemy()["counterSkills"].truthy()) {
        obj = &enemy();
        const Value& cs = (*obj)["counterSkills"];
        for (size_t i = 0; i < cs.size(); i++) {
            if (!contains_int(counter_skills_, cs[i].as_int())) counter_skills_.push_back(cs[i].as_int());
        }
        extend_counter_skills_eval(*obj);
    }
    if (counter_skills_.empty()) counter_skills_.push_back(obj ? (*obj)["defaultCounter"].as_int() : 0);
}

double GameEnemy::counter_total() {
    if (has_counter_total_cache) return counter_total_cache;
    double value = base_counter_total();
    value += enemy()["counterTotal"].num();
    value += get_counter_total_eval(enemy());
    counter_total_cache = value;
    has_counter_total_cache = true;
    return value;
}

double GameEnemy::target_counter_rate() { return GameBattler::target_counter_rate() * enemy()["targetCounterRate"].num(); }
double GameEnemy::target_counter_flat() { return GameBattler::target_counter_flat() + enemy()["targetCounterFlat"].num(); }

double GameEnemy::target_counter_rate_eval(double rate, GameBattler* target, const ItemRef& item) {
    rate = GameBattler::target_counter_rate_eval(rate, target, item);
    return get_target_cnt_rate_eval(&enemy(), rate, target, item);
}

bool GameEnemy::force_evade_counter() { return GameBattler::force_evade_counter() || enemy()["evadeCounter"].truthy(); }
bool GameEnemy::force_hit_counter() { return GameBattler::force_hit_counter() || enemy()["hitCounter"].truthy(); }
bool GameEnemy::ai_consider_taunt() const { return enemy()["aiConsiderTaunt"].truthy(); }

double GameEnemy::max_buff_limit(int id) { return GameBattlerBase::max_buff_limit(id) + enemy()["maxBuff"][id].num(); }
double GameEnemy::max_debuff_limit(int id) { return GameBattlerBase::max_debuff_limit(id) - enemy()["maxDebuff"][id].num(); }

bool GameEnemy::is_damage_capped() {
    if (has_dmg_capped) return dmg_capped_cache;
    if (enemy()["breakDamageCap"].truthy()) {
        has_dmg_capped = true;
        dmg_capped_cache = false;
        return false;
    }
    return GameBattler::is_damage_capped();
}

double GameEnemy::maximum_damage() {
    if (has_maximum_damage) return maximum_damage_cache;
    double value = GameBattler::maximum_damage();
    if (enemy()["damageCap"].truthy()) value = std::max(value, enemy()["damageCap"].num());
    has_maximum_damage = true;
    maximum_damage_cache = value;
    return value;
}

double GameEnemy::maximum_healing() {
    if (has_maximum_healing) return maximum_healing_cache;
    double value = GameBattler::maximum_healing();
    if (enemy()["healCap"].truthy()) value = std::min(value, enemy()["healCap"].num());
    has_maximum_healing = true;
    maximum_healing_cache = value;
    return value;
}

double GameEnemy::critical_multiplier_bonus() { return GameBattler::critical_multiplier_bonus() + enemy()["critMultBonus"].num(); }
double GameEnemy::flat_critical_bonus() { return GameBattler::flat_critical_bonus() + enemy()["flatCritBonus"].num(); }
double GameEnemy::certain_crit_rate_bonus() { return GameBattler::certain_crit_rate_bonus() + enemy()["certainCritRateBonus"].num(); }
double GameEnemy::physical_crit_rate_bonus() { return GameBattler::physical_crit_rate_bonus() + enemy()["physicalCritRateBonus"].num(); }
double GameEnemy::magical_crit_rate_bonus() { return GameBattler::magical_crit_rate_bonus() + enemy()["magicalCritRateBonus"].num(); }

double GameEnemy::get_life_steal_rate(const std::string& type, GameBattler* target) {
    double rate = 1 - GameBattler::get_life_steal_rate(type, target);
    rate *= 1 - enemy()["lifeSteal"][type.c_str()].num();
    if (enemy()["lifeSteal"][(type + "Eval").c_str()].truthy()) {
        if (const Value* ast = obj_ast(enemy(), "lifeSteal." + type + "Eval")) rate *= 1 - life_steal_eval(ast, target, "rate", "LIFE STEAL RATE FORMULA ERROR");
    }
    return 1 - rate;
}

double GameEnemy::get_life_steal_flat(const std::string& type, GameBattler* target) {
    double value = GameBattler::get_life_steal_flat(type, target);
    value += enemy()["lifeSteal"][type.c_str()].num();
    if (enemy()["lifeSteal"][(type + "Eval").c_str()].truthy()) {
        if (const Value* ast = obj_ast(enemy(), "lifeSteal." + type + "Eval")) value += life_steal_eval(ast, target, "flat", "LIFE STEAL FLAT FORMULA ERROR");
    }
    return value;
}

bool GameEnemy::is_life_steal_state(const std::string& type) const {
    if (GameBattler::is_life_steal_state(type)) return true;
    return enemy()["lifeSteal"][type.c_str()].truthy();
}

bool GameEnemy::taunt_physical() {
    if (null_taunt_physical()) return false;
    if (enemy()["physTaunt"].truthy()) return true;
    return GameBattlerBase::taunt_physical();
}
bool GameEnemy::taunt_magical() {
    if (null_taunt_magical()) return false;
    if (enemy()["magicTaunt"].truthy()) return true;
    return GameBattlerBase::taunt_magical();
}
bool GameEnemy::taunt_certain() {
    if (null_taunt_certain()) return false;
    if (enemy()["certainTaunt"].truthy()) return true;
    return GameBattlerBase::taunt_certain();
}
bool GameEnemy::null_taunt_physical() { return enemy()["nullPhysTaunt"].truthy() || GameBattlerBase::null_taunt_physical(); }
bool GameEnemy::null_taunt_magical() { return enemy()["nullMagicTaunt"].truthy() || GameBattlerBase::null_taunt_magical(); }
bool GameEnemy::null_taunt_certain() { return enemy()["nullCertainTaunt"].truthy() || GameBattlerBase::null_taunt_certain(); }
bool GameEnemy::ignore_taunt_physical() { return enemy()["ignorePhysTaunt"].truthy() || GameBattlerBase::ignore_taunt_physical(); }
bool GameEnemy::ignore_taunt_magical() { return enemy()["ignoreMagicTaunt"].truthy() || GameBattlerBase::ignore_taunt_magical(); }
bool GameEnemy::ignore_taunt_certain() { return enemy()["nullCertainTaunt"].truthy() || GameBattlerBase::ignore_taunt_certain(); }

std::vector<int> GameEnemy::passive_states_raw_impl() {
    if (has_passive_raw) return passive_raw;
    std::vector<int> arr = GameBattlerBase::passive_states_raw_impl();
    auto add = [&](const std::vector<int>& v) { arr.insert(arr.end(), v.begin(), v.end()); };
    add(get_passive_state_data(&enemy()));
    for (auto& s : skills()) add(get_passive_state_data(&s.data()));
    std::vector<int> uniq;
    for (int id : arr) {
        if (!contains_int(uniq, id)) uniq.push_back(id);
    }
    passive_raw = uniq;
    has_passive_raw = true;
    return passive_raw;
}

std::vector<int> GameEnemy::aura_state_ids() {
    if (game_temp().is_gathering_aura_data) return {};
    std::vector<int> arr = GameBattlerBase::aura_state_ids();
    GameUnit& friends = friends_unit();
    GameUnit& opponents = opponents_unit();
    game_temp().is_gathering_aura_data = true;
    auto add = [&](const std::vector<int>& v) { arr.insert(arr.end(), v.begin(), v.end()); };
    add(friends.aura_state_type_ids("troop"));
    add(opponents.aura_state_type_ids("troop"));
    if (check_aura_alive()) {
        add(friends.aura_state_type_ids("aliveTroop"));
        add(opponents.aura_state_type_ids("aliveTroop"));
    } else if (check_aura_dead()) {
        add(friends.aura_state_type_ids("deadTroop"));
        add(opponents.aura_state_type_ids("deadTroop"));
    }
    game_temp().is_gathering_aura_data = false;
    return arr;
}

void GameEnemy::create_stealable_items() {
    stealables.clear();
    const Value& list = enemy()["stealableItems"];
    for (size_t i = 0; i < list.size(); i++) {
        StealableItem s;
        s.type = list[i]["type"].string();
        s.id = list[i]["id"].as_int();
        s.rate = data::js_string(list[i]["rate"]);
        s.is_stolen = list[i]["isStolen"].truthy();
        s.is_drop = list[i]["isDrop"].truthy();
        stealables.push_back(s);
    }
    has_stealables = true;
}

std::vector<StealableItem>& GameEnemy::stealable_items() {
    if (!has_stealables) create_stealable_items();
    return stealables;
}

bool GameEnemy::all_items_stolen(const ItemRef& skill) {
    auto& list = stealable_items();
    if (list.empty()) return true;
    const Value& types = skill.data()["stealType"];
    auto has_type = [&](const std::string& t) {
        for (size_t i = 0; i < types.size(); i++) {
            if (types[i].string() == t) return true;
        }
        return false;
    };
    for (auto& st : list) {
        if (!has_type("all") && !has_type(st.type)) continue;
        if (!st.is_stolen) return false;
    }
    return true;
}

static bool drop_condition(GameEnemy* enemy, const std::string& line0) {
    std::string line = line0;
    js::Val self = enemy->to_js();
    JsEval e(self);
    e.set("user", self).set("enemy", self).set("a", self).sv();
    re::Groups g;
    auto check = [&](const std::string& code, const char* where) { return run_condition(code, e, where); };
    if (re::search("EVAL[ ](.*)", "i", line, &g)) return check(g[1], "ENEMY DROP EVAL ERROR");
    if (re::search("ALIVE MEMBERS[ ](.*)", "i", line, &g)) return check("$gameParty.aliveMembers().length " + g[1], "ENEMY DROP ALIVE CONDITION ERROR");
    if (to_upper(line) == "ALWAYS") return true;
    if (re::search("(.*)[ ]COUNT[ ](.*)", "i", line, &g)) {
        std::string l1 = g[1], l2 = g[2];
        ItemRef item;
        re::Groups h;
        if (re::search("ITEM[ ](\\d+)", "i", l1, &h)) item = ItemRef::item(h.num(1));
        else if (re::search("WEAPON[ ](\\d+)", "i", l1, &h)) item = ItemRef::weapon(h.num(1));
        else if (re::search("ARMOR[ ](\\d+)", "i", l1, &h)) item = ItemRef::armor(h.num(1));
        else if (ref_id("ItemIdRef", l1)) item = ItemRef::item(ref_id("ItemIdRef", l1));
        else if (ref_id("WeaponIdRef", l1)) item = ItemRef::weapon(ref_id("WeaponIdRef", l1));
        else if (ref_id("ArmorIdRef", l1)) item = ItemRef::armor(ref_id("ArmorIdRef", l1));
        if (!item.valid()) return false;
        e.num("quantity", game_party().num_items(item));
        return check("quantity " + l2, "ENEMY DROP COUNT ERROR");
    }
    if (re::search("DEAD MEMBERS[ ](.*)", "i", line, &g)) return check("$gameParty.deadMembers().length " + g[1], "ENEMY DROP DEAD MEMBERS ERROR");
    if (re::search("DEATH TURN[ ](.*)", "i", line, &g)) return check("user.deathTurn() " + g[1], "ENEMY DROP DEATH TURN ERROR");
    if (re::search("ENEMY LEVEL[ ](.*)", "i", line, &g)) return false;
    if (re::search("LAST STRIKE[ ](.*)", "i", line, &g)) {
        std::string l = g[1];
        re::Groups h;
        ItemRef last = enemy->last_struck_action();
        if (re::search("SKILL[ ](\\d+)", "i", l, &h)) return last == ItemRef::skill(h.num(1));
        if (re::search("ITEM[ ](\\d+)", "i", l, &h)) return last == ItemRef::item(h.num(1));
        if (ref_id("SkillIdRef", l)) return last == ItemRef::skill(ref_id("SkillIdRef", l));
        if (ref_id("ItemIdRef", l)) return last == ItemRef::item(ref_id("ItemIdRef", l));
        return false;
    }
    if (re::search("PARTY MEMBERS[ ](.*)", "i", line, &g)) return check("$gameParty.battleMembers().length " + g[1], "ENEMY DROP PARTY SIZE ERROR");
    if (re::search("RANDOM[ ](\\d+)([%\xEF\xBC\x85])", "i", line, &g)) return rng::next() < js_parse_float(g[1]) * 0.01;
    if (re::search("TIMES[ ](.*)[ ]STRUCK[ ](.*)", "i", line, &g)) {
        std::string l1 = g[1], l2 = g[2];
        double times = 0;
        re::Groups h;
        if (re::search("SKILL[ ](\\d+)", "i", l1, &h)) times = enemy->times_struck(enemy->struck_skills, h.num(1));
        else if (re::search("SKILL[ ](.*)", "i", l1, &h)) times = enemy->times_struck(enemy->struck_skills, ref_id("SkillIdRef", h[1]));
        else if (re::search("ITEM[ ](\\d+)", "i", l1, &h)) times = enemy->times_struck(enemy->struck_items, h.num(1));
        else if (re::search("ITEM[ ](.*)", "i", l1, &h)) times = enemy->times_struck(enemy->struck_items, ref_id("ItemIdRef", h[1]));
        else if (re::search("STYPE[ ](\\d+)", "i", l1, &h)) times = enemy->times_struck(enemy->struck_stype, h.num(1));
        else if (re::search("STYPE[ ](.*)", "i", l1, &h)) times = enemy->times_struck(enemy->struck_stype, ref_id("STypeIdRef", h[1]));
        else if (re::search("STATE[ ](\\d+)", "i", l1, &h)) times = enemy->times_struck(enemy->struck_states, h.num(1));
        else if (re::search("STATE[ ](.*)", "i", l1, &h)) times = enemy->times_struck(enemy->struck_states, ref_id("StateIdRef", h[1]));
        else if (re::search("ELEMENT[ ](\\d+)", "i", l1, &h)) times = enemy->times_struck(enemy->struck_elements, h.num(1));
        else if (re::search("ELEMENT[ ](.*)", "i", l1, &h)) times = enemy->times_struck(enemy->struck_elements, ref_id("ElementIdRef", h[1]));
        e.num("times", times);
        return check("times " + l2, "ENEMY DROP TIMES STRUCK ERROR");
    }
    if (re::search("SWITCH[ ](\\d+)[ ](.*)", "i", line, &g)) {
        std::string c = to_upper(g[2]);
        bool cond = c == "ON" || c == "TRUE";
        return game_switches().value(g.num(1)) == cond;
    }
    if (re::search("TURN[ ](.*)", "i", line, &g)) return check("$gameTroop.turnCount() " + g[1], "ENEMY DROP TURN ERROR");
    if (re::search("VARIABLE[ ](\\d+)[ ](.*)", "i", line, &g)) {
        e.num("varId", g.num(1));
        return check("$gameVariables.value(varId) " + to_upper(g[2]), "ENEMY DROP VARIABLE ERROR");
    }
    return false;
}

std::vector<ItemRef> GameEnemy::make_conditional_drop_items() {
    std::vector<ItemRef> drops;
    const Value& list = enemy()["conditionalDropItems"];
    for (size_t i = 0; i < list.size(); ++i) {
        const Value& entry = list[i];
        const Value* item = &entry[0];
        const Value& conditions = entry[1];
        double rate = 0;
        for (size_t c = 0; c < conditions.size(); c++) {
            re::Groups g;
            if (re::search("(.*):[ ]([\\+\\-]\\d+)([%\xEF\xBC\x85])", "i", conditions[c].string(), &g)) {
                double value = js_parse_float(g[2]) * 0.01;
                if (drop_condition(this, g[1])) rate += value;
            }
        }
        if (rng::next() < rate) drops.push_back(ItemRef::from_data(item));
    }
    return drops;
}

void GameEnemy::mark_death_turn() {
    has_death_turn = true;
    death_turn_ = self_turn_count;
}

void GameEnemy::mark_struck_state(int id) {
    create_times_struck();
    struck_states[id] += 1;
}

ItemRef GameEnemy::last_struck_action() const {
    if (last_struck_skill) return ItemRef::skill(last_struck_id);
    return ItemRef::item(last_struck_id);
}

void GameEnemy::mark_struck_actions(const ItemRef& item, GameBattler* subject, GameAction* action) {
    (void)action;
    if (!item.valid()) return;
    create_times_struck();
    last_struck_id = item.id;
    last_struck_skill = item.is_skill();
    mark_last_struck_actor(subject);
    if (item.is_skill()) {
        struck_skills[item.id] += 1;
        struck_stype[item.data()["stypeId"].as_int()] += 1;
    }
    if (item.is_item()) struck_items[item.id] += 1;
    mark_struck_elements(item, subject);
}

GameBattler* GameEnemy::killer() {
    if (last_struck_actor > 0) return game_actors().actor(last_struck_actor);
    return this;
}

void GameEnemy::mark_last_struck_actor(GameBattler* subject) {
    if (subject && subject->is_actor()) last_struck_actor = static_cast<GameActor*>(subject)->actor()["id"].as_int();
}

void GameEnemy::mark_struck_elements(const ItemRef& item, GameBattler* subject) {
    std::vector<int> elements;
    int el = item.data()["damage"]["elementId"].as_int();
    if (el < 0) elements = subject->attack_elements();
    else elements = {el};
    for (int id : elements) {
        if (id <= 0) continue;
        struck_elements[id] += 1;
    }
}

double GameEnemy::times_struck(const std::map<int, double>& m, int id) const {
    auto it = m.find(id);
    return it == m.end() ? 0 : it->second;
}

void GameEnemy::clear_sv_attributes() {
    has_sv_weapon = false;
    sv_attack_motion.clear();
    has_sv_attack_animation = false;
    has_sv_battler_name = false;
    sv_battler_name = js::Val();
    sv_idle_motion.clear();
}

static const Value& random_element(const Value& arr) {
    if (!arr.is_arr() || arr.size() == 0) return Value::null_value();
    return arr[(size_t)std::floor(rng::next() * arr.size())];
}

void GameEnemy::setup_sv_attributes() {
    const Value& entry = random_element(enemy()["sideviewWeaponImage"]);
    sv_weapon_image_id = entry[0].as_int();
    has_sv_weapon = true;
    sv_attack_motion = entry[1].string();
    const Value& ani = entry[2];
    has_sv_attack_animation = true;
    sv_attack_animation_id = ani.is_null() && entry.size() < 3 ? base_attack_animation_id() : ani.as_int();
}

js::Val GameEnemy::sv_battler_name_value() {
    if (has_sv_battler_name && sv_battler_name.truthy()) return sv_battler_name;
    const Value& arr = enemy()["sideviewBattler"];
    if (!arr.is_arr() || arr.size() == 0) {
        has_sv_battler_name = true;
        sv_battler_name = js::Val();
        return sv_battler_name;
    }
    sv_battler_name = js::value_to_js(random_element(arr));
    has_sv_battler_name = true;
    return sv_battler_name;
}

bool GameEnemy::has_sv_battler() { return !sv_battler_name_value().is_undef(); }

int GameEnemy::weapon_image_id() {
    if (is_hide_sv_weapon()) return 0;
    if (has_sv_weapon && sv_weapon_image_id) return sv_weapon_image_id;
    setup_sv_attributes();
    return sv_weapon_image_id;
}

std::string GameEnemy::attack_motion() {
    if (weapon_image_id() == 0) return enemy()["sideviewAttackMotion"].string();
    if (!sv_attack_motion.empty()) return sv_attack_motion;
    setup_sv_attributes();
    return sv_attack_motion;
}

std::string GameEnemy::idle_motion() {
    if (!sv_idle_motion.empty()) return sv_idle_motion;
    sv_idle_motion = random_element(enemy()["sideviewIdleMotion"]).string();
    return sv_idle_motion;
}

double GameEnemy::anchor_x() {
    if (has_sv_battler()) return enemy()["sideviewAnchorX"].num();
    return yanfly_param("BECAnchorX").num();
}

double GameEnemy::anchor_y() {
    if (has_sv_battler()) return enemy()["sideviewAnchorY"].num();
    return yanfly_param("BECAnchorY").num();
}

js::Val GameEnemy::sideview_width() const { return js::value_to_js(enemy()["sideviewWidth"]); }
js::Val GameEnemy::sideview_height() const { return js::value_to_js(enemy()["sideviewHeight"]); }

double GameEnemy::sprite_scale_x() const {
    if (enemy()["spriteMirrored"].truthy()) return enemy()["spriteScaleX"].num() * -1;
    return enemy()["spriteScaleX"].num();
}

void GameEnemy::perform_attack() {
    if (!has_sv_battler()) return;
    force_motion(attack_motion());
    start_weapon_animation(weapon_image_id());
}

void GameEnemy::perform_evasion() {
    GameBattler::perform_evasion();
    if (!has_sv_battler()) return;
    request_motion(evade_motion());
}

void GameEnemy::perform_magic_evasion() {
    GameBattler::perform_magic_evasion();
    if (!has_sv_battler()) return;
    request_motion(evade_motion());
}

void GameEnemy::perform_counter() {
    GameBattler::perform_counter();
    if (!has_sv_battler()) return;
    perform_attack();
}

void GameEnemy::perform_escape() {
    if (!has_sv_battler()) return;
    if (!can_move()) return;
    request_motion(escape_motion());
}

bool GameEnemy::is_hide_sv_weapon() {
    for (const Value* s : states()) {
        if ((*s)["hideSVWeapon"].truthy()) return true;
    }
    return false;
}

void GameEnemy::add_state(int id) {
    GameBattler::add_state(id);
    refresh_emotion_state_transform();
}

void GameEnemy::remove_state(int id) {
    GameBattler::remove_state(id);
    refresh_emotion_state_transform();
}

void GameEnemy::on_add_state_extra(int id, bool addable) {
    const Value& exhusband = data::enemies()[402];
    if (id == death_state_id() && name() == exhusband["name"].string() && !immortal_state) {
        std::string base = actor_meta(enemy(), "TransformBaseID");
        if (js_parse_int(base) != enemy_id_) {
            game_screen().set_flash_wait(60);
            double flash[4] = {255, 255, 255, 255};
            game_screen().start_flash(flash, 130);
        }
    }
    if (is_emotional_state(id) && !addable) no_effect_message = js::Val::boolean(true);
}

void GameEnemy::refresh_emotion_state_transform() {
    if (is_dead() || is_state_transforming) return;
    std::string emotion = get_state_emotion();
    std::string base_meta = actor_meta(enemy(), "TransformBaseID");
    int base = base_meta.empty() ? enemy_id_ : (int)js_number(base_meta);
    int tid = enemy_id_;
    if (emotion == "normal") tid = base;
    else if (emotion == "happy") tid = base + 1;
    else if (emotion == "sad") tid = base + 2;
    else if (emotion == "angry") tid = base + 3;
    if (tid != enemy_id_) {
        is_state_transforming = true;
        if (name() == "SPACE EX-HUSBAND") {
            game_screen().set_flash_wait(60);
            double flash[4] = {255, 255, 255, 255};
            game_screen().start_flash(flash, 130);
        }
        transform(tid);
        is_state_transforming = false;
    }
}

int GameEnemy::base_id() const {
    std::string base = actor_meta(enemy(), "TransformBaseID");
    std::string foe = actor_meta(enemy(), "FoeEntry");
    if (!foe.empty()) return (int)js_parse_int(trim(foe));
    return base.empty() ? enemy_id_ : (int)js_number(base);
}

bool GameEnemy::is_scanned() const { return contains_int(game_party().scanned_enemy_list, enemy_id_); }

static double meta_num_or(const Value& e, const char* key, double def) {
    std::string v = actor_meta(e, key);
    return v.empty() ? def : js_number(v);
}

double GameEnemy::battle_status_position() const { return meta_num_or(enemy(), "StatusPosition", 4); }
double GameEnemy::battle_status_cursor_position() const { return meta_num_or(enemy(), "StatusCursorPosition", 3); }
double GameEnemy::battle_status_x_offset() const { return meta_num_or(enemy(), "StatusXOffset", 0); }
double GameEnemy::battle_status_y_offset() const { return meta_num_or(enemy(), "StatusYOffset", 0); }
double GameEnemy::battle_damage_x_offset() const { return meta_num_or(enemy(), "DamageOffsetX", 0); }
double GameEnemy::battle_damage_y_offset() const { return meta_num_or(enemy(), "DamageOffsetY", 0); }

void GameEnemy::apply_battle_position_offsets() {
    const Value& d = data::enemies()[base_id()];
    double ox = 0, oy = 0;
    re::Groups g;
    if (re::search("<(?:Position Offset X):[ ]*([-]?\\d+)>", "i", d["note"].string(), &g)) ox = js_number(g[1]);
    if (re::search("<(?:Position Offset Y):[ ]*([-]?\\d+)>", "i", d["note"].string(), &g)) oy = js_number(g[1]);
    const Value* mx = d["meta"].find("PositionOffsetX");
    const Value* my = d["meta"].find("PositionOffsetY");
    if (mx && mx->is_num()) ox = mx->num();
    if (my && my->is_num()) oy = my->num();
    screen_x_ += ox;
    screen_y_ += oy;
}

void GameEnemy::die() {
    GameBattler::die();
    game_party().add_defeated_enemy(base_id());
}

void GameEnemy::appear() {
    GameBattler::appear();
    game_party().add_defeated_enemy(base_id());
}

void GameEnemy::on_battle_start() {
    GameBattler::on_battle_start();
    if (is_appeared()) game_party().add_defeated_enemy(base_id());
}

js::Val GameEnemy::gauge(int n) const {
    std::string key = "gauge" + std::to_string(n == 3 ? 1 : n);
    for (const Value* s : const_cast<GameEnemy*>(this)->states()) {
        const Value& g = (*s)[key.c_str()];
        if (g.is_str() && g.string() == "UNDEFINED") continue;
        return js::value_to_js(g);
    }
    return js::value_to_js(enemy()[("gauge" + std::to_string(n)).c_str()]);
}

js::Val GameEnemy::gauge_icon(int n) const {
    std::string key = "gaugeIcon" + std::to_string(n);
    for (const Value* s : const_cast<GameEnemy*>(this)->states()) {
        const Value& g = (*s)[key.c_str()];
        if (g.is_str() && g.string() == "UNDEFINED") continue;
        return js::value_to_js(g);
    }
    return js::value_to_js(enemy()[key.c_str()]);
}

namespace ai {

struct State {
    GameBattler* battler = nullptr;
    std::shared_ptr<GameAction> action;
    std::string orig_condition;
    int skill_id = 0;
    std::string target;
    bool has_group = false;
    std::vector<GameBattler*> group;
};

static State g_ai;

static void set_target(int index) { g_ai.action->set_target(index); }

static int param_id(std::string s) {
    s = to_upper(s);
    if (s == "MAXHP" || s == "MAX HP") return 0;
    if (s == "MAXMP" || s == "MAX MP" || s == "MAXSP" || s == "MAX SP") return 1;
    if (s == "ATK" || s == "STR") return 2;
    if (s == "DEF") return 3;
    if (s == "MAT" || s == "INT" || s == "SPI") return 4;
    if (s == "MDF" || s == "RES") return 5;
    if (s == "AGI" || s == "SPD") return 6;
    if (s == "LUK") return 7;
    if (s == "HP") return 8;
    if (s == "MP" || s == "SP") return 9;
    if (s == "HP%") return 10;
    if (s == "MP%" || s == "SP%") return 11;
    if (s == "LEVEL" || s == "LV" || s == "LVL") return 12;
    if (s == "MAXTP") return 13;
    if (s == "TP") return 14;
    return -1;
}

static GameBattler* random_of(const std::vector<GameBattler*>& g) {
    if (g.empty()) return nullptr;
    return g[(size_t)std::floor(rng::next() * g.size())];
}

static double level_of(GameBattler* b) { return b->is_actor() ? static_cast<GameActor*>(b)->level : NAN; }

template <class F>
static void pick(const std::vector<GameBattler*>& group, F better) {
    GameBattler* main = random_of(group);
    for (GameBattler* t : group) {
        if (better(t, main)) main = t;
    }
    set_target(main->index());
}

static std::vector<GameBattler*> common(const std::vector<GameBattler*>& a, const std::vector<GameBattler*>& b) {
    std::vector<GameBattler*> out;
    for (GameBattler* x : a) {
        if (std::find(b.begin(), b.end(), x) != b.end()) out.push_back(x);
    }
    return out;
}

static std::vector<GameBattler*> get_action_group() {
    GameAction* action = g_ai.action.get();
    std::vector<GameBattler*> group;
    if (!action) return {};
    if (action->is_for_user()) {
        group = {g_ai.battler};
    } else if (action->is_for_dead_friend()) {
        group = action->friends_unit().dead_members();
    } else if (action->is_for_friend()) {
        group = action->friends_unit().alive_members();
    } else if (action->is_for_opponent()) {
        if (static_cast<GameEnemy*>(g_ai.battler)->ai_consider_taunt()) {
            game_temp().taunt_mode = true;
            game_temp().taunt_action = action;
            group = action->opponents_unit().taunt_members();
            game_temp().taunt_mode = false;
            game_temp().taunt_action = nullptr;
        } else {
            group = action->opponents_unit().alive_members();
        }
    }
    if (g_ai.has_group) group = common(g_ai.group, group);
    g_ai.has_group = true;
    g_ai.group = group;
    return group;
}

static void set_proper_target(const std::vector<GameBattler*>& group) {
    g_ai.has_group = true;
    g_ai.group = group;
    GameBattler* rnd = random_of(group);
    if (!rnd) {
        set_target(0);
        return;
    }
    std::string line = to_upper(g_ai.target);
    re::Groups g;
    if (re::search("FIRST", "i", line)) {
        set_target(0);
    } else if (re::search("USER", "i", line)) {
        set_target(g_ai.action->subject()->index());
    } else if (re::search("HIGHEST[ ](.*)", "i", line, &g)) {
        int p = param_id(g[1]);
        if (p < 0) return set_target(rnd->index());
        if (p == 8) return pick(group, [](GameBattler* t, GameBattler* m) { return t->hp() > m->hp(); });
        if (p == 9) return pick(group, [](GameBattler* t, GameBattler* m) { return t->mp() > m->mp(); });
        if (p == 10) return pick(group, [](GameBattler* t, GameBattler* m) { return t->hp() / t->mhp() > m->hp() / m->mhp(); });
        if (p == 11) return pick(group, [](GameBattler* t, GameBattler* m) { return t->mp() / t->mmp() > m->mp() / m->mmp(); });
        if (p == 12) return pick(group, [](GameBattler* t, GameBattler* m) { return level_of(t) > level_of(m); });
        if (p == 13) return pick(group, [](GameBattler* t, GameBattler* m) { return level_of(t) > m->max_tp(); });
        if (p == 14) return pick(group, [](GameBattler* t, GameBattler* m) { return level_of(t) > m->tp(); });
        if (p > 15) return set_target(rnd->index());
        pick(group, [p](GameBattler* t, GameBattler* m) { return t->param(p) > m->param(p); });
    } else if (re::search("LOWEST[ ](.*)", "i", line, &g)) {
        int p = param_id(g[1]);
        if (p < 0) return set_target(rnd->index());
        if (p == 8) return pick(group, [](GameBattler* t, GameBattler* m) { return t->hp() < m->hp(); });
        if (p == 9) return pick(group, [](GameBattler* t, GameBattler* m) { return t->mp() < m->mp(); });
        if (p == 10) return pick(group, [](GameBattler* t, GameBattler* m) { return t->hp() / t->mhp() < m->hp() / m->mhp(); });
        if (p == 11) return pick(group, [](GameBattler* t, GameBattler* m) { return t->mp() / t->mmp() < m->mp() / m->mmp(); });
        if (p == 12) return pick(group, [](GameBattler* t, GameBattler* m) { return level_of(t) < level_of(m); });
        if (p == 13) return pick(group, [](GameBattler* t, GameBattler* m) { return level_of(t) < m->max_tp(); });
        if (p == 14) return pick(group, [](GameBattler* t, GameBattler* m) { return level_of(t) < m->tp(); });
        if (p > 15) return set_target(rnd->index());
        pick(group, [p](GameBattler* t, GameBattler* m) { return t->param(p) < m->param(p); });
    } else {
        set_target(random_of(group)->index());
    }
}

static JsEval make_eval() {
    JsEval e{js::Val()};
    e.set("action", g_ai.action->to_js()).set("item", g_ai.action->item().to_js()).set("user", jsv(g_ai.battler)).sv();
    return e;
}

static bool element_rate_match(GameBattler* target, int element_id, const std::string& type) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", target->element_rate(element_id));
    std::string rate_s = buf;
    double rate = js_number(rate_s);
    if (g_ai.battler->is_actor()) {
        if (!game_party().ai_element_rate_known(target, element_id)) return true;
    } else if (!game_troop().ai_element_rate_known(target, element_id)) {
        return true;
    }
    if (type == "NEUTRAL" || type == "NORMAL") return rate >= 0.90 && rate <= 1.10;
    if (type == "WEAK" || type == "WEAKNESS" || type == "VULNERABLE") return rate > 1.00;
    if (type == "RESIST" || type == "RESISTANT" || type == "STRONG") return rate < 1.00;
    if (type == "NULL" || type == "CANCEL" || type == "NO EFFECT") return false;
    if (type == "ABSORB" || type == "HEAL") return rate < 0.00;
    return false;
}

static std::string percent_to_decimal(const std::string& cond) {
    std::string out;
    size_t i = 0;
    while (i < cond.size()) {
        size_t j = i;
        while (j < cond.size() && std::isdigit((unsigned char)cond[j])) j++;
        if (j > i) {
            bool pct = j < cond.size() && cond[j] == '%';
            bool fw = cond.compare(j, 3, "\xEF\xBC\x85") == 0;
            if (pct || fw) {
                out += js::number_to_string(js_parse_int(cond.substr(i, j - i)) * 0.01);
                i = j + (pct ? 1 : 3);
                continue;
            }
            out += cond.substr(i, j - i);
            i = j;
            continue;
        }
        out += cond[i++];
    }
    return out;
}

static bool pass_condition(const std::string& line) {
    re::Groups g;
    if (re::search("ALWAYS", "i", line)) {
        set_proper_target(get_action_group());
        return true;
    }
    if (re::search("ELEMENT[ ](.*)", "i", line)) {
        std::string l = g_ai.orig_condition;
        int element_id;
        std::string type;
        if (re::search("ELEMENT[ ](\\d+)[ ](.*)", "i", l, &g)) {
            element_id = g.num(1);
            type = to_upper(g[2]);
        } else if (re::search("ELEMENT[ ](.*)[ ](.*)", "i", l, &g)) {
            element_id = ref_id("ElementIdRef", g[1]);
            type = to_upper(g[2]);
        } else {
            return false;
        }
        std::vector<GameBattler*> valid;
        for (GameBattler* t : get_action_group()) {
            if (t && element_rate_match(t, element_id, type)) valid.push_back(t);
        }
        if (valid.empty()) return false;
        set_proper_target(valid);
        return true;
    }
    if (re::search("EVAL[ ](.*)", "i", line, &g)) {
        JsEval e = make_eval();
        if (!run_condition(g[1], e, "A.I. EVAL ERROR")) return false;
        set_proper_target(get_action_group());
        return true;
    }
    auto group_check = [&](bool alive, const std::string& members0, const std::string& cond) {
        std::string members = to_upper(members0);
        std::vector<GameBattler*> m;
        if (members == "TROOP" || members == "TROOPS" || members == "ENEMY" || members == "ENEMIES") m = alive ? game_troop().alive_members() : game_troop().dead_members();
        else if (members == "PARTY" || members == "PLAYER") m = alive ? game_party().alive_members() : game_party().dead_members();
        else return false;
        if (m.empty()) return false;
        JsEval e = make_eval();
        std::vector<js::Val> arr;
        for (GameBattler* b : m) arr.push_back(b->to_js());
        e.set("members", js::make_array(arr));
        if (!run_condition("members.length " + cond, e, alive ? "A.I. GROUP ALIVE ERROR" : "A.I. GROUP DEAD ERROR")) return false;
        set_proper_target(get_action_group());
        return true;
    };
    if (re::search("(.*)[ ]ALIVE[ ]MEMBERS[ ](.*)", "i", line, &g)) return group_check(true, g[1], g[2]);
    if (re::search("(.*)[ ]DEAD[ ]MEMBERS[ ](.*)", "i", line, &g)) return group_check(false, g[1], g[2]);
    auto param_eval = [&](bool user, int pid, std::string cond) {
        cond = percent_to_decimal(cond);
        if (pid < 0) return false;
        std::string who = user ? "user" : "target";
        if (pid <= 7) cond = who + ".param(paramId) " + cond;
        else if (pid == 8) cond = who + ".hp " + cond;
        else if (pid == 9) cond = who + ".mp " + cond;
        else if (pid == 10) cond = who + ".hp / " + who + ".mhp " + cond;
        else if (pid == 11) cond = who + ".mp / " + who + ".mmp " + cond;
        else if (pid == 12) cond = who + ".level " + cond;
        std::vector<GameBattler*> valid;
        for (GameBattler* t : get_action_group()) {
            if (!t) continue;
            JsEval e = make_eval();
            e.set("target", t->to_js()).num("paramId", pid);
            if (run_condition(cond, e, user ? "A.I. USER PARAM ERROR" : "A.I. PARAM ERROR")) valid.push_back(t);
        }
        if (valid.empty()) return false;
        set_proper_target(valid);
        return true;
    };
    if (re::search("USER[ ](.*)[ ]PARAM[ ](.*)", "i", line, &g)) return param_eval(true, param_id(g[1]), g[2]);
    if (re::search("(.*)[ ]PARAM[ ](.*)", "i", line, &g)) return param_eval(false, param_id(g[1]), g[2]);
    if (re::search("(.*)[ ]PARTY[ ]LEVEL[ ](.*)", "i", line, &g)) {
        std::string type = g[1], cond = g[2];
        if (re::search("HIGHEST", "i", type)) cond = ".highestLevel() " + cond;
        else if (re::search("LOWEST", "i", type)) cond = ".lowestLevel() " + cond;
        else if (re::search("AVERAGE", "i", type)) cond = ".averageLevel() " + cond;
        else return false;
        if (g_ai.action->is_for_friend()) cond = "action.friendsUnit()" + cond;
        else if (g_ai.action->is_for_opponent()) cond = "action.opponentsUnit()" + cond;
        JsEval e = make_eval();
        if (!run_condition(cond, e, "A.I. PARTY LEVEL ERROR")) return false;
        set_proper_target(get_action_group());
        return true;
    }
    if (re::search("RANDOM[ ](\\d+)([%\xEF\xBC\x85])", "i", line, &g)) {
        if (rng::next() >= js_parse_float(g[1]) * 0.01) return false;
        set_proper_target(get_action_group());
        return true;
    }
    auto state_check = [&](bool has, const std::string& cond) {
        int state_id;
        re::Groups h;
        if (re::search("STATE[ ](\\d+)", "i", cond, &h)) state_id = h.num(1);
        else {
            state_id = ref_id("StateIdRef", cond);
            if (!state_id) return false;
        }
        if (!data::states()[state_id].is_obj()) return false;
        std::vector<GameBattler*> valid;
        for (GameBattler* t : get_action_group()) {
            if (!t) continue;
            if (has ? t->has_state(state_id) : t->not_state(state_id)) valid.push_back(t);
        }
        if (valid.empty()) return false;
        set_proper_target(valid);
        return true;
    };
    if (re::search("STATE[ ]===[ ](.*)", "i", line, &g)) return state_check(true, g[1]);
    if (re::search("STATE[ ]!==[ ](.*)", "i", line, &g)) return state_check(false, g[1]);
    if (re::search("SWITCH[ ](\\d+)[ ](.*)", "i", line, &g)) {
        std::string v = to_upper(g[2]);
        bool value;
        if (v == "ON" || v == "TRUE" || v == "YES") value = true;
        else if (v == "OFF" || v == "FALSE" || v == "NO") value = false;
        else return false;
        if (game_switches().value(g.num(1)) != value) return false;
        set_proper_target(get_action_group());
        return true;
    }
    if (re::search("TURN[ ](.*)", "i", line, &g)) {
        std::string cond;
        if (battle::st().phase == "input" && battle::is_turn_based()) cond = "(user.turnCount() + 1) " + g[1];
        else cond = "user.turnCount() " + g[1];
        JsEval e = make_eval();
        if (!run_condition(cond, e, "A.I. TURN COUNT ERROR")) return false;
        set_proper_target(get_action_group());
        return true;
    }
    if (re::search("VARIABLE[ ](\\d+)[ ](.*)", "i", line, &g)) {
        JsEval e = make_eval();
        if (!run_condition("$gameVariables.value(" + std::to_string(g.num(1)) + ") " + g[2], e, "A.I. VARIABLE ERROR")) return false;
        set_proper_target(get_action_group());
        return true;
    }
    return false;
}

static bool pass_all(const std::string& line) {
    g_ai.has_group = false;
    g_ai.group.clear();
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        size_t p = line.find("+++", start);
        if (p == std::string::npos) {
            parts.push_back(line.substr(start));
            break;
        }
        parts.push_back(line.substr(start, p - start));
        start = p + 3;
    }
    for (auto& c : parts) {
        if (!pass_condition(trim(c))) return false;
    }
    return true;
}

static bool is_decided_action(const std::string& line) {
    re::Groups g;
    std::string condition;
    if (re::search("[ ]*(.*):[ ](?:SKILL)[ ](\\d+),[ ](.*)", "i", line, &g)) {
        g_ai.orig_condition = condition = g[1];
        g_ai.skill_id = g.num(2);
        g_ai.target = g[3];
    } else if (re::search("[ ]*(.*):[ ](?:SKILL)[ ](\\d+)", "i", line, &g)) {
        g_ai.orig_condition = condition = g[1];
        g_ai.skill_id = g.num(2);
        g_ai.target = "RANDOM";
    } else if (re::search("[ ]*(.*):[ ](.*),[ ](.*)", "i", line, &g)) {
        g_ai.orig_condition = condition = g[1];
        g_ai.skill_id = ref_id("SkillIdRef", g[2]);
        g_ai.target = g[3];
    } else if (re::search("[ ]*(.*):[ ](.*)", "i", line, &g)) {
        g_ai.orig_condition = condition = g[1];
        g_ai.skill_id = ref_id("SkillIdRef", g[2]);
        g_ai.target = "RANDOM";
    } else {
        return false;
    }
    if (!data::skills()[g_ai.skill_id].is_obj()) return false;
    if (!static_cast<GameEnemy*>(g_ai.battler)->has_skill_id(g_ai.skill_id)) return false;
    if (!g_ai.battler->meets_skill_conditions(ItemRef::skill(g_ai.skill_id))) return false;
    g_ai.action->set_skill(g_ai.skill_id);
    return pass_all(condition);
}

}

bool GameEnemy::has_skill_id(int id) const {
    for (auto& s : skills()) {
        if (s.id == id) return true;
    }
    return false;
}

void GameEnemy::set_ai_pattern() {
    GameBattler::base_make_actions();
    if (num_actions() <= 0) return;
    ai::g_ai.battler = this;
    ai::g_ai.action = action(0);
    const Value& pattern = enemy()["aiPattern"];
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (rng::next() > ai_level()) continue;
        if (ai::is_decided_action(pattern[i].string())) return;
    }
    base_make_actions();
}

GameTroop::GameTroop() : interpreter(new GameInterpreter()) { clear(); }

GameTroop::~GameTroop() = default;

std::vector<GameBattler*> GameTroop::unit_members() {
    std::vector<GameBattler*> out;
    for (auto& e : enemies) out.push_back(e.get());
    return out;
}

std::vector<GameEnemy*> GameTroop::members() const {
    std::vector<GameEnemy*> out;
    for (auto& e : enemies) out.push_back(e.get());
    return out;
}

bool GameTroop::is_event_running() const { return interpreter->is_running(); }

void GameTroop::update_interpreter() { interpreter->update(); }

void GameTroop::clear() {
    interpreter->clear();
    troop_id = 0;
    event_flags.clear();
    enemies.clear();
    turn_count_ = 0;
    names_count.clear();
    ignore_pages.clear();
}

const Value& GameTroop::troop() const { return data::troops()[troop_id]; }

void GameTroop::setup(int id) {
    clear();
    troop_id = id;
    enemies.clear();
    const Value& members = troop()["members"];
    for (size_t i = 0; i < members.size(); i++) {
        const Value& m = members[i];
        if (!data::enemies()[m["enemyId"].as_int()].is_obj()) continue;
        auto e = std::make_shared<GameEnemy>(m["enemyId"].as_int(), m["x"].num(), m["y"].num());
        if (m["hidden"].truthy()) e->hide();
        enemies.push_back(e);
    }
    make_unique_names();
    ai_known_element_rates.clear();
    clear_reinforcements();
}

void GameTroop::make_unique_names() {
    for (auto& e : enemies) {
        std::string name = e->original_name();
        auto it = names_count.find(name);
        if (it != names_count.end() && it->second >= 2) e->set_plural(true);
    }
}

std::vector<std::string> GameTroop::enemy_names() {
    std::vector<std::string> names;
    for (auto& e : enemies) {
        std::string n = e->original_name();
        if (e->is_alive() && std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
    }
    return names;
}

bool GameTroop::base_meets_conditions(const Value& page) {
    const Value& c = page["conditions"];
    if (!c["turnEnding"].truthy() && !c["turnValid"].truthy() && !c["enemyValid"].truthy() && !c["actorValid"].truthy() && !c["switchValid"].truthy())
        return false;
    if (c["turnEnding"].truthy() && !battle::is_turn_end()) return false;
    if (c["turnValid"].truthy()) {
        double n = turn_count_, a = c["turnA"].num(), b = c["turnB"].num();
        if (b == 0 && n != a) return false;
        if (b > 0 && (n < 1 || n < a || std::fmod(n, b) != std::fmod(a, b))) return false;
    }
    if (c["enemyValid"].truthy()) {
        int idx = c["enemyIndex"].as_int();
        GameEnemy* e = idx >= 0 && idx < (int)enemies.size() ? enemies[(size_t)idx].get() : nullptr;
        if (!e || e->hp_rate() * 100 > c["enemyHp"].num()) return false;
    }
    if (c["actorValid"].truthy()) {
        GameActor* a = game_actors().actor(c["actorId"].as_int());
        if (!a || a->hp_rate() * 100 > c["actorHp"].num()) return false;
    }
    if (c["switchValid"].truthy() && !game_switches().value(c["switchId"].as_int())) return false;
    return true;
}

bool GameTroop::meets_conditions(const Value& page) {
    const Value& pages = troop()["pages"];
    int index = -1;
    for (size_t i = 0; i < pages.size(); i++) {
        if (&pages[i] == &page) index = (int)i;
    }
    if (contains_int(ignore_pages, index)) return false;
    return base_meets_conditions(page);
}

void GameTroop::setup_battle_event() {
    if (interpreter->is_running()) return;
    if (interpreter->setup_reserved_common_event()) return;
    const Value& pages = troop()["pages"];
    for (size_t i = 0; i < pages.size(); i++) {
        const Value& page = pages[i];
        if (meets_conditions(page) && !event_flags[(int)i]) {
            interpreter->setup(&page["list"], 0, data::keep());
            if (page["span"].as_int() <= 1) event_flags[(int)i] = true;
            break;
        }
    }
}

void GameTroop::increase_turn() {
    const Value& pages = troop()["pages"];
    for (size_t i = 0; i < pages.size(); i++) {
        if (pages[i]["span"].as_int() == 1) event_flags[(int)i] = false;
    }
    turn_count_++;
}

double GameTroop::exp_total() {
    double r = 0;
    for (GameBattler* e : dead_members()) r += static_cast<GameEnemy*>(e)->exp();
    return r;
}

double GameTroop::gold_total() {
    double r = 0;
    for (GameBattler* e : dead_members()) r += static_cast<GameEnemy*>(e)->gold();
    return r * gold_rate();
}

double GameTroop::gold_rate() const { return game_party().has_gold_double() ? 2 : 1; }

std::vector<ItemRef> GameTroop::make_drop_items() {
    std::vector<ItemRef> r;
    for (GameBattler* e : dead_members()) {
        for (auto& it : static_cast<GameEnemy*>(e)->make_drop_items()) r.push_back(it);
    }
    return r;
}

void GameTroop::update_ai_patterns() {
    auto alive = alive_members();
    for (size_t i = 0; i < alive.size(); ++i) {
        auto cur = alive_members();
        if (i < cur.size() && cur[i]) cur[i]->set_ai_pattern();
    }
}

bool GameTroop::ai_element_rate_known(GameBattler* target, int element_id) {
    if (target->is_enemy()) return true;
    if (!yanfly_param("CoreAIElementTest").truthy()) return true;
    return contains_int(ai_known_element_rates[target->index()], element_id);
}

void GameTroop::ai_register_element_rate(GameBattler* target, int element_id) {
    if (!yanfly_param("CoreAIElementTest").truthy()) return;
    auto& v = ai_known_element_rates[target->index()];
    if (!contains_int(v, element_id)) v.push_back(element_id);
}

void GameTroop::add_reinforcement_member(int tid, int member_id, const Value& member) {
    if (!data::enemies()[member["enemyId"].as_int()].is_obj()) return;
    auto e = std::make_shared<GameEnemy>(member["enemyId"].as_int(), member["x"].num(), member["y"].num());
    e->troop_id = tid;
    e->troop_member_id = member_id;
    e->has_troop_member_id = true;
    if (member["hidden"].truthy()) e->hide();
    enemies.push_back(e);
    new_enemies.push_back(e);
}

void GameTroop::add_enemy_reinforcement(int tid, int member_id) {
    const Value& member = data::troops()[tid]["members"][member_id - 1];
    add_reinforcement_member(tid, member_id, member);
    make_unique_names();
    battle::refresh_enemy_reinforcements();
}

void GameTroop::add_troop_reinforcements(int tid) {
    const Value& members = data::troops()[tid]["members"];
    for (size_t i = 0; i < members.size(); i++) add_reinforcement_member(tid, (int)i, members[i]);
    make_unique_names();
    battle::refresh_enemy_reinforcements();
}

void GameTroop::remove_enemy_reinforcement(int tid, int member_id) {
    for (int i = (int)enemies.size() - 1; i > -1; i--) {
        if (enemies[(size_t)i]->troop_id == tid && enemies[(size_t)i]->has_troop_member_id && enemies[(size_t)i]->troop_member_id == member_id)
            enemies.erase(enemies.begin() + i);
    }
    battle::refresh_enemy_reinforcements();
}

void GameTroop::remove_troop_reinforcements(int tid) {
    for (int i = (int)enemies.size() - 1; i > -1; i--) {
        if (enemies[(size_t)i]->troop_id == tid) enemies.erase(enemies.begin() + i);
    }
    battle::refresh_enemy_reinforcements();
}

bool GameTroop::is_enemy_reinforcement_added(int tid, int member_id, bool needs_alive) {
    for (auto& e : enemies) {
        if (e->troop_id == tid && e->has_troop_member_id && e->troop_member_id == member_id) {
            if (needs_alive) {
                if (e->is_alive()) return true;
            } else {
                return true;
            }
        }
    }
    return false;
}

bool GameTroop::is_troop_reinforcement_added(int tid, bool needs_alive) {
    for (auto& e : enemies) {
        if (e->troop_id == tid) {
            if (needs_alive) {
                if (e->is_alive()) return true;
            } else {
                return true;
            }
        }
    }
    return false;
}

void GameTroop::setup_common_event(int id) {
    const Value& ce = data::common_events()[id];
    if (!ce.is_obj()) return;
    int eid = interpreter->is_on_current_map() ? interpreter->event_id() : 0;
    interpreter->setup_child(&ce["list"], eid, data::keep());
}

void GameTroop::ignore_page(int page) {
    if (!contains_int(ignore_pages, page)) ignore_pages.push_back(page);
}

void GameTroop::stop_ignore_page(int page) {
    auto it = std::find(ignore_pages.begin(), ignore_pages.end(), page);
    if (it != ignore_pages.end()) ignore_pages.erase(it);
}

bool GameTroop::is_all_dead() {
    if (battle::is_battle_intro_active()) return false;
    return GameUnit::is_all_dead();
}

bool GameTroop::is_humphrey_battle() const {
    static const int ids[] = {436, 437, 438, 439, 440, 606};
    return std::find(std::begin(ids), std::end(ids), troop_id) != std::end(ids);
}

bool GameTroop::are_summon_troops() const { return troop_id == 410 || troop_id == 427 || troop_id == 603; }

bool GameTroop::wait_for_emotion() const {
    const Value& pages = troop()["pages"];
    bool any_turn_zero = false;
    bool wait = false;
    for (size_t i = 0; i < pages.size(); i++) {
        const Value& c = pages[i]["conditions"];
        if (c["turnA"].num() != 0 || c["turnB"].num() != 0 || !c["turnValid"].truthy()) continue;
        any_turn_zero = true;
        const Value& list = pages[i]["list"];
        for (size_t k = 0; k < list.size(); k++) {
            if (list[k]["code"].as_int() == 117 && list[k]["parameters"][0].as_int() == 985) {
                wait = true;
                break;
            }
        }
        if (wait) break;
    }
    if (!any_turn_zero) return false;
    static const int tutorial[] = {541, 542, 543}, rare_bear[] = {167, 168, 189}, pluto[] = {616},
                     fears[] = {336, 344, 351, 353, 358, 365, 367, 368, 372, 373, 375, 377, 441, 442, 564, 565, 566};
    auto in = [&](const int* b, const int* e) { return std::find(b, e, troop_id) != e; };
    return wait || in(std::begin(tutorial), std::end(tutorial)) || in(std::begin(rare_bear), std::end(rare_bear)) || in(std::begin(pluto), std::end(pluto)) ||
           in(std::begin(fears), std::end(fears));
}

void GameTroop::on_battle_start() {
    for (GameBattler* m : unit_members()) m->on_battle_start();
    in_battle_ = true;
    process_state_eval("battle");
}

void GameTroop::on_battle_end() {
    in_battle_ = false;
    for (GameBattler* m : unit_members()) m->on_battle_end();
}

void GameTroop::make_actions() {
    for (GameBattler* m : unit_members()) m->make_actions();
}

void GameTroop::create_actions() {
    auto m = unit_members();
    for (size_t i = 0; i < m.size(); ++i) {
        if (m[i]) m[i]->create_actions();
    }
}

void GameTroop::on_turn_start() {
    auto m = unit_members();
    for (size_t i = 0; i < m.size(); ++i) {
        if (m[i]) {
            m[i]->on_turn_start();
            m[i]->refresh();
        }
    }
}

void GameTroop::update_tick() {
    for (GameBattler* m : unit_members()) {
        if (m) m->update_tick();
    }
}

}
