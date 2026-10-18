#include <algorithm>
#include <cmath>

#include "../js/random.h"
#include "audio_manager.h"
#include "battler.h"
#include "character.h"
#include "config.h"
#include "data.h"
#include "game.h"
#include "battle.h"
#include "input.h"
#include "interpreter.h"
#include "params.h"
#include "party.h"

namespace rpg {

GameFollowers::GameFollowers() {
    visible = data::system()["optFollowers"].truthy();
    gathering = false;
    data.clear();
    int max_battle = game_party().max_battle_members();
    for (int i = 1; i < max_battle; i++) data.emplace_back(new GameFollower(i));
    int extra_start = (int)data.size();
    for (int i = 0; i < params().max_extra_followers; i++) data.emplace_back(new GameFollower(extra_start + i + 1));
}

GameFollower* GameFollowers::follower(int index) { return index >= 0 && index < (int)data.size() ? data[(size_t)index].get() : nullptr; }

void GameFollowers::refresh() {
    for (auto& f : data) f->refresh();
}

void GameFollowers::update() {
    if (!are_gathering()) {
        int last_index = 0;
        int n = (int)data.size();
        for (int i = 0; i < n; i++) {
            GameFollower* f = data[(size_t)i].get();
            if (!f->is_moving()) {
                bool moved = f->update_position_auto_move();
                if (moved) {
                    update_move();
                    last_index = i + 1;
                }
            }
            if (last_index > 0) {
                for (i = last_index; i < n; i++) data[(size_t)i]->chase_character(*data[(size_t)i - 1]);
            }
        }
    }
    if (are_gathering()) {
        if (!are_moving()) update_move();
        if (are_gathered()) gathering = false;
    }
    for (auto& f : data) f->update();
}

void GameFollowers::update_move() {
    if (fc::follower_stop) return;
    for (int i = (int)data.size() - 1; i >= 0; i--) {
        GameCharacterBase* preceding = i > 0 ? static_cast<GameCharacterBase*>(data[(size_t)i - 1].get()) : &game_player();
        data[(size_t)i]->chase_character(*preceding);
    }
}

void GameFollowers::jump_all() {
    if (fc::follower_stop) return;
    GamePlayer& p = game_player();
    if (p.is_jumping()) {
        for (auto& f : data) {
            double sx = game_map().delta_x(p.x_, f->x_);
            double sy = game_map().delta_y(p.y_, f->y_);
            f->jump(sx, sy);
        }
    }
}

void GameFollowers::synchronize(double x, double y, int d) {
    for (auto& f : data) {
        f->locate(x, y);
        f->set_direction(d);
    }
}

std::vector<GameFollower*> GameFollowers::visible_followers() {
    std::vector<GameFollower*> out;
    for (auto& f : data) {
        if (f->is_visible()) out.push_back(f.get());
    }
    return out;
}

bool GameFollowers::are_moving() {
    for (GameFollower* f : visible_followers()) {
        if (f->is_moving()) return true;
    }
    return false;
}

bool GameFollowers::are_gathered() {
    GamePlayer& p = game_player();
    for (GameFollower* f : visible_followers()) {
        if (f->is_moving() || !f->pos(p.x_, p.y_)) return false;
    }
    return true;
}

bool GameFollowers::is_someone_collided(double x, double y) {
    for (GameFollower* f : visible_followers()) {
        if (f->pos(x, y)) return true;
    }
    return false;
}

int GameFollowers::next_extra_follower_slot() {
    int index = game_party().max_battle_members() - 1;
    for (int i = index; i < (int)data.size(); i++) {
        index = i;
        if (!data[(size_t)i]->graphics_data) break;
    }
    return index;
}

int GameFollowers::last_extra_follower_slot() {
    int start = game_party().max_battle_members() - 1;
    int index = (int)data.size() - 1;
    for (int i = (int)data.size() - 1; i >= start; i--) {
        index = i;
        if (data[(size_t)i]->graphics_data) break;
    }
    return index;
}

void GameFollowers::clear_extra_followers() {
    for (auto& f : data) f->graphics_data.reset();
    refresh();
}

void GameFollowers::add_extra_follower(const js::Val& id, const std::string& name, int index, int slot) {
    if (slot < 0) slot = next_extra_follower_slot();
    GameFollower* f = follower(slot);
    if (!f) js::vm().throw_type_error("Cannot set property '_graphicsData' of undefined");
    f->graphics_data.reset(new ExtraGraphics{id, name, index});
    refresh();
}

void GameFollowers::remove_extra_follower(int index) {
    if (index < 0) index = last_extra_follower_slot();
    GameFollower* f = follower(index);
    if (f) f->graphics_data.reset();
    realign_extra_followers();
    refresh();
}

void GameFollowers::remove_follower_by_id(const js::Val& id) {
    for (auto& f : data) {
        if (f->graphics_data && js::strict_equals(f->graphics_data->id, id)) f->graphics_data.reset();
    }
    realign_extra_followers();
    refresh();
}

void GameFollowers::realign_extra_followers() {
    std::vector<ExtraGraphics> graphics;
    for (auto& f : data) {
        if (f->graphics_data) graphics.push_back(*f->graphics_data);
    }
    int index = game_party().max_battle_members() - 1;
    for (int i = std::max(index, 0); i < (int)data.size(); i++) {
        GameFollower* f = data[(size_t)i].get();
        int k = i - index;
        f->graphics_data.reset();
        if (k >= 0 && k < (int)graphics.size()) f->graphics_data.reset(new ExtraGraphics(graphics[(size_t)k]));
    }
}

GameFollower* GameFollowers::get_follower_by_id(const js::Val& id) {
    for (auto& f : data) {
        if (f->graphics_data && js::strict_equals(f->graphics_data->id, id)) return f.get();
    }
    return nullptr;
}

GameFollower::GameFollower(int idx) : member_index(idx) {
    set_transparent(data::system()["optTransparent"].truthy());
    set_through(true);
    fc_event_id = -1;
    fc_event_lock = false;
    fc_prelock_direction = 0;
    fc_retain_attrib = false;
    character_movement_graphics.reset();
    has_character_movement_graphics = false;
}

GameActor* GameFollower::actor() const { return game_party().battle_member(member_index); }

bool GameFollower::is_visible() const {
    if (game_player().followers().is_visible() && graphics_data) return true;
    return actor() && game_player().followers().is_visible();
}

void GameFollower::refresh() {
    if (graphics_data) {
        set_image(graphics_data->name, graphics_data->index);
    } else {
        GameActor* a = actor();
        std::string name = is_visible() && a ? a->character_name() : std::string();
        int index = is_visible() && a ? a->character_index() : 0;
        set_image(name, index);
    }
    if (GameActor* a = actor()) horror_filters = horror::copy(a->horror_filters);
}

void GameFollower::update() {
    if (fc_retain_attrib || fc::follower_control) {
        GameCharacterBase::update();
    } else {
        GameCharacterBase::update();
        GamePlayer& p = game_player();
        set_move_speed(p.real_move_speed());
        set_opacity(p.opacity());
        set_blend_mode(p.blend_mode());
        set_walk_anime(p.has_walk_anime());
        set_step_anime(p.has_step_anime());
        set_direction_fix(p.is_direction_fixed());
        set_transparent(p.is_transparent());
    }
    update_toast();
}

void GameFollower::chase_character(const GameCharacterBase& c) {
    double sx = delta_x_from(c.x_);
    double sy = delta_y_from(c.y_);
    if (sx != 0 && sy != 0) move_diagonally(sx > 0 ? 4 : 6, sy > 0 ? 8 : 2);
    else if (sx != 0) move_straight(sx > 0 ? 4 : 6);
    else if (sy != 0) move_straight(sy > 0 ? 8 : 2);
    set_move_speed(game_player().real_move_speed());
}

void GameFollower::update_stop() {
    if (fc_event_lock) reset_stop_count();
    character_update_stop();
    if (!is_move_route_forcing()) update_self_movement();
}

void GameFollower::lock() {
    if (!fc_event_lock) {
        fc_prelock_direction = direction();
        turn_toward_player();
        fc_event_lock = true;
    }
}

void GameFollower::unlock() {
    if (fc_event_lock) {
        fc_event_lock = false;
        set_direction(fc_prelock_direction);
    }
}

void GameFollower::update_self_movement() {
    if (move_type > 0) {
        if (!fc_event_lock && is_near_the_screen() && check_stop(stop_count_threshold())) {
            switch (move_type) {
            case 1: move_type_random(); break;
            case 2: move_type_toward_player(); break;
            case 3: update_routine_move(); break;
            }
        }
    }
}

void GameFollower::move_type_random() {
    switch (rng::below(6)) {
    case 0:
    case 1: move_random(); break;
    case 2:
    case 3:
    case 4: move_forward(); break;
    case 5: reset_stop_count(); break;
    }
}

void GameFollower::move_type_toward_player() {
    if (is_near_the_player()) {
        switch (rng::below(6)) {
        case 0:
        case 1:
        case 2:
        case 3: move_toward_player(); break;
        case 4: move_random(); break;
        case 5: move_forward(); break;
        }
    } else {
        move_random();
    }
}

bool GameFollower::is_near_the_player() const {
    double sx = std::fabs(delta_x_from(game_player().x_));
    double sy = std::fabs(delta_y_from(game_player().y_));
    return sx + sy < 20;
}

bool GameFollower::is_collided_with_characters(double x, double y) {
    if (fc::follower_collision) return base_is_collided_with_characters(x, y) || (is_normal_priority() && game_player().is_collided(x, y));
    return base_is_collided_with_characters(x, y);
}

bool GameFollower::can_update_movement_graphics() {
    GameActor* source = movement_graphics_source();
    if (source) return game_player().is_normal() && is_visible() && source->is_alive();
    return graphics_data && graphics_data->id.truthy() && has_character_movement_graphics;
}

GameActor* GameFollower::movement_graphics_source() const { return actor(); }

bool GameFollower::should_use_running_graphics() const { return game_player().is_dashing(); }

static void set_graphic_data(MovementGraphic& m, const js::Val& v) {
    if (v.is_str()) {
        m.name = v.to_str();
        m.index = 1;
        m.set = true;
        return;
    }
    if (v.is_obj()) {
        js::Vm& vm = js::vm();
        m.name = vm.get(v, js::atom("name")).to_str();
        m.index = vm.get(v, js::atom("index")).to_int();
        m.set = true;
        return;
    }
    m.set = false;
}

void GameFollower::set_movement_graphics(const js::Val& idle, const js::Val& walking, const js::Val& running) {
    if (!character_movement_graphics) character_movement_graphics.reset(new MovementGraphics());
    has_character_movement_graphics = true;
    set_graphic_data(character_movement_graphics->idle, idle);
    set_graphic_data(character_movement_graphics->walking, walking);
    set_graphic_data(character_movement_graphics->running, running);
}

void GameFollower::set_movement_graphic_data(const std::string& name, const js::Val& data) {
    if (!character_movement_graphics) character_movement_graphics.reset(new MovementGraphics());
    MovementGraphic* m = character_movement_graphics->get(to_lower(name));
    if (m) set_graphic_data(*m, data);
}

js::Val GameFollower::movement_graphics_js() const {
    if (!character_movement_graphics) return js::Val();
    js::Val o = js::make_object();
    js::Vm& vm = js::vm();
    const MovementGraphics& g = *character_movement_graphics;
    auto put = [&](const char* k, const MovementGraphic& m) {
        if (!m.set) return;
        js::Val d = js::make_object();
        vm.put(d, js::atom("name"), js::Val::str(m.name));
        vm.put(d, js::atom("index"), js::Val::num(m.index));
        vm.put(o, js::atom(k), d);
    };
    put("idle", g.idle);
    put("walking", g.walking);
    put("running", g.running);
    put("climbing", g.climbing);
    return o;
}

void GameFollower::set_movement_graphics_js(const js::Val& v) {
    character_movement_graphics.reset(new MovementGraphics());
    js::Vm& vm = js::vm();
    set_graphic_data(character_movement_graphics->idle, vm.get(v, js::atom("idle")));
    set_graphic_data(character_movement_graphics->walking, vm.get(v, js::atom("walking")));
    set_graphic_data(character_movement_graphics->running, vm.get(v, js::atom("running")));
    set_graphic_data(character_movement_graphics->climbing, vm.get(v, js::atom("climbing")));
}

void GameFollower::clear_movement_graphics() {
    character_movement_graphics.reset();
    has_character_movement_graphics = false;
}

void GameFollower::update_movement_graphics() {
    if (can_update_movement_graphics()) {
        GameActor* actor_src = movement_graphics_source();
        MovementGraphics* g = nullptr;
        if (actor_src) g = &actor_src->character_movement_graphics;
        else if (character_movement_graphics) g = character_movement_graphics.get();
        if (g) {
            const std::string name = character_name();
            int index = character_index();
            auto basil_climb = [&]() {
                if (name == "DW_BASIL" || name == "$DW_BASIL_RUN%(8)") {
                    if (!character_movement_graphics) character_movement_graphics.reset(new MovementGraphics());
                    character_movement_graphics->climbing = MovementGraphic{"DW_Climb", 0, true};
                }
            };
            auto apply = [&](MovementGraphic& m) {
                if (m.set && (name != m.name || index != m.index)) set_image(m.name, m.index);
            };
            if (is_moving()) {
                if (is_climbing()) {
                    basil_climb();
                    apply(g->climbing);
                } else if (should_use_running_graphics()) {
                    apply(g->running);
                } else {
                    apply(g->walking);
                }
            } else {
                if (is_climbing()) {
                    basil_climb();
                    apply(g->climbing);
                } else if (g->idle.set && stop_count > 0) {
                    apply(g->idle);
                }
            }
        }
    }
    update_toast();
}

bool GameFollower::can_play_footsteps() {
    if (!is_visible()) return false;
    return GameCharacterBase::can_play_footsteps();
}

bool GameFollower::update_position_auto_move() {
    int dir = position_auto_move_direction();
    if (dir > 0 && !is_moving()) {
        move_straight(dir);
        return true;
    }
    return false;
}

void GameFollower::clear_horror_effects() {
    if (GameActor* a = actor()) {
        horror::clear(a->horror_filters);
        horror_filters = horror::copy(a->horror_filters);
    }
}

void GameFollower::create_horror_effect(const std::string& type) {
    if (GameActor* a = actor()) {
        horror::create(a->horror_filters, type);
        horror_filters = horror::copy(a->horror_filters);
    }
}

void GameFollower::remove_horror_effect(const std::string& type) {
    if (GameActor* a = actor()) {
        horror::remove(a->horror_filters, type);
        horror_filters = horror::copy(a->horror_filters);
    }
}

void GameFollower::set_horror_effect_to_value(const std::string& type, const std::string& key, const js::Val& v) {
    if (GameActor* a = actor()) {
        horror::set_value(a->horror_filters, type, key, v);
        horror_filters = horror::copy(a->horror_filters);
    }
}

GamePlayer::GamePlayer() {
    GamePlayer::init_members();
    set_transparent(data::system()["optTransparent"].truthy());
}

void GamePlayer::init_members() {
    blocked_movement_inputs.clear();
    clear_ambience_data();
    GameCharacter::init_members();
    vehicle_type = "walk";
    vehicle_getting_on = false;
    vehicle_getting_off = false;
    dashing = false;
    needs_map_reload = false;
    transferring = false;
    new_map_id = 0;
    new_x = new_y = 0;
    new_direction = 0;
    fade_type = 0;
    followers_ = GameFollowers();
    encounter_count = 0;
    footstep_disabled = false;
}

void GamePlayer::clear_transfer_info() {
    transferring = false;
    new_map_id = 0;
    new_x = new_y = 0;
    new_direction = 0;
}

void GamePlayer::refresh() {
    game_party().refresh_members();
    GameActor* actor = game_party().leader();
    set_image(actor ? actor->character_name() : std::string(), actor ? actor->character_index() : 0);
    followers_.refresh();
    if (GameActor* l = game_party().leader()) horror_filters = horror::copy(l->horror_filters);
}

bool GamePlayer::is_stopping() const {
    if (vehicle_getting_on || vehicle_getting_off) return false;
    return GameCharacterBase::is_stopping();
}

void GamePlayer::reserve_transfer(int map_id, double x, double y, int d, int fade) {
    transferring = true;
    new_map_id = map_id;
    new_x = x;
    new_y = y;
    new_direction = d;
    fade_type = fade;
}

void GamePlayer::perform_transfer() {
    if (!is_transferring()) return;
    set_direction(new_direction);
    if (new_map_id != game_map().map_id() || needs_map_reload) {
        game_map().setup(new_map_id);
        needs_map_reload = false;
    }
    locate(new_x, new_y);
    refresh();
    clear_transfer_info();
}

bool GamePlayer::is_map_passable(double x, double y, int d) {
    if (GameVehicle* v = vehicle()) return v->is_map_passable(x, y, d);
    return GameCharacterBase::is_map_passable(x, y, d);
}

GameVehicle* GamePlayer::vehicle() const { return game_map().vehicle(vehicle_type); }

bool GamePlayer::is_dashing() const {
    if (on_slippery_floor()) return false;
    return dashing;
}

bool GamePlayer::is_collided(double x, double y) {
    if (is_through()) return false;
    return pos(x, y) || followers_.is_someone_collided(x, y);
}

double GamePlayer::center_x() const {
    GameMap& m = game_map();
    return ((640.0 / m.tile_width() - (1 * m.zoom_x)) / 2.0) / m.zoom_x;
}

double GamePlayer::center_y() const {
    GameMap& m = game_map();
    return ((480.0 / m.tile_height() - 1.75 * m.zoom_y) / 2.0) / m.zoom_y;
}

void GamePlayer::center(double x, double y) {
    GameMap& m = game_map();
    if (m.cam_target.kind == CharRef::Player || m.cam_norm) m.set_display_pos(x - center_x(), y - center_y());
}

void GamePlayer::locate(double x, double y) {
    base_locate(x, y);
    center(x, y);
    make_encounter_count();
    if (is_in_vehicle()) vehicle()->refresh();
    followers_.synchronize(x, y, direction());
}

void GamePlayer::increase_steps() {
    base_increase_steps();
    if (is_normal()) game_party().increase_steps();
    process_footstep_sound();
}

void GamePlayer::make_encounter_count() {
    int n = (int)game_map().encounter_step();
    encounter_count = rng::below(n) + rng::below(n) + 1;
}

int GamePlayer::make_encounter_troop_id() {
    std::vector<const Value*> list;
    double weight_sum = 0;
    const Value& el = game_map().encounter_list();
    for (size_t i = 0; i < el.size(); i++) {
        if (meets_encounter_conditions(el[i])) {
            list.push_back(&el[i]);
            weight_sum += el[i]["weight"].num();
        }
    }
    if (weight_sum > 0) {
        double value = rng::below((int)weight_sum);
        for (const Value* e : list) {
            value -= (*e)["weight"].num();
            if (value < 0) return (*e)["troopId"].as_int();
        }
    }
    return 0;
}

bool GamePlayer::meets_encounter_conditions(const Value& encounter) const {
    const Value& rs = encounter["regionSet"];
    if (rs.size() == 0) return true;
    int r = region_id();
    for (size_t i = 0; i < rs.size(); i++) {
        if (rs[i].as_int() == r) return true;
    }
    return false;
}

bool GamePlayer::execute_encounter() {
    if (!game_map().is_event_running() && encounter_count <= 0) {
        make_encounter_count();
        int troop = make_encounter_troop_id();
        if (data::troops()[troop].is_obj()) {
            battle::setup(troop, true, false);
            battle::on_encounter();
            return true;
        }
        return false;
    }
    return false;
}

void GamePlayer::start_map_event(double x, double y, const std::vector<int>& triggers, bool normal) {
    GameMap& m = game_map();
    if (!m.is_event_running()) {
        for (size_t index = 0; index < followers_.data.size(); index++) {
            GameFollower* f = followers_.data[index].get();
            if (f->fc_event_id > -1 && f->pos(x, y)) {
                GameEvent* ev = m.event(f->fc_event_id);
                if (!ev) js::vm().throw_type_error("Cannot read property 'isTriggerIn' of undefined");
                if (ev->is_trigger_in(triggers)) {
                    ev->fc_follower_id = (int)index;
                    ev->start();
                }
            }
        }
    }
    if (!m.is_event_running()) {
        for (GameEvent* ev : m.events_xy(x, y)) {
            if (ev->is_trigger_in(triggers) && ev->is_normal_priority() == normal) ev->start();
        }
    }
    if (!m.is_event_running() && !m.is_any_event_starting()) start_proximity_event(triggers, normal);
}

void GamePlayer::move_by_input() {
    if (map_pizza_note_visible()) return;
    if (!is_moving() && can_move()) {
        int direction = get_input_direction();
        if (direction > 0) {
            game_temp().clear_destination();
        } else if (game_temp().is_destination_valid()) {
            direction = find_direction_to(game_temp().destination_x, game_temp().destination_y);
        }
        if (direction > 0) execute_move(direction);
    }
}

bool GamePlayer::can_move() {
    if (map_character_tag_active()) return false;
    if (game_temp().stop_map_player) return false;
    if (game_message().is_busy()) return false;
    if (is_move_route_forcing() || are_followers_gathering()) return false;
    if (vehicle_getting_on || vehicle_getting_off) return false;
    if (is_in_vehicle() && !vehicle()->can_move()) return false;
    GameMap& m = game_map();
    if (m.is_event_running() && m.move_after_common_event()) return true;
    if (m.is_event_running() || game_message().is_busy()) return false;
    if (is_move_route_forcing() || are_followers_gathering()) return false;
    if (vehicle_getting_on || vehicle_getting_off) return false;
    if (is_in_vehicle() && !vehicle()->can_move()) return false;
    return true;
}

int GamePlayer::get_input_direction() {
    int d = input::dir4();
    if (std::find(blocked_movement_inputs.begin(), blocked_movement_inputs.end(), d) != blocked_movement_inputs.end()) return 0;
    return d;
}

void GamePlayer::update_scene(bool scene_active) {
    double last_x = scrolled_x();
    double last_y = scrolled_y();
    bool was_moving = is_moving();
    update_dashing();
    if (scene_active) move_by_input();
    GameCharacterBase::update();
    update_scroll(last_x, last_y);
    update_vehicle();
    if (!is_moving()) update_nonmoving(was_moving);
    followers_.update();
    update_slippery();
}

void GamePlayer::update_dashing() {
    if (is_moving()) return;
    if (can_move() && !is_in_vehicle() && !game_map().is_dash_disabled()) {
        dashing = is_dash_button_pressed() || game_temp().is_destination_valid();
    } else {
        dashing = false;
    }
}

bool GamePlayer::is_dash_button_pressed() const {
    bool shift = input::is_pressed(input::SHIFT);
    return config().always_dash ? !shift : shift;
}

void GamePlayer::update_scroll(double x1, double y1) {
    GameMap& m = game_map();
    if (!m.cam_norm) return;
    double x2 = scrolled_x();
    double y2 = scrolled_y();
    if (y2 > y1 && y2 > center_y()) m.scroll_down(y2 - y1);
    if (x2 < x1 && x2 < center_x()) m.scroll_left(x1 - x2);
    if (x2 > x1 && x2 > center_x()) m.scroll_right(x2 - x1);
    if (y2 < y1 && y2 < center_y()) m.scroll_up(y1 - y2);
}

void GamePlayer::update_vehicle() {
    if (is_in_vehicle() && !are_followers_gathering()) {
        if (vehicle_getting_on) update_vehicle_get_on();
        else if (vehicle_getting_off) update_vehicle_get_off();
        else vehicle()->sync_with_player();
    }
}

void GamePlayer::update_vehicle_get_on() {
    if (!are_followers_gathering() && !is_moving()) {
        set_direction(vehicle()->direction());
        set_move_speed(vehicle()->move_speed());
        vehicle_getting_on = false;
        set_transparent(true);
        if (is_in_airship()) set_through(true);
        vehicle()->get_on();
    }
}

void GamePlayer::update_vehicle_get_off() {
    if (!are_followers_gathering() && vehicle()->is_lowest()) {
        vehicle_getting_off = false;
        vehicle_type = "walk";
        set_transparent(false);
    }
}

void GamePlayer::update_nonmoving(bool was_moving) {
    GameMap& m = game_map();
    bool handled = false;
    if (!m.is_event_running()) {
        if (was_moving) {
            game_party().on_player_walk();
            check_event_trigger_here({1, 2});
            if (m.setup_starting_event()) handled = true;
        }
        if (!handled && trigger_action()) handled = true;
        if (!handled) {
            if (was_moving) update_encounter_count();
            else game_temp().clear_destination();
        }
    }
    if (!m.is_event_running()) {
        if (was_moving) {
            int dir = position_auto_move_direction();
            if (dir > 0) move_straight(dir);
        }
    }
}

bool GamePlayer::trigger_action() {
    if (can_move()) {
        if (trigger_button_action()) return true;
        if (game_temp().is_destination_valid()) {
            int direction = this->direction();
            GameMap& m = game_map();
            double x1 = x_, y1 = y_;
            double x2 = m.round_x_with_direction(x1, direction);
            double y2 = m.round_y_with_direction(y1, direction);
            double x3 = m.round_x_with_direction(x2, direction);
            double y3 = m.round_y_with_direction(y2, direction);
            double dx = game_temp().destination_x, dy = game_temp().destination_y;
            if (dx == x1 && dy == y1) {
                check_event_trigger_here({0});
                if (m.setup_starting_event()) return true;
            } else if (dx == x2 && dy == y2) {
                check_event_trigger_there({0, 1, 2});
                if (m.setup_starting_event()) return true;
            } else if (dx == x3 && dy == y3) {
                if (m.is_counter(x2, y2)) check_event_trigger_there({0, 1, 2});
                if (m.setup_starting_event()) return true;
            }
        }
    }
    return false;
}

bool GamePlayer::trigger_button_action() {
    if (input::is_triggered(input::OK)) {
        if (get_on_off_vehicle()) return true;
        check_event_trigger_here({0});
        if (game_map().setup_starting_event()) return true;
        check_event_trigger_there({0, 1, 2});
        if (game_map().setup_starting_event()) return true;
    }
    return false;
}

void GamePlayer::update_encounter_count() {
    if (can_encounter()) encounter_count -= encounter_progress_value();
}

bool GamePlayer::can_encounter() const {
    return !game_party().has_encounter_none() && game_system().encounter_enabled && !is_in_airship() && !is_move_route_forcing() && !is_debug_through();
}

double GamePlayer::encounter_progress_value() const {
    double value = game_map().is_bush(x_, y_) ? 2 : 1;
    if (game_party().has_encounter_half()) value *= 0.5;
    if (is_in_ship()) value *= 0.5;
    return value;
}

void GamePlayer::check_event_trigger_here(const std::vector<int>& triggers) {
    if (!can_start_local_events()) return;
    if (std::find(triggers.begin(), triggers.end(), 0) == triggers.end()) process_region_event();
    if (can_start_local_events()) start_map_event(x_, y_, triggers, false);
    game_map().check_map_level_changing(x_, y_);
}

void GamePlayer::check_event_trigger_there(const std::vector<int>& triggers) {
    if (!can_start_local_events()) return;
    GameMap& m = game_map();
    int direction = this->direction();
    double x2 = m.round_x_with_direction(x_, direction);
    double y2 = m.round_y_with_direction(y_, direction);
    start_map_event(x2, y2, triggers, true);
    if (!m.is_any_event_starting() && m.is_counter(x2, y2)) {
        double x3 = m.round_x_with_direction(x2, direction);
        double y3 = m.round_y_with_direction(y2, direction);
        start_map_event(x3, y3, triggers, true);
    }
}

void GamePlayer::check_event_trigger_touch(double x, double y) {
    if (can_start_local_events()) start_map_event(x, y, {1, 2}, true);
}

bool GamePlayer::can_start_local_events() const {
    if (map_character_tag_active()) return false;
    return !is_in_airship();
}

bool GamePlayer::get_on_off_vehicle() {
    if (is_in_vehicle()) return get_off_vehicle();
    return get_on_vehicle();
}

bool GamePlayer::get_on_vehicle() {
    GameMap& m = game_map();
    int direction = this->direction();
    double x1 = x_, y1 = y_;
    double x2 = m.round_x_with_direction(x1, direction);
    double y2 = m.round_y_with_direction(y1, direction);
    if (m.airship()->pos(x1, y1)) vehicle_type = "airship";
    else if (m.ship()->pos(x2, y2)) vehicle_type = "ship";
    else if (m.boat()->pos(x2, y2)) vehicle_type = "boat";
    if (is_in_vehicle()) {
        vehicle_getting_on = true;
        if (!is_in_airship()) force_move_forward();
        gather_followers();
    }
    bool success = vehicle_getting_on;
    if (success) {
        const PluginParams& p = params();
        if (vehicle_type == "airship" && p.uce_airship_enter > 0) game_temp().reserve_common_event(p.uce_airship_enter);
        else if (vehicle_type == "ship" && p.uce_ship_enter > 0) game_temp().reserve_common_event(p.uce_ship_enter);
        else if (vehicle_type == "boat" && p.uce_boat_enter > 0) game_temp().reserve_common_event(p.uce_boat_enter);
    }
    return success;
}

bool GamePlayer::get_off_vehicle() {
    if (vehicle()->is_land_ok(x_, y_, direction())) {
        if (is_in_airship()) set_direction(2);
        followers_.synchronize(x_, y_, direction());
        vehicle()->get_off();
        if (!is_in_airship()) {
            force_move_forward();
            set_transparent(false);
        }
        vehicle_getting_off = true;
        set_move_speed(4);
        set_through(false);
        make_encounter_count();
        gather_followers();
    }
    bool success = vehicle_getting_off;
    if (success) {
        const PluginParams& p = params();
        if (vehicle_type == "airship" && p.uce_airship_exit > 0) game_temp().reserve_common_event(p.uce_airship_exit);
        else if (vehicle_type == "ship" && p.uce_ship_exit > 0) game_temp().reserve_common_event(p.uce_ship_exit);
        else if (vehicle_type == "boat" && p.uce_boat_exit > 0) game_temp().reserve_common_event(p.uce_boat_exit);
    }
    return success;
}

void GamePlayer::force_move_forward() {
    set_through(true);
    move_forward();
    set_through(false);
}

bool GamePlayer::is_on_damage_floor() const { return game_map().is_damage_floor(x_, y_) && !is_in_airship(); }

void GamePlayer::move_straight(int d) {
    if (can_pass(x_, y_, d)) followers_.update_move();
    GameCharacterBase::move_straight(d);
}

void GamePlayer::move_diagonally(int horz, int vert) {
    if (can_pass_diagonally(x_, y_, horz, vert)) followers_.update_move();
    GameCharacterBase::move_diagonally(horz, vert);
}

void GamePlayer::jump(double x_plus, double y_plus) {
    GameCharacterBase::jump(x_plus, y_plus);
    followers_.jump_all();
}

bool GamePlayer::is_collided_with_characters(double x, double y) {
    return is_collided_with_followers(x, y) || base_is_collided_with_characters(x, y);
}

bool GamePlayer::is_collided_with_followers(double x, double y) {
    if (fc::follower_collision) return followers_.is_someone_collided(x, y);
    return false;
}

bool GamePlayer::can_update_movement_graphics() {
    GameActor* l = movement_graphics_source();
    if (!l) js::vm().throw_type_error("Cannot read property 'isAlive' of undefined");
    return is_normal() && l->is_alive();
}

GameActor* GamePlayer::movement_graphics_source() const { return game_party().leader(); }

void GamePlayer::start_proximity_event(const std::vector<int>& triggers, bool) {
    for (GameEvent* ev : game_map().events()) {
        if (!ev) continue;
        if (!ev->is_trigger_in(triggers)) continue;
        if (ev->activation_type.empty() || ev->activation_type == "none") continue;
        if (meet_player_proximity_conditions(*ev)) ev->start();
    }
}

bool GamePlayer::meet_player_proximity_conditions(const GameEvent& ev) const {
    if (ev.activation_type == "radius") {
        double radius = game_map().distance(x_, y_, ev.x_, ev.y_);
        return ev.activation_dist >= radius;
    }
    if (ev.activation_type == "square") return ev.activation_dist >= std::fabs(ev.delta_x_from(x_)) && ev.activation_dist >= std::fabs(ev.delta_y_from(y_));
    if (ev.activation_type == "row") return ev.activation_dist >= std::fabs(ev.delta_y_from(y_));
    if (ev.activation_type == "column") return ev.activation_dist >= std::fabs(ev.delta_x_from(x_));
    return false;
}

bool GamePlayer::can_play_footsteps() {
    if (!game_system().footsteps_enabled) return false;
    if (!is_normal()) return false;
    return params().foot_player_enable;
}

void GamePlayer::process_footstep_sound() {
    if (can_play_footsteps()) {
        double volume = std::isnan(params().foot_player_volume) ? 0 : params().foot_player_volume;
        double pitch = std::isnan(params().foot_player_pitch) ? 0 : params().foot_player_pitch;
        play_footstep_sound(volume, pitch, 0);
    }
}

void GamePlayer::process_region_event() {
    GameMap& m = game_map();
    if (!m.is_region_event(x_, y_)) return;
    if (input::is_triggered(input::OK)) return;
    int region = m.region_id(x_, y_);
    int ce = m.get_unique_region_common_event(region);
    if (ce <= 0) {
        auto it = params().region_events.find(region);
        ce = it == params().region_events.end() ? 0 : (int)it->second;
    }
    game_temp().reserve_common_event(ce);
}

void GamePlayer::update_slippery() {
    if (game_map().is_event_running()) return;
    if (on_slippery_floor() && !is_moving()) {
        game_temp().clear_destination();
        move_straight(direction_);
    }
}

void GamePlayer::block_movement_inputs(const std::vector<int>& dirs) {
    for (int d : dirs) {
        if (std::find(blocked_movement_inputs.begin(), blocked_movement_inputs.end(), d) == blocked_movement_inputs.end()) blocked_movement_inputs.push_back(d);
    }
}

void GamePlayer::unblock_movement_inputs(const std::vector<int>& dirs) {
    for (int d : dirs) {
        auto it = std::find(blocked_movement_inputs.begin(), blocked_movement_inputs.end(), d);
        if (it != blocked_movement_inputs.end()) blocked_movement_inputs.erase(it);
    }
}

double GamePlayer::ambience_screen_x() const { return ambience.use_custom_position ? ambience.x : screen_x(); }
double GamePlayer::ambience_screen_y() const { return ambience.use_custom_position ? ambience.y : screen_y() - 10; }

bool GamePlayer::can_use_character_tag() const { return map_can_use_character_tag(); }

void GamePlayer::clear_horror_effects() {
    if (GameActor* l = game_party().leader()) {
        horror::clear(l->horror_filters);
        horror_filters = horror::copy(l->horror_filters);
    }
}

void GamePlayer::create_horror_effect(const std::string& type) {
    if (GameActor* l = game_party().leader()) {
        horror::create(l->horror_filters, type);
        horror_filters = horror::copy(l->horror_filters);
    }
}

void GamePlayer::remove_horror_effect(const std::string& type) {
    if (GameActor* l = game_party().leader()) {
        horror::remove(l->horror_filters, type);
        horror_filters = horror::copy(l->horror_filters);
    }
}

void GamePlayer::set_horror_effect_to_value(const std::string& type, const std::string& key, const js::Val& v) {
    if (GameActor* l = game_party().leader()) {
        horror::set_value(l->horror_filters, type, key, v);
        horror_filters = horror::copy(l->horror_filters);
    }
}

bool GamePlayer::can_create_footprints() const {
    if (game_switches().value(881)) return false;
    if (region_id() != 28) return false;
    return true;
}

std::string GamePlayer::footprint_graphics_name() const {
    if (game_switches().value(84)) return "bloody_footprints";
    return "footprints_turn";
}

bool GamePlayer::can_play_footsound() const { return !is_on_ladder() && !game_map().interpreter->is_running(); }

void GamePlayer::play_foot_sound() {
    if (!can_play_footsound()) return;
    const AudioObj* s = game_map().get_footsound(game_player().x_, game_player().y_);
    if (!s) return;
    audio_manager::play_se(*s);
}

int GamePlayer::get_footsound_frequency() const {
    switch (max_pattern()) {
    case 4: return 2;
    case 8: return 3;
    }
    return 0;
}

void GamePlayer::update_pattern() {
    base_update_pattern();
    if (is_moving() && is_foot_sound_enabled()) {
        int f = get_footsound_frequency();
        if (f && pattern_ % f == 0) play_foot_sound();
    }
}

GameVehicle::GameVehicle(const std::string& t) {
    GameVehicle::init_members();
    type = t;
    reset_direction();
    init_move_speed();
    load_system_settings();
}

void GameVehicle::init_members() {
    GameCharacter::init_members();
    type.clear();
    map_id = 0;
    altitude = 0;
    driving = false;
    bgm = AudioObj();
}

void GameVehicle::init_move_speed() {
    if (is_boat()) set_move_speed(4);
    else if (is_ship()) set_move_speed(5);
    else if (is_airship()) set_move_speed(6);
}

const Value& GameVehicle::vehicle() const {
    const Value& sys = data::system();
    if (is_boat()) return sys["boat"];
    if (is_ship()) return sys["ship"];
    if (is_airship()) return sys["airship"];
    return Value::null_value();
}

void GameVehicle::load_system_settings() {
    const Value& v = vehicle();
    map_id = v["startMapId"].as_int();
    set_position(v["startX"].num(), v["startY"].num());
    set_image(v["characterName"].string(), v["characterIndex"].as_int());
}

void GameVehicle::refresh() {
    if (driving) {
        map_id = game_map().map_id();
        sync_with_player();
    } else if (map_id == game_map().map_id()) {
        locate(x_, y_);
    }
    if (is_airship()) set_priority_type(driving ? 2 : 0);
    else set_priority_type(1);
    set_walk_anime(driving);
    set_step_anime(driving);
    set_transparent(map_id != game_map().map_id());
}

void GameVehicle::set_location(int mid, double x, double y) {
    map_id = mid;
    set_position(x, y);
    refresh();
}

bool GameVehicle::pos(double x, double y) const {
    if (map_id == game_map().map_id()) return GameCharacterBase::pos(x, y);
    return false;
}

bool GameVehicle::is_map_passable(double x, double y, int d) {
    GameMap& m = game_map();
    double x2 = m.round_x_with_direction(x, d);
    double y2 = m.round_y_with_direction(y, d);
    if (is_boat()) return m.is_boat_passable(x2, y2);
    if (is_ship()) return m.is_ship_passable(x2, y2);
    if (is_airship()) return true;
    return false;
}

void GameVehicle::get_on() {
    driving = true;
    set_walk_anime(true);
    set_step_anime(true);
    game_system().save_walking_bgm();
    play_bgm();
}

void GameVehicle::get_off() {
    driving = false;
    set_walk_anime(false);
    set_step_anime(false);
    reset_direction();
    game_system().replay_walking_bgm();
}

void GameVehicle::play_bgm() { audio_manager::play_bgm(bgm.valid ? bgm : AudioObj::from(vehicle()["bgm"])); }

void GameVehicle::sync_with_player() {
    copy_position(game_player());
    refresh_bush_depth();
}

double GameVehicle::screen_y() const { return GameCharacterBase::screen_y() - altitude; }

void GameVehicle::update() {
    GameCharacterBase::update();
    if (is_airship()) update_airship();
}

void GameVehicle::update_airship() {
    if (driving && !is_highest()) altitude++;
    if (!driving && !is_lowest()) altitude--;
    set_step_anime(is_highest());
    set_priority_type(is_lowest() ? 0 : 2);
}

bool GameVehicle::is_takeoff_ok() { return game_player().are_followers_gathered(); }

bool GameVehicle::is_land_ok(double x, double y, int d) {
    GameMap& m = game_map();
    bool value = true;
    if (is_airship()) {
        if (!m.is_airship_land_ok(x, y)) value = false;
        else if (!m.events_xy(x, y).empty()) value = false;
    } else {
        double x2 = m.round_x_with_direction(x, d);
        double y2 = m.round_y_with_direction(y, d);
        if (!m.is_valid(x2, y2)) value = false;
        else if (!m.is_passable(x2, y2, reverse_dir(d))) value = false;
        else if (is_collided_with_characters(x2, y2)) value = false;
    }
    if (!value) return false;
    GamePlayer& p = game_player();
    if (is_airship()) {
        d = 5;
        p.through = false;
    }
    if (p.is_player_region_forbid(x, y, d)) {
        if (is_airship()) p.through = true;
        return false;
    }
    if (p.is_player_region_allow(x, y, d)) {
        if (is_airship()) p.through = true;
        return true;
    }
    return true;
}

std::vector<GameFollower*> GameFollowers::all() {
    std::vector<GameFollower*> out;
    for (auto& f : data) out.push_back(f.get());
    return out;
}

}
