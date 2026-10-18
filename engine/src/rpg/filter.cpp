#include "filter.h"

#include <cmath>
#include <map>

#include "../js/random.h"
#include "../js/value.h"
#include "config.h"
#include "game.h"

namespace rpg {

static const std::map<std::string, std::vector<double>>& default_params() {
    static const std::map<std::string, std::vector<double>> m = {
        {"bulgepinch", {0, 0, 0, 1}},
        {"radialblur", {0, 0, 0, 0, 9}},
        {"godray", {30, 0.5, 2.5, 1.0}},
        {"ascii", {8}},
        {"crosshatch", {}},
        {"dot", {5, 1}},
        {"emboss", {5}},
        {"shockwave", {0, 0, -1, 30, 160, 1}},
        {"twist", {0, 0, 0, 4}},
        {"zoomblur", {0, 0, 0, 0.1}},
        {"noise", {0.5}},
        {"blur", {8, 4}},
        {"oldfilm", {0.5, 0.15, 0.3}},
        {"rgbsplit", {0, 0}},
        {"bloom", {8, 1, 0.5, 1}},
        {"godray-np", {0, 0, 0.5, 2.5, 1.0}},
        {"adjustment", {1, 1, 1, 1, 1, 1, 1, 1}},
        {"pixelate", {1, 1}},
        {"crt", {1, 3, 0.3, 0.2, 1}},
        {"reflection-m", {0.5, 0, 20, 30, 100, 1, 1}},
        {"reflection-w", {0.5, 0, 20, 30, 100, 1, 1}},
        {"motionblur", {0, 0}},
        {"glow", {0, 4, 255, 255, 255}},
        {"displacement", {1, 1, 20}},
    };
    return m;
}

FilterController::FilterController(const std::string& filter_name, const std::string& id_, int target, bool char_num, double ch, bool has_map, int map_id)
    : id(id_), target_obj(target), char_is_number(char_num), character(ch), has_char_map(has_map), char_map_id(map_id) {
    init_filter_param(filter_name);
    init_target();
}

void FilterController::init_filter_param(const std::string& name) {
    filter_type.clear();
    for (char c : name) filter_type += (char)std::tolower((unsigned char)c);
    auto it = default_params().find(filter_type);
    if (it == default_params().end()) {
        filter_type.clear();
        erase_flag = true;
        current_params.clear();
        return;
    }
    current_params = it->second;
    if (filter_type == "godray" || filter_type == "godray-np" || filter_type == "shockwave") addi_time = 0.01;
    else if (filter_type == "oldfilm" || filter_type == "noise") addi_time = 1;
    else if (filter_type == "crt") addi_time = 0.25;
    else if (filter_type == "reflection-m" || filter_type == "reflection-w") addi_time = 0.1;
}

void FilterController::init_target() {
    target_params = current_params;
    duration = 0;
}

void FilterController::update() {
    update_move();
    check_erase();
}

void FilterController::update_move() {
    if (duration <= 0) {
        if (!move_queue.empty()) {
            Queued q = move_queue.front();
            move_queue.erase(move_queue.begin());
            move(q.target, q.has, q.duration);
        } else if (erase_after_move_flag) {
            erase();
        }
    }
    if (duration > 0) {
        double d = duration;
        for (size_t i = 0; i < current_params.size(); i++) current_params[i] = (current_params[i] * (d - 1) + target_params[i]) / d;
        duration--;
    }
    time += addi_time;
}

bool FilterController::is_on_current_map() const { return has_char_map && char_map_id == game_map().map_id(); }

void FilterController::check_erase() {
    if (!char_is_number) return;
    bool map_truthy = has_char_map && char_map_id != 0;
    if (map_truthy && !is_on_current_map()) erase();
    else if (!map_truthy && !party_in_battle()) erase();
}

void FilterController::set(const std::vector<double>& target, const std::vector<bool>& has) {
    clear_move_queue();
    for (size_t i = 0; i < current_params.size(); i++) {
        if (i < target.size() && has[i]) current_params[i] = target[i];
    }
    init_target();
}

void FilterController::move(const std::vector<double>& target, const std::vector<bool>& has, int dur) {
    init_target();
    for (size_t i = 0; i < target_params.size(); i++) {
        if (i < target.size() && has[i]) target_params[i] = target[i];
    }
    duration = dur;
}

void FilterController::move_queue_push(const std::vector<double>& target, const std::vector<bool>& has, int dur) {
    move_queue.push_back(Queued{target, has, dur});
}

void FilterController::char_loc(double& x, double& y) const {
    x = y = 0;
    bool char_truthy = char_is_number && character != 0 && character == character;
    if (!char_truthy) return;
    if (has_char_map && char_map_id != 0) {
        if (character < 0) {
            x = game_player().screen_x();
            y = game_player().screen_y();
        } else if (GameEvent* ev = game_map().event((int)character)) {
            x = ev->screen_x();
            y = ev->screen_y();
        }
        return;
    }
    if (!party_in_battle()) return;
    battler_sprite_position((int)character, x, y);
}

static bool number_or_x(const std::string& s, double& out) {
    if (s.empty() || s == "x") return false;
    double n = js::string_to_number(s);
    if (!std::isnan(n)) {
        out = n;
        return true;
    }
    if (s[0] == 'v') {
        double id = js::string_to_number(s.substr(1));
        if (std::isnan(id)) id = 0;
        out = game_variables().number((int)id);
        return true;
    }
    if (s[0] == 'r') {
        std::string rest = s.substr(1);
        size_t t = rest.find('~');
        std::string a = rest.substr(0, t);
        std::string b = t == std::string::npos ? std::string() : rest.substr(t + 1);
        double n1 = 0, n2 = 0;
        if (!number_or_x(a, n1) || std::isnan(n1)) n1 = 0;
        if (t == std::string::npos || !number_or_x(b, n2) || std::isnan(n2)) n2 = 0;
        if (n1 > n2) std::swap(n1, n2);
        out = rng::next() * (n2 - n1) + n1;
        return true;
    }
    return false;
}

FilterArgs filter_param_process(const std::vector<std::string>& args) {
    FilterArgs r;
    for (auto& a : args) {
        double v = 0;
        bool ok = number_or_x(a, v);
        r.values.push_back(v);
        r.has.push_back(ok);
    }
    return r;
}

bool filters_enabled_all() { return config().filters_enabled; }

}
