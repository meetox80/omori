#include "map.h"
#include "perf.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "../core/regex.h"
#include "../js/random.h"
#include "audio_manager.h"
#include "battler.h"
#include "data.h"
#include "filter.h"
#include "game.h"
#include "hooks.h"
#include "interpreter.h"
#include "params.h"
#include "party.h"

namespace rpg {

namespace {

std::unordered_map<int, std::shared_ptr<Document>>& preload_cache() {
    static std::unordered_map<int, std::shared_ptr<Document>> c;
    return c;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t c = s.find(sep, start);
        out.push_back(s.substr(start, c == std::string::npos ? std::string::npos : c - start));
        if (c == std::string::npos) break;
        start = c + 1;
    }
    return out;
}

}

std::shared_ptr<Document> load_map_doc(int id) {
    char path[64];
    std::snprintf(path, sizeof(path), "maps/map%03d.bin", id);
    auto doc = std::make_shared<Document>();
    if (!doc->load_data(path)) return nullptr;
    return doc;
}

const Value* preloaded_event(int map_id, int event_id, std::shared_ptr<const void>* keep) {
    if (!params().preloaded_maps.count(map_id)) return nullptr;
    auto& c = preload_cache();
    auto it = c.find(map_id);
    std::shared_ptr<Document> doc;
    if (it == c.end()) {
        if (game_map().map_id_ == map_id && game_map().doc) doc = game_map().doc;
        else doc = load_map_doc(map_id);
        c[map_id] = doc;
    } else {
        doc = it->second;
    }
    if (!doc) return nullptr;
    if (keep) *keep = doc;
    const Value& ev = doc->root()["data"]["events"][event_id];
    return ev.is_obj() ? &ev : nullptr;
}

const Value* map_event(int map_id, int event_id, std::shared_ptr<const void>* keep) {
    if (params().preloaded_maps.count(map_id)) return preloaded_event(map_id, event_id, keep);
    std::shared_ptr<Document> doc = game_map().map_id_ == map_id ? game_map().doc : nullptr;
    if (!doc) doc = load_map_doc(map_id);
    if (!doc) return nullptr;
    if (keep) *keep = doc;
    const Value& ev = doc->root()["data"]["events"][event_id];
    return ev.is_obj() ? &ev : nullptr;
}

GameCommonEvent::GameCommonEvent(int id) : common_event_id(id) { refresh(); }
GameCommonEvent::~GameCommonEvent() = default;

const Value& GameCommonEvent::event() const { return data::common_events()[common_event_id]; }
const Value* GameCommonEvent::list() const { return &event()["list"]; }

bool GameCommonEvent::is_active() const {
    const Value& e = event();
    return e["trigger"].as_int() == 2 && game_switches().value(e["switchId"].as_int());
}

void GameCommonEvent::refresh() {
    if (is_active()) {
        if (!interpreter) interpreter.reset(new GameInterpreter());
    } else {
        retire_interpreter(std::move(interpreter));
    }
}

void GameCommonEvent::update() {
    if (!interpreter) return;
    if (!interpreter->is_running()) interpreter->setup(list(), 0, data::keep());
    interpreter->update();
}

GameMapFog::GameMapFog() {
    origin_js = js::make_object();
    origin_js.o->set(js::atom("x"), js::Val::num(0));
    origin_js.o->set(js::atom("y"), js::Val::num(0));
    move_js = js::make_object();
    move_js.o->set(js::atom("x"), js::Val::num(0));
    move_js.o->set(js::atom("y"), js::Val::num(0));
}

void GameMapFog::sync_points() {
    js::Vm& vm = js::vm();
    origin_x = vm.get(origin_js, js::atom("x")).to_num();
    origin_y = vm.get(origin_js, js::atom("y")).to_num();
    move_px = vm.get(move_js, js::atom("x")).to_num();
    move_py = vm.get(move_js, js::atom("y")).to_num();
}

GameMap::GameMap() {
    interpreter.reset(new GameInterpreter());
    create_vehicles();
    region_move_directions.clear();
    clear_map_fogs();
    init_footprints();
}

GameMap::~GameMap() = default;

const Value& GameMap::data() const { return doc ? doc->root()["data"] : Value::null_value(); }
const Value& GameMap::tiled() const { return doc ? doc->root()["tiled"] : Value::null_value(); }
bool GameMap::is_tiled_map() const { return dims_.tiled; }

void GameMap::refresh_dims() {
    dims_ = Dims();
    if (!doc) return;
    const Value& t = tiled();
    const Value& d = data();
    dims_.tiled = t.is_obj();
    dims_.data_w = d["width"].num();
    dims_.data_h = d["height"].num();
    dims_.scroll_type = d["scrollType"].as_int();
    if (dims_.tiled) {
        dims_.width = t["w"].as_int();
        dims_.height = t["h"].as_int();
        dims_.tile_w = t["tw"].num(32);
        dims_.tile_h = t["th"].num(32);
    } else {
        dims_.width = d["width"].as_int();
        dims_.height = d["height"].as_int();
    }
}

void GameMap::refresh_tileset_cache() const {
    tileset_cache_id_ = tileset_id;
    tileset_ = &data::tilesets()[tileset_id];
    tileset_flags_ = &(*tileset_)["flags"];
    slip_tags_.clear();
    if (!tileset_->is_obj()) return;
    std::string n = (*tileset_)["note"].string();
    for (auto& line : re::split_lines(n)) {
        re::Groups g;
        if (re::search("<(?:SLIPPERY|slippery tile):[ ]*(\\d+(?:\\s*,\\s*\\d+)*)>", "i", line, &g)) {
            for (auto& d : re::all("\\d+", "g", g[1])) slip_tags_.push_back(std::atoi(d.c_str()));
        }
    }
}
std::string GameMap::note() const { return data()["note"].string(); }

bool GameMap::load_map_data(int id) {
    if (map_id_ == id && doc) return true;
    std::shared_ptr<Document> d = load_map_doc(id);
    if (!d) return false;
    doc = d;
    refresh_dims();
    route_cache_clear();
    for (auto it = preload_cache().begin(); it != preload_cache().end(); ++it) {
        if (it->first == id) it->second = doc;
    }
    return true;
}

void GameMap::setup(int id) {
    std::shared_ptr<Document> d = load_map_doc(id);
    if (d) {
        doc = d;
        refresh_dims();
        route_cache_clear();
    }
    if (!doc) return;
    if (!cam_norm) {
        cam_target_set(CharRef::of(&game_player()), 800);
        has_saved_cam = true;
        saved_cam_args = {"PLAYER"};
    }
    process_map_notetags();
    if (id != map_id_) game_system().remove_temporary_map_spawned_events(map_id_);
    std::vector<std::shared_ptr<GameEvent>> spawned_src = game_system().get_map_spawned_event_data(id);
    recorded_morphed_events = false;
    if (game_player().needs_map_reload && id == map_id_) record_morphed_events();
    map_id_ = id;
    tileset_id = data()["tilesetId"].as_int();
    display_x_ = 0;
    display_y_ = 0;
    referesh_vehicles();
    setup_events();
    setup_scroll();
    setup_parallax();
    setup_battleback();
    needs_refresh = false;
    levels.clear();
    reflections.clear();
    set_current_map_level(0);
    if (is_tiled_map()) setup_tiled();
    process_morphed_events();
    spawned_events = game_system().get_map_spawned_event_data(id);
    for (size_t i = 0; i < spawned_events.size(); i++) {
        std::shared_ptr<GameEvent> se = spawned_events[i];
        if (!se) continue;
        size_t slot = i + (size_t)params().spawner_id;
        if (events_.size() <= slot) events_.resize(slot + 1);
        events_[slot] = se;
        se->page_index = -2;
        needs_refresh = true;
    }
    region_move_directions.clear();
    setup_tkm_filters();
    init_footprints();
    process_footstep_sound();
}

void GameMap::process_map_notetags() {
    const PluginParams& p = params();
    std::string n = note();
    std::vector<std::string> lines = re::split_lines(n);
    region_footstep_sounds.clear();
    region_footstep_sounds[0] = FootprintSound{p.foot_default_sound, p.foot_default_volume, p.foot_default_pitch};
    region_common_events.clear();
    for (auto& line : lines) {
        re::Groups g;
        if (re::search("<REGION[ ](\\d+)[ ]FOOTSTEP SOUND:[ ](.*)>", "i", line, &g)) {
            int region = std::min(std::max(g.num(1), 0), 255);
            auto parts = split(g[2], ',');
            FootprintSound fs;
            fs.name = trim(parts[0]);
            fs.volume = parts.size() > 1 && !parts[1].empty() ? js_parse_int(parts[1]) : p.foot_default_volume;
            fs.pitch = parts.size() > 2 && !parts[2].empty() ? js_parse_int(parts[2]) : p.foot_default_pitch;
            region_footstep_sounds[region] = fs;
        }
        if (re::search("<(?:REGION)[ ](\\d+)[ ](?:EVENT):[ ](\\d+)>", "i", line, &g)) region_common_events[g.num(1)] = g.num(2);
    }
    auto to_int = [](const std::vector<double>& v) {
        std::vector<int> out;
        for (double d : v) out.push_back(std::isnan(d) ? -2147483647 : (int)d);
        return out;
    };
    std::vector<int> all_r = to_int(p.rr_all_restrict), all_a = to_int(p.rr_all_allow);
    restrict_player_regions = all_r;
    for (int v : to_int(p.rr_player_restrict)) restrict_player_regions.push_back(v);
    restrict_event_regions = all_r;
    for (int v : to_int(p.rr_event_restrict)) restrict_event_regions.push_back(v);
    allow_player_regions = all_a;
    for (int v : to_int(p.rr_player_allow)) allow_player_regions.push_back(v);
    allow_event_regions = all_a;
    for (int v : to_int(p.rr_event_allow)) allow_event_regions.push_back(v);
    struct Tag {
        const char* list;
        const char* range;
        std::vector<int>* a;
        std::vector<int>* b;
    };
    Tag tags[] = {
        {"<(?:PLAYER RESTRICT REGION):[ ]*(\\d+(?:\\s*,\\s*\\d+)*)>", "<(?:PLAYER RESTRICT REGION):[ ](\\d+)[ ](?:TO)[ ](\\d+)>", &restrict_player_regions, nullptr},
        {"<(?:EVENT RESTRICT REGION):[ ]*(\\d+(?:\\s*,\\s*\\d+)*)>", "<(?:EVENT RESTRICT REGION):[ ](\\d+)[ ](?:TO)[ ](\\d+)>", &restrict_event_regions, nullptr},
        {"<(?:ALL RESTRICT REGION):[ ]*(\\d+(?:\\s*,\\s*\\d+)*)>", "<(?:ALL RESTRICT REGION):[ ](\\d+)[ ](?:TO)[ ](\\d+)>", &restrict_player_regions, &restrict_event_regions},
        {"<(?:PLAYER ALLOW REGION):[ ]*(\\d+(?:\\s*,\\s*\\d+)*)>", "<(?:PLAYER ALLOW REGION):[ ](\\d+)[ ](?:TO)[ ](\\d+)>", &allow_player_regions, nullptr},
        {"<(?:EVENT ALLOW REGION):[ ]*(\\d+(?:\\s*,\\s*\\d+)*)>", "<(?:EVENT ALLOW REGION):[ ](\\d+)[ ](?:TO)[ ](\\d+)>", &allow_event_regions, nullptr},
        {"<(?:ALL ALLOW REGION):[ ]*(\\d+(?:\\s*,\\s*\\d+)*)>", "<(?:ALL ALLOW REGION):[ ](\\d+)[ ](?:TO)[ ](\\d+)>", &allow_player_regions, &allow_event_regions},
    };
    for (auto& line : lines) {
        for (auto& t : tags) {
            re::Groups g;
            if (re::search(t.list, "i", line, &g)) {
                for (auto& d : re::all("\\d+", "g", g[1])) {
                    t.a->push_back(std::atoi(d.c_str()));
                    if (t.b) t.b->push_back(std::atoi(d.c_str()));
                }
                break;
            }
            if (re::search(t.range, "i", line, &g)) {
                for (int v = g.num(1); v <= g.num(2); v++) {
                    t.a->push_back(v);
                    if (t.b) t.b->push_back(v);
                }
                break;
            }
        }
    }
    save_event_locations = false;
    for (auto& line : lines) {
        if (re::search("<(?:SAVE EVENT LOCATION|save event locations)>", "i", line)) save_event_locations = true;
    }
}

bool GameMap::is_event_running() const { return interpreter->is_running() || is_any_event_starting(); }

double GameMap::tile_width() const { return dims_.tile_w; }
double GameMap::tile_height() const { return dims_.tile_h; }

double GameMap::display_x() const { return (display_x_ * tile_width()) / tile_width(); }
double GameMap::display_y() const { return (display_y_ * tile_height()) / tile_height(); }

std::string GameMap::battleback1_name() const {
    std::string b = get_region_battleback_name(1);
    if (!b.empty()) return b;
    return battleback1_name_;
}

std::string GameMap::battleback2_name() const {
    std::string b = get_region_battleback_name(2);
    if (!b.empty()) return b;
    return battleback2_name_;
}

void GameMap::create_vehicles() {
    vehicles_.clear();
    vehicles_.emplace_back(new GameVehicle("boat"));
    vehicles_.emplace_back(new GameVehicle("ship"));
    vehicles_.emplace_back(new GameVehicle("airship"));
}

void GameMap::referesh_vehicles() {
    for (auto& v : vehicles_) v->refresh();
}

GameVehicle* GameMap::vehicle(const std::string& type) const {
    if (type == "boat" || type == "0") return boat();
    if (type == "ship" || type == "1") return ship();
    if (type == "airship" || type == "2") return airship();
    return nullptr;
}

void GameMap::setup_events() {
    events_.clear();
    const Value& evs = data()["events"];
    events_.resize(evs.size());
    for (size_t i = 0; i < evs.size(); i++) {
        if (evs[i].is_obj()) events_[i] = std::make_shared<GameEvent>(map_id_, (int)i);
    }
    common_events.clear();
    const Value& ces = data::common_events();
    for (size_t i = 0; i < ces.size(); i++) {
        if (ces[i].is_obj() && ces[i]["trigger"].as_int() == 2) common_events.emplace_back(new GameCommonEvent(ces[i]["id"].as_int()));
    }
    refresh_tile_events();
}

std::vector<GameEvent*> GameMap::events() const {
    std::vector<GameEvent*> out;
    out.reserve(events_.size());
    for (auto& e : events_) {
        if (e) out.push_back(e.get());
    }
    return out;
}

GameEvent* GameMap::event(int id) const { return id >= 0 && id < (int)events_.size() ? events_[(size_t)id].get() : nullptr; }

void GameMap::erase_event(int id) {
    GameEvent* e = event(id);
    if (!e) js::vm().throw_type_error("Cannot read property 'erase' of undefined");
    e->erase();
}

void GameMap::setup_scroll() {
    scroll_direction = 2;
    scroll_rest = 0;
    scroll_speed = 4;
    has_scroll_target = false;
    scroll_duration = 0;
    scrolling_time = 0;
}

void GameMap::setup_parallax() {
    const Value& d = data();
    parallax_name_ = d["parallaxName"].string();
    parallax_zero = !parallax_name_.empty() && parallax_name_[0] == '!';
    parallax_loop_x = d["parallaxLoopX"].truthy();
    parallax_loop_y = d["parallaxLoopY"].truthy();
    parallax_sx = d["parallaxSx"].num();
    parallax_sy = d["parallaxSy"].num();
    parallax_x = 0;
    parallax_y = 0;
}

void GameMap::setup_battleback() {
    const Value& d = data();
    if (d["specifyBattleback"].truthy()) {
        has_battleback1 = has_battleback2 = true;
        battleback1_name_ = d["battleback1Name"].string();
        battleback2_name_ = d["battleback2Name"].string();
    } else {
        has_battleback1 = has_battleback2 = false;
        battleback1_name_.clear();
        battleback2_name_.clear();
    }
}

void GameMap::set_display_pos(double x, double y) {
    if (is_loop_horizontal()) {
        double w = width();
        display_x_ = std::fmod(std::fmod(x, w) + w, w);
        parallax_x = x;
    } else {
        double end_x = width() - screen_tile_x();
        display_x_ = end_x < 0 ? end_x / 2 : std::min(std::max(x, 0.0), end_x);
        parallax_x = display_x_;
    }
    if (is_loop_vertical()) {
        double h = height();
        display_y_ = std::fmod(std::fmod(y, h) + h, h);
        parallax_y = y;
    } else {
        double end_y = height() - screen_tile_y();
        display_y_ = end_y < 0 ? end_y / 2 : std::min(std::max(y, 0.0), end_y);
        parallax_y = display_y_;
    }
}

double GameMap::parallax_ox() const {
    if (parallax_zero) return parallax_x * tile_width();
    if (parallax_loop_x) return parallax_x * tile_width() / 2;
    return 0;
}

double GameMap::parallax_oy() const {
    if (parallax_zero) return parallax_y * tile_height();
    if (parallax_loop_y) return parallax_y * tile_height() / 2;
    return 0;
}

const Value& GameMap::tileset() const {
    if (tileset_cache_id_ != tileset_id) refresh_tileset_cache();
    return *tileset_;
}

std::string GameMap::display_name() const {
    const Value& loc = data::lang_entry("XX_BLUE.Map_Display_Names");
    const Value* v = loc.find(std::to_string(map_id_).c_str());
    if (v && v->truthy()) return data::js_string(*v);
    if (has_override_display_name) return override_display_name;
    return data()["displayName"].string();
}

int GameMap::width() const { return dims_.width; }
int GameMap::height() const { return dims_.height; }

bool GameMap::is_loop_horizontal() const {
    int t = dims_.scroll_type;
    return t == 2 || t == 3;
}

bool GameMap::is_loop_vertical() const {
    int t = dims_.scroll_type;
    return t == 1 || t == 3;
}

bool GameMap::is_dash_disabled() const {
    bool v = false;
    if (game_system().disable_dashing) v = true;
    else {
        GameActor* first = game_party().first_member();
        if (first && first->is_dash_disabled()) v = true;
        else v = data()["disableDashing"].truthy();
    }
    return v || is_ladder(game_player().x_, game_player().y_);
}

const Value& GameMap::encounter_list() const { return data()["encounterList"]; }
double GameMap::encounter_step() const { return data()["encounterStep"].num(); }
double GameMap::screen_tile_x() const { return 640.0 / tile_width(); }
double GameMap::screen_tile_y() const { return 480.0 / tile_height(); }

double GameMap::adjust_x(double x) const {
    double dx = display_x();
    if (is_loop_horizontal() && x < dx - (width() - screen_tile_x()) / 2) return x - dx + dims_.data_w;
    return x - dx;
}

double GameMap::adjust_y(double y) const {
    double dy = display_y();
    if (is_loop_vertical() && y < dy - (height() - screen_tile_y()) / 2) return y - dy + dims_.data_h;
    return y - dy;
}

double GameMap::round_x(double x) const {
    if (!is_loop_horizontal()) return x;
    double w = width();
    return std::fmod(std::fmod(x, w) + w, w);
}

double GameMap::round_y(double y) const {
    if (!is_loop_vertical()) return y;
    double h = height();
    return std::fmod(std::fmod(y, h) + h, h);
}

double GameMap::delta_x(double x1, double x2) const {
    double r = x1 - x2;
    if (is_loop_horizontal() && std::fabs(r) > width() / 2.0) r += r < 0 ? width() : -width();
    return r;
}

double GameMap::delta_y(double y1, double y2) const {
    double r = y1 - y2;
    if (is_loop_vertical() && std::fabs(r) > height() / 2.0) r += r < 0 ? height() : -height();
    return r;
}

double GameMap::distance(double x1, double y1, double x2, double y2) const { return std::fabs(delta_x(x1, x2)) + std::fabs(delta_y(y1, y2)); }

void GameMap::autoplay() {
    const Value& d = data();
    if (d["autoplayBgm"].truthy()) {
        if (game_player().is_in_vehicle()) game_system().save_walking_bgm2();
        else audio_manager::play_bgm(AudioObj::from(d["bgm"]));
    }
    if (d["autoplayBgs"].truthy()) audio_manager::play_bgs(AudioObj::from(d["bgs"]));
}

void GameMap::refresh_if_needed() {
    if (needs_refresh) refresh();
}

void GameMap::refresh() {
    for (GameEvent* e : events()) e->refresh();
    for (auto& c : common_events) c->refresh();
    refresh_tile_events();
    needs_refresh = false;
}

void GameMap::refresh_tile_events() {
    tile_events.clear();
    for (GameEvent* e : events()) {
        if (e && e->is_tile()) tile_events.push_back(e);
    }
}

std::vector<GameEvent*> GameMap::events_xy(double x, double y) const {
    std::vector<GameEvent*> out;
    for (auto& e : events_) {
        if (e && e->pos(x, y)) out.push_back(e.get());
    }
    return out;
}

std::vector<GameEvent*> GameMap::events_xy_nt(double x, double y) const {
    std::vector<GameEvent*> out;
    for (auto& e : events_) {
        if (e && e->pos_nt(x, y)) out.push_back(e.get());
    }
    return out;
}

std::vector<GameEvent*> GameMap::tile_events_xy(double x, double y) const {
    std::vector<GameEvent*> out;
    for (GameEvent* e : tile_events) {
        if (e->pos_nt(x, y)) out.push_back(e);
    }
    return out;
}

int GameMap::event_id_xy(double x, double y) const {
    auto l = events_xy(x, y);
    return l.empty() ? 0 : l[0]->event_id();
}

void GameMap::scroll_down(double d) {
    if (is_loop_vertical()) {
        display_y_ += d;
        display_y_ = std::fmod(display_y_, data()["height"].num());
        if (parallax_loop_y) parallax_y += d;
    } else if (height() >= screen_tile_y()) {
        double last = display_y_;
        display_y_ = std::min(display_y_ + d, height() - screen_tile_y());
        parallax_y += display_y_ - last;
    }
}

void GameMap::scroll_left(double d) {
    if (is_loop_horizontal()) {
        display_x_ += data()["width"].num() - d;
        display_x_ = std::fmod(display_x_, data()["width"].num());
        if (parallax_loop_x) parallax_x -= d;
    } else if (width() >= screen_tile_x()) {
        double last = display_x_;
        display_x_ = std::max(display_x_ - d, 0.0);
        parallax_x += display_x_ - last;
    }
}

void GameMap::scroll_right(double d) {
    if (is_loop_horizontal()) {
        display_x_ += d;
        display_x_ = std::fmod(display_x_, data()["width"].num());
        if (parallax_loop_x) parallax_x += d;
    } else if (width() >= screen_tile_x()) {
        double last = display_x_;
        display_x_ = std::min(display_x_ + d, width() - screen_tile_x());
        parallax_x += display_x_ - last;
    }
}

void GameMap::scroll_up(double d) {
    if (is_loop_vertical()) {
        display_y_ += data()["height"].num() - d;
        display_y_ = std::fmod(display_y_, data()["height"].num());
        if (parallax_loop_y) parallax_y -= d;
    } else if (height() >= screen_tile_y()) {
        double last = display_y_;
        display_y_ = std::max(display_y_ - d, 0.0);
        parallax_y += display_y_ - last;
    }
}

bool GameMap::is_valid(double x, double y) const { return x >= 0 && x < width() && y >= 0 && y < height(); }

bool GameMap::check_passage(double x, double y, int bit) const {
    const Value& flags = tileset()["flags"];
    std::vector<int> tiles;
    for (GameEvent* e : tile_events_xy(x, y)) tiles.push_back(e->tile_id());
    for (int i = 0; i < 4; i++) tiles.push_back(tile_id(x, y, 3 - i));
    for (int t : tiles) {
        int flag = t >= 0 && t < (int)flags.size() ? flags[t].as_int() : 0;
        if ((flag & 0x10) != 0) continue;
        if ((flag & bit) == 0) return true;
        if ((flag & bit) == bit) return false;
    }
    return false;
}

int GameMap::tile_id(double, double, int) const { return 0; }

bool GameMap::check_layered_tiles_flags(double x, double y, int bit) const {
    const Value& flags = tileset()["flags"];
    for (int i = 0; i < 4; i++) {
        int t = tile_id(x, y, 3 - i);
        int flag = t >= 0 && t < (int)flags.size() ? flags[t].as_int() : 0;
        if ((flag & bit) != 0) return true;
    }
    return false;
}

int GameMap::current_map_level() const {
    int v = params().map_level_variable;
    if (!v) return 0;
    return (int)game_variables().number(v);
}

void GameMap::set_current_map_level(int level) {
    int v = params().map_level_variable;
    if (v) game_variables().set_value(v, js::Val::num(level));
}

bool GameMap::is_passable(double x, double y, int d) const {
    if (!is_tiled_map()) return check_passage(x, y, (1 << (d / 2 - 1)) & 0x0f);
    int lvl = current_map_level();
    if (lvl < 0 || lvl >= (int)levels.size() || !levels[(size_t)lvl].valid) return false;
    const MapLevel& L = levels[(size_t)lvl];
    double fi = x + width() * y;
    if (fi < 0 || fi >= (double)L.collision.size() || fi != std::floor(fi)) {
        if (d == 4 || d == 8 || d == 6 || d == 2) return false;
        return false;
    }
    size_t i = (size_t)fi;
    uint8_t a = L.arrows[i];
    if (d == 4 && !(a & 1)) return false;
    if (d == 8 && !(a & 2)) return false;
    if (d == 6 && !(a & 4)) return false;
    if (d == 2 && !(a & 8)) return false;
    return L.collision[i] == 0;
}

bool GameMap::is_ladder(double x, double y) const {
    return is_valid(x, y) && (check_layered_tiles_flags(x, y, 0x20) || region_id(x, y) == params().ladder_region);
}

bool GameMap::is_bush(double x, double y) const {
    bool on_region = region_id(x, y) == params().bush_region;
    return is_valid(x, y) && (check_layered_tiles_flags(x, y, 0x40) || on_region);
}

bool GameMap::is_counter(double x, double y) const {
    return is_valid(x, y) && (check_layered_tiles_flags(x, y, 0x80) || region_id(x, y) == params().counter_region);
}

bool GameMap::is_damage_floor(double x, double y) const {
    return is_valid(x, y) && (check_layered_tiles_flags(x, y, 0x100) || region_id(x, y) == params().damage_floor_region);
}

int GameMap::terrain_tag(double x, double y) const {
    if (!is_valid(x, y)) return 0;
    tileset();
    const Value& flags = *tileset_flags_;
    for (int i = 0; i < 4; i++) {
        int t = tile_id(x, y, 3 - i);
        int flag = t >= 0 && t < (int)flags.size() ? flags[t].as_int() : 0;
        int tag = flag >> 12;
        if (tag > 0) return tag;
    }
    return 0;
}

int GameMap::region_id(double x, double y) const {
    if (!is_tiled_map()) return is_valid(x, y) ? tile_id(x, y, 5) : 0;
    int lvl = current_map_level();
    if (lvl < 0 || lvl >= (int)levels.size() || !levels[(size_t)lvl].valid) return -1;
    const MapLevel& L = levels[(size_t)lvl];
    double fi = x + width() * y;
    if (fi < 0 || fi >= (double)L.regions.size() || fi != std::floor(fi)) return -1;
    return L.regions[(size_t)fi];
}

void GameMap::start_scroll(int direction, double distance, double speed) {
    scroll_direction = direction;
    scroll_rest = distance;
    scroll_speed = speed;
}

void GameMap::update(bool scene_active) {
    refresh_if_needed();
    if (scene_active) {
        perf::Scope ps(perf::Interpreter);
        update_interpreter();
    }
    update_scroll();
    {
        perf::Scope ps(perf::MapEvents);
        update_events();
    }
    update_vehicles();
    update_parallax();
}

void GameMap::base_update_scroll() {
    if (is_scrolling()) {
        double lx = display_x_, ly = display_y_;
        do_scroll(scroll_direction, scroll_distance());
        if (display_x_ == lx && display_y_ == ly) scroll_rest = 0;
        else scroll_rest -= scroll_distance();
    }
}

double GameMap::cam_screen_x() const {
    if (cam_point) {
        double tw = tile_width();
        return std::ceil(adjust_x(cam_px) * tw + tw / 2);
    }
    GameCharacterBase* c = cam_target.get();
    return c ? c->screen_x() : 0;
}

double GameMap::cam_screen_y() const {
    if (cam_point) {
        double th = tile_height();
        return std::round(adjust_y(cam_py) * th + th);
    }
    GameCharacterBase* c = cam_target.get();
    return c ? c->screen_y() : 0;
}

void GameMap::update_scroll() {
    if (has_scroll_target && scroll_duration > 0) {
        update_lerp_scrolling();
        return;
    }
    if (cam_norm) {
        base_update_scroll();
        return;
    }
    scroll_rest = 0;
    double cw = 640 / 2.0, ch = 480 / 2.0;
    if (!cam_point && !cam_target.get()) return;
    double sx0 = cam_screen_x() * zoom_x;
    double sy0 = cam_screen_y() * zoom_y;
    double sx = std::fabs(sx0 - cw) / cam_speed;
    double sy = std::fabs(sy0 - ch) / cam_speed;
    if (sx < 0.005) sx = 0;
    if (sy < 0.005) sy = 0;
    if (sy0 < ch) scroll_up(sy);
    else if (sy0 > ch) scroll_down(sy);
    if (sx0 < cw) scroll_left(sx);
    else if (sx0 > cw) scroll_right(sx);
}

double GameMap::scroll_distance() const { return std::pow(2, scroll_speed) / 256; }

void GameMap::do_scroll(int direction, double distance) {
    switch (direction) {
    case 2: scroll_down(distance); break;
    case 4: scroll_left(distance); break;
    case 6: scroll_right(distance); break;
    case 8: scroll_up(distance); break;
    }
}

void GameMap::update_events() {
    std::vector<std::shared_ptr<GameEvent>> group;
    group.swap(update_group_);
    group.clear();
    group.reserve(events_.size());
    for (auto& e : events_) {
        if (e) group.push_back(e);
    }
    for (auto& e : group) e->update();
    group.clear();
    if (group.capacity() > update_group_.capacity()) update_group_.swap(group);
    for (size_t i = 0; i < common_events.size(); i++) {
        if (common_events[i]) common_events[i]->update();
    }
}

void GameMap::update_vehicles() {
    for (auto& v : vehicles_) v->update();
}

void GameMap::update_parallax() {
    if (parallax_loop_x) parallax_x += parallax_sx / tile_width() / 2;
    if (parallax_loop_y) parallax_y += parallax_sy / tile_height() / 2;
}

void GameMap::change_tileset(int id) {
    tileset_id = id;
    refresh();
}

void GameMap::change_battleback(const std::string& b1, const std::string& b2) {
    has_battleback1 = has_battleback2 = true;
    battleback1_name_ = b1;
    battleback2_name_ = b2;
}

void GameMap::change_parallax(const std::string& name, bool loop_x, bool loop_y, double sx, double sy) {
    parallax_name_ = name;
    parallax_zero = !name.empty() && name[0] == '!';
    if (parallax_loop_x && !loop_x) parallax_x = 0;
    if (parallax_loop_y && !loop_y) parallax_y = 0;
    parallax_loop_x = loop_x;
    parallax_loop_y = loop_y;
    parallax_sx = sx;
    parallax_sy = sy;
}

void GameMap::update_interpreter() {
    for (;;) {
        interpreter->update();
        if (interpreter->is_running()) return;
        if (interpreter->event_id() > 0) {
            unlock_event(interpreter->event_id());
            interpreter->clear();
        }
        if (!setup_starting_event()) return;
    }
}

void GameMap::unlock_event(int id) {
    if (GameEvent* e = event(id)) e->unlock();
}

bool GameMap::setup_starting_event() {
    refresh_if_needed();
    if (interpreter->setup_reserved_common_event()) return true;
    if (setup_starting_map_event()) return true;
    if (setup_autorun_common_event()) return true;
    return false;
}

bool GameMap::setup_starting_map_event() {
    for (GameEvent* e : events()) {
        if (e->is_starting()) {
            e->clear_starting_flag();
            interpreter->setup(e->list(), e->event_id(), e->event_keep());
            return true;
        }
    }
    return false;
}

bool GameMap::setup_autorun_common_event() {
    struct Autorun {
        const Value* list;
        int switch_id;
    };
    static const Value* source = nullptr;
    static std::vector<Autorun> autoruns;
    const Value& ces = data::common_events();
    if (source != &ces) {
        source = &ces;
        autoruns.clear();
        for (size_t i = 0; i < ces.size(); i++) {
            const Value& e = ces[i];
            if (e.is_obj() && e["trigger"].as_int() == 1) autoruns.push_back({&e["list"], e["switchId"].as_int()});
        }
    }
    for (const Autorun& a : autoruns) {
        if (game_switches().value(a.switch_id)) {
            interpreter->setup(a.list, 0, data::keep());
            return true;
        }
    }
    return false;
}

bool GameMap::is_any_event_starting() const {
    for (auto& e : events_) {
        if (e && e->is_starting()) return true;
    }
    return false;
}

void GameMap::random_pos(int event_id, int region) {
    std::vector<std::pair<int, int>> coords;
    int w = data()["width"].as_int(), h = data()["height"].as_int();
    for (int x = 0; x < w; x++) {
        for (int y = 0; y < h; y++) {
            if (region_id(x, y) == region) coords.push_back({x, y});
        }
    }
    if (coords.empty()) return;
    auto c = coords[(size_t)rng::below((int)coords.size())];
    GameEvent* e = event(event_id);
    if (!e) js::vm().throw_type_error("Cannot read property 'setPosition' of undefined");
    e->set_position(c.first, c.second);
}

void GameMap::setup_tiled() {
    setup_tiled_levels();
    const Value& te = tiled()["tevents"];
    for (size_t i = 0; i < te.size(); i++) {
        GameEvent* e = event(te[i][0].as_int());
        if (!e) continue;
        e->locate(te[i][1].num(), te[i][2].num());
        e->load_location();
    }
}

void GameMap::restore_begin(int id) {
    std::shared_ptr<Document> d = load_map_doc(id);
    if (d) doc = d;
    refresh_dims();
    route_cache_clear();
    map_id_ = id;
    tileset_id = data()["tilesetId"].as_int();
    common_events.clear();
    const Value& ces = data::common_events();
    for (size_t i = 0; i < ces.size(); i++) {
        if (ces[i].is_obj() && ces[i]["trigger"].as_int() == 2) common_events.emplace_back(new GameCommonEvent(ces[i]["id"].as_int()));
    }
    process_map_notetags();
    levels.clear();
    reflections.clear();
    if (is_tiled_map()) setup_tiled_levels();
    process_footstep_sound();
}

void GameMap::restore_end() { refresh_tile_events(); }

void GameMap::setup_tiled_levels() {
    const Value& t = tiled();
    int size = width() * height();
    const Value& lv = t["levels"];
    levels.assign(lv.size(), MapLevel());
    for (size_t i = 0; i < lv.size(); i++) {
        const Value& l = lv[i];
        if (!l.is_arr() || l.size() < 4) continue;
        MapLevel& L = levels[i];
        L.valid = true;
        L.collision.assign(l[0].bytes, l[0].bytes + l[0].size());
        L.arrows.assign(l[1].bytes, l[1].bytes + l[1].size());
        L.regions.assign(l[2].bytes, l[2].bytes + l[2].size());
        L.change.assign((const int8_t*)l[3].bytes, (const int8_t*)l[3].bytes + l[3].size());
        if ((int)L.collision.size() < size) L.collision.resize((size_t)size, 0);
        if ((int)L.arrows.size() < size) L.arrows.resize((size_t)size, 15);
        if ((int)L.regions.size() < size) L.regions.resize((size_t)size, 0);
        if ((int)L.change.size() < size) L.change.resize((size_t)size, -1);
    }
    if (levels.empty()) {
        levels.resize(1);
        MapLevel& L = levels[0];
        L.valid = true;
        L.collision.assign((size_t)size, 0);
        L.arrows.assign((size_t)size, 15);
        L.regions.assign((size_t)size, 0);
        L.change.assign((size_t)size, -1);
    }
    const Value& rs = t["reflections"];
    for (size_t i = 0; i < rs.size(); i++) {
        const Value& r = rs[i];
        reflections.push_back(Reflection{r[0].num(), r[1].num(), r[2].num(), r[3].num(), r[4].num(), r[5].num(255), r[6].num()});
    }
}

bool GameMap::is_on_reflection(const GameCharacterBase& c) const {
    if (!is_tiled_map() || reflections.empty()) return false;
    double mx = c.real_x * tile_width(), my = c.real_y * tile_height();
    for (auto& r : reflections) {
        if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) return true;
    }
    return false;
}

std::vector<int> GameMap::get_reflections(const GameCharacterBase& c) const {
    std::vector<int> out;
    double mx = c.real_x * tile_width(), my = c.real_y * tile_height();
    for (size_t i = 0; i < reflections.size(); i++) {
        const Reflection& r = reflections[i];
        if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) out.push_back((int)i);
    }
    return out;
}

bool GameMap::check_map_level_changing(double x, double y) {
    int lvl = current_map_level();
    if (lvl < 0 || lvl >= (int)levels.size() || !levels[(size_t)lvl].valid) return false;
    double fi = y * width() + x;
    const MapLevel& L = levels[(size_t)lvl];
    if (fi < 0 || fi >= (double)L.change.size()) return false;
    int to = L.change[(size_t)fi];
    if (to < 0) return false;
    set_current_map_level(to);
    return true;
}

void GameMap::record_morphed_events() {
    recorded_morphed_events = true;
    recorded_morphed_event_data.clear();
    for (GameEvent* e : events()) {
        if (!e->morphed) continue;
        recorded_morphed_event_data[e->event_id()] = {e->morph_map_id, e->morph_event_id};
    }
}

void GameMap::process_morphed_events() {
    if (!recorded_morphed_events) return;
    for (auto& kv : recorded_morphed_event_data) {
        GameEvent* target = event(kv.first);
        if (!target || party_in_battle()) continue;
        std::shared_ptr<const void> keep;
        if (!preloaded_event(kv.second.first, kv.second.second, &keep)) continue;
        target->morph_into(kv.second.first, kv.second.second);
    }
    recorded_morphed_events = false;
}

void GameMap::spawn_event(int mid, int eid, double x, double y, bool preserved) {
    int spawn_id = (int)spawned_events.size() + (int)params().spawner_id;
    SpawnData sd;
    sd.base_map_id = map_id_;
    sd.spawn_id = spawn_id;
    sd.map_id = mid;
    sd.event_id = eid;
    sd.x = x;
    sd.y = y;
    sd.preserved = preserved;
    auto ev = std::make_shared<GameEvent>(mid, eid, &sd);
    if ((int)events_.size() <= spawn_id) events_.resize((size_t)spawn_id + 1);
    events_[(size_t)spawn_id] = ev;
    size_t idx = (size_t)(spawn_id - (int)params().spawner_id);
    auto& store = game_system().get_map_spawned_event_data(map_id_);
    if (store.size() <= idx) store.resize(idx + 1);
    store[idx] = ev;
    spawned_events = store;
}

void GameMap::despawn_event_id(int id) {
    if (id < params().spawner_id) return;
    GameEvent* ev = event(id);
    if (!ev) js::vm().throw_type_error("Cannot read property 'locate' of undefined");
    ev->locate(-1, -1);
    erase_event(id);
    size_t idx = (size_t)(id - (int)params().spawner_id);
    auto& store = game_system().get_map_spawned_event_data(map_id_);
    if (idx < store.size()) store[idx].reset();
    spawned_events = store;
}

std::vector<std::pair<int, int>> GameMap::valid_spawn_points(const std::vector<int>& regions) {
    std::vector<std::pair<int, int>> out;
    for (int x = 0; x < width(); ++x) {
        for (int y = 0; y < height(); ++y) {
            if (std::find(regions.begin(), regions.end(), region_id(x, y)) == regions.end()) continue;
            if (!events_xy(x, y).empty()) continue;
            if (game_player().x_ == x && game_player().y_ == y) continue;
            if (boat()->pos_nt(x, y)) continue;
            if (ship()->pos_nt(x, y)) continue;
            out.push_back({x, y});
        }
    }
    return out;
}

GameEvent* GameMap::first_spawned_event() const {
    for (auto& e : spawned_events) {
        if (e) return e.get();
    }
    return nullptr;
}

GameEvent* GameMap::last_spawned_event() const {
    for (size_t i = spawned_events.size(); i-- > 0;) {
        if (spawned_events[i]) return spawned_events[i].get();
    }
    return nullptr;
}

bool GameMap::is_region_event(double x, double y) const { return is_valid(x, y) && region_event_tag(x, y); }

int GameMap::get_unique_region_common_event(int region) const {
    auto it = region_common_events.find(region);
    return it != region_common_events.end() && it->second ? it->second : 0;
}

bool GameMap::region_event_tag(double x, double y) const {
    int r = region_id(x, y);
    if (r != -1 && r <= 0) return false;
    if (get_unique_region_common_event(r) > 0) return true;
    auto it = params().region_events.find(r);
    return it != params().region_events.end() && it->second > 0;
}

bool GameMap::move_after_common_event() {
    GameInterpreter& in = *interpreter;
    const Value* list = in.list();
    if (!list) return false;
    if (in.event_id() > 0) return false;
    if (game_temp().destination_x == game_player().x_ && game_temp().destination_y == game_player().y_) game_temp().clear_destination();
    for (size_t i = 0; i < list->size(); ++i) {
        int code = (*list)[i]["code"].as_int();
        if (code == 201 || code == 205 || code == 230 || code == 232 || code == 261 || code == 301) return false;
    }
    return true;
}

std::string GameMap::get_region_battleback_name(int type) const {
    if (!doc) return "";
    int region = game_player().region_id();
    for (auto& line : re::split_lines(note())) {
        re::Groups g;
        if (re::search("<REGION[ ](\\d+)[ ]BATTLEBACK(\\d+):[ ](.*)>", "i", line, &g)) {
            if (g.num(1) == region && g.num(2) == type) return g[3];
        }
    }
    return "";
}

void GameMap::reset_all_event_locations() {
    for (GameEvent* e : events()) e->reset_location();
}

bool GameMap::is_slippery(double x, double y) const {
    if (party_in_battle()) return false;
    if (is_valid(x, y) && tileset().is_obj()) {
        double sr = params().slip_region;
        if (sr != 0 && region_id(x, y) == sr) return true;
        if (slip_tags_.empty()) return false;
        int tag = terrain_tag(x, y);
        for (int t : slip_tags_) {
            if (t == tag) return true;
        }
    }
    return false;
}

bool GameMap::is_event_run_quick() const { return interpreter->is_running() || is_any_event_starting_quick(); }

bool GameMap::is_any_event_starting_quick() const { return is_any_event_starting(); }

void GameMap::cam_target_set(const CharRef& target, double speed) {
    cam_target = target;
    cam_point = false;
    cam_norm = false;
    cam_speed = speed ? speed : 800;
}

void GameMap::cam_control(const std::vector<std::string>& args) {
    if (args.empty()) return;
    std::string key = to_lower(args[0]);
    double speed = 100;
    auto value = [](const std::string& s) -> double {
        if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) {
            std::string r = s;
            size_t p = r.find('v');
            if (p != std::string::npos) r.erase(p, 1);
            return game_variables().number((int)js_number(r));
        }
        return js_number(s);
    };
    if (key == "player") {
        if (args.size() > 1 && !args[1].empty()) speed = value(args[1]);
        cam_target_set(CharRef::of(&game_player()), speed);
    } else if (key == "event") {
        int id = args.size() > 1 ? (int)value(args[1]) : 0;
        if (args.size() > 2 && !args[2].empty()) speed = value(args[2]);
        CharRef r;
        r.kind = CharRef::Event;
        r.id = id;
        cam_target_set(r, speed);
    } else if (key == "disable") {
        cam_target = CharRef::of(&game_player());
        cam_point = false;
        cam_norm = true;
        has_saved_cam = false;
        saved_cam_args.clear();
        return;
    } else {
        double px = value(args[0]);
        double py = args.size() > 1 ? value(args[1]) : 0;
        if (args.size() > 2 && !args[2].empty()) speed = value(args[2]);
        cam_target_set(CharRef(), speed);
        cam_point = true;
        cam_px = px;
        cam_py = py;
    }
    has_saved_cam = true;
    saved_cam_args = args;
}

void GameMap::on_map_loaded_camera() {
    if (!cam_norm) {
        if (!has_saved_cam) {
            has_saved_cam = true;
            saved_cam_args = {"PLAYER"};
        }
        cam_control(saved_cam_args);
    }
}

void GameMap::setup_common_event(int id) {
    const Value& ce = data::common_events()[id];
    if (ce.is_obj()) {
        int eid = interpreter->is_on_current_map() ? interpreter->event_id() : 0;
        interpreter->setup_child(&ce["list"], eid, data::keep());
    }
}

void GameMap::stop_current_event() {
    if (is_event_running()) interpreter->terminate();
}

int GameMap::position_auto_move_direction(double x, double y) const {
    int r = region_id(x, y);
    auto it = region_move_directions.find(r);
    return it != region_move_directions.end() ? it->second : 0;
}

void GameMap::scroll_to_target(double x, double y, double duration) {
    double end_x = width() - screen_tile_x();
    double end_y = height() - screen_tile_y();
    x = end_x < 0 ? end_x / 2 : std::min(std::max(x, 0.0), end_x);
    y = end_y < 0 ? end_y / 2 : std::min(std::max(y, 0.0), end_y);
    has_scroll_target = true;
    scroll_start_x = display_x_;
    scroll_start_y = display_y_;
    scroll_target_x = x;
    scroll_target_y = y;
    scroll_duration = duration;
    scrolling_time = 0;
}

void GameMap::update_lerp_scrolling() {
    scrolling_time++;
    double t = scrolling_time / scroll_duration;
    double x = (1 - t) * scroll_start_x + t * scroll_target_x;
    double y = (1 - t) * scroll_start_y + t * scroll_target_y;
    set_display_pos(x, y);
    if (scrolling_time >= scroll_duration) {
        has_scroll_target = false;
        scroll_duration = 0;
    }
}

std::shared_ptr<GameMapFog> GameMap::get_map_fog(const std::string& name) const {
    auto it = map_fogs.find(name);
    return it == map_fogs.end() ? nullptr : it->second;
}

void GameMap::create_filter(const std::string& id, const std::string& filter, int target, int ch, bool char_is_number) {
    auto it = filter_con.find(id);
    bool existed = it != filter_con.end() && std::find(filter_con_order.begin(), filter_con_order.end(), id) != filter_con_order.end();
    if (it != filter_con.end() && it->second) scenes::remove_filter(it->second.get());
    bool has_map = char_is_number && !party_in_battle();
    filter_con[id].reset(new FilterController(filter, id, target, char_is_number, ch, has_map, has_map ? map_id_ : 0));
    if (!existed) filter_con_order.push_back(id);
}

void GameMap::update_filter_con_arr() {
    for (auto& id : filter_con_order) {
        FilterController* fc = filter_con[id].get();
        if (!fc) continue;
        if (fc->is_map_event_only() && party_in_battle()) continue;
        fc->update();
    }
}

FilterController* GameMap::get_filter_controller(const std::string& id) const {
    auto it = filter_con.find(id);
    return it == filter_con.end() ? nullptr : it->second.get();
}

bool GameMap::set_filter(const std::string& id, const FilterArgs& target) {
    FilterController* f = get_filter_controller(id);
    if (!f) return false;
    f->set(target.values, target.has);
    return true;
}

bool GameMap::enable_filter(const std::string& id, bool enabled) {
    FilterController* f = get_filter_controller(id);
    if (!f) return false;
    f->enable(enabled);
    return true;
}

bool GameMap::move_filter(const std::string& id, const FilterArgs& target, int d) {
    FilterController* f = get_filter_controller(id);
    if (!f) return false;
    f->clear_move_queue();
    f->move(target.values, target.has, d ? d : 1);
    return true;
}

bool GameMap::move_filter_queue(const std::string& id, const FilterArgs& target, int d) {
    FilterController* f = get_filter_controller(id);
    if (!f) return false;
    f->move_queue_push(target.values, target.has, d ? d : 1);
    return true;
}

bool GameMap::erase_filter(const std::string& id) {
    FilterController* f = get_filter_controller(id);
    if (!f) return false;
    f->erase();
    return true;
}

bool GameMap::erase_filter_after_move(const std::string& id) {
    FilterController* f = get_filter_controller(id);
    if (!f) return false;
    f->erase_after_move();
    return true;
}

bool GameMap::set_filter_addi_time(const std::string& id, double t) {
    FilterController* f = get_filter_controller(id);
    if (!f) return false;
    f->set_addi_time(t);
    return true;
}

void GameMap::setup_tkm_filters() {
    std::string n = note();
    size_t from = 0;
    re::Groups g;
    std::vector<std::string> filters, sets, speeds;
    while (re::search("<([^<>:]+)(:?)([^>]*)>", "", n, &g, from)) {
        if (g[2] == ":") {
            if (g[1] == "Filter") filters.push_back(g[3]);
            else if (g[1] == "SetFilter") sets.push_back(g[3]);
            else if (g[1] == "SetFilterSpeed") speeds.push_back(g[3]);
        }
        from = g.index + std::max<size_t>(g.length, 1);
    }
    if (filters.empty()) return;
    for (auto& f0 : filters) {
        auto f = split(f0, ',');
        double t = f.size() > 2 ? js_parse_int(f[2]) : NAN;
        create_filter(f[0], f.size() > 1 ? f[1] : "", std::isnan(t) ? 0 : (int)t, 0, true);
    }
    if (sets.empty()) return;
    for (auto& f0 : sets) {
        auto f = split(f0, ',');
        std::vector<std::string> rest(f.begin() + 1, f.end());
        FilterArgs a = filter_param_process(rest);
        if (FilterController* fc = get_filter_controller(f[0])) fc->set(a.values, a.has);
    }
    if (speeds.empty()) return;
    for (auto& f0 : speeds) {
        auto f = split(f0, ',');
        double sp = f.size() > 1 ? js_number(f[1]) : NAN;
        if (std::isnan(sp) || sp == 0) sp = 0.01;
        if (get_filter_controller(f[0])) set_filter_addi_time(f[0], sp);
    }
}

std::string GameMap::display_name_dim_color(int which) const {
    re::Groups g;
    std::string pat = which == 1 ? "<DIM COLOR 1:[ ]*(\\d+(?:\\s*,\\s*\\d+)*)>" : "<DIM COLOR 2:[ ]*(\\d+(?:\\s*,\\s*\\d+)*)>";
    if (re::search(pat.c_str(), "i", note(), &g)) {
        auto nums = re::all("\\d+", "g", g[1]);
        auto at = [&](size_t i) { return i < nums.size() ? std::atof(nums[i].c_str()) : NAN; };
        auto clampi = [](double v, double lo, double hi) { return std::min(std::max(v, lo), hi); };
        char buf[96];
        double a = clampi(at(3) * 0.01, 0, 1);
        std::snprintf(buf, sizeof(buf), "rgba(%d,%d,%d,%s)", (int)clampi(at(0), 0, 255), (int)clampi(at(1), 0, 255), (int)clampi(at(2), 0, 255),
                      js::number_to_string(a).c_str());
        return buf;
    }
    return data::plugin_param("Olivia_MapDisplayNameCore", which == 1 ? "Dim Color 1" : "Dim Color 2", which == 1 ? "rgba(0, 0, 0, 0.6)" : "rgba(0, 0, 0, 0)");
}

void GameMap::remove_footprint(const GameFootprint* f) {
    for (size_t i = 0; i < footprints_.size(); i++) {
        if (footprints_[i].get() == f) {
            footprints_[i]->removed = true;
            footprints_.erase(footprints_.begin() + (long)i);
            return;
        }
    }
}

const AudioObj* GameMap::get_footsound(double x, double y) const {
    if (footsounds.empty()) return nullptr;
    for (auto& r : footsounds) {
        if (r.contains(x, y)) return &r.sound;
    }
    return nullptr;
}

void GameMap::process_footstep_sound() {
    footsounds.clear();
    std::string n = note();
    for (auto& m : re::all("<FootSound:\\s*(.*)>", "g", n)) {
        re::Groups g;
        if (!re::search("<FootSound:\\s*(.*)>", "", m, &g)) continue;
        std::string body = g[1];
        std::vector<std::string> parts;
        size_t s0 = 0;
        while (true) {
            re::Groups sep;
            if (!re::search("\\s*\\|\\s*", "", body, &sep, s0)) {
                parts.push_back(body.substr(s0));
                break;
            }
            parts.push_back(body.substr(s0, sep.index - s0));
            s0 = sep.index + std::max<size_t>(sep.length, 1);
        }
        auto nums = [](const std::string& s) {
            std::vector<double> out;
            size_t a = 0;
            while (true) {
                re::Groups sep;
                if (!re::search("\\s*,\\s*", "", s, &sep, a)) {
                    out.push_back(js_number(s.substr(a)));
                    break;
                }
                out.push_back(js_number(s.substr(a, sep.index - a)));
                a = sep.index + std::max<size_t>(sep.length, 1);
            }
            return out;
        };
        if (parts.size() < 3) continue;
        auto sp = nums(parts[0]);
        auto ep = nums(parts[1]);
        std::vector<std::string> sound;
        size_t a = 0;
        while (true) {
            re::Groups sep;
            if (!re::search("\\s*,\\s*", "", parts[2], &sep, a)) {
                sound.push_back(parts[2].substr(a));
                break;
            }
            sound.push_back(parts[2].substr(a, sep.index - a));
            a = sep.index + std::max<size_t>(sep.length, 1);
        }
        FootsoundRect r;
        r.x = sp.size() > 0 ? sp[0] : NAN;
        r.y = sp.size() > 1 ? sp[1] : NAN;
        r.w = std::fabs(r.x - (ep.size() > 0 ? ep[0] : NAN));
        r.h = std::fabs(r.y - (ep.size() > 1 ? ep[1] : NAN));
        r.sound.valid = true;
        r.sound.name = sound[0];
        r.sound.volume = sound.size() > 1 ? js_parse_int(sound[1]) : NAN;
        r.sound.pitch = sound.size() > 2 ? js_parse_int(sound[2]) : NAN;
        r.sound.pan = 0;
        footsounds.push_back(r);
    }
}

}
