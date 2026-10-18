#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "../core/value.h"
#include "../js/bind.h"
#include "../js/value.h"

namespace rpg {

struct AudioObj {
    std::string name;
    double volume = 90;
    double pitch = 100;
    double pan = 0;
    double pos = 0;
    bool valid = false;
    static AudioObj from(const Value& v);
    js::Val to_js() const;
    static AudioObj from_js(const js::Val& v);
};

class GameTemp : public js::JsObject {
public:
    GameTemp();
    js::HostClass* js_class() const override;
    bool is_playtest() const { return false; }
    void reserve_common_event(int id) { common_event_id = id; }
    void clear_common_event();
    bool is_common_event_reserved() const;
    const Value* reserved_common_event_list() const;
    void set_destination(int x, int y) {
        destination_x = x;
        destination_y = y;
        destination_valid = true;
    }
    void clear_destination() { destination_valid = false; }
    bool is_destination_valid() const { return destination_valid; }
    void force_action_queue(const js::Val& command);
    bool is_common_event_for_battle_only(int id = -1) const;
    void clear_shop_data();
    int common_event_id = 0;
    int destination_x = 0, destination_y = 0;
    bool destination_valid = false;
    bool stop_map_events = false;
    bool stop_map_player = false;
    bool move_allow_player_collision = false;
    int load_location_direction = 0;
    bool bypass_load_location = false;
    bool death_state_clear = false;
    bool recover_all_clear = false;
    bool custom_leave_effect_eval = false;
    bool is_gathering_aura_data = false;
    bool check_all_alive_members = false;
    bool taunt = false;
    bool taunt_mode = false;
    class GameAction* taunt_action = nullptr;
    js::Val force_action_queue_list;
};

class GameSwitches : public js::JsObject {
public:
    js::HostClass* js_class() const override;
    GameSwitches() { clear(); }
    void clear() { data_.clear(); }
    bool value(int id) const { return id >= 0 && id < (int)data_.size() && data_[id]; }
    void set_value(int id, bool v);
    std::vector<uint8_t>& raw() { return data_; }

private:
    std::vector<uint8_t> data_;
};

class GameVariables : public js::JsObject {
public:
    js::HostClass* js_class() const override;
    GameVariables() { clear(); }
    void clear() { data_.clear(); }
    js::Val value(int id) const;
    double number(int id) const;
    void set_value(int id, js::Val v);
    void set_number(int id, double v) { set_value(id, js::Val::num(v)); }
    std::vector<js::Val>& raw() { return data_; }

private:
    std::vector<js::Val> data_;
};

class GameSelfSwitches : public js::JsObject {
public:
    js::HostClass* js_class() const override;
    static std::string key(int map, int event, const std::string& letter);
    void clear() { data_.clear(); }
    bool value(const std::string& key) const { return data_.count(key) != 0; }
    bool value(int map, int event, const std::string& letter) const { return value(key(map, event, letter)); }
    void set_value(const std::string& key, bool v);
    void set_value(int map, int event, const std::string& letter, bool v) { set_value(key(map, event, letter), v); }
    std::map<std::string, bool>& raw() { return data_; }

private:
    std::map<std::string, bool> data_;
};

class GameSelfVariables : public js::JsObject {
public:
    js::HostClass* js_class() const override;
    void clear() { data_.clear(); }
    js::Val value(const std::string& key) const;
    void set_value(const std::string& key, js::Val v);
    std::map<std::string, js::Val>& raw() { return data_; }

private:
    std::map<std::string, js::Val> data_;
};

class GameEventData : public js::JsObject {
public:
    js::HostClass* js_class() const override;
    void clear_all() { data_.clear(); }
    void add_category(const std::string& c) {
        if (!c.empty() && !data_.count(c)) data_[c];
    }
    void remove_category(const std::string& c) { data_.erase(c); }
    void clear(const std::string& c) {
        if (!c.empty()) data_[c].clear();
    }
    bool has_category(const std::string& c) const { return !c.empty() && data_.count(c) != 0; }
    js::Val value(const std::string& c, const std::string& key) const;
    void set_value(const std::string& c, const std::string& key, const js::Val& v);
    void set_value_ext(const std::string& c, int map, int event, const js::Val& v) {
        if (!c.empty() && map && event) set_value(c, std::to_string(map) + "," + std::to_string(event), v);
    }
    std::map<std::string, std::map<std::string, js::Val>>& raw() { return data_; }

private:
    std::map<std::string, std::map<std::string, js::Val>> data_;
};

class GameTimer : public js::JsObject {
public:
    js::HostClass* js_class() const override;
    void update(bool scene_active);
    void start(int count);
    void stop();
    bool is_working() const { return working; }
    int seconds() const { return frames / 60; }
    void on_expire();
    void pause();
    void resume();
    void gain_frames(int v);
    void change_direction(int v);
    int frames = 0;
    bool working = false;
    bool paused = false;
    int direction = -1;
};

struct ExtraFace {
    std::string face_name;
    int face_index = 0;
    std::string color;
    bool has_color = false;
};

class GameMessage : public js::JsObject {
public:
    GameMessage() { clear(); }
    js::HostClass* js_class() const override;
    void clear();
    std::vector<std::string> texts;
    std::vector<std::string> choices;
    std::string face_name;
    int face_index = 0;
    int background = 0;
    int position_type = 2;
    int choice_default_type = 0;
    int choice_cancel_type = 0;
    int choice_background = 0;
    int choice_position_type = 2;
    int num_input_variable_id = 0;
    int num_input_max_digits = 0;
    int item_choice_variable_id = 0;
    int item_choice_itype_id = 0;
    bool scroll_mode = false;
    int scroll_speed = 2;
    bool scroll_no_fast = false;
    std::function<void(int)> choice_callback;
    std::vector<ExtraFace> extra_faces;
    std::string face_background_color;
    bool has_face_background_color = false;
    std::vector<std::string> choice_labels;

    void add(const std::string& t) { texts.push_back(t); }
    void add_text(const std::string& t);
    void set_face_image(const std::string& n, int i) {
        face_name = n;
        face_index = i;
    }
    void set_choices(const std::vector<std::string>& c, int def, int cancel) {
        choices = c;
        choice_default_type = def;
        choice_cancel_type = cancel;
    }
    void set_number_input(int var, int digits) {
        num_input_variable_id = var;
        num_input_max_digits = digits;
    }
    void set_item_choice(int var, int itype) {
        item_choice_variable_id = var;
        item_choice_itype_id = itype;
    }
    void set_scroll(int speed, bool no_fast) {
        scroll_mode = true;
        scroll_speed = speed;
        scroll_no_fast = no_fast;
    }
    void on_choice(int n);
    bool has_text() const { return !texts.empty(); }
    bool is_choice() const { return !choices.empty(); }
    bool is_number_input() const { return num_input_variable_id > 0; }
    bool is_item_choice() const { return item_choice_variable_id > 0; }
    bool is_busy() const { return has_text() || is_choice() || is_number_input() || is_item_choice(); }
    void new_page();
    std::string all_text() const;
    void show_language_message(const std::string& code);
    static std::string make_face_background_color(const Value& color, const std::string& name, int index, bool& has);
    void set_extra_face(int index, const std::string& name, int face_index, const std::string& color, bool has_color);
    int face_count() const { return (face_name.empty() ? 0 : 1) + (int)extra_faces.size(); }
};

}
