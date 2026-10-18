#pragma once
#include <cstdint>
#include <string>

#include "../core/value.h"
#include "character.h"
#include "map.h"
#include "screen.h"
#include "state.h"
#include "system.h"

namespace rpg {

class GameActors;
class GameParty;
class GameTroop;

GameTemp& game_temp();
GameSystem& game_system();
GameScreen& game_screen();
GameTimer& game_timer();
GameMessage& game_message();
GameSwitches& game_switches();
GameVariables& game_variables();
GameSelfSwitches& game_self_switches();
GameSelfVariables& game_self_variables();
GameEventData& game_event_data();
GameActors& game_actors();
GameParty& game_party();
GameTroop& game_troop();
GameMap& game_map();
GamePlayer& game_player();

void create_game_objects();
void destroy_game_objects();

uint64_t frame_count();
void set_frame_count(uint64_t f);
const Value& current_map_data();
bool party_in_battle();
double battle_camera_x();
double battle_camera_y();
void filter_con_update();
void timer_expire_hook();
void after_load_hook();
void battler_sprite_position(int index, double& x, double& y);
bool map_character_tag_active();
bool map_pizza_note_visible();
bool map_can_use_character_tag();
void map_create_spawned_event(GameEvent* ev);
bool meta_has(const std::string& note, const std::string& key, std::string* value = nullptr);
void script_error(const char* where, const std::string& msg);
double image_logical_height(const char* folder, const std::string& name);
void scene_add_footprint(std::shared_ptr<GameFootprint> f);
js::Val switches_proxy();
js::Val variables_proxy();
bool game_objects_exist();
bool is_text_in_current_language(const std::string& text);
js::Val game_screen_js();
js::Val game_system_js();
js::Val game_timer_js();
js::Val game_temp_js();
js::Val game_message_js();
js::Val picture_js(int id);
void install_bindings();

}
