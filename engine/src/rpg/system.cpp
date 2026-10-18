#include "system.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "../js/random.h"
#include "../js/vm.h"
#include "../platform/platform.h"
#include "audio_manager.h"
#include "data.h"
#include "game.h"
#include "params.h"

namespace rpg {

namespace playtime_manager {
double accumulator = 0;
double snap() { return std::floor(plat::time_seconds() * 1000.0); }
void clear() { accumulator = snap(); }
double save_time() { return snap() - accumulator; }
}

static js::Val str_array(std::initializer_list<const char*> items) {
    std::vector<js::Val> v;
    for (const char* s : items) v.push_back(js::Val::str(s));
    return js::make_array(std::move(v));
}

static double param_num(const char* plugin, const char* key, double def) {
    std::string s = data::plugin_param(plugin, key, "");
    if (s.empty()) return def;
    return std::atof(s.c_str());
}

static bool param_bool(const char* plugin, const char* key, bool def) {
    std::string s = data::plugin_param(plugin, key, "");
    if (s.empty()) return def;
    return s == "true";
}

GameSystem::GameSystem() {
    battle_system = "dtb";
    default_damage_cap = param_bool("YEP_DamageCore", "Enable Cap", true);
    show_event_mini_label = param_bool("YEP_EventMiniLabel", "Default Show", true);
    word_wrap_ = param_bool("YEP_MessageCore", "Word Wrapping", true);
    fast_forward = param_bool("YEP_MessageCore", "Enable Fast Forward", false);
    init_message_font_settings();
    init_message_sounds();
    msg_choice_max = (int)param_num("YEP_X_ExtMesPack1", "Max Rows", 8);
    init_message_position();
    original_magical_seed = rng::below(100);
    magical_seed = original_magical_seed;
    expando.set(js::atom("_washingDishes"), js::Val::boolean(false));
    expando.set(js::atom("_dishList"),
                str_array({"farawaytown_extras_misc.message_32", "farawaytown_extras_misc.message_33", "farawaytown_extras_misc.message_34",
                           "farawaytown_extras_misc.message_35", "farawaytown_extras_misc.message_36", "farawaytown_extras_misc.message_37",
                           "farawaytown_extras_misc.message_39", "farawaytown_extras_misc.message_40", "farawaytown_extras_misc.message_41",
                           "farawaytown_extras_misc.message_42", "farawaytown_extras_misc.message_43", "farawaytown_extras_misc.message_44",
                           "farawaytown_extras_misc.message_45"}));
    {
        js::Val list = *expando.find(js::atom("_dishList"));
        auto& items = list.o->items;
        for (size_t cur = items.size(); cur != 0;) {
            size_t r = (size_t)std::floor(rng::next() * (double)cur);
            cur -= 1;
            std::swap(items[cur], items[r]);
        }
    }
    expando.set(js::atom("_bigBox"),
                str_array({"farawaytown_extras_misc.message_140", "farawaytown_extras_misc.message_141", "farawaytown_extras_misc.message_142",
                           "farawaytown_extras_misc.message_143", "farawaytown_extras_misc.message_144", "farawaytown_extras_misc.message_145",
                           "farawaytown_extras_misc.message_146", "farawaytown_extras_misc.message_147", "farawaytown_extras_misc.message_148",
                           "farawaytown_extras_misc.message_149", "farawaytown_extras_misc.message_150", "farawaytown_extras_misc.message_151",
                           "farawaytown_extras_misc.message_152", "farawaytown_extras_misc.message_153", "farawaytown_extras_misc.message_154",
                           "farawaytown_extras_misc.message_155", "farawaytown_extras_misc.message_157", "farawaytown_extras_misc.message_160",
                           "farawaytown_extras_misc.message_161", "farawaytown_extras_misc.message_162", "farawaytown_extras_misc.message_164",
                           "farawaytown_extras_misc.message_165", "farawaytown_extras_misc.message_167", "farawaytown_extras_misc.message_170",
                           "farawaytown_extras_misc.message_171", "farawaytown_extras_misc.message_172", "farawaytown_extras_misc.message_173",
                           "farawaytown_extras_misc.message_174", "farawaytown_extras_misc.message_175"}));
    expando.set(js::atom("_toyBox"), js::make_array());
    expando.set(js::atom("_bookBox"), js::make_array());
    expando.set(js::atom("_trashBox"), js::make_array());
    js::Val full = js::make_object();
    for (int id = 198; id <= 210; id++) {
        char a[80], b[80];
        std::snprintf(a, sizeof(a), "sidequest_farawaytown_ginojukebox.message_%d", 20 + (id - 198));
        std::snprintf(b, sizeof(b), "sidequest_farawaytown_ginojukebox.message_%d", 200 + (id - 198));
        full.o->set(js::atom(std::to_string(id)), js::make_array({js::Val::str(a), js::Val::str(b)}));
    }
    expando.set(js::atom("_jukeboxListFull"), full);
    expando.set(js::atom("_jukeboxList"), js::make_array());
    clear_horror_effects();
    playtime_ms = 0;
}

void GameSystem::init_message_position() {
    msg_position_x = js::Val::str("auto");
    msg_position_y = js::Val::str("auto");
    std::string dx = data::plugin_param("YEP_X_ExtMesPack1", "Default X", "left");
    std::string dy = data::plugin_param("YEP_X_ExtMesPack1", "Default Y", "top");
    auto has = [](std::string s, const char* w) {
        for (auto& c : s) c = (char)tolower((unsigned char)c);
        return s.find(w) != std::string::npos;
    };
    msg_anchor_x = has(dx, "center") ? 0.5 : has(dx, "right") ? 1 : 0;
    msg_anchor_y = has(dy, "center") ? 0.5 : has(dy, "bottom") ? 1 : 0;
}

double GameSystem::window_tone(int i) const {
    if (has_window_tone) return window_tone_[i];
    static const Value* tone = &data::system()["windowTone"];
    return (*tone)[i].num();
}

AudioObj GameSystem::battle_bgm() const { return battle_bgm_.valid ? battle_bgm_ : AudioObj::from(data::system()["battleBgm"]); }
AudioObj GameSystem::victory_me() const { return victory_me_.valid ? victory_me_ : AudioObj::from(data::system()["victoryMe"]); }
AudioObj GameSystem::defeat_me() const { return defeat_me_.valid ? defeat_me_ : AudioObj::from(data::system()["defeatMe"]); }

void GameSystem::on_battle_win() {
    win_count++;
    int ce = (int)param_num("YEP_UtilityCommonEvents", "Battle Won", 0);
    if (ce > 0) game_temp().reserve_common_event(ce);
}

void GameSystem::on_battle_escape() {
    escape_count++;
    int ce = (int)param_num("YEP_UtilityCommonEvents", "Battle Escape", 0);
    if (ce > 0) game_temp().reserve_common_event(ce);
}

void GameSystem::on_before_save() {
    save_count++;
    version_id = data::system()["versionId"].as_int();
    frames_on_save = (double)frame_count();
    bgm_on_save = audio_manager::save_bgm();
    bgs_on_save = audio_manager::save_bgs();
    playtime_ms += playtime_manager::save_time();
    playtime_manager::clear();
}

void GameSystem::on_after_load() {
    set_frame_count((uint64_t)frames_on_save);
    audio_manager::play_bgm(bgm_on_save);
    audio_manager::play_bgs(bgs_on_save);
    game_temp().reserve_common_event(40);
    after_load_hook();
    if (playtime_ms <= 0) playtime_ms = std::floor(frames_on_save * 1000.0 / 60.0);
    playtime_manager::clear();
}

int GameSystem::playtime() const {
    if (playtime_ms <= 0) return (int)(frame_count() / 60);
    double snapv = playtime_manager::snap();
    return (int)std::floor((snapv - playtime_manager::accumulator + playtime_ms) / 1000.0);
}

std::string GameSystem::playtime_text() const {
    int pt = playtime();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", pt / 3600, (pt / 60) % 60, pt % 60);
    return buf;
}

void GameSystem::save_bgm() { saved_bgm = audio_manager::save_bgm(); }
void GameSystem::replay_bgm() {
    if (saved_bgm.valid) audio_manager::replay_bgm(saved_bgm);
}
void GameSystem::save_walking_bgm() { walking_bgm = audio_manager::save_bgm(); }
void GameSystem::replay_walking_bgm() {
    if (walking_bgm.valid) audio_manager::play_bgm(walking_bgm);
}
void GameSystem::save_walking_bgm2() { walking_bgm = AudioObj::from(current_map_data()["bgm"]); }

int GameSystem::message_rows() const {
    double r = message_rows_expr.empty() ? 0 : std::atof(message_rows_expr.c_str());
    if (!r) r = param_num("YEP_MessageCore", "Default Rows", 4);
    return std::max(1, (int)r);
}

double GameSystem::message_width() const {
    auto eval = [](const std::string& e) -> double {
        if (e.empty()) return 0;
        if (e == "Graphics.boxWidth" || e == "Graphics.width") return 640;
        if (e == "Graphics.boxHeight" || e == "Graphics.height") return 480;
        return std::atof(e.c_str());
    };
    double w = eval(message_width_expr);
    if (!w) w = eval(data::plugin_param("YEP_MessageCore", "Default Width", "Graphics.boxWidth"));
    return w;
}

js::Val GameSystem::get_message_sound() {
    js::Val o = js::make_object();
    o.o->set(js::atom("name"), js::Val::str(msg_sound_name));
    o.o->set(js::atom("volume"), js::Val::num(msg_sound_vol));
    double maxp = msg_sound_pitch + msg_sound_pitch_var, minp = msg_sound_pitch - msg_sound_pitch_var;
    o.o->set(js::atom("pitch"), js::Val::num(std::floor(rng::next() * (maxp - minp + 1) + minp)));
    double maxa = msg_sound_pan + msg_sound_pan_var, mina = msg_sound_pan - msg_sound_pan_var;
    o.o->set(js::atom("pan"), js::Val::num(std::floor(rng::next() * (maxa - mina + 1) + mina)));
    return o;
}

static int choice_switch(bool show, int id) {
    char key[48];
    std::snprintf(key, sizeof(key), "Choice %d %s Switch", id + 1, show ? "Show" : "On");
    return (int)param_num("YEP_X_ExtMesPack1", key, 0);
}

void GameSystem::init_choice_show() {
    if (choice_show_initialized) return;
    hide_choices.clear();
    for (int i = 0; i < 20; i++) {
        int sw = choice_switch(true, i);
        if (sw > 0) game_switches().set_value(sw, true);
    }
    choice_show_initialized = true;
}

void GameSystem::init_choice_enable() {
    if (choice_enable_initialized) return;
    disable_choices.clear();
    for (int i = 0; i < 20; i++) {
        int sw = choice_switch(false, i);
        if (sw > 0) game_switches().set_value(sw, true);
    }
    choice_enable_initialized = true;
}

static bool contains(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

bool GameSystem::is_choice_shown(int id) {
    if (contains(hide_choices, id)) return false;
    if (id >= 20) return true;
    int sw = choice_switch(true, id);
    if (sw <= 0) return true;
    return game_switches().value(sw);
}

bool GameSystem::is_choice_enabled(int id) {
    if (contains(disable_choices, id)) return false;
    if (id >= 20) return true;
    int sw = choice_switch(false, id);
    if (sw <= 0) return true;
    return game_switches().value(sw);
}

void GameSystem::hide_choice(int id) {
    id -= 1;
    if (id < 0 || contains(hide_choices, id)) return;
    hide_choices.push_back(id);
    int sw = id < 20 ? choice_switch(true, id) : 0;
    if (sw) game_switches().set_value(sw, false);
}

void GameSystem::show_choice(int id) {
    id -= 1;
    if (id < 0 || !contains(hide_choices, id)) return;
    hide_choices.erase(std::find(hide_choices.begin(), hide_choices.end(), id));
    int sw = id < 20 ? choice_switch(true, id) : 0;
    if (sw) game_switches().set_value(sw, true);
}

void GameSystem::disable_choice(int id) {
    id -= 1;
    if (id < 0 || contains(disable_choices, id)) return;
    disable_choices.push_back(id);
    int sw = id < 20 ? choice_switch(false, id) : 0;
    if (sw) game_switches().set_value(sw, false);
}

void GameSystem::enable_choice(int id) {
    id -= 1;
    if (id < 0 || !contains(disable_choices, id)) return;
    disable_choices.erase(std::find(disable_choices.begin(), disable_choices.end(), id));
    int sw = id < 20 ? choice_switch(false, id) : 0;
    if (sw) game_switches().set_value(sw, true);
}

void GameSystem::clear_choice_settings() {
    choice_show_initialized = false;
    init_choice_show();
    choice_enable_initialized = false;
    init_choice_enable();
}

void GameSystem::grow_magical_seed() {
    magical_seed = std::clamp((magical_seed + 1) % 100, 1, 100);
}

void GameSystem::replant_magical_seed(int seed) { magical_seed = seed; }

double GameSystem::get_magical_seed(double offset, bool flt) {
    double value = magical_seed + offset;
    double base = 2516837;
    grow_magical_seed();
    double r = std::fmod(value * 16807, base);
    return flt ? r / base : r;
}

bool GameSystem::is_saved_event_location(int map, int event) const {
    return saved_event_locations.count(std::to_string(map) + "," + std::to_string(event)) != 0;
}

void GameSystem::save_event_location(int map, int event, int x, int y, int d) {
    saved_event_locations[std::to_string(map) + "," + std::to_string(event)] = {x, y, d};
}

void GameSystem::unlock_achievement(const std::string& code) { (void)code; }

void GameSystem::clear_horror_effects() { expando.set(js::atom("_horrorFilters"), js::make_object()); }

js::Val GameSystem::horror_filters() {
    js::Val* v = expando.find(js::atom("_horrorFilters"));
    if (!v || !v->is_obj()) {
        clear_horror_effects();
        v = expando.find(js::atom("_horrorFilters"));
    }
    return *v;
}

static double horror_param(const char* key, double def) { return param_num("Olivia_HorrorEffects", key, def); }
static bool horror_bool(const char* key) { return param_bool("Olivia_HorrorEffects", key, true); }

void GameSystem::create_horror_effect(const std::string& type0) {
    std::string type;
    for (char c : type0) type += (char)tolower((unsigned char)c);
    js::Val hf = horror_filters();
    auto set = [](js::Val& o, const char* k, js::Val v) { o.o->set(js::atom(k), std::move(v)); };
    if (type.find("noise") != std::string::npos && !hf.o->find(js::atom("noiseFilter"))) {
        js::Val f = js::make_object();
        set(f, "noise", js::Val::num(horror_param("Noise Default", 0.5)));
        set(f, "animated", js::Val::boolean(horror_bool("Noise Animated")));
        set(f, "needUpdate", js::Val::boolean(true));
        hf.o->set(js::atom("noiseFilter"), f);
    } else if (type.find("glitch") != std::string::npos && !hf.o->find(js::atom("glitchFilter"))) {
        double slices = horror_param("Glitch Slices", 10);
        js::Val f = js::make_object();
        set(f, "slices", js::Val::num(slices));
        set(f, "offset", js::Val::num(horror_param("Glitch Offset", 100)));
        set(f, "sliceMin", js::Val::num(std::ceil(slices / 2)));
        set(f, "sliceMax", js::Val::num(slices));
        set(f, "animated", js::Val::boolean(horror_bool("Glitch Animated")));
        set(f, "aniFrequency", js::Val::num(horror_param("Glitch Animated Frequency", 300)));
        set(f, "aniStrength", js::Val::num(horror_param("Glitch Animated Strength", 30)));
        set(f, "needUpdate", js::Val::boolean(true));
        hf.o->set(js::atom("glitchFilter"), f);
    } else if (type.find("tv") != std::string::npos && !hf.o->find(js::atom("tvFilter"))) {
        js::Val f = js::make_object();
        set(f, "lineWidth", js::Val::num(horror_param("TV Line Thickness", 5)));
        set(f, "vignetting", js::Val::num(horror_param("TV Corner Size", 0.3)));
        set(f, "animated", js::Val::boolean(horror_bool("TV Animated")));
        set(f, "aniSpeed", js::Val::num(horror_param("TV Speed", 0.25)));
        set(f, "needUpdate", js::Val::boolean(true));
        hf.o->set(js::atom("tvFilter"), f);
    }
}

void GameSystem::remove_horror_effect(const std::string& type) {
    js::Val hf = horror_filters();
    hf.o->set(js::atom(type + "Filter"), js::Val());
}

void GameSystem::set_horror_effect_to_value(const std::string& type, const std::string& key, const js::Val& v) {
    js::Val hf = horror_filters();
    js::Val* f = hf.o->find(js::atom(type + "Filter"));
    if (f && f->truthy() && f->is_obj()) {
        f->o->set(js::atom(key), v);
        f->o->set(js::atom("needUpdate"), js::Val::boolean(true));
    }
}

const MorphTarget* GameSystem::get_preserved_event_morph(int map_id, int event_id) const {
    auto it = preserved_morphs.find(std::to_string(map_id) + "," + std::to_string(event_id));
    return it == preserved_morphs.end() ? nullptr : &it->second;
}

void GameSystem::log_preserved_event_morph_raw(int target_map, int target_event, int map_id, int event_id) {
    preserved_morphs[std::to_string(target_map) + "," + std::to_string(target_event)] = MorphTarget{map_id, event_id};
}

void GameSystem::remove_preserved_event_morph(int map_id, int event_id) { preserved_morphs.erase(std::to_string(map_id) + "," + std::to_string(event_id)); }

std::vector<std::shared_ptr<GameEvent>>& GameSystem::get_map_spawned_event_data(int map_id) {
    auto& v = map_spawned_events[map_id];
    if (v.empty()) v.push_back(nullptr);
    return v;
}

void GameSystem::init_message_font_settings() {
    msg_font_name = data::plugin_param("YEP_MessageCore", "Font Name", "GameFont");
    msg_font_size = (int)param_num("YEP_MessageCore", "Font Size", 28);
    msg_font_outline = (int)param_num("YEP_MessageCore", "Font Outline", 4);
}

void GameSystem::init_message_sounds() {
    msg_sound_enable = param_bool("YEP_X_ExtMesPack1", "Enable Sound", true);
    msg_sound_name = data::plugin_param("YEP_X_ExtMesPack1", "Sound Name", "Cursor1");
    msg_sound_vol = param_num("YEP_X_ExtMesPack1", "Sound Volume", 100);
    msg_sound_pitch = param_num("YEP_X_ExtMesPack1", "Sound Pitch", 100);
    msg_sound_pitch_var = param_num("YEP_X_ExtMesPack1", "Pitch Variance", 0);
    msg_sound_pan = param_num("YEP_X_ExtMesPack1", "Sound Pan", 0);
    msg_sound_pan_var = param_num("YEP_X_ExtMesPack1", "Pan Variance", 0);
    msg_sound_interval = (int)param_num("YEP_X_ExtMesPack1", "Sound Interval", 2);
}

void GameSystem::set_new_damage_cap(double value, bool damage) {
    if (damage) {
        has_new_damage_cap = true;
        new_damage_cap = value;
    } else {
        has_new_healing_cap = true;
        new_healing_cap = value * -1;
    }
}

void GameSystem::reset_damage_settings() {
    damage_rate = 1.0;
    flat_damage = 0;
    heal_rate = 1.0;
    flat_heal = 0;
    default_damage_cap = yanfly_param("DMGEnableCap").truthy();
    crit_mult = 1.0;
    flat_crit = 0;
    force_critical = false;
    force_no_critical = false;
}

double GameSystem::maximum_damage() const {
    if (has_new_damage_cap) return new_damage_cap;
    return yanfly_param("DMGMaxDamage").num();
}

double GameSystem::maximum_healing() const {
    if (has_new_healing_cap) return new_healing_cap;
    return yanfly_param("DMGMaxHealing").num() * -1;
}

static int gacha_type_index(const std::string& t) {
    if (t == "item") return 0;
    if (t == "weapon") return 1;
    if (t == "armor") return 2;
    return -1;
}

void GameSystem::clear_gacha() {
    has_gacha = true;
    for (auto& m : gacha) m.clear();
}

void GameSystem::add_to_gacha(const std::string& type, int id) {
    if (!has_gacha) clear_gacha();
    int i = gacha_type_index(type);
    if (i >= 0) gacha[i][id] = true;
}

void GameSystem::remove_from_gacha(const std::string& type, int id) {
    if (!has_gacha) return;
    int i = gacha_type_index(type);
    if (i >= 0) gacha[i][id] = false;
}

bool GameSystem::is_in_gacha(const std::string& type, int id) const {
    if (!has_gacha) return false;
    int i = gacha_type_index(type);
    if (i < 0) return false;
    auto it = gacha[i].find(id);
    return it != gacha[i].end() && it->second;
}

void GameSystem::clear_gacha_book() {
    has_gacha_book = true;
    for (auto& m : gacha_book) m.clear();
}

void GameSystem::add_to_gacha_book(const std::string& type, int id) {
    if (!has_gacha_book) clear_gacha_book();
    int i = gacha_type_index(type);
    if (i >= 0) gacha_book[i][id] = true;
}

void GameSystem::remove_from_gacha_book(const std::string& type, int id) {
    if (!has_gacha_book) return;
    int i = gacha_type_index(type);
    if (i >= 0) gacha_book[i][id] = false;
}

void GameSystem::complete_gacha_book() {
    clear_gacha_book();
    for (size_t i = 1; i < data::items().size(); i++) gacha_book[0][(int)i] = true;
    for (size_t i = 1; i < data::weapons().size(); i++) gacha_book[1][(int)i] = true;
    for (size_t i = 1; i < data::armors().size(); i++) gacha_book[2][(int)i] = true;
}

bool GameSystem::is_in_gacha_book(const std::string& type, int id) const {
    if (!has_gacha_book) return false;
    int i = gacha_type_index(type);
    if (i < 0) return false;
    auto it = gacha_book[i].find(id);
    return it != gacha_book[i].end() && it->second;
}

}
