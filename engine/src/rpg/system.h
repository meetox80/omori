#pragma once
#include <map>
#include <string>
#include <vector>

#include <memory>

#include "state.h"

namespace rpg {

class GameEvent;

struct MorphTarget {
    int map_id = 0;
    int event_id = 0;
};

struct SavedLocation {
    int x = 0, y = 0, d = 2;
};

class GameSystem : public js::JsObject {
public:
    GameSystem();
    js::HostClass* js_class() const override;
    bool save_enabled = true;
    bool menu_enabled = true;
    bool encounter_enabled = true;
    bool formation_enabled = true;
    int battle_count = 0;
    int win_count = 0;
    int escape_count = 0;
    int save_count = 0;
    int version_id = 0;
    double frames_on_save = 0;
    AudioObj bgm_on_save, bgs_on_save;
    bool has_window_tone = false;
    double window_tone_[4] = {0, 0, 0, 0};
    AudioObj battle_bgm_, victory_me_, defeat_me_, saved_bgm, walking_bgm;
    std::string battle_system = "dtb";
    bool act_seq_bypass_damage_cap = false;
    bool has_act_seq_damage_cap = false;
    double act_seq_damage_cap = 0;
    double damage_rate = 1.0, flat_damage = 0, heal_rate = 1.0, flat_heal = 0;
    bool default_damage_cap = true;
    bool has_new_damage_cap = false, has_new_healing_cap = false;
    double new_damage_cap = 0, new_healing_cap = 0;
    double crit_mult = 1.0, flat_crit = 0;
    bool force_critical = false, force_no_critical = false;
    bool show_event_mini_label = true;
    bool disable_dashing = false;
    bool footsteps_enabled = true;
    std::map<std::string, SavedLocation> saved_event_locations;
    bool word_wrap_ = true;
    bool fast_forward = false;
    std::string msg_font_name;
    int msg_font_size = 28;
    int msg_font_outline = 4;
    std::string message_rows_expr;
    std::string message_width_expr;
    bool msg_sound_enable = false;
    std::string msg_sound_name;
    double msg_sound_vol = 100, msg_sound_pitch = 100, msg_sound_pitch_var = 0, msg_sound_pan = 0, msg_sound_pan_var = 0;
    int msg_sound_interval = 2;
    int msg_choice_max = 8;
    std::vector<int> hide_choices, disable_choices;
    bool choice_show_initialized = false, choice_enable_initialized = false;
    js::Val msg_position_x, msg_position_y;
    double msg_anchor_x = 0, msg_anchor_y = 1;
    int original_magical_seed = 0;
    int magical_seed = 0;
    double playtime_ms = 0;
    std::map<std::string, MorphTarget> preserved_morphs;
    std::map<int, std::vector<std::shared_ptr<GameEvent>>> map_spawned_events;
    bool has_gacha_book = false;
    std::map<int, bool> gacha_book[3];
    bool has_gacha = false;
    std::map<int, bool> gacha[3];

    void add_to_gacha(const std::string& type, int id);
    void remove_from_gacha(const std::string& type, int id);
    void clear_gacha();
    bool is_in_gacha(const std::string& type, int id) const;
    void init_message_sounds();
    void init_message_font_settings();
    void set_new_damage_cap(double value, bool damage);
    void reset_damage_settings();
    double maximum_damage() const;
    double maximum_healing() const;
    void add_to_gacha_book(const std::string& type, int id);
    void remove_from_gacha_book(const std::string& type, int id);
    void clear_gacha_book();
    void complete_gacha_book();
    bool is_in_gacha_book(const std::string& type, int id) const;
    double window_tone(int i) const;
    const MorphTarget* get_preserved_event_morph(int map_id, int event_id) const;
    void log_preserved_event_morph_raw(int target_map, int target_event, int map_id, int event_id);
    void remove_preserved_event_morph(int map_id, int event_id);
    std::vector<std::shared_ptr<GameEvent>>& get_map_spawned_event_data(int map_id);
    void remove_temporary_map_spawned_events(int map_id);
    AudioObj battle_bgm() const;
    AudioObj victory_me() const;
    AudioObj defeat_me() const;
    void on_battle_start() { battle_count++; }
    void on_battle_win();
    void on_battle_escape();
    void on_before_save();
    void on_after_load();
    int playtime() const;
    std::string playtime_text() const;
    void save_bgm();
    void replay_bgm();
    void save_walking_bgm();
    void replay_walking_bgm();
    void save_walking_bgm2();
    int message_rows() const;
    double message_width() const;
    bool word_wrap() const { return word_wrap_; }
    js::Val get_message_sound();
    void init_choice_show();
    void init_choice_enable();
    bool is_choice_shown(int id);
    bool is_choice_enabled(int id);
    void hide_choice(int id);
    void show_choice(int id);
    void disable_choice(int id);
    void enable_choice(int id);
    void clear_choice_settings();
    void init_message_position();
    double get_magical_seed(double offset = 0, bool flt = true);
    void grow_magical_seed();
    void replant_magical_seed(int seed);
    bool is_saved_event_location(int map, int event) const;
    void save_event_location(int map, int event, int x, int y, int d);
    void unlock_achievement(const std::string& code);
    void create_horror_effect(const std::string& type);
    void remove_horror_effect(const std::string& type);
    void set_horror_effect_to_value(const std::string& type, const std::string& key, const js::Val& v);
    js::Val horror_filters();
    void clear_horror_effects();
};

namespace playtime_manager {
double snap();
void clear();
double save_time();
extern double accumulator;
}

}
