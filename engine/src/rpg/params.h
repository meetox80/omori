#pragma once
#include <map>
#include <set>
#include <string>
#include <initializer_list>
#include <vector>

#include "../core/value.h"

namespace rpg {

struct GalvBehavior {
    std::string undetected, detected;
    double range = 0, los = 0, speed = 0, frequency = 0, balloon = 0;
};

struct CopierTemplate {
    int map_id = 0;
    int event_id = 0;
    std::string pre_copy, post_copy;
};

struct PluginParams {
    int tiled_z_below = 1, tiled_z_player = 3, tiled_z_above = 5;
    int map_level_variable = 26;
    double emf_cycle_time = 60;
    int emf_idle_frame = 0;
    std::vector<std::vector<int>> emf_default_pattern;
    double bush_height = 12;
    int ladder_region = 90, counter_region = 91, damage_floor_region = 92, bush_region = 93;
    double slip_region = 10, slip_frame = 2, slip_speed = 5;
    std::vector<double> jump_illegal_region, jump_equal_region;
    std::string foot_default_sound;
    double foot_default_volume = 10, foot_default_pitch = 150;
    bool foot_player_enable = true, foot_event_enable = true;
    double foot_player_volume = 1, foot_player_pitch = 1, foot_event_volume = 1, foot_distance_volume = -0.1, foot_event_pitch = 1,
           foot_distance_pitch = 0, foot_distance_pan = 10;
    std::vector<double> rr_all_allow, rr_all_restrict, rr_event_allow, rr_event_restrict, rr_player_allow, rr_player_restrict;
    double galv_tile = 32, galv_searches = 15;
    std::vector<double> galv_block_terrain, galv_block_regions;
    std::map<int, GalvBehavior> galv_behaviors;
    double ecp_sight_lock = 300;
    bool ecp_see_player = true;
    double ecp_alert_timer = 120, ecp_alert_balloon = 1, ecp_alert_event = 0, ecp_return_wait = 180;
    std::string ecp_alert_sound;
    bool ecp_return = true;
    int max_extra_followers = 10;
    double spawner_id = 1000;
    bool stop_event = false, stop_msg = false;
    double default_fade_time = 30;
    std::map<int, double> region_events;
    std::set<int> preloaded_maps;
    std::map<std::string, CopierTemplate> copier_templates;
    std::string copier_pre_copy, copier_post_copy;
    std::map<std::string, CopierTemplate> spawner_templates;
    std::map<std::string, CopierTemplate> morpher_templates;
    int uce_load_game = 62;
    int uce_battle_won = 0, uce_escape = 0, uce_close_menu = 0;
    int uce_boat_enter = 0, uce_boat_exit = 0, uce_ship_enter = 0, uce_ship_exit = 0, uce_airship_enter = 0, uce_airship_exit = 0;
    int tag_common_event = 11, tag_selected_variable = 20, tag_disable_switch = 16;
    double galv_cc_size = 48;
};

const PluginParams& params();

double js_number(const std::string& s);
double js_parse_int(const std::string& s);
double js_parse_float(const std::string& s);
std::string to_upper(std::string s);
std::string to_lower(std::string s);
std::string trim(const std::string& s);
std::string js_format(const std::string& fmt, std::initializer_list<std::string> args);
const Value& yanfly_param(const char* key);
const Value* yanfly_ast(const char* key);
const Value* obj_ast(const Value& obj, const std::string& path);
double yanfly_eval_num(const char* key, double def);

}
