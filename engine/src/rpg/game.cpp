#include "game.h"

#include <cmath>
#include <memory>

#include "../core/image.h"
#include "../core/regex.h"
#include "../js/vm.h"
#include "../platform/platform.h"
#include "battler.h"
#include "data.h"
#include "hooks.h"
#include "interpreter.h"
#include "params.h"
#include "party.h"
#include "scene.h"

namespace rpg {

namespace {

struct Registry {
    std::unique_ptr<GameTemp> temp;
    std::unique_ptr<GameSystem> system;
    std::unique_ptr<GameScreen> screen;
    std::unique_ptr<GameTimer> timer;
    std::unique_ptr<GameMessage> message;
    std::unique_ptr<GameSwitches> switches;
    std::unique_ptr<GameVariables> variables;
    std::unique_ptr<GameSelfSwitches> self_switches;
    std::unique_ptr<GameSelfVariables> self_variables;
    std::unique_ptr<GameEventData> event_data;
    std::unique_ptr<GameActors> actors;
    std::unique_ptr<GameParty> party;
    std::unique_ptr<GameTroop> troop;
    std::unique_ptr<GameMap> map;
    std::unique_ptr<GamePlayer> player;
};

Registry g;

}

GameTemp& game_temp() { return *g.temp; }
GameSystem& game_system() { return *g.system; }
GameScreen& game_screen() { return *g.screen; }
GameTimer& game_timer() { return *g.timer; }
GameMessage& game_message() { return *g.message; }
GameSwitches& game_switches() { return *g.switches; }
GameVariables& game_variables() { return *g.variables; }
GameSelfSwitches& game_self_switches() { return *g.self_switches; }
GameSelfVariables& game_self_variables() { return *g.self_variables; }
GameEventData& game_event_data() { return *g.event_data; }
GameActors& game_actors() { return *g.actors; }
GameParty& game_party() { return *g.party; }
GameTroop& game_troop() { return *g.troop; }
GameMap& game_map() { return *g.map; }
GamePlayer& game_player() { return *g.player; }

bool game_objects_exist() { return (bool)g.player; }

void destroy_game_objects() {
    g.player.reset();
    g.map.reset();
    g.troop.reset();
    g.party.reset();
    g.actors.reset();
    g.event_data.reset();
    g.self_variables.reset();
    g.self_switches.reset();
    g.variables.reset();
    g.switches.reset();
    g.message.reset();
    g.timer.reset();
    g.screen.reset();
    g.system.reset();
    g.temp.reset();
    route_cache_clear();
}

void create_game_objects() {
    destroy_game_objects();
    fc::follower_control = false;
    fc::follower_control_id = -1;
    fc::follower_stop = false;
    fc::follower_collision = false;
    g.temp.reset(new GameTemp());
    g.system.reset(new GameSystem());
    g.screen.reset(new GameScreen());
    g.timer.reset(new GameTimer());
    g.message.reset(new GameMessage());
    g.switches.reset(new GameSwitches());
    g.variables.reset(new GameVariables());
    g.self_switches.reset(new GameSelfSwitches());
    g.self_variables.reset(new GameSelfVariables());
    g.event_data.reset(new GameEventData());
    g.actors.reset(new GameActors());
    g.party.reset(new GameParty());
    g.troop.reset(new GameTroop());
    g.map.reset(new GameMap());
    g.player.reset(new GamePlayer());
}

uint64_t frame_count() { return scene_manager::frame_count(); }

void set_frame_count(uint64_t f) { scene_manager::set_frame_count(f); }

const Value& current_map_data() { return game_map().data(); }

bool party_in_battle() { return g.party && g.party->in_battle(); }

void filter_con_update() { game_map().update_filter_con_arr(); }

void timer_expire_hook() {
    const Value* ast = yanfly_ast("TimerExpire");
    if (!ast) return;
    js::ScopePtr scope = std::make_shared<js::Scope>();
    scope->function_scope = true;
    js::Val r;
    if (!js::vm().run(*ast, game_timer_js(), scope, &r, data::keep())) script_error("EVENT TIMER CONTROL EXPIRE CODE ERROR", js::vm().last_error);
}

void after_load_hook() {
    if (game_system().playtime_ms <= 0) game_system().playtime_ms = std::floor(game_system().frames_on_save * 1000.0 / 60.0);
    playtime_manager::clear();
    fc::follower_control = false;
    fc::follower_control_id = -1;
    fc::follower_stop = false;
    fc::follower_collision = false;
    for (GameActor* a : game_party().members()) {
        a->last_battle_skill = GameItem();
        a->last_command_symbol.clear();
    }
    GameFollower* f = game_player().followers_.follower(0);
    if (f && f->sprite_offset_x == 32 && f->sprite_offset_y == -18) {
        f->sprite_offset_x = 0;
        f->sprite_offset_y = 0;
    }
}

bool meta_has(const std::string& note, const std::string& key, std::string* value) {
    re::Groups gr;
    std::string pattern = "<" + key + "(?::[ ]*([^>]*))?>";
    if (!re::search(pattern.c_str(), "", note, &gr)) return false;
    if (value) *value = gr.has(1) ? gr[1] : std::string();
    return true;
}

void script_error(const char* where, const std::string& msg) { plat::log("%s: %s", where, msg.c_str()); }

double image_logical_height(const char* folder, const std::string& name) {
    int w, h;
    images::logical_size(folder, name, w, h);
    return h;
}

static js::Val make_index_proxy(bool switches) {
    static js::HostClass sw_cls;
    static js::HostClass var_cls;
    js::HostClass& c = switches ? sw_cls : var_cls;
    if (!c.name) {
        c.name = switches ? "SwitchesProxy" : "VariablesProxy";
        bool sw = switches;
        c.get = [sw](js::Vm&, js::Obj*, uint32_t key, js::Val& out) {
            double id = js::string_to_number(js::atom_name(key));
            if (std::isnan(id)) return false;
            out = sw ? js::Val::boolean(game_switches().value((int)id)) : game_variables().value((int)id);
            return true;
        };
        c.set = [sw](js::Vm&, js::Obj*, uint32_t key, const js::Val& v) {
            double id = js::string_to_number(js::atom_name(key));
            if (std::isnan(id)) return false;
            if (sw) game_switches().set_value((int)id, v.truthy());
            else game_variables().set_value((int)id, v);
            return true;
        };
    }
    return js::make_host(&c, nullptr, switches ? 1 : 2);
}

js::Val switches_proxy() {
    static js::Val v = make_index_proxy(true);
    return v;
}

js::Val variables_proxy() {
    static js::Val v = make_index_proxy(false);
    return v;
}

namespace tddp_ff {
bool text_ff_disabled = false;
bool next_text_ff_disabled = false;

static bool param_true(const char* key) { return data::plugin_param("TDDP_NoFastForward", key, "") == "true"; }

bool is_text_ff_disabled() {
    static bool global = param_true("Disable for Show Text");
    return next_text_ff_disabled || text_ff_disabled || global;
}

bool is_route_ff_disabled() {
    static bool global = param_true("Disable for Move Routes");
    return global;
}
}

bool is_text_in_current_language(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i];
        if (c < 0x80) {
            bool ok = std::isalnum(c) || std::string("!@#$%^&*()_+-=[]{};':\"\\|,.<>/? ").find((char)c) != std::string::npos;
            if (!ok) return false;
            i++;
            continue;
        }
        if (text.compare(i, 3, "\xE2\x98\x86") == 0) {
            i += 3;
            continue;
        }
        return false;
    }
    return true;
}

}
