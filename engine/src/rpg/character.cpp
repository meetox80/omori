#include "character.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "../core/regex.h"
#include "../js/random.h"
#include "../js/vm.h"
#include "audio_manager.h"
#include "battler.h"
#include "data.h"
#include "game.h"
#include "interpreter.h"
#include "params.h"
#include "party.h"

namespace rpg {

namespace fc {
bool follower_control = false;
int follower_control_id = -1;
bool follower_stop = false;
bool follower_collision = false;
}

namespace {

int random_int(int n) { return rng::below(n); }

bool contains(const std::vector<double>& v, double x) { return std::find(v.begin(), v.end(), x) != v.end(); }

bool contains_int(const std::vector<int>& v, int x) { return std::find(v.begin(), v.end(), x) != v.end(); }

bool galv_is_block(double x0, double y0) {
    double x = std::round(x0), y = std::round(y0);
    GameMap& m = game_map();
    if (contains(params().galv_block_regions, m.region_id(x, y))) return true;
    if (contains(params().galv_block_terrain, m.terrain_tag(x, y))) return true;
    for (GameEvent* ev : m.events_xy(x, y)) {
        if (ev->block_los) return true;
    }
    return false;
}

bool galv_los(const GameCharacterBase& c1, const GameCharacterBase& c2) {
    double ax = c1.x_, ay = c1.y_, bx = c2.x_, by = c2.y_;
    switch (c2.direction_) {
    case 2:
        if (by > ay) return false;
        break;
    case 4:
        if (bx < ax) return false;
        break;
    case 6:
        if (bx > ax) return false;
        break;
    case 8:
        if (by < ay) return false;
        break;
    }
    if (std::fabs(ax - bx) >= std::fabs(ay - by)) {
        bool null_slope = ax == bx;
        double slope = null_slope ? 0 : (ay - by) / (ax - bx);
        double icpt = null_slope ? ax : ay - slope * ax;
        for (double x = ax; x <= bx; x++) {
            double y = null_slope ? 0 * x + icpt : slope * x + icpt;
            if (galv_is_block(x, y)) return false;
        }
        for (double x = ax; x >= bx; x--) {
            double y = null_slope ? 0 * x + icpt : slope * x + icpt;
            if (galv_is_block(x, y)) return false;
        }
    } else if (std::fabs(ay - by) >= std::fabs(ax - bx)) {
        bool null_slope = ay == by;
        double slope = null_slope ? 0 : (ax - bx) / (ay - by);
        double icpt = null_slope ? ay : ax - slope * ay;
        for (double y = ay; y <= by; y++) {
            double x = slope * y + icpt;
            if (galv_is_block(x, y)) return false;
        }
        for (double y = ay; y >= by; y--) {
            double x = slope * y + icpt;
            if (galv_is_block(x, y)) return false;
        }
    }
    return true;
}


}

double RouteCmd::param_num(size_t i, double def) const {
    if (synthetic) return i == 0 ? num : def;
    if (!params || !params->is_arr() || i >= params->size()) return def;
    const Value& v = (*params)[i];
    return v.is_num() ? v.num() : (v.t == Value::Bool ? (v.b ? 1 : 0) : def);
}

std::string RouteCmd::param_str(size_t i) const {
    if (!params || !params->is_arr() || i >= params->size()) return "";
    return (*params)[i].string();
}

RoutePtr route_from(const Value& v, std::shared_ptr<const void> keep) {
    auto r = std::make_shared<MoveRoute>();
    r->keep = std::move(keep);
    if (!v.is_obj()) return r;
    r->repeat = v["repeat"].truthy();
    r->skippable = v["skippable"].truthy();
    r->wait = v["wait"].truthy();
    const Value& list = v["list"];
    for (size_t i = 0; i < list.size(); i++) {
        RouteCmd c;
        c.code = list[i]["code"].as_int();
        const Value* p = list[i].find("parameters");
        c.params = p;
        r->list.push_back(c);
    }
    return r;
}

static std::unordered_map<const Value*, RoutePtr>& route_cache() {
    static std::unordered_map<const Value*, RoutePtr> c;
    return c;
}

RoutePtr route_shared(const Value& v, std::shared_ptr<const void> keep) {
    auto it = route_cache().find(&v);
    if (it != route_cache().end() && it->second->keep == keep) return it->second;
    RoutePtr r = route_from(v, keep);
    route_cache()[&v] = r;
    return r;
}

RoutePtr route_copy(const RoutePtr& r) {
    if (!r) return r;
    return std::make_shared<MoveRoute>(*r);
}

void route_cache_clear() { route_cache().clear(); }

CharRef CharRef::of(const GameCharacterBase* c) {
    CharRef r;
    if (!c) return r;
    if (c == &game_player()) {
        r.kind = Player;
        return r;
    }
    if (const GameEvent* e = dynamic_cast<const GameEvent*>(c)) {
        r.kind = Event;
        r.id = e->event_id_;
        return r;
    }
    if (const GameFollower* f = dynamic_cast<const GameFollower*>(c)) {
        r.kind = Follower;
        r.id = f->member_index - 1;
        return r;
    }
    if (const GameVehicle* v = dynamic_cast<const GameVehicle*>(c)) {
        r.kind = Vehicle;
        r.id = v->is_boat() ? 0 : v->is_ship() ? 1 : 2;
    }
    return r;
}

GameCharacter* CharRef::get() const {
    switch (kind) {
    case Player: return &game_player();
    case Event: return game_map().event(id);
    case Follower: return game_player().followers().follower(id);
    case Vehicle: return id >= 0 && id < (int)game_map().vehicles_.size() ? game_map().vehicles_[id].get() : nullptr;
    default: return nullptr;
    }
}

MovementGraphic* MovementGraphics::get(const std::string& n) {
    if (n == "idle") return &idle;
    if (n == "walking") return &walking;
    if (n == "running") return &running;
    if (n == "climbing") return &climbing;
    return nullptr;
}

namespace horror {

void clear(js::Val& hf) { hf = js::make_object(); }

static double hparam(const char* key, double def) {
    std::string v = data::plugin_param("Olivia_HorrorEffects", key, "");
    if (v.empty()) return def;
    double d = js_number(v);
    return std::isnan(d) ? def : d;
}

static bool hbool(const char* key) {
    std::string v = data::plugin_param("Olivia_HorrorEffects", key, "true");
    return to_lower(trim(v)) == "true";
}

void create(js::Val& hf, const std::string& type0) {
    if (!hf.is_obj()) clear(hf);
    std::string type = to_lower(type0);
    auto set = [](js::Val& o, const char* k, js::Val v) { o.o->set(js::atom(k), std::move(v)); };
    if (type.find("noise") != std::string::npos && !hf.o->find(js::atom("noiseFilter"))) {
        js::Val f = js::make_object();
        set(f, "noise", js::Val::num(hparam("Noise Default", 0.5)));
        set(f, "animated", js::Val::boolean(hbool("Noise Animated")));
        set(f, "needUpdate", js::Val::boolean(true));
        hf.o->set(js::atom("noiseFilter"), f);
    } else if (type.find("glitch") != std::string::npos && !hf.o->find(js::atom("glitchFilter"))) {
        double slices = hparam("Glitch Slices", 10);
        js::Val f = js::make_object();
        set(f, "slices", js::Val::num(slices));
        set(f, "offset", js::Val::num(hparam("Glitch Offset", 100)));
        set(f, "sliceMin", js::Val::num(std::ceil(slices / 2)));
        set(f, "sliceMax", js::Val::num(slices));
        set(f, "animated", js::Val::boolean(hbool("Glitch Animated")));
        set(f, "aniFrequency", js::Val::num(hparam("Glitch Animated Frequency", 300)));
        set(f, "aniStrength", js::Val::num(hparam("Glitch Animated Strength", 30)));
        set(f, "needUpdate", js::Val::boolean(true));
        hf.o->set(js::atom("glitchFilter"), f);
    } else if (type.find("tv") != std::string::npos && !hf.o->find(js::atom("tvFilter"))) {
        js::Val f = js::make_object();
        set(f, "lineWidth", js::Val::num(hparam("TV Line Thickness", 5)));
        set(f, "vignetting", js::Val::num(hparam("TV Corner Size", 0.3)));
        set(f, "animated", js::Val::boolean(hbool("TV Animated")));
        set(f, "aniSpeed", js::Val::num(hparam("TV Speed", 0.25)));
        set(f, "needUpdate", js::Val::boolean(true));
        hf.o->set(js::atom("tvFilter"), f);
    }
}

void remove(js::Val& hf, const std::string& type) {
    if (!hf.is_obj()) clear(hf);
    hf.o->set(js::atom(type + "Filter"), js::Val());
}

void set_value(js::Val& hf, const std::string& type, const std::string& key, const js::Val& v) {
    if (!hf.is_obj()) clear(hf);
    js::Val* f = hf.o->find(js::atom(type + "Filter"));
    if (f && f->truthy() && f->is_obj()) {
        f->o->set(js::atom(key), v);
        f->o->set(js::atom("needUpdate"), js::Val::boolean(true));
    }
}

js::Val copy(const js::Val& hf) {
    if (!hf.is_obj()) return hf;
    try {
        return js::json_parse_text(js::json_stringify(hf));
    } catch (...) {
        return js::make_object();
    }
}

}

GameCharacterBase::GameCharacterBase() { GameCharacterBase::init_members(); }

void GameCharacterBase::init_members() {
    clear_emf_state();
    has_custom_frame_x = has_custom_frame_y = has_custom_frame_w = has_custom_frame_h = false;
    single_frame = false;
    x_ = y_ = 0;
    real_x = real_y = 0;
    move_speed_ = 4;
    move_frequency_ = 6;
    opacity_ = 255;
    blend_mode_ = 0;
    direction_ = 2;
    pattern_ = 1;
    priority_type = 1;
    tile_id_ = 0;
    character_name_.clear();
    character_index_ = 0;
    is_object_character_ = false;
    walk_anime = true;
    step_anime = false;
    direction_fix = false;
    through = false;
    transparent = false;
    bush_depth_ = 0;
    animation_id_ = 0;
    balloon_id_ = 0;
    animation_playing = false;
    balloon_playing = false;
    animation_count = 0;
    stop_count = 0;
    jump_count = 0;
    jump_peak = 0;
    movement_success = true;
    reflections.clear();
    init_sprite_offsets();
    target = CharRef();
    has_target_xy = false;
    has_bobble = false;
    has_input_icon = false;
    clear_spin();
    has_shadow = false;
    custom_frame_animation.reset();
    disable_footprints = true;
    footprint_x = footprint_y = 0;
    last_direction = direction_;
    horror::clear(horror_filters);
}

double GameCharacterBase::jump_height() const {
    double d = jump_count - jump_peak;
    return (jump_peak * jump_peak - d * d) / 2;
}

bool GameCharacterBase::pos(double x, double y) const {
    if (has_hitbox) return is_within_hitboxes(x, y);
    return x_ == x && y_ == y;
}

double GameCharacterBase::real_move_speed() const {
    if (on_slippery_floor() && params().slip_speed > 0) return params().slip_speed;
    return move_speed_ + (is_dashing() ? 1 : 0);
}

double GameCharacterBase::distance_per_frame() const {
    double d = std::pow(2, real_move_speed()) / 256;
    return d * (48 / std::min(game_map().tile_width(), game_map().tile_height()));
}

void GameCharacterBase::straighten() {
    if (is_emf) {
        if (has_walk_anime() || has_step_anime()) pattern_ = -1;
        animation_count = 0;
    } else {
        if (has_walk_anime() || has_step_anime()) pattern_ = 1;
        animation_count = 0;
    }
}

bool GameCharacterBase::can_pass(double x, double y, int d) {
    if (hitbox_up || hitbox_left || hitbox_right || hitbox_down) return is_through();
    GameMap& m = game_map();
    double x2 = m.round_x_with_direction(x, d);
    double y2 = m.round_y_with_direction(y, d);
    if (!m.is_valid(x2, y2)) return false;
    if (is_through() || is_debug_through()) return true;
    if (!is_map_passable(x, y, d)) return false;
    if (is_collided_with_characters(x2, y2)) return false;
    return true;
}

bool GameCharacterBase::can_pass_diagonally(double x, double y, int horz, int vert) {
    if (hitbox_up || hitbox_left || hitbox_right || hitbox_down) return is_through();
    GameMap& m = game_map();
    double x2 = m.round_x_with_direction(x, horz);
    double y2 = m.round_y_with_direction(y, vert);
    if (can_pass(x, y, vert) && can_pass(x, y2, horz)) return true;
    if (can_pass(x, y, horz) && can_pass(x2, y, vert)) return true;
    return false;
}

bool GameCharacterBase::base_is_map_passable(double x, double y, int d) {
    GameMap& m = game_map();
    double x2 = m.round_x_with_direction(x, d);
    double y2 = m.round_y_with_direction(y, d);
    int d2 = reverse_dir(d);
    return m.is_passable(x, y, d) && m.is_passable(x2, y2, d2);
}

bool GameCharacterBase::is_map_passable(double x, double y, int d) {
    if (is_event_region_forbid(x, y, d)) return false;
    if (is_player_region_forbid(x, y, d)) return false;
    if (is_event_region_allow(x, y, d)) return true;
    if (is_player_region_allow(x, y, d)) return true;
    return base_is_map_passable(x, y, d);
}

bool GameCharacterBase::base_is_collided_with_characters(double x, double y) {
    return is_collided_with_events(x, y) || is_collided_with_vehicles(x, y);
}

bool GameCharacterBase::is_collided_with_characters(double x, double y) { return base_is_collided_with_characters(x, y); }

bool GameCharacterBase::is_collided_with_events(double x, double y) {
    for (GameEvent* e : game_map().events_xy_nt(x, y)) {
        if (e->is_normal_priority()) return true;
    }
    return false;
}

bool GameCharacterBase::is_collided_with_vehicles(double x, double y) {
    GameMap& m = game_map();
    return m.boat()->pos_nt(x, y) || m.ship()->pos_nt(x, y);
}

void GameCharacterBase::set_position(double x, double y) {
    x_ = std::round(x);
    y_ = std::round(y);
    real_x = x;
    real_y = y;
}

void GameCharacterBase::copy_position(const GameCharacterBase& c) {
    x_ = c.x_;
    y_ = c.y_;
    real_x = c.real_x;
    real_y = c.real_y;
    direction_ = c.direction_;
}

void GameCharacterBase::base_locate(double x, double y) {
    set_position(x, y);
    straighten();
    refresh_bush_depth();
}

void GameCharacterBase::locate(double x, double y) { base_locate(x, y); }

void GameCharacterBase::set_direction(int d) {
    if (!is_direction_fixed() && d) direction_ = d;
    reset_stop_count();
    save_location();
}

double GameCharacterBase::scrolled_x() const { return game_map().adjust_x(real_x); }
double GameCharacterBase::scrolled_y() const { return game_map().adjust_y(real_y); }

double GameCharacterBase::screen_x() const {
    double tw = game_map().tile_width();
    double v = std::ceil(scrolled_x() * tw + tw / 2);
    v += sprite_offset_x_value();
    return std::round(v) + (tremble.active ? tremble.offsetx : 0);
}

void GameCharacterBase::start_tremble(double power, double speed, double stop_cycle) {
    if (!tremble.active) {
        tremble = TrembleInfo();
        tremble.active = true;
    }
    tremble.has_power = true;
    tremble.power = power;
    tremble.speed = speed;
    if (stop_cycle != 0 && !std::isnan(stop_cycle)) {
        tremble.has_stop = true;
        tremble.stop_cycle = stop_cycle;
        tremble.stop_ang = tremble.ang + stop_cycle * M_PI * 2;
    } else {
        tremble.has_stop = false;
        tremble.stop_cycle = 0;
        tremble.stop_ang = 0;
    }
}

bool GameCharacterBase::stop_tremble() {
    if (!tremble.active) return false;
    tremble = TrembleInfo();
    return true;
}

void GameCharacterBase::update_tremble() {
    if (!tremble.active || !tremble.has_power || tremble.power == 0) return;
    tremble.ang += tremble.speed;
    tremble.offsetx = std::round(std::sin(tremble.ang) * tremble.power);
    if (tremble.has_stop && tremble.stop_ang != 0 && tremble.ang > tremble.stop_ang) stop_tremble();
}

double GameCharacterBase::screen_y() const {
    double th = game_map().tile_height();
    double v = std::ceil(scrolled_y() * th + th - shift_y() - jump_height());
    v += sprite_offset_y_value();
    return std::round(v);
}

double GameCharacterBase::screen_z() const {
    if (priority_type == 0) return params().tiled_z_below;
    if (priority_type == 2) return params().tiled_z_above;
    return params().tiled_z_player;
}

bool GameCharacterBase::is_near_the_screen() const {
    if (always_update_movement) return true;
    double gw = 640, gh = 480;
    double tw = game_map().tile_width(), th = game_map().tile_height();
    double px = scrolled_x() * tw + tw / 2 - gw / 2;
    double py = scrolled_y() * th + th / 2 - gh / 2;
    return px >= -gw && px <= gw && py >= -gh && py <= gh;
}

void GameCharacterBase::base_update() {
    if (is_stopping()) update_stop();
    if (is_jumping()) update_jump();
    else if (is_moving()) update_move();
    update_animation();
    update_reflection();
    update_movement_graphics();
    update_spinning();
    update_custom_frame_animation();
    update_tremble();
}

void GameCharacterBase::update() { base_update(); }

void GameCharacterBase::update_stop() {
    stop_count++;
    GameCharacterBase* t = target.get();
    if (target.valid() && t) {
        target_x = t->x_;
        target_y = t->y_;
        has_target_xy = true;
    }
    if (has_target_xy) {
        GameCharacter* self = dynamic_cast<GameCharacter*>(this);
        if (!self) return;
        int direction = self->find_direction_to(target_x, target_y);
        if (direction > 0) {
            move_straight(direction);
            double tx = target_x, ty = target_y;
            if (t) {
                tx = t->x_;
                ty = t->y_;
            }
            if (!is_movement_succeeded()) {
                if (is_position_side_of(tx, ty)) {
                    double x = tx - x_;
                    double y = ty - y_;
                    if (x > 1) x = 0;
                    if (y > 1) y = 0;
                    check_event_trigger_touch(x_ + x, y_ + y);
                }
            }
        }
    }
}

void GameCharacterBase::update_jump() {
    jump_count--;
    real_x = (real_x * jump_count + x_) / (jump_count + 1.0);
    real_y = (real_y * jump_count + y_) / (jump_count + 1.0);
    refresh_bush_depth();
    if (jump_count == 0) {
        real_x = x_ = game_map().round_x(x_);
        real_y = y_ = game_map().round_y(y_);
    }
}

void GameCharacterBase::update_move() {
    double dpf = distance_per_frame();
    if (x_ < real_x) real_x = std::max(real_x - dpf, x_);
    if (x_ > real_x) real_x = std::min(real_x + dpf, x_);
    if (y_ < real_y) real_y = std::max(real_y - dpf, y_);
    if (y_ > real_y) real_y = std::min(real_y + dpf, y_);
    if (!is_moving()) refresh_bush_depth();
}

void GameCharacterBase::update_animation() {
    update_animation_count();
    if (animation_count >= animation_wait()) {
        update_pattern();
        animation_count = 0;
    }
}

double GameCharacterBase::animation_wait() const {
    if (is_emf) {
        double real_speed = real_move_speed();
        double frame_num = max_pattern();
        return std::floor((8 - real_speed) * (params().emf_cycle_time / (4 * frame_num)));
    }
    return (9 - real_move_speed()) * 3;
}

void GameCharacterBase::update_animation_count() {
    if (is_moving() && has_walk_anime()) animation_count += 1.5;
    else if (has_step_anime() || !is_original_pattern()) animation_count++;
}

void GameCharacterBase::base_update_pattern() {
    if (gstop) return;
    if (pattern_move_route_locked) return;
    if (step_behavior_set && step_animation_behavior != "LOOP" && has_step_anime()) {
        update_step_animation_behavior();
        return;
    }
    if (!has_step_anime() && stop_count > 0) {
        reset_pattern();
    } else {
        int mp = max_pattern();
        pattern_ = mp ? (pattern_ + 1) % mp : 0;
    }
}

void GameCharacterBase::update_pattern() { base_update_pattern(); }

int GameCharacterBase::max_pattern() const { return is_emf ? (int)emf.pattern.size() : 4; }

int GameCharacterBase::pattern() const {
    if (slippery_pose()) return (int)params().slip_frame;
    if (is_emf) {
        if (pattern_ < 0) return emf.idle_frame;
        int n = (int)emf.pattern.size();
        if (n == 0) return 0;
        int idx = pattern_ % n;
        return emf.pattern[(size_t)idx];
    }
    return pattern_ < 3 ? pattern_ : 1;
}

bool GameCharacterBase::is_original_pattern() const { return is_emf ? pattern() == -1 : pattern() == 1; }

void GameCharacterBase::reset_pattern() { set_pattern(is_emf ? -1 : 1); }

void GameCharacterBase::refresh_bush_depth() {
    if (is_normal_priority() && !is_object_character() && is_on_bush() && !is_jumping()) {
        if (!is_moving()) bush_depth_ = params().bush_height;
    } else {
        bush_depth_ = 0;
    }
}

bool GameCharacterBase::is_on_ladder() const { return game_map().is_ladder(x_, y_); }
bool GameCharacterBase::is_on_bush() const { return game_map().is_bush(x_, y_); }
int GameCharacterBase::terrain_tag() const { return game_map().terrain_tag(x_, y_); }
int GameCharacterBase::region_id() const { return game_map().region_id(x_, y_); }

void GameCharacterBase::base_increase_steps() {
    if (is_on_ladder()) set_direction(8);
    reset_stop_count();
    refresh_bush_depth();
    event_proximity_increase_steps();
}

void GameCharacterBase::increase_steps() {
    base_increase_steps();
    process_footstep_sound();
}

void GameCharacterBase::set_image(const std::string& name, int index) {
    std::string old_name = character_name_;
    int old_index = character_index_;
    tile_id_ = 0;
    character_name_ = name;
    character_index_ = index;
    is_object_character_ = false;
    size_t i = 0;
    while (i < name.size() && (name[i] == '!' || name[i] == '$')) i++;
    is_object_character_ = name.substr(0, i).find('!') != std::string::npos;
    setup_emf_character();
    if (old_name != name) reset_pattern();
    if (old_name == name && old_index != index) reset_pattern();
    single_frame = name.find("[SF]") != std::string::npos;
}

void GameCharacterBase::set_tile_image(int tile) {
    tile_id_ = tile;
    character_name_.clear();
    character_index_ = 0;
    is_object_character_ = true;
}

void GameCharacterBase::check_event_trigger_touch_front(int d) {
    double x2 = game_map().round_x_with_direction(x_, d);
    double y2 = game_map().round_y_with_direction(y_, d);
    check_event_trigger_touch(x2, y2);
}

void GameCharacterBase::base_move_straight(int d) {
    set_movement_success(can_pass(x_, y_, d));
    if (is_movement_succeeded()) {
        set_direction(d);
        x_ = game_map().round_x_with_direction(x_, d);
        y_ = game_map().round_y_with_direction(y_, d);
        real_x = game_map().x_with_direction(x_, reverse_dir(d));
        real_y = game_map().y_with_direction(y_, reverse_dir(d));
        increase_steps();
    } else {
        set_direction(d);
        check_event_trigger_touch_front(d);
    }
}

void GameCharacterBase::move_straight(int d) {
    double old_x = real_x, old_y = real_y;
    last_direction = direction_;
    base_move_straight(d);
    if (is_moving()) {
        footprint_x = old_x;
        footprint_y = old_y;
        footprint_movement();
    }
}

void GameCharacterBase::base_move_diagonally(int horz, int vert) {
    set_movement_success(can_pass_diagonally(x_, y_, horz, vert));
    if (is_movement_succeeded()) {
        x_ = game_map().round_x_with_direction(x_, horz);
        y_ = game_map().round_y_with_direction(y_, vert);
        real_x = game_map().x_with_direction(x_, reverse_dir(horz));
        real_y = game_map().y_with_direction(y_, reverse_dir(vert));
        increase_steps();
    }
    if (direction_ == reverse_dir(horz)) set_direction(horz);
    if (direction_ == reverse_dir(vert)) set_direction(vert);
}

void GameCharacterBase::move_diagonally(int horz, int vert) { base_move_diagonally(horz, vert); }

void GameCharacterBase::base_jump(double x_plus, double y_plus) {
    if (std::fabs(x_plus) > std::fabs(y_plus)) {
        if (x_plus != 0) set_direction(x_plus < 0 ? 4 : 6);
    } else {
        if (y_plus != 0) set_direction(y_plus < 0 ? 8 : 2);
    }
    x_ += x_plus;
    y_ += y_plus;
    double distance = std::round(std::sqrt(x_plus * x_plus + y_plus * y_plus));
    jump_peak = (int)(10 + distance - move_speed_);
    jump_count = jump_peak * 2;
    reset_stop_count();
    straighten();
}

void GameCharacterBase::jump(double x_plus, double y_plus) { base_jump(x_plus, y_plus); }

bool GameCharacterBase::is_transparent() const {
    if (character_name() == "DEV_TEST") return true;
    return transparent;
}

void GameCharacterBase::request_animation(double id) { animation_id_ = (int)std::round(id); }

void GameCharacterBase::update_reflection() {
    if (!game_map().is_on_reflection(*this)) {
        reflections.clear();
        return;
    }
    reflections = game_map().get_reflections(*this);
}

void GameCharacterBase::clear_emf_state() {
    is_emf = false;
    emf = EmfState();
    emf.idle_frame = params().emf_idle_frame;
}

void GameCharacterBase::setup_emf_character() {
    clear_emf_state();
    const std::string& name = character_name();
    if (!re::search("\\%[\\(\\[][\\d\\s]+[\\)\\]]", "", name)) return;
    re::Groups g;
    if (!re::search("(?:\\%[\\(\\[])[\\d\\s]+(?:[\\)\\]])", "", name, &g)) return;
    std::vector<std::string> digits = re::all("\\d+", "g", g[0]);
    if (digits.empty()) return;
    is_emf = true;
    std::vector<int> args;
    for (auto& d : digits) args.push_back(std::atoi(d.c_str()));
    size_t len = args.size();
    emf.frame_num = args[0];
    emf.idle_frame = len > 1 ? args[1] : params().emf_idle_frame;
    if (len > 2) {
        emf.pattern.assign(args.begin() + 2, args.end());
    } else {
        bool success = false;
        for (auto& dp : params().emf_default_pattern) {
            if (!dp.empty() && dp[0] == emf.frame_num) {
                emf.idle_frame = dp.size() > 1 ? dp[1] : 0;
                emf.pattern.assign(dp.size() > 2 ? dp.begin() + 2 : dp.end(), dp.end());
                success = true;
                break;
            }
        }
        if (!success) {
            emf.pattern.clear();
            int idle_plus = emf.idle_frame + 1;
            for (int i = 0; i < emf.frame_num; i++) emf.pattern.push_back(emf.frame_num ? (i + idle_plus) % emf.frame_num : 0);
        }
    }
}

void GameCharacterBase::update_movement_graphics() {
    if (can_update_movement_graphics()) {
        GameActor* actor = movement_graphics_source();
        if (actor) {
            MovementGraphics& g = actor->character_movement_graphics;
            const std::string name = character_name();
            int index = character_index();
            auto apply = [&](MovementGraphic& m) {
                if (m.set && (name != m.name || index != m.index)) set_image(m.name, m.index);
            };
            if (is_moving()) {
                if (is_climbing()) apply(g.climbing);
                else if (should_use_running_graphics()) apply(g.running);
                else apply(g.walking);
            } else {
                if (is_climbing()) apply(g.climbing);
                else if (g.idle.set && stop_count > 0) apply(g.idle);
            }
        }
    }
    update_toast();
}

void GameCharacterBase::update_toast() {
    GameActor* actor = movement_graphics_source();
    if (!actor) return;
    if (game_switches().value(12)) {
        if (actor->is_dead()) {
            if (character_name() != "$Toasted") set_image("$Toasted", 0);
            set_step_anime(true);
        } else {
            set_step_anime(false);
        }
    }
}

void GameCharacterBase::set_custom_frame_xy(const js::Val& x, const js::Val& y) {
    has_custom_frame_x = !x.is_nullish();
    custom_frame_x = x.to_num();
    has_custom_frame_y = !y.is_nullish();
    custom_frame_y = y.to_num();
}

void GameCharacterBase::set_custom_frame_wh(const js::Val& w, const js::Val& h) {
    has_custom_frame_w = !w.is_nullish();
    custom_frame_w = w.to_num();
    has_custom_frame_h = !h.is_nullish();
    custom_frame_h = h.to_num();
}

static bool dir_in(int d, std::initializer_list<int> l) { return std::find(l.begin(), l.end(), d) != l.end(); }

bool GameCharacterBase::is_facing_towards(const GameCharacterBase& ev) const {
    int e = ev.direction();
    switch (direction()) {
    case 1: return dir_in(e, {8, 9, 6});
    case 2: return dir_in(e, {7, 8, 9});
    case 3: return dir_in(e, {4, 7, 8});
    case 4: return dir_in(e, {9, 6, 3});
    case 6: return dir_in(e, {7, 4, 1});
    case 7: return dir_in(e, {2, 3, 6});
    case 8: return dir_in(e, {1, 2, 3});
    case 9: return dir_in(e, {4, 1, 2});
    }
    return false;
}

bool GameCharacterBase::is_facing_away(const GameCharacterBase& ev) const {
    int e = ev.direction();
    switch (direction()) {
    case 1: return dir_in(e, {4, 1, 2});
    case 2: return dir_in(e, {1, 2, 3});
    case 3: return dir_in(e, {2, 3, 6});
    case 4: return dir_in(e, {7, 4, 1});
    case 6: return dir_in(e, {9, 6, 3});
    case 7: return dir_in(e, {4, 7, 8});
    case 8: return dir_in(e, {7, 8, 9});
    case 9: return dir_in(e, {8, 9, 6});
    }
    return false;
}

bool GameCharacterBase::is_facing_sideways(const GameCharacterBase& ev) const {
    int e = ev.direction();
    switch (direction()) {
    case 1: return dir_in(e, {4, 7, 8, 2, 3, 6});
    case 2: return dir_in(e, {7, 4, 1, 9, 6, 3});
    case 3: return dir_in(e, {4, 1, 2, 8, 9, 6});
    case 4: return dir_in(e, {7, 8, 9, 1, 2, 3});
    case 6: return dir_in(e, {7, 8, 9, 1, 2, 3});
    case 7: return dir_in(e, {4, 1, 2, 8, 9, 6});
    case 8: return dir_in(e, {7, 4, 1, 9, 6, 3});
    case 9: return dir_in(e, {4, 7, 8, 2, 3, 6});
    }
    return false;
}

bool GameCharacterBase::is_position_front_of(double ex, double ey) const {
    switch (direction()) {
    case 1:
    case 2:
    case 3: return ey > y_;
    case 4: return ex < x_;
    case 6: return ex > x_;
    case 7:
    case 8:
    case 9: return ey < y_;
    }
    return false;
}

bool GameCharacterBase::is_position_back_of(double ex, double ey) const {
    switch (direction()) {
    case 1:
    case 2:
    case 3: return ey < y_;
    case 4: return ex > x_;
    case 6: return ex < x_;
    case 7:
    case 8:
    case 9: return ey > y_;
    }
    return false;
}

bool GameCharacterBase::is_position_side_of(double ex, double ey) const {
    switch (direction()) {
    case 1: return (x_ < ex && y_ > ey) || (x_ > ex && y_ < ey);
    case 2: return x_ != ex;
    case 3: return (x_ > ex && y_ > ey) || (x_ < ex && y_ < ey);
    case 4: return y_ != ey;
    case 6: return y_ != ey;
    case 7: return (x_ > ex && y_ > ey) || (x_ < ex && y_ < ey);
    case 8: return x_ != ex;
    case 9: return (x_ < ex && y_ > ey) || (x_ > ex && y_ < ey);
    }
    return false;
}

void GameCharacterBase::update_step_animation_behavior() {
    GameCharacter* self = dynamic_cast<GameCharacter*>(this);
    const std::string& b = step_animation_behavior;
    if (b == "LEFT TO RIGHT") {
        pattern_ += 1;
        if (pattern_ > 2) pattern_ = 0;
    } else if (b == "RIGHT TO LEFT") {
        pattern_ -= 1;
        if (pattern_ < 0) pattern_ = 2;
    } else if (b == "SPIN CLOCKWISE" || b == "SPIN CW") {
        if (self) self->turn_right_90();
    } else if (b == "SPIN COUNTERCLOCKWISE" || b == "SPIN CCW" || b == "SPIN ANTICLOCKWISE" || b == "SPIN ACW") {
        if (self) self->turn_left_90();
    }
}

bool GameCharacterBase::is_within_hitboxes(double x, double y) const {
    double left = x_ - hitbox_left;
    double right = x_ + hitbox_right;
    double up = y_ - hitbox_up;
    double down = y_ + hitbox_down;
    return left <= x && x <= right && up <= y && y <= down;
}

bool GameCharacterBase::can_play_footsteps() {
    if (!game_system().footsteps_enabled) return false;
    if (foot_can_play >= 0) return foot_can_play != 0;
    foot_can_play = params().foot_event_enable ? 1 : 0;
    return foot_can_play != 0;
}

void GameCharacterBase::process_footstep_sound() {
    if (can_play_footsteps() && game_system().footsteps_enabled) {
        GamePlayer& player = game_player();
        double distance = game_map().distance(x_, y_, player.x_, player.y_);
        const PluginParams& p = params();
        double volume = std::isnan(p.foot_event_volume) ? 0 : p.foot_event_volume;
        volume += distance * p.foot_distance_volume;
        double pitch = std::isnan(p.foot_event_pitch) ? 0 : p.foot_event_pitch;
        pitch += distance * p.foot_distance_pitch;
        double pan = 0;
        pan -= game_map().delta_x(x_, player.x_);
        play_footstep_sound(volume, pitch, pan);
    }
}

void GameCharacterBase::play_footstep_sound(double volume, double pitch, double pan) {
    if (!(volume > 0)) return;
    if (!(pitch > 0)) return;
    GameMap& m = game_map();
    double x = x_;
    if (x_ == 6) x += 1;
    else if (x_ == 4) x -= 1;
    double y = y_;
    if (y_ == 2) y += 1;
    else if (y_ == 8) y -= 1;
    int region = m.region_id(x, y);
    int tag = m.terrain_tag(x, y);
    const FootprintSound* fd = nullptr;
    if (region > 0) {
        auto it = m.region_footstep_sounds.find(region);
        if (it != m.region_footstep_sounds.end()) fd = &it->second;
    }
    static FootprintSound tileset_sound;
    if (!fd && tag > 0) {
        const Value& ts = m.tileset();
        std::string note = ts["note"].string();
        for (auto& line : re::split_lines(note)) {
            re::Groups g;
            if (re::search("<TERRAIN[ ]TAG[ ](\\d+)[ ]FOOTSTEP SOUND:[ ](.*)>", "i", line, &g)) {
                int tag_id = std::min(std::max(g.num(1), 1), 7);
                if (tag_id != tag) continue;
                std::vector<std::string> parts;
                size_t s0 = 0;
                std::string body = g[2];
                while (true) {
                    size_t c = body.find(',', s0);
                    parts.push_back(body.substr(s0, c == std::string::npos ? std::string::npos : c - s0));
                    if (c == std::string::npos) break;
                    s0 = c + 1;
                }
                tileset_sound.name = trim(parts[0]);
                tileset_sound.volume = parts.size() > 1 && !parts[1].empty() ? js_parse_int(parts[1]) : js_parse_int(std::to_string((int)params().foot_default_volume));
                tileset_sound.pitch = parts.size() > 2 && !parts[2].empty() ? js_parse_int(parts[2]) : js_parse_int(std::to_string((int)params().foot_default_pitch));
                fd = &tileset_sound;
            }
        }
    }
    if (!fd) {
        auto it = m.region_footstep_sounds.find(0);
        if (it == m.region_footstep_sounds.end()) return;
        fd = &it->second;
    }
    AudioObj se;
    se.valid = true;
    se.name = fd->name;
    se.volume = fd->volume * volume;
    se.pitch = fd->pitch * pitch;
    se.pan = std::min(std::max(pan, -100.0), 100.0);
    audio_manager::play_se(se);
}

int GameCharacterBase::get_region_id(double x, double y, int d) const {
    GameMap& m = game_map();
    switch (d) {
    case 1: return m.region_id(x - 1, y + 1);
    case 2: return m.region_id(x, y + 1);
    case 3: return m.region_id(x + 1, y + 1);
    case 4: return m.region_id(x - 1, y);
    case 5: return m.region_id(x, y);
    case 6: return m.region_id(x + 1, y);
    case 7: return m.region_id(x - 1, y - 1);
    case 8: return m.region_id(x, y - 1);
    case 9: return m.region_id(x + 1, y - 1);
    default: return m.region_id(x, y);
    }
}

bool GameCharacterBase::is_event_region_forbid(double x, double y, int d) {
    if (is_player()) return false;
    if (is_through()) return false;
    int r = get_region_id(x, y, d);
    if (r == 0) return false;
    return contains_int(game_map().restrict_event_regions, r);
}

bool GameCharacterBase::is_player_region_forbid(double x, double y, int d) {
    if (is_event()) return false;
    if (is_through()) return false;
    int r = get_region_id(x, y, d);
    if (r == 0) return false;
    return contains_int(game_map().restrict_player_regions, r);
}

bool GameCharacterBase::is_event_region_allow(double x, double y, int d) {
    if (is_player()) return false;
    int r = get_region_id(x, y, d);
    if (r == 0) return false;
    return contains_int(game_map().allow_event_regions, r);
}

bool GameCharacterBase::is_player_region_allow(double x, double y, int d) {
    if (is_event()) return false;
    int r = get_region_id(x, y, d);
    if (r == 0) return false;
    return contains_int(game_map().allow_player_regions, r);
}

bool GameCharacterBase::on_slippery_floor() const { return game_map().is_slippery(x_, y_); }

bool GameCharacterBase::slippery_pose() const {
    if (!on_slippery_floor()) return false;
    if (step_anime) return false;
    return true;
}

void GameCharacterBase::smart_jump(int distance) {
    if (distance == 0) {
        jump(0, 0);
        return;
    }
    setup_smart_jump(get_smart_jump_distance(distance));
}

void GameCharacterBase::setup_smart_jump(int distance) {
    if (direction_ == 2) jump(0, distance);
    else if (direction_ == 4) jump(-distance, 0);
    else if (direction_ == 6) jump(distance, 0);
    else if (direction_ == 8) jump(0, -distance);
}

int GameCharacterBase::get_smart_jump_distance(int distance) {
    if (direction_ == 2) {
        for (int i = 0; i < distance; ++i) {
            if (is_smart_jump_illegal_region(x_, y_ + i + 1)) {
                distance = i;
                break;
            }
        }
    } else if (direction_ == 4) {
        for (int i = 0; i < distance; ++i) {
            if (is_smart_jump_illegal_region(x_ - i - 1, y_)) {
                distance = i;
                break;
            }
        }
    } else if (direction_ == 6) {
        for (int i = 0; i < distance; ++i) {
            if (is_smart_jump_illegal_region(x_ + i + 1, y_)) {
                distance = i;
                break;
            }
        }
    } else if (direction_ == 8) {
        for (int i = 0; i < distance; ++i) {
            if (is_smart_jump_illegal_region(x_, y_ - i - 1)) {
                distance = i;
                break;
            }
        }
    }
    return calc_smart_jump_distance(distance);
}

static std::vector<double> tileset_number_tags(const char* pattern_list, const char* pattern_range) {
    std::vector<double> out;
    std::string note = game_map().tileset()["note"].string();
    for (auto& line : re::split_lines(note)) {
        re::Groups g;
        if (re::search(pattern_list, "i", line, &g)) {
            for (auto& d : re::all("\\d+", "g", g[1])) out.push_back(js_number(d));
        } else if (pattern_range && re::search(pattern_range, "i", line, &g)) {
            int a = g.num(1), b = g.num(2);
            for (int i = a; i <= b; i++) out.push_back(i);
        }
    }
    return out;
}

bool GameCharacterBase::is_smart_jump_illegal_region(double x, double y) {
    GameMap& m = game_map();
    if (x < 0 || y < 0) return true;
    if (x > m.width() - 1 || y > m.height() - 1) return true;
    if (is_through()) return false;
    int r = m.region_id(x, y);
    if (contains(params().jump_equal_region, r)) {
        if (region_id() == r) return false;
    }
    if (r > 0 && contains(params().jump_illegal_region, r)) return true;
    if (m.tileset().is_obj()) {
        std::vector<double> tags = tileset_number_tags("<(?:ILLEGAL JUMP):[ ]*(\\d+(?:\\s*,\\s*\\d+)*)>", "<(?:ILLEGAL JUMP):[ ](\\d+)[ ](?:THROUGH|to)[ ](\\d+)>");
        if (contains(tags, m.terrain_tag(x, y))) return true;
    }
    for (GameEvent* ev : m.events_xy(x, y)) {
        if (!ev) continue;
        if (ev->is_through()) continue;
        if (ev->is_smart_jump_blocked()) return true;
    }
    return false;
}

int GameCharacterBase::calc_smart_jump_distance(int distance) {
    int max = distance;
    int value = 0;
    if (direction_ == 2) {
        for (int i = 0; i < max; ++i) {
            if (is_smart_jump_valid(x_, y_ + max - i)) {
                value = max - i;
                break;
            }
        }
    } else if (direction_ == 4) {
        for (int i = 0; i < max; ++i) {
            if (is_smart_jump_valid(x_ - max + i, y_)) {
                value = max - i;
                break;
            }
        }
    } else if (direction_ == 6) {
        for (int i = 0; i < max; ++i) {
            if (is_smart_jump_valid(x_ + max - i, y_)) {
                value = max - i;
                break;
            }
        }
    } else if (direction_ == 8) {
        for (int i = 0; i < max; ++i) {
            if (is_smart_jump_valid(x_, y_ - max + i)) {
                value = max - i;
                break;
            }
        }
    }
    return value;
}

bool GameCharacterBase::is_smart_jump_valid(double x, double y) {
    if (is_through()) return true;
    GameMap& m = game_map();
    int r = m.region_id(x, y);
    if (contains(params().jump_equal_region, r)) {
        if (region_id() != r) return false;
    }
    for (GameEvent* ev : m.events_xy_nt(x, y)) {
        if (!ev) continue;
        if (ev->is_through()) continue;
        if (ev->is_normal_priority()) return false;
        if (ev->is_smart_jump_blocked()) return false;
    }
    if (r > 0 && contains(params().jump_equal_region, r)) {
        if (region_id() == r) return true;
    }
    return m.is_passable(x, y, direction_);
}

void GameCharacterBase::set_target(const CharRef& t, double tx, double ty, bool has_xy) {
    target = t;
    GameCharacterBase* c = t.get();
    if (t.valid() && c) {
        target_x = c->x_;
        target_y = c->y_;
        has_target_xy = true;
    } else {
        target_x = tx;
        target_y = ty;
        has_target_xy = has_xy;
    }
}

double GameCharacterBase::sprite_height() {
    if (sprite_height_cache >= 0) return sprite_height_cache;
    if (tile_id() > 0) return game_map().tile_height();
    double h = image_logical_height("img/characters/", character_name());
    if (h <= 0) {
        sprite_height_cache = 0;
        return 0;
    }
    bool big = false;
    size_t i = 0;
    while (i < character_name().size() && (character_name()[i] == '!' || character_name()[i] == '$')) i++;
    big = character_name().substr(0, i).find('$') != std::string::npos;
    sprite_height_cache = h / (big ? 4 : 8);
    return sprite_height_cache;
}

void GameCharacterBase::setup_eventcon(const std::string& img, int index) {
    if (is_eventcon_playing()) remove_eventcon();
    eventcon_img = img;
    eventcon_index_defined = true;
    has_eventcon = true;
    eventcon_index = index;
}

int GameCharacterBase::position_auto_move_direction() const { return game_map().position_auto_move_direction(x_, y_); }

void GameCharacterBase::set_bobble(const std::vector<js::Val>& args) {
    has_bobble = true;
    bobble = Bobble();
    if (js::has_arg(args, 0)) bobble.base = args[0].to_num();
    if (js::has_arg(args, 1)) bobble.speed = args[1].to_num();
    if (js::has_arg(args, 2)) bobble.amplitude = args[2].to_num();
}

void GameCharacterBase::setup_input_icon(const std::string& input, const js::Val& key, const js::Val& gamepad) {
    has_input_icon = true;
    input_icon.input = input;
    input_icon.key = key.is_undef() ? js::Val::boolean(false) : key;
    input_icon.gamepad = gamepad.is_undef() ? js::Val::boolean(false) : gamepad;
}

void GameCharacterBase::update_spinning() {
    if (spin_count > 0) {
        spin_delay_count--;
        if (spin_delay_count <= 0) {
            spin_count--;
            switch (direction()) {
            case 2: direction_ = 6; break;
            case 4: direction_ = 2; break;
            case 6: direction_ = 8; break;
            case 8: direction_ = 4; break;
            }
            spin_delay_count = spin_delay;
        }
    }
}

void GameCharacterBase::start_custom_frame_animation(const std::vector<FrameAnimStep>& list, int repeats) {
    if (list.empty()) return;
    custom_frame_animation.reset(new CustomFrameAnimation());
    custom_frame_animation->index = 0;
    custom_frame_animation->count = list[0].duration;
    custom_frame_animation->repeat = repeats;
    custom_frame_animation->list = list;
    if (list[0].se.valid) audio_manager::play_se(list[0].se);
}

void GameCharacterBase::update_custom_frame_animation() {
    CustomFrameAnimation* d = custom_frame_animation.get();
    if (!d) return;
    const FrameAnimStep anim = d->list[(size_t)d->index];
    d->count--;
    set_image(anim.name, anim.index);
    direction_ = anim.direction;
    set_pattern(anim.frame);
    if (d->count <= 0) {
        d->index = (d->index + 1) % (int)d->list.size();
        const FrameAnimStep& next = d->list[(size_t)d->index];
        d->count = next.duration;
        if (d->index >= (int)d->list.size() - 1) {
            d->repeat--;
            if (d->repeat <= 0) {
                custom_frame_animation.reset();
                return;
            }
        }
        (void)next;
    }
}

void GameCharacterBase::footprint_movement() {
    if (!can_create_footprints()) return;
    if (game_map().region_id(footprint_x, footprint_y) != 28) return;
    auto f = std::make_shared<GameFootprint>();
    f->graphics_name = footprint_graphics_name();
    f->frame = pattern();
    f->direction = direction_;
    f->last_direction = last_direction;
    f->x = footprint_x;
    f->y = footprint_y;
    scene_add_footprint(f);
}

void GameCharacterBase::clear_horror_effects() { horror::clear(horror_filters); }
void GameCharacterBase::create_horror_effect(const std::string& type) { horror::create(horror_filters, type); }
void GameCharacterBase::remove_horror_effect(const std::string& type) { horror::remove(horror_filters, type); }
void GameCharacterBase::set_horror_effect_to_value(const std::string& type, const std::string& key, const js::Val& v) {
    horror::set_value(horror_filters, type, key, v);
}

GameCharacter::GameCharacter() { GameCharacter::init_members(); }

void GameCharacter::init_members() {
    set_detect_vars();
    GameCharacterBase::init_members();
    move_route_forcing = false;
    move_route.reset();
    move_route_index = 0;
    original_move_route.reset();
    original_move_route_index = 0;
    wait_count = 0;
}

void GameCharacter::memorize_move_route() {
    original_move_route = move_route;
    original_move_route_index = move_route_index;
}

void GameCharacter::restore_move_route() {
    move_route = original_move_route;
    move_route_index = original_move_route_index;
    original_move_route.reset();
}

void GameCharacter::set_move_route(const RoutePtr& r0) {
    RoutePtr r = route_copy(r0);
    if (!move_route_forcing) {
        move_route = r;
        move_route_index = 0;
        move_route_forcing = false;
    } else {
        queue_move_route(r);
    }
}

void GameCharacter::force_move_route(const RoutePtr& r) {
    if (!original_move_route) memorize_move_route();
    move_route = r;
    move_route_index = 0;
    move_route_forcing = true;
    wait_count = 0;
}

void GameCharacter::character_update_stop() {
    GameCharacterBase::update_stop();
    if (move_route_forcing) update_routine_move();
}

void GameCharacter::update_stop() { character_update_stop(); }

void GameCharacter::update_routine_move() {
    if (wait_count > 0) {
        wait_count--;
    } else {
        set_movement_success(true);
        if (!move_route) return;
        if (move_route_index >= 0 && move_route_index < (int)move_route->list.size()) {
            RouteCmd cmd = move_route->list[(size_t)move_route_index];
            RoutePtr keep = move_route;
            process_move_command(cmd);
            advance_move_route_index();
        }
    }
}

void GameCharacter::process_move_command(const RouteCmd& cmd) {
    if (cmd.code == ROUTE_SCRIPT) {
        process_move_route_script(cmd);
        return;
    }
    process_yep_move_command(cmd);
}

void GameCharacter::process_yep_move_command(const RouteCmd& cmd) {
    switch (cmd.code) {
    case ROUTE_END: process_route_end(); break;
    case ROUTE_MOVE_DOWN: move_straight(2); break;
    case ROUTE_MOVE_LEFT: move_straight(4); break;
    case ROUTE_MOVE_RIGHT: move_straight(6); break;
    case ROUTE_MOVE_UP: move_straight(8); break;
    case ROUTE_MOVE_LOWER_L: move_diagonally(4, 2); break;
    case ROUTE_MOVE_LOWER_R: move_diagonally(6, 2); break;
    case ROUTE_MOVE_UPPER_L: move_diagonally(4, 8); break;
    case ROUTE_MOVE_UPPER_R: move_diagonally(6, 8); break;
    case ROUTE_MOVE_RANDOM: move_random(); break;
    case ROUTE_MOVE_TOWARD: move_toward_player(); break;
    case ROUTE_MOVE_AWAY: move_away_from_player(); break;
    case ROUTE_MOVE_FORWARD: move_forward(); break;
    case ROUTE_MOVE_BACKWARD: move_backward(); break;
    case ROUTE_JUMP: jump(cmd.param_num(0), cmd.param_num(1)); break;
    case ROUTE_WAIT: wait_count = (int)cmd.param_num(0) - 1; break;
    case ROUTE_TURN_DOWN: set_direction(2); break;
    case ROUTE_TURN_LEFT: set_direction(4); break;
    case ROUTE_TURN_RIGHT: set_direction(6); break;
    case ROUTE_TURN_UP: set_direction(8); break;
    case ROUTE_TURN_90D_R: turn_right_90(); break;
    case ROUTE_TURN_90D_L: turn_left_90(); break;
    case ROUTE_TURN_180D: turn_180(); break;
    case ROUTE_TURN_90D_R_L: turn_right_or_left_90(); break;
    case ROUTE_TURN_RANDOM: turn_random(); break;
    case ROUTE_TURN_TOWARD: turn_toward_player(); break;
    case ROUTE_TURN_AWAY: turn_away_from_player(); break;
    case ROUTE_SWITCH_ON: game_switches().set_value((int)cmd.param_num(0), true); break;
    case ROUTE_SWITCH_OFF: game_switches().set_value((int)cmd.param_num(0), false); break;
    case ROUTE_CHANGE_SPEED: set_move_speed(cmd.param_num(0)); break;
    case ROUTE_CHANGE_FREQ: set_move_frequency(cmd.param_num(0)); break;
    case ROUTE_WALK_ANIME_ON: set_walk_anime(true); break;
    case ROUTE_WALK_ANIME_OFF: set_walk_anime(false); break;
    case ROUTE_STEP_ANIME_ON: set_step_anime(true); break;
    case ROUTE_STEP_ANIME_OFF: set_step_anime(false); break;
    case ROUTE_DIR_FIX_ON: set_direction_fix(true); break;
    case ROUTE_DIR_FIX_OFF: set_direction_fix(false); break;
    case ROUTE_THROUGH_ON: set_through(true); break;
    case ROUTE_THROUGH_OFF: set_through(false); break;
    case ROUTE_TRANSPARENT_ON: set_transparent(true); break;
    case ROUTE_TRANSPARENT_OFF: set_transparent(false); break;
    case ROUTE_CHANGE_IMAGE:
        set_image(cmd.param_str(0), (int)cmd.param_num(1));
        update_animation();
        break;
    case ROUTE_CHANGE_OPACITY: set_opacity(cmd.param_num(0)); break;
    case ROUTE_CHANGE_BLEND_MODE: set_blend_mode((int)cmd.param_num(0)); break;
    case ROUTE_PLAY_SE:
        if (cmd.params && cmd.params->is_arr() && cmd.params->size()) audio_manager::play_se(AudioObj::from((*cmd.params)[0]));
        break;
    }
}

double GameCharacter::delta_x_from(double x) const { return game_map().delta_x(x_, x); }
double GameCharacter::delta_y_from(double y) const { return game_map().delta_y(y_, y); }

void GameCharacter::move_random() {
    int d = 2 + random_int(4) * 2;
    if (can_pass(x_, y_, d)) move_straight(d);
}

void GameCharacter::move_toward_xy(double x, double y) {
    double sx = delta_x_from(x);
    double sy = delta_y_from(y);
    if (std::fabs(sx) > std::fabs(sy)) {
        move_straight(sx > 0 ? 4 : 6);
        if (!is_movement_succeeded() && sy != 0) move_straight(sy > 0 ? 8 : 2);
    } else if (sy != 0) {
        move_straight(sy > 0 ? 8 : 2);
        if (!is_movement_succeeded() && sx != 0) move_straight(sx > 0 ? 4 : 6);
    }
}

void GameCharacter::move_away_from_xy(double x, double y) {
    double sx = delta_x_from(x);
    double sy = delta_y_from(y);
    if (std::fabs(sx) > std::fabs(sy)) {
        move_straight(sx > 0 ? 6 : 4);
        if (!is_movement_succeeded() && sy != 0) move_straight(sy > 0 ? 2 : 8);
    } else if (sy != 0) {
        move_straight(sy > 0 ? 2 : 8);
        if (!is_movement_succeeded() && sx != 0) move_straight(sx > 0 ? 6 : 4);
    }
}

void GameCharacter::turn_toward_xy(double x, double y) {
    double sx = delta_x_from(x);
    double sy = delta_y_from(y);
    if (std::fabs(sx) > std::fabs(sy)) set_direction(sx > 0 ? 4 : 6);
    else if (sy != 0) set_direction(sy > 0 ? 8 : 2);
}

void GameCharacter::turn_away_from_xy(double x, double y) {
    double sx = delta_x_from(x);
    double sy = delta_y_from(y);
    if (std::fabs(sx) > std::fabs(sy)) set_direction(sx > 0 ? 6 : 4);
    else if (sy != 0) set_direction(sy > 0 ? 2 : 8);
}

void GameCharacter::turn_toward_player() { turn_toward_character(game_player()); }
void GameCharacter::turn_away_from_player() { turn_away_from_character(game_player()); }
void GameCharacter::move_toward_player() { move_toward_character(game_player()); }
void GameCharacter::move_away_from_player() { move_away_from_character(game_player()); }

void GameCharacter::move_backward() {
    bool last = is_direction_fixed();
    set_direction_fix(true);
    move_straight(reverse_dir(direction()));
    set_direction_fix(last);
}

void GameCharacter::process_route_end() {
    if (move_route && move_route->repeat) {
        move_route_index = -1;
    } else if (move_route_forcing) {
        move_route_forcing = false;
        restore_move_route();
    }
}

void GameCharacter::advance_move_route_index() {
    RoutePtr r = move_route;
    if (r && (is_movement_succeeded() || r->skippable)) {
        int num = (int)r->list.size() - 1;
        move_route_index++;
        if (r->repeat && move_route_index >= num) move_route_index = 0;
    }
}

void GameCharacter::turn_right_90() {
    switch (direction()) {
    case 2: set_direction(4); break;
    case 4: set_direction(8); break;
    case 6: set_direction(2); break;
    case 8: set_direction(6); break;
    }
}

void GameCharacter::turn_left_90() {
    switch (direction()) {
    case 2: set_direction(6); break;
    case 4: set_direction(2); break;
    case 6: set_direction(8); break;
    case 8: set_direction(4); break;
    }
}

void GameCharacter::turn_right_or_left_90() {
    switch (random_int(2)) {
    case 0: turn_right_90(); break;
    case 1: turn_left_90(); break;
    }
}

void GameCharacter::turn_random() { set_direction(2 + random_int(4) * 2); }

void GameCharacter::swap(GameCharacterBase& c) {
    double nx = c.x_, ny = c.y_;
    c.locate(x_, y_);
    locate(nx, ny);
}

int GameCharacter::search_limit() const { return (int)params().galv_searches; }

int GameCharacter::find_direction_to_grid(int goal_x, int goal_y) {
    struct Node {
        int parent;
        int x, y, g, f;
    };
    int limit = search_limit();
    GameMap& m = game_map();
    int map_width = m.width();
    int map_height = m.height();
    int sx = (int)x_, sy = (int)y_;
    if (sx == goal_x && sy == goal_y) return 0;
    int base = map_width + 1;
    size_t cells = (size_t)((map_height + 2) * map_width + 2);
    std::vector<int8_t> closed(cells, 0);
    std::vector<int> open_node(cells, -1);
    auto slot = [&](int pos) -> int {
        int k = pos + base;
        return k >= 0 && (size_t)k < cells ? k : -1;
    };
    auto dist = [&](int x1, int y1) { return (int)m.distance(x1, y1, goal_x, goal_y); };
    std::vector<Node> nodes;
    std::vector<int> node_list;
    nodes.reserve(256);
    node_list.reserve(64);
    nodes.push_back(Node{-1, sx, sy, 0, dist(sx, sy)});
    int start = 0;
    int best = start;
    node_list.push_back(start);
    int s0 = slot(sy * map_width + sx);
    if (s0 < 0) return find_direction_to_slow(goal_x, goal_y);
    open_node[(size_t)s0] = start;
    while (!node_list.empty()) {
        size_t best_index = 0;
        int best_f = nodes[(size_t)node_list[0]].f;
        for (size_t i = 1; i < node_list.size(); i++) {
            int f = nodes[(size_t)node_list[i]].f;
            if (f < best_f) {
                best_f = f;
                best_index = i;
            }
        }
        int current = node_list[best_index];
        int x1 = nodes[(size_t)current].x, y1 = nodes[(size_t)current].y;
        int g1 = nodes[(size_t)current].g;
        int k1 = slot(y1 * map_width + x1);
        node_list.erase(node_list.begin() + (long)best_index);
        if (k1 >= 0) {
            open_node[(size_t)k1] = -1;
            closed[(size_t)k1] = 1;
        }
        if (x1 == goal_x && y1 == goal_y) {
            best = current;
            break;
        }
        if (g1 >= limit) continue;
        for (int j = 0; j < 4; j++) {
            int direction = 2 + j * 2;
            int x2 = (int)m.round_x_with_direction(x1, direction);
            int y2 = (int)m.round_y_with_direction(y1, direction);
            int k2 = slot(y2 * map_width + x2);
            if (k2 >= 0 && closed[(size_t)k2]) continue;
            if (!can_pass(x1, y1, direction)) continue;
            if (k2 < 0) return find_direction_to_slow(goal_x, goal_y);
            int g2 = g1 + 1;
            int existing = open_node[(size_t)k2];
            if (existing < 0 || g2 < nodes[(size_t)existing].g) {
                int neighbor;
                if (existing >= 0) {
                    neighbor = existing;
                } else {
                    nodes.push_back(Node{-1, 0, 0, 0, 0});
                    neighbor = (int)nodes.size() - 1;
                    node_list.push_back(neighbor);
                    open_node[(size_t)k2] = neighbor;
                }
                Node& nd = nodes[(size_t)neighbor];
                nd.parent = current;
                nd.x = x2;
                nd.y = y2;
                nd.g = g2;
                nd.f = g2 + dist(x2, y2);
                const Node& b = nodes[(size_t)best];
                if (nd.f - nd.g < b.f - b.g) best = neighbor;
            }
        }
    }
    int node = best;
    while (nodes[(size_t)node].parent >= 0 && nodes[(size_t)node].parent != start) node = nodes[(size_t)node].parent;
    double dx1 = m.delta_x(nodes[(size_t)node].x, nodes[(size_t)start].x);
    double dy1 = m.delta_y(nodes[(size_t)node].y, nodes[(size_t)start].y);
    if (dy1 > 0) return 2;
    if (dx1 < 0) return 4;
    if (dx1 > 0) return 6;
    if (dy1 < 0) return 8;
    double dx2 = delta_x_from(goal_x);
    double dy2 = delta_y_from(goal_y);
    if (std::fabs(dx2) > std::fabs(dy2)) return dx2 > 0 ? 4 : 6;
    if (dy2 != 0) return dy2 > 0 ? 8 : 2;
    return 0;
}

int GameCharacter::find_direction_to(double goal_x, double goal_y) {
    GameMap& m = game_map();
    if (x_ == std::floor(x_) && y_ == std::floor(y_) && goal_x == std::floor(goal_x) && goal_y == std::floor(goal_y) && m.width() > 0 &&
        m.height() > 0 && std::fabs(goal_x) < 1e6 && std::fabs(goal_y) < 1e6 && std::fabs(x_) < 1e6 && std::fabs(y_) < 1e6)
        return find_direction_to_grid((int)goal_x, (int)goal_y);
    return find_direction_to_slow(goal_x, goal_y);
}

int GameCharacter::find_direction_to_slow(double goal_x, double goal_y) {
    struct Node {
        int parent;
        double x, y, g, f;
    };
    int limit = search_limit();
    GameMap& m = game_map();
    double map_width = m.width();
    std::vector<Node> nodes;
    std::vector<int> node_list;
    std::vector<double> open_list;
    std::vector<double> closed_list;
    if (x_ == goal_x && y_ == goal_y) return 0;
    nodes.push_back(Node{-1, x_, y_, 0, m.distance(x_, y_, goal_x, goal_y)});
    int start = 0;
    int best = start;
    node_list.push_back(start);
    open_list.push_back(y_ * map_width + x_);
    while (!node_list.empty()) {
        size_t best_index = 0;
        for (size_t i = 0; i < node_list.size(); i++) {
            if (nodes[(size_t)node_list[i]].f < nodes[(size_t)node_list[best_index]].f) best_index = i;
        }
        int current = node_list[best_index];
        double x1 = nodes[(size_t)current].x, y1 = nodes[(size_t)current].y;
        double pos1 = y1 * map_width + x1;
        double g1 = nodes[(size_t)current].g;
        node_list.erase(node_list.begin() + (long)best_index);
        auto oi = std::find(open_list.begin(), open_list.end(), pos1);
        if (oi != open_list.end()) open_list.erase(oi);
        else open_list.erase(open_list.end() - 1);
        closed_list.push_back(pos1);
        if (x1 == goal_x && y1 == goal_y) {
            best = current;
            break;
        }
        if (g1 >= limit) continue;
        for (int j = 0; j < 4; j++) {
            int direction = 2 + j * 2;
            double x2 = m.round_x_with_direction(x1, direction);
            double y2 = m.round_y_with_direction(y1, direction);
            double pos2 = y2 * map_width + x2;
            if (std::find(closed_list.begin(), closed_list.end(), pos2) != closed_list.end()) continue;
            if (!can_pass(x1, y1, direction)) continue;
            double g2 = g1 + 1;
            auto it2 = std::find(open_list.begin(), open_list.end(), pos2);
            long index2 = it2 == open_list.end() ? -1 : (long)(it2 - open_list.begin());
            if (index2 < 0 || g2 < nodes[(size_t)node_list[(size_t)index2]].g) {
                int neighbor;
                if (index2 >= 0) {
                    neighbor = node_list[(size_t)index2];
                } else {
                    nodes.push_back(Node{-1, 0, 0, 0, 0});
                    neighbor = (int)nodes.size() - 1;
                    node_list.push_back(neighbor);
                    open_list.push_back(pos2);
                }
                Node& n = nodes[(size_t)neighbor];
                n.parent = current;
                n.x = x2;
                n.y = y2;
                n.g = g2;
                n.f = g2 + m.distance(x2, y2, goal_x, goal_y);
                const Node& b = nodes[(size_t)best];
                if (n.f - n.g < b.f - b.g) best = neighbor;
            }
        }
    }
    int node = best;
    while (nodes[(size_t)node].parent >= 0 && nodes[(size_t)node].parent != start) node = nodes[(size_t)node].parent;
    double dx1 = m.delta_x(nodes[(size_t)node].x, nodes[(size_t)start].x);
    double dy1 = m.delta_y(nodes[(size_t)node].y, nodes[(size_t)start].y);
    if (dy1 > 0) return 2;
    if (dx1 < 0) return 4;
    if (dx1 > 0) return 6;
    if (dy1 < 0) return 8;
    double dx2 = delta_x_from(goal_x);
    double dy2 = delta_y_from(goal_y);
    if (std::fabs(dx2) > std::fabs(dy2)) return dx2 > 0 ? 4 : 6;
    if (dy2 != 0) return dy2 > 0 ? 8 : 2;
    return 0;
}

void GameCharacter::process_move_route_script(const RouteCmd& cmd) {
    if (!cmd.params || !cmd.params->is_arr() || cmd.params->size() < 2) return;
    const Value& c = (*cmd.params)[1];
    if (!c.is_arr() || c.size() < 1) return;
    if (c[0].as_int() != 0) {
        process_move_route_eval(c);
        return;
    }
    std::string kind = c[1].string();
    auto n = [&](size_t i) { return c[i].num(); };
    if (kind == "NOOP") return;
    if (kind == "HUG_LEFT" || kind == "HUG_RIGHT") {
        bool left = kind == "HUG_LEFT";
        bool collision = c[2].truthy();
        static const int L[10] = {0, 3, 6, 9, 2, 0, 8, 1, 4, 7};
        static const int R[10] = {0, 7, 4, 1, 8, 0, 2, 9, 6, 3};
        int d = direction();
        if (d < 0 || d > 9) return;
        int main_dir = left ? L[d] : R[d];
        int opp_dir = left ? R[d] : L[d];
        if (collision) game_temp().move_allow_player_collision = true;
        if (can_pass(x_, y_, main_dir)) {
            if (left) turn_left_90();
            else turn_right_90();
        } else if (!can_pass(x_, y_, direction())) {
            if (can_pass(x_, y_, opp_dir)) {
                if (left) turn_right_90();
                else turn_left_90();
            } else {
                turn_180();
            }
        }
        if (can_pass(x_, y_, direction())) {
            game_temp().move_allow_player_collision = false;
            move_forward();
        }
        game_temp().move_allow_player_collision = false;
        return;
    }
    if (kind == "INDEX_SET" || kind == "INDEX_ADJUST") {
        size_t i = 0;
        while (i < character_name_.size() && (character_name_[i] == '!' || character_name_[i] == '$')) i++;
        if (character_name_.substr(0, i).find('$') != std::string::npos) return;
        double v = kind == "INDEX_SET" ? n(2) : character_index_ + n(2);
        v = std::min(std::max(v, 0.0), 7.0);
        set_image(character_name_, (int)v);
        return;
    }
    if (kind == "MOVE_UNTIL_STOP") {
        int d = (int)n(2);
        if (d == -1) d = direction();
        bool collision = c[3].truthy();
        if (collision) game_temp().move_allow_player_collision = true;
        if (can_pass(x_, y_, d)) {
            game_temp().move_allow_player_collision = false;
            move_straight(d);
            move_route_index -= 1;
        }
        game_temp().move_allow_player_collision = false;
        return;
    }
    if (kind == "OPACITY_SET") {
        opacity_ = std::min(std::max(std::round(n(2)), 0.0), 255.0);
        return;
    }
    if (kind == "OPACITY_ADJUST") {
        opacity_ = std::min(std::max(opacity_ + std::round(n(2)), 0.0), 255.0);
        return;
    }
    if (kind == "ANIMATION") request_animation(n(2));
    else if (kind == "ICON_BALLOON") return;
    else if (kind == "BALLOON") request_balloon((int)n(2));
    else if (kind == "JUMP_FORWARD") jump_forward(n(2));
    else if (kind == "JUMP_TO_POINT") jump_to_point(n(2), n(3));
    else if (kind == "JUMP_TO_EVENT") jump_to_event((int)n(2));
    else if (kind == "JUMP_TO_PLAYER") jump_to_event(0);
    else if (kind == "MOVE_TO_POINT") move_to_point(n(2), n(3), c[4].truthy());
    else if (kind == "MOVE_TO_EVENT") move_to_event((int)n(2), c[3].truthy());
    else if (kind == "MOVE_TO_PLAYER") move_to_event(0, false);
    else if (kind == "PATTERN_LOCK") pattern_lock(n(2));
    else if (kind == "PATTERN_UNLOCK") pattern_unlock();
    else if (kind == "SELF_SWITCH_ON") process_move_route_self_switch(c[2].string(), "on");
    else if (kind == "SELF_SWITCH_OFF") process_move_route_self_switch(c[2].string(), "off");
    else if (kind == "SELF_SWITCH_TOGGLE") process_move_route_self_switch(c[2].string(), "toggle");
    else if (kind == "STEP_AWAY_POINT") step_away_from_point(n(2), n(3));
    else if (kind == "STEP_AWAY_EVENT") step_away_from_event((int)n(2));
    else if (kind == "STEP_AWAY_PLAYER") step_away_from_event(0);
    else if (kind == "STEP_TOWARD_POINT") step_toward_point(n(2), n(3));
    else if (kind == "STEP_TOWARD_EVENT") step_toward_event((int)n(2));
    else if (kind == "STEP_TOWARD_PLAYER") step_toward_event(0);
    else if (kind == "TELEPORT_POINT") teleport_to_point(n(2), n(3));
    else if (kind == "TELEPORT_EVENT") teleport_to_event((int)n(2));
    else if (kind == "TELEPORT_PLAYER") teleport_to_event(0);
    else if (kind == "TURN_AWAY_POINT") turn_away_from_point(n(2), n(3));
    else if (kind == "TURN_AWAY_EVENT") turn_away_from_event((int)n(2));
    else if (kind == "TURN_AWAY_PLAYER") turn_away_from_event(0);
    else if (kind == "TURN_TOWARD_POINT") turn_toward_point(n(2), n(3));
    else if (kind == "TURN_TOWARD_EVENT") turn_toward_event((int)n(2));
    else if (kind == "TURN_TOWARD_PLAYER") turn_toward_event(0);
    else if (kind.size() == 6 && kind.rfind("MOVE_", 0) == 0) move_repeat(kind[5] - '0', (int)n(2));
}

void GameCharacter::process_move_route_eval(const Value& ast) {
    js::ScopePtr scope = std::make_shared<js::Scope>();
    js::Val self = to_js();
    scope->declare(js::atom("a"), self);
    scope->declare(js::atom("b"), self);
    scope->declare(js::atom("player"), game_player().to_js());
    scope->declare(js::atom("s"), switches_proxy());
    scope->declare(js::atom("v"), variables_proxy());
    js::Val result;
    std::shared_ptr<const void> owner = move_route ? move_route->keep : nullptr;
    if (!js::vm().run(ast, self, scope, &result, owner)) script_error("MOVE ROUTE SCRIPT ERROR", js::vm().last_error);
}

void GameCharacter::process_move_route_self_switch(const std::string& key_name, const std::string& setting) {
    if (this == &game_player()) return;
    std::string key = GameSelfSwitches::key(game_map().map_id(), event_id(), key_name);
    if (setting == "on") game_self_switches().set_value(key, true);
    else if (setting == "off") game_self_switches().set_value(key, false);
    else if (setting == "toggle") game_self_switches().set_value(key, !game_self_switches().value(key));
}

void GameCharacter::process_move_route_self_variable(const std::string&, const Value&) {}

void GameCharacter::jump_forward(double distance) {
    distance = std::round(distance);
    double dx = 0, dy = 0;
    switch (direction()) {
    case 1:
        dx = -distance;
        dy = distance;
        break;
    case 2: dy = distance; break;
    case 3:
        dx = distance;
        dy = distance;
        break;
    case 4: dx = -distance; break;
    case 6: dx = distance; break;
    case 7:
        dx = -distance;
        dy = -distance;
        break;
    case 8: dy = -distance; break;
    case 9:
        dx = distance;
        dy = -distance;
        break;
    }
    jump(dx, dy);
}

void GameCharacter::jump_to_point(double x, double y) {
    x = std::round(x);
    y = std::round(y);
    jump((x_ - x) * -1, (y_ - y) * -1);
}

void GameCharacter::jump_to_event(int id) {
    if (id == 0) {
        jump_to_point(game_player().x_, game_player().y_);
    } else {
        GameEvent* ev = game_map().event(id);
        if (!ev) return;
        jump_to_point(ev->x_, ev->y_);
    }
}

void GameCharacter::move_repeat(int direction, int times) {
    int code = 1;
    switch (direction) {
    case 1: code = ROUTE_MOVE_LOWER_L; break;
    case 2: code = ROUTE_MOVE_DOWN; break;
    case 3: code = ROUTE_MOVE_LOWER_R; break;
    case 4: code = ROUTE_MOVE_LEFT; break;
    case 5: return;
    case 6: code = ROUTE_MOVE_RIGHT; break;
    case 7: code = ROUTE_MOVE_UPPER_L; break;
    case 8: code = ROUTE_MOVE_UP; break;
    case 9: code = ROUTE_MOVE_UPPER_R; break;
    }
    move_route = route_copy(move_route);
    if (!move_route) return;
    RouteCmd c;
    c.code = code;
    c.synthetic = true;
    while (times-- > 0) {
        size_t at = (size_t)std::max(0, std::min((int)move_route->list.size(), move_route_index + 1));
        move_route->list.insert(move_route->list.begin() + (long)at, c);
    }
}

void GameCharacter::move_to_event(int id, bool collision) {
    if (id == 0) {
        move_to_point(game_player().x_, game_player().y_, collision);
    } else {
        GameEvent* ev = game_map().event(id);
        if (!ev) return;
        move_to_point(ev->x_, ev->y_, collision);
    }
}

void GameCharacter::pattern_lock(double index) {
    pattern_move_route_locked = true;
    set_pattern((int)std::round(index));
}

void GameCharacter::step_away_from_point(double x, double y) { move_away_from_xy(std::round(x), std::round(y)); }

void GameCharacter::step_away_from_event(int id) {
    if (id == 0) {
        step_away_from_point(game_player().x_, game_player().y_);
    } else if (GameEvent* ev = game_map().event(id)) {
        step_away_from_point(ev->x_, ev->y_);
    }
}

void GameCharacter::step_toward_point(double x, double y) { move_toward_xy(std::round(x), std::round(y)); }

void GameCharacter::step_toward_event(int id) {
    if (id == 0) {
        step_toward_point(game_player().x_, game_player().y_);
    } else if (GameEvent* ev = game_map().event(id)) {
        step_toward_point(ev->x_, ev->y_);
    }
}

void GameCharacter::teleport_to_point(double x, double y) { locate(std::round(x), std::round(y)); }

void GameCharacter::teleport_to_event(int id) {
    if (id == 0) {
        teleport_to_point(game_player().x_, game_player().y_);
    } else if (GameEvent* ev = game_map().event(id)) {
        teleport_to_point(ev->x_, ev->y_);
    }
}

void GameCharacter::turn_away_from_point(double x, double y) { turn_away_from_xy(std::round(x), std::round(y)); }

void GameCharacter::turn_away_from_event(int id) {
    if (id == 0) {
        turn_away_from_point(game_player().x_, game_player().y_);
    } else if (GameEvent* ev = game_map().event(id)) {
        turn_away_from_point(ev->x_, ev->y_);
    }
}

void GameCharacter::turn_toward_point(double x, double y) { turn_toward_xy(std::round(x), std::round(y)); }

void GameCharacter::turn_toward_event(int id) {
    if (id == 0) {
        turn_toward_point(game_player().x_, game_player().y_);
    } else if (GameEvent* ev = game_map().event(id)) {
        turn_toward_point(ev->x_, ev->y_);
    }
}

void GameCharacter::move_to_point(double x, double y, bool collision) {
    x = std::round(x);
    y = std::round(y);
    if (collision) game_temp().move_allow_player_collision = true;
    int direction = find_direction_to(x, y);
    if (collision) game_temp().move_allow_player_collision = false;
    if (direction > 0) move_straight(direction);
    if (x_ != x || y_ != y) move_route_index -= 1;
    set_movement_success(true);
}

void GameCharacter::return_to_saved_xy() {
    if (x_ != orig_x || y_ != orig_y) {
        int direction = find_direction_to(orig_x, orig_y);
        if (direction > 0) move_straight(direction);
    } else {
        direction_ = orig_direction;
        reset_stop_count();
    }
}

void GameCharacter::fade_to(double fade_opac, double frames) {
    double time = params().default_fade_time;
    if (frames != 0 && !std::isnan(frames)) time = frames;
    std::vector<RouteCmd> steps;
    RouteCmd wait;
    wait.code = ROUTE_WAIT;
    wait.synthetic = true;
    wait.num = 1;
    double step = (fade_opac - opacity_) / time;
    if (step < 0) step = std::ceil(std::fabs(step)) * -1;
    else step = std::ceil(step);
    int guard = 0;
    for (double i = opacity_; i != fade_opac; i += step) {
        if (++guard > 100000 || step == 0 || std::isnan(step)) break;
        RouteCmd op;
        op.code = ROUTE_CHANGE_OPACITY;
        op.synthetic = true;
        op.num = i;
        steps.push_back(op);
        steps.push_back(wait);
        if (i != fade_opac) {
            if ((step > 0 && (i + step) > fade_opac) || (step < 0 && (i + step) < fade_opac)) {
                RouteCmd last;
                last.code = ROUTE_CHANGE_OPACITY;
                last.synthetic = true;
                last.num = fade_opac;
                steps.push_back(last);
                i = fade_opac - step;
            }
        }
    }
    if (!move_route) return;
    std::vector<RouteCmd> list;
    int idx = move_route_index;
    for (int k = 0; k < idx && k < (int)move_route->list.size(); k++) list.push_back(move_route->list[(size_t)k]);
    list.insert(list.end(), steps.begin(), steps.end());
    for (int k = idx + 1; k < (int)move_route->list.size(); k++) list.push_back(move_route->list[(size_t)k]);
    move_route->list = list;
    move_route_index--;
}

void GameCharacter::fade(double speed) {
    set_opacity(opacity() + speed);
    if (speed > 0 && opacity() < 255) move_route_index -= 1;
    else if (speed < 0 && opacity() > 0) move_route_index -= 1;
}

void GameCharacter::jump_forward_galv(double count) {
    double sx = 0, sy = 0;
    switch (direction()) {
    case 2: sy = count; break;
    case 4: sx = -count; break;
    case 6: sx = count; break;
    case 8: sy = -count; break;
    }
    jump(sx, sy);
}

void GameCharacter::jump_to_galv(const std::vector<js::Val>& args) {
    double sx = 0, sy = 0;
    if (args.size() > 1) {
        sx = x_ - args[0].to_num();
        sy = y_ - args[1].to_num();
    } else if (args.size() == 1) {
        if (args[0].is_num() && args[0].n == 0) {
            sx = x_ - game_player().x_;
            sy = y_ - game_player().y_;
        } else {
            GameEvent* ev = game_map().event(args[0].to_int());
            if (!ev) js::vm().throw_type_error("Cannot read property 'x' of undefined");
            sx = x_ - ev->x_;
            sy = y_ - ev->y_;
        }
    }
    jump(-sx, -sy);
}

void GameCharacter::sswitch(const std::string& letter, const js::Val& status, int eid, int mapid) {
    std::string key = GameSelfSwitches::key(mapid ? mapid : game_map().map_id(), eid ? eid : event_id(), letter);
    game_self_switches().set_value(key, status.truthy());
}

void GameCharacter::rwait(int low, int high) { wait_count = random_int(high - low) + low; }

void GameCharacter::step_rand(const std::vector<js::Val>& regions) {
    int d = 2 + random_int(4) * 2;
    GameMap& m = game_map();
    double x2 = m.round_x_with_direction(x_, d);
    double y2 = m.round_y_with_direction(y_, d);
    bool ok = false;
    int r = m.region_id(x2, y2);
    for (auto& v : regions) {
        if (v.is_num() && v.n == r) {
            ok = true;
            break;
        }
    }
    if (!ok) return;
    if (!m.is_valid(x2, y2)) return;
    if (can_pass(x_, y_, d)) move_straight(d);
}

void GameCharacter::set_frame(const std::string& name, int index, int pattern, int dir) {
    gstop = true;
    direction_ = dir;
    pattern_ = pattern - 1;
    if (!name.empty()) character_name_ = name;
    character_index_ = index - 1;
}

bool GameCharacter::dist_detect(double range, bool los, int id) {
    range = range * params().galv_tile;
    GameCharacterBase* target_c = id > 0 ? static_cast<GameCharacterBase*>(game_map().event(id)) : &game_player();
    if (!target_c) js::vm().throw_type_error("Cannot read property 'screenX' of undefined");
    double x1 = screen_x(), y1 = screen_y();
    double x2 = target_c->screen_x(), y2 = target_c->screen_y();
    double dist = std::sqrt(std::pow(x1 - x2, 2) + std::pow(y1 - y2, 2));
    if (dist <= range) {
        if ((los && galv_los(*target_c, *this)) || !los) {
            d_target_x = target_c->x_;
            d_target_y = target_c->y_;
            do_detected(id);
            return true;
        }
    }
    do_undetected(id);
    return false;
}

bool GameCharacter::is_detecting(int id) const { return id ? contains_int(detecting, id) : !detecting.empty(); }

void GameCharacter::do_detected(int id) {
    if (!contains_int(detecting, id)) {
        detecting.push_back(id);
        if (!(has_search_xy && search_x != 0)) balloon_id_ = detect_balloon;
    }
}

void GameCharacter::do_undetected(int id) {
    auto it = std::find(detecting.begin(), detecting.end(), id);
    if (it != detecting.end()) detecting.erase(it);
}

void GameCharacter::detector(int id) {
    if (game_system().expando.find(js::atom("_undetectable")) && game_system().expando.find(js::atom("_undetectable"))->truthy()) return;
    auto it = params().galv_behaviors.find(id);
    if (it == params().galv_behaviors.end()) js::vm().throw_type_error("Cannot read property '6' of undefined");
    detect_balloon = (int)it->second.balloon;
    bool detected = dist_detect(it->second.range, it->second.los != 0 && !std::isnan(it->second.los), 0);
    if (detected) {
        do_detect_move(id, 1);
    } else {
        if (has_search_xy && search_x != 0) move_toward_last_detect();
        else do_detect_move(id, 0);
    }
}

void GameCharacter::do_detect_move(int id, int type) {
    const GalvBehavior& b = params().galv_behaviors.at(id);
    if (type == 0) {
        move_speed_ = orig_move_speed;
        move_frequency_ = orig_move_frequency;
    } else {
        move_speed_ = b.speed;
        move_frequency_ = b.frequency;
    }
    const std::string& mode = type == 0 ? b.undetected : b.detected;
    if (mode == "approach") {
        move_toward_player();
    } else if (mode == "flee") {
        move_away_from_player();
    } else if (mode == "search") {
        if (detect_pause <= 0) {
            move_frequency_ = 5;
            has_search_xy = true;
            search_x = game_player().x_;
            search_y = game_player().y_;
            move_toward_last_detect();
            detect_pause = (int)(30 * (5 - orig_move_frequency) + 3);
        }
        detect_pause -= 1;
    } else if (mode == "rand") {
        if (detect_pause <= 0) {
            move_frequency_ = 5;
            move_random();
            detect_pause = (int)(30 * (5 - orig_move_frequency) + 1);
        }
        detect_pause -= 1;
    } else if (mode == "return") {
        move_frequency_ = 5;
        return_to_saved_xy();
        reset_stop_count();
    } else {
        reset_stop_count();
    }
}

void GameCharacter::set_detect_vars() {
    detecting.clear();
    has_search_xy = false;
    search_x = search_y = 0;
    detect_pause = 0;
    detect_pause_time = 40;
    orig_move_frequency = NAN;
    orig_move_speed = NAN;
    orig_x = orig_y = 0;
    has_orig_direction = false;
}

void GameCharacter::move_toward_last_detect() {
    static const char* actions[4] = {"turnRight90", "turnLeft90", "turnLeft90", "turnRight90"};
    if (!(has_search_xy && search_x != 0)) {
        has_search_xy = false;
        reset_stop_count();
    } else if (search_x == x_ && search_y == y_) {
        if (search_turn_index > -1) {
            if (search_turn_index < 4) {
                if (std::string(actions[search_turn_index]) == "turnRight90") turn_right_90();
                else turn_left_90();
            }
            search_x = x_;
            search_y = y_;
            search_turn_index -= 1;
            wait_count = 20;
        } else {
            has_search_xy = false;
            reset_stop_count();
        }
    } else {
        search_turn_index = 3;
        int direction = find_direction_to(search_x, search_y);
        if (direction > 0) move_straight(direction);
    }
}

void GameCharacter::get_tool_graphic(int shelf) {
    js::Val x, y;
    if (shelf == 0) {
        x = js::Val::num(0);
        y = js::Val::num(0);
    } else if (shelf == 10) {
        x = js::Val::num(1);
        y = js::Val::num(3);
    } else if (shelf == 3) {
        x = js::Val::num(0);
        y = js::Val::num(1);
    } else if (shelf == 4) {
        x = js::Val::num(1);
        y = js::Val::num(1);
    } else if (shelf == 6) {
        x = js::Val::num(0);
        y = js::Val::num(2);
    } else if (shelf == 7) {
        x = js::Val::num(1);
        y = js::Val::num(2);
    }
    set_custom_frame_xy(x, y);
}

void GameCharacter::get_random_npc_graphic() {
    opacity_ = 0;
    static const char* names[2] = {"FA_PizzaPeople_01", "FA_PizzaPeople_02"};
    character_name_ = names[random_int(2)];
    character_index_ = random_int(8);
    direction_ = 2;
    pattern_ = 1 % 3;
    if (GameEvent* ev = dynamic_cast<GameEvent*>(this)) ev->original_pattern = pattern_;
    priority_type = 2;
}

}
