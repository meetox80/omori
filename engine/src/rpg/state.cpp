#include "state.h"

#include <cmath>
#include <cstdio>

#include "../js/vm.h"
#include "data.h"
#include "game.h"

namespace rpg {

AudioObj AudioObj::from(const Value& v) {
    AudioObj a;
    if (!v.is_obj()) return a;
    a.valid = true;
    a.name = v["name"].string();
    a.volume = v["volume"].num(90);
    a.pitch = v["pitch"].num(100);
    a.pan = v["pan"].num(0);
    a.pos = v["pos"].num(0);
    return a;
}

js::Val AudioObj::to_js() const {
    if (!valid) return js::Val::null();
    js::Val o = js::make_object();
    o.o->set(js::atom("name"), js::Val::str(name));
    o.o->set(js::atom("volume"), js::Val::num(volume));
    o.o->set(js::atom("pitch"), js::Val::num(pitch));
    o.o->set(js::atom("pan"), js::Val::num(pan));
    o.o->set(js::atom("pos"), js::Val::num(pos));
    return o;
}

AudioObj AudioObj::from_js(const js::Val& v) {
    AudioObj a;
    if (!v.is_obj()) return a;
    a.valid = true;
    js::Vm& vm = js::vm();
    a.name = vm.get(v, js::atom("name")).to_str();
    js::Val vol = vm.get(v, js::atom("volume"));
    js::Val pit = vm.get(v, js::atom("pitch"));
    js::Val pan = vm.get(v, js::atom("pan"));
    js::Val pos = vm.get(v, js::atom("pos"));
    a.volume = vol.is_undef() ? 90 : vol.to_num();
    a.pitch = pit.is_undef() ? 100 : pit.to_num();
    a.pan = pan.is_undef() ? 0 : pan.to_num();
    a.pos = pos.is_undef() ? 0 : pos.to_num();
    return a;
}

GameTemp::GameTemp() { clear_shop_data(); }

void GameTemp::clear_common_event() {
    force_action_queue_list = js::Val();
    common_event_id = 0;
}

bool GameTemp::is_common_event_reserved() const {
    if (force_action_queue_list.truthy()) return true;
    return common_event_id > 0;
}

const Value* GameTemp::reserved_common_event_list() const {
    const Value& ce = data::common_events()[common_event_id];
    return ce.is_obj() ? &ce["list"] : nullptr;
}

void GameTemp::force_action_queue(const js::Val& command) {
    if (!force_action_queue_list.truthy()) force_action_queue_list = js::make_array();
    force_action_queue_list.o->items.push_back(command);
}

bool GameTemp::is_common_event_for_battle_only(int id) const {
    if (id < 0) id = common_event_id;
    const Value& ce = data::common_events()[id];
    if (!ce.is_obj()) return false;
    std::string name = ce["name"].string();
    for (auto& c : name) c = (char)toupper((unsigned char)c);
    return name.find("[BATTLEONLY]") != std::string::npos;
}

void GameTemp::clear_shop_data() {
    js::Val shop = js::make_object();
    shop.o->set(js::atom("name"), js::Val::str(""));
    shop.o->set(js::atom("shopType"), js::Val::num(0));
    shop.o->set(js::atom("goods"), js::make_array());
    expando.set(js::atom("_shopData"), shop);
}

void GameSwitches::set_value(int id, bool v) {
    if (id > 0 && id < data::system()["switchCount"].as_int()) {
        if ((int)data_.size() <= id) data_.resize((size_t)id + 1, 0);
        data_[id] = v ? 1 : 0;
        game_map().request_refresh();
    }
}

js::Val GameVariables::value(int id) const {
    if (id < 0 || id >= (int)data_.size()) return js::Val::num(0);
    const js::Val& v = data_[id];
    return v.truthy() ? v : js::Val::num(0);
}

double GameVariables::number(int id) const { return value(id).to_num(); }

void GameVariables::set_value(int id, js::Val v) {
    if (id > 0 && id < data::system()["variableCount"].as_int()) {
        if (v.is_num()) v = js::Val::num(std::round(v.n * 1000 + 0.0000001) / 1000);
        if ((int)data_.size() <= id) data_.resize((size_t)id + 1);
        data_[id] = std::move(v);
        game_map().request_refresh();
    }
}

std::string GameSelfSwitches::key(int map, int event, const std::string& letter) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%d,%d,", map, event);
    return buf + letter;
}

void GameSelfSwitches::set_value(const std::string& k, bool v) {
    if (v) data_[k] = true;
    else data_.erase(k);
    game_map().request_refresh();
}

js::Val GameSelfVariables::value(const std::string& key) const {
    auto it = data_.find(key);
    if (it == data_.end() || !it->second.truthy()) return js::Val::num(0);
    return it->second;
}

void GameSelfVariables::set_value(const std::string& key, js::Val v) {
    if (v.truthy()) {
        if (v.is_num()) v = js::Val::num(std::floor(v.n));
        data_[key] = std::move(v);
    } else {
        data_.erase(key);
    }
    game_map().request_refresh();
}

js::Val GameEventData::value(const std::string& c, const std::string& key) const {
    auto it = data_.find(c);
    if (it == data_.end()) return js::Val();
    auto k = it->second.find(key);
    return k == it->second.end() ? js::Val() : k->second;
}

void GameEventData::set_value(const std::string& c, const std::string& key, const js::Val& v) {
    if (v.truthy()) data_[c][key] = v;
    else data_[c].erase(key);
    game_map().request_refresh();
}

void GameTimer::update(bool scene_active) {
    if (!scene_active || !working || paused || frames <= 0) return;
    frames += direction;
    if (frames <= 0) on_expire();
}

void GameTimer::start(int count) {
    frames = count;
    working = true;
    paused = false;
}

void GameTimer::stop() {
    working = false;
    paused = false;
}

void GameTimer::on_expire() {
    timer_expire_hook();
}

void GameTimer::pause() {
    if (frames <= 0) return;
    paused = true;
    working = true;
}

void GameTimer::resume() {
    if (frames <= 0) return;
    paused = false;
    working = true;
}

void GameTimer::gain_frames(int v) {
    frames += v;
    working = true;
}

void GameTimer::change_direction(int v) {
    direction = v;
    working = true;
    if (v > 0) frames = std::max(frames, 1);
}

void GameMessage::clear() {
    texts.clear();
    choices.clear();
    face_name.clear();
    face_index = 0;
    background = 0;
    position_type = 2;
    choice_default_type = 0;
    choice_cancel_type = 0;
    choice_background = 0;
    choice_position_type = 2;
    num_input_variable_id = 0;
    num_input_max_digits = 0;
    item_choice_variable_id = 0;
    item_choice_itype_id = 0;
    scroll_mode = false;
    scroll_speed = 2;
    scroll_no_fast = false;
    choice_callback = nullptr;
    extra_faces.clear();
    face_background_color.clear();
    has_face_background_color = false;
}

void GameMessage::add_text(const std::string& t) {
    if (game_system().word_wrap()) add("<WordWrap>" + t);
    else add(t);
}

void GameMessage::on_choice(int n) {
    if (choice_callback) {
        auto cb = choice_callback;
        choice_callback = nullptr;
        cb(n);
    }
}

void GameMessage::new_page() {
    if (!texts.empty()) texts.back() += '\f';
}

std::string GameMessage::all_text() const {
    std::string out;
    for (size_t i = 0; i < texts.size(); i++) {
        if (i) out += '\n';
        out += texts[i];
    }
    return out;
}

std::string GameMessage::make_face_background_color(const Value& color, const std::string& name, int index, bool& has) {
    (void)index;
    has = false;
    if (color.is_str()) {
        std::string c = color.string();
        if (c.rfind("rgba", 0) == 0 || c.rfind("#", 0) == 0) {
            has = true;
            return c;
        }
    }
    bool face_name_flag = color.is_str() && color.string() == "FaceName";
    if ((!name.empty() && face_name_flag) || color.is_null()) {
        if (name == "04_HERO_OW") {
            has = true;
            return "#52b9fc";
        }
    }
    return "";
}

void GameMessage::set_extra_face(int index, const std::string& name, int fi, const std::string& color, bool has_color) {
    if ((int)extra_faces.size() <= index) extra_faces.resize((size_t)index + 1);
    extra_faces[index].face_name = name;
    extra_faces[index].face_index = fi;
    extra_faces[index].color = color;
    extra_faces[index].has_color = has_color;
}

void GameMessage::show_language_message(const std::string& code) {
    std::shared_ptr<Document> keep = data::lang_file(code.substr(0, code.find('.')));
    const Value& d = data::lang_entry(code);
    std::string faceset = d["faceset"].truthy() ? d["faceset"].string() : std::string();
    int faceindex = d["faceindex"].truthy() ? d["faceindex"].as_int() : 0;
    int bg = d["background"].truthy() ? d["background"].as_int() : 0;
    const Value* pos = d.find("position");
    int position = (!pos || pos->is_null()) ? 2 : pos->as_int();
    if (pos && pos->is_null()) position = 0;
    const Value& extra = d["extraFaces"];
    if (extra.truthy()) {
        for (size_t i = 0; i < extra.size(); i++) {
            const Value& f = extra[i];
            bool has;
            std::string c = make_face_background_color(f["faceBackgroundColor"], f["faceset"].string(), f["faceindex"].as_int(), has);
            set_extra_face((int)i, f["faceset"].string(), f["faceindex"].as_int(), c, has);
        }
    }
    set_face_image(faceset, faceindex);
    background = bg;
    position_type = position;
    face_background_color = make_face_background_color(d["faceBackgroundColor"], faceset, faceindex, has_face_background_color);
    const Value* text = d.find("text");
    add_text(text ? data::js_string(*text) : std::string("undefined"));
}

}
