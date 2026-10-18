#include "battle.h"

#include <algorithm>
#include <cmath>

#include "../core/audio.h"
#include "../core/regex.h"
#include "../js/compile.h"
#include "../js/dataview.h"
#include "../js/random.h"
#include "../js/vm.h"
#include "../platform/platform.h"
#include "jsops.h"
#include "audio_manager.h"
#include "config.h"
#include "data.h"
#include "enemy.h"
#include "evalctx.h"
#include "game.h"
#include "hooks.h"
#include "interpreter.h"
#include "params.h"
#include "party.h"
#include "save.h"
#include "scene.h"

namespace rpg {
namespace battle {

namespace {

State g_state;

js::Val jsv(GameBattler* b) { return b ? b->to_js() : js::Val(); }

std::vector<GameBattler*> resolve(const std::vector<BattlerRef>& refs) {
    std::vector<GameBattler*> out;
    for (auto& r : refs) out.push_back(r.get());
    return out;
}

std::vector<BattlerRef> refs_of(const std::vector<GameBattler*>& v) {
    std::vector<BattlerRef> out;
    for (GameBattler* b : v) out.push_back(BattlerRef::of(b));
    return out;
}

template <class T>
bool contains(const std::vector<T*>& v, const T* x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

std::vector<GameBattler*> unique(const std::vector<GameBattler*>& v) {
    std::vector<GameBattler*> out;
    for (GameBattler* b : v) {
        if (!contains(out, b)) out.push_back(b);
    }
    return out;
}

bool in_list(const std::string& s, std::initializer_list<const char*> list) {
    for (const char* x : list) {
        if (s == x) return true;
    }
    return false;
}

std::string arg(const std::vector<std::string>& a, size_t i) { return i < a.size() ? a[i] : std::string(); }

double js_or(const std::string& s, double def) {
    if (s.empty()) return def;
    double v = js_number(s);
    return (std::isnan(v) || v == 0) ? def : v;
}

JsEval seq_eval() {
    State& s = g_state;
    JsEval e{js::Val()};
    GameBattler* subj = s.has_subject ? s.subject.get() : nullptr;
    auto ts = resolve(s.targets);
    std::vector<js::Val> arr;
    for (GameBattler* t : ts) arr.push_back(jsv(t));
    e.set("subject", jsv(subj)).set("user", jsv(subj)).set("target", ts.empty() ? js::Val() : jsv(ts[0]));
    e.set("targets", js::make_array(arr)).set("action", s.action ? s.action->to_js() : js::Val());
    e.set("item", s.action ? s.action->item().to_js() : js::Val());
    return e;
}

js::Val eval_string(const std::string& code, JsEval& e, const char* where, bool* ok = nullptr) {
    auto prog = js::compile_cached(code);
    if (!prog) {
        if (ok) *ok = false;
        script_error(where, "could not compile: " + code);
        return js::Val();
    }
    bool good = e.run(prog.get(), where);
    if (ok) *ok = good;
    return good ? e.completion() : js::Val();
}

std::vector<ActSeq> seq_list(const Value& v) {
    std::vector<ActSeq> out;
    for (size_t i = 0; i < v.size(); i++) {
        ActSeq a;
        a.name = v[i][0].string();
        const Value& args = v[i][1];
        for (size_t k = 0; k < args.size(); k++) a.args.push_back(data::js_string(args[k]));
        out.push_back(a);
    }
    return out;
}

std::vector<ActSeq> seq_list_js(const js::Val& v) {
    std::vector<ActSeq> out;
    if (!v.is_array()) return out;
    js::Vm& vm = js::vm();
    for (auto& e : v.o->items) {
        ActSeq a;
        a.name = vm.get(e, js::Val::num(0)).to_str();
        js::Val args = vm.get(e, js::Val::num(1));
        if (args.is_array()) {
            for (auto& x : args.o->items) a.args.push_back(x.to_str());
        }
        out.push_back(a);
    }
    return out;
}

AudioObj audio_from_args(const std::vector<std::string>& args) {
    AudioObj a;
    a.valid = true;
    a.name = args[0];
    a.volume = args.size() > 1 && !args[1].empty() ? js_number(args[1]) : yanfly_param("SoundVolume").num();
    a.pitch = args.size() > 2 && !args[2].empty() ? js_number(args[2]) : yanfly_param("SoundPitch").num();
    a.pan = args.size() > 3 && !args[3].empty() ? js_number(args[3]) : yanfly_param("SoundPan").num();
    return a;
}

int param_id_of(const std::string& stat) {
    if (in_list(stat, {"HP", "MAXHP", "MAX HP"})) return 0;
    if (in_list(stat, {"MP", "MAXMP", "MAX MP", "SP", "MAXSP", "MAX SP"})) return 1;
    if (in_list(stat, {"ATK", "STR"})) return 2;
    if (stat == "DEF") return 3;
    if (in_list(stat, {"MAT", "INT"})) return 4;
    if (in_list(stat, {"MDF", "RES"})) return 5;
    if (in_list(stat, {"AGI", "SPD"})) return 6;
    if (stat == "LUK") return 7;
    return -1;
}

bool has_show(const std::vector<std::string>& args) {
    for (auto& a : args) {
        if (to_upper(a) == "SHOW") return true;
    }
    return false;
}

}

State& st() { return g_state; }

GameBattler* subject() { return g_state.has_subject ? g_state.subject.get() : nullptr; }
std::vector<GameBattler*> targets() { return resolve(g_state.targets); }
std::vector<GameBattler*> all_targets() { return resolve(g_state.all_targets); }

void register_scene(BattleLog* log, Spriteset* spriteset, BattleScene* scene) {
    g_state.log = log;
    g_state.spriteset = spriteset;
    g_state.scene = scene;
}

void unregister_scene() {
    g_state.log = nullptr;
    g_state.spriteset = nullptr;
    g_state.scene = nullptr;
}

void init_members() {
    State& s = g_state;
    BattleLog* log = s.log;
    Spriteset* spriteset = s.spriteset;
    BattleScene* scene = s.scene;
    bool retried = s.battle_retried;
    int final_phase = s.final_battle_phase;
    bool has_setup = s.has_retry_setup;
    int rt = s.retry_troop;
    bool rce = s.retry_can_escape, rcl = s.retry_can_lose;
    std::vector<uint8_t> retry = s.retry_data;
    bool show_enc = s.show_encounter_effect;
    bool show_dmg = s.show_damage;
    bool waiting_retry = s.waiting_for_retry_video;
    bool bypass = s.bypass_move_to_start_location;
    bool final_battle = s.is_final_battle;
    js::Expando exp = s.expando;
    double cx = s.camera_x, cy = s.camera_y, cox = s.camera_offset_x, coy = s.camera_offset_y;
    std::vector<BattlerRef> cfg = s.camera_focus_group;
    std::string cfx = s.camera_focus_pos_x, cfy = s.camera_focus_pos_y;
    bool cc = s.camera_clamp;
    s = State();
    s.log = log;
    s.spriteset = spriteset;
    s.scene = scene;
    s.battle_retried = retried;
    s.final_battle_phase = final_phase;
    s.has_retry_setup = has_setup;
    s.retry_troop = rt;
    s.retry_can_escape = rce;
    s.retry_can_lose = rcl;
    s.retry_data = retry;
    s.show_encounter_effect = show_enc;
    s.show_damage = show_dmg;
    s.waiting_for_retry_video = waiting_retry;
    s.bypass_move_to_start_location = bypass;
    s.is_final_battle = final_battle;
    s.expando = exp;
    s.camera_x = cx;
    s.camera_y = cy;
    s.camera_offset_x = cox;
    s.camera_offset_y = coy;
    s.camera_focus_group = cfg;
    s.camera_focus_pos_x = cfx;
    s.camera_focus_pos_y = cfy;
    s.camera_clamp = cc;
    s.phase = "init";
    s.actor_command_index = 0;
    s.picture_display_layer = "front";
    s.hide_low_hp_overlay = false;
}

void setup(int troop_id, bool can_esc, bool can_l) {
    State& s = g_state;
    s.has_retry_setup = true;
    s.retry_troop = troop_id;
    s.retry_can_escape = can_esc;
    s.retry_can_lose = can_l;
    if (!s.battle_retried) {
        s.final_battle_phase = 0;
        make_battle_retry_data();
    }
    reset_camera(0);
    action_reset_zoom(1);
    init_members();
    s.can_escape = can_esc;
    s.can_lose = can_l;
    game_troop().setup(troop_id);
    game_screen().on_battle_start();
    make_escape_ratio();
    s.escape_animation_played = false;
    s.escape_by_command = false;
    if (game_troop().is_humphrey_battle()) game_temp().expando.set(js::atom("_bb1Name"), js::Val::str(game_map().battleback1_name()));
    else game_temp().expando.set(js::atom("_bb1Name"), js::Val());
}

void set_event_callback(std::function<void(int)> cb) { g_state.event_callback = std::move(cb); }

double rate_preemptive() { return game_party().rate_preemptive(game_troop().agility()); }
double rate_surprise() { return game_party().rate_surprise(game_troop().agility()); }

void on_encounter() {
    g_state.preemptive = rng::next() < rate_preemptive();
    g_state.surprise = rng::next() < rate_surprise() && !g_state.preemptive;
}

void save_bgm_and_bgs() {
    g_state.map_bgm = audio_manager::save_bgm();
    g_state.has_map_bgm = true;
    g_state.map_bgs = audio_manager::save_bgs();
    g_state.has_map_bgs = true;
}

void play_battle_bgm() {
    audio_manager::play_bgm(game_system().battle_bgm());
    audio_manager::stop_bgs();
}

void play_victory_me() {
    audio_manager::play_me(game_system().victory_me());
    audio_manager::loop_current_me();
}

void play_defeat_me() { audio_manager::play_me(game_system().defeat_me()); }

void replay_bgm_and_bgs() {
    if (g_state.has_map_bgm) audio_manager::replay_bgm(g_state.map_bgm);
    else audio_manager::stop_bgm();
    if (g_state.has_map_bgs) audio_manager::replay_bgs(g_state.map_bgs);
}

void make_escape_ratio() {
    State& s = g_state;
    if (is_dtb()) {
        JsEval e{js::Val()};
        bool ok = false;
        js::Val r = e.run(yanfly_ast("BECEscRatio"), "ESCAPE RATIO FORMULA ERROR") ? e.completion() : js::Val::num(0);
        s.escape_ratio = r.to_num();
        JsEval f{js::Val()};
        ok = f.run(yanfly_ast("BECEscFail"), "ESCAPE FAIL BOOST FORMULA ERROR");
        s.escape_fail_boost = ok ? f.completion().to_num() : 0;
    } else {
        s.escape_fail_boost = 0.1;
        s.escape_ratio = 0.5 * game_party().agility() / game_troop().agility();
    }
}

bool is_busy() {
    if (is_battle_intro_active()) return true;
    State& s = g_state;
    return game_message().is_busy() || (s.spriteset && s.spriteset->is_busy()) || (s.log && s.log->is_busy());
}

bool is_inputting() {
    if (game_temp().taunt_mode) return false;
    return g_state.phase == "input";
}

bool is_in_turn() { return g_state.phase == "turn"; }
bool is_turn_end() { return g_state.phase == "turnEnd"; }
bool is_aborting() { return g_state.phase == "aborting"; }
bool is_battle_end() { return g_state.phase == "battleEnd"; }
bool can_escape() { return g_state.can_escape; }
bool can_lose() { return g_state.can_lose; }
bool is_escaped() { return g_state.escaped; }

void update() {
    State& s = g_state;
    if (s.phase == "escape_animation") {
        if (!game_switches().value(1601)) {
            s.escape_animation_played = true;
            process_abort();
            return;
        }
        game_troop().update_interpreter();
        return;
    }
    if (!is_busy() && !update_event()) {
        const std::string& p = s.phase;
        if (p == "start") start_input();
        else if (p == "turn") update_turn();
        else if (p == "action") update_action();
        else if (p == "phaseChange") update_phase();
        else if (p == "actionList") update_action_list();
        else if (p == "actionTargetList") update_action_target_list();
        else if (p == "turnEnd") update_turn_end();
        else if (p == "battleEnd") update_battle_end();
    }
}

bool update_event() {
    State& s = g_state;
    if (s.suspend_events) return false;
    if (s.processing_forced_action) return false;
    const std::string& p = s.phase;
    if (p == "start" || p == "turn" || p == "turnEnd" || p == "actionList" || p == "actionTargetList") {
        if (is_action_forced()) {
            process_forced_action();
            return true;
        }
        return update_event_main();
    }
    return check_abort();
}

bool update_event_main() {
    game_troop().update_interpreter();
    game_party().request_motion_refresh_all();
    if (game_troop().is_event_running() || check_battle_end()) return true;
    game_troop().setup_battle_event();
    if (game_troop().is_event_running() || scene_manager::is_scene_changing()) return true;
    return false;
}

GameActor* actor() {
    int i = g_state.actor_index;
    if (i < 0) return nullptr;
    auto m = game_party().members();
    return i < (int)m.size() ? m[(size_t)i] : nullptr;
}

void clear_actor() { change_actor(-1, ""); }

void change_actor(int new_index, const std::string& last_state) {
    GameActor* last = actor();
    g_state.actor_index = new_index;
    GameActor* now = actor();
    if (last) last->set_action_state(last_state);
    if (now) now->set_action_state("inputting");
}

void start_battle() {
    State& s = g_state;
    s.phase = "start";
    game_system().on_battle_start();
    game_party().on_battle_start();
    game_troop().on_battle_start();
    game_party().stress_energy_count = 3;
    set_picture_display_layer("top");
    if (s.scene) s.scene->raise_fade_sprite();
}

void start_input() {
    State& s = g_state;
    s.phase = "input";
    game_party().make_actions();
    game_troop().make_actions();
    clear_actor();
    if (s.surprise || !game_party().can_input()) start_turn();
}

std::shared_ptr<GameAction> inputting_action() {
    GameActor* a = actor();
    return a ? a->inputting_action() : nullptr;
}

std::vector<int> get_actor_input_order() {
    auto members = game_party().members();
    std::vector<int> order = game_switches().value(7) ? std::vector<int>{8, 10, 9, 11} : std::vector<int>{1, 2, 3, 4};
    std::vector<int> list;
    for (int id : order) {
        GameActor* a = game_actors().actor(id);
        auto it = std::find(members.begin(), members.end(), a);
        if (it == members.end()) continue;
        GameActor* m = *it;
        if (m->is_alive() && m->is_battle_member()) list.push_back((int)(it - members.begin()));
    }
    return list;
}

void select_next_command() {
    State& s = g_state;
    if (s.actor_command_index < 0) s.actor_command_index = 0;
    auto order = get_actor_input_order();
    auto at = [&](int i) { return i >= 0 && i < (int)order.size() ? order[(size_t)i] : -1; };
    s.actor_index = at(s.actor_command_index);
    do {
        if (!actor() || !actor()->select_next_command()) {
            change_actor(at(s.actor_command_index++), "waiting");
            if (s.actor_command_index > (int)order.size()) {
                start_turn();
                break;
            }
        }
    } while (actor() && !actor()->can_input());
}

void select_previous_command() {
    State& s = g_state;
    s.actor_command_index--;
    auto order = get_actor_input_order();
    auto at = [&](int i) { return i >= 0 && i < (int)order.size() ? order[(size_t)i] : -1; };
    s.actor_index = at(s.actor_command_index);
    do {
        if (!actor() || !actor()->select_previous_command()) {
            GameActor* a = actor();
            if (a) {
                auto cur = a->current_action();
                if (cur) cur->clear();
            }
            change_actor(at(s.actor_command_index - 1), "undecided");
            if (actor() && !actor()->can_input()) s.actor_command_index--;
            if (s.actor_command_index <= 0) {
                s.actor_index = -1;
                s.actor_command_index = 0;
                return;
            }
        }
    } while (actor() && !actor()->can_input());
    clear_inputting_action();
}

void refresh_status() {
    if (g_state.scene) g_state.scene->refresh_status();
}

void start_turn() {
    State& s = g_state;
    s.enter_end_phase = false;
    s.phase = "turn";
    clear_actor();
    game_troop().increase_turn();
    game_party().on_turn_start();
    game_troop().on_turn_start();
    s.performed_battlers.clear();
    make_action_orders();
    game_party().request_motion_refresh_all();
    if (s.log) s.log->start_turn();
    GameBattler* n = get_next_subject();
    s.subject = BattlerRef::of(n);
    s.has_subject = n != nullptr;
    s.actor_command_index = 0;
}

void update_turn() {
    State& s = g_state;
    game_party().request_motion_refresh_all();
    if (!subject()) {
        GameBattler* n = get_next_subject();
        s.subject = BattlerRef::of(n);
        s.has_subject = n != nullptr;
    }
    if (subject()) process_turn();
    else end_turn();
}

void process_turn() {
    State& s = g_state;
    s.process_turn = true;
    GameBattler* sub = subject();
    auto action = sub->current_action();
    if (action) {
        action->prepare();
        if (action->is_valid()) start_action();
        sub->remove_current_action();
    } else {
        sub->on_all_actions_end();
        refresh_status();
        if (s.log) {
            s.log->display_auto_affected_status(sub);
            s.log->display_current_state(sub);
            s.log->display_regeneration(sub);
        }
        GameBattler* n = get_next_subject();
        s.subject = BattlerRef::of(n);
        s.has_subject = n != nullptr;
    }
    s.process_turn = false;
}

void end_turn() {
    State& s = g_state;
    if (is_turn_based() && s.spriteset && s.spriteset->is_popup_playing()) return;
    if (is_turn_based() && s.enter_end_phase) {
        s.phase = "turnEnd";
        s.preemptive = false;
        s.surprise = false;
        return;
    }
    s.enter_end_phase = true;
    s.phase = "turnEnd";
    s.preemptive = false;
    s.surprise = false;
    for (GameBattler* b : all_battle_members()) {
        b->on_turn_end();
        refresh_status();
        if (s.log) {
            s.log->display_auto_affected_status(b);
            s.log->display_regeneration(b);
        }
    }
    if (is_forced_turn()) s.turn_forced = false;
    refresh_all_members();
}

bool is_forced_turn() { return g_state.turn_forced; }

void update_turn_end() { start_input(); }

GameBattler* get_next_subject() {
    State& s = g_state;
    if (game_troop().turn_count() <= 0) return nullptr;
    make_action_orders();
    GameBattler* battler = nullptr;
    for (;;) {
        std::vector<BattlerRef> arr;
        auto performed = resolve(s.performed_battlers);
        for (auto& r : s.action_battlers) {
            if (!contains(performed, r.get())) arr.push_back(r);
        }
        s.action_battlers = arr;
        if (s.action_battlers.empty()) return nullptr;
        BattlerRef first = s.action_battlers.front();
        s.action_battlers.erase(s.action_battlers.begin());
        GameBattler* b = first.get();
        if (!b) return nullptr;
        bool member = b->is_actor() ? static_cast<GameActor*>(b)->is_battle_member() : static_cast<GameEnemy*>(b)->is_battle_member();
        if (member && b->is_alive()) {
            s.performed_battlers.push_back(first);
            battler = b;
            break;
        }
    }
    if (battler && battler->is_enemy()) battler->set_ai_pattern();
    return battler;
}

std::vector<GameBattler*> all_battle_members() {
    std::vector<GameBattler*> out = game_party().unit_members();
    for (GameBattler* e : game_troop().unit_members()) out.push_back(e);
    return out;
}

void make_action_orders() {
    State& s = g_state;
    std::vector<GameBattler*> battlers;
    if (!s.surprise) {
        for (GameBattler* b : game_party().unit_members()) battlers.push_back(b);
    }
    if (!s.preemptive) {
        for (GameBattler* b : game_troop().unit_members()) battlers.push_back(b);
    }
    for (GameBattler* b : battlers) b->make_speed();
    std::stable_sort(battlers.begin(), battlers.end(), [](GameBattler* a, GameBattler* b) { return b->speed() - a->speed() < 0; });
    s.action_battlers = refs_of(battlers);
}

void set_targets(const std::vector<GameBattler*>& arr) {
    g_state.targets.clear();
    for (GameBattler* t : arr) {
        if (t) g_state.targets.push_back(BattlerRef::of(t));
    }
}

void start_action() {
    State& s = g_state;
    if (subject()) subject()->on_action_start_state_effects();
    GameBattler* sub = subject();
    if (!sub) {
        end_action();
        return;
    }
    auto action = sub->current_action();
    s.action = action;
    if (!s.action || !s.action->item().valid()) {
        end_action();
        return;
    }
    auto ts = action->make_targets();
    set_targets(ts);
    s.all_targets = refs_of(ts);
    s.individual_targets = refs_of(ts);
    s.phase = "phaseChange";
    s.phase_steps = {"setup", "whole", "target", "follow", "finish"};
    s.return_phase.clear();
    s.action_list.clear();
    sub->use_item(s.action->item());
    s.action->apply_global();
    if (s.log) s.log->start_action(sub, s.action.get(), resolve(s.targets));
}

void update_action() {
    State& s = g_state;
    if (!s.targets.empty()) {
        BattlerRef t = s.targets.front();
        s.targets.erase(s.targets.begin());
        GameBattler* target = t.get();
        if (target) {
            invoke_action(subject(), target);
            return;
        }
    }
    if (s.return_phase == "target") {
        GameBattler* first = s.individual_targets.empty() ? nullptr : s.individual_targets[0].get();
        set_targets({first});
        s.phase = "actionTargetList";
    } else {
        set_targets(resolve(s.all_targets));
        s.phase = "actionList";
    }
}

static bool load_pre_force_action_settings();

void end_action() {
    State& s = g_state;
    if (subject()) subject()->on_action_end_state_effects();
    if (subject()) subject()->on_all_actions_end();
    if (s.processing_forced_action) {
        if (subject()) subject()->remove_current_action();
        s.phase = s.pre_force_phase;
    }
    s.processing_forced_action = false;
    if (!load_pre_force_action_settings()) {
        if (s.log) s.log->end_action(subject());
        s.phase = "turn";
    }
    for (GameBattler* b : all_battle_members()) {
        b->no_effect_message = js::Val();
        b->no_state_message = js::Val();
    }
    refresh_status();
}

void invoke_action(GameBattler* sub, GameBattler* target) {
    State& s = g_state;
    bool opt = yanfly_param("BECOptSpeed").truthy();
    if (!opt && s.log) s.log->push("pushBaseLine");
    if (rng::next() < s.action->item_mrf(target)) invoke_magic_reflection(sub, target);
    else if (rng::next() < s.action->item_cnt(target)) invoke_counter_attack(sub, target);
    else invoke_normal_action(sub, target);
    if (sub) sub->set_last_target(target);
    if (!opt && s.log) s.log->push("popBaseLine");
}

void invoke_normal_action(GameBattler* sub, GameBattler* target) {
    GameBattler* real = apply_substitute(target);
    g_state.action->apply(real);
    if (g_state.log) g_state.log->display_action_results(sub, real);
}

static bool is_valid_counter_action(GameBattler* target) {
    State& s = g_state;
    if (s.counter_queue.empty()) return true;
    return s.counter_queue.front()->subject() != target;
}

static bool is_max_counter_queue() { return g_state.counter_sequence > yanfly_param("CounterMaxQueue").num(); }

static bool check_counter_eval(const std::string& code, const ItemRef& skill, GameBattler* sub, GameBattler* target) {
    JsEval e{js::Val()};
    e.set("action", g_state.action->to_js()).set("a", jsv(sub)).set("user", jsv(sub)).set("attacker", jsv(sub)).set("b", jsv(target)).set("defender", jsv(target));
    e.set("item", skill.to_js()).sv();
    bool ok = false;
    js::Val r = eval_string(code, e, "COUNTER CHECK ERROR", &ok);
    return ok && r.truthy();
}

static bool check_counter_line(const std::string& line, const ItemRef& skill, GameBattler* sub, GameBattler* target) {
    State& s = g_state;
    re::Groups g;
    std::string up = to_upper(line);
    auto hit = [&](int t) { return s.action->item_data()["hitType"].as_int() == t; };
    if (re::search("EVAL:[ ](.*)", "i", line, &g)) return check_counter_eval(g[1], skill, sub, target);
    if (up == "CERTAIN HIT") return hit(0);
    if (up == "PHYSICAL HIT") return hit(1);
    if (up == "MAGICAL HIT") return hit(2);
    if (up == "NOT CERTAIN HIT") return !hit(0);
    if (up == "NOT PHYSICAL HIT") return !hit(1);
    if (up == "NOT MAGICAL HIT") return !hit(2);
    if (up == "SINGLE TARGET") return s.action->is_for_one();
    if (up == "MULTI TARGET") return !s.action->is_for_one();
    if (up == "COUNTER HIT") return !s.action->is_counter();
    if (up == "NOT COUNTER HIT") return !s.action->is_counter();
    if (re::search("RANDOM:[ ](\\d+)([%\xEF\xBC\x85])", "i", line, &g)) return !(rng::next() < js_parse_float(g[1]) * 0.01);
    auto element = [&](const std::string& v) {
        int id;
        re::Groups h;
        if (re::search("(\\d+)", "i", v, &h)) id = h.num(1);
        else {
            id = data::root()["yanfly"]["ElementIdRef"][to_upper(v).c_str()].as_int();
            if (!id) return true;
        }
        int ae = s.action->item_data()["damage"]["elementId"].as_int();
        if (ae < 0) {
            auto el = subject()->attack_elements();
            return std::find(el.begin(), el.end(), id) != el.end();
        }
        return id == ae;
    };
    if (re::search("NOT ELEMENT:[ ](.*)", "i", line, &g)) return !element(g[1]);
    if (re::search("ELEMENT:[ ](.*)", "i", line, &g)) return element(g[1]);
    if (re::search("SWITCH[ ](\\d+)[ ]ON", "i", line, &g)) return game_switches().value(g.num(1));
    if (re::search("SWITCH[ ](\\d+)[ ]OFF", "i", line, &g)) return !game_switches().value(g.num(1));
    if (re::search("VARIABLE[ ](\\d+)[ ](.*)", "i", line, &g)) return check_counter_eval("$gameVariables.value(" + g[1] + ") " + g[2], skill, sub, target);
    auto skill_check = [&](const std::string& v) {
        if (!s.action->is_skill()) return false;
        re::Groups h;
        int id = re::search("(\\d+)", "i", v, &h) ? h.num(1) : data::root()["yanfly"]["SkillIdRef"][to_upper(v).c_str()].as_int();
        return s.action->item() == ItemRef::skill(id) && ItemRef::skill(id).valid();
    };
    auto stype_check = [&](const std::string& v) {
        if (!s.action->is_skill()) return false;
        re::Groups h;
        int id = re::search("(\\d+)", "i", v, &h) ? h.num(1) : data::root()["yanfly"]["STypeIdRef"][to_upper(v).c_str()].as_int();
        return s.action->item_data()["stypeId"].as_int() == id;
    };
    auto item_check = [&](const std::string& v) {
        if (!s.action->is_item()) return false;
        script_error("COUNTER CHECK ERROR", "ReferenceError: skillId is not defined");
        return false;
    };
    if (re::search("NOT SKILL:[ ](.*)", "i", line, &g)) return !skill_check(g[1]);
    if (re::search("SKILL:[ ](.*)", "i", line, &g)) return skill_check(g[1]);
    if (re::search("NOT STYPE:[ ](.*)", "i", line, &g)) return !stype_check(g[1]);
    if (re::search("STYPE:[ ](.*)", "i", line, &g)) return stype_check(g[1]);
    if (re::search("NOT ITEM:[ ](.*)", "i", line, &g)) return !item_check(g[1]);
    if (re::search("ITEM:[ ](.*)", "i", line, &g)) return item_check(g[1]);
    auto stat_eval = [&](const std::string& who, const std::string& v1) -> std::string {
        std::string u = to_upper(v1);
        std::string e = who + ".";
        if (in_list(u, {"LEVEL", "LV", "LVL"})) return e + "level";
        if (in_list(u, {"MAX HP", "MAXHP", "MHP"})) return e + "mhp";
        if (in_list(u, {"HP", "CURRENT HP"})) return e + "hp";
        if (in_list(u, {"MAX MP", "MAXMP", "MMP"})) return e + "mmp";
        if (in_list(u, {"MP", "CURRENT MP"})) return e + "mp";
        if (in_list(u, {"ATK", "STR"})) return e + "atk";
        if (u == "DEF") return e + "def";
        if (in_list(u, {"MAT", "INT", "SPI"})) return e + "mat";
        if (in_list(u, {"MDF", "RES"})) return e + "mdf";
        if (in_list(u, {"AGI", "SPD"})) return e + "agi";
        if (u == "LUK") return e + "luk";
        return "";
    };
    if (re::search("ATTACKER[ ](.*)[ ](.*)", "i", line, &g)) {
        std::string e = stat_eval("subject", g[1]);
        if (e.empty()) return false;
        JsEval ev{js::Val()};
        ev.set("subject", jsv(sub));
        return check_counter_eval(e + " " + g[1], skill, sub, target);
    }
    if (re::search("DEFENDER[ ](.*)[ ](.*)", "i", line, &g)) {
        std::string e = stat_eval("target", g[1]);
        if (e.empty()) return false;
        return check_counter_eval(e + " " + g[1], skill, sub, target);
    }
    return true;
}

static bool meet_counter_conditions(const ItemRef& skill, GameBattler* sub, GameBattler* target) {
    if (!target->can_use(skill)) return false;
    const Value& conds = skill.data()["counterConditions"];
    for (size_t i = 0; i < conds.size(); ++i) {
        if (!check_counter_line(conds[i].string(), skill, sub, target)) return false;
    }
    return true;
}

static bool get_counter_skill(GameBattler* sub, GameBattler* target, ItemRef& out) {
    target->make_counter_skills();
    for (auto& skill : target->counter_skills()) {
        if (!skill.valid()) {
            out = skill;
            return true;
        }
        if (meet_counter_conditions(skill, sub, target)) {
            out = skill;
            return true;
        }
    }
    return false;
}

static bool evade_and_counter(GameBattler* sub, GameBattler* target) {
    (void)sub;
    State& s = g_state;
    if (!s.has_counter_skill) return false;
    if (s.counter_skill_null) return false;
    if (target->force_hit_counter()) return false;
    if (target->force_evade_counter()) return true;
    return s.counter_skill.data()["evadeCounter"].truthy();
}

static void add_counter_queue(GameBattler* sub, GameBattler* target) {
    State& s = g_state;
    if (!target->can_counter()) return;
    if (!is_valid_counter_action(target)) return;
    auto action = std::make_shared<GameAction>(target);
    action->set_skill(s.counter_skill.id);
    action->set_counter();
    if (action->is_for_opponent()) action->set_target(sub->index());
    else action->set_target(target->index());
    s.counter_queue.push_back(action);
    if (!s.has_counter_original_subject) {
        s.counter_original_subject = BattlerRef::of(sub);
        s.has_counter_original_subject = true;
    }
    if (!s.counter_original_action) s.counter_original_action = s.action;
}

static void base_invoke_counter(GameBattler* sub, GameBattler* target) {
    State& s = g_state;
    auto action = std::make_shared<GameAction>(target);
    if (s.log) s.log->display_counter(target);
    action->set_attack();
    action->apply(sub);
    if (s.log) s.log->display_action_results(target, sub);
    if (sub->is_dead()) sub->perform_collapse();
}

void invoke_counter_attack(GameBattler* sub, GameBattler* target) {
    State& s = g_state;
    if (is_valid_counter_action(target)) s.counter_sequence += 1;
    if (!is_max_counter_queue()) {
        if (is_valid_counter_action(target)) target->pay_counter(1);
    }
    if (target->can_counter()) {
        ItemRef skill;
        bool found = get_counter_skill(sub, target, skill);
        s.has_counter_skill = found;
        s.counter_skill_null = found && !skill.valid() && skill.kind == ItemRef::None;
        s.counter_skill = skill;
        if (found && s.counter_skill_null) {
            if (s.action->is_physical() && target->can_move()) base_invoke_counter(sub, target);
            else invoke_normal_action(sub, target);
            return;
        } else if (evade_and_counter(sub, target)) {
            target->perform_evasion();
            target->force_evade_popup();
            add_counter_queue(sub, target);
        } else if (found) {
            invoke_normal_action(sub, target);
            add_counter_queue(sub, target);
        } else {
            if (is_valid_counter_action(target)) {
                target->pay_counter(-1);
                s.counter_sequence -= 1;
            }
            invoke_normal_action(sub, target);
            return;
        }
        if (s.log) s.log->display_action_results(target, sub);
        if (sub->is_dead()) sub->perform_collapse();
    } else {
        invoke_normal_action(sub, target);
    }
}

void invoke_magic_reflection(GameBattler* sub, GameBattler* target) {
    State& s = g_state;
    s.action->has_reflection_target = true;
    s.action->reflection_target = BattlerRef::of(target);
    if (s.log) s.log->display_reflection(target);
    s.action->apply(sub);
    if (s.log) s.log->display_action_results(target, sub);
    if (sub->is_dead()) sub->perform_collapse();
}

GameBattler* apply_substitute(GameBattler* target) {
    if (check_substitute(target)) {
        GameBattler* sub = target->friends_unit().substitute_battler();
        if (sub && target != sub) {
            if (g_state.log) g_state.log->display_substitute(sub, target);
            return sub;
        }
    }
    return target;
}

bool check_substitute(GameBattler* target) { return target->is_dying() && !g_state.action->is_certain_hit(); }

bool is_action_forced() { return g_state.has_action_forced_battler; }

static PreForce set_pre_force_action_settings() {
    State& s = g_state;
    PreForce p;
    p.subject = s.subject;
    p.has_subject = s.has_subject;
    p.action = s.action ? s.action->clone() : nullptr;
    p.action_list = s.action_list;
    p.targets = s.targets;
    p.all_targets = s.all_targets;
    p.ind_targets = s.individual_targets;
    p.phase_steps = s.phase_steps;
    p.return_phase = s.return_phase;
    p.phase = s.phase;
    p.condition_flags = s.condition_flags;
    p.true_flags = s.true_flags;
    return p;
}

static bool load_pre_force_action_settings() {
    State& s = g_state;
    if (s.force_action_queue.empty()) return false;
    PreForce p = s.force_action_queue.front();
    s.force_action_queue.erase(s.force_action_queue.begin());
    s.subject = p.subject;
    s.has_subject = p.has_subject;
    s.action = p.action;
    s.action_list = p.action_list;
    s.targets = p.targets;
    s.all_targets = p.all_targets;
    s.individual_targets = p.ind_targets;
    s.phase_steps = p.phase_steps;
    s.return_phase = p.return_phase;
    s.condition_flags = p.condition_flags;
    s.true_flags = p.true_flags;
    s.phase = p.phase;
    return subject() && subject()->is_appeared();
}

void force_action(GameBattler* battler) {
    State& s = g_state;
    if (subject()) subject()->clear_result();
    s.force_action_queue.push_back(set_pre_force_action_settings());
    s.action_forced_battler = BattlerRef::of(battler);
    s.has_action_forced_battler = true;
    for (size_t i = 0; i < s.action_battlers.size(); i++) {
        if (s.action_battlers[i].get() == battler) {
            s.action_battlers.erase(s.action_battlers.begin() + (long)i);
            break;
        }
    }
}

void process_forced_action() {
    State& s = g_state;
    if (s.has_action_forced_battler) {
        s.pre_force_phase = s.phase;
        s.processing_forced_action = true;
        s.turn_forced = true;
        s.subject = s.action_forced_battler;
        s.has_subject = true;
        s.has_action_forced_battler = false;
        start_action();
        if (subject()) subject()->remove_current_action();
    }
}

void abort() { g_state.phase = "aborting"; }

bool check_battle_end() {
    State& s = g_state;
    const std::string& p = s.phase;
    if (p == "actionList" || p == "actionTargetList" || p == "action" || p == "phaseChange") return false;
    if (game_troop().is_event_running()) return false;
    if (!s.phase.empty() && !s.phase_null) {
        if (check_abort()) return true;
        if (game_party().is_all_dead()) {
            process_defeat();
            return true;
        }
        if (game_troop().is_all_dead()) {
            process_victory();
            return true;
        }
    }
    return false;
}

bool check_abort() {
    State& s = g_state;
    if (game_party().is_empty() || is_aborting()) {
        audio_manager::play_system_sound(8);
        s.escaped = true;
        process_abort();
    }
    return false;
}

void process_victory() {
    State& s = g_state;
    if (s.log) s.log->clear();
    s.victory_phase = true;
    if (s.scene) s.scene->set_window_layer_hidden(false);
    game_party().remove_battle_states();
    game_party().perform_victory();
    play_victory_me();
    replay_bgm_and_bgs();
    make_rewards();
    display_victory_message();
    display_rewards();
    gain_rewards();
    end_battle(0);
}

bool process_escape() {
    State& s = g_state;
    game_party().perform_escape();
    audio_manager::play_system_sound(8);
    bool success = s.preemptive ? true : rng::next() < s.escape_ratio;
    if (success) {
        game_party().perform_escape_success();
        display_escape_success_message();
        s.escaped = true;
        process_abort();
    } else {
        display_escape_failure_message();
        s.escape_ratio += s.escape_fail_boost;
        game_party().clear_actions();
        start_turn();
    }
    return success;
}

void process_abort() {
    State& s = g_state;
    if (s.escaped && !s.escape_animation_played && s.escape_by_command) {
        s.phase = "escape_animation";
        game_temp().reserve_common_event(997);
        game_switches().set_value(1601, true);
        game_troop().interpreter->setup_reserved_common_event();
        audio_manager::fade_out_bgm(2);
        return;
    }
    game_party().remove_battle_states();
    game_party().remove_battle_states();
    replay_bgm_and_bgs();
    end_battle(1);
}

void process_defeat() {
    State& s = g_state;
    if (s.log) {
        s.log->push("waitFrames", {js::Val::num(30)});
        s.log->push("showGlobalDefeatMessage");
    }
    display_defeat_message();
    play_defeat_me();
    if (s.can_lose) replay_bgm_and_bgs();
    else audio_manager::stop_bgm();
    end_battle(2);
}

void end_battle(int result) {
    State& s = g_state;
    game_actors().actor(1)->remove_state(299);
    game_actors().actor(1)->remove_state(300);
    game_temp().expando.set(js::atom("_secondChance"), js::Val::boolean(false));
    refresh_status();
    for (GameActor* a : game_party().all_members()) a->expando.set(js::atom("_hasLeveled"), js::Val::boolean(false));
    s.hide_low_hp_overlay = true;
    if (!is_battle_same_of_map_bgm()) audio_manager::fade_out_bgm(2);
    audio_manager::fade_out_bgs(2);
    audio_manager::fade_out_danger_bgs(1);
    if (result == 0) game_party().process_state_eval("victory");
    else if (result == 1) game_party().process_state_eval("escape");
    else if (result == 2) game_party().process_state_eval("defeat");
    s.phase = "battleEnd";
    if (s.event_callback) s.event_callback(result);
    if (result == 0) game_system().on_battle_win();
    else if (s.escaped) game_system().on_battle_escape();
    for (GameActor* a : game_party().all_members()) a->use_victory_face = false;
}

void update_battle_end() {
    State& s = g_state;
    if (!s.escaped && game_party().is_all_dead()) {
        if (s.can_lose) {
            game_party().revive_battle_members();
            scene_manager::pop();
        } else {
            scenes::goto_gameover();
        }
    } else {
        scene_manager::pop();
    }
    s.phase.clear();
    s.phase_null = true;
}

void make_rewards() {
    State& s = g_state;
    s.rewards_gold = game_troop().gold_total();
    s.rewards_exp = game_troop().exp_total();
    s.rewards_items = game_troop().make_drop_items();
}

static std::string term(const char* key) { return data::system()["terms"]["messages"][key].string(); }

void display_victory_message() {
    if (!g_state.log) return;
    g_state.log->push("addText", {js::Val::str(js_format(term("victory"), {game_party().name()}))});
    g_state.log->push("wait");
    g_state.log->push("waitForInput");
}

void display_defeat_message() {
    if (!g_state.log) return;
    g_state.log->push("addText", {js::Val::str(js_format(term("defeat"), {game_party().name()}))});
    g_state.log->push("wait", {js::Val::num(60)});
}

void display_escape_success_message() {
    if (!g_state.log) return;
    g_state.log->push("addText", {js::Val::str(js_format(term("escapeStart"), {game_party().name()}))});
    g_state.log->push("wait");
}

void display_escape_failure_message() {
    if (g_state.log) {
        g_state.log->push("addText", {js::Val::str(js_format(term("escapeStart"), {game_party().name()}))});
        g_state.log->push("wait");
        g_state.log->push("addText", {js::Val::str("\\." + term("escapeFailure"))});
    }
    g_state.escape_by_command = false;
}

void display_rewards() {
    State& s = g_state;
    if (!s.log) return;
    if (s.rewards_exp > 0) {
        std::string text = js_format(term("obtainExp"), {js::number_to_string(s.rewards_exp), data::system()["terms"]["basic"][8].string()});
        s.log->push("addText", {js::Val::str("\\." + text)});
        s.log->push("wait");
        s.log->push("waitForInput");
    }
    if (s.rewards_gold > 0) {
        s.log->push("addText", {js::Val::str("\\." + js_format(term("obtainGold"), {js::number_to_string(s.rewards_gold)}))});
        s.log->push("wait");
        s.log->push("waitForInput");
    }
    if (!s.rewards_items.empty()) {
        game_message().new_page();
        for (auto& it : s.rewards_items) {
            s.log->push("addText", {js::Val::str(js_format(term("obtainItem"), {it.data()["name"].string()}))});
            s.log->push("wait");
        }
        s.log->push("waitForInput");
    }
}

void gain_rewards() {
    gain_exp();
    gain_gold();
    gain_drop_items();
}

void gain_exp() {
    State& s = g_state;
    for (GameActor* a : game_party().all_members()) a->gain_exp(s.rewards_exp);
    game_message().clear();
    for (GameActor* a : game_party().all_members()) {
        js::Val d = a->level_up_data;
        if (d.truthy() && s.log) {
            js::Vm& vm = js::vm();
            s.log->push("clear");
            js::Val se = js::make_object();
            vm.put(se, js::atom("name"), js::Val::str("BA_Happy"));
            vm.put(se, js::atom("volume"), js::Val::num(90));
            vm.put(se, js::atom("pitch"), js::Val::num(100));
            vm.put(se, js::atom("pan"), js::Val::num(0));
            s.log->push("playSE", {se});
            std::string lvl = vm.get(d, js::atom("level")).to_str();
            s.log->push("addText", {js::Val::str(js_format(data::system()["terms"]["messages"]["levelUp"].string(),
                                                           {a->name_, data::system()["terms"]["basic"][0].string(), lvl}))});
            s.log->push("wait");
            s.log->push("waitForInput");
            js::Val skills = vm.get(d, js::atom("skills"));
            if (skills.is_array()) {
                for (auto& sk : skills.o->items) {
                    s.log->push("addText", {js::Val::str(js_format(data::system()["terms"]["messages"]["obtainSkill"].string(),
                                                                   {vm.get(sk, js::atom("name")).to_str()}))});
                    s.log->push("wait");
                }
                if (!skills.o->items.empty()) s.log->push("waitForInput");
            }
        }
        a->level_up_data = js::Val();
    }
    if (s.log && s.log->last_method() != "" && s.log->last_method() != "waitForInput") s.log->push("waitForInput");
}

void gain_gold() { game_party().gain_gold(g_state.rewards_gold); }

void gain_drop_items() {
    for (auto& it : g_state.rewards_items) game_party().gain_item(it, 1);
}

bool is_dtb() { return to_lower(game_system().battle_system) == "dtb"; }
bool is_turn_based() { return is_dtb(); }
bool is_tick_based() { return !is_turn_based(); }
double tick_rate() { return 1; }
void force_selection() { g_state.force_selection = true; }
bool is_force_selection() { return g_state.force_selection; }
void reset_selection() { g_state.force_selection = false; }
void start_all_selection() { g_state.all_selection = true; }
bool is_all_selection() { return g_state.all_selection && is_inputting(); }
void stop_all_selection() { g_state.all_selection = false; }

bool time_based_states() {
    if (!party_in_battle()) return false;
    if (is_turn_based()) return false;
    return yanfly_param("BECTimeStates").truthy();
}

bool time_based_buffs() {
    if (!party_in_battle()) return false;
    if (is_turn_based()) return false;
    return yanfly_param("BECTimeBuffs").truthy();
}

void create_actions() {
    game_party().create_actions();
    game_troop().create_actions();
}

void clear_inputting_action() {
    auto a = inputting_action();
    if (a) a->clear();
}

void refresh_all_members() {
    game_party().refresh_members();
    game_troop().refresh_members();
}

void queue_force_action(GameBattler* user, int skill_id, const js::Val& target, bool clear_result) {
    int target_index;
    if (target.is_undef()) target_index = 0;
    else if (target.is_num()) target_index = (int)target.n;
    else if (GameBattler* tb = js::host_cast<GameBattler>(target)) target_index = tb->index();
    else target_index = 0;
    js::Val params = js::make_array({js::Val::num(user->is_enemy() ? 0 : 1),
                                     js::Val::num(user->is_actor() ? static_cast<GameActor*>(user)->actor_id() : user->index()), js::Val::num(skill_id),
                                     js::Val::num(target_index)});
    js::Val cmd = js::make_object();
    js::vm().put(cmd, js::atom("code"), js::Val::num(339));
    js::vm().put(cmd, js::atom("indent"), js::Val::num(0));
    js::vm().put(cmd, js::atom("parameters"), params);
    game_temp().force_action_queue(cmd);
    if (clear_result) clear_results();
    if (is_tick_based()) g_state.phase = "action";
}

void queue_force_script_call(const std::string& code) {
    js::Val cmd = js::make_object();
    js::vm().put(cmd, js::atom("code"), js::Val::num(355));
    js::vm().put(cmd, js::atom("indent"), js::Val::num(0));
    js::vm().put(cmd, js::atom("parameters"), js::make_array({js::Val::str(code)}));
    game_temp().force_action_queue(cmd);
    clear_results();
    if (is_tick_based()) g_state.phase = "action";
}

void force_custom_battle_log(GameBattler* target) {
    State& s = g_state;
    if (!s.log) return;
    if (target) {
        s.log->display_action_results(subject(), target);
        return;
    }
    for (GameBattler* t : all_targets()) s.log->display_action_results(subject(), t);
}

void add_text(const std::string& text, int wait) {
    State& s = g_state;
    if (!s.log) return;
    s.log->add_text(text);
    if (wait <= 0) return;
    if (!s.action_list.empty() && s.action_list.back().name == "WAIT") return;
    s.action_list.push_back(ActSeq{"WAIT", {std::to_string(wait)}});
}

void clear_results() {
    State& s = g_state;
    for (GameBattler* m : all_battle_members()) {
        if (m) m->clear_result();
    }
    s.all_targets.clear();
    s.targets.clear();
    s.has_target = false;
}

void push_action_list_wait(int frames) { g_state.action_list.push_back(ActSeq{"WAIT", {std::to_string(frames)}}); }

void log_push_line(const std::string& text) {
    if (g_state.log) g_state.log->push_line(text);
}

void update_phase() {
    State& s = g_state;
    std::string phase;
    if (!s.phase_steps.empty()) {
        phase = s.phase_steps.front();
        s.phase_steps.erase(s.phase_steps.begin());
    }
    if (!phase.empty()) {
        s.phase = "actionList";
        set_targets(resolve(s.all_targets));
        s.condition_flags.clear();
        s.true_flags.clear();
    }
    const Value& item = s.action ? s.action->item_data() : Value::null_value();
    if (phase == "setup") {
        s.return_phase = "setup";
        s.action_list = seq_list(item["setupActions"]);
    } else if (phase == "whole") {
        s.return_phase = "whole";
        s.action_list = seq_list(item["wholeActions"]);
    } else if (phase == "target") {
        s.return_phase = "target";
        s.phase = "actionTargetList";
        GameBattler* first = s.individual_targets.empty() ? nullptr : s.individual_targets[0].get();
        set_targets({first});
        s.action_list = seq_list(item["targetActions"]);
    } else if (phase == "follow") {
        s.return_phase = "follow";
        s.action_list = seq_list(item["followActions"]);
    } else if (phase == "finish") {
        s.return_phase = "finish";
        s.action_list = seq_list(item["finishActions"]);
        s.action_list.push_back(ActSeq{"START COUNTER PHASE", {}});
        static const int acs_skills[] = {47, 48, 50, 51, 53, 54, 55, 56, 57, 59, 60, 61, 87, 88, 90, 91, 93, 94, 97, 98, 99, 100, 101,
                                         127, 128, 130, 131, 133, 134, 137, 138, 139, 167, 170, 172, 173, 175, 176, 178, 179, 180};
        static const int acs_check[] = {46, 49, 52, 86, 89, 92, 126, 129, 132, 168, 171, 174};
        ItemRef it = s.action->item();
        auto remove_immortal = [&]() {
            for (size_t i = 0; i < s.action_list.size(); i++) {
                if (s.action_list[i].name.find("IMMORTAL") != std::string::npos) {
                    s.action_list.erase(s.action_list.begin() + (long)i);
                    break;
                }
            }
        };
        if (it.is_skill()) {
            if (std::find(std::begin(acs_skills), std::end(acs_skills), it.id) != std::end(acs_skills)) remove_immortal();
            if (std::find(std::begin(acs_check), std::end(acs_check), it.id) != std::end(acs_check)) {
                remove_immortal();
                s.action_list.push_back(ActSeq{"EVAL", {"$gameTroop.members().forEach(m => m.removeImmortal())"}});
                s.action_list.push_back(ActSeq{"WAIT FOR EFFECT", {}});
            }
        }
        js::Val* add = game_temp().expando.find(js::atom("_addToFinishActions"));
        if (add && add->truthy()) {
            for (auto& a : seq_list_js(*add)) s.action_list.push_back(a);
            game_temp().expando.set(js::atom("_addToFinishActions"), js::Val());
        }
    } else {
        end_action();
    }
}

static bool action_conditions_met(const ActSeq& seq) {
    State& s = g_state;
    int ci = (int)s.condition_flags.size() - 1;
    re::Groups g;
    if (re::search("ELSE[ ]IF[ ](.*)", "i", seq.name, &g)) {
        if (s.condition_flags.empty()) return false;
        if (s.condition_flags[(size_t)ci]) {
            s.condition_flags[(size_t)ci] = false;
            s.true_flags[(size_t)ci] = true;
        } else if (!s.condition_flags[(size_t)ci] && !s.true_flags[(size_t)ci]) {
            JsEval e = seq_eval();
            bool ok = false;
            bool r = eval_string(g[1], e, "ACTION SEQUENCE IF CONDITION ERROR", &ok).truthy();
            s.condition_flags[(size_t)ci] = ok && r;
            s.true_flags[(size_t)ci] = ok && r;
        }
        return false;
    }
    if (re::search("ELSE[ ]*(.*)", "i", seq.name, &g)) {
        if (s.condition_flags.empty()) return false;
        if (s.condition_flags[(size_t)ci]) {
            s.condition_flags[(size_t)ci] = false;
            s.true_flags[(size_t)ci] = true;
        } else if (!s.condition_flags[(size_t)ci] && !s.true_flags[(size_t)ci]) {
            s.condition_flags[(size_t)ci] = true;
            s.true_flags[(size_t)ci] = true;
        }
        return false;
    }
    if (to_upper(seq.name) == "END") {
        if (s.condition_flags.empty()) return false;
        s.condition_flags.pop_back();
        s.true_flags.pop_back();
        return false;
    }
    if (!s.condition_flags.empty()) return s.condition_flags[(size_t)ci];
    return true;
}

void update_action_list() {
    State& s = g_state;
    for (;;) {
        if (s.action_list.empty()) {
            s.phase = "phaseChange";
            break;
        }
        s.act_seq = s.action_list.front();
        s.action_list.erase(s.action_list.begin());
        if (!action_conditions_met(s.act_seq)) continue;
        if (!process_action_sequence_check(to_upper(s.act_seq.name), s.act_seq.args)) break;
    }
}

void update_action_target_list() {
    State& s = g_state;
    for (;;) {
        if (!s.action_list.empty()) {
            s.act_seq = s.action_list.front();
            s.action_list.erase(s.action_list.begin());
            if (!action_conditions_met(s.act_seq)) continue;
            if (!process_action_sequence_check(to_upper(s.act_seq.name), s.act_seq.args)) break;
        } else if (!s.individual_targets.empty()) {
            s.individual_targets.erase(s.individual_targets.begin());
            if (!s.individual_targets.empty()) {
                set_targets({s.individual_targets[0].get()});
                s.action_list = seq_list(s.action->item_data()["targetActions"]);
            } else {
                s.phase = "phaseChange";
                break;
            }
        } else {
            s.phase = "phaseChange";
            break;
        }
    }
}

static bool action_if_conditions() {
    State& s = g_state;
    re::Groups g;
    if (re::search("IF[ ](.*)", "i", s.act_seq.name, &g)) {
        JsEval e = seq_eval();
        auto ts = resolve(s.targets);
        bool critical = false;
        if (!ts.empty() && ts[0]) critical = ts[0]->result().critical;
        e.set("critical", js::Val::boolean(critical));
        bool ok = false;
        bool r = eval_string(g[1], e, "ACTION SEQUENCE IF CONDITION ERROR", &ok).truthy();
        s.condition_flags.push_back(ok && r);
        s.true_flags.push_back(false);
    }
    return true;
}

bool process_action_sequence_check(const std::string& name, const std::vector<std::string>& args) {
    if (re::search("IF[ ](.*)", "i", name)) return action_if_conditions();
    return process_action_sequence(name, args);
}

std::vector<GameBattler*> make_action_targets(const std::string& name) {
    State& s = g_state;
    std::string str = to_upper(name);
    std::vector<GameBattler*> targets;
    GameBattler* sub = subject();
    auto add_appeared = [&](const std::vector<GameBattler*>& group, bool require_appeared, GameBattler* exclude) {
        for (GameBattler* t : group) {
            if (!t) continue;
            if (exclude && t == exclude) continue;
            if (require_appeared && !t->is_appeared()) continue;
            targets.push_back(t);
        }
        return targets;
    };
    if (in_list(str, {"SUBJECT", "USER"})) return {sub};
    if (in_list(str, {"TARGET", "TARGETS"})) return add_appeared(resolve(s.targets), true, nullptr);
    if (in_list(str, {"ACTORS", "EXISTING ACTORS", "ALIVE ACTORS"})) return add_appeared(game_party().alive_members(), true, nullptr);
    if (in_list(str, {"ACTORS ALL", "ALL ACTORS", "PARTY"})) {
        std::vector<GameBattler*> g;
        for (GameActor* a : game_party().battle_members()) g.push_back(a);
        return add_appeared(g, true, nullptr);
    }
    if (in_list(str, {"DEAD ACTORS", "DEAD ACTOR"})) return add_appeared(game_party().dead_members(), false, nullptr);
    if (in_list(str, {"ACTORS NOT USER", "ACTORS NOT SUBJECT"})) return add_appeared(game_party().alive_members(), true, sub);
    if (in_list(str, {"ENEMIES", "EXISTING ENEMIES", "ALIVE ENEMIES", "TROOP", "TROOPS"})) return add_appeared(game_troop().alive_members(), true, nullptr);
    if (in_list(str, {"ENEMIES ALL", "ALL ENEMIES"})) return add_appeared(game_troop().unit_members(), true, nullptr);
    if (in_list(str, {"DEAD ENEMIES", "DEAD ENEMY"})) return add_appeared(game_troop().dead_members(), false, nullptr);
    if (in_list(str, {"ENEMIES NOT USER", "ENEMIES NOT SUBJECT", "TROOP NOT USER", "TROOP NOT SUBJECT"}))
        return add_appeared(game_troop().alive_members(), true, sub);
    re::Groups g;
    if (re::search("ACTOR[ ](\\d+)", "i", str, &g)) {
        auto bm = game_party().battle_members();
        int i = g.num(1);
        GameBattler* t = i < (int)bm.size() ? bm[(size_t)i] : nullptr;
        if (t && t->is_appeared()) return {t};
    }
    if (re::search("ENEMY[ ](\\d+)", "i", str, &g)) {
        auto m = game_troop().unit_members();
        int i = g.num(1);
        GameBattler* t = i < (int)m.size() ? m[(size_t)i] : nullptr;
        if (t && t->is_appeared()) return {t};
    }
    GameAction* action = s.action.get();
    if (in_list(str, {"FRIEND", "FRIENDS", "ALLIES"})) return add_appeared(action->friends_unit().alive_members(), true, nullptr);
    if (in_list(str, {"ALL FRIENDS", "ALL ALLIES"})) return add_appeared(action->friends_unit().unit_members(), true, nullptr);
    if (in_list(str, {"DEAD FRIEND", "DEAD FRIENDS", "DEAD ALLIES"})) return add_appeared(action->friends_unit().dead_members(), true, nullptr);
    if (in_list(str, {"OPPONENT", "OPPONENTS", "RIVALS", "FOES"})) return add_appeared(action->opponents_unit().alive_members(), true, nullptr);
    if (in_list(str, {"ALL OPPONENTS", "ALL RIVALS", "ALL FOES"})) return add_appeared(action->opponents_unit().unit_members(), true, nullptr);
    if (in_list(str, {"DEAD OPPONENT", "DEAD OPPONENTS", "DEAD RIVALS", "DEAD FOES"})) return add_appeared(action->opponents_unit().dead_members(), false, nullptr);
    if (in_list(str, {"FRIENDS NOT USER", "ALLIES NOT USER"})) return add_appeared(action->friends_unit().alive_members(), true, sub);
    if (re::search("(?:FRIEND|ALLY)[ ](\\d+)", "i", str, &g)) {
        auto m = action->friends_unit().unit_members();
        int i = g.num(1);
        GameBattler* t = i < (int)m.size() ? m[(size_t)i] : nullptr;
        if (t && t->is_appeared()) return {t};
    }
    if (re::search("(?:OPPONENT|FOE|RIVAL)[ ](\\d+)", "i", str, &g)) {
        auto m = action->opponents_unit().unit_members();
        int i = g.num(1);
        GameBattler* t = i < (int)m.size() ? m[(size_t)i] : nullptr;
        if (t && t->is_appeared()) return {t};
    }
    auto concat = [](std::vector<GameBattler*> a, const std::vector<GameBattler*>& b) {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    };
    if (str == "ALL ALIVE") return add_appeared(concat(action->friends_unit().alive_members(), action->opponents_unit().alive_members()), true, nullptr);
    if (str == "ALL MEMBERS") return add_appeared(concat(action->friends_unit().unit_members(), action->opponents_unit().unit_members()), true, nullptr);
    if (str == "ALL DEAD") return add_appeared(concat(action->friends_unit().dead_members(), action->opponents_unit().dead_members()), false, nullptr);
    if (str == "ALL NOT USER") return add_appeared(concat(action->friends_unit().alive_members(), action->opponents_unit().alive_members()), true, sub);
    if (in_list(str, {"FOCUS", "PARTICIPANTS"})) {
        add_appeared(resolve(s.targets), true, nullptr);
        if (!contains(targets, sub)) targets.push_back(sub);
        return targets;
    }
    if (in_list(str, {"NOT FOCUS", "NONPARTICIPANTS"})) {
        auto ts = resolve(s.targets);
        for (GameBattler* t : concat(action->friends_unit().unit_members(), action->opponents_unit().unit_members())) {
            if (!t) continue;
            if (t == sub) continue;
            if (t->is_hidden()) continue;
            if (contains(ts, t)) continue;
            if (t->is_dead()) {
                if (t->is_enemy()) {
                    GameEnemy* e = static_cast<GameEnemy*>(t);
                    if (!(e->has_sv_battler() && !e->enemy()["sideviewCollapse"].truthy())) continue;
                } else if (!t->is_actor()) {
                    continue;
                }
            }
            targets.push_back(t);
        }
        return targets;
    }
    if (re::search("(?:CHAR|CHARA|CHARACTER)[ ](\\d+)", "i", str, &g)) {
        GameActor* a = game_actors().actor(g.num(1));
        auto bm = game_party().battle_members();
        if (a && std::find(bm.begin(), bm.end(), a) != bm.end()) return {a};
    }
    if (str == "FIRST") {
        auto ts = resolve(s.targets);
        return {ts.empty() ? nullptr : ts[0]};
    }
    return targets;
}

static bool action_action_animation(const std::vector<std::string>& args) {
    State& s = g_state;
    std::vector<GameBattler*> ts = !arg(args, 0).empty() ? make_action_targets(args[0]) : resolve(s.targets);
    bool mirror = to_upper(arg(args, 1)) == "MIRROR";
    auto group = unique(ts);
    int ani = s.action->item_data()["animationId"].as_int();
    if (!s.log) return true;
    if (ani < 0) {
        if (mirror) s.log->show_actor_atk_ani_mirror(subject(), group);
        else s.log->show_attack_animation(subject(), group);
    } else {
        s.log->show_normal_animation(group, ani, mirror);
    }
    return true;
}

static bool action_action_effect(const std::vector<std::string>& args) {
    State& s = g_state;
    std::vector<GameBattler*> ts = !arg(args, 0).empty() ? make_action_targets(args[0]) : resolve(s.targets);
    for (GameBattler* t : ts) {
        if (!t) continue;
        bool already = t->is_dead();
        invoke_action(subject(), t);
        if (t->is_dead() && !already) t->perform_collapse();
    }
    return true;
}

static bool action_immortal(const std::vector<std::string>& args) {
    auto ts = unique(make_action_targets(arg(args, 0)));
    JsEval e{js::Val()};
    bool ok = false;
    bool value = eval_string(to_lower(args.size() > 1 ? args[1] : "undefined"), e, "IMMORTAL", &ok).truthy() && ok;
    for (GameBattler* t : ts) {
        if (!t) continue;
        if (value) t->add_immortal();
        else t->remove_immortal();
    }
    return true;
}

static bool action_buff(const std::string& name, const std::vector<std::string>& args, bool add, bool debuff) {
    State& s = g_state;
    auto ts = make_action_targets(arg(args, 0));
    if (ts.empty()) return false;
    bool show = has_show(args);
    re::Groups g;
    int pid;
    std::string pat = std::string(add ? "ADD" : "REMOVE") + "[ ](.*)[ ]" + (debuff ? "DEBUFF" : "BUFF");
    if (re::search(pat.c_str(), "i", name, &g)) pid = param_id_of(to_upper(g[1]));
    else return true;
    double turns = 5;
    if (add) {
        double t = js_parse_int(arg(args, 1));
        if (!arg(args, 1).empty() && t > 0) turns = t;
    }
    if (pid < 0) return true;
    for (GameBattler* t : ts) {
        if (add) {
            if (debuff) t->add_debuff(pid, turns);
            else t->add_buff(pid, turns);
            if (show && s.log) s.log->display_action_results(subject(), t);
        } else if (debuff ? t->is_debuff_affected(pid) : t->is_buff_affected(pid)) {
            t->remove_buff(pid);
            if (show && s.log) s.log->display_action_results(subject(), t);
        }
    }
    return true;
}

static std::vector<int> state_list(const std::string& name, const char* pat) {
    std::vector<int> out;
    re::Groups g;
    if (!re::search(pat, "i", name, &g)) return out;
    for (auto& d : re::all("\\d+", "g", g[1])) out.push_back(std::atoi(d.c_str()));
    return out;
}

static bool action_add_state(const std::string& name, const std::vector<std::string>& args) {
    State& s = g_state;
    auto ts = make_action_targets(arg(args, 0));
    if (ts.empty()) return false;
    bool show = has_show(args);
    if (!re::search("(?:ADD_STATE|ADD STATE)[ ](\\d+(?:\\s*,\\s*\\d+)*)", "i", name)) return true;
    auto states = state_list(name, "(?:ADD_STATE|ADD STATE)[ ](\\d+(?:\\s*,\\s*\\d+)*)");
    for (GameBattler* t : ts) {
        for (int id : states) {
            if (id == t->death_state_id()) {
                js::Val* prev = t->expando.find(js::atom("_prevImmortalState"));
                if (prev && prev->is_bool() && !prev->b) t->remove_immortal();
            }
            t->add_state(id);
            if (show && s.log) s.log->display_action_results(subject(), t);
        }
    }
    return true;
}

static bool action_remove_state(const std::string& name, const std::vector<std::string>& args) {
    State& s = g_state;
    auto ts = make_action_targets(arg(args, 0));
    if (ts.empty()) return false;
    bool show = has_show(args);
    if (!re::search("(?:REMOVE_STATE|REMOVE STATE)[ ](\\d+(?:\\s*,\\s*\\d+)*)", "i", name)) return true;
    auto states = state_list(name, "(?:REMOVE_STATE|REMOVE STATE)[ ](\\d+(?:\\s*,\\s*\\d+)*)");
    for (GameBattler* t : ts) {
        for (int id : states) {
            if (t->is_state_affected(id)) {
                t->remove_state(id);
                if (show && s.log) s.log->display_action_results(subject(), t);
            }
        }
    }
    return true;
}

static bool action_sound(const char* kind, const std::vector<std::string>& args) {
    State& s = g_state;
    if (args.empty()) return true;
    std::string a0 = to_upper(args[0]);
    std::string k = kind;
    if (a0 == "STOP") {
        if (k == "bgm") audio_manager::stop_bgm();
        else if (k == "bgs") audio_manager::stop_bgs();
        else audio_manager::stop_me();
        return true;
    }
    if (k != "me" && a0 == "MEMORIZE") {
        if (k == "bgm") {
            s.memorized_bgm = audio_manager::save_bgm();
            s.has_memorized_bgm = true;
        } else {
            s.memorized_bgs = audio_manager::save_bgs();
            s.has_memorized_bgs = true;
        }
        return true;
    }
    if (k != "me" && a0 == "MEMORY") {
        if (k == "bgm" && s.has_memorized_bgm) audio_manager::replay_bgm(s.memorized_bgm);
        if (k == "bgs" && s.has_memorized_bgs) audio_manager::replay_bgs(s.memorized_bgs);
        return true;
    }
    if (args[0].empty()) return true;
    AudioObj a = audio_from_args(args);
    if (k == "bgm") audio_manager::play_bgm(a);
    else if (k == "bgs") audio_manager::play_bgs(a);
    else audio_manager::play_me(a);
    return true;
}

static bool action_se(const std::vector<std::string>& args) {
    if (args.empty()) return true;
    static const struct {
        const char* name;
        int id;
    } table[] = {{"PLAY CURSOR", 0},       {"PLAY OK", 1},          {"PLAY CANCEL", 2},          {"PLAY BUZZER", 3},          {"PLAY EQUIP", 4},
                 {"PLAY SAVE", 5},         {"PLAY LOAD", 6},        {"PLAY BATTLE START", 7},    {"PLAY ESCAPE", 8},          {"PLAY ENEMY ATTACK", 9},
                 {"PLAY ENEMY DAMAGE", 10}, {"PLAY ENEMY COLLAPSE", 11}, {"PLAY BOSS COLLAPSE 1", 12}, {"PLAY BOSS COLLAPSE 2", 13}, {"PLAY ACTOR DAMAGE", 14},
                 {"PLAY ACTOR COLLAPSE", 15}, {"PLAY RECOVERY", 16},   {"PLAY MISS", 17},           {"PLAY EVASION", 18},        {"PLAY MAGIC EVASION", 19},
                 {"PLAY REFLECTION", 20},   {"PLAY SHOP", 21},       {"PLAY USE ITEM", 22},       {"PLAY USE SKILL", 23}};
    std::string a0 = to_upper(args[0]);
    for (auto& t : table) {
        if (a0 == t.name) {
            audio_manager::play_system_sound(t.id);
            return true;
        }
    }
    if (args[0].empty()) return true;
    audio_manager::play_se(audio_from_args(args));
    return true;
}

static bool action_point_modify(const std::string& name, const std::vector<std::string>& args, const char* stat) {
    State& s = g_state;
    auto ts = make_action_targets(arg(args, 0));
    if (ts.empty()) return false;
    std::string st = stat;
    re::Groups g;
    double change;
    bool percent;
    if (re::search((st + "[ ]([+-])(?:VARIABLE|VAR)[ ](\\d+)").c_str(), "i", name, &g)) {
        change = js_parse_int(game_variables().value(g.num(2)).to_str());
        if (g[1] == "-") change *= -1;
        percent = false;
    } else if (re::search((st + "[ ]([\\+\\-]\\d+)([%\xEF\xBC\x85])").c_str(), "i", name, &g)) {
        change = js_parse_int(g[1]);
        percent = true;
    } else if (re::search((st + "[ ]([\\+\\-]\\d+)").c_str(), "i", name, &g)) {
        change = js_parse_int(g[1]);
        percent = false;
    } else {
        return false;
    }
    bool show = has_show(args);
    for (GameBattler* t : ts) {
        t->clear_result();
        double base = st == "HP" ? t->mhp() : st == "MP" ? t->mmp() : t->max_tp();
        double value = percent ? base * change * 0.01 : change;
        double iv = js_parse_int(js::number_to_string(value));
        if (st == "HP") t->gain_hp(iv);
        else if (st == "MP") t->gain_mp(iv);
        else t->gain_tp(iv);
        if (show) {
            t->start_damage_popup();
            if (s.log) s.log->display_action_results(subject(), t);
        }
    }
    return true;
}

static bool action_eval(const std::vector<std::string>& args) {
    if (args.empty()) return true;
    std::string text = args[0];
    for (size_t i = 1; i < args.size(); ++i) text += ", " + args[i];
    JsEval e = seq_eval();
    eval_string(text, e, "ACTION SEQUENCE EVAL ERROR");
    return false;
}

static bool action_change_switch(const std::string& name, const std::vector<std::string>& args) {
    re::Groups g;
    std::vector<int> switches;
    if (re::search("SWITCH[ ](\\d+)", "i", name, &g)) switches = {g.num(1)};
    else if (re::search("SWITCH[ ](\\d+)..(\\d+)", "i", name, &g) || re::search("SWITCH[ ](\\d+)[ ]TO[ ](\\d+)", "i", name, &g)) {
        script_error("ACTION SEQUENCE", "ReferenceError: getRange is not defined");
        return true;
    } else {
        return true;
    }
    std::string result = to_upper(arg(args, 0));
    int mode = -1;
    bool value = false;
    if (result == "ON" || result == "TRUE") value = true, mode = 0;
    else if (result == "OFF" || result == "FALSE") value = false, mode = 0;
    else if (result == "TOGGLE" || result == "OPPOSITE" || result == "REVERSE") mode = 1;
    else if (re::search("SWITCH[ ](\\d+)", "i", result, &g)) value = game_switches().value(g.num(1)), mode = 0;
    for (int id : switches) {
        if (mode == 1) game_switches().set_value(id, !game_switches().value(id));
        else game_switches().set_value(id, mode == 0 ? value : false);
    }
    return true;
}

static bool action_change_variable() {
    State& s = g_state;
    re::Groups g;
    int main_var;
    std::string op;
    js::Val edit;
    JsEval e = seq_eval();
    if (re::search("CHANGE[ ](?:VARIABLE|VAR)[ ](\\d+)[ ](.*)[ ](?:VARIABLE|VAR)[ ](\\d+)", "i", s.act_seq.name, &g)) {
        main_var = g.num(1);
        op = g[2];
        edit = game_variables().value(g.num(3));
    } else if (re::search("CHANGE[ ](?:VARIABLE|VAR)[ ](\\d+)[ ](.*?)[ ](.*)", "i", s.act_seq.name, &g)) {
        main_var = g.num(1);
        op = g[2];
        edit = eval_string(g[3], e, "ACTION SEQUENCE CHANGE VARIABLE ERROR");
    } else {
        return true;
    }
    js::Val main_value = game_variables().value(main_var);
    js::Val v = edit.is_str() ? eval_string(edit.to_str(), e, "ACTION SEQUENCE CHANGE VARIABLE ERROR") : edit;
    js::Vm& vm = js::vm();
    if (op == "=") game_variables().set_value(main_var, v);
    else if (op == "+=" || op == "+") game_variables().set_value(main_var, vm.binop(JB_ADD, main_value, v));
    else if (op == "-=" || op == "-") game_variables().set_value(main_var, vm.binop(JB_SUB, main_value, v));
    else if (op == "*=" || op == "*") game_variables().set_value(main_var, vm.binop(JB_MUL, main_value, v));
    else if (op == "/=" || op == "/") game_variables().set_value(main_var, vm.binop(JB_DIV, main_value, v));
    else if (op == "%=" || op == "%") game_variables().set_value(main_var, vm.binop(JB_MOD, main_value, v));
    return true;
}

static bool action_gain_item(const std::string& name, const std::vector<std::string>& args) {
    re::Groups g;
    bool gain;
    std::string type;
    int id;
    if (re::search("GAIN[ ](.*)[ ](\\d+)", "i", name, &g)) {
        gain = true;
        type = to_upper(g[1]);
        id = g.num(2);
    } else if (re::search("LOSE[ ](.*)[ ](\\d+)", "i", name, &g)) {
        gain = false;
        type = to_upper(g[1]);
        id = g.num(2);
    } else {
        return true;
    }
    ItemRef item;
    if (type == "ITEM") item = ItemRef::item(id);
    else if (type == "WPN" || type == "WEAPON") item = ItemRef::weapon(id);
    else if (type == "ARM" || type == "ARMOR" || type == "ARMOUR") item = ItemRef::armor(id);
    else return true;
    double amount = std::max(1.0, js_parse_int(arg(args, 0)));
    if (std::isnan(amount)) amount = 1;
    if (gain) game_party().gain_item(item, amount);
    else game_party().lose_item(item, amount);
    return true;
}

static bool action_collapse(const std::vector<std::string>& args) {
    auto ts = make_action_targets(arg(args, 0));
    bool force = to_upper(arg(args, 1)) == "FORCE";
    for (GameBattler* t : ts) {
        if (!t) continue;
        if (force) {
            t->remove_immortal();
            t->add_state(t->death_state_id());
        }
        if (t->is_death_state_affected()) t->perform_collapse();
    }
    return false;
}

static bool action_common_event(int id) {
    if (game_troop().is_event_running()) {
        const Value& ev = data::common_events()[id];
        if (!ev.is_obj()) return false;
        game_troop().interpreter->setup_child(&ev["list"], 0, data::keep());
    } else {
        game_temp().reserve_common_event(id);
    }
    return false;
}

static bool action_motion_target(std::string name, const std::vector<std::string>& args) {
    State& s = g_state;
    if (to_upper(name) == "WAIT") {
        auto ts = make_action_targets(arg(args, 0));
        if (!ts.empty() && ts[0]->is_actor() && static_cast<GameActor*>(ts[0])->is_sprite_visible()) {
            if (s.log) s.log->wait_count() += 12;
            return false;
        }
        return true;
    }
    if (to_upper(name) == "STANDBY") name = "WAIT";
    auto movers = make_action_targets(arg(args, 0));
    if (movers.empty()) return true;
    std::string cmd = to_lower(name);
    std::string motion = "wait";
    bool show_weapon = to_upper(arg(args, 1)) != "NO WEAPON";
    if (in_list(cmd, {"wait", "chant", "guard", "evade", "skill", "spell", "item", "escape", "victory", "dying", "abnormal", "sleep", "dead"})) motion = cmd;
    else if (in_list(cmd, {"walk", "move"})) motion = "walk";
    else if (in_list(cmd, {"damage", "hit"})) motion = "damage";
    else if (cmd == "attack") {
        for (GameBattler* m : movers) m->perform_attack();
        return false;
    } else if (cmd == "randattack") {
        static const char* motions[] = {"thrust", "swing", "missile"};
        for (GameBattler* m : movers) m->force_motion(motions[(size_t)std::floor(rng::next() * 3)]);
        return false;
    } else if (in_list(cmd, {"thrust", "swing", "missile"})) {
        for (GameBattler* m : movers) {
            m->force_motion(cmd);
            int wtype = 0;
            if (m->is_actor() && show_weapon) {
                auto w = static_cast<GameActor*>(m)->weapons();
                wtype = w.empty() ? 0 : w[0].data()["wtypeId"].as_int();
                const Value& am = data::system()["attackMotions"][wtype];
                if (am.is_obj() && am["type"].as_int() >= 0 && am["type"].as_int() <= 2) m->start_weapon_animation(am["weaponImageId"].as_int());
            }
            if (m->is_enemy() && static_cast<GameEnemy*>(m)->has_sv_battler() && show_weapon) m->start_weapon_animation(static_cast<GameEnemy*>(m)->weapon_image_id());
        }
        return false;
    }
    for (GameBattler* m : movers) m->force_motion(motion);
    return false;
}

static bool action_opacity(const std::string& name, const std::vector<std::string>& args) {
    auto ts = make_action_targets(name);
    if (ts.empty()) return true;
    std::string cmd = arg(args, 0);
    double frames = js_or(arg(args, 1), 12);
    re::Groups g;
    double opacity;
    if (re::search("(\\d+)([%\xEF\xBC\x85])", "i", cmd, &g)) opacity = std::min(std::max(js_parse_int(js::number_to_string(js_number(g[1]) * 0.01 * 255)), 0.0), 255.0);
    else if (re::search("(\\d+)", "i", cmd, &g)) opacity = js_parse_int(g[1]);
    else return false;
    for (GameBattler* t : ts) t->sprite_opacity(opacity, frames);
    return false;
}

static bool action_flash_screen(const std::vector<std::string>& args) {
    std::string c = to_upper(arg(args, 0));
    double flash[4];
    double frames;
    auto set = [&](double r, double g, double b) {
        flash[0] = r;
        flash[1] = g;
        flash[2] = b;
        flash[3] = 255;
        frames = js_or(arg(args, 1), 60);
    };
    if (c == "WHITE") set(255, 255, 255);
    else if (c == "RED") set(255, 0, 0);
    else if (c == "ORANGE") set(255, 128, 0);
    else if (c == "YELLOW") set(255, 255, 0);
    else if (c == "GREEN") set(0, 255, 0);
    else if (c == "BLUE") set(0, 128, 255);
    else if (c == "PURPLE") set(128, 64, 255);
    else if (c == "MAGENTA") set(255, 0, 255);
    else if (c == "BLACK") set(0, 0, 0);
    else {
        for (int i = 0; i < 4; i++) flash[i] = js_parse_int(arg(args, (size_t)i).empty() ? "0" : args[(size_t)i]);
        frames = js_or(arg(args, 4), 60);
    }
    game_screen().start_flash(flash, (int)frames);
    return false;
}

static bool action_tint_screen(const std::vector<std::string>& args) {
    std::string c = to_upper(arg(args, 0));
    double tint[4];
    double frames;
    auto set = [&](double r, double g, double b, double gr) {
        tint[0] = r;
        tint[1] = g;
        tint[2] = b;
        tint[3] = gr;
        frames = js_or(arg(args, 1), 60);
    };
    if (c == "NORMAL") set(0, 0, 0, 0);
    else if (c == "DARK") set(-68, -68, -68, 0);
    else if (c == "SEPIA") set(34, -34, -68, 170);
    else if (c == "SUNSET") set(68, -34, -34, 0);
    else if (c == "NIGHT") set(68, -68, 0, 68);
    else {
        for (int i = 0; i < 4; i++) tint[i] = js_parse_int(arg(args, (size_t)i).empty() ? "0" : args[(size_t)i]);
        frames = js_or(arg(args, 4), 60);
    }
    game_screen().start_tint(tint, (int)frames);
    return false;
}

static double rate_from(const std::string& a, bool& ok) {
    re::Groups g;
    ok = true;
    if (re::search("(?:VARIABLE|VAR)[ ](\\d+)", "i", a, &g)) return js_parse_float(js::number_to_string(game_variables().number(g.num(1)) * 0.01));
    if (re::search("(\\d+)([%\xEF\xBC\x85])", "i", a, &g)) return js_number(g[1]) * 0.01;
    if (re::search("(\\d+).(\\d+)", "i", a, &g)) return js_parse_float(g[1] + "." + g[1]);
    ok = false;
    return 0;
}

static double flat_from(const std::string& a, bool& ok) {
    re::Groups g;
    ok = true;
    if (re::search("(?:VARIABLE|VAR)[ ](\\d+)", "i", a, &g)) return js_parse_int(game_variables().value(g.num(1)).to_str());
    if (re::search("([\\+\\-]\\d+)", "i", a, &g)) return js_parse_int(g[1]);
    if (re::search("(\\d+)", "i", a, &g)) return js_parse_int(g[1]);
    ok = false;
    return 0;
}

static bool camera_enabled() { return config().battle_camera; }

static double eval_num(const std::string& code, double def) {
    if (code.empty()) return def;
    JsEval e = seq_eval();
    js::Val v = eval_string(code, e, "ACTION SEQUENCE CAMERA ERROR");
    double n = v.to_num();
    return (std::isnan(n) || n == 0) ? def : n;
}

static bool action_camera_focus(const std::vector<std::string>& args) {
    State& s = g_state;
    if (!camera_enabled()) return true;
    s.camera_focus_group = refs_of(make_action_targets(arg(args, 0)));
    if (s.camera_focus_group.empty()) return false;
    std::string type = args.size() > 1 && !args[1].empty() ? to_upper(args[1]) : "CENTER";
    double frames = js_or(arg(args, 2), 30);
    auto set = [&](const char* x, const char* y) {
        s.camera_focus_pos_x = x;
        s.camera_focus_pos_y = y;
    };
    if (in_list(type, {"FRONT BASE", "FRONT FOOT", "FRONT FEET"})) set("FRONT", "BASE");
    else if (in_list(type, {"BASE", "FOOT", "FEET"})) set("MIDDLE", "BASE");
    else if (in_list(type, {"BACK BASE", "BACK FOOT", "BACK FEET"})) set("BACK", "BASE");
    else if (in_list(type, {"FRONT CENTER", "FRONT MIDDLE", "FRONT"})) set("FRONT", "MIDDLE");
    else if (in_list(type, {"CENTER", "MIDDLE"})) set("MIDDLE", "MIDDLE");
    else if (in_list(type, {"BACK CENTER", "BACK MIDDLE", "BACK"})) set("BACK", "MIDDLE");
    else if (in_list(type, {"FRONT HEAD", "FRONT TOP"})) set("FRONT", "TOP");
    else if (in_list(type, {"HEAD", "TOP"})) set("MIDDLE", "TOP");
    else if (in_list(type, {"BACK HEAD", "BACK TOP"})) set("BACK", "TOP");
    else set("MIDDLE", "MIDDLE");
    game_screen().set_camera_duration((int)frames);
    return true;
}

static bool action_camera_offset(const std::vector<std::string>& args) {
    State& s = g_state;
    if (!camera_enabled()) return true;
    std::string cmd = to_upper(arg(args, 0));
    if (cmd == "LEFT") s.camera_offset_x = eval_num("-1 * (" + arg(args, 1) + ")", 100);
    else if (cmd == "RIGHT") s.camera_offset_x = eval_num(arg(args, 1), 100);
    else if (cmd == "UP") s.camera_offset_y = eval_num("-1 * (" + arg(args, 1) + ")", 100);
    else if (cmd == "DOWN") s.camera_offset_y = eval_num(arg(args, 1), 100);
    return true;
}

static bool action_camera_pan(const std::vector<std::string>& args) {
    State& s = g_state;
    if (!camera_enabled()) return true;
    std::string cmd = to_upper(arg(args, 0));
    double frames = 30;
    if (cmd == "LEFT") s.camera_x -= eval_num(arg(args, 1), 100), frames = js_or(arg(args, 2), 30);
    else if (cmd == "RIGHT") s.camera_x += eval_num(arg(args, 1), 100), frames = js_or(arg(args, 2), 30);
    else if (cmd == "UP") s.camera_y -= eval_num(arg(args, 1), 100), frames = js_or(arg(args, 2), 30);
    else if (cmd == "DOWN") s.camera_y += eval_num(arg(args, 1), 100), frames = js_or(arg(args, 2), 30);
    game_screen().set_camera_duration((int)frames);
    return true;
}

static bool action_camera_screen(const std::vector<std::string>& args) {
    State& s = g_state;
    if (!camera_enabled()) return true;
    std::string cmd = to_upper(arg(args, 0));
    double frames = 30;
    const double W = 640, H = 480;
    auto at = [&](double x, double y) {
        s.camera_x = x;
        s.camera_y = y;
        frames = js_or(arg(args, 1), 30);
    };
    if (in_list(cmd, {"TOP LEFT", "UPPER LEFT"})) at(0, 0);
    else if (in_list(cmd, {"FAR LEFT", "ABSOLUTE LEFT"})) at(0, H / 2);
    else if (in_list(cmd, {"BOTTOM LEFT", "LOWER LEFT"})) at(0, H);
    else if (in_list(cmd, {"TOP CENTER", "UPPER CENTER"})) at(W / 2, 0);
    else if (in_list(cmd, {"CENTER", "MIDDLE"})) at(W / 2, H / 2);
    else if (in_list(cmd, {"BOTTOM CENTER", "LOWER CENTER"})) at(W / 2, H);
    else if (in_list(cmd, {"TOP RIGHT", "UPPER RIGHT"})) at(W, 0);
    else if (in_list(cmd, {"FAR RIGHT", "ABSOLUTE RIGHT"})) at(W, H / 2);
    else if (in_list(cmd, {"BOTTOM RIGHT", "LOWER RIGHT"})) at(W, H);
    else if (in_list(cmd, {"POINT", "POSITION", "COORDINATE", "SCREEN", "SCREEN POS", "COORDINATES"})) {
        s.camera_x = eval_num(arg(args, 1), 0);
        s.camera_y = eval_num(arg(args, 2), 0);
        frames = js_or(arg(args, 3), 30);
    } else {
        auto ts = make_action_targets(arg(args, 0));
        if (ts.empty()) return false;
        std::string type = to_upper(arg(args, 1));
        frames = js_or(arg(args, 2), 30);
        auto pos = [&](const char* x, const char* y) {
            s.camera_x = target_pos_x(ts, x);
            s.camera_y = target_pos_y(ts, y);
        };
        if (in_list(type, {"FRONT BASE", "FRONT FOOT", "FRONT FEET", "FRONT"})) pos("FRONT", "BASE");
        else if (in_list(type, {"BASE", "FOOT", "FEET"})) pos("MIDDLE", "BASE");
        else if (in_list(type, {"BACK BASE", "BACK FOOT", "BACK FEET", "BACK"})) pos("BACK", "BASE");
        else if (in_list(type, {"FRONT CENTER", "FRONT MIDDLE"})) pos("FRONT", "MIDDLE");
        else if (in_list(type, {"CENTER", "MIDDLE"})) pos("MIDDLE", "MIDDLE");
        else if (in_list(type, {"BACK CENTER", "BACK MIDDLE"})) pos("BACK", "MIDDLE");
        else if (in_list(type, {"FRONT HEAD", "FRONT TOP"})) pos("FRONT", "TOP");
        else if (in_list(type, {"HEAD", "TOP"})) pos("MIDDLE", "TOP");
        else if (in_list(type, {"BACK HEAD", "BACK TOP"})) pos("BACK", "TOP");
        else return true;
    }
    game_screen().set_camera_duration((int)frames);
    return true;
}

static bool action_zoom(const std::vector<std::string>& args) {
    if (!camera_enabled()) return true;
    re::Groups g;
    double scale;
    if (re::search("(\\d+)([%\xEF\xBC\x85])", "i", arg(args, 0), &g)) scale = js_number(g[1]) * 0.01;
    else scale = js_parse_float(arg(args, 0));
    if (std::isnan(scale) || scale == 0) scale = 1.0;
    double duration = js_parse_int(arg(args, 1));
    if (std::isnan(duration) || duration == 0) duration = 30;
    game_screen().start_battle_zoom(scale, (int)duration);
    return true;
}

static bool action_start_counter_phase();

bool process_action_sequence(const std::string& name, const std::vector<std::string>& args) {
    State& s = g_state;
    re::Groups g;
    if (re::search("(?:BATTLEBACK|BATTLE BACK)[ ](\\d+)", "i", name)) {
        std::string line = name + ": ";
        std::string str;
        for (size_t i = 0; i < args.size(); ++i) {
            str += args[i];
            if (i != args.size() - 1) str += ", ";
        }
        line += trim(str);
        alter_battleback(line);
        return false;
    }
    if (re::search("CUSTOM MOTION[ ](.*)", "i", name, &g)) {
        auto movers = make_action_targets(arg(args, 0));
        if (movers.empty()) return true;
        for (GameBattler* m : movers) m->force_motion(to_lower(g[1]));
        return false;
    }
    if (name == "CRITICAL MULTIPLIER") {
        bool ok;
        double v = rate_from(arg(args, 0), ok);
        if (ok) game_system().crit_mult = v;
        return true;
    }
    if (name == "FLAT CRITICAL") {
        bool ok;
        double v = flat_from(arg(args, 0), ok);
        if (ok) game_system().flat_crit = v;
        return true;
    }
    if (name == "FORCE CRITICAL") {
        game_system().force_critical = true;
        game_system().force_no_critical = false;
        return true;
    }
    if (name == "FORCE NO CRITICAL") {
        game_system().force_critical = false;
        game_system().force_no_critical = true;
        return true;
    }
    if (name == "NORMAL CRITICAL") {
        game_system().force_critical = false;
        game_system().force_no_critical = false;
        return true;
    }
    if (name == "BYPASS DAMAGE CAP") {
        game_system().act_seq_bypass_damage_cap = true;
        return true;
    }
    if (name == "DAMAGE CAP" || name == "HEALING CAP") {
        if (!arg(args, 0).empty()) {
            game_system().has_act_seq_damage_cap = true;
            game_system().act_seq_damage_cap = js_parse_int(args[0]);
        }
        return true;
    }
    if (name == "DAMAGE RATE" || name == "GLOBAL RATE" || name == "HEAL RATE") {
        bool ok;
        double v = rate_from(arg(args, 0), ok);
        if (!ok) return true;
        if (name != "HEAL RATE") game_system().damage_rate = v;
        if (name != "DAMAGE RATE") game_system().heal_rate = v;
        return true;
    }
    if (name == "FLAT DAMAGE" || name == "FLAT GLOBAL" || name == "FLAT HEAL") {
        bool ok;
        double v = flat_from(arg(args, 0), ok);
        if (!ok) return true;
        if (name != "FLAT HEAL") game_system().flat_damage = v;
        if (name != "FLAT DAMAGE") game_system().flat_heal = v;
        return true;
    }
    if (name == "RESET DAMAGE CAP") {
        game_system().act_seq_bypass_damage_cap = false;
        game_system().has_act_seq_damage_cap = false;
        return true;
    }
    if (name == "RESET DAMAGE MODIFIERS") {
        game_system().reset_damage_settings();
        return true;
    }
    if (name == "START COUNTER PHASE") return action_start_counter_phase();
    if (name == "CAMERA CLAMP ON" || name == "CAMERA CLAMP OFF") {
        if (!camera_enabled()) return true;
        s.camera_clamp = name == "CAMERA CLAMP ON";
        return true;
    }
    if (name == "CAMERA FOCUS" || name == "FOCUS CAMERA") return action_camera_focus(args);
    if (name == "CAMERA OFFSET" || name == "OFFSET CAMERA") return action_camera_offset(args);
    if (name == "CAMERA PAN" || name == "PAN CAMERA") return action_camera_pan(args);
    if (name == "CAMERA SCREEN") return action_camera_screen(args);
    if (name == "RESET CAMERA") {
        double d = js_parse_int(arg(args, 0));
        reset_camera(std::isnan(d) || d == 0 ? 30 : (int)d);
        return true;
    }
    if (name == "RESET ZOOM") {
        double d = js_parse_int(arg(args, 0));
        action_reset_zoom(std::isnan(d) || d == 0 ? 30 : (int)d);
        return true;
    }
    if (name == "WAIT FOR CAMERA") {
        if (!camera_enabled()) return true;
        if (s.log) s.log->wait_for_camera();
        return false;
    }
    if (name == "WAIT FOR ZOOM") {
        if (!camera_enabled()) return true;
        if (s.log) s.log->wait_for_zoom();
        return false;
    }
    if (name == "ZOOM") return action_zoom(args);
    if (name == "ATTACK ANIMATION") {
        auto ts = unique(make_action_targets(arg(args, 0)));
        if (s.log) {
            if (to_upper(arg(args, 1)) == "MIRROR") s.log->show_actor_atk_ani_mirror(subject(), ts);
            else s.log->show_attack_animation(subject(), ts);
        }
        return true;
    }
    if (name == "ENEMY EFFECT") {
        auto ts = make_action_targets(arg(args, 0));
        if (ts.empty()) return true;
        std::string e = to_upper(arg(args, 1));
        for (GameBattler* t : ts) {
            if (!t->is_enemy()) continue;
            if (e == "WHITEN") t->request_effect("whiten");
            else if (e == "BLINK") t->request_effect("blink");
        }
        return true;
    }
    if (re::search("FACE[ ](.*)", "i", name, &g)) {
        if (!make_action_targets(g[1]).empty()) return false;
    }
    if (name == "FADE IN" || name == "FADE OUT") {
        double frames = js_or(arg(args, 0), 60);
        if (name == "FADE IN") game_screen().start_fade_in((int)frames);
        else game_screen().start_fade_out((int)frames);
        return false;
    }
    if (name == "FLASH SCREEN") return action_flash_screen(args);
    if (re::search("FLOAT[ ](.*)", "i", name, &g)) {
        if (!make_action_targets(g[1]).empty()) return false;
    }
    if (name == "HIDE BATTLE HUD" || name == "SHOW BATTLE HUD") {
        if (s.scene) s.scene->set_window_layer_hidden(name == "HIDE BATTLE HUD");
        return false;
    }
    if (re::search("JUMP[ ](.*)", "i", name, &g)) {
        if (!make_action_targets(g[1]).empty()) return true;
    }
    if (re::search("MOTION[ ](.*)", "i", name, &g)) return action_motion_target(g[1], args);
    if (re::search("MOVE[ ](.*)", "i", name, &g)) {
        if (!make_action_targets(g[1]).empty()) return true;
    }
    if (re::search("OPACITY[ ](.*)", "i", name, &g)) {
        if (!make_action_targets(g[1]).empty()) return action_opacity(g[1], args);
    }
    if (name == "SHAKE SCREEN") {
        double power = js_or(arg(args, 0), 5), speed = js_or(arg(args, 1), 5), frames = js_or(arg(args, 2), 60);
        game_screen().start_shake(js_parse_int(js::number_to_string(power)), js_parse_int(js::number_to_string(speed)), (int)js_parse_int(js::number_to_string(frames)));
        return false;
    }
    if (name == "TINT SCREEN") return action_tint_screen(args);
    if (name == "WAIT FOR FLOAT") {
        if (s.log) s.log->wait_for_float();
        return false;
    }
    if (name == "WAIT FOR JUMP") {
        if (s.log) s.log->wait_for_jump();
        return false;
    }
    if (name == "WAIT FOR OPACITY") {
        if (s.log) s.log->wait_for_opacity();
        return false;
    }
    if (re::search("ADD[ ](.*)[ ]BUFF", "i", name)) return action_buff(name, args, true, false);
    if (re::search("ADD[ ](.*)[ ]DEBUFF", "i", name)) return action_buff(name, args, true, true);
    if (re::search("(?:ADD_STATE|ADD STATE)[ ](\\d+(?:\\s*,\\s*\\d+)*)", "i", name)) return action_add_state(name, args);
    if (re::search("ANIMATION[ ](\\d+)", "i", name, &g)) {
        int ani = g.num(1);
        if (ani <= 0) return false;
        auto ts = make_action_targets(arg(args, 0));
        if (ts.empty()) return false;
        if (s.log) s.log->show_normal_animation(ts, ani, to_upper(arg(args, 1)) == "MIRROR");
        return true;
    }
    if (in_list(name, {"BGM", "MUSIC", "SONG"})) return action_sound("bgm", args);
    if (in_list(name, {"BGS", "AMBIENCE"})) return action_sound("bgs", args);
    if (name == "BREAK ACTION") {
        s.targets.clear();
        s.action_list.clear();
        s.individual_targets.clear();
        s.phase = "phaseChange";
        return false;
    }
    if (name == "COLLAPSE") return action_collapse(args);
    if (name == "COMMON EVENT") return action_common_event((int)js_number(arg(args, 0)));
    if (re::search("CHANGE[ ]SWITCH[ ](.*)", "i", name)) return action_change_switch(name, args);
    if (re::search("CHANGE[ ]VARIABLE[ ](.*)", "i", name)) return action_change_variable();
    if (name == "EVAL" || name == "SCRIPT") return action_eval(args);
    if (re::search("GAIN[ ](.*)[ ](\\d+)", "i", name) || re::search("LOSE[ ](.*)[ ](\\d+)", "i", name)) return action_gain_item(name, args);
    if (re::search("GOLD[ ]([\\+\\-]\\d+)", "i", name, &g)) {
        game_party().gain_gold(js_parse_int(g[1]));
        return true;
    }
    if (name == "ME" || name == "FANFARE") return action_sound("me", args);
    if (name == "REFRESH STATUS" || name == "REFRESH WINDOW") {
        refresh_status();
        return false;
    }
    if (re::search("REMOVE[ ](.*)[ ]BUFF", "i", name)) return action_buff(name, args, false, false);
    if (re::search("REMOVE[ ](.*)[ ]DEBUFF", "i", name)) return action_buff(name, args, false, true);
    if (re::search("(?:REMOVE_STATE|REMOVE STATE)[ ](\\d+(?:\\s*,\\s*\\d+)*)", "i", name)) return action_remove_state(name, args);
    if (in_list(name, {"SE", "SOUND", "SFX"})) return action_se(args);
    if (re::search("HP[ ](.*)", "i", name)) return action_point_modify(name, args, "HP");
    if (re::search("MP[ ](.*)", "i", name)) return action_point_modify(name, args, "MP");
    if (re::search("TP[ ](.*)", "i", name)) return action_point_modify(name, args, "TP");
    if (name.empty()) return true;
    if (name == "ACTION ANIMATION") return action_action_animation(args);
    if (name == "ACTION COMMON EVENT") {
        const Value& effects = s.action->item_data()["effects"];
        for (size_t i = 0; i < effects.size(); i++) {
            if (effects[i]["code"].as_int() == GameAction::EFFECT_COMMON_EVENT) game_temp().reserve_common_event(effects[i]["dataId"].as_int());
        }
        return false;
    }
    if (name == "ACTION EFFECT") return action_action_effect(args);
    if (in_list(name, {"ANI WAIT", "ANIWAIT", "ANIMATION WAIT"})) {
        double rate = yanfly_param("AnimationRate").num();
        double frames = js_number(arg(args, 0)) * (rate ? rate : 4);
        if (s.log) s.log->wait_count() = (int)js_parse_int(js::number_to_string(frames));
        return false;
    }
    if (name == "CAST ANIMATION") {
        if (subject()->is_actor()) return true;
        if (!s.action->is_attack() && !s.action->is_guard() && s.action->is_skill()) {
            int cast = s.action->item_data()["castAnimation"].as_int();
            if (cast > 0 && s.log) s.log->show_animation(subject(), {subject()}, cast);
        }
        return true;
    }
    if (name == "CLEAR BATTLE LOG") {
        if (s.log) s.log->clear();
        return false;
    }
    if (name == "DEATH BREAK") {
        if (subject()->is_dead()) {
            s.targets.clear();
            s.action_list.clear();
            s.individual_targets.clear();
            s.phase = "phaseChange";
            return false;
        }
        return true;
    }
    if (name == "DISPLAY ACTION") {
        if (s.log) s.log->display_action(subject(), s.action->item());
        return false;
    }
    if (name == "IMMORTAL") return action_immortal(args);
    if (name == "MOTION WAIT") {
        auto ts = make_action_targets(arg(args, 0));
        if (!ts.empty() && ts[0]->is_actor() && static_cast<GameActor*>(ts[0])->is_sprite_visible()) {
            if (s.log) s.log->wait_count() += 12;
            return false;
        }
        return true;
    }
    if (name == "PERFORM ACTION") {
        if (s.log) s.log->perform_action(subject(), s.action.get());
        if (subject()->is_actor()) {
            if (s.log) s.log->wait_count() += 20;
            return false;
        }
        return true;
    }
    if (name == "PERFORM FINISH") {
        action_reset_zoom(30);
        reset_camera(30);
        if (s.log) s.log->perform_action_end(subject());
        return true;
    }
    if (name == "PERFORM START") {
        if (s.log) s.log->perform_action_start(subject(), s.action.get());
        return true;
    }
    if (name == "WAIT") {
        if (s.log) s.log->wait_count() = (int)js_parse_int(arg(args, 0));
        return false;
    }
    if (name == "WAIT FOR ANIMATION") {
        if (s.log) s.log->wait_for_animation();
        return false;
    }
    if (name == "WAIT FOR EFFECT") {
        if (s.log) s.log->wait_for_effect();
        return false;
    }
    if (name == "WAIT FOR MOVEMENT") {
        if (s.log) s.log->wait_for_movement();
        return false;
    }
    if (name == "WAIT FOR NEW LINE") {
        if (s.log) s.log->wait_for_new_line();
        return false;
    }
    if (name == "WAIT FOR POPUPS") {
        if (s.log) s.log->wait_for_popups();
        return false;
    }
    return false;
}

static void action_perform_counter_phase(const std::shared_ptr<GameAction>& action) {
    State& s = g_state;
    s.countering = true;
    if (subject()) subject()->remove_counter_action();
    GameBattler* sub = action->subject();
    s.subject = BattlerRef::of(sub);
    s.has_subject = sub != nullptr;
    s.action = action;
    auto ts = action->make_targets();
    set_targets(ts);
    s.all_targets = refs_of(ts);
    s.individual_targets = refs_of(ts);
    s.phase = "phaseChange";
    s.phase_steps = {"setup", "whole", "target", "follow", "finish"};
    s.return_phase.clear();
    s.action_list.clear();
    sub->use_item(s.action->item());
    s.action->apply_global();
    if (s.log) s.log->start_action(sub, s.action.get(), resolve(s.targets));
}

static void action_end_counter_phase() {
    State& s = g_state;
    s.countering = false;
    if (s.has_counter_original_subject) {
        s.subject = s.counter_original_subject;
        s.has_subject = true;
        s.action = s.counter_original_action;
    }
    s.has_counter_original_subject = false;
    s.counter_original_action = nullptr;
    s.counter_sequence = 0;
    s.counter_queue.clear();
}

static bool action_start_counter_phase() {
    State& s = g_state;
    std::shared_ptr<GameAction> action;
    if (!s.counter_queue.empty()) {
        action = s.counter_queue.front();
        s.counter_queue.erase(s.counter_queue.begin());
    }
    if (is_max_counter_queue() || !action) action_end_counter_phase();
    else if (action->subject()->is_dead()) action_start_counter_phase();
    else action_perform_counter_phase(action);
    return false;
}

bool is_countering() { return g_state.countering; }

void refresh_enemy_reinforcements() {
    if (g_state.spriteset) g_state.spriteset->refresh_enemy_reinforcements();
}

void alter_battleback(const std::string& line) {
    State& s = g_state;
    re::Groups g;
    if (!re::search("(?:BATTLEBACK|BATTLE BACK)[ ](\\d+)", "i", line, &g)) return;
    int id = std::max(1, g.num(1));
    if (!s.spriteset) return;
    re::Groups h;
    if (re::search("TESTING", "i", line)) {
        plat::log("Test Passed");
    } else if (re::search("CHANGE TO", "i", line)) {
        if (re::search(":[ ](.*),[ ](.*),[ ](\\d+)", "i", line, &h)) s.spriteset->change_battleback_to(id, "img/" + h[1] + "/", h[2], std::min(std::max(h.num(3), 0), 360));
        else if (re::search(":[ ](.*),[ ](.*)", "i", line, &h)) s.spriteset->change_battleback_to(id, "img/" + h[1] + "/", h[2], 0);
    } else if (re::search("FADE IN", "i", line)) {
        s.spriteset->battleback_fade_in(id, re::search(":[ ](\\d+)", "i", line, &h) ? h.num(1) : 20);
    } else if (re::search("FADE OUT", "i", line)) {
        s.spriteset->battleback_fade_out(id, re::search(":[ ](\\d+)", "i", line, &h) ? h.num(1) : 20);
    } else if (re::search("OPACITY", "i", line)) {
        if (re::search(":[ ](\\d+)([%\xEF\xBC\x85])", "i", line, &h)) s.spriteset->battleback_opacity(id, (int)std::round(js_parse_float(h[1]) * 0.01 * 255));
        else if (re::search(":[ ](\\d+)", "i", line, &h)) s.spriteset->battleback_opacity(id, h.num(1));
    } else if (re::search("RESET SCROLL SPEED", "i", line)) {
        s.spriteset->reset_scroll_speeds(id);
    } else if (re::search("SCROLL SPEED X:[ ]([\\+\\-]\\d+)", "i", line, &h)) {
        s.spriteset->set_battleback_scroll_speed_x(id, (int)js_parse_int(h[1]));
    } else if (re::search("SCROLL SPEED Y:[ ]([\\+\\-]\\d+)", "i", line, &h)) {
        s.spriteset->set_battleback_scroll_speed_y(id, (int)js_parse_int(h[1]));
    } else if (re::search("ADD", "i", line)) {
        if (re::search(":[ ](.*),[ ](.*),[ ](\\d+)", "i", line, &h)) s.spriteset->add_new_battleback(id, "img/" + h[1] + "/", h[2], std::min(std::max(h.num(3), 0), 360), 0, 20);
        else if (re::search(":[ ](.*),[ ](.*)", "i", line, &h)) s.spriteset->add_new_battleback(id, "img/" + h[1] + "/", h[2], 0, 0, 20);
    } else if (re::search("REMOVE", "i", line)) {
        s.spriteset->remove_battleback(id);
    }
}

bool is_battle_intro_active() { return g_state.battle_intro.truthy(); }

void set_battle_intro(const js::Val& name) { g_state.battle_intro = name; }

void make_battle_retry_data() { g_state.retry_data = save::snapshot_for_retry(); }

void process_retry() {
    State& s = g_state;
    if (game_troop().is_humphrey_battle()) {
        js::Val* bb1 = game_temp().expando.find(js::atom("_bb1Name"));
        if (bb1 && bb1->truthy()) game_map().change_battleback(bb1->to_str(), game_map().battleback2_name());
    }
    save::restore_retry_snapshot(s.retry_data);
    s.battle_retried = true;
    game_variables().set_value(1220, js::Val::num(s.final_battle_phase));
    setup(s.retry_troop, s.retry_can_escape, s.retry_can_lose);
    scenes::goto_battle();
}

void set_picture_display_layer(const std::string& layer) {
    g_state.picture_display_layer = to_lower(layer.empty() ? "front" : layer);
    if (g_state.spriteset) g_state.spriteset->set_picture_layer(g_state.picture_display_layer);
}

void start_fade_out(int duration, bool white) {
    if (g_state.scene) g_state.scene->start_fade_out(duration, white);
}

void start_fade_in(int duration, bool white) {
    if (g_state.scene) g_state.scene->start_fade_in(duration, white);
}

bool is_battle_same_of_map_bgm() {
    if (!g_state.has_map_bgm || !g_state.map_bgm.valid) return false;
    AudioObj cur = audio_manager::current_bgm();
    if (!cur.valid) return false;
    return cur.name == g_state.map_bgm.name;
}

double target_pos_x(const std::vector<GameBattler*>& group, const std::string& position) {
    double value = 0;
    if (position == "MIDDLE") {
        for (GameBattler* b : group) {
            if (b) value += b->camera_pos_x();
        }
    } else if (position == "FRONT") {
        for (GameBattler* b : group) {
            if (!b) continue;
            double off = b->is_actor() ? -1 * b->sprite_width() / 2 : b->sprite_width() / 2;
            value = std::max(b->camera_pos_x() + off, value);
        }
        value *= group.size();
    } else if (position == "BACK") {
        value = 640;
        for (GameBattler* b : group) {
            if (!b) continue;
            double off = b->is_actor() ? b->sprite_width() / 2 : -1 * b->sprite_width() / 2;
            value = std::min(b->camera_pos_x() + off, value);
        }
        value *= group.size();
    }
    return value / group.size();
}

double target_pos_y(const std::vector<GameBattler*>& group, const std::string& position) {
    double value = 0;
    if (position == "BASE") {
        for (GameBattler* b : group) {
            if (b) value = std::max(b->camera_pos_y(), value);
        }
        value *= group.size();
    } else if (position == "MIDDLE") {
        for (GameBattler* b : group) {
            if (b) value += b->camera_pos_y() - b->sprite_height() / 2;
        }
    } else if (position == "TOP") {
        value = 480;
        for (GameBattler* b : group) {
            if (b) value = std::min(b->camera_pos_y() - b->sprite_height(), value);
        }
        value *= group.size();
    }
    return value / group.size();
}

double camera_x() {
    State& s = g_state;
    double v = s.camera_focus_group.empty() ? s.camera_x : target_pos_x(resolve(s.camera_focus_group), s.camera_focus_pos_x);
    return v + s.camera_offset_x;
}

double camera_y() {
    State& s = g_state;
    double v = s.camera_focus_group.empty() ? s.camera_y : target_pos_y(resolve(s.camera_focus_group), s.camera_focus_pos_y);
    return v + s.camera_offset_y;
}

void reset_camera(int duration) {
    State& s = g_state;
    s.camera_x = 320;
    s.camera_y = 240;
    s.camera_offset_x = 0;
    s.camera_offset_y = 0;
    s.camera_focus_group.clear();
    s.camera_focus_pos_x = "BASE";
    s.camera_focus_pos_y = "BASE";
    s.camera_clamp = true;
    game_screen().set_camera_duration(duration);
}

void action_reset_zoom(int duration) { game_screen().start_battle_zoom(1, duration); }

}

double battle_camera_x() { return battle::camera_x(); }
double battle_camera_y() { return battle::camera_y(); }

}
