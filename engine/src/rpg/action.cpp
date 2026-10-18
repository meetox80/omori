#include "action.h"

#include <algorithm>
#include <cmath>

#include "../js/dataview.h"
#include "../js/random.h"
#include "audio_manager.h"
#include "battle.h"
#include "data.h"
#include "enemy.h"
#include "evalctx.h"
#include "game.h"
#include "hooks.h"
#include "params.h"
#include "party.h"

namespace rpg {

namespace {

int random_int(double n) { return (int)std::floor(n * rng::next()); }

bool contains_int(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

js::Val jsv(GameBattler* b) { return b ? b->to_js() : js::Val(); }

}

BattlerRef BattlerRef::of(const GameBattlerBase* b) {
    BattlerRef r;
    if (!b) return r;
    if (b->is_actor()) r.actor_id = static_cast<const GameActor*>(b)->actor_id();
    else r.enemy_index = b->index();
    return r;
}

GameBattler* BattlerRef::get() const {
    if (actor_id > 0) return game_actors().actor(actor_id);
    if (enemy_index >= 0) {
        auto m = game_troop().unit_members();
        return enemy_index < (int)m.size() ? m[(size_t)enemy_index] : nullptr;
    }
    return nullptr;
}

GameAction::GameAction(GameBattler* subject, bool forcing_) : forcing(forcing_) {
    set_subject(subject);
    clear();
}

GameAction::GameAction(const GameAction& o)
    : js::JsObject(o), subject_actor_id(o.subject_actor_id), subject_enemy_index(o.subject_enemy_index), forcing(o.forcing), item_(o.item_),
      target_index(o.target_index), is_counter_(o.is_counter_), is_force_miss(o.is_force_miss), target_group(o.target_group),
      has_reflection_target(o.has_reflection_target), reflection_target(o.reflection_target), snatch_target_index(o.snatch_target_index),
      active_chain_skill(o.active_chain_skill) {}

void GameAction::clear() {
    item_ = GameItem();
    target_index = -1;
    is_counter_ = false;
    is_force_miss = false;
    target_group = -1;
}

void GameAction::set_subject(GameBattler* subject) {
    if (!subject) return;
    if (subject->is_actor()) {
        subject_actor_id = static_cast<GameActor*>(subject)->actor_id();
        subject_enemy_index = -1;
    } else {
        subject_enemy_index = subject->index();
        subject_actor_id = 0;
    }
}

GameBattler* GameAction::subject() const {
    if (subject_actor_id > 0) return game_actors().actor(subject_actor_id);
    auto m = game_troop().unit_members();
    return subject_enemy_index >= 0 && subject_enemy_index < (int)m.size() ? m[(size_t)subject_enemy_index] : nullptr;
}

GameUnit& GameAction::friends_unit() const { return subject()->friends_unit(); }
GameUnit& GameAction::opponents_unit() const { return subject()->opponents_unit(); }

void GameAction::set_enemy_action(const Value* action) {
    if (action && action->is_obj()) set_skill((*action)["skillId"].as_int());
    else clear();
}

void GameAction::set_attack() { set_skill(subject()->attack_skill_id()); }
void GameAction::set_guard() { set_skill(subject()->guard_skill_id()); }
void GameAction::set_skill(int id) { item_.set_object(ItemRef::skill(id)); }
void GameAction::set_item(int id) { item_.set_object(ItemRef::item(id)); }

int GameAction::num_repeats() const {
    double repeats = item_data()["repeats"].num();
    if (is_attack()) repeats += subject()->attack_times_add();
    return (int)std::floor(repeats);
}

bool GameAction::check_item_scope(std::initializer_list<int> list) const {
    if (!item().valid()) return false;
    int scope = item_data()["scope"].as_int();
    for (int s : list) {
        if (s == scope) return true;
    }
    return false;
}

bool GameAction::is_for_opponent() const {
    if (target_group == 1) return true;
    return check_item_scope({1, 2, 3, 4, 5, 6});
}

bool GameAction::is_for_friend() const {
    if (target_group == 0) return true;
    return check_item_scope({7, 8, 9, 10, 11});
}

bool GameAction::needs_selection() const {
    if (is_for_everybody()) return false;
    if (party_in_battle() && item_data()["scope"].as_int() == 0) return false;
    if (party_in_battle() && battle::is_force_selection()) return true;
    return check_item_scope({1, 7, 9});
}

bool GameAction::check_damage_type(std::initializer_list<int> list) const {
    if (!item().valid()) return false;
    int t = item_data()["damage"]["type"].as_int();
    for (int s : list) {
        if (s == t) return true;
    }
    return false;
}

bool GameAction::is_attack() const {
    GameBattler* s = subject();
    return s && item() == ItemRef::skill(s->attack_skill_id()) && item().valid();
}

bool GameAction::is_guard() const {
    GameBattler* s = subject();
    return s && item() == ItemRef::skill(s->guard_skill_id()) && item().valid();
}

bool GameAction::is_magic_skill() const {
    if (!is_skill()) return false;
    const Value& ms = data::system()["magicSkills"];
    int st = item_data()["stypeId"].as_int();
    for (size_t i = 0; i < ms.size(); i++) {
        if (ms[i].as_int() == st) return true;
    }
    return false;
}

void GameAction::decide_random_target() {
    GameBattler* target;
    if (is_for_dead_friend()) target = friends_unit().random_dead_target();
    else if (is_for_friend()) target = friends_unit().random_target();
    else target = opponents_unit().random_target();
    if (target) target_index = target->index();
    else clear();
}

void GameAction::prepare() {
    if (subject()->is_confused() && !forcing) set_confusion();
}

bool GameAction::is_valid() {
    GameBattler* s = subject();
    if (active_chain_skill) s->active_chain_enabled = s->is_actor() ? true : s->active_chain_enabled;
    bool result = (forcing && item().valid()) || s->can_use(item());
    if (active_chain_skill && s->is_actor()) s->active_chain_enabled = false;
    return result;
}

double GameAction::speed() {
    GameBattler* user = subject();
    double speed = 0;
    if (const Value* ast = yanfly_ast("BECActionSpeed")) {
        JsEval e(to_js());
        js::Val u = user->to_js();
        e.set("user", u).set("a", u);
        e.num("maxhp", user->mhp()).num("mhp", user->mhp()).num("hp", user->hp());
        e.num("maxmp", user->mmp()).num("mmp", user->mmp()).num("mp", user->mp());
        e.num("maxtp", user->max_tp()).num("mtp", user->max_tp()).num("tp", user->tp());
        e.num("atk", user->atk()).num("def", user->def()).num("mat", user->mat());
        e.num("int", user->mat()).num("mdf", user->mdf()).set("res", js::Val());
        e.num("agi", user->agi()).num("luk", user->luk());
        if (e.run(ast, "ACTION SPEED FORMULA ERROR")) speed = e.completion().to_num();
    }
    if (item().valid()) speed += item_data()["speed"].num();
    if (is_attack()) speed += user->attack_speed();
    return speed;
}

std::vector<GameBattler*> GameAction::make_targets() {
    if (is_for_everybody()) {
        std::vector<GameBattler*> t = friends_unit().alive_members();
        for (GameBattler* b : opponents_unit().alive_members()) t.push_back(b);
        return repeat_targets(t);
    }
    bool taunt = is_valid() && is_tauntable();
    if (taunt) {
        game_temp().taunt = true;
        game_temp().taunt_action = this;
    }
    std::vector<GameBattler*> value = base_make_targets();
    if (is_valid() && is_tauntable()) game_temp().taunt = false;
    game_temp().taunt_action = nullptr;
    return value;
}

std::vector<GameBattler*> GameAction::base_make_targets() {
    std::vector<GameBattler*> targets;
    if (!forcing && subject()->is_confused()) targets = {confusion_target()};
    else if (is_for_opponent()) targets = targets_for_opponents();
    else if (is_for_friend()) targets = targets_for_friends();
    return repeat_targets(targets);
}

std::vector<GameBattler*> GameAction::repeat_targets(const std::vector<GameBattler*>& targets) const {
    std::vector<GameBattler*> out;
    int repeats = num_repeats();
    for (GameBattler* t : targets) {
        if (!t) continue;
        for (int j = 0; j < repeats; j++) out.push_back(t);
    }
    return out;
}

GameBattler* GameAction::confusion_target() {
    switch (subject()->confusion_level()) {
    case 1: return opponents_unit().random_target();
    case 2:
        if (random_int(2) == 0) return opponents_unit().random_target();
        return friends_unit().random_target();
    default: return friends_unit().random_target();
    }
}

std::vector<GameBattler*> GameAction::targets_for_opponents() {
    std::vector<GameBattler*> targets;
    GameUnit& unit = opponents_unit();
    if (is_for_random()) {
        for (int i = 0; i < num_targets(); i++) targets.push_back(unit.random_target());
    } else if (is_for_one()) {
        if (target_index < 0) targets.push_back(unit.random_target());
        else targets.push_back(unit.smooth_target(target_index));
    } else {
        targets = unit.alive_members();
    }
    if (is_valid() && is_tauntable() && game_temp().taunt) {
        if (is_for_one() && target_index >= 0) targets = get_taunt_target(targets);
    }
    return targets;
}

std::vector<GameBattler*> GameAction::targets_for_friends() {
    std::vector<GameBattler*> targets;
    GameUnit& unit = friends_unit();
    if (is_for_user()) return {subject()};
    if (is_for_dead_friend()) {
        if (is_for_one()) targets.push_back(unit.smooth_dead_target(target_index));
        else targets = unit.dead_members();
    } else if (is_for_one()) {
        if (target_index < 0) targets.push_back(unit.random_target());
        else targets.push_back(unit.smooth_target(target_index));
    } else {
        targets = unit.alive_members();
    }
    return targets;
}

double GameAction::evaluate() {
    double value = 0;
    for (GameBattler* target : item_target_candidates()) {
        double tv = evaluate_with_target(target);
        if (is_for_all()) {
            value += tv;
        } else if (tv > value) {
            value = tv;
            target_index = target->index();
        }
    }
    value *= num_repeats();
    if (value > 0) value += rng::next();
    return value;
}

std::vector<GameBattler*> GameAction::item_target_candidates() {
    if (!is_valid()) return {};
    if (is_for_opponent()) return opponents_unit().alive_members();
    if (is_for_user()) return {subject()};
    if (is_for_dead_friend()) return friends_unit().dead_members();
    return friends_unit().alive_members();
}

double GameAction::evaluate_with_target(GameBattler* target) {
    if (is_hp_effect()) {
        double value = make_damage_value(target, false);
        if (is_for_opponent()) return value / std::max(target->hp(), 1.0);
        double recovery = std::min(-value, target->mhp() - target->hp());
        return recovery / target->mhp();
    }
    return NAN;
}

bool GameAction::test_apply(GameBattler* target) {
    return is_for_dead_friend() == target->is_dead() &&
           (party_in_battle() || is_for_opponent() || (is_hp_recover() && target->hp() < target->mhp()) ||
            (is_mp_recover() && target->mp() < target->mmp()) || has_item_any_valid_effects(target));
}

bool GameAction::has_item_any_valid_effects(GameBattler* target) {
    const Value& effects = item_data()["effects"];
    for (size_t i = 0; i < effects.size(); i++) {
        if (test_item_effect(target, effects[i])) return true;
    }
    return false;
}

bool GameAction::test_item_effect(GameBattler* target, const Value& effect) {
    int code = effect["code"].as_int();
    int data_id = effect["dataId"].as_int();
    if (code == EFFECT_COMMON_EVENT) {
        if (!party_in_battle() && game_temp().is_common_event_for_battle_only(data_id)) return false;
    }
    switch (code) {
    case EFFECT_LEARN_SKILL: return target->is_actor() && !static_cast<GameActor*>(target)->is_learned_skill(data_id);
    case EFFECT_RECOVER_HP: return target->hp() < target->mhp() || effect["value1"].num() < 0 || effect["value2"].num() < 0;
    case EFFECT_RECOVER_MP: return target->mp() < target->mmp() || effect["value1"].num() < 0 || effect["value2"].num() < 0;
    case EFFECT_ADD_STATE: return !target->is_state_affected(data_id);
    case EFFECT_REMOVE_STATE: return target->is_state_affected(data_id);
    case EFFECT_ADD_BUFF: return !target->is_max_buff_affected(data_id);
    case EFFECT_ADD_DEBUFF: return !target->is_max_debuff_affected(data_id);
    case EFFECT_REMOVE_BUFF: return target->is_buff_affected(data_id);
    case EFFECT_REMOVE_DEBUFF: return target->is_debuff_affected(data_id);
    default: return true;
    }
}

double GameAction::item_cnt(GameBattler* target) {
    const Value& it = item_data();
    if (it["cannotCounter"].truthy()) return 0;
    if (!it["allyCounter"].truthy()) {
        if (target->is_actor() == subject()->is_actor()) return 0;
    }
    if (!target->can_move()) return 0;
    if (!target->can_counter()) return 0;
    double rate = target->cnt();
    rate *= subject()->target_counter_rate();
    rate *= it["counterRate"].num();
    rate += subject()->target_counter_flat();
    rate += it["counterMod"].num();
    rate = subject()->target_counter_rate_eval(rate, target, item());
    rate = custom_counter_rate_eval(rate, target);
    return rate;
}

double GameAction::item_mrf(GameBattler* target) const { return is_magical() ? target->mrf() : 0; }

double GameAction::item_hit(GameBattler* target) const {
    (void)target;
    if (is_physical()) return item_data()["successRate"].num() * 0.01 * subject()->hit();
    return item_data()["successRate"].num() * 0.01;
}

double GameAction::item_eva(GameBattler* target) const {
    if (is_physical()) return target->eva();
    if (is_magical()) return target->mev();
    return 0;
}

double GameAction::item_cri(GameBattler* target) {
    if (!item_data()["damage"]["critical"].truthy()) return 0;
    GameBattler* user = subject();
    double rate = apply_item_critical_rate(target);
    if (is_certain_hit()) rate += user->certain_crit_rate_bonus();
    if (is_physical()) rate += user->physical_crit_rate_bonus();
    if (is_magical()) rate += user->magical_crit_rate_bonus();
    return rate;
}

void GameAction::apply_core(GameBattler* target) {
    target->result_.reset(new GameActionResult());
    subject()->result_.reset(new GameActionResult());
    GameActionResult& result = target->result();
    subject()->clear_result();
    result.clear();
    result.used = test_apply(target);
    result.missed = result.used && rng::next() >= item_hit(target);
    result.evaded = !result.missed && rng::next() < item_eva(target);
    result.physical = is_physical();
    result.drain = is_drain();
    if (result.is_hit()) {
        if (item_data()["damage"]["type"].as_int() > 0) {
            result.critical = rng::next() < item_cri(target);
            double value = make_damage_value(target, result.critical);
            execute_damage(target, value);
        }
        const Value& effects = item_data()["effects"];
        for (size_t i = 0; i < effects.size(); i++) apply_item_effect(target, effects[i]);
        apply_item_user_effect(target);
    }
    if (party_in_battle()) {
        target->start_damage_popup();
        target->perform_result_effects();
        if (target != subject()) subject()->start_damage_popup();
    }
}

void GameAction::apply(GameBattler* target) {
    on_apply_state_effects(target);
    on_select_state_effects(target);
    apply_before_eval(target);
    apply_core(target);
    apply_after_eval(target);
    ai_register_element_rate(target);
    on_deselect_state_effects(target);
    off_apply_state_effects(target);
    GameActionResult& result = target->result();
    bool hit = result.is_hit();
    if (hit) {
        double rate = calc_element_rate(target);
        result.element_strong = rate > 1;
        result.element_weak = rate < 1;
        if (result.hp_damage > 0) {
            AudioObj se;
            se.valid = true;
            se.pitch = 100;
            se.pan = 0;
            if (result.critical) {
                se.name = "BA_CRITICAL_HIT";
                se.volume = 250;
                audio_manager::play_se(se);
            } else if (result.element_strong) {
                se.name = "se_impact_double";
                se.volume = 150;
                audio_manager::play_se(se);
            } else if (result.element_weak) {
                se.name = "se_impact_soft";
                se.volume = 150;
                audio_manager::play_se(se);
            } else {
                audio_manager::play_system_sound(10);
            }
        }
    }
    if (target->is_enemy()) {
        GameEnemy* en = static_cast<GameEnemy*>(target);
        if (hit && item().valid() && item_data()["meta"]["ScanEnemy"].truthy() && en->can_scan()) game_party().add_scanned_enemy(en->enemy_id());
    } else if (hit && result.hp_damage > 0) {
        game_party().stress_energy_count++;
    }
}

double GameAction::make_damage_value(GameBattler* target, bool critical) {
    double base = eval_damage_formula(target);
    double value = base;
    base *= calc_element_rate(target);
    critical = modify_critical(critical, base, target);
    target->result().critical = critical;
    value = base;
    if (base > 0) value = apply_damage_rate(value);
    if (base < 0) value = apply_heal_rate(value, target);
    if (critical) value = apply_critical_rate(value, base, target);
    if (is_physical()) value *= target->pdr();
    if (is_magical()) value *= target->mdr();
    if (base > 0) value += game_system().flat_damage;
    if (base < 0) value -= game_system().flat_heal;
    if (critical) value = apply_flat_critical(value, base, target);
    value = apply_variance(value, item_data()["damage"]["variance"].num());
    value = apply_guard(value, target);
    value = apply_minimum_damage(value, base, target);
    return std::round(value);
}

double GameAction::eval_damage_formula(GameBattler* target) {
    const Value& it = item_data();
    const Value& dmg = it["damage"];
    int type = dmg["type"].as_int();
    double sign = (type == 3 || type == 4) ? -1 : 1;
    const Value* ast = dmg.find("_ast");
    JsEval e(to_js());
    js::Val a = jsv(subject()), b = jsv(target);
    e.set("item", item().to_js()).set("a", a).set("b", b).set("user", a).set("subject", a).sv();
    double value = 0;
    if (dmg["custom"].truthy()) {
        e.num("value", 0);
        if (!e.run(ast, "DAMAGE FORMULA ERROR")) return 0;
        value = std::max(e.get("value").to_num(), 0.0) * sign;
    } else {
        if (!ast) return 0;
        if (!e.run(ast, "DAMAGE FORMULA ERROR")) return 0;
        double r = e.completion().to_num();
        value = (std::isnan(r) ? NAN : std::max(r, 0.0)) * sign;
    }
    return value;
}

double GameAction::calc_element_rate(GameBattler* target) {
    int element = item_data()["damage"]["elementId"].as_int();
    if (element < 1) {
        static const int emotion_states[] = {6, 7, 8, 10, 11, 12, 14, 15, 16, 119, 120, 121, 122, 123, 197, 124, 125, 126};
        bool afflicted = false;
        for (int id : subject()->states_) {
            if (std::find(std::begin(emotion_states), std::end(emotion_states), id) != std::end(emotion_states)) afflicted = true;
        }
        if (afflicted) {
            auto el = subject()->attack_elements();
            return el.empty() ? target->element_rate_undefined() : target->element_rate(el[0]);
        }
        return target->element_rate(element);
    }
    return target->element_rate(element);
}

double GameAction::elements_max_rate(GameBattler* target, const std::vector<int>& elements) {
    if (elements.empty()) return 1;
    double m = -INFINITY;
    for (int e : elements) m = std::max(m, target->element_rate(e));
    return m;
}

double GameAction::apply_variance(double damage, double variance) const {
    double amp = std::floor(std::max(std::abs(damage) * variance / 100, 0.0));
    double v = random_int(amp + 1) + random_int(amp + 1) - amp;
    return damage >= 0 ? damage + v : damage - v;
}

double GameAction::apply_guard(double damage, GameBattler* target) const {
    return damage / ((damage > 0 && target->is_guard()) ? 2 * target->grd() : 1);
}

void GameAction::execute_damage(GameBattler* target, double value) {
    value = on_pre_damage_state_effects(target, value);
    value = on_react_state_effects(target, value);
    value = apply_pre_damage_eval(target, value);
    GameActionResult& result = target->result();
    if (value == 0) result.critical = false;
    if (is_hp_effect()) execute_hp_damage(target, value);
    if (is_mp_effect()) execute_mp_damage(target, value);
    apply_post_damage_eval(target, value);
    value = on_respond_state_effects(target, value);
    value = on_post_damage_state_effects(target, value);
}

void GameAction::execute_hp_damage(GameBattler* target, double value) {
    double v = apply_minimum_damage(value, value, target);
    if (is_drain()) v = std::min(target->hp(), v);
    make_success(target);
    target->gain_hp(-v);
    if (v > 0) target->on_damage(v);
    gain_drained_hp(v);
    double damage = target->result().hp_damage;
    perform_life_steal(damage, target, value);
}

void GameAction::execute_mp_damage(GameBattler* target, double value) {
    double v = apply_minimum_damage(value, value, target);
    if (!is_mp_recover()) v = std::min(target->mp(), v);
    if (v != 0) make_success(target);
    target->gain_mp(-v);
    gain_drained_mp(v);
    double damage = target->result().mp_damage;
    perform_life_steal(damage, target, value);
}

void GameAction::gain_drained_hp(double value) {
    if (!is_drain()) return;
    GameBattler* g = has_reflection_target ? reflection_target.get() : subject();
    if (g) g->gain_hp(value);
}

void GameAction::gain_drained_mp(double value) {
    if (!is_drain()) return;
    GameBattler* g = has_reflection_target ? reflection_target.get() : subject();
    if (g) g->gain_mp(value);
}

void GameAction::apply_item_effect(GameBattler* target, const Value& effect) {
    switch (effect["code"].as_int()) {
    case EFFECT_RECOVER_HP: item_effect_recover_hp(target, effect); break;
    case EFFECT_RECOVER_MP: item_effect_recover_mp(target, effect); break;
    case EFFECT_GAIN_TP: item_effect_gain_tp(target, effect); break;
    case EFFECT_ADD_STATE: item_effect_add_state(target, effect); break;
    case EFFECT_REMOVE_STATE: item_effect_remove_state(target, effect); break;
    case EFFECT_ADD_BUFF: item_effect_add_buff(target, effect); break;
    case EFFECT_ADD_DEBUFF: item_effect_add_debuff(target, effect); break;
    case EFFECT_REMOVE_BUFF: item_effect_remove_buff(target, effect); break;
    case EFFECT_REMOVE_DEBUFF: item_effect_remove_debuff(target, effect); break;
    case EFFECT_SPECIAL: item_effect_special(target, effect); break;
    case EFFECT_GROW: item_effect_grow(target, effect); break;
    case EFFECT_LEARN_SKILL: item_effect_learn_skill(target, effect); break;
    case EFFECT_COMMON_EVENT: break;
    }
}

void GameAction::item_effect_recover_hp(GameBattler* target, const Value& effect) {
    double value = (target->mhp() * effect["value1"].num() + effect["value2"].num()) * target->rec();
    if (is_item()) value *= subject()->pha();
    value = std::floor(value);
    if (value != 0) {
        target->gain_hp(value);
        make_success(target);
    }
}

void GameAction::item_effect_recover_mp(GameBattler* target, const Value& effect) {
    double value = (target->mmp() * effect["value1"].num() + effect["value2"].num()) * target->rec();
    if (is_item()) value *= subject()->pha();
    value = std::floor(value);
    if (value != 0) {
        target->gain_mp(value);
        make_success(target);
    }
}

void GameAction::item_effect_gain_tp(GameBattler* target, const Value& effect) {
    double value = std::floor(effect["value1"].num());
    if (value != 0) {
        target->gain_tp(value);
        make_success(target);
    }
}

void GameAction::item_effect_add_state(GameBattler* target, const Value& effect) {
    if (effect["dataId"].as_int() == 0) item_effect_add_attack_state(target, effect);
    else item_effect_add_normal_state(target, effect);
}

void GameAction::item_effect_add_attack_state(GameBattler* target, const Value& effect) {
    for (int state_id : subject()->attack_states()) {
        double chance = effect["value1"].num();
        chance *= target->state_rate(state_id);
        chance *= subject()->attack_states_rate(state_id);
        chance *= luk_effect_rate(target);
        if (rng::next() < chance) {
            if (state_id == target->death_state_id() && target->is_immortal()) target->remove_immortal();
            target->add_state(state_id);
            make_success(target);
        }
    }
}

void GameAction::item_effect_add_normal_state(GameBattler* target, const Value& effect) {
    int state_id = effect["dataId"].as_int();
    double chance = effect["value1"].num();
    if (!is_certain_hit()) {
        chance *= target->state_rate(state_id);
        chance *= luk_effect_rate(target);
    }
    if (rng::next() < chance) {
        if (state_id == target->death_state_id() && target->is_immortal()) target->remove_immortal();
        target->add_state(state_id);
        make_success(target);
    }
}

void GameAction::item_effect_remove_state(GameBattler* target, const Value& effect) {
    if (rng::next() < effect["value1"].num()) {
        target->remove_state(effect["dataId"].as_int());
        make_success(target);
    }
}

void GameAction::item_effect_add_buff(GameBattler* target, const Value& effect) {
    target->add_buff(effect["dataId"].as_int(), effect["value1"].num());
    make_success(target);
}

void GameAction::item_effect_add_debuff(GameBattler* target, const Value& effect) {
    int id = effect["dataId"].as_int();
    double chance = target->debuff_rate(id) * luk_effect_rate(target);
    if (rng::next() < chance) {
        target->add_debuff(id, effect["value1"].num());
        make_success(target);
    }
}

void GameAction::item_effect_remove_buff(GameBattler* target, const Value& effect) {
    int id = effect["dataId"].as_int();
    if (target->is_buff_affected(id)) {
        target->remove_buff(id);
        make_success(target);
    }
}

void GameAction::item_effect_remove_debuff(GameBattler* target, const Value& effect) {
    int id = effect["dataId"].as_int();
    if (target->is_debuff_affected(id)) {
        target->remove_buff(id);
        make_success(target);
    }
}

void GameAction::item_effect_special(GameBattler* target, const Value& effect) {
    if (effect["dataId"].as_int() == 0) {
        target->escape();
        make_success(target);
    }
}

void GameAction::item_effect_grow(GameBattler* target, const Value& effect) {
    target->add_param(effect["dataId"].as_int(), std::floor(effect["value1"].num()));
    make_success(target);
}

void GameAction::item_effect_learn_skill(GameBattler* target, const Value& effect) {
    if (target->is_actor()) {
        static_cast<GameActor*>(target)->learn_skill(effect["dataId"].as_int());
        make_success(target);
    }
}

void GameAction::make_success(GameBattler* target) { target->result().success = true; }

void GameAction::apply_item_user_effect(GameBattler* target) {
    double value = std::floor(item_data()["tpGain"].num() * subject()->tcr());
    subject()->gain_silent_tp(value);
    if (target) {
        apply_modify_buff_turns(target);
        apply_modify_debuff_turns(target);
        apply_modify_state_turns(target);
    }
    if (item().valid() && item_data()["removeCategory"].is_obj()) apply_state_category_removal_effect(target);
    apply_item_steal_effect(target);
    apply_item_snatch_effect(target);
    if (target && target->is_enemy()) static_cast<GameEnemy*>(target)->mark_struck_actions(item(), subject(), this);
    if (item_data()["meta"]["AntiFail"].truthy()) make_success(target);
}

double GameAction::luk_effect_rate(GameBattler* target) const { return std::max(1.0 + (subject()->luk() - target->luk()) * 0.001, 0.0); }

void GameAction::apply_global() {
    if (party_in_battle()) return;
    const Value& effects = item_data()["effects"];
    for (size_t i = 0; i < effects.size(); i++) {
        if (effects[i]["code"].as_int() == EFFECT_COMMON_EVENT) game_temp().reserve_common_event(effects[i]["dataId"].as_int());
    }
}

void GameAction::init_eval(JsEval& e, GameBattler* target) const {
    js::Val a = jsv(subject()), b = jsv(target);
    e.set("item", item().to_js()).set("a", a).set("b", b).set("user", a).set("subject", a).sv();
}

void GameAction::apply_before_eval(GameBattler* target) {
    const Value* ast = obj_ast(item_data(), "customBeforeEval");
    if (!ast) return;
    JsEval e(to_js());
    init_eval(e, target);
    e.run(ast, "BEFORE EVAL ERROR");
}

void GameAction::apply_after_eval(GameBattler* target) {
    const Value* ast = obj_ast(item_data(), "customAfterEval");
    if (!ast) return;
    JsEval e(to_js());
    init_eval(e, target);
    e.run(ast, "AFTER EVAL ERROR");
}

double GameAction::apply_pre_damage_eval(GameBattler* target, double value) {
    const Value* ast = obj_ast(item_data(), "customPreDamageEval");
    if (!ast) return value;
    JsEval e(to_js());
    init_eval(e, target);
    e.num("value", value);
    e.run(ast, "PRE-DAMAGE EVAL ERROR");
    return e.get("value").to_num();
}

void GameAction::apply_post_damage_eval(GameBattler* target, double value) {
    const Value* ast = obj_ast(item_data(), "customPostDamageEval");
    if (!ast) return;
    JsEval e(to_js());
    init_eval(e, target);
    e.num("value", value);
    e.run(ast, "POST-DAMAGE EVAL ERROR");
}

double GameAction::custom_counter_rate_eval(double rate, GameBattler* target) {
    const Value* ast = obj_ast(item_data(), "counterRateEval");
    if (!ast || item_data()["counterRateEval"].string().empty()) return rate;
    JsEval e(to_js());
    init_eval(e, target);
    e.set("skill", item().to_js()).num("rate", rate);
    e.run(ast, "COUNTER RATE ERROR");
    return e.get("rate").to_num();
}

void GameAction::ai_register_element_rate(GameBattler* target) {
    int element = item_data()["damage"]["elementId"].as_int();
    if (element < 0) return;
    if (subject()->is_actor()) return;
    game_troop().ai_register_element_rate(target, element);
}

double GameAction::apply_buff_turns_eval(double turn, int param_id, GameBattler* target, bool debuff) {
    (void)debuff;
    const Value& code = item_data()["modifyTurnBuffEval"][param_id];
    if (code.string().empty()) return turn;
    const Value* ast = obj_ast(item_data(), "modifyTurnBuffEval." + std::to_string(param_id));
    if (!ast) return turn;
    JsEval e(to_js());
    js::Val a = jsv(subject()), b = jsv(target);
    e.set("item", item().to_js()).set("a", a).set("b", b).set("user", a).sv().num("turn", turn);
    e.run(ast, debuff ? "CUSTOM DEBUFF TURN SET ERROR" : "CUSTOM BUFF TURN SET ERROR");
    return e.get("turn").to_num();
}

void GameAction::apply_modify_buff_turns(GameBattler* target) {
    if (!item().valid()) return;
    bool affected = false;
    for (int i = 0; i < 8; ++i) {
        if (!target->is_buff_affected(i)) continue;
        double turn = item_data()["modifyTurnBuff"][i].num() + target->buff_turns_of(i);
        turn = apply_buff_turns_eval(turn, i, target, false);
        target->set_buff_turns(i, turn);
        if (target->buff_turns_of(i) <= 0) target->erase_buff(i);
        affected = true;
    }
    if (affected) target->refresh();
}

void GameAction::apply_modify_debuff_turns(GameBattler* target) {
    if (!item().valid()) return;
    bool affected = false;
    for (int i = 0; i < 8; ++i) {
        if (!target->is_debuff_affected(i)) continue;
        double turn = item_data()["modifyTurnDebuff"][i].num() + target->buff_turns_of(i);
        turn = apply_buff_turns_eval(turn, i, target, true);
        target->set_buff_turns(i, turn);
        if (target->buff_turns_of(i) <= 0) target->erase_buff(i);
        affected = true;
    }
    if (affected) target->refresh();
}

double GameAction::apply_state_turns_eval(double turn, int state_id, GameBattler* target) {
    const Value* ev = item_data()["modifyTurnStateEval"].find(std::to_string(state_id).c_str());
    if (!ev) return turn;
    const Value* ast = obj_ast(item_data(), "modifyTurnStateEval." + std::to_string(state_id));
    if (!ast) return turn;
    JsEval e(to_js());
    js::Val a = jsv(subject()), b = jsv(target);
    GameBattlerBase* origin = target->state_origin_battler(state_id);
    e.set("item", item().to_js()).set("a", a).set("b", b).set("user", a).set("origin", origin ? origin->to_js() : js::Val()).sv().num("turn", turn);
    e.run(ast, "CUSTOM STATE TURN SET ERROR");
    return e.get("turn").to_num();
}

void GameAction::apply_modify_state_turns(GameBattler* target) {
    if (!item().valid()) return;
    bool affected = false;
    std::vector<int> removed;
    for (const Value* st : target->states()) {
        const Value& state = *st;
        int id = state["id"].as_int();
        if (state["autoRemovalTiming"].as_int() <= 0) continue;
        if (!target->is_state_affected(id)) continue;
        double turn = target->state_turns_of(id);
        const Value* mt = item_data()["modifyTurnState"].find(std::to_string(id).c_str());
        if (mt && mt->truthy()) turn += mt->num();
        turn = apply_state_turns_eval(turn, id, target);
        target->set_state_turns(id, turn);
        if (target->state_turns_of(id) <= 0) removed.push_back(id);
    }
    for (int id : removed) {
        target->remove_state(id);
        affected = true;
    }
    if (affected) target->refresh();
}

js::Val GameAction::custom_effect_eval(GameBattler* target, int state_id, const char* type, GameBattler* side, const js::Val& value) {
    const Value& state = data::states()[state_id];
    if (!state.is_obj()) return value;
    if (state["customEffectEval"][type].string().empty()) return value;
    const Value* ast = obj_ast(state, std::string("customEffectEval.") + type);
    if (!ast) return value;
    JsEval e(to_js());
    js::Val a = jsv(subject()), b = jsv(target);
    GameBattlerBase* origin = side->state_origin_battler(state_id);
    e.set("attacker", a).set("defender", b).set("a", a).set("b", b).set("user", a).set("origin", origin ? origin->to_js() : js::Val()).sv();
    e.set("target", b).set("stateId", js::Val::num(state_id)).set("type", js::Val::str(type)).set("side", jsv(side));
    e.set("state", js::data_view(state, data::keep()));
    e.set("value", value);
    e.run(ast, "CUSTOM STATE CODE ERROR");
    return e.get("value");
}

void GameAction::on_apply_state_effects(GameBattler* target) {
    for (const Value* s : subject()->states()) custom_effect_eval(target, (*s)["id"].as_int(), "initiateState", subject(), js::Val());
}

void GameAction::on_select_state_effects(GameBattler* target) {
    for (const Value* s : target->states()) custom_effect_eval(target, (*s)["id"].as_int(), "selectState", target, js::Val());
}

void GameAction::on_deselect_state_effects(GameBattler* target) {
    for (const Value* s : target->states()) custom_effect_eval(target, (*s)["id"].as_int(), "deselectState", target, js::Val());
}

void GameAction::off_apply_state_effects(GameBattler* target) {
    for (const Value* s : subject()->states()) custom_effect_eval(target, (*s)["id"].as_int(), "concludeState", subject(), js::Val());
}

double GameAction::on_pre_damage_state_effects(GameBattler* target, double value) {
    js::Val v = js::Val::num(value);
    for (const Value* s : subject()->states()) v = custom_effect_eval(target, (*s)["id"].as_int(), "confirmState", subject(), v);
    return v.to_num();
}

double GameAction::on_react_state_effects(GameBattler* target, double value) {
    js::Val v = js::Val::num(value);
    for (const Value* s : target->states()) v = custom_effect_eval(target, (*s)["id"].as_int(), "reactState", target, v);
    return v.to_num();
}

double GameAction::on_respond_state_effects(GameBattler* target, double value) {
    js::Val v = js::Val::num(value);
    for (const Value* s : target->states()) v = custom_effect_eval(target, (*s)["id"].as_int(), "respondState", target, v);
    return v.to_num();
}

double GameAction::on_post_damage_state_effects(GameBattler* target, double value) {
    js::Val v = js::Val::num(value);
    for (const Value* s : subject()->states()) v = custom_effect_eval(target, (*s)["id"].as_int(), "establishState", subject(), v);
    return v.to_num();
}

void GameAction::apply_state_category_removal_effect(GameBattler* target) { target->remove_state_category_effect(item_data(), subject()); }

bool GameAction::modify_critical(bool critical, double base, GameBattler* target) {
    (void)base, (void)target;
    if (game_system().force_critical) critical = true;
    if (game_system().force_no_critical) critical = false;
    return critical;
}

double GameAction::apply_damage_rate(double value) const {
    value *= game_system().damage_rate;
    return std::max(0.0, value);
}

double GameAction::apply_heal_rate(double value, GameBattler* target) const {
    value *= game_system().heal_rate;
    value *= target->rec();
    return std::min(0.0, value);
}

double GameAction::apply_critical_rate(double value, double base, GameBattler* target) {
    value = apply_critical(value);
    value = apply_item_critical_mult(value, base, target);
    value *= game_system().crit_mult;
    return value;
}

double GameAction::apply_item_critical_mult(double value, double base, GameBattler* target) {
    (void)base;
    const Value* ast = obj_ast(item_data(), "critMult");
    if (!ast) return value;
    JsEval e(to_js());
    init_eval(e, target);
    e.num("value", value).num("bonus", subject()->critical_multiplier_bonus());
    e.run(ast, "CRITICAL MULTIPLIER FORMULA ERROR");
    return e.get("value").to_num();
}

double GameAction::apply_flat_critical(double value, double base, GameBattler* target) {
    value = apply_item_flat_crit(value, base, target);
    if (base > 0) value += game_system().flat_crit;
    else if (base < 0) value -= game_system().flat_crit;
    return value;
}

double GameAction::apply_minimum_damage(double value, double base, GameBattler* target) {
    (void)target;
    if (base > 0) value = std::max(0.0, value);
    else if (base < 0) value = std::min(0.0, value);
    if (is_damage_capped()) {
        double mn, mx;
        if (game_system().has_act_seq_damage_cap) {
            mn = game_system().act_seq_damage_cap * -1;
            mx = game_system().act_seq_damage_cap;
        } else if (item_data()["damageCap"].truthy()) {
            mn = item_data()["damageCap"].num() * -1;
            mx = item_data()["damageCap"].num();
        } else {
            mn = subject()->maximum_healing();
            mx = subject()->maximum_damage();
        }
        value = std::min(std::max(value, mn), mx);
    }
    return value;
}

bool GameAction::is_damage_capped() const {
    if (game_system().act_seq_bypass_damage_cap) return false;
    if (game_system().has_act_seq_damage_cap) return true;
    if (item_data().find("damageCap")) return true;
    if (item_data()["breakDamageCap"].truthy()) return false;
    return subject()->is_damage_capped();
}

double GameAction::apply_item_critical_rate(GameBattler* target) {
    const Value* ast = obj_ast(item_data(), "critRate");
    JsEval e(to_js());
    init_eval(e, target);
    e.num("rate", 0).num("bonus", subject()->critical_multiplier_bonus());
    if (ast) e.run(ast, "CRITICAL RATE FORMULA ERROR");
    return e.get("rate").to_num();
}

double GameAction::apply_item_flat_crit(double value, double base, GameBattler* target) {
    (void)base;
    const Value* ast = obj_ast(item_data(), "flatCrit");
    if (!ast) return value;
    JsEval e(to_js());
    init_eval(e, target);
    e.num("value", value).num("bonus", subject()->flat_critical_bonus()).num("baseDamage", base);
    e.run(ast, "CRITICAL FLAT BONUS ERROR");
    return e.get("value").to_num();
}

bool GameAction::can_life_steal_hp(double damage, GameBattler* target) const {
    const Value& ls = item_data()["lifeSteal"];
    if (ls.is_obj()) {
        if (ls["allNull"].truthy() || ls["hpNull"].truthy()) return false;
    }
    if (subject()->is_life_steal_state("allNull") || subject()->is_life_steal_state("hpNull")) return false;
    if (target->is_life_steal_state("allGuard") || target->is_life_steal_state("hpGuard")) return false;
    return damage > 0;
}

bool GameAction::can_life_steal_mp(double damage, GameBattler* target) const {
    const Value& ls = item_data()["lifeSteal"];
    if (ls.is_obj()) {
        if (ls["allNull"].truthy() || ls["mpNull"].truthy()) return false;
    }
    if (subject()->is_life_steal_state("allNull") || subject()->is_life_steal_state("mpNull")) return false;
    if (target->is_life_steal_state("allGuard") || target->is_life_steal_state("mpGuard")) return false;
    return damage > 0;
}

void GameAction::perform_life_steal(double damage, GameBattler* target, double value) {
    if (can_life_steal_hp(damage, target)) {
        double rate = get_life_steal_rate(target, value, "hp");
        double flat = get_life_steal_flat(target, value, "hp");
        if (is_physical()) subject()->life_steal(damage, "hpPhysical", target, rate, flat, false);
        else if (is_magical()) subject()->life_steal(damage, "hpMagical", target, rate, flat, false);
        else if (is_certain_hit()) subject()->life_steal(damage, "hpCertain", target, rate, flat, false);
    }
    if (can_life_steal_mp(damage, target)) {
        double rate = get_life_steal_rate(target, value, "mp");
        double flat = get_life_steal_flat(target, value, "mp");
        if (is_physical()) subject()->life_steal(damage, "mpPhysical", target, rate, flat, true);
        else if (is_magical()) subject()->life_steal(damage, "mpMagical", target, rate, flat, true);
        else if (is_certain_hit()) subject()->life_steal(damage, "mpCertain", target, rate, flat, true);
    }
}

double GameAction::life_steal_eval(const Value* ast, GameBattler* target, double value, const char* var, const char* where) {
    JsEval e(to_js());
    js::Val a = jsv(subject()), b = jsv(target);
    e.num(var, 0).set("a", a).set("user", a).set("subject", a).set("b", b).sv().num("damage", value).set("item", item().to_js()).set("skill", item().to_js());
    e.run(ast, where);
    return e.get(var).to_num();
}

double GameAction::get_life_steal_rate(GameBattler* target, double value, const std::string& type) {
    const Value& ls = item_data()["lifeSteal"];
    double rate = 1;
    if (ls[(type + "Rate").c_str()].truthy()) rate *= 1 - ls[(type + "Rate").c_str()].num();
    if (ls[(type + "RateEval").c_str()].truthy()) {
        const Value* ast = obj_ast(item_data(), "lifeSteal." + type + "RateEval");
        if (ast) rate *= 1 - life_steal_eval(ast, target, value, "rate", "LIFE STEAL RATE FORMULA ERROR");
    }
    return 1 - rate;
}

double GameAction::get_life_steal_flat(GameBattler* target, double value, const std::string& type) {
    const Value& ls = item_data()["lifeSteal"];
    double flat = 0;
    if (ls[(type + "Flat").c_str()].truthy()) flat += ls[(type + "Flat").c_str()].num();
    if (ls[(type + "RateFlat").c_str()].truthy()) {
        const Value* ast = obj_ast(item_data(), "lifeSteal." + type + "RateFlat");
        if (ast) flat += life_steal_eval(ast, target, value, "flat", "LIFE STEAL FLAT FORMULA ERROR");
    }
    return flat;
}

bool GameAction::is_tauntable() const {
    if (!check_item_scope({1})) return false;
    if (item_data()["bypassTaunt"].truthy()) return false;
    GameBattler* s = subject();
    if (is_physical() && s->ignore_taunt_physical()) return false;
    if (is_magical() && s->ignore_taunt_magical()) return false;
    if (is_certain_hit() && s->ignore_taunt_certain()) return false;
    return true;
}

std::vector<GameBattler*> GameAction::get_taunt_target(std::vector<GameBattler*> targets) {
    GameUnit& unit = opponents_unit();
    GameBattler* target = unit.smooth_target(target_index);
    auto pick = [&](std::vector<GameBattler*> group) {
        if (std::find(group.begin(), group.end(), target) == group.end()) {
            target = group[(size_t)std::floor(rng::next() * group.size())];
            targets = {target};
        }
    };
    if (is_physical() && !unit.physical_taunt_members().empty()) pick(unit.physical_taunt_members());
    else if (is_magical() && !unit.magical_taunt_members().empty()) pick(unit.magical_taunt_members());
    else if (is_certain_hit() && !unit.certain_taunt_members().empty()) pick(unit.certain_taunt_members());
    return targets;
}

bool GameAction::can_process_steal(GameBattler* target) const {
    if (!item().valid()) return false;
    if (!subject()->is_actor()) return false;
    if (!target->is_enemy()) return false;
    return item_data()["steal"].string() == "steal";
}

bool GameAction::can_process_snatch(GameBattler* target) const {
    if (!item().valid()) return false;
    if (!subject()->is_actor()) return false;
    if (!target->is_enemy()) return false;
    if (!needs_selection()) return false;
    return item_data()["steal"].string() == "snatch";
}

bool GameAction::match_steal_type(GameBattler* target, const StealableItem* s) const {
    (void)target;
    if (!s || s->is_stolen) return false;
    const Value& types = item_data()["stealType"];
    for (size_t i = 0; i < types.size(); i++) {
        if (types[i].string() == "all" || types[i].string() == s->type) return true;
    }
    return false;
}

double GameAction::get_stealable_rate(GameBattler* target, const StealableItem* s) {
    double rate = js_parse_float(s->rate);
    rate += item_data()["stealRate"]["all"].num();
    rate += item_data()["stealRate"][s->type.c_str()].num();
    rate += subject()->steal_rate_bonus_type(s->type);
    rate += steal_bonus_formula(target);
    rate -= static_cast<GameEnemy*>(target)->steal_resist();
    rate = custom_steal_rate_eval(target, rate);
    return rate;
}

double GameAction::steal_bonus_formula(GameBattler* target) {
    const Value* ast = yanfly_ast("StealBonusFormula");
    if (!ast) return 0;
    JsEval e(to_js());
    init_eval(e, target);
    e.set("skill", item().to_js()).num("rate", 0);
    if (!e.run(ast, "STEAL BONUS FORMULA ERROR")) return 0;
    return e.completion().to_num();
}

double GameAction::custom_steal_rate_eval(GameBattler* target, double rate) {
    if (item_data()["stealRateEval"].string().empty()) return rate;
    const Value* ast = obj_ast(item_data(), "stealRateEval");
    if (!ast) return rate;
    JsEval e(to_js());
    init_eval(e, target);
    e.set("skill", item().to_js()).num("rate", rate);
    e.run(ast, "STEAL RATE FORMULA ERROR");
    return e.get("rate").to_num();
}

void GameAction::apply_item_steal_effect(GameBattler* target) {
    if (!target || !can_process_steal(target)) return;
    GameEnemy* en = static_cast<GameEnemy*>(target);
    if (en->all_items_stolen(item())) {
        display_steal_empty(target);
        return;
    }
    for (auto& st : en->stealable_items()) {
        if (!match_steal_type(target, &st)) continue;
        double rate = get_stealable_rate(target, &st);
        if (rng::next() < rate) {
            get_stealable_item(target, &st);
            return;
        }
    }
    display_steal_failure(target);
}

void GameAction::apply_item_snatch_effect(GameBattler* target) {
    if (!target || !can_process_snatch(target)) return;
    GameEnemy* en = static_cast<GameEnemy*>(target);
    auto& list = en->stealable_items();
    StealableItem* st = snatch_target_index >= 0 && snatch_target_index < (int)list.size() ? &list[(size_t)snatch_target_index] : nullptr;
    if (match_steal_type(target, st)) {
        double rate = get_stealable_rate(target, st);
        if (rng::next() < rate) {
            get_stealable_item(target, st);
            return;
        }
    }
    display_steal_failure(target);
}

void GameAction::play_steal_sound(const Value& sound) {
    if (!sound.is_obj()) return;
    if (!sound["name"].string().empty()) audio_manager::play_se(AudioObj::from(sound));
}

void GameAction::get_stealable_item(GameBattler* target, StealableItem* s) {
    s->is_stolen = true;
    int id = s->id;
    std::string name, icon;
    ItemRef ref;
    if (s->type == "item") ref = ItemRef::item(id);
    else if (s->type == "weapon") ref = ItemRef::weapon(id);
    else if (s->type == "armor") ref = ItemRef::armor(id);
    if (ref.valid()) {
        const Value& it = ref.data();
        play_steal_sound(it["stealSound"]);
        game_party().gain_item(ref, 1);
        name = it["name"].string();
        icon = "\\i[" + std::to_string(it["iconIndex"].as_int()) + "]";
        after_steal_effect(target, ref);
    } else if (s->type == "gold") {
        game_party().gain_gold(id);
        play_steal_sound(yanfly_param("StealSEGold"));
        name = js_format(yanfly_param("StealGoldFmt").string(), {js::number_to_string(id), data::system()["currencyUnit"].string()});
        int gold_icon = data::root()["yanfly"]["Icon"]["Gold"].as_int();
        if (gold_icon > 0) icon = "\\i[" + std::to_string(gold_icon) + "]";
        subject()->after_steal_success_eval(target, item(), ItemRef());
    }
    make_battle_engine_core_steal_wait();
    std::string fmt = yanfly_param("StealSuccess").string();
    if (fmt.empty()) return;
    display_steal_text(js_format(fmt, {subject()->name(), target->name(), name, icon}));
}

void GameAction::after_steal_effect(GameBattler* target, const ItemRef& it) {
    subject()->after_steal_success_eval(target, item(), it);
    subject()->after_steal_eval(target, item(), it);
}

void GameAction::make_battle_engine_core_steal_wait() {
    double frames = yanfly_param("StealWait").num();
    if (frames > 0) battle::push_action_list_wait((int)frames);
}

void GameAction::display_steal_text(const std::string& text0) {
    if (!party_in_battle()) return;
    if (text0.empty()) return;
    std::string text = text0;
    if (yanfly_param("StealCenter").truthy()) text = "<CENTER>" + text;
    battle::log_push_line(text);
}

void GameAction::display_steal_failure(GameBattler* target) {
    play_steal_sound(yanfly_param("StealSEFail"));
    std::string fmt = yanfly_param("StealFail").string();
    if (fmt.empty()) return;
    display_steal_text(js_format(fmt, {subject()->name(), target->name()}));
    make_battle_engine_core_steal_wait();
}

void GameAction::display_steal_empty(GameBattler* target) {
    play_steal_sound(yanfly_param("StealSEEmpty"));
    std::string fmt = yanfly_param("StealEmpty").string();
    if (fmt.empty()) return;
    display_steal_text(js_format(fmt, {target->name()}));
    make_battle_engine_core_steal_wait();
}

void GameAction::set_active_chain_skill(int skill_id) {
    set_skill(skill_id);
    set_target(subject()->last_target_index);
    active_chain_skill = true;
}

bool GameAction::can_change_target_groups() const { return item().valid() && item_data()["meta"]["Enemy or Actor Select"].truthy(); }

bool GameAction::not_target_user() const { return item().valid() && item_data()["meta"]["Not User"].truthy(); }

bool GameAction::is_for_everybody() const {
    if (!item().valid()) return false;
    const Value* t = item_data()["meta"].find("Target");
    return t && t->is_str() && to_lower(trim(t->string())) == "everybody";
}

}
