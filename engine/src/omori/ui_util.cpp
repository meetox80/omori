#include "ui_util.h"

#include <cmath>
#include <cstdio>

#include "../core/regex.h"
#include "../js/vm.h"
#include "../rpg/audio_manager.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/params.h"
#include "../rpg/party.h"

namespace ui {

const Value& yparam(const char* key) { return rpg::yanfly_param(key); }
double yparam_num(const char* key, double def) {
    const Value& v = yparam(key);
    if (v.is_null()) return def;
    if (v.is_str()) return rpg::js_number(v.string());
    return v.num(def);
}
bool yparam_bool(const char* key) { return yparam(key).truthy(); }
std::string yparam_str(const char* key) { return data::js_string(yparam(key)); }

const Value& lang_data(const std::string& code) { return data::lang_entry(code); }

std::shared_ptr<const void> lang_keep(const std::string& code) {
    size_t dot = code.find('.');
    return data::lang_file(dot == std::string::npos ? code : code.substr(0, dot));
}

std::string lang_message_text(const std::string& code) { return data::lang_text(code); }

const Value& system_lang() {
    auto doc = data::lang_file("System");
    return doc ? doc->root() : Value::null_value();
}

std::string database_text(const std::string& key) {
    auto doc = data::lang_file("Database");
    if (!doc) return "- ERROR -";
    const Value* v = doc->root().find(key.c_str());
    return v ? data::js_string(*v) : std::string("undefined");
}

std::string system_text(const std::string& type, const std::string& name) {
    const Value& s = system_lang();
    if (s.is_null()) return "- ERROR -";
    return data::js_string(s["terms"][type.c_str()][name.c_str()]);
}

std::string plugin_text(const std::string& type, const std::string& name) {
    const Value& s = system_lang();
    if (s.is_null()) return " - ERROR -";
    return data::js_string(s["plugins"][type.c_str()][name.c_str()]);
}

std::string replace_database_codes(std::string text) {
    re::Groups g;
    while (re::search("\\{(.*?)\\}", "", text, &g)) {
        std::string rep = database_text(g[1]);
        text = text.substr(0, g.index) + rep + text.substr(g.index + g.length);
    }
    return text;
}

g2d::BitmapPtr load_folder(const std::string& folder, const std::string& name) {
    if (name.empty()) return g2d::Bitmap::create(0, 0);
    return g2d::Bitmap::load("img/" + folder + "/" + name);
}

g2d::BitmapPtr load_system(const std::string& name) { return load_folder("system", name); }
g2d::BitmapPtr load_picture(const std::string& name) { return load_folder("pictures", name); }
g2d::BitmapPtr load_face(const std::string& name) { return load_folder("faces", name); }
g2d::BitmapPtr load_character(const std::string& name) { return load_folder("characters", name); }
g2d::BitmapPtr load_atlas(const std::string& name) { return load_folder("atlases", name); }
g2d::BitmapPtr load_parallax(const std::string& name) { return load_folder("parallaxes", name); }
g2d::BitmapPtr load_battleback1(const std::string& name) { return load_folder("battlebacks1", name); }
g2d::BitmapPtr load_battleback2(const std::string& name) { return load_folder("battlebacks2", name); }
g2d::BitmapPtr load_enemy(const std::string& name) { return load_folder("enemies", name); }
g2d::BitmapPtr load_sv_enemy(const std::string& name) { return load_folder("sv_enemies", name); }
g2d::BitmapPtr load_animation(const std::string& name) { return load_folder("animations", name); }
g2d::BitmapPtr load_title1(const std::string& name) { return load_folder("titles1", name); }
g2d::BitmapPtr load_title2(const std::string& name) { return load_folder("titles2", name); }

static std::string sign_of(const std::string& name) {
    size_t i = 0;
    while (i < name.size() && (name[i] == '!' || name[i] == '$')) i++;
    return name.substr(0, i);
}

bool is_big_character(const std::string& name) { return sign_of(name).find('$') != std::string::npos; }
bool is_object_character(const std::string& name) { return sign_of(name).find('!') != std::string::npos; }

void play_cursor() { rpg::audio_manager::play_system_sound(0); }
void play_ok() { rpg::audio_manager::play_system_sound(1); }
void play_cancel() { rpg::audio_manager::play_system_sound(2); }
void play_buzzer() { rpg::audio_manager::play_system_sound(3); }
void play_equip() { rpg::audio_manager::play_system_sound(4); }
void play_save() { rpg::audio_manager::play_system_sound(5); }
void play_load() { rpg::audio_manager::play_system_sound(6); }
void play_shop() { rpg::audio_manager::play_system_sound(21); }

void play_se(const std::string& name, double volume, double pitch, double pan) {
    rpg::AudioObj a;
    a.valid = true;
    a.name = name;
    a.volume = volume;
    a.pitch = pitch;
    a.pan = pan;
    rpg::audio_manager::play_se(a);
}

std::string to_js_string(const js::Val& v) { return v.to_str(); }
std::string number_text(double n) { return js::number_to_string(n); }
std::string group_digits(double n) { return js::number_to_string(n); }
int current_world() { return rpg::current_world_index(); }

std::string input_key_code(const std::string& input) {
    static const struct {
        const char* input;
        const char* key;
    } map[] = {{"ok", "0"},  {"escape", "1"}, {"shift", "2"}, {"tag", "3"},    {"pageup", "4"}, {"pagedown", "5"},
               {"tab", "8"}, {"up", "12"},    {"down", "13"}, {"left", "14"}, {"right", "15"}};
    for (auto& m : map) {
        if (input == m.input) return m.key;
    }
    return "";
}

static const char* kIconType = "gamepadBlack24";
static const double kSonyOffset = 72;

void key_icon_rects(const std::string& key, IconRect& up, IconRect& down, const std::string& type) {
    std::string t = type.empty() ? std::string(kIconType) : type;
    const Value& data = system_lang()["InputIcons"][t.c_str()];
    const Value& rects = data["rects"];
    const Value* kk = key.empty() ? nullptr : rects.find(key.c_str());
    bool found = kk && kk->is_obj();
    auto rd = [](const Value& r, IconRect& out) {
        out.x = r["x"].num();
        out.y = r["y"].num();
        out.w = r["width"].num(24);
        out.h = r["height"].num(24);
    };
    if (found) {
        rd((*kk)["up"], up);
        rd((*kk)["down"], down);
    } else {
        up = IconRect{0, 0, 24, 24};
        down = IconRect{24, 0, 24, 24};
    }
    if (t == "gamepadBlack24" && found) {
        up.x -= kSonyOffset;
        down.x -= kSonyOffset;
    }
}

g2d::BitmapPtr input_icon_bitmap() {
    const Value& data = system_lang()["InputIcons"][kIconType];
    return load_system("Input/" + data["source"].string());
}

void draw_aligned_key_icon(g2d::Bitmap* dst, const std::string& key, double x, double y, double width, double height, const std::string& align, int side) {
    IconRect up, down;
    key_icon_rects(key, up, down);
    IconRect r = side == 0 ? up : down;
    double cx = 0, cy = (height - r.h) / 2;
    if (align == "center") cx = (width - r.w) / 2;
    else if (align == "right") cx = width - r.w;
    g2d::BitmapPtr src = input_icon_bitmap();
    dst->blt(src.get(), r.x, r.y, r.w, r.h, x + cx, y + cy);
}

void draw_key_icon(g2d::Bitmap* dst, const std::string& key, double x, double y, int side) {
    IconRect up, down;
    key_icon_rects(key, up, down);
    IconRect r = side == 0 ? up : down;
    g2d::BitmapPtr src = input_icon_bitmap();
    dst->blt(src.get(), r.x, r.y, r.w, r.h, x, y);
}

}
