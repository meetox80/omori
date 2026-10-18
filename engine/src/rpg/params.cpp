#include "params.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

#include "../core/regex.h"
#include "../js/value.h"
#include "../js/vm.h"
#include "data.h"

namespace rpg {

double js_number(const std::string& s) { return js::string_to_number(s); }

double js_parse_int(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    int radix = 10;
    if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        radix = 16;
        i += 2;
    }
    double v = 0;
    bool any = false;
    for (; i < s.size(); i++) {
        int d;
        char c = s[i];
        if (c >= '0' && c <= '9') d = c - '0';
        else if (radix == 16 && c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (radix == 16 && c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = v * radix + d;
        any = true;
    }
    if (!any) return NAN;
    return neg ? -v : v;
}

double js_parse_float(const std::string& s) {
    size_t i = 0;
    while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
    std::string rest = s.substr(i);
    if (rest.rfind("Infinity", 0) == 0 || rest.rfind("+Infinity", 0) == 0) return INFINITY;
    if (rest.rfind("-Infinity", 0) == 0) return -INFINITY;
    size_t j = 0;
    if (j < rest.size() && (rest[j] == '+' || rest[j] == '-')) j++;
    bool digits = false;
    while (j < rest.size() && std::isdigit((unsigned char)rest[j])) {
        j++;
        digits = true;
    }
    if (j < rest.size() && rest[j] == '.') {
        j++;
        while (j < rest.size() && std::isdigit((unsigned char)rest[j])) {
            j++;
            digits = true;
        }
    }
    if (!digits) return NAN;
    if (j < rest.size() && (rest[j] == 'e' || rest[j] == 'E')) {
        size_t k = j + 1;
        if (k < rest.size() && (rest[k] == '+' || rest[k] == '-')) k++;
        if (k < rest.size() && std::isdigit((unsigned char)rest[k])) {
            while (k < rest.size() && std::isdigit((unsigned char)rest[k])) k++;
            j = k;
        }
    }
    return std::strtod(rest.substr(0, j).c_str(), nullptr);
}

std::string to_upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

std::string to_lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}

std::string js_format(const std::string& fmt, std::initializer_list<std::string> args) {
    std::vector<std::string> a(args);
    std::string out;
    for (size_t i = 0; i < fmt.size(); i++) {
        if (fmt[i] == '%' && i + 1 < fmt.size() && std::isdigit((unsigned char)fmt[i + 1])) {
            size_t j = i + 1;
            int n = 0;
            while (j < fmt.size() && std::isdigit((unsigned char)fmt[j])) n = n * 10 + (fmt[j++] - '0');
            if (n >= 1 && n <= (int)a.size()) out += a[(size_t)n - 1];
            else out += fmt.substr(i, j - i);
            i = j - 1;
            continue;
        }
        out += fmt[i];
    }
    return out;
}

const Value& yanfly_param(const char* key) { return data::root()["yanfly"]["Param"][key]; }

const Value* yanfly_ast(const char* key) {
    const Value* v = data::root()["yanfly"]["_ev"].find(key);
    return v && v->is_arr() ? v : nullptr;
}

const Value* obj_ast(const Value& obj, const std::string& path) {
    const Value* v = obj["_ev"].find(path.c_str());
    return v && v->is_arr() ? v : nullptr;
}

double yanfly_eval_num(const char* key, double def) {
    const Value& p = yanfly_param(key);
    if (p.is_num()) return p.num();
    const Value* ast = yanfly_ast(key);
    if (!ast) return def;
    js::Val r;
    if (!js::vm().run(*ast, js::Val(), nullptr, &r, data::keep())) return def;
    return r.to_num();
}

static std::string param(const char* plugin, const char* key) { return data::plugin_param(plugin, key, ""); }

static bool has_param(const char* plugin, const char* key) { return data::plugin(plugin).find(key) != nullptr; }

static double num_param(const char* plugin, const char* key) {
    if (!has_param(plugin, key)) return NAN;
    return js_number(param(plugin, key));
}

static bool eval_bool(const std::string& s) { return trim(s) == "true"; }

static std::vector<double> space_list(const std::string& s) {
    std::vector<double> out;
    size_t start = 0;
    while (true) {
        size_t sp = s.find(' ', start);
        out.push_back(js_number(s.substr(start, sp == std::string::npos ? std::string::npos : sp - start)));
        if (sp == std::string::npos) break;
        start = sp + 1;
    }
    return out;
}

static std::vector<double> space_list_int(const std::string& s) {
    std::vector<double> out;
    size_t start = 0;
    while (true) {
        size_t sp = s.find(' ', start);
        out.push_back(js_parse_int(s.substr(start, sp == std::string::npos ? std::string::npos : sp - start)));
        if (sp == std::string::npos) break;
        start = sp + 1;
    }
    return out;
}

static js::Val parse_json(const std::string& s) {
    try {
        return js::json_parse_text(s);
    } catch (...) {
        return js::Val();
    }
}

static std::string jstr(const js::Val& o, const char* key) {
    if (!o.is_obj()) return "";
    const js::Val* v = o.o->find(js::atom(key));
    return v ? v->to_str() : std::string();
}

static void load_templates(const char* plugin, std::map<std::string, CopierTemplate>& out, std::set<int>& preload) {
    js::Val list = parse_json(param(plugin, "TemplateNames"));
    if (list.is_array()) {
        for (auto& item : list.o->items) {
            js::Val d = parse_json(item.to_str());
            CopierTemplate t;
            t.map_id = (int)js_parse_int(jstr(d, "MapID"));
            t.event_id = (int)js_parse_int(jstr(d, "EventID"));
            out[to_upper(jstr(d, "Name"))] = t;
            if (t.map_id > 0) preload.insert(std::min(std::max(t.map_id, 1), 999));
        }
    }
    js::Val maps = parse_json(param(plugin, "TemplateMaps"));
    if (maps.is_array()) {
        for (auto& m : maps.o->items) {
            double id = js_parse_int(m.to_str());
            if (!std::isnan(id)) preload.insert(std::min(std::max((int)id, 1), 999));
        }
    }
}

static PluginParams build() {
    PluginParams p;
    p.tiled_z_below = (int)js_parse_int(param("YED_Tiled", "Z - Below Player"));
    p.tiled_z_player = (int)js_parse_int(param("YED_Tiled", "Z - Player"));
    p.tiled_z_above = (int)js_parse_int(param("YED_Tiled", "Z - Above Player"));
    p.map_level_variable = (int)js_parse_int(param("YED_Tiled", "Map Level Variable"));
    double ct = num_param("ExtraMovementFrames", "Cycle Time");
    p.emf_cycle_time = (std::isnan(ct) || ct == 0) ? 60 : ct;
    double idle = num_param("ExtraMovementFrames", "Default Idle Frame");
    p.emf_idle_frame = (std::isnan(idle) || idle == 0) ? 0 : (int)idle;
    for (auto& m : re::all("\\[.+?\\]", "g", param("ExtraMovementFrames", "Default Pattern"))) {
        std::vector<int> digits;
        for (auto& d : re::all("\\d+", "g", m)) digits.push_back(std::atoi(d.c_str()));
        if (!digits.empty()) p.emf_default_pattern.push_back(digits);
    }
    p.ladder_region = (int)js_number(param("AEL_TilesetPropertyRegions", "Ladder Region"));
    p.counter_region = (int)js_number(param("AEL_TilesetPropertyRegions", "Counter Region"));
    p.damage_floor_region = (int)js_number(param("AEL_TilesetPropertyRegions", "Damage Floor Region"));
    p.bush_region = (int)js_number(param("AEL_TilesetPropertyRegions", "Bush Region"));
    p.bush_height = js_number(param("AEL_TilesetPropertyRegions", "Bush Height"));
    p.slip_region = num_param("YEP_SlipperyTiles", "Slippery Region");
    p.slip_frame = num_param("YEP_SlipperyTiles", "Slippery Frame");
    p.slip_speed = num_param("YEP_SlipperyTiles", "Slippery Speed");
    p.jump_illegal_region = space_list_int(param("YEP_SmartJump", "Illegal Regions"));
    {
        js::Val l = parse_json(has_param("YEP_SmartJump", "Illegal Regions List") && !param("YEP_SmartJump", "Illegal Regions List").empty()
                                   ? param("YEP_SmartJump", "Illegal Regions List")
                                   : "[]");
        if (l.is_array())
            for (auto& v : l.o->items) p.jump_illegal_region.push_back(js_parse_int(v.to_str()));
    }
    p.jump_equal_region = space_list_int(param("YEP_SmartJump", "Equal Regions"));
    {
        js::Val l = parse_json(has_param("YEP_SmartJump", "Equal Regions List") && !param("YEP_SmartJump", "Equal Regions List").empty()
                                   ? param("YEP_SmartJump", "Equal Regions List")
                                   : "[]");
        if (l.is_array())
            for (auto& v : l.o->items) p.jump_equal_region.push_back(js_parse_int(v.to_str()));
    }
    p.foot_default_sound = param("YEP_FootstepSounds", "Default Sound");
    p.foot_default_volume = num_param("YEP_FootstepSounds", "Default Volume");
    p.foot_default_pitch = num_param("YEP_FootstepSounds", "Default Pitch");
    p.foot_player_enable = eval_bool(param("YEP_FootstepSounds", "Player Enable"));
    p.foot_player_volume = js_parse_float(param("YEP_FootstepSounds", "Player Volume"));
    p.foot_player_pitch = js_parse_float(param("YEP_FootstepSounds", "Player Pitch"));
    p.foot_event_enable = eval_bool(param("YEP_FootstepSounds", "Event Enable"));
    p.foot_event_volume = js_parse_float(param("YEP_FootstepSounds", "Event Volume"));
    p.foot_distance_volume = js_parse_float(param("YEP_FootstepSounds", "Distance Volume"));
    p.foot_event_pitch = js_parse_float(param("YEP_FootstepSounds", "Event Pitch"));
    p.foot_distance_pitch = js_parse_float(param("YEP_FootstepSounds", "Distance Pitch"));
    p.foot_distance_pan = js_parse_int(param("YEP_FootstepSounds", "Distance Pan"));
    p.rr_all_allow = space_list(param("YEP_RegionRestrictions", "All Allow"));
    p.rr_all_restrict = space_list(param("YEP_RegionRestrictions", "All Restrict"));
    p.rr_event_allow = space_list(param("YEP_RegionRestrictions", "Event Allow"));
    p.rr_event_restrict = space_list(param("YEP_RegionRestrictions", "Event Restrict"));
    p.rr_player_allow = space_list(param("YEP_RegionRestrictions", "Player Allow"));
    p.rr_player_restrict = space_list(param("YEP_RegionRestrictions", "Player Restrict"));
    p.galv_tile = num_param("GALV_EventDetectors", "Tile Size");
    p.galv_searches = num_param("GALV_EventDetectors", "Search Limit");
    {
        std::string t = param("GALV_EventDetectors", "LOS Blocking Terrain");
        if (!t.empty()) {
            size_t start = 0;
            while (true) {
                size_t c = t.find(',', start);
                p.galv_block_terrain.push_back(js_number(t.substr(start, c == std::string::npos ? std::string::npos : c - start)));
                if (c == std::string::npos) break;
                start = c + 1;
            }
        }
        std::string r = param("GALV_EventDetectors", "LOS Blocking Regions");
        size_t start = 0;
        while (true) {
            size_t c = r.find(',', start);
            p.galv_block_regions.push_back(js_number(r.substr(start, c == std::string::npos ? std::string::npos : c - start)));
            if (c == std::string::npos) break;
            start = c + 1;
        }
        for (int i = 0;; i++) {
            std::string key = "Behavior " + std::to_string(i);
            std::string b = param("GALV_EventDetectors", key.c_str());
            if (b.empty()) break;
            std::vector<std::string> parts;
            size_t s0 = 0;
            while (true) {
                size_t c = b.find(',', s0);
                parts.push_back(b.substr(s0, c == std::string::npos ? std::string::npos : c - s0));
                if (c == std::string::npos) break;
                s0 = c + 1;
            }
            GalvBehavior gb;
            auto at = [&](size_t k) { return k < parts.size() ? js_number(parts[k]) : NAN; };
            gb.undetected = parts.size() > 0 ? parts[0] : "";
            gb.detected = parts.size() > 1 ? parts[1] : "";
            gb.range = at(2);
            gb.los = at(3);
            gb.speed = at(4);
            gb.frequency = at(5);
            gb.balloon = at(6);
            p.galv_behaviors[i] = gb;
        }
    }
    p.ecp_sight_lock = num_param("YEP_EventChasePlayer", "Sight Lock");
    p.ecp_see_player = eval_bool(param("YEP_EventChasePlayer", "See Player"));
    p.ecp_alert_timer = num_param("YEP_EventChasePlayer", "Alert Timer");
    p.ecp_alert_balloon = num_param("YEP_EventChasePlayer", "Alert Balloon");
    p.ecp_alert_sound = param("YEP_EventChasePlayer", "Alert Sound");
    p.ecp_alert_event = num_param("YEP_EventChasePlayer", "Alert Common Event");
    p.ecp_return = eval_bool(param("YEP_EventChasePlayer", "Return After"));
    p.ecp_return_wait = num_param("YEP_EventChasePlayer", "Return Wait");
    p.max_extra_followers = (int)js_number(param("Extra_Followers", "Max Extra Followers"));
    p.spawner_id = num_param("YEP_EventSpawner", "IdStartRange");
    p.stop_event = eval_bool(param("YEP_StopMapMovement", "Stop During Events"));
    p.stop_msg = eval_bool(param("YEP_StopMapMovement", "Stop During Message"));
    p.default_fade_time = num_param("SimpleEventFade", "Default Fade Time");
    for (int i = 1; i <= 255; i++) {
        std::string key = "Region " + std::to_string(i);
        std::string v = param("YEP_RegionEvents", key.c_str());
        p.region_events[i] = v.empty() ? 0 : js_number(v);
    }
    load_templates("YEP_EventCopier", p.copier_templates, p.preloaded_maps);
    load_templates("YEP_EventSpawner", p.spawner_templates, p.preloaded_maps);
    load_templates("YEP_EventMorpher", p.morpher_templates, p.preloaded_maps);
    auto uce = [&](const char* key) {
        double v = num_param("YEP_UtilityCommonEvents", key);
        return std::isnan(v) ? 0 : (int)v;
    };
    p.uce_load_game = uce("Load Game Event");
    p.uce_battle_won = uce("Battle Won Event");
    p.uce_escape = uce("Escape Battle Event");
    p.uce_close_menu = uce("Close Menu Event");
    p.uce_boat_enter = uce("Boat Enter Event");
    p.uce_boat_exit = uce("Boat Exit Event");
    p.uce_ship_enter = uce("Ship Enter Event");
    p.uce_ship_exit = uce("Ship Exit Event");
    p.uce_airship_enter = uce("Airship Enter Event");
    p.uce_airship_exit = uce("Airship Exit Event");
    p.tag_common_event = (int)js_number(param("Map_Character_Tag", "Common Event ID"));
    p.tag_selected_variable = (int)js_number(param("Map_Character_Tag", "Selected Variable ID"));
    p.tag_disable_switch = (int)js_number(param("Map_Character_Tag", "Disable Switch ID"));
    p.galv_cc_size = num_param("GALV_CamControl", "Tile Size");
    return p;
}

const PluginParams& params() {
    static PluginParams p = build();
    return p;
}

}
