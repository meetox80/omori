#include <algorithm>
#include <cmath>

#include "../js/dataview.h"
#include "../js/random.h"
#include "action.h"
#include "audio_manager.h"
#include "battle.h"
#include "battler.h"
#include "data.h"
#include "enemy.h"
#include "evalctx.h"
#include "game.h"
#include "params.h"
#include "party.h"

namespace rpg {

namespace {

bool contains_int(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

void extend_unique(std::vector<int>& dst, const Value& src) {
    for (size_t i = 0; i < src.size(); i++) {
        int id = src[i].as_int();
        if (!contains_int(dst, id)) dst.push_back(id);
    }
}

void extend_unique(std::vector<int>& dst, const js::Val& src) {
    if (!src.is_array()) return;
    for (auto& v : src.o->items) {
        int id = v.to_int();
        if (!contains_int(dst, id)) dst.push_back(id);
    }
}

js::Val jsv(GameBattler* b) { return b ? b->to_js() : js::Val(); }

}

std::vector<const Value*> GameBattlerBase::all_traits() {
    std::vector<const Value*> out;
    for (const Value* o : trait_objects()) {
        const Value& t = (*o)["traits"];
        for (size_t i = 0; i < t.size(); i++) out.push_back(&t[i]);
    }
    return out;
}

bool GameBattlerBase::is_bypass_update_turns() { return game_troop().is_event_running(); }

void GameBattlerBase::update_state_ticks() {
    game_temp().custom_leave_effect_eval = true;
    bool need_refresh = false;
    for (size_t i = 0; i < states_.size(); ++i) {
        int id = states_[i];
        const Value& st = data::states()[id];
        if (!st.is_obj()) continue;
        if (st["autoRemovalTiming"].as_int() != 2) continue;
        auto t = state_turns.find(id);
        if (t == state_turns.end() || t->second == 0 || std::isnan(t->second)) continue;
        double value = battle::tick_rate() / yanfly_param("BECTurnTime").num();
        double shown1 = std::ceil(t->second);
        t->second -= value;
        double shown2 = std::ceil(t->second);
        if (shown1 != shown2) need_refresh = true;
        if (t->second <= 0) {
            if (GameBattler* b = dynamic_cast<GameBattler*>(this)) b->remove_state(id);
        }
    }
    if (need_refresh) refresh();
    game_temp().custom_leave_effect_eval = false;
}

void GameBattlerBase::update_state_action_start() {
    if (battle::is_countering()) return;
    update_state_turn_timing(3);
}

void GameBattlerBase::update_buff_ticks() {
    bool need_refresh = false;
    for (int i = 0; i < 8; i++) {
        if (buff_turns[i] <= 0) continue;
        double value = battle::tick_rate() / yanfly_param("BECTurnTime").num();
        double shown1 = std::ceil(buff_turns[i]);
        buff_turns[i] -= value;
        double shown2 = std::ceil(buff_turns[i]);
        if (shown1 != shown2) need_refresh = true;
        if (buff_turns[i] <= 0) {
            if (GameBattler* b = dynamic_cast<GameBattler*>(this)) b->remove_buff(i);
        }
    }
    if (need_refresh) refresh();
}

double GameBattlerBase::timed_tick() const { return 1 * battle::tick_rate(); }

js::Val GameBattlerBase::gauge(int n) const { return js::Val::str(n == 1 ? "HP" : n == 2 ? "MP" : "TP"); }

js::Val GameBattlerBase::gauge_icon(int n) const {
    (void)n;
    return js::Val::num(0);
}

void GameBattlerBase::clamp_state_counter(int id, double mn, double mx) {
    double v = get_state_counter(id).to_num();
    v = std::min(std::max(v, mn), mx);
    set_state_counter(id, js::Val::num(v));
}

bool GameBattlerBase::aura_state_condition_eval(const Value& state) {
    int id = state["id"].as_int();
    check_aura_state_condition.push_back(id);
    bool condition = true;
    if (const Value* ast = obj_ast(state, "auraConditionEval")) {
        js::Val self = to_js();
        JsEval e(self);
        e.set("condition", js::Val::boolean(true)).set("a", self).set("user", self).set("subject", self).set("b", self).set("target", self).sv();
        e.run(ast, "PASSIVE AURA CUSTOM CONDITION ERROR");
        condition = e.get("condition").truthy();
    }
    auto it = std::find(check_aura_state_condition.begin(), check_aura_state_condition.end(), id);
    if (it != check_aura_state_condition.end()) check_aura_state_condition.erase(it);
    return condition;
}

bool GameBattler::is_selected() {
    if (party_in_battle() && battle::is_all_selection()) {
        if (!is_appeared()) return false;
        auto action = battle::inputting_action();
        if (action && action->item().valid()) {
            if (is_dead() && is_enemy()) return false;
            if (is_dead() && is_actor()) return action->is_for_dead_friend();
            if (action->is_for_friend() && is_actor()) return true;
            if (action->is_for_opponent() && is_enemy()) return true;
        }
    }
    return selected;
}

GameBattler::Anim GameBattler::shift_animation() {
    if (animations.empty()) return Anim();
    Anim a = animations.front();
    animations.erase(animations.begin());
    return a;
}

GameActionResult GameBattler::shift_damage_popup() {
    if (damage_popup.empty()) return GameActionResult();
    GameActionResult r = damage_popup.front();
    damage_popup.erase(damage_popup.begin());
    return r;
}

void GameBattler::start_weapon_animation(int id) {
    weapon_image_id = id;
    if (sprite_link) sprite_link->link_setup_weapon_animation();
}

std::shared_ptr<GameAction> GameBattler::action(int index) const {
    return index >= 0 && index < (int)actions.size() ? actions[(size_t)index] : nullptr;
}

void GameBattler::set_action(int index, std::shared_ptr<GameAction> a) {
    if (index < 0) return;
    if ((int)actions.size() <= index) actions.resize((size_t)index + 1);
    actions[(size_t)index] = std::move(a);
}

int GameBattler::make_action_times() {
    int r = 1;
    for (double p : action_plus_set()) {
        if (rng::next() < p) r++;
    }
    return r;
}

void GameBattler::base_make_actions() {
    clear_actions();
    if (can_move()) {
        int times = make_action_times();
        actions.clear();
        for (int i = 0; i < times; i++) actions.push_back(std::make_shared<GameAction>(this));
    }
}

void GameBattler::make_actions() { base_make_actions(); }

void GameBattler::make_speed() {
    if (actions.empty()) {
        speed_ = 0;
        return;
    }
    double m = INFINITY;
    for (auto& a : actions) m = std::min(m, a ? a->speed() : NAN);
    speed_ = (std::isnan(m) || m == 0) ? 0 : m;
}

void GameBattler::set_last_target(GameBattler* target) { last_target_index = target ? target->index() : 0; }

void GameBattler::use_item(const ItemRef& item) {
    if (item.is_skill()) pay_skill_cost(item);
    else if (item.is_item()) consume_item(item);
    refresh();
    if (!party_in_battle()) return;
    increase_self_turn_count();
    update_state_action_start();
}

void GameBattler::init_tp() { set_tp(rng::below(25)); }

void GameBattler::charge_tp_by_damage(double rate) { gain_silent_tp(std::floor(50 * rate * tcr())); }

void GameBattler::regenerate_hp() {
    double value = std::floor(mhp() * hrg());
    value = std::max(value, -max_slip_damage());
    if (value != 0) gain_hp(value);
}

double GameBattler::max_slip_damage() { return data::system()["optSlipDeath"].truthy() ? hp() : std::max(hp() - 1, 0.0); }

void GameBattler::regenerate_mp() {
    double value = std::floor(mmp() * mrg());
    if (value != 0) gain_mp(value);
}

void GameBattler::regenerate_tp() { gain_silent_tp(std::floor(100 * trg())); }

void GameBattler::regenerate_all() {
    if (party_in_battle()) on_regenerate_state_effects();
    clear_result();
    bool life_state = is_alive();
    if (is_alive()) {
        regenerate_hp();
        regenerate_mp();
        regenerate_tp();
    }
    if (party_in_battle()) {
        if (!battle::time_based_states()) update_state_turns();
        if (!battle::time_based_buffs()) {
            update_buff_turns();
            remove_buffs_auto();
        }
        if (is_dead() && life_state) perform_collapse();
        start_damage_popup();
    }
}

void GameBattler::on_battle_start() {
    set_action_state("undecided");
    clear_motion();
    if (!is_preserve_tp()) init_tp();
    free_state_turn.clear();
    immortal_state = false;
    self_turn_count = 0;
    counters = 0;
    active_chain_enabled = false;
    no_effect_message = js::Val();
    no_state_message = js::Val();
}

void GameBattler::on_all_actions_end() {
    clear_result();
    remove_states_auto(1);
    remove_buffs_auto();
    if (!battle::st().process_turn) update_state_action_end();
}

void GameBattler::on_turn_end() {
    if (party_in_battle()) refresh();
    clear_result();
    if (battle::is_turn_based()) regenerate_all();
    else if (battle::is_tick_based() && !battle::is_turn_end()) regenerate_all();
    remove_states_auto(2);
    if (meet_turn_end_state_effects_conditions()) on_turn_end_state_effects();
    no_effect_message = js::Val();
    no_state_message = js::Val();
}

void GameBattler::on_battle_end() {
    clear_result();
    remove_battle_states();
    remove_all_buffs();
    clear_actions();
    if (!is_preserve_tp()) clear_tp();
    appear();
    free_state_turn.clear();
    immortal_state = false;
    clear_enemy_state_origins();
    active_chain_enabled = false;
    no_effect_message = js::Val();
    no_state_message = js::Val();
}

void GameBattler::on_damage(double value) {
    remove_states_by_damage();
    charge_tp_by_damage(value / mhp());
}

void GameBattler::escape() {
    if (game_party().in_battle()) hide();
    clear_actions();
    clear_states();
    audio_manager::play_system_sound(8);
}

bool GameBattler::is_chanting() const {
    if (action_state != "waiting") return false;
    for (auto& a : actions) {
        if (a && a->is_magic_skill()) return true;
    }
    return false;
}

bool GameBattler::is_guard_waiting() const {
    if (action_state != "waiting") return false;
    for (auto& a : actions) {
        if (a && a->is_guard()) return true;
    }
    return false;
}

void GameBattler::perform_action_start(GameAction* action) {
    if (!action->is_guard()) set_action_state("acting");
}

void GameBattler::perform_action_end() {
    set_action_state("done");
    game_system().reset_damage_settings();
}

void GameBattler::perform_damage() {}

void GameBattler::perform_miss() { audio_manager::play_system_sound(17); }

void GameBattler::perform_recovery() { audio_manager::play_system_sound(16); }

void GameBattler::perform_evasion() { audio_manager::play_system_sound(18); }

void GameBattler::perform_magic_evasion() { audio_manager::play_system_sound(19); }

void GameBattler::perform_counter() { audio_manager::play_system_sound(18); }

void GameBattler::perform_reflection() {
    audio_manager::play_system_sound(20);
    if (is_actor()) return;
    start_animation(reflect_animation_id(), is_actor(), 0);
}

void GameBattler::perform_substitute(GameBattler* target) { (void)target; }

void GameBattler::perform_collapse() {}

void GameBattler::remove_immortal() {
    bool already = is_dead();
    immortal_state = false;
    refresh();
    if (is_dead() && !already) perform_collapse();
}

void GameBattler::perform_result_effects() {
    GameActionResult& r = result();
    if (r.missed && r.physical) perform_miss();
    if (r.evaded) {
        if (r.physical) perform_evasion();
        else perform_magic_evasion();
    }
    if (r.hp_affected) {
        if (r.hp_damage > 0 && !r.drain) perform_damage();
        if (r.hp_damage < 0) perform_recovery();
    }
    if (is_alive() && r.mp_damage != 0 && r.mp_damage < 0) perform_recovery();
    if (is_alive() && r.tp_damage != 0 && r.tp_damage < 0) perform_recovery();
}

void GameBattler::force_motion(const std::string& motion) {
    motion_type = motion;
    if (sprite_link) sprite_link->link_force_motion(motion);
}

int GameBattler::reflect_animation_id() {
    for (const Value* s : states()) {
        if ((*s)["reflectAnimationId"].num() > 0) return (*s)["reflectAnimationId"].as_int();
    }
    return yanfly_param("BECReflectAni").as_int();
}

double GameBattler::sprite_pos_x() const { return sprite_link ? sprite_link->link_x() : 0; }

double GameBattler::sprite_pos_y() const { return sprite_link ? sprite_link->link_y() : 0; }

double GameBattler::sprite_width() {
    if (GameEnemy* e = dynamic_cast<GameEnemy*>(this)) {
        if (e->has_sv_battler()) {
            js::Val w = e->sideview_width();
            if (!(w.is_str() && w.to_str() == "auto")) return std::floor(w.to_num());
        }
    }
    double v = sprite_link ? sprite_link->link_bitmap_width() : 1;
    return std::floor(v);
}

double GameBattler::sprite_height() {
    if (GameEnemy* e = dynamic_cast<GameEnemy*>(this)) {
        if (e->has_sv_battler()) {
            js::Val h = e->sideview_height();
            if (!(h.is_str() && h.to_str() == "auto")) return std::floor(h.to_num());
        }
    }
    double v = sprite_link ? sprite_link->link_bitmap_height() : 1;
    return std::floor(v);
}

void GameBattler::force_motion_refresh() {
    if (!party_in_battle()) return;
    if (sprite_link) sprite_link->link_refresh_motion();
}

void GameBattler::on_turn_start() {
    update_state_turn_start();
    counters = 0;
    on_turn_start_state_effects();
    no_effect_message = js::Val();
    no_state_message = js::Val();
}

void GameBattler::update_tick() {
    if (battle::time_based_states()) update_state_ticks();
    if (battle::time_based_buffs()) update_buff_ticks();
}

double GameBattler::turn_count() const {
    if (battle::is_turn_based()) return game_troop().turn_count();
    if (battle::is_tick_based() && yanfly_param("BECAISelfTurn").truthy()) return self_turn_count;
    return game_troop().turn_count();
}

void GameBattler::create_actions() {
    if (current_action()) return;
    make_actions();
}

void GameBattler::sprite_opacity(double opacity, double duration) {
    if (!sprite_link) return;
    sprite_link->link_setup_opacity_change(opacity, duration);
}

double GameBattler::camera_pos_y() {
    double value = sprite_pos_y();
    if (sprite_link) {
        value -= sprite_link->link_float_height() * sprite_height();
        value -= sprite_link->link_jump_height() * sprite_height();
    }
    return value;
}

std::vector<ItemRef> GameBattler::counter_skills() const {
    std::vector<ItemRef> out;
    if (!has_counter_skills) return out;
    for (int id : counter_skills_) out.push_back(ItemRef::skill(id));
    return out;
}

void GameBattler::base_make_counter_skills() {
    counter_skills_.clear();
    has_counter_skills = true;
    for (const Value* obj : states()) {
        if (!obj) continue;
        const Value& cs = (*obj)["counterSkills"];
        if (cs.is_arr()) extend_unique(counter_skills_, cs);
        extend_counter_skills_eval(*obj);
    }
}

void GameBattler::make_counter_skills() { base_make_counter_skills(); }

void GameBattler::extend_counter_skills_eval(const Value& obj) {
    if (!obj.is_obj()) return;
    if (obj["counterSkillsEval"].string().empty()) return;
    const Value* ast = obj_ast(obj, "counterSkillsEval");
    if (!ast) return;
    js::Val self = to_js();
    JsEval e(self);
    e.set("skills", js::make_array()).set("a", self).set("user", self).set("subject", self).set("b", self).set("target", self).sv();
    e.run(ast, "COUNTER SKILLS ERROR");
    extend_unique(counter_skills_, obj["counterSkills"]);
}

double GameBattler::get_counter_total_eval(const Value& obj) {
    if (!obj.is_obj()) return 0;
    if (obj["counterTotalEval"].string().empty()) return 0;
    const Value* ast = obj_ast(obj, "counterTotalEval");
    if (!ast) return 0;
    js::Val self = to_js();
    JsEval e(self);
    e.num("value", 0).set("a", self).set("user", self).set("subject", self).set("b", self).set("target", self).sv();
    e.run(ast, "COUNTER TOTAL ERROR");
    return e.get("value").to_num();
}

double GameBattler::base_counter_total() {
    double value = yanfly_param("CounterTotal").num();
    for (const Value* obj : states()) {
        value += (*obj)["counterTotal"].num();
        value += get_counter_total_eval(*obj);
    }
    return value;
}

double GameBattler::counter_total() {
    if (has_counter_total_cache) return counter_total_cache;
    counter_total_cache = base_counter_total();
    has_counter_total_cache = true;
    return counter_total_cache;
}

bool GameBattler::can_counter() {
    if (!can_move()) return false;
    return counter_total() >= counters;
}

double GameBattler::target_counter_rate() {
    double rate = 1;
    for (const Value* obj : states()) rate *= (*obj)["targetCounterRate"].num();
    return rate;
}

double GameBattler::target_counter_flat() {
    double value = 0;
    for (const Value* obj : states()) value += (*obj)["targetCounterFlat"].num();
    return value;
}

double GameBattler::target_counter_rate_eval(double rate, GameBattler* target, const ItemRef& item) {
    for (const Value* obj : states()) rate = get_target_cnt_rate_eval(obj, rate, target, item);
    return rate;
}

double GameBattler::get_target_cnt_rate_eval(const Value* obj, double rate, GameBattler* target, const ItemRef& item) {
    if (!obj || !obj->is_obj()) return rate;
    if ((*obj)["targetCounterRateEval"].string().empty()) return rate;
    const Value* ast = obj_ast(*obj, "targetCounterRateEval");
    if (!ast) return rate;
    js::Val self = to_js();
    JsEval e(self);
    e.set("skill", item.to_js()).set("a", self).set("user", self).set("subject", self).set("b", jsv(target)).set("target", jsv(target)).sv();
    e.num("rate", rate);
    e.run(ast, "TARGET COUNTER RATE ERROR");
    return e.get("rate").to_num();
}

bool GameBattler::force_evade_counter() {
    for (const Value* obj : states()) {
        if ((*obj)["evadeCounter"].truthy()) return true;
    }
    return false;
}

bool GameBattler::force_hit_counter() {
    for (const Value* obj : states()) {
        if ((*obj)["hitCounter"].truthy()) return true;
    }
    return false;
}

void GameBattler::force_evade_popup() {
    result_.reset(new GameActionResult());
    result_->evaded = true;
    start_damage_popup();
    clear_result();
}

void GameBattler::set_counter_action(std::shared_ptr<GameAction> action) {
    if (!has_original_pre_counter_actions) {
        original_pre_counter_actions.clear();
        for (auto& a : actions) original_pre_counter_actions.push_back(a ? a->clone() : nullptr);
        has_original_pre_counter_actions = true;
    }
    action->set_counter();
    set_action(0, action);
}

void GameBattler::remove_counter_action() {
    if (has_original_pre_counter_actions) {
        actions.clear();
        for (auto& a : original_pre_counter_actions) actions.push_back(a ? a->clone() : nullptr);
    }
    has_original_pre_counter_actions = false;
    original_pre_counter_actions.clear();
}

bool GameBattler::has_state(int id) {
    const Value* st = &data::states()[id];
    for (const Value* s : states()) {
        if (s == st) return true;
    }
    return false;
}

double GameBattler::ai_level() const { return yanfly_param("CoreAIDefaultLevel").num() * 0.01; }

void GameBattler::on_turn_start_state_effects() {
    for (const Value* s : states()) custom_effect_eval((*s)["id"].as_int(), "turnStartState");
}

void GameBattler::on_turn_end_state_effects() {
    for (const Value* s : states()) custom_effect_eval((*s)["id"].as_int(), "turnEndState");
}

bool GameBattler::meet_turn_end_state_effects_conditions() {
    if (!party_in_battle()) return false;
    if (battle::is_turn_based()) return true;
    if (battle::is_tick_based() && !battle::is_turn_end()) return true;
    return false;
}

void GameBattler::on_regenerate_state_effects() {
    for (const Value* s : states()) regenerate_state_effects((*s)["id"].as_int());
}

void GameBattler::regenerate_state_effects(int state_id) {
    if (party_in_battle()) clear_result();
    bool life_state = is_alive();
    custom_effect_eval(state_id, "regenerateState");
    if (party_in_battle() && is_dead() && life_state) perform_collapse();
    if (party_in_battle()) start_damage_popup();
}

void GameBattler::on_action_start_state_effects() {
    for (const Value* s : states()) custom_effect_eval((*s)["id"].as_int(), "actionStartState");
}

void GameBattler::on_action_end_state_effects() {
    for (const Value* s : states()) custom_effect_eval((*s)["id"].as_int(), "actionEndState");
}

void GameBattler::remove_state_category_effect(const Value& obj, GameBattler* user) {
    const Value& cats = obj["removeCategory"];
    for (const Member& m : cats.members()) {
        std::string category = m.key;
        if (m.v.is_str() && m.v.string() == "ALL") {
            remove_state_category_all(category);
        } else {
            double value = remove_state_category_eval(obj, category, user, m.v.num());
            remove_state_category(category, (int)value);
        }
    }
}

double GameBattler::remove_state_category_eval(const Value& obj, const std::string& c, GameBattler* user, double value) {
    if (!obj["removeCategoryEval"][c.c_str()].truthy()) return value;
    const Value* ast = obj_ast(obj, "removeCategoryEval." + c);
    if (!ast) return value;
    JsEval e(to_js());
    ItemRef ref = ItemRef::from_data(&obj);
    e.set("category", js::Val::str(c)).set("item", ref.to_js()).set("skill", ref.to_js()).set("a", jsv(user)).set("subject", jsv(user));
    e.set("b", to_js()).set("target", to_js()).sv().num("value", value);
    e.run(ast, "REMOVE STATE CATEGORY ERROR");
    return e.get("value").to_num();
}

bool GameBattler::is_damage_capped() {
    for (const Value* s : states()) {
        if ((*s)["breakDamageCap"].truthy()) {
            has_dmg_capped = true;
            dmg_capped_cache = false;
            return false;
        }
    }
    has_dmg_capped = true;
    dmg_capped_cache = game_system().default_damage_cap;
    return dmg_capped_cache;
}

double GameBattler::maximum_damage() {
    double value = game_system().maximum_damage();
    for (const Value* s : states()) {
        if ((*s)["damageCap"].truthy()) value = std::max(value, (*s)["damageCap"].num());
    }
    return value;
}

double GameBattler::maximum_healing() {
    double value = game_system().maximum_healing();
    for (const Value* s : states()) {
        if ((*s)["healCap"].truthy()) value = std::min(value, (*s)["healCap"].num());
    }
    return value;
}

double GameBattler::states_sum(const char* key) const {
    double v = 0;
    for (const Value* s : const_cast<GameBattler*>(this)->states()) {
        if (s) v += (*s)[key].num();
    }
    return v;
}

double GameBattler::critical_multiplier_bonus() { return std::max(0.0, states_sum("critMultBonus")); }
double GameBattler::flat_critical_bonus() { return states_sum("flatCritBonus"); }
double GameBattler::certain_crit_rate_bonus() { return std::max(0.0, states_sum("certainCritRateBonus")); }
double GameBattler::physical_crit_rate_bonus() { return std::max(0.0, states_sum("physicalCritRateBonus")); }
double GameBattler::magical_crit_rate_bonus() { return std::max(0.0, states_sum("magicalCritRateBonus")); }

void GameBattler::life_steal(double damage, const std::string& type, GameBattler* target, double rate, double flat, bool mp) {
    if (type.empty()) return;
    rate = 1 - rate;
    rate *= 1 - get_life_steal_rate(type + "Rate", target);
    flat += get_life_steal_flat(type + "Flat", target);
    double steal;
    bool neg = yanfly_param(mp ? "LSMPNeg" : "LSHPNeg").truthy();
    if (neg) steal = std::floor(damage * (1 - rate) + flat);
    else steal = std::max(0.0, std::floor(damage * (1 - rate) + flat));
    if (yanfly_param(mp ? "LSMPOver" : "LSHPOver").truthy()) steal = std::min(steal, damage);
    if (steal <= 0) return;
    if (mp) gain_mp(steal);
    else gain_hp(steal);
}

double GameBattler::life_steal_eval(const Value* ast, GameBattler* target, const char* var, const char* where) {
    if (!target) target = this;
    js::Val self = to_js();
    JsEval e(self);
    e.num(var, 0).set("a", self).set("user", self).set("subject", self).set("b", jsv(target)).set("target", jsv(target)).sv();
    e.run(ast, where);
    return e.get(var).to_num();
}

double GameBattler::get_life_steal_rate(const std::string& type, GameBattler* target) {
    double rate = 1;
    for (const Value* obj : states()) {
        const Value& ls = (*obj)["lifeSteal"];
        if (!ls.is_obj()) continue;
        if (ls[type.c_str()].truthy()) rate *= 1 - ls[type.c_str()].num();
        if (ls[(type + "Eval").c_str()].truthy()) {
            if (const Value* ast = obj_ast(*obj, "lifeSteal." + type + "Eval")) rate *= 1 - life_steal_eval(ast, target, "rate", "LIFE STEAL RATE FORMULA ERROR");
        }
    }
    return 1 - rate;
}

double GameBattler::get_life_steal_flat(const std::string& type, GameBattler* target) {
    double value = 0;
    for (const Value* obj : states()) {
        const Value& ls = (*obj)["lifeSteal"];
        if (!ls.is_obj()) continue;
        if (ls[type.c_str()].truthy()) value += ls[type.c_str()].num();
        if (ls[(type + "Eval").c_str()].truthy()) {
            if (const Value* ast = obj_ast(*obj, "lifeSteal." + type + "Eval")) value += life_steal_eval(ast, target, "flat", "LIFE STEAL FLAT FORMULA ERROR");
        }
    }
    return value;
}

bool GameBattler::is_life_steal_state(const std::string& type) const {
    for (const Value* obj : const_cast<GameBattler*>(this)->states()) {
        const Value& ls = (*obj)["lifeSteal"];
        if (ls.is_obj() && ls[type.c_str()].truthy()) return true;
    }
    return false;
}

double GameBattler::steal_rate_bonus(int index) const {
    double rate = 0;
    for (const Value* s : const_cast<GameBattler*>(this)->states()) rate += (*s)["stealRateBonus"][index].num();
    if (const GameActor* a = dynamic_cast<const GameActor*>(this)) rate += a->steal_rate_bonus_actor(index);
    return rate;
}

double GameBattler::steal_rate_bonus_type(const std::string& type) const {
    static const char* arr[] = {"all", "item", "weapon", "armor", "gold"};
    int index = -1;
    for (int i = 0; i < 5; i++) {
        if (type == arr[i]) index = i;
    }
    return steal_rate_bonus(index);
}

void GameBattler::after_steal_eval(GameBattler* target, const ItemRef& skill, const ItemRef& item) {
    if (!item.valid() || item.data()["afterStealEval"].string().empty()) return;
    const Value* ast = obj_ast(item.data(), "afterStealEval");
    if (ast) {
        js::Val self = to_js();
        JsEval e(self);
        e.set("a", self).set("user", self).set("subject", self).set("b", jsv(target)).set("target", jsv(target)).sv();
        e.set("skill", skill.to_js()).set("item", item.to_js());
        e.run(ast, "AFTER STEAL CUSTOM EFFECT ERROR");
    }
    refresh();
    target->refresh();
}

void GameBattler::after_steal_success_eval(GameBattler* target, const ItemRef& skill, const ItemRef& item) {
    if (!skill.valid() || skill.data()["stealSuccessEval"].string().empty()) return;
    const Value* ast = obj_ast(skill.data(), "stealSuccessEval");
    if (ast) {
        js::Val self = to_js();
        JsEval e(self);
        e.set("a", self).set("user", self).set("subject", self).set("b", jsv(target)).set("target", jsv(target)).sv();
        e.set("skill", skill.to_js()).set("item", item.to_js());
        e.run(ast, "AFTER STEAL SUCCESS EFFECT ERROR");
    }
    refresh();
    target->refresh();
}

std::string GameBattler::get_state_emotion() {
    for (const Value* s : states()) {
        std::string t = actor_meta(*s, "TransformEmotion");
        if (!t.empty()) return to_lower(trim(t));
    }
    return "normal";
}

void GameBattler::add_active_skill_chain(int skill_id) {
    auto a = std::make_shared<GameAction>(this);
    a->set_active_chain_skill(skill_id);
    if (actions.size() <= 1) actions.push_back(a);
    else actions.insert(actions.begin() + 1, a);
}

bool GameBattler::is_emotion_addable(const std::string& type0) {
    std::string type = to_lower(type0);
    if (type == "happy") {
        if (is_state_affected(7)) return is_state_addable(8);
        if (is_state_affected(6)) return is_state_addable(7);
        return is_state_addable(6);
    }
    if (type == "sad") {
        if (is_state_affected(11)) return is_state_addable(12);
        if (is_state_affected(10)) return is_state_addable(11);
        return is_state_addable(10);
    }
    if (type == "angry") {
        if (is_state_affected(15)) return is_state_addable(16);
        if (is_state_affected(14)) return is_state_addable(15);
        return is_state_addable(14);
    }
    if (type == "afraid") return is_state_addable(18);
    return false;
}

void GameBattler::force_action(int skill_id, int target_index) {
    clear_actions();
    auto a = std::make_shared<GameAction>(this, true);
    a->set_skill(skill_id);
    if (target_index == -2) a->set_target(last_target_index);
    else if (target_index == -1) a->decide_random_target();
    else a->set_target(target_index);
    actions.push_back(a);
}

bool GameActor::is_sprite_visible() const { return yanfly_param("BECFrontSprite").truthy(); }

void GameActor::perform_action(GameAction* action) {
    if (action->is_attack()) perform_attack();
    else if (action->is_guard()) request_motion("guard");
    else if (action->is_magic_skill()) request_motion("spell");
    else if (action->is_skill()) request_motion("skill");
    else if (action->is_item()) request_motion("item");
}

void GameActor::perform_attack() {
    auto w = weapons();
    int wtype = w.empty() ? 0 : w[0].data()["wtypeId"].as_int();
    const Value& am = data::system()["attackMotions"][wtype];
    if (am.is_obj()) {
        int t = am["type"].as_int();
        if (t == 0) force_motion("thrust");
        else if (t == 1) force_motion("swing");
        else if (t == 2) force_motion("missile");
        start_weapon_animation(am["weaponImageId"].as_int());
    }
}

void GameActor::perform_damage() {
    if (is_sprite_visible()) request_motion("damage");
    else game_screen().start_shake(5, 5, 10);
}

void GameActor::perform_evasion() {
    GameBattler::perform_evasion();
    request_motion("evade");
}

void GameActor::perform_magic_evasion() {
    GameBattler::perform_magic_evasion();
    request_motion("evade");
}

void GameActor::perform_counter() {
    GameBattler::perform_counter();
    perform_attack();
}

void GameActor::perform_victory() {
    if (can_move()) request_motion("victory");
    use_victory_face = true;
}

void GameActor::perform_escape() {
    if (can_move()) request_motion("escape");
}

void GameActor::perform_escape_success() {
    if (sprite_link) {
        perform_escape();
        sprite_link->link_start_move(300, 0, 60);
    }
}

std::vector<std::shared_ptr<GameAction>> GameActor::make_action_list() {
    std::vector<std::shared_ptr<GameAction>> list;
    auto a = std::make_shared<GameAction>(this);
    a->set_attack();
    list.push_back(a);
    for (auto& s : usable_skills()) {
        auto b = std::make_shared<GameAction>(this);
        b->set_skill(s.id);
        list.push_back(b);
    }
    return list;
}

void GameActor::make_auto_battle_actions() {
    for (int i = 0; i < num_actions(); i++) {
        auto list = make_action_list();
        double max_value = 5e-324;
        for (auto& a : list) {
            double v = a->evaluate();
            if (v > max_value) {
                max_value = v;
                set_action(i, a);
            }
        }
    }
    set_action_state("waiting");
}

void GameActor::make_confusion_actions() {
    for (int i = 0; i < num_actions(); i++) action(i)->set_confusion();
    set_action_state("waiting");
}

void GameActor::make_actions() {
    base_make_actions();
    if (num_actions() > 0) set_action_state("undecided");
    else set_action_state("waiting");
    if (is_auto_battle()) make_auto_battle_actions();
    else if (is_confused()) make_confusion_actions();
}

bool GameActor::select_next_command() {
    if (action_input_index < num_actions() - 1) {
        action_input_index++;
        return true;
    }
    return false;
}

bool GameActor::select_previous_command() {
    if (action_input_index > 0) {
        action_input_index--;
        return true;
    }
    return false;
}

bool GameActor::test_escape(const ItemRef& item) const {
    const Value& effects = item.data()["effects"];
    for (size_t i = 0; i < effects.size(); i++) {
        if (effects[i].is_obj() && effects[i]["code"].as_int() == 41) return true;
    }
    return false;
}

int GameActor::reflect_animation_id() {
    if (actor()["reflectAnimationId"].num() > 0) return actor()["reflectAnimationId"].as_int();
    if (current_class()["reflectAnimationId"].num() > 0) return current_class()["reflectAnimationId"].as_int();
    for (auto& e : equips()) {
        if (e.valid() && e.data()["reflectAnimationId"].num() > 0) return e.data()["reflectAnimationId"].as_int();
    }
    return GameBattler::reflect_animation_id();
}

bool GameActor::sprite_can_move() {
    if (actor()["spriteCannotMove"].truthy()) return false;
    if (current_class()["spriteCannotMove"].truthy()) return false;
    for (auto& e : equips()) {
        if (e.valid() && e.data()["spriteCannotMove"].truthy()) return false;
    }
    return GameBattler::sprite_can_move();
}

std::string GameActor::attack_motion() {
    auto w = weapons();
    int wtype = w.empty() ? 0 : w[0].data()["wtypeId"].as_int();
    const Value& am = data::system()["attackMotions"][wtype];
    if (am.is_obj()) {
        int t = am["type"].as_int();
        if (t == 0) return "thrust";
        if (t == 1) return "swing";
        if (t == 2) return "missile";
    }
    return "thrust";
}

bool GameActor::has_custom_battle_commands() const { return battle_commands_list.is_array() && !battle_commands_list.o->items.empty(); }

bool GameActor::can_show_item_battle_command(const ItemRef& item) {
    const Value& meta = item.data()["meta"];
    if (meta["CommandHideUntilUsable"].truthy() && !can_use(item)) return false;
    if (meta["CommandHideUntilLearned"].truthy() && !is_learned_skill(item.id)) return false;
    if (meta["CommandHideUntilSwitch"].truthy() && !game_switches().value((int)js_number(meta["CommandHideUntilSwitch"].string()))) return false;
    return true;
}

js::Val GameActor::gauge(int n) const {
    std::string key = "gauge" + std::to_string(n);
    GameActor* self = const_cast<GameActor*>(this);
    for (auto& e : self->equips()) {
        if (!e.valid()) continue;
        const Value& g = e.data()[key.c_str()];
        if (g.is_str() && g.string() == "UNDEFINED") continue;
        return js::value_to_js(g);
    }
    for (const Value* s : self->states()) {
        const Value& g = (*s)[key.c_str()];
        if (g.is_str() && g.string() == "UNDEFINED") continue;
        return js::value_to_js(g);
    }
    return js::value_to_js(current_class()[key.c_str()]);
}

js::Val GameActor::gauge_icon(int n) const {
    std::string key = "gaugeIcon" + std::to_string(n);
    GameActor* self = const_cast<GameActor*>(this);
    for (auto& e : self->equips()) {
        if (!e.valid()) continue;
        const Value& g = e.data()[key.c_str()];
        if (g.is_str() && g.string() == "UNDEFINED") continue;
        return js::value_to_js(g);
    }
    for (const Value* s : self->states()) {
        const Value& g = (*s)[key.c_str()];
        if (g.is_str() && g.string() == "UNDEFINED") continue;
        return js::value_to_js(g);
    }
    return js::value_to_js(current_class()[key.c_str()]);
}

void GameActor::make_counter_skills() {
    base_make_counter_skills();
    const Value* obj = nullptr;
    for (auto& e : equips()) {
        if (!e.valid()) continue;
        obj = &e.data();
        if ((*obj)["counterSkills"].is_arr()) extend_unique(counter_skills_, (*obj)["counterSkills"]);
        extend_counter_skills_eval(*obj);
    }
    if (current_class()["counterSkills"].truthy()) {
        obj = &current_class();
        extend_unique(counter_skills_, (*obj)["counterSkills"]);
        extend_counter_skills_eval(*obj);
    }
    if (actor()["counterSkills"].truthy()) {
        obj = &actor();
        extend_unique(counter_skills_, (*obj)["counterSkills"]);
        extend_counter_skills_eval(*obj);
    }
    if (counter_skills_.empty()) counter_skills_.push_back(obj ? (*obj)["defaultCounter"].as_int() : 0);
}

double GameActor::target_counter_rate() {
    double rate = GameBattler::target_counter_rate();
    for (auto& e : equips()) {
        if (e.valid()) rate *= e.data()["targetCounterRate"].num();
    }
    rate *= current_class()["targetCounterRate"].num();
    rate *= actor()["targetCounterRate"].num();
    return rate;
}

double GameActor::target_counter_flat() {
    double value = GameBattler::target_counter_flat();
    for (auto& e : equips()) {
        if (e.valid()) value += e.data()["targetCounterFlat"].num();
    }
    value += current_class()["targetCounterFlat"].num();
    value += actor()["targetCounterFlat"].num();
    return value;
}

double GameActor::target_counter_rate_eval(double rate, GameBattler* target, const ItemRef& item) {
    rate = GameBattler::target_counter_rate_eval(rate, target, item);
    for (auto& e : equips()) rate = get_target_cnt_rate_eval(e.valid() ? &e.data() : nullptr, rate, target, item);
    rate = get_target_cnt_rate_eval(&current_class(), rate, target, item);
    rate = get_target_cnt_rate_eval(&actor(), rate, target, item);
    return rate;
}

bool GameActor::force_evade_counter() {
    if (GameBattler::force_evade_counter()) return true;
    if (actor()["evadeCounter"].truthy()) return true;
    if (current_class()["evadeCounter"].truthy()) return true;
    for (auto& e : equips()) {
        if (e.valid() && e.data()["evadeCounter"].truthy()) return true;
    }
    return false;
}

bool GameActor::force_hit_counter() {
    if (GameBattler::force_hit_counter()) return true;
    if (actor()["hitCounter"].truthy()) return true;
    if (current_class()["hitCounter"].truthy()) return true;
    for (auto& e : equips()) {
        if (e.valid() && e.data()["hitCounter"].truthy()) return true;
    }
    return false;
}

bool GameActor::is_damage_capped() {
    if (has_dmg_capped) return dmg_capped_cache;
    auto cap = [&](bool v) {
        has_dmg_capped = true;
        dmg_capped_cache = v;
        return v;
    };
    if (actor()["breakDamageCap"].truthy()) return cap(false);
    if (current_class()["breakDamageCap"].truthy()) return cap(false);
    for (auto& e : equips()) {
        if (e.valid() && e.data()["breakDamageCap"].truthy()) return cap(false);
    }
    return GameBattler::is_damage_capped();
}

double GameActor::maximum_damage() {
    if (has_maximum_damage) return maximum_damage_cache;
    double value = GameBattler::maximum_damage();
    if (actor()["damageCap"].truthy()) value = std::max(value, actor()["damageCap"].num());
    if (current_class()["damageCap"].truthy()) value = std::max(value, current_class()["damageCap"].num());
    for (auto& e : equips()) {
        if (e.valid() && e.data()["damageCap"].truthy()) value = std::max(value, e.data()["damageCap"].num());
    }
    has_maximum_damage = true;
    maximum_damage_cache = value;
    return value;
}

double GameActor::maximum_healing() {
    if (has_maximum_healing) return maximum_healing_cache;
    double value = GameBattler::maximum_healing();
    if (actor()["healCap"].truthy()) value = std::min(value, actor()["healCap"].num());
    if (current_class()["healCap"].truthy()) value = std::min(value, current_class()["healCap"].num());
    for (auto& e : equips()) {
        if (e.valid() && e.data()["healCap"].truthy()) value = std::min(value, e.data()["healCap"].num());
    }
    has_maximum_healing = true;
    maximum_healing_cache = value;
    return value;
}

static double actor_bonus_sum(GameActor* a, const char* key) {
    double v = a->actor()[key].num() + a->current_class()[key].num();
    for (auto& e : a->equips()) {
        if (e.valid()) v += e.data()[key].num();
    }
    return v;
}

double GameActor::critical_multiplier_bonus() { return GameBattler::critical_multiplier_bonus() + actor_bonus_sum(this, "critMultBonus"); }
double GameActor::flat_critical_bonus() { return GameBattler::flat_critical_bonus() + actor_bonus_sum(this, "flatCritBonus"); }
double GameActor::certain_crit_rate_bonus() { return GameBattler::certain_crit_rate_bonus() + actor_bonus_sum(this, "certainCritRateBonus"); }
double GameActor::physical_crit_rate_bonus() { return GameBattler::physical_crit_rate_bonus() + actor_bonus_sum(this, "physicalCritRateBonus"); }
double GameActor::magical_crit_rate_bonus() { return GameBattler::magical_crit_rate_bonus() + actor_bonus_sum(this, "magicalCritRateBonus"); }

double GameActor::get_life_steal_rate(const std::string& type, GameBattler* target) {
    double rate = 1 - GameBattler::get_life_steal_rate(type, target);
    auto apply = [&](const Value& obj) {
        const Value& ls = obj["lifeSteal"];
        if (!ls.is_obj()) return;
        if (ls[type.c_str()].truthy()) rate *= 1 - ls[type.c_str()].num();
        if (ls[(type + "Eval").c_str()].truthy()) {
            if (const Value* ast = obj_ast(obj, "lifeSteal." + type + "Eval")) rate *= 1 - life_steal_eval(ast, target, "rate", "LIFE STEAL RATE FORMULA ERROR");
        }
    };
    for (auto& e : equips()) {
        if (e.valid()) apply(e.data());
    }
    rate *= 1 - actor()["lifeSteal"][type.c_str()].num();
    if (actor()["lifeSteal"][(type + "Eval").c_str()].truthy()) {
        if (const Value* ast = obj_ast(actor(), "lifeSteal." + type + "Eval")) rate *= 1 - life_steal_eval(ast, target, "rate", "LIFE STEAL RATE FORMULA ERROR");
    }
    rate *= 1 - current_class()["lifeSteal"][type.c_str()].num();
    if (current_class()["lifeSteal"][(type + "Eval").c_str()].truthy()) {
        if (const Value* ast = obj_ast(current_class(), "lifeSteal." + type + "Eval")) rate *= 1 - life_steal_eval(ast, target, "rate", "LIFE STEAL RATE FORMULA ERROR");
    }
    return 1 - rate;
}

double GameActor::get_life_steal_flat(const std::string& type, GameBattler* target) {
    double value = GameBattler::get_life_steal_flat(type, target);
    auto apply = [&](const Value& obj) {
        const Value& ls = obj["lifeSteal"];
        if (!ls.is_obj()) return;
        if (ls[type.c_str()].truthy()) value += ls[type.c_str()].num();
        if (ls[(type + "Eval").c_str()].truthy()) {
            if (const Value* ast = obj_ast(obj, "lifeSteal." + type + "Eval")) value += life_steal_eval(ast, target, "flat", "LIFE STEAL FLAT FORMULA ERROR");
        }
    };
    for (auto& e : equips()) {
        if (e.valid()) apply(e.data());
    }
    value += actor()["lifeSteal"][type.c_str()].num();
    if (actor()["lifeSteal"][(type + "Eval").c_str()].truthy()) {
        if (const Value* ast = obj_ast(actor(), "lifeSteal." + type + "Eval")) value += life_steal_eval(ast, target, "flat", "LIFE STEAL FLAT FORMULA ERROR");
    }
    value += current_class()["lifeSteal"][type.c_str()].num();
    if (current_class()["lifeSteal"][(type + "Eval").c_str()].truthy()) {
        if (const Value* ast = obj_ast(current_class(), "lifeSteal." + type + "Eval")) value += life_steal_eval(ast, target, "flat", "LIFE STEAL FLAT FORMULA ERROR");
    }
    return value;
}

bool GameActor::is_life_steal_state(const std::string& type) const {
    if (GameBattler::is_life_steal_state(type)) return true;
    GameActor* self = const_cast<GameActor*>(this);
    for (auto& e : self->equips()) {
        if (e.valid() && e.data()["lifeSteal"][type.c_str()].truthy()) return true;
    }
    if (actor()["lifeSteal"][type.c_str()].truthy()) return true;
    if (current_class()["lifeSteal"][type.c_str()].truthy()) return true;
    return false;
}

std::vector<int> GameActor::aura_state_ids() {
    if (game_temp().is_gathering_aura_data) return {};
    std::vector<int> arr = GameBattlerBase::aura_state_ids();
    GameUnit& friends = friends_unit();
    GameUnit& opponents = opponents_unit();
    game_temp().is_gathering_aura_data = true;
    auto add = [&](const std::vector<int>& v) { arr.insert(arr.end(), v.begin(), v.end()); };
    add(friends.aura_state_type_ids("party"));
    add(opponents.aura_state_type_ids("party"));
    if (check_aura_alive()) {
        add(friends.aura_state_type_ids("aliveParty"));
        add(opponents.aura_state_type_ids("aliveParty"));
    } else if (check_aura_dead()) {
        add(friends.aura_state_type_ids("deadParty"));
        add(opponents.aura_state_type_ids("deadParty"));
    }
    game_temp().is_gathering_aura_data = false;
    return arr;
}

double GameActor::steal_rate_bonus_actor(int index) const {
    double rate = actor()["stealRateBonus"][index].num() + current_class()["stealRateBonus"][index].num();
    for (auto& e : const_cast<GameActor*>(this)->equips()) {
        if (!e.valid()) continue;
        if (!e.data()["stealRateBonus"].is_arr()) continue;
        rate += e.data()["stealRateBonus"][index].num();
    }
    return rate;
}

void GameActor::on_battle_start() {
    GameBattler::on_battle_start();
    use_victory_face = false;
}

void GameActor::on_battle_end() {
    GameBattler::on_battle_end();
    set_last_command_symbol("");
    set_last_battle_skill(ItemRef());
    game_temp().expando.set(js::atom("_damagedPlayer"), js::Val());
}

bool GameActor::is_equipment_slot_locked(int slot) const { return contains_int(locked_equipment_slots, slot); }

void GameActor::lock_equipment_slots(const std::vector<int>& slots) {
    for (int s : slots) {
        if (!contains_int(locked_equipment_slots, s)) locked_equipment_slots.push_back(s);
    }
}

void GameActor::unlock_equipment_slots(const std::vector<int>& slots) {
    locked_equipment_slots.erase(
        std::remove_if(locked_equipment_slots.begin(), locked_equipment_slots.end(), [&](int s) { return contains_int(slots, s); }),
        locked_equipment_slots.end());
}

bool GameActor::is_battle_command_disabled(int index) const { return contains_int(disabled_battle_commands, index); }

void GameActor::disable_battle_commands(const std::vector<int>& ids) {
    for (int c : ids) {
        if (!contains_int(disabled_battle_commands, c)) disabled_battle_commands.push_back(c);
    }
}

void GameActor::enable_battle_commands(const std::vector<int>& ids) {
    disabled_battle_commands.erase(
        std::remove_if(disabled_battle_commands.begin(), disabled_battle_commands.end(), [&](int c) { return contains_int(ids, c); }),
        disabled_battle_commands.end());
}

void GameActor::lock_equip_skill(const std::vector<int>& ids) {
    for (int id : ids) {
        if (!contains_int(locked_skills, id)) locked_skills.push_back(id);
    }
}

void GameActor::unlock_equip_skill(const std::vector<int>& ids) {
    locked_skills.erase(std::remove_if(locked_skills.begin(), locked_skills.end(), [&](int id) { return contains_int(ids, id); }), locked_skills.end());
}

std::map<std::string, std::string> GameActor::status_state_particles_data() {
    std::map<std::string, std::string> out;
    auto st = states();
    if (st.empty()) return out;
    const Value& state = *st[0];
    static const char* layers[] = {"Behind", "Front", "Top"};
    for (const char* layer : layers) {
        std::string name = actor_meta(state, (std::string("Particle") + layer).c_str());
        if (!name.empty()) out[to_lower(layer)] = to_lower(trim(name));
    }
    return out;
}

double GameActor::anchor_x() {
    if (has_anchor_x) return anchor_x_cache;
    auto take = [&](const Value& o) {
        const Value* v = o.find("anchorX");
        if (v) {
            has_anchor_x = true;
            anchor_x_cache = v->num();
        }
        return v != nullptr;
    };
    for (const Value* s : states()) {
        if (take(*s)) return anchor_x_cache;
    }
    for (auto& e : equips()) {
        if (e.valid() && take(e.data())) return anchor_x_cache;
    }
    if (take(current_class())) return anchor_x_cache;
    if (take(actor())) return anchor_x_cache;
    has_anchor_x = true;
    anchor_x_cache = yanfly_param("BECAnchorX").num();
    return anchor_x_cache;
}

double GameActor::anchor_y() {
    if (has_anchor_y) return anchor_y_cache;
    auto take = [&](const Value& o) {
        const Value* v = o.find("anchorY");
        if (v) {
            has_anchor_y = true;
            anchor_y_cache = v->num();
        }
        return v != nullptr;
    };
    for (const Value* s : states()) {
        if (take(*s)) return anchor_y_cache;
    }
    for (auto& e : equips()) {
        if (e.valid() && take(e.data())) return anchor_y_cache;
    }
    if (take(current_class())) return anchor_y_cache;
    if (take(actor())) return anchor_y_cache;
    has_anchor_y = true;
    anchor_y_cache = yanfly_param("BECAnchorY").num();
    return anchor_y_cache;
}

}
