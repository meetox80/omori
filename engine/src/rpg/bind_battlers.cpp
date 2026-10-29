#include <cmath>

#include "action.h"
#include "battle.h"
#include "bind_util.h"
#include "data.h"
#include "enemy.h"
#include "game.h"
#include "interpreter.h"
#include "party.h"

namespace rpg {
namespace bind {

namespace {

using BB = GameBattlerBase;
using BT = GameBattler;

Val actions_js(const std::vector<std::shared_ptr<GameAction>>& v) {
    std::vector<Val> out;
    for (auto& a : v) out.push_back(held_js(a));
    return js::make_array(std::move(out));
}

std::vector<std::shared_ptr<GameAction>> actions_from(const Val& v) {
    std::vector<std::shared_ptr<GameAction>> out;
    if (!v.is_array()) return out;
    for (auto& x : v.o->items) {
        GameAction* a = js::host_cast<GameAction>(x);
        std::shared_ptr<GameAction> sp = a ? held_ptr(a) : nullptr;
        if (!sp && a) sp = a->clone();
        out.push_back(sp);
    }
    return out;
}

Val items_js(const std::vector<ItemRef>& v) {
    std::vector<Val> out;
    for (auto& r : v) out.push_back(item_js(r));
    return js::make_array(std::move(out));
}

Val states_js(const std::vector<const Value*>& v) {
    std::vector<Val> out;
    for (const Value* s : v) out.push_back(s ? data_js(*s) : Val::null());
    return js::make_array(std::move(out));
}

void bind_result() {
    auto k = js::klass_of<GameActionResult>(klass("Game_ActionResult"));
    k.bool_field("used", &GameActionResult::used);
    k.bool_field("missed", &GameActionResult::missed);
    k.bool_field("evaded", &GameActionResult::evaded);
    k.bool_field("physical", &GameActionResult::physical);
    k.bool_field("drain", &GameActionResult::drain);
    k.bool_field("critical", &GameActionResult::critical);
    k.bool_field("success", &GameActionResult::success);
    k.bool_field("hpAffected", &GameActionResult::hp_affected);
    k.num_field("hpDamage", &GameActionResult::hp_damage);
    k.num_field("mpDamage", &GameActionResult::mp_damage);
    k.num_field("tpDamage", &GameActionResult::tp_damage);
    k.bool_field("elementStrong", &GameActionResult::element_strong);
    k.bool_field("elementWeak", &GameActionResult::element_weak);
    auto vec_prop = [&](const char* name, std::vector<int> GameActionResult::*m) {
        k.prop(
            name, [m](GameActionResult& r) { return ints_of(r.*m); }, [m](GameActionResult& r, const Val& v) { r.*m = ints_from(v); });
    };
    vec_prop("addedStates", &GameActionResult::added_states);
    vec_prop("removedStates", &GameActionResult::removed_states);
    vec_prop("addedBuffs", &GameActionResult::added_buffs);
    vec_prop("addedDebuffs", &GameActionResult::added_debuffs);
    vec_prop("removedBuffs", &GameActionResult::removed_buffs);
    k.call("clear", [](GameActionResult& r, Args&) { r.clear(); });
    k.boolean("isStatusAffected", [](GameActionResult& r, Args&) { return r.is_status_affected(); });
    k.boolean("isHit", [](GameActionResult& r, Args&) { return r.is_hit(); });
    k.boolean("isStateAdded", [](GameActionResult& r, Args& a) { return r.is_state_added(js::arg_int(a, 0)); });
    k.call("pushAddedState", [](GameActionResult& r, Args& a) { r.push_added_state(js::arg_int(a, 0)); });
    k.boolean("isStateRemoved", [](GameActionResult& r, Args& a) { return r.is_state_removed(js::arg_int(a, 0)); });
    k.call("pushRemovedState", [](GameActionResult& r, Args& a) { r.push_removed_state(js::arg_int(a, 0)); });
    k.boolean("isBuffAdded", [](GameActionResult& r, Args& a) { return r.is_buff_added(js::arg_int(a, 0)); });
    k.call("pushAddedBuff", [](GameActionResult& r, Args& a) { r.push_added_buff(js::arg_int(a, 0)); });
    k.boolean("isDebuffAdded", [](GameActionResult& r, Args& a) { return r.is_debuff_added(js::arg_int(a, 0)); });
    k.call("pushAddedDebuff", [](GameActionResult& r, Args& a) { r.push_added_debuff(js::arg_int(a, 0)); });
    k.boolean("isBuffRemoved", [](GameActionResult& r, Args& a) { return r.is_buff_removed(js::arg_int(a, 0)); });
    k.call("pushRemovedBuff", [](GameActionResult& r, Args& a) { r.push_removed_buff(js::arg_int(a, 0)); });
}

void bind_action() {
    auto k = js::klass_of<GameAction>(klass("Game_Action"));
    k.get("_item", [](GameAction& a) {
        js::Val o = js::make_object();
        js::vm().put(o, js::atom("_dataClass"), Val::str(a.item_.data_class));
        js::vm().put(o, js::atom("_itemId"), Val::num(a.item_.item_id));
        std::string cls = a.item_.data_class;
        int id = a.item_.item_id;
        js::vm().put(o, js::atom("itemId"), native([id](Args&) { return Val::num(id); }));
        js::vm().put(o, js::atom("isSkill"), native([cls](Args&) { return Val::boolean(cls == "skill"); }));
        js::vm().put(o, js::atom("isItem"), native([cls](Args&) { return Val::boolean(cls == "item"); }));
        return o;
    });
    k.num_field("_targetIndex", &GameAction::target_index);
    k.bool_field("_forcing", &GameAction::forcing);
    k.bool_field("_isCounter", &GameAction::is_counter_);
    k.method("subject", [](GameAction& a, js::Vm&, Args&) { return obj_js(a.subject()); });
    k.call("setSubject", [](GameAction& a, Args& x) { a.set_subject(battler_of(js::arg(x, 0))); });
    k.method("friendsUnit", [](GameAction& a, js::Vm&, Args&) { return a.friends_unit().to_js(); });
    k.method("opponentsUnit", [](GameAction& a, js::Vm&, Args&) { return a.opponents_unit().to_js(); });
    k.call("clear", [](GameAction& a, Args&) { a.clear(); });
    k.call("setAttack", [](GameAction& a, Args&) { a.set_attack(); });
    k.call("setGuard", [](GameAction& a, Args&) { a.set_guard(); });
    k.call("setSkill", [](GameAction& a, Args& x) { a.set_skill(js::arg_int(x, 0)); });
    k.call("setItem", [](GameAction& a, Args& x) { a.set_item(js::arg_int(x, 0)); });
    k.call("setItemObject", [](GameAction& a, Args& x) { a.set_item_object(item_of(js::arg(x, 0))); });
    k.call("setTarget", [](GameAction& a, Args& x) { a.set_target(js::arg_int(x, 0)); });
    k.method("item", [](GameAction& a, js::Vm&, Args&) { return item_js(a.item()); });
    k.boolean("isSkill", [](GameAction& a, Args&) { return a.is_skill(); });
    k.boolean("isItem", [](GameAction& a, Args&) { return a.is_item(); });
    k.num("numRepeats", [](GameAction& a, Args&) { return (double)a.num_repeats(); });
    k.boolean("isForOpponent", [](GameAction& a, Args&) { return a.is_for_opponent(); });
    k.boolean("isForFriend", [](GameAction& a, Args&) { return a.is_for_friend(); });
    k.boolean("isForDeadFriend", [](GameAction& a, Args&) { return a.is_for_dead_friend(); });
    k.boolean("isForUser", [](GameAction& a, Args&) { return a.is_for_user(); });
    k.boolean("isForOne", [](GameAction& a, Args&) { return a.is_for_one(); });
    k.boolean("isForRandom", [](GameAction& a, Args&) { return a.is_for_random(); });
    k.boolean("isForAll", [](GameAction& a, Args&) { return a.is_for_all(); });
    k.boolean("needsSelection", [](GameAction& a, Args&) { return a.needs_selection(); });
    k.num("numTargets", [](GameAction& a, Args&) { return (double)a.num_targets(); });
    k.boolean("isHpEffect", [](GameAction& a, Args&) { return a.is_hp_effect(); });
    k.boolean("isMpEffect", [](GameAction& a, Args&) { return a.is_mp_effect(); });
    k.boolean("isDamage", [](GameAction& a, Args&) { return a.is_damage(); });
    k.boolean("isRecover", [](GameAction& a, Args&) { return a.is_recover(); });
    k.boolean("isDrain", [](GameAction& a, Args&) { return a.is_drain(); });
    k.boolean("isHpRecover", [](GameAction& a, Args&) { return a.is_hp_recover(); });
    k.boolean("isMpRecover", [](GameAction& a, Args&) { return a.is_mp_recover(); });
    k.boolean("isCertainHit", [](GameAction& a, Args&) { return a.is_certain_hit(); });
    k.boolean("isPhysical", [](GameAction& a, Args&) { return a.is_physical(); });
    k.boolean("isMagical", [](GameAction& a, Args&) { return a.is_magical(); });
    k.boolean("isAttack", [](GameAction& a, Args&) { return a.is_attack(); });
    k.boolean("isGuard", [](GameAction& a, Args&) { return a.is_guard(); });
    k.boolean("isMagicSkill", [](GameAction& a, Args&) { return a.is_magic_skill(); });
    k.call("decideRandomTarget", [](GameAction& a, Args&) { a.decide_random_target(); });
    k.call("setConfusion", [](GameAction& a, Args&) { a.set_confusion(); });
    k.call("prepare", [](GameAction& a, Args&) { a.prepare(); });
    k.boolean("isValid", [](GameAction& a, Args&) { return a.is_valid(); });
    k.num("speed", [](GameAction& a, Args&) { return a.speed(); });
    k.method("makeTargets", [](GameAction& a, js::Vm&, Args&) { return arr_of(a.make_targets()); });
    k.num("evaluate", [](GameAction& a, Args&) { return a.evaluate(); });
    k.boolean("testApply", [](GameAction& a, Args& x) {
        GameBattler* t = battler_of(js::arg(x, 0));
        return t && a.test_apply(t);
    });
    k.call("apply", [](GameAction& a, Args& x) {
        GameBattler* t = battler_of(js::arg(x, 0));
        if (t) a.apply(t);
    });
    k.num("makeDamageValue", [](GameAction& a, Args& x) {
        GameBattler* t = battler_of(js::arg(x, 0));
        return t ? a.make_damage_value(t, js::arg(x, 1).truthy()) : 0.0;
    });
    k.num("calcElementRate", [](GameAction& a, Args& x) {
        GameBattler* t = battler_of(js::arg(x, 0));
        return t ? a.calc_element_rate(t) : 1.0;
    });
    k.call("executeDamage", [](GameAction& a, Args& x) {
        GameBattler* t = battler_of(js::arg(x, 0));
        if (t) a.execute_damage(t, js::arg_num(x, 1));
    });
    k.call("applyGlobal", [](GameAction& a, Args&) { a.apply_global(); });
    k.call("setCounter", [](GameAction& a, Args&) { a.set_counter(); });
    k.boolean("isCounter", [](GameAction& a, Args&) { return a.is_counter(); });
    k.boolean("isTauntable", [](GameAction& a, Args&) { return a.is_tauntable(); });
}

void bind_battler_base() {
    auto k = js::klass_of<BB>(klass("Game_BattlerBase"));
    k.get("hp", [](BB& b) { return Val::num(b.hp()); });
    k.get("mp", [](BB& b) { return Val::num(b.mp()); });
    k.get("tp", [](BB& b) { return Val::num(b.tp()); });
    k.get("mhp", [](BB& b) { return Val::num(b.mhp()); });
    k.get("mmp", [](BB& b) { return Val::num(b.mmp()); });
    k.get("atk", [](BB& b) { return Val::num(b.atk()); });
    k.get("def", [](BB& b) { return Val::num(b.def()); });
    k.get("mat", [](BB& b) { return Val::num(b.mat()); });
    k.get("mdf", [](BB& b) { return Val::num(b.mdf()); });
    k.get("agi", [](BB& b) { return Val::num(b.agi()); });
    k.get("luk", [](BB& b) { return Val::num(b.luk()); });
    k.get("hit", [](BB& b) { return Val::num(b.hit()); });
    k.get("eva", [](BB& b) { return Val::num(b.eva()); });
    k.get("cri", [](BB& b) { return Val::num(b.cri()); });
    k.get("cev", [](BB& b) { return Val::num(b.cev()); });
    k.get("mev", [](BB& b) { return Val::num(b.mev()); });
    k.get("mrf", [](BB& b) { return Val::num(b.mrf()); });
    k.get("cnt", [](BB& b) { return Val::num(b.cnt()); });
    k.get("hrg", [](BB& b) { return Val::num(b.hrg()); });
    k.get("mrg", [](BB& b) { return Val::num(b.mrg()); });
    k.get("trg", [](BB& b) { return Val::num(b.trg()); });
    k.get("tgr", [](BB& b) { return Val::num(b.tgr()); });
    k.get("grd", [](BB& b) { return Val::num(b.grd()); });
    k.get("rec", [](BB& b) { return Val::num(b.rec()); });
    k.get("pha", [](BB& b) { return Val::num(b.pha()); });
    k.get("mcr", [](BB& b) { return Val::num(b.mcr()); });
    k.get("tcr", [](BB& b) { return Val::num(b.tcr()); });
    k.get("pdr", [](BB& b) { return Val::num(b.pdr()); });
    k.get("mdr", [](BB& b) { return Val::num(b.mdr()); });
    k.get("fdr", [](BB& b) { return Val::num(b.fdr()); });
    k.get("exr", [](BB& b) { return Val::num(b.exr()); });
    k.num_field("_hp", &BB::hp_);
    k.num_field("_mp", &BB::mp_);
    k.num_field("_tp", &BB::tp_);
    k.bool_field("_hidden", &BB::hidden);
    k.prop(
        "_buffs",
        [](BB& b) {
            std::vector<Val> v;
            for (double x : b.buffs_) v.push_back(Val::num(x));
            return js::make_array(std::move(v));
        },
        [](BB& b, const Val& v) {
            for (int i = 0; i < 8; i++) b.buffs_[i] = v.is_array() && (size_t)i < v.o->items.size() ? v.o->items[(size_t)i].to_num() : 0;
        });
    k.prop(
        "_states", [](BB& b) { return ints_of(b.states_); }, [](BB& b, const Val& v) { b.states_ = ints_from(v); });
    k.prop(
        "_paramPlus",
        [](BB& b) {
            std::vector<Val> v;
            for (double x : b.param_plus_) v.push_back(Val::num(x));
            return js::make_array(std::move(v));
        },
        [](BB& b, const Val& v) {
            for (int i = 0; i < 8; i++) b.param_plus_[i] = v.is_array() && (size_t)i < v.o->items.size() ? v.o->items[(size_t)i].to_num() : 0;
        });
    k.boolean("isActor", [](BB& b, Args&) { return b.is_actor(); });
    k.boolean("isEnemy", [](BB& b, Args&) { return b.is_enemy(); });
    k.method("friendsUnit", [](BB& b, js::Vm&, Args&) { return b.friends_unit().to_js(); });
    k.method("opponentsUnit", [](BB& b, js::Vm&, Args&) { return b.opponents_unit().to_js(); });
    k.num("index", [](BB& b, Args&) { return (double)b.index(); });
    k.call("clearParamPlus", [](BB& b, Args&) { b.clear_param_plus(); });
    k.call("clearStates", [](BB& b, Args&) { b.clear_states(); });
    k.call("eraseState", [](BB& b, Args& a) { b.erase_state(js::arg_int(a, 0)); });
    k.boolean("isStateAffected", [](BB& b, Args& a) { return b.is_state_affected(js::arg_int(a, 0)); });
    k.boolean("isDeathStateAffected", [](BB& b, Args&) { return b.is_death_state_affected(); });
    k.num("deathStateId", [](BB& b, Args&) { return (double)b.death_state_id(); });
    k.call("resetStateCounts", [](BB& b, Args& a) { b.reset_state_counts(js::arg_int(a, 0)); });
    k.boolean("isStateExpired", [](BB& b, Args& a) { return b.is_state_expired(js::arg_int(a, 0)); });
    k.call("updateStateTurns", [](BB& b, Args&) { b.update_state_turns(); });
    k.call("clearBuffs", [](BB& b, Args&) { b.clear_buffs(); });
    k.call("eraseBuff", [](BB& b, Args& a) { b.erase_buff(js::arg_int(a, 0)); });
    k.num("buffLength", [](BB& b, Args&) { return (double)b.buff_length(); });
    k.num("buff", [](BB& b, Args& a) { return b.buff(js::arg_int(a, 0)); });
    k.boolean("isBuffAffected", [](BB& b, Args& a) { return b.is_buff_affected(js::arg_int(a, 0)); });
    k.boolean("isDebuffAffected", [](BB& b, Args& a) { return b.is_debuff_affected(js::arg_int(a, 0)); });
    k.boolean("isBuffOrDebuffAffected", [](BB& b, Args& a) { return b.is_buff_or_debuff_affected(js::arg_int(a, 0)); });
    k.boolean("isMaxBuffAffected", [](BB& b, Args& a) { return b.is_max_buff_affected(js::arg_int(a, 0)); });
    k.boolean("isMaxDebuffAffected", [](BB& b, Args& a) { return b.is_max_debuff_affected(js::arg_int(a, 0)); });
    k.call("increaseBuff", [](BB& b, Args& a) { b.increase_buff(js::arg_int(a, 0)); });
    k.call("decreaseBuff", [](BB& b, Args& a) { b.decrease_buff(js::arg_int(a, 0)); });
    k.call("overwriteBuffTurns", [](BB& b, Args& a) { b.overwrite_buff_turns(js::arg_int(a, 0), js::arg_num(a, 1)); });
    k.boolean("isBuffExpired", [](BB& b, Args& a) { return b.is_buff_expired(js::arg_int(a, 0)); });
    k.call("updateBuffTurns", [](BB& b, Args&) { b.update_buff_turns(); });
    k.call("die", [](BB& b, Args&) { b.die(); });
    k.call("revive", [](BB& b, Args&) { b.revive(); });
    k.method("states", [](BB& b, js::Vm&, Args&) { return states_js(b.states()); });
    k.method("stateIcons", [](BB& b, js::Vm&, Args&) { return ints_of(b.state_icons()); });
    k.method("buffIcons", [](BB& b, js::Vm&, Args&) { return ints_of(b.buff_icons()); });
    k.method("allIcons", [](BB& b, js::Vm&, Args&) { return ints_of(b.all_icons()); });
    k.method("traitObjects", [](BB& b, js::Vm&, Args&) { return states_js(b.trait_objects()); });
    k.method("allTraits", [](BB& b, js::Vm&, Args&) { return states_js(b.all_traits()); });
    k.num("traitsPi", [](BB& b, Args& a) { return b.traits_pi(js::arg_int(a, 0), js::arg_int(a, 1)); });
    k.num("traitsSum", [](BB& b, Args& a) { return b.traits_sum(js::arg_int(a, 0), js::arg_int(a, 1)); });
    k.num("traitsSumAll", [](BB& b, Args& a) { return b.traits_sum_all(js::arg_int(a, 0)); });
    k.method("traitsSet", [](BB& b, js::Vm&, Args& a) { return ints_of(b.traits_set(js::arg_int(a, 0))); });
    k.num("paramBase", [](BB& b, Args& a) { return b.param_base(js::arg_int(a, 0)); });
    k.num("paramPlus", [](BB& b, Args& a) { return b.param_plus(js::arg_int(a, 0)); });
    k.num("paramMin", [](BB& b, Args& a) { return b.param_min(js::arg_int(a, 0)); });
    k.num("paramMax", [](BB& b, Args& a) { return b.param_max(js::arg_int(a, 0)); });
    k.num("paramRate", [](BB& b, Args& a) { return b.param_rate(js::arg_int(a, 0)); });
    k.num("paramBuffRate", [](BB& b, Args& a) { return b.param_buff_rate(js::arg_int(a, 0)); });
    k.num("param", [](BB& b, Args& a) { return b.param(js::arg_int(a, 0)); });
    k.num("xparam", [](BB& b, Args& a) { return b.xparam(js::arg_int(a, 0)); });
    k.num("sparam", [](BB& b, Args& a) { return b.sparam(js::arg_int(a, 0)); });
    k.num("elementRate", [](BB& b, Args& a) { return b.element_rate(js::arg_int(a, 0)); });
    k.num("debuffRate", [](BB& b, Args& a) { return b.debuff_rate(js::arg_int(a, 0)); });
    k.num("stateRate", [](BB& b, Args& a) { return b.state_rate(js::arg_int(a, 0)); });
    k.method("stateResistSet", [](BB& b, js::Vm&, Args&) { return ints_of(b.state_resist_set()); });
    k.boolean("isStateResist", [](BB& b, Args& a) { return b.is_state_resist(js::arg_int(a, 0)); });
    k.method("attackElements", [](BB& b, js::Vm&, Args&) { return ints_of(b.attack_elements()); });
    k.method("attackStates", [](BB& b, js::Vm&, Args&) { return ints_of(b.attack_states()); });
    k.num("attackStatesRate", [](BB& b, Args& a) { return b.attack_states_rate(js::arg_int(a, 0)); });
    k.num("attackSpeed", [](BB& b, Args&) { return b.attack_speed(); });
    k.num("attackTimesAdd", [](BB& b, Args&) { return b.attack_times_add(); });
    k.method("addedSkillTypes", [](BB& b, js::Vm&, Args&) { return ints_of(b.added_skill_types()); });
    k.boolean("isSkillTypeSealed", [](BB& b, Args& a) { return b.is_skill_type_sealed(js::arg_int(a, 0)); });
    k.method("addedSkills", [](BB& b, js::Vm&, Args&) { return ints_of(b.added_skills()); });
    k.boolean("isSkillSealed", [](BB& b, Args& a) { return b.is_skill_sealed(js::arg_int(a, 0)); });
    k.boolean("isEquipWtypeOk", [](BB& b, Args& a) { return b.is_equip_wtype_ok(js::arg_int(a, 0)); });
    k.boolean("isEquipAtypeOk", [](BB& b, Args& a) { return b.is_equip_atype_ok(js::arg_int(a, 0)); });
    k.boolean("isEquipTypeLocked", [](BB& b, Args& a) { return b.is_equip_type_locked(js::arg_int(a, 0)); });
    k.boolean("isEquipTypeSealed", [](BB& b, Args& a) { return b.is_equip_type_sealed(js::arg_int(a, 0)); });
    k.num("slotType", [](BB& b, Args&) { return (double)b.slot_type(); });
    k.boolean("isDualWield", [](BB& b, Args&) { return b.is_dual_wield(); });
    k.boolean("specialFlag", [](BB& b, Args& a) { return b.special_flag(js::arg_int(a, 0)); });
    k.num("collapseType", [](BB& b, Args&) { return (double)b.collapse_type(); });
    k.boolean("partyAbility", [](BB& b, Args& a) { return b.party_ability(js::arg_int(a, 0)); });
    k.boolean("isAutoBattle", [](BB& b, Args&) { return b.is_auto_battle(); });
    k.boolean("isGuard", [](BB& b, Args&) { return b.is_guard(); });
    k.boolean("isSubstitute", [](BB& b, Args&) { return b.is_substitute(); });
    k.boolean("isPreserveTp", [](BB& b, Args&) { return b.is_preserve_tp(); });
    k.call("addParam", [](BB& b, Args& a) { b.add_param(js::arg_int(a, 0), js::arg_num(a, 1)); });
    k.call("setHp", [](BB& b, Args& a) { b.set_hp(js::arg_num(a, 0)); });
    k.call("setMp", [](BB& b, Args& a) { b.set_mp(js::arg_num(a, 0)); });
    k.call("setTp", [](BB& b, Args& a) { b.set_tp(js::arg_num(a, 0)); });
    k.num("maxTp", [](BB& b, Args&) { return b.max_tp(); });
    k.call("refresh", [](BB& b, Args&) { b.refresh(); });
    k.call("recoverAll", [](BB& b, Args&) { b.recover_all(); });
    k.num("hpRate", [](BB& b, Args&) { return b.hp_rate(); });
    k.num("mpRate", [](BB& b, Args&) { return b.mp_rate(); });
    k.num("tpRate", [](BB& b, Args&) { return b.tp_rate(); });
    k.call("hide", [](BB& b, Args&) { b.hide(); });
    k.call("appear", [](BB& b, Args&) { b.appear(); });
    k.boolean("isHidden", [](BB& b, Args&) { return b.is_hidden(); });
    k.boolean("isAppeared", [](BB& b, Args&) { return b.is_appeared(); });
    k.boolean("isDead", [](BB& b, Args&) { return b.is_dead(); });
    k.boolean("isAlive", [](BB& b, Args&) { return b.is_alive(); });
    k.boolean("isDying", [](BB& b, Args&) { return b.is_dying(); });
    k.boolean("isRestricted", [](BB& b, Args&) { return b.is_restricted(); });
    k.boolean("canInput", [](BB& b, Args&) { return b.can_input(); });
    k.boolean("canMove", [](BB& b, Args&) { return b.can_move(); });
    k.boolean("isConfused", [](BB& b, Args&) { return b.is_confused(); });
    k.num("confusionLevel", [](BB& b, Args&) { return (double)b.confusion_level(); });
    k.num("restriction", [](BB& b, Args&) { return (double)b.restriction(); });
    k.call("addNewState", [](BB& b, Args& a) { b.add_new_state(js::arg_int(a, 0)); });
    k.str("mostImportantStateText", [](BB& b, Args&) { return b.most_important_state_text(); });
    k.num("stateMotionIndex", [](BB& b, Args&) { return (double)b.state_motion_index(); });
    k.num("stateOverlayIndex", [](BB& b, Args&) { return (double)b.state_overlay_index(); });
    k.num("skillMpCost", [](BB& b, Args& a) { return b.skill_mp_cost(item_of(js::arg(a, 0))); });
    k.num("skillTpCost", [](BB& b, Args& a) { return b.skill_tp_cost(item_of(js::arg(a, 0))); });
    k.num("skillHpCost", [](BB& b, Args& a) { return b.skill_hp_cost(item_of(js::arg(a, 0))); });
    k.boolean("canPaySkillCost", [](BB& b, Args& a) { return b.can_pay_skill_cost(item_of(js::arg(a, 0))); });
    k.call("paySkillCost", [](BB& b, Args& a) { b.pay_skill_cost(item_of(js::arg(a, 0))); });
    k.boolean("meetsUsableItemConditions", [](BB& b, Args& a) { return b.meets_usable_item_conditions(item_of(js::arg(a, 0))); });
    k.boolean("meetsSkillConditions", [](BB& b, Args& a) { return b.meets_skill_conditions(item_of(js::arg(a, 0))); });
    k.boolean("meetsItemConditions", [](BB& b, Args& a) { return b.meets_item_conditions(item_of(js::arg(a, 0))); });
    k.boolean("canUse", [](BB& b, Args& a) { return b.can_use(item_of(js::arg(a, 0))); });
    k.boolean("canEquip", [](BB& b, Args& a) { return b.can_equip(item_of(js::arg(a, 0))); });
    k.num("attackSkillId", [](BB& b, Args&) { return (double)b.attack_skill_id(); });
    k.num("guardSkillId", [](BB& b, Args&) { return (double)b.guard_skill_id(); });
    k.boolean("canAttack", [](BB& b, Args&) { return b.can_attack(); });
    k.boolean("canGuard", [](BB& b, Args&) { return b.can_guard(); });
    k.call("requestStatusRefresh", [](BB& b, Args&) { b.request_status_refresh(); });
    k.boolean("isImmortal", [](BB& b, Args&) { return b.is_immortal(); });
    k.num("stateTurns", [](BB& b, Args& a) { return b.state_turns_of(js::arg_int(a, 0)); });
    k.call("setStateTurns", [](BB& b, Args& a) { b.set_state_turns(js::arg_int(a, 0), js::arg_num(a, 1)); });
    k.num("buffTurns", [](BB& b, Args& a) { return b.buff_turns_of(js::arg_int(a, 0)); });
    k.call("setBuffTurns", [](BB& b, Args& a) { b.set_buff_turns(js::arg_int(a, 0), js::arg_num(a, 1)); });
    k.num("maxBuffLimit", [](BB& b, Args& a) { return b.max_buff_limit(js::arg_int(a, 0)); });
    k.num("maxDebuffLimit", [](BB& b, Args& a) { return b.max_debuff_limit(js::arg_int(a, 0)); });
    k.call("initStateCounter", [](BB& b, Args&) { b.init_state_counter(); });
    k.call("setStateCounter", [](BB& b, Args& a) { b.set_state_counter(js::arg_int(a, 0), js::arg(a, 1)); });
    k.call("addStateCounter", [](BB& b, Args& a) { b.add_state_counter(js::arg_int(a, 0), js::arg_num(a, 1)); });
    k.call("removeStateCounter", [](BB& b, Args& a) { b.remove_state_counter(js::arg_int(a, 0)); });
    k.method("getStateCounter", [](BB& b, js::Vm&, Args& a) { return b.get_state_counter(js::arg_int(a, 0)); });
    k.method("passiveStates", [](BB& b, js::Vm&, Args&) { return states_js(b.passive_states()); });
    k.boolean("isPassiveStateAffected", [](BB& b, Args& a) { return b.is_passive_state_affected(js::arg_int(a, 0)); });
    k.num("totalBuffs", [](BB& b, Args&) { return (double)b.total_buffs(); });
    k.num("totalDebuffs", [](BB& b, Args&) { return (double)b.total_debuffs(); });
    k.call("clearHorrorEffects", [](BB& b, Args&) { b.clear_horror_effects(); });
    k.call("createHorrorEffect", [](BB& b, Args& a) { b.create_horror_effect(js::arg(a, 0).to_str()); });
    k.call("removeHorrorEffect", [](BB& b, Args& a) { b.remove_horror_effect(js::arg(a, 0).to_str()); });
    k.call("setHorrorEffectToValue", [](BB& b, Args& a) { b.set_horror_effect_to_value(js::arg(a, 0).to_str(), js::arg(a, 1).to_str(), js::arg(a, 2)); });
    k.boolean("tauntPhysical", [](BB& b, Args&) { return b.taunt_physical(); });
    k.boolean("tauntMagical", [](BB& b, Args&) { return b.taunt_magical(); });
    k.boolean("tauntCertain", [](BB& b, Args&) { return b.taunt_certain(); });
}

void bind_battler() {
    auto k = js::klass_of<BT>(klass("Game_Battler", "Game_BattlerBase"));
    k.prop(
        "_actions", [](BT& b) { return actions_js(b.actions); }, [](BT& b, const Val& v) { b.actions = actions_from(v); });
    k.num_field("_speed", &BT::speed_);
    k.str_field("_actionState", &BT::action_state);
    k.num_field("_lastTargetIndex", &BT::last_target_index);
    k.num_field("_selfTurnCount", &BT::self_turn_count);
    k.num("turnCount", [](BT& b, Args&) { return b.turn_count(); });
    k.num_field("_counters", &BT::counters);
    k.val_field("_noEffectMessage", &BT::no_effect_message);
    k.val_field("_noStateMessage", &BT::no_state_message);
    k.get("_result", [](BT& b) { return b.result().to_js(); });
    k.method("result", [](BT& b, js::Vm&, Args&) { return b.result().to_js(); });
    k.call("clearResult", [](BT& b, Args&) { b.clear_result(); });
    k.call("addState", [](BT& b, Args& a) { b.add_state(js::arg_int(a, 0)); });
    k.boolean("isStateAddable", [](BT& b, Args& a) { return b.is_state_addable(js::arg_int(a, 0)); });
    k.boolean("isStateRestrict", [](BT& b, Args& a) { return b.is_state_restrict(js::arg_int(a, 0)); });
    k.call("removeState", [](BT& b, Args& a) { b.remove_state(js::arg_int(a, 0)); });
    k.call("escape", [](BT& b, Args&) { b.escape(); });
    k.call("addBuff", [](BT& b, Args& a) { b.add_buff(js::arg_int(a, 0), js::arg_num(a, 1)); });
    k.call("addDebuff", [](BT& b, Args& a) { b.add_debuff(js::arg_int(a, 0), js::arg_num(a, 1)); });
    k.call("removeBuff", [](BT& b, Args& a) { b.remove_buff(js::arg_int(a, 0)); });
    k.call("removeBattleStates", [](BT& b, Args&) { b.remove_battle_states(); });
    k.call("removeAllBuffs", [](BT& b, Args&) { b.remove_all_buffs(); });
    k.call("removeStatesAuto", [](BT& b, Args& a) { b.remove_states_auto(js::arg_int(a, 0)); });
    k.call("removeBuffsAuto", [](BT& b, Args&) { b.remove_buffs_auto(); });
    k.call("removeStatesByDamage", [](BT& b, Args&) { b.remove_states_by_damage(); });
    k.num("makeActionTimes", [](BT& b, Args&) { return (double)b.make_action_times(); });
    k.call("makeActions", [](BT& b, Args&) { b.make_actions(); });
    k.num("speed", [](BT& b, Args&) { return b.speed(); });
    k.call("makeSpeed", [](BT& b, Args&) { b.make_speed(); });
    k.method("currentAction", [](BT& b, js::Vm&, Args&) {
        auto a = b.current_action();
        return a ? held_js(a) : Val();
    });
    k.call("removeCurrentAction", [](BT& b, Args&) { b.remove_current_action(); });
    k.call("setLastTarget", [](BT& b, Args& a) { b.set_last_target(battler_of(js::arg(a, 0))); });
    k.method("action", [](BT& b, js::Vm&, Args& a) {
        auto x = b.action(js::arg_int(a, 0));
        return x ? held_js(x) : Val();
    });
    k.call("setAction", [](BT& b, Args& a) {
        GameAction* x = js::host_cast<GameAction>(js::arg(a, 1));
        std::shared_ptr<GameAction> sp = x ? held_ptr(x) : nullptr;
        if (!sp && x) sp = x->clone();
        b.set_action(js::arg_int(a, 0), sp);
    });
    k.num("numActions", [](BT& b, Args&) { return (double)b.num_actions(); });
    k.call("clearActions", [](BT& b, Args&) { b.clear_actions(); });
    k.call("forceAction", [](BT& b, Args& a) { b.force_action(js::arg_int(a, 0), js::arg_int(a, 1)); });
    k.call("useItem", [](BT& b, Args& a) { b.use_item(item_of(js::arg(a, 0))); });
    k.call("consumeItem", [](BT& b, Args& a) { b.consume_item(item_of(js::arg(a, 0))); });
    k.call("gainHp", [](BT& b, Args& a) { b.gain_hp(js::arg_num(a, 0)); });
    k.call("gainMp", [](BT& b, Args& a) { b.gain_mp(js::arg_num(a, 0)); });
    k.call("gainTp", [](BT& b, Args& a) { b.gain_tp(js::arg_num(a, 0)); });
    k.call("gainSilentTp", [](BT& b, Args& a) { b.gain_silent_tp(js::arg_num(a, 0)); });
    k.call("initTp", [](BT& b, Args&) { b.init_tp(); });
    k.call("clearTp", [](BT& b, Args&) { b.clear_tp(); });
    k.call("chargeTpByDamage", [](BT& b, Args& a) { b.charge_tp_by_damage(js::arg_num(a, 0)); });
    k.call("regenerateHp", [](BT& b, Args&) { b.regenerate_hp(); });
    k.num("maxSlipDamage", [](BT& b, Args&) { return b.max_slip_damage(); });
    k.call("regenerateMp", [](BT& b, Args&) { b.regenerate_mp(); });
    k.call("regenerateTp", [](BT& b, Args&) { b.regenerate_tp(); });
    k.call("regenerateAll", [](BT& b, Args&) { b.regenerate_all(); });
    k.call("onBattleStart", [](BT& b, Args&) { b.on_battle_start(); });
    k.call("onAllActionsEnd", [](BT& b, Args&) { b.on_all_actions_end(); });
    k.call("onTurnEnd", [](BT& b, Args&) { b.on_turn_end(); });
    k.call("onBattleEnd", [](BT& b, Args&) { b.on_battle_end(); });
    k.call("onDamage", [](BT& b, Args& a) { b.on_damage(js::arg_num(a, 0)); });
    k.call("setActionState", [](BT& b, Args& a) { b.set_action_state(js::arg(a, 0).to_str()); });
    k.boolean("isUndecided", [](BT& b, Args&) { return b.is_undecided(); });
    k.boolean("isInputting", [](BT& b, Args&) { return b.is_inputting(); });
    k.boolean("isWaiting", [](BT& b, Args&) { return b.is_waiting(); });
    k.boolean("isActing", [](BT& b, Args&) { return b.is_acting(); });
    k.boolean("isChanting", [](BT& b, Args&) { return b.is_chanting(); });
    k.boolean("isGuardWaiting", [](BT& b, Args&) { return b.is_guard_waiting(); });
    k.call("performActionStart", [](BT& b, Args& a) {
        GameAction* x = js::host_cast<GameAction>(js::arg(a, 0));
        if (x) b.perform_action_start(x);
    });
    k.call("performAction", [](BT& b, Args& a) {
        GameAction* x = js::host_cast<GameAction>(js::arg(a, 0));
        if (x) b.perform_action(x);
    });
    k.call("performActionEnd", [](BT& b, Args&) { b.perform_action_end(); });
    k.call("performDamage", [](BT& b, Args&) { b.perform_damage(); });
    k.call("performMiss", [](BT& b, Args&) { b.perform_miss(); });
    k.call("performRecovery", [](BT& b, Args&) { b.perform_recovery(); });
    k.call("performEvasion", [](BT& b, Args&) { b.perform_evasion(); });
    k.call("performMagicEvasion", [](BT& b, Args&) { b.perform_magic_evasion(); });
    k.call("performCounter", [](BT& b, Args&) { b.perform_counter(); });
    k.call("performReflection", [](BT& b, Args&) { b.perform_reflection(); });
    k.call("performSubstitute", [](BT& b, Args& a) { b.perform_substitute(battler_of(js::arg(a, 0))); });
    k.call("performCollapse", [](BT& b, Args&) { b.perform_collapse(); });
    k.call("performAttack", [](BT& b, Args&) { b.perform_attack(); });
    k.call("requestMotion", [](BT& b, Args& a) { b.request_motion(js::arg(a, 0).to_str()); });
    k.call("requestMotionRefresh", [](BT& b, Args&) { b.request_motion_refresh(); });
    k.call("requestEffect", [](BT& b, Args& a) { b.request_effect(js::arg(a, 0).to_str()); });
    k.call("forceMotion", [](BT& b, Args& a) { b.force_motion(js::arg(a, 0).to_str()); });
    k.call("startAnimation", [](BT& b, Args& a) { b.start_animation(js::arg_int(a, 0), js::arg(a, 1).truthy(), js::arg_num(a, 2)); });
    k.call("startDamagePopup", [](BT& b, Args&) { b.start_damage_popup(); });
    k.call("startWeaponAnimation", [](BT& b, Args& a) { b.start_weapon_animation(js::arg_int(a, 0)); });
    k.call("select", [](BT& b, Args&) { b.select(); });
    k.call("deselect", [](BT& b, Args&) { b.deselect(); });
    k.boolean("isSelected", [](BT& b, Args&) { return b.is_selected(); });
    k.call("addImmortal", [](BT& b, Args&) { b.add_immortal(); });
    k.call("removeImmortal", [](BT& b, Args&) { b.remove_immortal(); });
    k.call("removeStateCategoryAll", [](BT& b, Args& a) { b.remove_state_category_all(js::arg(a, 0).to_str()); });
    k.call("removeStateCategory", [](BT& b, Args& a) { b.remove_state_category(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
    k.boolean("isStateCategoryAffected", [](BT& b, Args& a) { return b.is_state_category_affected(js::arg(a, 0).to_str()); });
    k.num("getStateCategoryAffectedCount", [](BT& b, Args& a) { return (double)b.get_state_category_affected_count(js::arg(a, 0).to_str()); });
    k.boolean("isEmotionAffected", [](BT& b, Args& a) { return b.is_emotion_affected(js::arg(a, 0).to_str()); });
    k.boolean("isAnyEmotionAffected", [](BT& b, Args&) { return b.is_any_emotion_affected(); });
    k.boolean("isEmotionAddable", [](BT& b, Args& a) { return b.is_emotion_addable(js::arg(a, 0).to_str()); });
    k.boolean("_isEmotionalState", [](BT& b, Args& a) { return b.is_emotional_state(js::arg_int(a, 0)); });
    k.boolean("_isStateState", [](BT& b, Args& a) { return b.is_state_state(js::arg_int(a, 0)); });
    k.str("getStateEmotion", [](BT& b, Args&) { return b.get_state_emotion(); });
    k.boolean("hasState", [](BT& b, Args& a) { return b.has_state(js::arg_int(a, 0)); });
    k.boolean("notState", [](BT& b, Args& a) { return b.not_state(js::arg_int(a, 0)); });
    k.boolean("canCounter", [](BT& b, Args&) { return b.can_counter(); });
    k.call("payCounter", [](BT& b, Args& a) { b.pay_counter(js::arg_num(a, 0)); });
    k.call("spriteOpacity", [](BT& b, Args& a) { b.sprite_opacity(js::arg_num(a, 0), js::arg_num(a, 1)); });
    k.num("spritePosX", [](BT& b, Args&) { return b.sprite_pos_x(); });
    k.num("spritePosY", [](BT& b, Args&) { return b.sprite_pos_y(); });
    k.num("spriteWidth", [](BT& b, Args&) { return b.sprite_width(); });
    k.num("spriteHeight", [](BT& b, Args&) { return b.sprite_height(); });
    k.method("battler", [](BT& b, js::Vm&, Args&) { return b.sprite_link ? b.sprite_link->link_js() : Val(); });
    k.call("enableActiveChain", [](BT& b, Args&) { b.enable_active_chain(); });
    k.call("disableActiveChain", [](BT& b, Args&) { b.disable_active_chain(); });
    k.call("lifeSteal", [](BT& b, Args& a) {
        b.life_steal(js::arg_num(a, 0), js::arg(a, 1).to_str(), battler_of(js::arg(a, 2)), js::arg_num(a, 3), js::arg_num(a, 4), js::arg(a, 5).truthy());
    });
    k.str("name", [](BT& b, Args&) { return b.name(); });
}

void bind_actor() {
    auto k = js::klass_of<GameActor>(klass("Game_Actor", "Game_Battler"));
    k.num_field("_actorId", &GameActor::actor_id_);
    k.str_field("_name", &GameActor::name_);
    k.str_field("_nickname", &GameActor::nickname_);
    k.str_field("_profile", &GameActor::profile_);
    k.num_field("_classId", &GameActor::class_id);
    k.num_field("_level", &GameActor::level);
    k.str_field("_characterName", &GameActor::character_name_);
    k.num_field("_characterIndex", &GameActor::character_index_);
    k.str_field("_faceName", &GameActor::face_name_);
    k.num_field("_faceIndex", &GameActor::face_index_);
    k.str_field("_battlerName", &GameActor::battler_name_);
    k.num_field("_actionInputIndex", &GameActor::action_input_index);
    k.str_field("_lastCommandSymbol", &GameActor::last_command_symbol);
    k.bool_field("_useVictoryFace", &GameActor::use_victory_face);
    k.bool_field("_playedCollapseSound", &GameActor::played_collapse_sound);
    k.num_field("_farawayActorId", &GameActor::faraway_actor_id);
    k.str_field("_battleFaceName", &GameActor::battle_face_name);
    k.val_field("_battleCommandsList", &GameActor::battle_commands_list);
    k.get("level", [](GameActor& a) { return Val::num(a.level); });
    k.prop(
        "_skills", [](GameActor& a) { return ints_of(a.skills_); }, [](GameActor& a, const Val& v) { a.skills_ = ints_from(v); });
    k.prop(
        "_equippedSkills", [](GameActor& a) { return ints_of(a.equipped_skills); }, [](GameActor& a, const Val& v) { a.equipped_skills = ints_from(v); });
    k.num("actorId", [](GameActor& a, Args&) { return (double)a.actor_id(); });
    k.method("actor", [](GameActor& a, js::Vm&, Args&) { return data_js(a.actor()); });
    k.call("setName", [](GameActor& a, Args& x) { a.set_name(js::arg(x, 0).to_str()); });
    k.str("nickname", [](GameActor& a, Args&) { return a.nickname(); });
    k.call("setNickname", [](GameActor& a, Args& x) { a.nickname_ = js::arg(x, 0).to_str(); });
    k.str("profile", [](GameActor& a, Args&) { return a.profile(); });
    k.call("setProfile", [](GameActor& a, Args& x) { a.profile_ = js::arg(x, 0).to_str(); });
    k.str("characterName", [](GameActor& a, Args&) { return a.character_name(); });
    k.num("characterIndex", [](GameActor& a, Args&) { return (double)a.character_index(); });
    k.str("faceName", [](GameActor& a, Args&) { return a.face_name(); });
    k.num("faceIndex", [](GameActor& a, Args&) { return (double)a.face_index(); });
    k.str("battlerName", [](GameActor& a, Args&) { return a.battler_name(); });
    k.num("expForLevel", [](GameActor& a, Args& x) { return a.exp_for_level(js::arg_int(x, 0)); });
    k.call("initExp", [](GameActor& a, Args&) { a.init_exp(); });
    k.num("currentExp", [](GameActor& a, Args&) { return a.current_exp(); });
    k.num("currentLevelExp", [](GameActor& a, Args&) { return a.current_level_exp(); });
    k.num("nextLevelExp", [](GameActor& a, Args&) { return a.next_level_exp(); });
    k.num("nextRequiredExp", [](GameActor& a, Args&) { return a.next_required_exp(); });
    k.num("maxLevel", [](GameActor& a, Args&) { return (double)a.max_level(); });
    k.boolean("isMaxLevel", [](GameActor& a, Args&) { return a.is_max_level(); });
    k.method("equipSlots", [](GameActor& a, js::Vm&, Args&) { return ints_of(a.equip_slots()); });
    k.method("equips", [](GameActor& a, js::Vm&, Args&) { return items_js(a.equips()); });
    k.method("weapons", [](GameActor& a, js::Vm&, Args&) { return items_js(a.weapons()); });
    k.method("armors", [](GameActor& a, js::Vm&, Args&) { return items_js(a.armors()); });
    k.boolean("hasWeapon", [](GameActor& a, Args& x) { return a.has_weapon(item_of(js::arg(x, 0))); });
    k.boolean("hasArmor", [](GameActor& a, Args& x) { return a.has_armor(item_of(js::arg(x, 0))); });
    k.boolean("isEquipChangeOk", [](GameActor& a, Args& x) { return a.is_equip_change_ok(js::arg_int(x, 0)); });
    k.call("changeEquip", [](GameActor& a, Args& x) { a.change_equip(js::arg_int(x, 0), item_of(js::arg(x, 1))); });
    k.call("forceChangeEquip", [](GameActor& a, Args& x) { a.force_change_equip(js::arg_int(x, 0), item_of(js::arg(x, 1))); });
    k.call("changeEquipById", [](GameActor& a, Args& x) { a.change_equip_by_id(js::arg_int(x, 0), js::arg_int(x, 1)); });
    k.boolean("isEquipped", [](GameActor& a, Args& x) { return a.is_equipped(item_of(js::arg(x, 0))); });
    k.call("discardEquip", [](GameActor& a, Args& x) { a.discard_equip(item_of(js::arg(x, 0))); });
    k.call("releaseUnequippableItems", [](GameActor& a, Args& x) { a.release_unequippable_items(js::arg(x, 0).truthy()); });
    k.call("clearEquipments", [](GameActor& a, Args&) { a.clear_equipments(); });
    k.call("optimizeEquipments", [](GameActor& a, Args&) { a.optimize_equipments(); });
    k.boolean("isBattleMember", [](GameActor& a, Args&) { return a.is_battle_member(); });
    k.method("currentClass", [](GameActor& a, js::Vm&, Args&) { return data_js(a.current_class()); });
    k.boolean("isClass", [](GameActor& a, Args& x) { return a.is_class(item_of(js::arg(x, 0)).id); });
    k.method("skills", [](GameActor& a, js::Vm&, Args&) { return items_js(a.skills()); });
    k.method("usableSkills", [](GameActor& a, js::Vm&, Args&) { return items_js(a.usable_skills()); });
    k.boolean("hasNoWeapons", [](GameActor& a, Args&) { return a.has_no_weapons(); });
    k.num("attackAnimationId1", [](GameActor& a, Args&) { return (double)a.attack_animation_id1(); });
    k.num("attackAnimationId2", [](GameActor& a, Args&) { return (double)a.attack_animation_id2(); });
    k.call("changeExp", [](GameActor& a, Args& x) { a.change_exp(js::arg_num(x, 0), js::arg(x, 1).truthy()); });
    k.call("levelUp", [](GameActor& a, Args&) { a.level_up(); });
    k.call("levelDown", [](GameActor& a, Args&) { a.level_down(); });
    k.call("gainExp", [](GameActor& a, Args& x) { a.gain_exp(js::arg_num(x, 0)); });
    k.num("finalExpRate", [](GameActor& a, Args&) { return a.final_exp_rate(); });
    k.call("changeLevel", [](GameActor& a, Args& x) { a.change_level(js::arg_int(x, 0), js::arg(x, 1).truthy()); });
    k.call("learnSkill", [](GameActor& a, Args& x) { a.learn_skill(js::arg_int(x, 0)); });
    k.call("forgetSkill", [](GameActor& a, Args& x) { a.forget_skill(js::arg_int(x, 0)); });
    k.boolean("isLearnedSkill", [](GameActor& a, Args& x) { return a.is_learned_skill(js::arg_int(x, 0)); });
    k.boolean("hasSkill", [](GameActor& a, Args& x) { return a.has_skill(js::arg_int(x, 0)); });
    k.call("changeClass", [](GameActor& a, Args& x) { a.change_class(js::arg_int(x, 0), js::arg(x, 1).truthy()); });
    k.call("setCharacterImage", [](GameActor& a, Args& x) { a.set_character_image(js::arg(x, 0).to_str(), js::arg_int(x, 1)); });
    k.call("setFaceImage", [](GameActor& a, Args& x) { a.set_face_image(js::arg(x, 0).to_str(), js::arg_int(x, 1)); });
    k.call("setBattlerImage", [](GameActor& a, Args& x) { a.set_battler_image(js::arg(x, 0).to_str()); });
    k.boolean("isSpriteVisible", [](GameActor& a, Args&) { return a.is_sprite_visible(); });
    k.call("performVictory", [](GameActor& a, Args&) { a.perform_victory(); });
    k.call("performEscape", [](GameActor& a, Args&) { a.perform_escape(); });
    k.method("inputtingAction", [](GameActor& a, js::Vm&, Args&) {
        auto x = a.inputting_action();
        return x ? held_js(x) : Val();
    });
    k.boolean("selectNextCommand", [](GameActor& a, Args&) { return a.select_next_command(); });
    k.boolean("selectPreviousCommand", [](GameActor& a, Args&) { return a.select_previous_command(); });
    k.call("equipSkill", [](GameActor& a, Args& x) { a.equip_skill(js::arg_int(x, 0), js::arg_int(x, 1)); });
    k.call("unequipSkill", [](GameActor& a, Args& x) { a.unequip_skill(js::arg_int(x, 0), js::has_arg(x, 1) ? js::arg(x, 1).truthy() : true); });
    k.call("setMovementGraphics", [](GameActor& a, Args& x) { a.set_movement_graphics(js::arg(x, 0), js::arg(x, 1), js::arg(x, 2)); });
    k.call("setMovementGraphicData", [](GameActor& a, Args& x) { a.set_movement_graphic_data(js::arg(x, 0).to_str(), js::arg(x, 1)); });
    k.call("clearMovementGraphics", [](GameActor& a, Args&) { a.clear_movement_graphics(); });
    k.str("faceSaveLoad", [](GameActor& a, Args&) { return a.face_save_load(); });
    k.num("faceSaveLoadIndex", [](GameActor& a, Args&) { return (double)a.face_save_load_index(); });
    k.call("lockEquipmentSlots", [](GameActor& a, Args& x) { a.lock_equipment_slots(ints_from(js::arg(x, 0))); });
    k.call("unlockEquipmentSlots", [](GameActor& a, Args& x) { a.unlock_equipment_slots(ints_from(js::arg(x, 0))); });
    k.call("disableBattleCommands", [](GameActor& a, Args& x) { a.disable_battle_commands(ints_from(js::arg(x, 0))); });
    k.call("enableBattleCommands", [](GameActor& a, Args& x) { a.enable_battle_commands(ints_from(js::arg(x, 0))); });
    k.call("lockEquipSkill", [](GameActor& a, Args& x) { a.lock_equip_skill(ints_from(js::arg(x, 0))); });
    k.call("unlockEquipSkill", [](GameActor& a, Args& x) { a.unlock_equip_skill(ints_from(js::arg(x, 0))); });
    k.num("battleStatusIndex", [](GameActor& a, Args&) { return (double)a.battle_status_index(); });
}

void bind_enemy() {
    auto k = js::klass_of<GameEnemy>(klass("Game_Enemy", "Game_Battler"));
    k.num_field("_enemyId", &GameEnemy::enemy_id_);
    k.str_field("_letter", &GameEnemy::letter);
    k.bool_field("_plural", &GameEnemy::plural);
    k.num_field("_screenX", &GameEnemy::screen_x_);
    k.num_field("_screenY", &GameEnemy::screen_y_);
    k.num_field("_troopId", &GameEnemy::troop_id);
    k.get("_stealableItems", [](GameEnemy& e) {
        std::vector<Val> v;
        js::Vm& vm = js::vm();
        for (auto& s : e.stealable_items()) {
            Val o = js::make_object();
            vm.put(o, js::atom("type"), Val::str(s.type));
            vm.put(o, js::atom("id"), Val::num(s.id));
            vm.put(o, js::atom("rate"), Val::str(s.rate));
            vm.put(o, js::atom("isStolen"), Val::boolean(s.is_stolen));
            vm.put(o, js::atom("isDrop"), Val::boolean(s.is_drop));
            v.push_back(o);
        }
        return js::make_array(std::move(v));
    });
    k.num("enemyId", [](GameEnemy& e, Args&) { return (double)e.enemy_id(); });
    k.method("enemy", [](GameEnemy& e, js::Vm&, Args&) { return data_js(e.enemy()); });
    k.num("exp", [](GameEnemy& e, Args&) { return e.exp(); });
    k.num("gold", [](GameEnemy& e, Args&) { return e.gold(); });
    k.method("makeDropItems", [](GameEnemy& e, js::Vm&, Args&) { return items_js(e.make_drop_items()); });
    k.num("dropItemRate", [](GameEnemy& e, Args&) { return e.drop_item_rate(); });
    k.boolean("isSpriteVisible", [](GameEnemy& e, Args&) { return e.is_sprite_visible(); });
    k.num("screenX", [](GameEnemy& e, Args&) { return e.screen_x(); });
    k.num("screenY", [](GameEnemy& e, Args&) { return e.screen_y(); });
    k.str("battlerName", [](GameEnemy& e, Args&) { return e.battler_name(); });
    k.num("battlerHue", [](GameEnemy& e, Args&) { return (double)e.battler_hue(); });
    k.str("originalName", [](GameEnemy& e, Args&) { return e.original_name(); });
    k.boolean("isLetterEmpty", [](GameEnemy& e, Args&) { return e.letter.empty(); });
    k.call("setLetter", [](GameEnemy& e, Args& x) { e.set_letter(js::arg(x, 0).to_str()); });
    k.call("setPlural", [](GameEnemy& e, Args& x) { e.set_plural(js::arg(x, 0).truthy()); });
    k.call("transform", [](GameEnemy& e, Args& x) { e.transform(js::arg_int(x, 0)); });
    k.method("skills", [](GameEnemy& e, js::Vm&, Args&) { return items_js(e.skills()); });
    k.boolean("hasSkill", [](GameEnemy& e, Args& x) { return e.has_skill_id(js::arg_int(x, 0)); });
    k.num("attackAnimationId", [](GameEnemy& e, Args&) { return (double)e.attack_animation_id(); });
    k.num("baseId", [](GameEnemy& e, Args&) { return (double)e.base_id(); });
    k.boolean("isScanned", [](GameEnemy& e, Args&) { return e.is_scanned(); });
    k.boolean("canScan", [](GameEnemy& e, Args&) { return e.can_scan(); });
    k.num("deathTurn", [](GameEnemy& e, Args&) { return e.death_turn(); });
    k.method("killer", [](GameEnemy& e, js::Vm&, Args&) { return obj_js(e.killer()); });
    k.call("setAIPattern", [](GameEnemy& e, Args&) { e.set_ai_pattern(); });
    k.num("aiLevel", [](GameEnemy& e, Args&) { return e.ai_level(); });
}

std::vector<GameBattler*> as_battlers(const std::vector<GameActor*>& v) { return std::vector<GameBattler*>(v.begin(), v.end()); }

void bind_units() {
    {
        auto k = js::klass_of<GameUnit>(klass("Game_Unit"));
        k.bool_field("_inBattle", &GameUnit::in_battle_);
        k.boolean("inBattle", [](GameUnit& u, Args&) { return u.in_battle(); });
        k.method("members", [](GameUnit& u, js::Vm&, Args&) { return arr_of(u.unit_members()); });
        k.method("allMembers", [](GameUnit& u, js::Vm&, Args&) { return arr_of(u.unit_all_members()); });
        k.method("aliveMembers", [](GameUnit& u, js::Vm&, Args&) { return arr_of(u.alive_members()); });
        k.method("deadMembers", [](GameUnit& u, js::Vm&, Args&) { return arr_of(u.dead_members()); });
        k.method("movableMembers", [](GameUnit& u, js::Vm&, Args&) { return arr_of(u.movable_members()); });
        k.method("allAliveMembers", [](GameUnit& u, js::Vm&, Args&) { return arr_of(u.all_alive_members()); });
        k.method("tauntMembers", [](GameUnit& u, js::Vm&, Args&) { return arr_of(u.taunt_members()); });
        k.num("agility", [](GameUnit& u, Args&) { return u.agility(); });
        k.call("clearActions", [](GameUnit& u, Args&) { u.clear_actions(); });
        k.call("clearResults", [](GameUnit& u, Args&) { u.clear_results(); });
        k.boolean("isAllDead", [](GameUnit& u, Args&) { return u.is_all_dead(); });
        k.call("refreshMembers", [](GameUnit& u, Args&) { u.refresh_members(); });
        k.call("processStateEval", [](GameUnit& u, Args& a) { u.process_state_eval(js::arg(a, 0).to_str()); });
        k.method("randomTarget", [](GameUnit& u, js::Vm&, Args&) { return obj_js(u.random_target()); });
        k.method("randomDeadTarget", [](GameUnit& u, js::Vm&, Args&) { return obj_js(u.random_dead_target()); });
        k.method("smoothTarget", [](GameUnit& u, js::Vm&, Args& a) { return obj_js(u.smooth_target(js::arg_int(a, 0))); });
        k.method("smoothDeadTarget", [](GameUnit& u, js::Vm&, Args& a) { return obj_js(u.smooth_dead_target(js::arg_int(a, 0))); });
        k.method("substituteBattler", [](GameUnit& u, js::Vm&, Args&) { return obj_js(u.substitute_battler()); });
        k.call("select", [](GameUnit& u, Args& a) { u.select(battler_of(js::arg(a, 0))); });
        k.call("requestMotionRefresh", [](GameUnit& u, Args&) { u.request_motion_refresh(); });
        k.num("lowestIndexMember", [](GameUnit& u, Args&) { return (double)u.lowest_index_member(); });
        k.num("getLowestIndexMember", [](GameUnit& u, Args&) { return (double)u.lowest_index_member(); });
    }
    {
        auto k = js::klass_of<GameParty>(klass("Game_Party", "Game_Unit"));
        k.num_field("_gold", &GameParty::gold_);
        k.num_field("_steps", &GameParty::steps_);
        k.num_field("_menuActorId", &GameParty::menu_actor_id);
        k.num_field("_targetActorId", &GameParty::target_actor_id);
        k.prop(
            "_actors", [](GameParty& p) { return ints_of(p.actors); }, [](GameParty& p, const Val& v) { p.actors = ints_from(v); });
        k.prop(
            "stressEnergyCount", [](GameParty& p) { return Val::num(p.stress_energy_count); }, [](GameParty& p, const Val& v) { p.set_stress_energy(v.to_num()); });
        k.val_field("_questList", &GameParty::quest_list);
        k.val_field("_albumPictures", &GameParty::album_pictures);
        k.prop(
            "_defeatedEnemies", [](GameParty& p) { return ints_of(p.defeated_enemies); }, [](GameParty& p, const Val& v) { p.defeated_enemies = ints_from(v); });
        k.prop(
            "_blackLetters", [](GameParty& p) { return ints_of(p.black_letters); }, [](GameParty& p, const Val& v) { p.black_letters = ints_from(v); });
        k.method("members", [](GameParty& p, js::Vm&, Args&) { return arr_of(p.members()); });
        k.method("allMembers", [](GameParty& p, js::Vm&, Args&) { return arr_of(p.all_members()); });
        k.method("battleMembers", [](GameParty& p, js::Vm&, Args&) { return arr_of(p.battle_members()); });
        k.num("size", [](GameParty& p, Args&) { return (double)p.size(); });
        k.boolean("isEmpty", [](GameParty& p, Args&) { return p.is_empty(); });
        k.boolean("exists", [](GameParty& p, Args&) { return p.exists(); });
        k.num("maxBattleMembers", [](GameParty& p, Args&) { return (double)p.max_battle_members(); });
        k.method("leader", [](GameParty& p, js::Vm&, Args&) { return obj_js(p.leader()); });
        k.call("reviveBattleMembers", [](GameParty& p, Args&) { p.revive_battle_members(); });
        k.method("items", [](GameParty& p, js::Vm&, Args&) { return items_js(p.items()); });
        k.method("weapons", [](GameParty& p, js::Vm&, Args&) { return items_js(p.weapons()); });
        k.method("armors", [](GameParty& p, js::Vm&, Args&) { return items_js(p.armors()); });
        k.method("equipItems", [](GameParty& p, js::Vm&, Args&) { return items_js(p.equip_items()); });
        k.method("allItems", [](GameParty& p, js::Vm&, Args&) { return items_js(p.all_items()); });
        k.call("setupStartingMembers", [](GameParty& p, Args&) { p.setup_starting_members(); });
        k.str("name", [](GameParty& p, Args&) { return p.name(); });
        k.num("highestLevel", [](GameParty& p, Args&) { return (double)p.highest_level(); });
        k.num("lowestLevel", [](GameParty& p, Args&) { return (double)p.lowest_level(); });
        k.num("averageLevel", [](GameParty& p, Args&) { return p.average_level(); });
        k.call("addActor", [](GameParty& p, Args& a) { p.add_actor(js::arg_int(a, 0)); });
        k.call("removeActor", [](GameParty& p, Args& a) { p.remove_actor(js::arg_int(a, 0)); });
        k.num("gold", [](GameParty& p, Args&) { return p.gold(); });
        k.call("gainGold", [](GameParty& p, Args& a) { p.gain_gold(js::arg_num(a, 0)); });
        k.call("loseGold", [](GameParty& p, Args& a) { p.lose_gold(js::arg_num(a, 0)); });
        k.num("maxGold", [](GameParty& p, Args&) { return p.max_gold(); });
        k.num("steps", [](GameParty& p, Args&) { return p.steps(); });
        k.call("increaseSteps", [](GameParty& p, Args&) { p.increase_steps(); });
        k.num("numItems", [](GameParty& p, Args& a) { return p.num_items(item_of(js::arg(a, 0))); });
        k.num("maxItems", [](GameParty& p, Args& a) { return p.max_items(item_of(js::arg(a, 0))); });
        k.boolean("hasMaxItems", [](GameParty& p, Args& a) { return p.has_max_items(item_of(js::arg(a, 0))); });
        k.boolean("hasItem", [](GameParty& p, Args& a) { return p.has_item(item_of(js::arg(a, 0)), js::arg(a, 1).truthy()); });
        k.boolean("isAnyMemberEquipped", [](GameParty& p, Args& a) { return p.is_any_member_equipped(item_of(js::arg(a, 0))); });
        k.call("gainItem", [](GameParty& p, Args& a) { p.gain_item(item_of(js::arg(a, 0)), js::arg_num(a, 1), js::arg(a, 2).truthy()); });
        k.call("loseItem", [](GameParty& p, Args& a) { p.lose_item(item_of(js::arg(a, 0)), js::arg_num(a, 1), js::arg(a, 2).truthy()); });
        k.call("consumeItem", [](GameParty& p, Args& a) { p.consume_item(item_of(js::arg(a, 0))); });
        k.boolean("canUse", [](GameParty& p, Args& a) { return p.can_use(item_of(js::arg(a, 0))); });
        k.boolean("canInput", [](GameParty& p, Args&) { return p.can_input(); });
        k.boolean("isAllDead", [](GameParty& p, Args&) { return p.is_all_dead(); });
        k.call("onPlayerWalk", [](GameParty& p, Args&) { p.on_player_walk(); });
        k.method("menuActor", [](GameParty& p, js::Vm&, Args&) { return obj_js(p.menu_actor()); });
        k.call("setMenuActor", [](GameParty& p, Args& a) { p.set_menu_actor(js::host_cast<GameActor>(js::arg(a, 0))); });
        k.call("makeMenuActorNext", [](GameParty& p, Args&) { p.make_menu_actor_next(); });
        k.call("makeMenuActorPrevious", [](GameParty& p, Args&) { p.make_menu_actor_previous(); });
        k.method("targetActor", [](GameParty& p, js::Vm&, Args&) { return obj_js(p.target_actor()); });
        k.call("setTargetActor", [](GameParty& p, Args& a) { p.set_target_actor(js::host_cast<GameActor>(js::arg(a, 0))); });
        k.method("lastItem", [](GameParty& p, js::Vm&, Args&) { return item_js(p.last_item_object()); });
        k.call("setLastItem", [](GameParty& p, Args& a) { p.set_last_item(item_of(js::arg(a, 0))); });
        k.call("swapOrder", [](GameParty& p, Args& a) { p.swap_order(js::arg_int(a, 0), js::arg_int(a, 1)); });
        k.boolean("partyAbility", [](GameParty& p, Args& a) { return p.party_ability(js::arg_int(a, 0)); });
        k.call("removeBattleStates", [](GameParty& p, Args&) { p.remove_battle_states(); });
        k.boolean("hasKeyItems", [](GameParty& p, Args& a) { return p.has_key_items(js::arg(a, 0).truthy()); });
        k.boolean("hasConsumableItems", [](GameParty& p, Args& a) { return p.has_consumable_items(js::arg(a, 0).truthy()); });
        k.boolean("hasToyItems", [](GameParty& p, Args& a) { return p.has_toy_items(js::arg(a, 0).truthy()); });
        k.boolean("hasValidPocketItems", [](GameParty& p, Args&) { return p.has_valid_pocket_items(); });
        k.boolean("isInFaraway", [](GameParty& p, Args&) { return p.is_in_faraway(); });
        k.call("changeWorldItemContainer", [](GameParty& p, Args& a) { p.change_world_item_container(js::has_arg(a, 0) ? js::arg_int(a, 0) : current_world_index()); });
        k.method("lastGainedItem", [](GameParty& p, js::Vm&, Args&) { return item_js(p.last_gained_item()); });
        k.call("sortMembersForTag", [](GameParty& p, Args& a) { p.sort_members_for_tag(js::arg_int(a, 0)); });
        k.call("addScannedEnemy", [](GameParty& p, Args& a) { p.add_scanned_enemy(js::arg_int(a, 0)); });
        k.boolean("actorIsAffectedByState", [](GameParty& p, Args& a) { return p.actor_is_affected_by_state(js::arg_int(a, 0), js::arg_int(a, 1)); });
        k.method("getOmori", [](GameParty& p, js::Vm&, Args&) { return obj_js(p.get_omori()); });
        k.call("setQuestStandByMessage", [](GameParty& p, Args& a) { p.set_quest_stand_by_message(js::arg(a, 0)); });
        k.method("addQuest", [](GameParty& p, js::Vm&, Args& a) { return p.add_quest(js::arg(a, 0).to_str(), js::arg(a, 1)); });
        k.call("removeQuest", [](GameParty& p, Args& a) { p.remove_quest(js::arg(a, 0).to_str()); });
        k.boolean("hasQuest", [](GameParty& p, Args& a) { return p.has_quest(js::arg(a, 0).to_str()); });
        k.call("setQuestCompleteState", [](GameParty& p, Args& a) { p.set_quest_complete_state(js::arg(a, 0).to_str(), js::arg(a, 1).truthy()); });
        k.call("setQuestMessageIndex", [](GameParty& p, Args& a) { p.set_quest_message_index(js::arg(a, 0).to_str(), js::arg(a, 1)); });
        k.call("createPictureAlbum", [](GameParty& p, Args& a) { p.create_picture_album(js::arg(a, 0).to_str()); });
        k.num("albumSize", [](GameParty& p, Args& a) { return (double)p.album_size(js::arg(a, 0).to_str()); });
        k.call("addPictureToAlbum", [](GameParty& p, Args& a) { p.add_picture_to_album(js::arg(a, 0).to_str(), js::arg_int(a, 1), js::arg(a, 2)); });
        k.call("removePictureToAlbum", [](GameParty& p, Args& a) { p.remove_picture_to_album(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
        k.method("getAlbumPictureAtIndex", [](GameParty& p, js::Vm&, Args& a) { return p.get_album_picture_at_index(js::arg(a, 0).to_str(), js::arg_int(a, 1)); });
        k.method("albumHasPicture", [](GameParty& p, js::Vm&, Args& a) { return p.album_has_picture(js::arg(a, 0).to_str(), js::arg(a, 1)); });
        k.call("addDefeatedEnemy", [](GameParty& p, Args& a) { p.add_defeated_enemy(js::arg_int(a, 0)); });
        k.str("lastBlackLetter", [](GameParty& p, Args&) { return p.last_black_letter(); });
        k.call("onBattleStart", [](GameParty& p, Args&) { p.on_battle_start(); });
        k.call("onBattleEnd", [](GameParty& p, Args&) { p.on_battle_end(); });
        k.call("makeActions", [](GameParty& p, Args&) { p.make_actions(); });
        k.call("performVictory", [](GameParty& p, Args&) { p.perform_victory(); });
        k.call("performEscape", [](GameParty& p, Args&) { p.perform_escape(); });
        k.call("requestMotionRefresh", [](GameParty& p, Args&) { p.request_motion_refresh_all(); });
        k.num("battleNumItems", [](GameParty& p, Args& a) { return p.battle_num_items(item_of(js::arg(a, 0))); });
    }
    {
        auto k = js::klass_of<GameTroop>(klass("Game_Troop", "Game_Unit"));
        k.num_field("_troopId", &GameTroop::troop_id);
        k.num_field("_turnCount", &GameTroop::turn_count_);
        k.get("_interpreter", [](GameTroop& t) { return t.interpreter ? t.interpreter->to_js() : Val::null(); });
        k.get("_enemies", [](GameTroop& t) { return arr_of(t.members()); });
        k.method("members", [](GameTroop& t, js::Vm&, Args&) { return arr_of(t.members()); });
        k.boolean("isEventRunning", [](GameTroop& t, Args&) { return t.is_event_running(); });
        k.call("updateInterpreter", [](GameTroop& t, Args&) { t.update_interpreter(); });
        k.num("turnCount", [](GameTroop& t, Args&) { return (double)t.turn_count(); });
        k.call("clear", [](GameTroop& t, Args&) { t.clear(); });
        k.method("troop", [](GameTroop& t, js::Vm&, Args&) { return data_js(t.troop()); });
        k.call("setup", [](GameTroop& t, Args& a) { t.setup(js::arg_int(a, 0)); });
        k.call("makeUniqueNames", [](GameTroop& t, Args&) { t.make_unique_names(); });
        k.method("enemyNames", [](GameTroop& t, js::Vm&, Args&) {
            std::vector<Val> v;
            for (auto& n : t.enemy_names()) v.push_back(Val::str(n));
            return js::make_array(std::move(v));
        });
        k.call("setupBattleEvent", [](GameTroop& t, Args&) { t.setup_battle_event(); });
        k.call("increaseTurn", [](GameTroop& t, Args&) { t.increase_turn(); });
        k.num("expTotal", [](GameTroop& t, Args&) { return t.exp_total(); });
        k.num("goldTotal", [](GameTroop& t, Args&) { return t.gold_total(); });
        k.num("goldRate", [](GameTroop& t, Args&) { return t.gold_rate(); });
        k.method("makeDropItems", [](GameTroop& t, js::Vm&, Args&) { return items_js(t.make_drop_items()); });
        k.call("addEnemyReinforcement", [](GameTroop& t, Args& a) { t.add_enemy_reinforcement(js::arg_int(a, 0), js::arg_int(a, 1)); });
        k.call("addTroopReinforcements", [](GameTroop& t, Args& a) { t.add_troop_reinforcements(js::arg_int(a, 0)); });
        k.call("removeEnemyReinforcement", [](GameTroop& t, Args& a) { t.remove_enemy_reinforcement(js::arg_int(a, 0), js::arg_int(a, 1)); });
        k.call("removeTroopReinforcements", [](GameTroop& t, Args& a) { t.remove_troop_reinforcements(js::arg_int(a, 0)); });
        k.boolean("isEnemyReinforcementAdded", [](GameTroop& t, Args& a) {
            return t.is_enemy_reinforcement_added(js::arg_int(a, 0), js::arg_int(a, 1), js::arg(a, 2).truthy());
        });
        k.boolean("isTroopReinforcementAdded", [](GameTroop& t, Args& a) { return t.is_troop_reinforcement_added(js::arg_int(a, 0), js::arg(a, 1).truthy()); });
        k.call("setupCommonEvent", [](GameTroop& t, Args& a) { t.setup_common_event(js::arg_int(a, 0)); });
        k.call("ignorePage", [](GameTroop& t, Args& a) { t.ignore_page(js::arg_int(a, 0)); });
        k.call("stopIgnorePage", [](GameTroop& t, Args& a) { t.stop_ignore_page(js::arg_int(a, 0)); });
        k.boolean("isHumphreyBattle", [](GameTroop& t, Args&) { return t.is_humphrey_battle(); });
        k.call("onBattleStart", [](GameTroop& t, Args&) { t.on_battle_start(); });
        k.call("onBattleEnd", [](GameTroop& t, Args&) { t.on_battle_end(); });
        k.call("makeActions", [](GameTroop& t, Args&) { t.make_actions(); });
    }
    {
        auto k = js::klass_of<GameActors>(klass("Game_Actors"));
        k.method("actor", [](GameActors& a, js::Vm&, Args& x) {
            GameActor* ac = a.actor(js::arg_int(x, 0));
            return ac ? ac->to_js() : Val::null();
        });
    }
}

}

void bind_battlers() {
    bind_result();
    bind_action();
    bind_battler_base();
    bind_battler();
    bind_actor();
    bind_enemy();
    bind_units();
}

}

js::HostClass* GameActionResult::js_class() const { return &bind::klass("Game_ActionResult").hc; }
js::HostClass* GameAction::js_class() const { return &bind::klass("Game_Action").hc; }
js::HostClass* GameActor::js_class() const { return &bind::klass("Game_Actor").hc; }
js::HostClass* GameEnemy::js_class() const { return &bind::klass("Game_Enemy").hc; }
js::HostClass* GameParty::js_class() const { return &bind::klass("Game_Party").hc; }
js::HostClass* GameTroop::js_class() const { return &bind::klass("Game_Troop").hc; }
js::HostClass* GameActors::js_class() const { return &bind::klass("Game_Actors").hc; }

}
