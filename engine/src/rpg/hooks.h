#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "../js/value.h"

namespace rpg {

class GameBattler;
class FilterController;
class GameScreen;

namespace scenes {

bool is_map();
bool is_battle();
void goto_title();
void goto_gameover();
void push_menu();
void push_save();
void push_shop(const Value& goods, std::shared_ptr<const void> keep, bool purchase_only);
void push_name_input(int actor_id, int max_chars);
void push_battle();
void goto_battle();
void push_omori_file(bool save, bool load);
void push_photo_album(int item_id, int interface_mode);
void push_black_letter_menu(bool forced);
void push_quest_menu();
void push_slot_machine();
void push_gacha();
void push_gacha_book();
bool is_input_window_active();
void show_name_input_windows(const std::string& name, int max);
void start_qte_smash(const js::Val& segments, const js::Val& time, const js::Val& width);
void start_qte_stop_the_arrow(const js::Val& direction, const js::Val& delay, const js::Val& x, const js::Val& y);
void start_qte_hit_the_mark(const js::Val& speed, int index, const js::Val& x, const js::Val& y);
void show_pizza_note(const std::string& text);
void refresh_mini_labels();
void reset_skip_cutscene_duration();
void play_movie(const std::string& name);
bool is_movie_playing();
void add_map_fog(const std::string& id);
void setup_ambience_bitmaps(const std::string& name);
void update_picture_custom_frame(int picture_id);
void snap_for_shop();
void remove_filter(FilterController* f);

}

namespace video {
void load(const std::string& name);
void release(const std::string& name);
void create(const std::string& name, const std::string& id);
void play(const std::string& id);
void stop(const std::string& id);
void set_loop(const std::string& id);
bool is_ready();
bool has_finished(const std::string& id);
bool exists(const std::string& id);
void pause_all();
void play_all();
}

namespace tddp_ff {
extern bool text_ff_disabled;
extern bool next_text_ff_disabled;
bool is_text_ff_disabled();
bool is_route_ff_disabled();
}

namespace slot_machine {
extern double expectation;
extern int scale;
}

void weather_plugin_command(const std::string& command, const std::vector<std::string>& args);
void weather_screen_clear(GameScreen& screen);
void weather_screen_update(GameScreen& screen);

}
