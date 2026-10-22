#include "battler.h"
#include "battle.h"

#include <algorithm>
#include <cmath>

#include "../core/regex.h"
#include "../js/dataview.h"
#include "../js/random.h"
#include "../js/vm.h"
#include "audio_manager.h"
#include "data.h"
#include "game.h"
#include "hooks.h"
#include "party.h"
#include "params.h"

namespace rpg {

namespace {

bool contains_int(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

bool category_has(const Value& state, const std::string& cat) {
    const Value& c = state["category"];
    for (size_t i = 0; i < c.size(); i++) {
        if (c[i].string() == cat) return true;
    }
    return false;
}

std::string upper_trim(const std::string& s) { return trim(to_upper(s)); }

}

std::string actor_meta(const Value& obj, const char* key) {
    const Value* v = obj["meta"].find(key);
    if (!v) return "";
    if (v->t == Value::Bool) return v->b ? "true" : "";
    return v->string();
}

ItemRef ItemRef::from_data(const Value* v) {
    if (!v || !v->is_obj()) return ItemRef();
    int id = (*v)["id"].as_int();
    if (&data::skills()[id] == v) return skill(id);
    if (&data::items()[id] == v) return item(id);
    if (&data::weapons()[id] == v) return weapon(id);
    if (&data::armors()[id] == v) return armor(id);
    return ItemRef();
}

ItemRef ItemRef::from_js(const js::Val& v) { return from_data(js::data_of(v)); }

const Value& ItemRef::data() const {
    switch (kind) {
    case Skill: return data::skills()[id];
    case Item: return data::items()[id];
    case Weapon: return data::weapons()[id];
    case Armor: return data::armors()[id];
    default: return Value::null_value();
    }
}

js::Val ItemRef::to_js() const {
    const Value& d = data();
    if (!d.is_obj()) return js::Val::null();
    return js::data_view(d, data::keep(), true);
}

const char* ItemRef::class_name() const {
    switch (kind) {
    case Skill: return "skill";
    case Item: return "item";
    case Weapon: return "weapon";
    case Armor: return "armor";
    default: return "";
    }
}

ItemRef GameItem::object() const {
    if (data_class == "skill") return ItemRef::skill(item_id);
    if (data_class == "item") return ItemRef::item(item_id);
    if (data_class == "weapon") return ItemRef::weapon(item_id);
    if (data_class == "armor") return ItemRef::armor(item_id);
    return ItemRef();
}

void GameItem::set_object(const ItemRef& r) {
    data_class = r.valid() ? r.class_name() : "";
    item_id = r.valid() ? r.id : 0;
}

void GameActionResult::clear() {
    used = missed = evaded = physical = drain = critical = success = hp_affected = false;
    hp_damage = mp_damage = tp_damage = 0;
    added_states.clear();
    removed_states.clear();
    added_buffs.clear();
    added_debuffs.clear();
    removed_buffs.clear();
    element_strong = element_weak = false;
}

bool GameActionResult::is_status_affected() const {
    return !added_states.empty() || !removed_states.empty() || !added_buffs.empty() || !added_debuffs.empty() || !removed_buffs.empty();
}

bool GameActionResult::is_state_added(int id) const { return contains_int(added_states, id); }
void GameActionResult::push_added_state(int id) {
    if (!is_state_added(id)) added_states.push_back(id);
}
bool GameActionResult::is_state_removed(int id) const { return contains_int(removed_states, id); }
void GameActionResult::push_removed_state(int id) {
    if (!is_state_removed(id)) removed_states.push_back(id);
}
bool GameActionResult::is_buff_added(int id) const { return contains_int(added_buffs, id); }
void GameActionResult::push_added_buff(int id) {
    if (!is_buff_added(id)) added_buffs.push_back(id);
}
bool GameActionResult::is_debuff_added(int id) const { return contains_int(added_debuffs, id); }
void GameActionResult::push_added_debuff(int id) {
    if (!is_debuff_added(id)) added_debuffs.push_back(id);
}
bool GameActionResult::is_buff_removed(int id) const { return contains_int(removed_buffs, id); }
void GameActionResult::push_removed_buff(int id) {
    if (!is_buff_removed(id)) removed_buffs.push_back(id);
}

GameBattlerBase::GameBattlerBase() {}

void GameBattlerBase::init_members() {
    hp_ = 1;
    mp_ = 0;
    tp_ = 0;
    hidden = false;
    clear_param_plus();
    clear_states();
    clear_buffs();
    init_state_counter();
}

void GameBattlerBase::clear_param_plus() {
    for (double& p : param_plus_) p = 0;
}

void GameBattlerBase::base_clear_states() {
    bool custom = is_custom_clear_states();
    std::vector<int> states_copy = states_;
    std::map<int, double> turns_copy = state_turns;
    states_.clear();
    state_turns.clear();
    if (custom) retain_custom_clear_states(states_copy, turns_copy);
}

void GameBattlerBase::clear_states() { base_clear_states(); }

void GameBattlerBase::erase_state(int id) {
    auto it = std::find(states_.begin(), states_.end(), id);
    if (it != states_.end()) states_.erase(it);
    state_turns.erase(id);
    update_auras(id);
}

bool GameBattlerBase::is_state_affected(int id) {
    if (is_passive_state_affected(id)) return true;
    return contains_int(states_, id);
}

void GameBattlerBase::reset_state_counts(int id) {
    const Value& st = data::states()[id];
    int rules = st["reapplyRules"].as_int();
    auto base = [&]() {
        double variance = 1 + std::max(st["maxTurns"].num() - st["minTurns"].num(), 0.0);
        state_turns[id] = st["minTurns"].num() + rng::below((int)variance);
    };
    if (rules == 1) {
        base();
    } else if (rules == 2) {
        double cur = state_turns.count(id) ? state_turns[id] : 0;
        double variance = 1 + std::max(st["maxTurns"].num() - st["minTurns"].num(), 0.0);
        state_turns[id] = cur + st["minTurns"].num() + rng::below((int)variance);
    } else if (rules == 0 && st["minTurns"].truthy()) {
        if (!state_turns.count(id)) base();
    }
}

bool GameBattlerBase::is_state_expired(int id) const {
    auto it = state_turns.find(id);
    return it != state_turns.end() && it->second == 0;
}

void GameBattlerBase::clear_buffs() {
    for (int i = 0; i < 8; i++) {
        buffs_[i] = 0;
        buff_turns[i] = 0;
    }
}

void GameBattlerBase::erase_buff(int id) {
    if (id < 0 || id >= 8) return;
    buffs_[id] = 0;
    buff_turns[id] = 0;
}

bool GameBattlerBase::is_max_buff_affected(int id) {
    double limit = std::max(1.0, max_buff_limit(id));
    double max = yanfly_param("BSCMaximumLimit").num();
    return buff(id) == std::min(limit, max);
}

bool GameBattlerBase::is_max_debuff_affected(int id) {
    double limit = std::min(-1.0, max_debuff_limit(id));
    double max = yanfly_param("BSCMaximumLimit").num() * -1;
    return buff(id) == std::max(limit, max);
}

void GameBattlerBase::increase_buff(int id) {
    if (!is_max_buff_affected(id) && id >= 0 && id < 8) buffs_[id]++;
}

void GameBattlerBase::decrease_buff(int id) {
    if (!is_max_debuff_affected(id) && id >= 0 && id < 8) buffs_[id]--;
}

void GameBattlerBase::overwrite_buff_turns(int id, double turns) {
    if (id >= 0 && id < 8 && buff_turns[id] < turns) buff_turns[id] = turns;
}

void GameBattlerBase::update_buff_turns() {
    for (int i = 0; i < 8; i++) {
        if (buff_turns[i] > 0) buff_turns[i]--;
    }
}

void GameBattlerBase::die() {
    game_temp().death_state_clear = true;
    hp_ = 0;
    clear_states();
    clear_buffs();
    game_temp().death_state_clear = false;
}

void GameBattlerBase::revive() {
    if (hp_ == 0) hp_ = 1;
}

std::vector<const Value*> GameBattlerBase::states() {
    std::vector<const Value*> out;
    for (int id : states_) out.push_back(&data::states()[id]);
    for (const Value* p : passive_states()) out.push_back(p);
    sort_passive_states(out);
    return out;
}

std::vector<int> GameBattlerBase::state_icons() {
    std::vector<int> out;
    for (const Value* s : states()) {
        int icon = (*s)["iconIndex"].as_int();
        if (icon > 0) out.push_back(icon);
    }
    return out;
}

int GameBattlerBase::buff_icon_index(double level, int param_id) const {
    if (level > 0) {
        int l = (int)std::min(level - 1, 1.0);
        return 32 + l * 8 + param_id;
    }
    if (level < 0) {
        int l = (int)std::min(-level - 1, 1.0);
        return 48 + l * 8 + param_id;
    }
    return 0;
}

std::vector<int> GameBattlerBase::buff_icons() {
    std::vector<int> out;
    for (int i = 0; i < 8; i++) {
        if (buffs_[i] != 0) out.push_back(buff_icon_index(buffs_[i], i));
    }
    return out;
}

std::vector<int> GameBattlerBase::all_icons() {
    std::vector<int> a = state_icons();
    for (int i : buff_icons()) a.push_back(i);
    return a;
}

std::vector<const Value*> GameBattlerBase::trait_objects() { return states(); }

void GameBattlerBase::for_each_trait(int code, const std::function<void(const Value&)>& fn) {
    for (const Value* o : trait_objects()) {
        const Value& traits = (*o)["traits"];
        for (size_t i = 0; i < traits.size(); i++) {
            if (traits[i]["code"].as_int() == code) fn(traits[i]);
        }
    }
}

double GameBattlerBase::traits_pi(int code, int id) {
    double r = 1;
    for_each_trait(code, [&](const Value& t) {
        if (t["dataId"].as_int() == id) r *= t["value"].num();
    });
    return r;
}

double GameBattlerBase::traits_sum(int code, int id) {
    double r = 0;
    for_each_trait(code, [&](const Value& t) {
        if (t["dataId"].as_int() == id) r += t["value"].num();
    });
    return r;
}

double GameBattlerBase::traits_sum_all(int code) {
    double r = 0;
    for_each_trait(code, [&](const Value& t) { r += t["value"].num(); });
    return r;
}

std::vector<int> GameBattlerBase::traits_set(int code) {
    std::vector<int> r;
    for_each_trait(code, [&](const Value& t) { r.push_back(t["dataId"].as_int()); });
    return r;
}

double GameBattlerBase::param_plus(int id) { return id >= 0 && id < 8 ? param_plus_[id] : 0; }

double GameBattlerBase::param_max(int id) const {
    if (id == 0) return yanfly_param("EnemyMaxHp").num();
    if (id == 1) return yanfly_param("EnemyMaxMp").num();
    return yanfly_param("EnemyParam").num();
}

double GameBattlerBase::param_buff_rate(int id) {
    auto it = cache_buff_rate.find(id);
    if (it != cache_buff_rate.end()) return it->second;
    double rate = 1;
    if (const Value* ast = yanfly_ast("BSCBuffFormula")) {
        js::Val v = eval_note_code(*ast, nullptr, js::Val(), {{"buffLevel", js::Val::num(buff(id))}, {"paramId", js::Val::num(id)}}, "PARAM BUFF RATE FORMULA ERROR");
        rate = v.is_undef() ? 1 : v.to_num();
    }
    cache_buff_rate[id] = rate;
    return rate;
}

double GameBattlerBase::param(int id) {
    double value = param_base(id) + param_plus(id);
    value *= param_rate(id) * param_buff_rate(id);
    double maxv = param_max(id), minv = param_min(id);
    return std::round(std::min(std::max(value, minv), maxv));
}

bool GameBattlerBase::is_state_resist(int id) {
    if (id == death_state_id() && is_immortal()) return true;
    return contains_int(state_resist_set(), id);
}

double GameBattlerBase::attack_times_add() { return std::max(traits_sum_all(TRAIT_ATTACK_TIMES), 0.0); }
bool GameBattlerBase::is_skill_type_sealed(int id) { return contains_int(traits_set(TRAIT_STYPE_SEAL), id); }
bool GameBattlerBase::is_skill_sealed(int id) { return contains_int(traits_set(TRAIT_SKILL_SEAL), id); }
bool GameBattlerBase::is_equip_wtype_ok(int id) { return contains_int(traits_set(TRAIT_EQUIP_WTYPE), id); }
bool GameBattlerBase::is_equip_atype_ok(int id) { return contains_int(traits_set(TRAIT_EQUIP_ATYPE), id); }
bool GameBattlerBase::is_equip_type_locked(int id) { return contains_int(traits_set(TRAIT_EQUIP_LOCK), id); }
bool GameBattlerBase::is_equip_type_sealed(int id) { return contains_int(traits_set(TRAIT_EQUIP_SEAL), id); }

int GameBattlerBase::slot_type() {
    std::vector<int> s = traits_set(TRAIT_SLOT_TYPE);
    return s.empty() ? 0 : *std::max_element(s.begin(), s.end());
}

std::vector<double> GameBattlerBase::action_plus_set() {
    std::vector<double> r;
    for_each_trait(TRAIT_ACTION_PLUS, [&](const Value& t) { r.push_back(t["value"].num()); });
    return r;
}

bool GameBattlerBase::special_flag(int id) {
    bool r = false;
    for_each_trait(TRAIT_SPECIAL_FLAG, [&](const Value& t) { r = r || t["dataId"].as_int() == id; });
    return r;
}

int GameBattlerBase::collapse_type() {
    std::vector<int> s = traits_set(TRAIT_COLLAPSE_TYPE);
    return s.empty() ? 0 : *std::max_element(s.begin(), s.end());
}

bool GameBattlerBase::party_ability(int id) {
    bool r = false;
    for_each_trait(TRAIT_PARTY_ABILITY, [&](const Value& t) { r = r || t["dataId"].as_int() == id; });
    return r;
}

void GameBattlerBase::add_param(int id, double value) {
    if (id < 0 || id >= 8) return;
    param_plus_[id] += value;
    refresh();
}

void GameBattlerBase::set_hp(double hp) {
    if (hp_ == hp) return;
    hp_ = hp;
    if (map_regen_update_check("hp")) refresh();
    else base_refresh();
}

void GameBattlerBase::set_mp(double mp) {
    if (mp_ == mp) return;
    mp_ = mp;
    if (map_regen_update_check("mp")) refresh();
    else base_refresh();
}

void GameBattlerBase::set_tp(double tp) {
    if (tp_ == tp) return;
    tp_ = tp;
    if (map_regen_update_check("tp")) refresh();
    else base_refresh();
}

void GameBattlerBase::base_refresh() {
    for (int id : state_resist_set()) erase_state(id);
    hp_ = std::min(std::max(hp_, 0.0), mhp());
    mp_ = std::min(std::max(mp_, 0.0), mmp());
    tp_ = std::min(std::max(tp_, 0.0), max_tp());
}

void GameBattlerBase::refresh() {
    has_passive_raw = false;
    cache_buff_rate.clear();
    base_refresh();
}

void GameBattlerBase::recover_all() {
    game_temp().recover_all_clear = true;
    clear_states();
    hp_ = mhp();
    mp_ = mmp();
    refresh();
    game_temp().recover_all_clear = false;
}

bool GameBattlerBase::is_confused() {
    int r = restriction();
    return is_appeared() && r >= 1 && r <= 3;
}

void GameBattlerBase::sort_states() {
    std::stable_sort(states_.begin(), states_.end(), [](int a, int b) {
        double p1 = data::states()[a]["priority"].num(), p2 = data::states()[b]["priority"].num();
        if (p1 != p2) return p2 < p1;
        return a < b;
    });
}

int GameBattlerBase::restriction() {
    int r = 0;
    for (const Value* s : states()) r = std::max(r, (*s)["restriction"].as_int());
    return r;
}

void GameBattlerBase::add_new_state(int id) {
    if (id == death_state_id()) die();
    bool restricted = is_restricted();
    states_.push_back(id);
    sort_states();
    if (!restricted && is_restricted()) on_restrict();
    update_auras(id);
}

std::string GameBattlerBase::most_important_state_text() {
    for (const Value* s : states()) {
        if ((*s)["message3"].truthy()) return (*s)["message3"].string();
    }
    return "";
}

int GameBattlerBase::state_motion_index() {
    auto s = states();
    return s.empty() ? 0 : (*s[0])["motion"].as_int();
}

int GameBattlerBase::state_overlay_index() {
    auto s = states();
    return s.empty() ? 0 : (*s[0])["overlay"].as_int();
}

js::Val GameBattlerBase::eval_note_code(const Value& ast, const char* result_var, const js::Val& init,
                                        std::initializer_list<std::pair<const char*, js::Val>> extra, const char* where) {
    js::ScopePtr scope = std::make_shared<js::Scope>();
    js::Val self = to_js();
    scope->declare(js::atom("a"), self);
    scope->declare(js::atom("user"), self);
    scope->declare(js::atom("subject"), self);
    scope->declare(js::atom("b"), self);
    scope->declare(js::atom("target"), self);
    scope->declare(js::atom("s"), switches_proxy());
    scope->declare(js::atom("v"), variables_proxy());
    for (auto& e : extra) scope->declare(js::atom(e.first), e.second);
    if (result_var) scope->declare(js::atom(result_var), init);
    js::Val completion;
    if (!js::vm().run(ast, self, scope, &completion, data::keep())) script_error(where, js::vm().last_error);
    if (!result_var) return completion;
    js::Val* r = scope->find(js::atom(result_var));
    return r ? *r : js::Val();
}

double GameBattlerBase::skill_mp_cost(const ItemRef& skill) {
    const Value& s = skill.data();
    double cost = s["mpCost"].num();
    cost += mmp() * s["mpCostPer"].num();
    if (const Value* ast = obj_ast(s, "mpCostEval")) {
        cost = eval_note_code(*ast, "cost", js::Val::num(cost), {{"item", skill.to_js()}}, "SKILL CUSTOM MP COST ERROR").to_num();
    }
    return std::max(0.0, std::floor(cost * mcr()));
}

double GameBattlerBase::skill_tp_cost(const ItemRef& skill) {
    const Value& s = skill.data();
    double cost = s["tpCost"].num();
    cost += max_tp() * s["tpCostPer"].num();
    if (const Value* ast = obj_ast(s, "tpCostEval")) {
        cost = eval_note_code(*ast, "cost", js::Val::num(cost), {{"item", skill.to_js()}}, "SKILL CUSTOM TP COST ERROR").to_num();
    }
    return std::max(0.0, std::floor(cost));
}

double GameBattlerBase::skill_hp_cost(const ItemRef& skill) {
    const Value& s = skill.data();
    double cost = s["hpCost"].num();
    cost += mhp() * s["hpCostPer"].num();
    if (const Value* ast = obj_ast(s, "hpCostEval")) {
        cost = eval_note_code(*ast, "cost", js::Val::num(cost), {{"item", skill.to_js()}}, "SKILL CUSTOM HP COST ERROR").to_num();
    }
    return std::max(0.0, std::floor(cost));
}

bool GameBattlerBase::base_can_pay_skill_cost(const ItemRef& skill) {
    if (!can_pay_skill_hp_cost(skill)) return false;
    return tp_ >= skill_tp_cost(skill) && mp_ >= skill_mp_cost(skill);
}

bool GameBattlerBase::can_pay_skill_cost(const ItemRef& skill) { return base_can_pay_skill_cost(skill); }

void GameBattlerBase::base_pay_skill_cost(const ItemRef& skill) {
    request_status_refresh();
    mp_ -= skill_mp_cost(skill);
    tp_ -= skill_tp_cost(skill);
    pay_skill_hp_cost(skill);
    pay_skill_eval_cost(skill);
}

void GameBattlerBase::pay_skill_cost(const ItemRef& skill) { base_pay_skill_cost(skill); }

bool GameBattlerBase::is_occasion_ok(const Value& item) {
    int o = item["occasion"].as_int();
    if (party_in_battle()) return o == 0 || o == 1;
    return o == 0 || o == 2;
}

bool GameBattlerBase::meets_usable_item_conditions(const ItemRef& item) { return can_move() && is_occasion_ok(item.data()); }

bool GameBattlerBase::meets_skill_conditions(const ItemRef& skill) {
    if (is_chain_skill_restrict(skill)) return false;
    const Value& s = skill.data();
    bool base = meets_usable_item_conditions(skill) && is_skill_wtype_ok(s) && can_pay_skill_cost(skill) && !is_skill_sealed(skill.id) &&
                !is_skill_type_sealed(s["stypeId"].as_int());
    if (!base) return false;
    if (!s.is_obj()) return false;
    if (!no_hidden_skill_conditions_met(skill)) return false;
    return meets_skill_conditions_eval(skill);
}

bool GameBattlerBase::meets_item_conditions(const ItemRef& item) { return meets_usable_item_conditions(item) && game_party().has_item(item, false); }

bool GameBattlerBase::can_use(const ItemRef& item) {
    if (!item.valid()) return false;
    if (item.is_skill()) return meets_skill_conditions(item);
    if (item.is_item()) return meets_item_conditions(item);
    return false;
}

bool GameBattlerBase::can_equip(const ItemRef& item) {
    if (!item.valid()) return false;
    if (item.is_weapon()) return can_equip_weapon(item.data());
    if (item.is_armor()) return can_equip_armor(item.data());
    return false;
}

bool GameBattlerBase::can_equip_weapon(const Value& item) { return is_equip_wtype_ok(item["wtypeId"].as_int()) && !is_equip_type_sealed(item["etypeId"].as_int()); }
bool GameBattlerBase::can_equip_armor(const Value& item) { return is_equip_atype_ok(item["atypeId"].as_int()) && !is_equip_type_sealed(item["etypeId"].as_int()); }

bool GameBattlerBase::map_regen_update_check(const char* type) {
    if (party_in_battle()) return true;
    std::string t = type;
    if (t == "hp") return yanfly_param("RefreshUpdateHp").truthy();
    if (t == "mp") return yanfly_param("RefreshUpdateMp").truthy();
    if (t == "tp") return yanfly_param("RefreshUpdateTp").truthy();
    return false;
}

bool GameBattlerBase::no_hidden_skill_conditions_met(const ItemRef& skill) {
    const Value& s = skill.data();
    if (!s.is_obj()) return false;
    if (is_enemy()) return true;
    const Value& hide = s["hideIfLearnedSkill"];
    for (size_t i = 0; i < hide.size(); i++) {
        if (is_learned_skill(hide[i].as_int())) return false;
    }
    if (s["hideInBattle"].truthy() && party_in_battle()) return false;
    if (s["hideInField"].truthy() && !party_in_battle()) return false;
    if (!meets_custom_show_eval(skill)) return false;
    return true;
}

bool GameBattlerBase::meets_custom_show_eval(const ItemRef& skill) {
    const Value& s = skill.data();
    if (s["costShowEval"].string().empty()) return true;
    const Value* ast = obj_ast(s, "costShowEval");
    if (!ast) return true;
    return eval_note_code(*ast, "visible", js::Val::boolean(true), {{"item", skill.to_js()}}, "SKILL CUSTOM SHOW EVAL ERROR").truthy();
}

bool GameBattlerBase::meets_skill_conditions_eval(const ItemRef& skill) {
    const Value& s = skill.data();
    if (s["requireEval"].string().empty()) return true;
    const Value* ast = obj_ast(s, "requireEval");
    if (!ast) return true;
    return eval_note_code(*ast, "value", js::Val::boolean(true), {{"item", skill.to_js()}}, "SKILL CUSTOM REQUIRE EVAL ERROR").truthy();
}

bool GameBattlerBase::can_pay_skill_hp_cost(const ItemRef& skill) {
    double cost = skill_hp_cost(skill);
    if (cost <= 0) return true;
    return hp_ > cost;
}

void GameBattlerBase::pay_skill_hp_cost(const ItemRef& skill) { hp_ -= skill_hp_cost(skill); }

void GameBattlerBase::pay_skill_eval_cost(const ItemRef& skill) {
    const Value& s = skill.data();
    if (s["executeEval"].string().empty()) return;
    if (const Value* ast = obj_ast(s, "executeEval")) eval_note_code(*ast, nullptr, js::Val(), {{"item", skill.to_js()}}, "SKILL PAY COST EVAL ERROR");
}

GameBattlerBase* GameBattlerBase::state_origin_battler(int id) {
    auto it = state_origin.find(id);
    if (it == state_origin.end()) return this;
    if (it->second.first == 0) return game_actors().actor(it->second.second);
    if (it->second.first == 1) {
        auto m = game_troop().unit_members();
        int i = it->second.second;
        return i >= 0 && i < (int)m.size() ? m[(size_t)i] : nullptr;
    }
    return this;
}

void GameBattlerBase::clear_enemy_state_origins() {
    for (auto it = state_origin.begin(); it != state_origin.end();) {
        if (it->second.first == 1) it = state_origin.erase(it);
        else ++it;
    }
}

double GameBattlerBase::state_turns_of(int id) const {
    auto it = state_turns.find(id);
    return it == state_turns.end() ? NAN : it->second;
}

double GameBattlerBase::max_buff_limit(int id) {
    double value = yanfly_param("BSCDefaultLimit").num();
    for (const Value* s : states()) {
        const Value& mb = (*s)["maxBuff"];
        if (mb.is_arr()) value += mb[id].num();
    }
    return value;
}

double GameBattlerBase::max_debuff_limit(int id) {
    double value = -1 * yanfly_param("BSCDefaultLimit").num();
    for (const Value* s : states()) {
        const Value& mb = (*s)["maxDebuff"];
        if (mb.is_arr()) value -= mb[id].num();
    }
    return value;
}

void GameBattlerBase::set_state_counter(int id, const js::Val& v) {
    state_counter[id] = v;
    refresh();
}

void GameBattlerBase::add_state_counter(int id, double v) {
    js::Val cur = get_state_counter(id);
    double base = cur.truthy() ? cur.to_num() : 0;
    set_state_counter(id, js::Val::num(v + base));
}

js::Val GameBattlerBase::get_state_counter(int id) const {
    auto it = state_counter.find(id);
    return it == state_counter.end() ? js::Val() : it->second;
}

bool GameBattlerBase::is_custom_clear_states() const { return game_temp().death_state_clear || game_temp().recover_all_clear; }

void GameBattlerBase::retain_custom_clear_states(const std::vector<int>& states, const std::map<int, double>& turns) {
    bool removed = false;
    for (int id : states) {
        const Value& st = data::states()[id];
        if (!st.is_obj()) continue;
        auto keep = [&]() {
            states_.push_back(id);
            auto t = turns.find(id);
            if (t != turns.end()) state_turns[id] = t->second;
            else state_turns.erase(id);
            removed = true;
        };
        if (game_temp().death_state_clear && category_has(st, "BYPASS DEATH REMOVAL")) keep();
        if (game_temp().recover_all_clear && category_has(st, "BYPASS RECOVER ALL REMOVAL")) keep();
    }
    if (removed) sort_states();
}

std::vector<int> GameBattlerBase::passive_states_raw() { return passive_states_raw_impl(); }

std::vector<int> GameBattlerBase::passive_states_raw_impl() {
    std::vector<int> arr;
    if (party_in_battle()) {
        for (int id : valid_aura_state_ids()) arr.push_back(id);
    }
    std::vector<int> uniq;
    for (int id : arr) {
        if (!contains_int(uniq, id)) uniq.push_back(id);
    }
    return uniq;
}

std::vector<const Value*> GameBattlerBase::passive_states() {
    std::vector<const Value*> arr;
    for (int id : passive_states_raw()) {
        const Value* st = &data::states()[id];
        if (st->is_obj() && std::find(arr.begin(), arr.end(), st) != arr.end()) continue;
        arr.push_back(st);
    }
    return arr;
}

std::vector<int> GameBattlerBase::get_passive_state_data(const Value* obj) {
    std::vector<int> arr;
    if (!obj || !obj->is_obj()) return arr;
    const Value& ps = (*obj)["passiveStates"];
    if (!ps.is_arr()) return arr;
    for (size_t i = 0; i < ps.size(); i++) {
        int id = ps[i].as_int();
        if (!meet_passive_state_condition(id)) continue;
        arr.push_back(id);
    }
    return arr;
}

bool GameBattlerBase::meet_passive_state_condition(int id) {
    if (contains_int(check_passive, id)) return false;
    const Value& st = data::states()[id];
    if (!st.is_obj()) return false;
    if (st["passiveCondition"].string() != "") {
        if (!passive_state_conditions(st)) return false;
    }
    if (st["passiveConditionEval"].string() == "") return true;
    return passive_state_condition_eval(st);
}

bool GameBattlerBase::passive_state_conditions(const Value& state) {
    int id = state["id"].as_int();
    check_passive.push_back(id);
    bool condition = true;
    if (const Value* ast = obj_ast(state, "passiveCondition")) {
        condition = eval_note_code(*ast, "condition", js::Val::boolean(true), {}, "PASSIVE STATE CUSTOM CONDITION ERROR").truthy();
    }
    auto it = std::find(check_passive.begin(), check_passive.end(), id);
    if (it != check_passive.end()) check_passive.erase(it);
    if (!condition) return false;
    check_passive.push_back(id);
    const Value& cases = state["passiveConditionCases"];
    for (size_t i = 0; i < cases.size(); i++) {
        if (!passive_condition_case_eval(state, cases[i])) {
            auto it2 = std::find(check_passive.begin(), check_passive.end(), id);
            if (it2 != check_passive.end()) check_passive.erase(it2);
            return false;
        }
    }
    auto it3 = std::find(check_passive.begin(), check_passive.end(), id);
    if (it3 != check_passive.end()) check_passive.erase(it3);
    return true;
}

bool GameBattlerBase::passive_state_condition_eval(const Value& state) {
    int id = state["id"].as_int();
    check_passive.push_back(id);
    bool condition = true;
    if (const Value* ast = obj_ast(state, "passiveConditionEval")) {
        condition = eval_note_code(*ast, "condition", js::Val::boolean(true), {}, "PASSIVE STATE CUSTOM CONDITION ERROR").truthy();
    }
    auto it = std::find(check_passive.begin(), check_passive.end(), id);
    if (it != check_passive.end()) check_passive.erase(it);
    return condition;
}

bool GameBattlerBase::passive_condition_case_eval(const Value& state, const Value& data) {
    const Value* ast = yanfly_ast("LunPasCasEffect");
    if (!ast) return true;
    js::ScopePtr scope = std::make_shared<js::Scope>();
    js::Val self = to_js();
    for (const char* n : {"a", "user", "subject", "b", "target"}) scope->declare(js::atom(n), self);
    scope->declare(js::atom("condition"), js::Val::boolean(true));
    scope->declare(js::atom("stateId"), js::Val::num(state["id"].num()));
    scope->declare(js::atom("s"), switches_proxy());
    scope->declare(js::atom("v"), variables_proxy());
    scope->declare(js::atom("skip"), js::Val::boolean(false));
    scope->declare(js::atom("value"), js::Val::num(0));
    scope->declare(js::atom("rate"), js::Val::num(1));
    scope->declare(js::atom("data"), js::value_to_js(data));
    scope->declare(js::atom("state"), js::data_view(state, data::keep(), true));
    js::Val r;
    if (!js::vm().run(*ast, self, scope, &r, data::keep())) script_error("PASSIVE CONDITION CASES EFFECT ERROR", js::vm().last_error);
    if (scope->find(js::atom("skip"))->truthy()) return true;
    return scope->find(js::atom("condition"))->truthy();
}

void GameBattlerBase::sort_passive_states(std::vector<const Value*>& arr) {
    std::stable_sort(arr.begin(), arr.end(), [](const Value* a, const Value* b) {
        double p1 = (*a)["priority"].num(), p2 = (*b)["priority"].num();
        if (p1 != p2) return p2 < p1;
        return false;
    });
}

bool GameBattlerBase::is_passive_state_affected(int id) { return contains_int(passive_states_raw(), id); }

std::vector<int> GameBattlerBase::valid_aura_state_ids() {
    std::vector<int> arr;
    for (int id : aura_state_ids()) {
        if (meet_aura_state_condition(id)) arr.push_back(id);
    }
    return arr;
}

std::vector<int> GameBattlerBase::aura_state_ids() {
    if (game_temp().is_gathering_aura_data) return {};
    std::vector<int> arr;
    GameUnit& friends = friends_unit();
    GameUnit& opponents = opponents_unit();
    game_temp().is_gathering_aura_data = true;
    auto add = [&](const std::vector<int>& v) { arr.insert(arr.end(), v.begin(), v.end()); };
    add(friends.aura_state_type_ids("all"));
    add(opponents.aura_state_type_ids("all"));
    add(friends.aura_state_type_ids("friends"));
    add(opponents.aura_state_type_ids("opponents"));
    if (check_aura_alive()) {
        add(friends.aura_state_type_ids("aliveAll"));
        add(opponents.aura_state_type_ids("aliveAll"));
        add(friends.aura_state_type_ids("aliveFriends"));
        add(opponents.aura_state_type_ids("aliveOpponents"));
    } else if (check_aura_dead()) {
        add(friends.aura_state_type_ids("deadAll"));
        add(opponents.aura_state_type_ids("deadAll"));
        add(friends.aura_state_type_ids("deadFriends"));
        add(opponents.aura_state_type_ids("deadOpponents"));
    }
    game_temp().is_gathering_aura_data = false;
    return arr;
}

std::vector<int> GameBattlerBase::get_aura_state_type_id(const std::string& type) {
    std::vector<int> arr;
    for (const Value* s : states()) {
        const Value& aura = (*s)["aura"];
        if (!aura.is_obj()) continue;
        bool is_aura = false;
        for (const Member& m : aura.members()) is_aura = is_aura || m.v.size() > 0;
        if (!is_aura) continue;
        const Value& list = aura[type.c_str()];
        for (size_t i = 0; i < list.size(); i++) arr.push_back(list[i].as_int());
    }
    return arr;
}

bool GameBattlerBase::meet_aura_state_condition(int id) {
    if (contains_int(check_aura, id)) return false;
    const Value& st = data::states()[id];
    if (!st.is_obj()) return false;
    if (st["auraConditionEval"].string() == "") return true;
    check_aura.push_back(id);
    bool condition = true;
    if (const Value* ast = obj_ast(st, "auraConditionEval")) {
        condition = eval_note_code(*ast, "condition", js::Val::boolean(true), {}, "PASSIVE AURA CUSTOM CONDITION ERROR").truthy();
    }
    auto it = std::find(check_aura.begin(), check_aura.end(), id);
    if (it != check_aura.end()) check_aura.erase(it);
    return condition;
}

void GameBattlerBase::update_auras(int id) {
    const Value& st = data::states()[id];
    if (!st.is_obj()) return;
    const Value& aura = st["aura"];
    if (aura["all"].size() + aura["friends"].size() > 0 || aura["party"].size() > 0) game_party().refresh_members();
    if (aura["all"].size() + aura["opponents"].size() > 0 || aura["troop"].size() > 0) game_troop().refresh_members();
}

int GameBattlerBase::total_buffs() const {
    int v = 0;
    for (int i = 0; i < 8; i++) v += is_buff_affected(i) ? 1 : 0;
    return v;
}

int GameBattlerBase::total_debuffs() const {
    int v = 0;
    for (int i = 0; i < 8; i++) v += is_debuff_affected(i) ? 1 : 0;
    return v;
}

bool GameBattlerBase::is_chain_skill_restrict(const ItemRef& skill) {
    if (!is_actor()) return false;
    if (!party_in_battle()) return false;
    if (!skill.data()["meta"]["ChainOnly"].truthy()) return false;
    return !active_chain_enabled;
}

bool GameBattlerBase::states_flag(const char* key) {
    for (const Value* s : states()) {
        if ((*s)[key].truthy()) return true;
    }
    return false;
}

bool GameBattlerBase::taunt_physical() { return states_flag("physTaunt"); }
bool GameBattlerBase::taunt_magical() { return states_flag("magicTaunt"); }
bool GameBattlerBase::taunt_certain() { return states_flag("certainTaunt"); }
bool GameBattlerBase::null_taunt_physical() { return states_flag("nullPhysTaunt"); }
bool GameBattlerBase::null_taunt_magical() { return states_flag("nullMagicTaunt"); }
bool GameBattlerBase::null_taunt_certain() { return states_flag("nullCertainTaunt"); }
bool GameBattlerBase::ignore_taunt_physical() { return states_flag("ignorePhysTaunt"); }
bool GameBattlerBase::ignore_taunt_magical() { return states_flag("ignoreMagicTaunt"); }
bool GameBattlerBase::ignore_taunt_certain() { return states_flag("ignoreCertainTaunt"); }

void GameBattlerBase::update_state_turn_timing(int timing) {
    if (game_troop().is_event_running()) return;
    std::vector<int> removed;
    for (size_t i = 0; i < states_.size(); ++i) {
        int id = states_[i];
        const Value& st = data::states()[id];
        if (!st.is_obj()) continue;
        if (st["autoRemovalTiming"].as_int() != timing) continue;
        auto t = state_turns.find(id);
        if (t == state_turns.end() || !(t->second != 0 && !std::isnan(t->second))) continue;
        GameBattler* self = dynamic_cast<GameBattler*>(this);
        auto& fst = self->free_state_turn;
        auto f = std::find(fst.begin(), fst.end(), id);
        if (f != fst.end()) fst.erase(f);
        else t->second -= 1;
        if (t->second <= 0) removed.push_back(id);
    }
    game_temp().custom_leave_effect_eval = true;
    for (int id : removed) dynamic_cast<GameBattler*>(this)->remove_state(id);
    game_temp().custom_leave_effect_eval = false;
}

GameBattler::GameBattler() : result_(new GameActionResult()) {}

void GameBattler::init_members() {
    GameBattlerBase::init_members();
    actions.clear();
    speed_ = 0;
    result_.reset(new GameActionResult());
    action_state.clear();
    last_target_index = 0;
    animations.clear();
    damage_popup.clear();
    effect_type.clear();
    motion_type.clear();
    weapon_image_id = 0;
    motion_refresh = false;
    selected = false;
}

void GameBattler::refresh() {
    has_counter_total_cache = false;
    has_group_defeat_cache = false;
    GameBattlerBase::refresh();
    if (hp() == 0) add_state(death_state_id());
    else remove_state(death_state_id());
}

void GameBattler::base_add_state(int id) {
    if (is_state_addable(id)) {
        if (!is_state_affected(id)) {
            add_new_state(id);
            refresh();
        }
        reset_state_counts(id);
        result_->push_added_state(id);
    }
}

void GameBattler::add_state(int id) {
    bool addable_gtp = is_state_addable(id) && !is_state_affected(id);
    bool addable = is_state_addable(id);
    base_add_state(id);
    if (party_in_battle() && battle::subject() == this && data::states()[id]["autoRemovalTiming"].as_int() == 1 &&
        data::states()[id]["reapplyRules"].as_int() != 0) {
        free_state_turn.push_back(id);
    }
    if (addable) {
        set_state_origin(id, nullptr);
        add_state_effects(id);
    }
    GameActor* actor = dynamic_cast<GameActor*>(this);
    bool paper_bag = actor && actor->is_equipped(ItemRef::armor(49));
    if (paper_bag && is_emotional_state(id)) no_effect_message = js::Val::boolean(true);
    if (is_state_state(id) && !addable_gtp) no_state_message = js::Val::boolean(true);
    on_add_state_extra(id, addable_gtp);
}

bool GameBattler::is_state_addable(int id) {
    if (is_passive_state_affected(id)) return false;
    const Value& st = data::states()[id];
    if (st.is_obj() && category_has(st, "BYPASS DEATH REMOVAL")) {
        return !is_state_resist(id) && !result_->is_state_removed(id) && !is_state_restrict(id);
    }
    return is_alive() && st.is_obj() && !is_state_resist(id) && !result_->is_state_removed(id) && !is_state_restrict(id);
}

bool GameBattler::is_state_restrict(int id) { return data::states()[id]["removeByRestriction"].truthy() && is_restricted(); }

void GameBattler::on_restrict() {
    clear_actions();
    for (const Value* s : states()) {
        if ((*s)["removeByRestriction"].truthy()) remove_state((*s)["id"].as_int());
    }
}

void GameBattler::remove_state(int id) {
    if (is_passive_state_affected(id)) return;
    bool affected = is_state_affected(id);
    if (is_state_affected(id)) {
        if (id == death_state_id()) revive();
        erase_state(id);
        refresh();
        result_->push_removed_state(id);
    }
    remove_state_counter(id);
    if (affected) {
        remove_state_effects(id);
        clear_state_origin(id);
    }
}

void GameBattler::add_buff(int id, double turns) {
    if (is_alive()) {
        increase_buff(id);
        if (is_buff_affected(id)) overwrite_buff_turns(id, turns);
        result_->push_added_buff(id);
        refresh();
    }
}

void GameBattler::add_debuff(int id, double turns) {
    if (is_alive()) {
        decrease_buff(id);
        if (is_debuff_affected(id)) overwrite_buff_turns(id, turns);
        result_->push_added_debuff(id);
        refresh();
    }
}

void GameBattler::remove_buff(int id) {
    if (is_alive() && is_buff_or_debuff_affected(id)) {
        erase_buff(id);
        result_->push_removed_buff(id);
        refresh();
    }
}

void GameBattler::remove_battle_states() {
    for (const Value* s : states()) {
        if ((*s)["removeAtBattleEnd"].truthy()) remove_state((*s)["id"].as_int());
    }
}

void GameBattler::remove_all_buffs() {
    for (int i = 0; i < buff_length(); i++) remove_buff(i);
}

void GameBattler::remove_states_auto(int timing) {
    game_temp().custom_leave_effect_eval = true;
    for (const Value* s : states()) {
        int id = (*s)["id"].as_int();
        if (is_state_expired(id) && (*s)["autoRemovalTiming"].as_int() == timing) remove_state(id);
    }
    game_temp().custom_leave_effect_eval = false;
}

void GameBattler::remove_buffs_auto() {
    for (int i = 0; i < buff_length(); i++) {
        if (is_buff_expired(i)) remove_buff(i);
    }
}

void GameBattler::remove_states_by_damage() {
    for (const Value* s : states()) {
        if ((*s)["removeByDamage"].truthy() && rng::below(100) < (*s)["chanceByDamage"].num()) remove_state((*s)["id"].as_int());
    }
}

void GameBattler::consume_item(const ItemRef& item) { game_party().consume_item(item); }

void GameBattler::gain_hp(double v) {
    result_->hp_damage = -v;
    result_->hp_affected = true;
    set_hp(hp() + v);
}

void GameBattler::gain_mp(double v) {
    result_->mp_damage = -v;
    set_mp(mp() + v);
}

void GameBattler::gain_tp(double v) {
    result_->tp_damage = -v;
    set_tp(tp() + v);
}

void GameBattler::set_action_state(const std::string& s) {
    action_state = s;
    request_motion_refresh();
}

void GameBattler::custom_effect_eval(int state_id, const std::string& type) {
    const Value& st = data::states()[state_id];
    if (!st.is_obj()) return;
    if (st["customEffectEval"][type.c_str()].string().empty()) return;
    const Value* ast = obj_ast(st, "customEffectEval." + type);
    if (!ast) return;
    GameBattlerBase* origin = state_origin_battler(state_id);
    eval_note_code(*ast, nullptr, js::Val(), {{"origin", origin ? origin->to_js() : js::Val()}}, "CUSTOM STATE CODE ERROR");
}

void GameBattler::add_state_effects(int id) { custom_effect_eval(id, "addState"); }

void GameBattler::remove_state_effects(int id) {
    custom_effect_eval(id, "removeState");
    if (game_temp().custom_leave_effect_eval) leave_state_effects(id);
}

void GameBattler::leave_state_effects(int id) { custom_effect_eval(id, "leaveState"); }

void GameBattler::remove_state_category_all(const std::string& category0) {
    std::string category = upper_trim(category0);
    std::vector<int> states_copy = states_;
    for (int id : states_copy) {
        const Value& st = data::states()[id];
        if (!st.is_obj()) continue;
        if (category_has(st, category)) remove_state(id);
    }
}

void GameBattler::remove_state_category(const std::string& category0, int count) {
    std::string category = upper_trim(category0);
    std::vector<int> states_copy = states_;
    int value = 0;
    for (int id : states_copy) {
        if (value >= count) return;
        const Value& st = data::states()[id];
        if (!st.is_obj()) continue;
        if (category_has(st, category)) {
            remove_state(id);
            value += 1;
        }
    }
}

bool GameBattler::is_state_category_affected(const std::string& category0) {
    std::string category = upper_trim(category0);
    for (const Value* s : states()) {
        if (category_has(*s, category)) return true;
    }
    return false;
}

int GameBattler::get_state_category_affected_count(const std::string& category0) {
    std::string category = upper_trim(category0);
    int count = 0;
    for (const Value* s : states()) {
        if (category_has(*s, category)) count++;
    }
    return count;
}

bool GameBattler::is_group_defeat_affected() {
    if (has_group_defeat_cache) return group_defeat_cache;
    group_defeat_cache = false;
    for (const Value* s : states()) {
        if (category_has(*s, "GROUP DEFEAT")) {
            group_defeat_cache = true;
            break;
        }
    }
    has_group_defeat_cache = true;
    return group_defeat_cache;
}

bool GameBattler::is_emotion_affected(const std::string& type0) {
    std::string type = to_lower(type0);
    bool angry_custom = is_state_affected(119) || is_state_affected(120) || is_state_affected(121);
    bool happy_custom = is_state_affected(197) || is_state_affected(122) || is_state_affected(123);
    bool sad_custom = is_state_affected(124) || is_state_affected(125) || is_state_affected(126);
    if (type == "happy") return is_state_affected(6) || is_state_affected(7) || is_state_affected(8) || happy_custom;
    if (type == "sad") return is_state_affected(10) || is_state_affected(11) || is_state_affected(12) || sad_custom;
    if (type == "angry") return is_state_affected(14) || is_state_affected(15) || is_state_affected(16) || angry_custom;
    if (type == "afraid") return is_state_affected(18);
    return false;
}

bool GameBattler::is_any_emotion_affected() {
    return is_emotion_affected("happy") || is_emotion_affected("angry") || is_emotion_affected("sad") || is_emotion_affected("afraid");
}

bool GameBattler::is_emotional_state(int id) {
    static const int ids[] = {6, 7, 8, 10, 11, 12, 14, 15, 16, 18};
    return std::find(std::begin(ids), std::end(ids), id) != std::end(ids);
}

bool GameBattler::is_state_state(int id) { return id >= 89 && id <= 106; }

void GameBattler::set_state_origin(int id, GameBattlerBase* battler) {
    if (!party_in_battle()) return;
    if (!battler) {
        battler = battle::subject();
        if (!battler) return;
    }
    if (battler->is_actor()) state_origin[id] = {0, static_cast<GameActor*>(battler)->actor_id()};
    else state_origin[id] = {1, battler->index()};
}

void GameBattler::start_animation(int id, bool mirror, double delay) {
    if (!data::animations()[id].is_obj()) return;
    animations.push_back(Anim{id, mirror, delay});
}

void GameBattler::start_damage_popup() {
    GameActionResult& r = *result_;
    if (r.missed || r.evaded) {
        GameActionResult c = r;
        c.hp_affected = false;
        c.mp_damage = 0;
        damage_popup.push_back(c);
    }
    if (r.hp_affected) {
        GameActionResult c = r;
        c.mp_damage = 0;
        damage_popup.push_back(c);
    }
    if (r.mp_damage != 0) {
        GameActionResult c = r;
        c.hp_affected = false;
        damage_popup.push_back(c);
    }
}

GameActor::GameActor(int id) {
    GameActor::init_members();
    horror::clear(horror_filters);
    setup(id);
}

void GameActor::init_members() {
    GameBattler::init_members();
    actor_id_ = 0;
    name_.clear();
    nickname_.clear();
    class_id = 0;
    level = 0;
    character_name_.clear();
    character_index_ = 0;
    face_name_.clear();
    face_index_ = 0;
    battler_name_.clear();
    exp_.clear();
    skills_.clear();
    equips_.clear();
    action_input_index = 0;
    last_menu_skill = GameItem();
    last_battle_skill = GameItem();
    last_command_symbol.clear();
    character_movement_graphics = MovementGraphics();
    equipped_skills.clear();
    locked_equipment_slots.clear();
    disabled_battle_commands.clear();
    faraway_actor_id = 0;
    use_victory_face = false;
    battle_face_name.clear();
}

const Value& GameActor::actor() const { return data::actors()[actor_id_]; }

void GameActor::setup(int id) {
    const Value& a = data::actors()[id];
    actor_id_ = id;
    name_ = a["name"].string();
    nickname_ = a["nickname"].string();
    profile_ = a["profile"].string();
    class_id = a["classId"].as_int();
    level = a["initialLevel"].as_int();
    init_images();
    init_exp();
    init_skills();
    init_equips(a["equips"]);
    clear_param_plus();
    recover_all();
    init_battle_commands_list();
    std::string far = actor_meta(a, "FarawayActorId");
    if (!far.empty()) faraway_actor_id = (int)js_number(far);
}

std::string GameActor::name() const {
    if (actor_id_ != 8) return name_;
    const Value& def = data::lang_entry("XX_BLUE.Omori_Name_Input");
    std::string fallback = def["defaultname"].string();
    if (!is_text_in_current_language(name_)) return fallback;
    return name_;
}

void GameActor::clear_states() {
    GameBattlerBase::clear_states();
    state_steps.clear();
}

void GameActor::erase_state(int id) {
    GameBattlerBase::erase_state(id);
    state_steps.erase(id);
}

void GameActor::reset_state_counts(int id) {
    GameBattlerBase::reset_state_counts(id);
    state_steps[id] = data::states()[id]["stepsToRemove"].num();
}

void GameActor::init_images() {
    const Value& a = actor();
    character_name_ = a["characterName"].string();
    character_index_ = a["characterIndex"].as_int();
    face_name_ = a["faceName"].string();
    face_index_ = a["faceIndex"].as_int();
    battler_name_ = a["battlerName"].string();
}

double GameActor::exp_for_level(int lvl) const {
    const Value& c = current_class();
    const Value& e = c["expParams"];
    double basis = e[0].num(), extra = e[1].num(), acc_a = e[2].num(), acc_b = e[3].num();
    return std::round(basis * (std::pow(lvl - 1, 0.9 + acc_a / 250)) * lvl * (lvl + 1) / (6 + std::pow(lvl, 2) / 50 / acc_b) + (lvl - 1) * extra);
}

void GameActor::init_exp() { exp_[class_id] = current_level_exp(); }

double GameActor::current_exp() const {
    auto it = exp_.find(class_id);
    return it == exp_.end() ? NAN : it->second;
}

int GameActor::max_level() const { return actor()["maxLevel"].as_int(); }

bool GameActor::is_max_level() const {
    if (max_level() == 0) return false;
    return level >= max_level();
}

void GameActor::init_skills() {
    skills_.clear();
    const Value& l = current_class()["learnings"];
    for (size_t i = 0; i < l.size(); i++) {
        if (l[i]["level"].as_int() <= level) learn_skill(l[i]["skillId"].as_int());
    }
    equipped_skills = {0, 0, 0, 0};
    locked_equipped_skills = {false, false, false, false};
    locked_skills.clear();
}

void GameActor::init_equips(const Value& equips_data) {
    std::vector<ItemRef> items;
    const Value& types = data::system()["equipTypes"];
    for (size_t i = 0; i < equips_data.size(); i++) {
        int id = equips_data[i].as_int();
        if (id <= 0) continue;
        bool weapon = types[i + 1].string() == types[1].string() || (i == 1 && is_dual_wield());
        items.push_back(weapon ? ItemRef::weapon(id) : ItemRef::armor(id));
    }
    std::vector<int> slots = equip_slots();
    equips_.assign(slots.size(), GameItem());
    for (size_t i = 0; i < slots.size(); ++i) {
        int slot_type = slots[i];
        ItemRef found;
        size_t k = 0;
        for (; k < items.size(); ++k) {
            const ItemRef& e = items[k];
            if (!e.valid()) continue;
            if (slot_type == 1 && e.is_weapon()) {
                found = e;
                break;
            }
            if (e.data()["etypeId"].as_int() == slot_type) {
                found = e;
                break;
            }
        }
        if (found.valid()) items[k] = ItemRef();
        if (can_equip(found)) equips_[i].set_object(found);
    }
    release_unequippable_items(true);
    recover_all();
    refresh();
}

std::vector<int> GameActor::equip_slots() {
    std::vector<int> slots;
    const Value& s = current_class()["equipSlots"];
    for (size_t i = 0; i < s.size(); i++) slots.push_back(s[i].as_int());
    if (slots.size() >= 2 && is_dual_wield()) slots[1] = 1;
    return slots;
}

std::vector<ItemRef> GameActor::equips() {
    size_t n = current_class()["equipSlots"].size();
    if (equips_.size() < n) equips_.resize(n);
    std::vector<ItemRef> out;
    for (auto& e : equips_) out.push_back(e.object());
    return out;
}

std::vector<ItemRef> GameActor::weapons() {
    std::vector<ItemRef> out;
    for (auto& e : equips()) {
        if (e.valid() && e.is_weapon()) out.push_back(e);
    }
    return out;
}

std::vector<ItemRef> GameActor::armors() {
    std::vector<ItemRef> out;
    for (auto& e : equips()) {
        if (e.valid() && e.is_armor()) out.push_back(e);
    }
    return out;
}

bool GameActor::has_weapon(const ItemRef& w) {
    for (auto& e : weapons()) {
        if (e == w) return true;
    }
    return false;
}

bool GameActor::has_armor(const ItemRef& a) {
    for (auto& e : armors()) {
        if (e == a) return true;
    }
    return false;
}

bool GameActor::is_equip_change_ok(int slot) {
    std::vector<int> slots = equip_slots();
    int type = slot >= 0 && slot < (int)slots.size() ? slots[(size_t)slot] : 0;
    return !is_equip_type_locked(type) && !is_equip_type_sealed(type);
}

void GameActor::change_equip(int slot, const ItemRef& item) {
    if (slot < 0) return;
    if ((int)equips_.size() <= slot) equips_.resize((size_t)slot + 1);
    std::vector<ItemRef> eq = equips();
    std::vector<int> slots = equip_slots();
    ItemRef old = slot < (int)eq.size() ? eq[(size_t)slot] : ItemRef();
    int stype = slot < (int)slots.size() ? slots[(size_t)slot] : -1;
    if (trade_item_with_party(item, old) && (!item.valid() || stype == item.data()["etypeId"].as_int())) {
        equips_[(size_t)slot].set_object(item);
        refresh();
    }
}

void GameActor::force_change_equip(int slot, const ItemRef& item) {
    if (slot < 0) return;
    if ((int)equips_.size() <= slot) {
        equips_.resize((size_t)slot + 1);
        std::vector<int> slots = equip_slots();
        equips_[(size_t)slot].set_equip(slot < (int)slots.size() && slots[(size_t)slot] == 1, 0);
    }
    equips_[(size_t)slot].set_object(item);
    release_unequippable_items(true);
    refresh();
}

bool GameActor::trade_item_with_party(const ItemRef& new_item, const ItemRef& old_item) {
    if (new_item.valid() && !game_party().has_item(new_item, false)) return false;
    game_party().gain_item(old_item, 1);
    game_party().lose_item(new_item, 1);
    return true;
}

void GameActor::change_equip_by_id(int etype, int item_id) {
    int slot = etype - 1;
    std::vector<int> slots = equip_slots();
    if (slot >= 0 && slot < (int)slots.size() && slots[(size_t)slot] == 1) change_equip(slot, ItemRef::weapon(item_id));
    else change_equip(slot, ItemRef::armor(item_id));
}

bool GameActor::is_equipped(const ItemRef& item) {
    if (!item.valid()) return false;
    for (auto& e : equips()) {
        if (e.valid() && e == item) return true;
    }
    return false;
}

void GameActor::discard_equip(const ItemRef& item) {
    std::vector<ItemRef> eq = equips();
    for (size_t i = 0; i < eq.size(); i++) {
        if (eq[i].valid() && eq[i] == item) {
            equips_[i].set_object(ItemRef());
            return;
        }
    }
}

void GameActor::release_unequippable_items(bool forcing) {
    for (;;) {
        std::vector<int> slots = equip_slots();
        std::vector<ItemRef> eq = equips();
        bool changed = false;
        for (size_t i = 0; i < eq.size(); i++) {
            const ItemRef& item = eq[i];
            if (item.valid() && (!can_equip(item) || i >= slots.size() || item.data()["etypeId"].as_int() != slots[i])) {
                if (!forcing) trade_item_with_party(ItemRef(), item);
                equips_[i].set_object(ItemRef());
                changed = true;
            }
        }
        if (!changed) break;
    }
}

void GameActor::clear_equipments() {
    size_t n = equip_slots().size();
    for (size_t i = 0; i < n; i++) {
        if (is_equip_change_ok((int)i)) change_equip((int)i, ItemRef());
    }
}

ItemRef GameActor::best_equip_item(int slot) {
    std::vector<int> slots = equip_slots();
    int etype = slot >= 0 && slot < (int)slots.size() ? slots[(size_t)slot] : -1;
    ItemRef best;
    double best_perf = -1000;
    for (auto& item : game_party().equip_items()) {
        if (item.data()["etypeId"].as_int() != etype || !can_equip(item)) continue;
        double perf = 0;
        const Value& p = item.data()["params"];
        for (size_t k = 0; k < p.size(); k++) perf += p[k].num();
        if (perf > best_perf) {
            best_perf = perf;
            best = item;
        }
    }
    return best;
}

void GameActor::optimize_equipments() {
    size_t n = equip_slots().size();
    clear_equipments();
    for (size_t i = 0; i < n; i++) {
        if (is_equip_change_ok((int)i)) change_equip((int)i, best_equip_item((int)i));
    }
}

bool GameActor::is_skill_wtype_ok(const Value& skill) {
    int w1 = skill["requiredWtypeId1"].as_int(), w2 = skill["requiredWtypeId2"].as_int();
    return (w1 == 0 && w2 == 0) || (w1 > 0 && is_wtype_equipped(w1)) || (w2 > 0 && is_wtype_equipped(w2));
}

bool GameActor::is_wtype_equipped(int wtype) {
    for (auto& w : weapons()) {
        if (w.data()["wtypeId"].as_int() == wtype) return true;
    }
    return false;
}

void GameActor::refresh() {
    has_anchor_x = has_anchor_y = false;
    release_unequippable_items(false);
    GameBattler::refresh();
    if (party_in_battle()) request_status_refresh();
    disable_dashing_cache = -1;
}

GameUnit& GameActor::friends_unit() const { return game_party(); }
GameUnit& GameActor::opponents_unit() const { return game_troop(); }

int GameActor::index() const {
    auto m = game_party().members();
    auto it = std::find(m.begin(), m.end(), this);
    return it == m.end() ? -1 : (int)(it - m.begin());
}

bool GameActor::is_battle_member() const {
    auto m = game_party().battle_members();
    return std::find(m.begin(), m.end(), this) != m.end();
}

const Value& GameActor::current_class() const { return data::classes()[class_id]; }

std::vector<ItemRef> GameActor::skills() {
    std::vector<ItemRef> list;
    std::vector<int> ids = equipped_skills;
    for (int a : added_skills()) ids.push_back(a);
    for (int id : ids) {
        ItemRef r = ItemRef::skill(id);
        if (!r.valid()) {
            list.push_back(ItemRef());
            continue;
        }
        if (std::find(list.begin(), list.end(), r) == list.end()) list.push_back(r);
    }
    return list;
}

std::vector<ItemRef> GameActor::usable_skills() {
    std::vector<ItemRef> out;
    for (auto& s : skills()) {
        if (can_use(s)) out.push_back(s);
    }
    return out;
}

std::vector<const Value*> GameActor::trait_objects() {
    std::vector<const Value*> objs = GameBattlerBase::trait_objects();
    objs.push_back(&actor());
    objs.push_back(&current_class());
    for (auto& e : equips()) {
        if (e.valid()) objs.push_back(&e.data());
    }
    return objs;
}

std::vector<int> GameActor::attack_elements() {
    std::vector<int> set = GameBattlerBase::attack_elements();
    if (has_no_weapons() && !contains_int(set, 1)) set.push_back(1);
    return set;
}

double GameActor::param_max(int id) const {
    if (id == 0) return yanfly_param("ActorMaxHp").num();
    if (id == 1) return yanfly_param("ActorMaxMp").num();
    return yanfly_param("ActorParam").num();
}

double GameActor::param_base(int id) const {
    const Value& p = current_class()["params"][id];
    if (level > 99) {
        double i = p[99].num(), j = p[98].num();
        return i + (i - j) * (level - 99);
    }
    return p[level].num();
}

double GameActor::param_plus(int id) {
    double value = GameBattlerBase::param_plus(id);
    std::vector<ItemRef> eq = equips();
    for (auto& e : eq) {
        if (e.valid()) value += e.data()["params"][id].num();
    }
    for (auto& e : eq) {
        if (!e.valid()) continue;
        value += eval_param_plus(e.data(), id);
    }
    return value;
}

double GameActor::eval_param_plus(const Value& item, int id) {
    if (item["parameterEval"].string().empty()) return 0;
    const Value* ast = obj_ast(item, "parameterEval");
    if (!ast) return 0;
    js::ScopePtr scope = std::make_shared<js::Scope>();
    js::Val self = to_js();
    static const char* names[] = {"value", "hp", "maxhp", "mhp", "mp", "maxmp", "mmp", "sp", "maxsp", "msp", "atk", "str", "def", "mat", "int",
                                  "spi", "mdf", "res", "agi", "spd", "luk", "all"};
    for (const char* n : names) scope->declare(js::atom(n), js::Val::num(0));
    scope->declare(js::atom("a"), self);
    scope->declare(js::atom("user"), self);
    scope->declare(js::atom("s"), switches_proxy());
    scope->declare(js::atom("v"), variables_proxy());
    js::Val r;
    if (!js::vm().run(*ast, self, scope, &r, data::keep())) script_error("CUSTOM PARAMETER FORMULA ERROR", js::vm().last_error);
    auto g = [&](const char* n) { return scope->find(js::atom(n))->to_num(); };
    double value = g("value");
    switch (id) {
    case 0: value += g("hp") + g("maxhp") + g("mhp"); break;
    case 1: value += g("mp") + g("maxmp") + g("mmp") + g("sp") + g("maxsp") + g("msp"); break;
    case 2: value += g("atk") + g("str"); break;
    case 3: value += g("def"); break;
    case 4: value += g("mat") + g("int") + g("spi"); break;
    case 5: value += g("mdf") + g("res"); break;
    case 6: value += g("agi") + g("spd"); break;
    case 7: value += g("luk"); break;
    }
    return value + g("all");
}

int GameActor::attack_animation_id1() {
    if (has_no_weapons()) return 1;
    auto w = weapons();
    return w.empty() ? 0 : w[0].data()["animationId"].as_int();
}

int GameActor::attack_animation_id2() {
    auto w = weapons();
    return w.size() > 1 ? w[1].data()["animationId"].as_int() : 0;
}

void GameActor::change_exp(double exp, bool show) {
    exp_[class_id] = std::max(exp, 0.0);
    int last_level = level;
    std::vector<ItemRef> last_skills = skills_list();
    while (!is_max_level() && current_exp() >= next_level_exp()) level_up();
    while (current_exp() < current_level_exp()) level_down();
    if (show && level > last_level) display_level_up(find_new_skills(last_skills));
    refresh();
}

void GameActor::level_up() {
    level++;
    const Value& l = current_class()["learnings"];
    for (size_t i = 0; i < l.size(); i++) {
        if (l[i]["level"].as_int() == level) learn_skill(l[i]["skillId"].as_int());
    }
}

std::vector<ItemRef> GameActor::find_new_skills(const std::vector<ItemRef>& last) {
    std::vector<ItemRef> n = skills_list();
    for (auto& s : last) {
        auto it = std::find(n.begin(), n.end(), s);
        if (it != n.end()) n.erase(it);
    }
    return n;
}

void GameActor::display_level_up(const std::vector<ItemRef>& new_skills) {
    if (scenes::is_battle()) {
        js::Val d = js::make_object();
        d.o->set(js::atom("level"), js::Val::num(level));
        std::vector<js::Val> sk;
        for (auto& s : new_skills) sk.push_back(s.to_js());
        d.o->set(js::atom("skills"), js::make_array(sk));
        level_up_data = d;
    }
    std::string fmt = data::system()["terms"]["messages"]["levelUp"].string();
    std::string text = js_format(fmt, {name_, data::system()["terms"]["basic"][0].string(), std::to_string(level)});
    game_message().new_page();
    game_message().add(text);
    std::string obtain = data::system()["terms"]["messages"]["obtainSkill"].string();
    for (auto& s : new_skills) game_message().add(js_format(obtain, {s.data()["name"].string()}));
}

double GameActor::final_exp_rate() { return exr() * (is_battle_member() ? 1 : (data::system()["optExtraExp"].truthy() ? 1 : 0)); }

void GameActor::gain_exp(double exp) {
    double n = current_exp() + std::round(exp * final_exp_rate());
    change_exp(n, true);
}

void GameActor::change_level(int lvl, bool show) {
    lvl = std::min(std::max(lvl, 1), max_level());
    change_exp(exp_for_level(lvl), show);
    if (party_in_battle()) {
        AudioObj se;
        se.valid = true;
        se.name = "BA_Level_Up";
        se.volume = 100;
        se.pitch = 100;
        audio_manager::play_se(se);
    }
}

void GameActor::learn_skill(int id) {
    bool is_new = std::find(skills_.begin(), skills_.end(), id) == skills_.end();
    if (is_new) {
        skills_.push_back(id);
        std::sort(skills_.begin(), skills_.end());
    }
    has_passive_raw = false;
    if (!is_new) return;
    auto it = std::find(equipped_skills.begin(), equipped_skills.end(), 0);
    if (it == equipped_skills.end()) return;
    equip_skill((int)(it - equipped_skills.begin()), id);
}

void GameActor::forget_skill(int id) {
    auto it = std::find(skills_.begin(), skills_.end(), id);
    if (it != skills_.end()) skills_.erase(it);
    has_passive_raw = false;
}

bool GameActor::is_learned_skill(int id) const { return std::find(skills_.begin(), skills_.end(), id) != skills_.end(); }

bool GameActor::has_skill(int id) {
    ItemRef r = ItemRef::skill(id);
    for (auto& s : skills()) {
        if (s == r) return true;
    }
    return false;
}

void GameActor::change_class(int cid, bool keep_exp) {
    if (keep_exp) exp_[cid] = exp_.count(class_id) ? exp_[class_id] : NAN;
    class_id = cid;
    double e = exp_.count(class_id) ? exp_[class_id] : 0;
    if (std::isnan(e)) e = 0;
    change_exp(e, false);
    refresh();
}

void GameActor::on_player_walk() {
    clear_result();
    check_floor_effect();
    if (game_player().is_normal()) {
        turn_end_on_map();
        for (const Value* s : states()) update_state_steps(*s);
        show_added_states();
        show_removed_states();
    }
}

void GameActor::update_state_steps(const Value& state) {
    if (!state["removeByWalking"].truthy()) return;
    int id = state["id"].as_int();
    auto it = state_steps.find(id);
    if (it != state_steps.end() && it->second > 0) {
        if (--it->second == 0) remove_state(id);
    }
}

void GameActor::show_added_states() {
    for (int id : result().added_states) {
        const Value& st = data::states()[id];
        if (st["message1"].truthy()) game_message().add(name_ + st["message1"].string());
    }
}

void GameActor::show_removed_states() {
    for (int id : result().removed_states) {
        const Value& st = data::states()[id];
        if (st["message4"].truthy()) game_message().add(name_ + st["message4"].string());
    }
}

void GameActor::turn_end_on_map() {
    if (std::fmod(game_party().steps(), 20) == 0) {
        on_turn_end();
        if (result().hp_damage > 0) perform_map_damage();
    }
}

void GameActor::check_floor_effect() {
    if (game_player().is_on_damage_floor()) execute_floor_damage();
}

void GameActor::execute_floor_damage() {
    double damage = std::floor(10 * fdr());
    double max_floor = data::system()["optFloorDeath"].truthy() ? hp() : std::max(hp() - 1, 0.0);
    damage = std::min(damage, max_floor);
    gain_hp(-damage);
    if (damage > 0) perform_map_damage();
}

void GameActor::perform_map_damage() {
    if (!party_in_battle()) game_screen().start_flash_for_damage();
}

bool GameActor::meets_usable_item_conditions(const ItemRef& item) {
    if (party_in_battle() && !battle::can_escape()) {
        const Value& effects = item.data()["effects"];
        for (size_t i = 0; i < effects.size(); i++) {
            if (effects[i].is_obj() && effects[i]["code"].as_int() == 41) return false;
        }
    }
    return GameBattlerBase::meets_usable_item_conditions(item);
}

void GameActor::init_battle_commands_list() {
    const Value& a = actor();
    const Value* l = a["meta"].find("BattleCommandsList");
    const Value* c = current_class()["meta"].find("BattleCommandsList");
    if (l && l->truthy()) battle_commands_list = js::value_to_js(*l);
    else if (c && c->truthy()) battle_commands_list = js::value_to_js(*c);
    else battle_commands_list = js::make_array();
}

bool GameActor::can_pay_skill_cost(const ItemRef& skill) {
    if (party_in_battle()) {
        std::string ec = actor_meta(skill.data(), "EnergyCost");
        if (!ec.empty() && js_number(ec) > game_party().stress_energy_count) return false;
    }
    return GameBattlerBase::can_pay_skill_cost(skill);
}

void GameActor::pay_skill_cost(const ItemRef& skill) {
    GameBattlerBase::pay_skill_cost(skill);
    if (party_in_battle()) {
        std::string ec = actor_meta(skill.data(), "EnergyCost");
        if (!ec.empty()) game_party().stress_energy_count -= js_number(ec);
    }
}

std::vector<ItemRef> GameActor::skills_list() {
    std::vector<ItemRef> list;
    std::vector<int> ids = skills_;
    for (int a : added_skills()) ids.push_back(a);
    for (int id : ids) {
        ItemRef r = ItemRef::skill(id);
        if (std::find(list.begin(), list.end(), r) == list.end()) list.push_back(r);
    }
    return list;
}

bool GameActor::is_equip_skill_slot_locked(int slot) const {
    int id = slot >= 0 && slot < (int)equipped_skills.size() ? equipped_skills[(size_t)slot] : 0;
    if (std::find(locked_skills.begin(), locked_skills.end(), id) != locked_skills.end()) return true;
    return slot >= 0 && slot < (int)locked_equipped_skills.size() && locked_equipped_skills[(size_t)slot];
}

bool GameActor::can_equip_skill(const ItemRef& skill) const {
    if (!skill.valid()) return false;
    if (std::find(equipped_skills.begin(), equipped_skills.end(), skill.id) != equipped_skills.end()) return false;
    if (skill.data()["meta"]["HideInMenu"].truthy()) return false;
    return true;
}

std::vector<ItemRef> GameActor::equipable_skills() {
    std::vector<ItemRef> list;
    for (int id : skills_) {
        ItemRef s = ItemRef::skill(id);
        if (can_equip_skill(s)) list.push_back(s);
    }
    return list;
}

void GameActor::equip_skill(int index, int skill_id) {
    if (index < 0) return;
    if ((int)equipped_skills.size() <= index) equipped_skills.resize((size_t)index + 1, 0);
    equipped_skills[(size_t)index] = skill_id;
}

void GameActor::unequip_skill(int index, bool learn) {
    if (index < 0 || index >= (int)equipped_skills.size()) return;
    int id = equipped_skills[(size_t)index];
    if (learn && id > 0) learn_skill(id);
    equipped_skills[(size_t)index] = 0;
}

std::string GameActor::battle_status_face_name() const {
    if (!battle_face_name.empty()) return battle_face_name;
    std::string n = actor_meta(actor(), "BattleStatusFaceName");
    return trim(n);
}

std::string GameActor::menu_status_face_name() const { return trim(actor_meta(actor(), "MenuStatusFaceName")); }

int GameActor::status_face_index() {
    if (game_temp().expando.find(js::atom("_secondChance")) && game_temp().expando.find(js::atom("_secondChance"))->truthy() && actor_id_ == 1) return 3;
    if (game_temp().expando.find(js::atom("_damagedPlayer")) && game_temp().expando.find(js::atom("_damagedPlayer"))->truthy()) return 2;
    if (use_victory_face && is_alive()) return 10;
    auto st = states();
    if (!st.empty()) {
        const Value& s = *st[0];
        std::string alt = actor_meta(s, "AltIndexSwitch");
        if (!alt.empty() && game_switches().value((int)js_number(alt))) return (int)js_number(actor_meta(s, "AltStateFaceIndex"));
        std::string sfi = actor_meta(s, "StateFaceIndex");
        if (!sfi.empty()) return (int)js_number(sfi);
    }
    std::string fear = actor_meta(actor(), "FearBattleFaceIndex");
    if (!fear.empty() && game_switches().value(92)) return (int)js_number(fear);
    return 0;
}

int GameActor::status_back_index() {
    if (game_temp().expando.find(js::atom("_secondChance")) && game_temp().expando.find(js::atom("_secondChance"))->truthy() && actor_id_ == 1) return 11;
    auto st = states();
    if (!st.empty()) {
        std::string v = actor_meta(*st[0], "StateBackIndex");
        if (!v.empty()) return (int)js_number(v);
    }
    return 0;
}

int GameActor::status_list_index() {
    auto st = states();
    if (!st.empty()) {
        std::string tag = "World_" + std::to_string(current_world_index()) + "_StateListIndex";
        std::string v = actor_meta(*st[0], tag.c_str());
        if (!v.empty()) return (int)js_number(v);
        v = actor_meta(*st[0], "StateListIndex");
        if (!v.empty()) return (int)js_number(v);
    }
    return 0;
}

int GameActor::battle_status_index() const { return (int)js_number(actor_meta(actor(), "BattleStatusIndex")); }

bool GameActor::owner_flag(const char* key) {
    if (actor()[key].truthy()) return true;
    if (current_class()[key].truthy()) return true;
    for (auto& e : equips()) {
        if (e.valid() && e.data()[key].truthy()) return true;
    }
    return false;
}

bool GameActor::is_dash_disabled() {
    if (disable_dashing_cache >= 0) return disable_dashing_cache != 0;
    bool v = owner_flag("disableDashing");
    if (!v) v = states_flag("disableDashing");
    disable_dashing_cache = v ? 1 : 0;
    return v;
}

std::vector<int> GameActor::passive_states_raw_impl() {
    if (has_passive_raw) return passive_raw;
    std::vector<int> arr = GameBattlerBase::passive_states_raw_impl();
    auto add = [&](const std::vector<int>& v) { arr.insert(arr.end(), v.begin(), v.end()); };
    add(get_passive_state_data(&actor()));
    add(get_passive_state_data(&current_class()));
    for (auto& e : equips()) add(get_passive_state_data(e.valid() ? &e.data() : nullptr));
    for (int id : skills_) add(get_passive_state_data(&data::skills()[id]));
    std::vector<int> uniq;
    for (int id : arr) {
        if (!contains_int(uniq, id)) uniq.push_back(id);
    }
    passive_raw = uniq;
    has_passive_raw = true;
    return passive_raw;
}

double GameActor::max_buff_limit(int id) {
    double value = GameBattlerBase::max_buff_limit(id);
    value += actor()["maxBuff"][id].num();
    value += current_class()["maxBuff"][id].num();
    for (auto& e : equips()) {
        if (e.valid() && e.data()["maxBuff"].is_arr()) value += e.data()["maxBuff"][id].num();
    }
    return value;
}

double GameActor::max_debuff_limit(int id) {
    double value = GameBattlerBase::max_debuff_limit(id);
    value -= actor()["maxDebuff"][id].num();
    value -= current_class()["maxDebuff"][id].num();
    for (auto& e : equips()) {
        if (e.valid() && e.data()["maxDebuff"].is_arr()) value -= e.data()["maxDebuff"][id].num();
    }
    return value;
}

bool GameActor::taunt_physical() {
    if (null_taunt_physical()) return false;
    if (owner_flag("physTaunt")) return true;
    return GameBattlerBase::taunt_physical();
}
bool GameActor::taunt_magical() {
    if (null_taunt_magical()) return false;
    if (owner_flag("magicTaunt")) return true;
    return GameBattlerBase::taunt_magical();
}
bool GameActor::taunt_certain() {
    if (null_taunt_certain()) return false;
    if (owner_flag("certainTaunt")) return true;
    return GameBattlerBase::taunt_certain();
}
bool GameActor::null_taunt_physical() { return owner_flag("nullPhysTaunt") || GameBattlerBase::null_taunt_physical(); }
bool GameActor::null_taunt_magical() { return owner_flag("nullMagicTaunt") || GameBattlerBase::null_taunt_magical(); }
bool GameActor::null_taunt_certain() { return owner_flag("nullCertainTaunt") || GameBattlerBase::null_taunt_certain(); }
bool GameActor::ignore_taunt_physical() { return owner_flag("ignorePhysTaunt") || GameBattlerBase::ignore_taunt_physical(); }
bool GameActor::ignore_taunt_magical() { return owner_flag("ignoreMagicTaunt") || GameBattlerBase::ignore_taunt_magical(); }
bool GameActor::ignore_taunt_certain() { return owner_flag("ignoreCertainTaunt") || GameBattlerBase::ignore_taunt_certain(); }

void GameActor::set_movement_graphic_data(const std::string& name, const js::Val& d) {
    MovementGraphic* m = character_movement_graphics.get(to_lower(name));
    if (!m) return;
    if (d.is_str()) {
        m->name = d.to_str();
        m->index = 1;
        m->set = true;
    } else if (d.is_obj()) {
        js::Vm& vm = js::vm();
        m->name = vm.get(d, js::atom("name")).to_str();
        m->index = vm.get(d, js::atom("index")).to_int();
        m->set = true;
    } else {
        m->set = false;
    }
}

void GameActor::set_movement_graphics(const js::Val& idle, const js::Val& walking, const js::Val& running) {
    set_movement_graphic_data("idle", idle);
    set_movement_graphic_data("walking", walking);
    set_movement_graphic_data("running", running);
}

std::string GameActor::face_save_load() const {
    switch (actor()["id"].as_int()) {
    case 1: return "01_OMORI_BATTLE";
    case 2: return "02_AUBREY_BATTLE";
    case 3: return "03_KEL_BATTLE";
    case 4: return "04_HERO_BATTLE";
    case 8: return "01_FA_OMORI_BATTLE";
    case 9: return "02_FA_AUBREY_BATTLE";
    case 10: return "03_FA_KEL_BATTLE";
    case 11: return "04_FA_HERO_BATTLE";
    default: return "default_face_image_here";
    }
}

}
