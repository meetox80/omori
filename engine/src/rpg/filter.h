#pragma once
#include <string>
#include <vector>

namespace rpg {

class FilterController {
public:
    FilterController(const std::string& filter_name, const std::string& id, int target_obj, bool char_is_number, double character, bool has_map, int map_id);
    std::string filter_type;
    bool erase_flag = false;
    bool erase_after_move_flag = false;
    struct Queued {
        std::vector<double> target;
        std::vector<bool> has;
        int duration = 1;
    };
    std::vector<Queued> move_queue;
    double time = 0;
    double addi_time = 0;
    std::string id;
    int target_obj = 0;
    bool char_is_number = false;
    double character = 0;
    bool has_char_map = false;
    int char_map_id = 0;
    std::vector<double> current_params;
    std::vector<double> target_params;
    bool enabled = true;
    int duration = 0;

    void init_filter_param(const std::string& name);
    void init_target();
    void update();
    void update_move();
    void check_erase();
    void set(const std::vector<double>& target, const std::vector<bool>& has);
    void enable(bool e) { enabled = e; }
    void move(const std::vector<double>& target, const std::vector<bool>& has, int duration);
    void move_queue_push(const std::vector<double>& target, const std::vector<bool>& has, int duration);
    void clear_move_queue() { move_queue.clear(); }
    void erase() { erase_flag = true; }
    void erase_after_move() { erase_after_move_flag = true; }
    bool is_on_current_map() const;
    bool is_map_event_only() const { return char_is_number && character != 0 && character == character && has_char_map && char_map_id != 0; }
    bool is_battle_only() const { return char_is_number && character != 0 && character == character && !(has_char_map && char_map_id != 0); }
    void set_addi_time(double t) { addi_time = t; }
    void char_loc(double& x, double& y) const;
};

struct FilterArgs {
    std::vector<double> values;
    std::vector<bool> has;
};

FilterArgs filter_param_process(const std::vector<std::string>& args);
bool filters_enabled_all();

}
