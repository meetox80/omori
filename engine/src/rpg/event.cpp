#include <algorithm>
#include <cmath>
#include <map>

#include "../core/regex.h"
#include "../../../shared/jsops.h"
#include "../js/dataview.h"
#include "../js/random.h"
#include "../js/vm.h"
#include "audio_manager.h"
#include "battler.h"
#include "character.h"
#include "data.h"
#include "filter.h"
#include "game.h"
#include "interpreter.h"
#include "params.h"
#include "party.h"

namespace rpg {

namespace {

std::map<std::pair<int, int>, std::pair<double, double>>& template_positions() {
    static std::map<std::pair<int, int>, std::pair<double, double>> m;
    return m;
}

bool is_comment(const Value& cmd) {
    int code = cmd["code"].as_int();
    return code == 108 || code == 408;
}

std::string comment_text(const Value& cmd) { return cmd["parameters"][0].string(); }

}

void GameSystem::remove_temporary_map_spawned_events(int map_id) {
    auto& data = get_map_spawned_event_data(map_id);
    for (size_t i = 1; i < data.size(); i++) {
        if (!data[i]) continue;
        if (data[i]->spawn_preserved) continue;
        data[i].reset();
    }
}

GameEvent::GameEvent(int map_id_arg, int event_id_arg, const SpawnData* spawn) {
    spawned = spawn != nullptr;
    if (spawn) {
        spawn_data = *spawn;
        spawned = true;
        page_index = -2;
        map_id = spawn->base_map_id;
        event_id_ = spawn->spawn_id;
        spawn_id = spawn->spawn_id;
        spawn_map_id = spawn->map_id;
        spawn_event_id = spawn->event_id;
        spawn_preserved = spawn->preserved;
    }
    init_event_morpher(map_id_arg, event_id_arg);
    GameEvent::init_members();
    map_id = map_id_arg;
    event_id_ = event_id_arg;
    const Value& e = event();
    double ex = e["x"].num(), ey = e["y"].num();
    if (morphed) {
        auto it = template_positions().find({morph_map_id, morph_event_id});
        if (it != template_positions().end()) {
            ex = it->second.first;
            ey = it->second.second;
        }
    }
    locate(ex, ey);
    refresh();
    setup_copy_event();
    if (spawn) {
        map_id = spawn->base_map_id;
        event_id_ = spawn->spawn_id;
        locate(spawn->x, spawn->y);
        refresh();
        map_create_spawned_event(this);
    }
    setup_tkm_filters();
}

void GameEvent::init_members() {
    GameCharacter::init_members();
    move_type = 0;
    trigger = 0;
    trigger_null = false;
    starting = false;
    erased = false;
    page_index = -2;
    original_pattern = 1;
    original_direction = 2;
    prelock_direction = 0;
    locked = false;
    fc_follower_id = -1;
}

int GameEvent::event_id() const { return spawned ? spawn_id : event_id_; }

const Value& GameEvent::event() const {
    if (spawned) {
        const Value* v = preloaded_event(spawn_map_id, spawn_event_id, nullptr);
        return v ? *v : Value::null_value();
    }
    if (morphed) {
        const Value* v = preloaded_event(morph_map_id, morph_event_id, nullptr);
        return v ? *v : Value::null_value();
    }
    if (copied_event) {
        const Value* v = preloaded_event(copied_map_id, copied_event_id, nullptr);
        return v ? *v : Value::null_value();
    }
    return game_map().data()["events"][event_id_];
}

std::shared_ptr<const void> GameEvent::event_keep() const {
    std::shared_ptr<const void> keep;
    if (spawned) preloaded_event(spawn_map_id, spawn_event_id, &keep);
    else if (morphed) preloaded_event(morph_map_id, morph_event_id, &keep);
    else if (copied_event) preloaded_event(copied_map_id, copied_event_id, &keep);
    else keep = game_map().keep();
    return keep;
}

std::string GameEvent::note() const { return event()["note"].string(); }

const Value* GameEvent::page() const {
    const Value& pages = event()["pages"];
    if (page_index < 0 || page_index >= (int)pages.size()) return nullptr;
    return &pages[page_index];
}

const Value* GameEvent::list() const {
    const Value* p = page();
    if (!p) return nullptr;
    return &(*p)["list"];
}

bool GameEvent::is_collided_with_characters(double x, double y) {
    return base_is_collided_with_characters(x, y) || is_collided_with_player_characters(x, y);
}

bool GameEvent::is_collided_with_events(double x, double y) {
    bool any = false;
    for (GameEvent* ev : game_map().events_xy_nt(x, y)) {
        if (ev->is_normal_priority()) {
            any = true;
            break;
        }
    }
    if (!any) return false;
    return is_normal_priority();
}

bool GameEvent::is_collided_with_player_characters(double x, double y) {
    if (game_temp().move_allow_player_collision) return false;
    return is_normal_priority() && game_player().is_collided(x, y);
}

void GameEvent::lock() {
    if (!locked) {
        prelock_direction = direction();
        turn_toward_player();
        locked = true;
    }
    if (fc_follower_id > -1) {
        if (GameFollower* f = game_player().followers().follower(fc_follower_id)) f->lock();
    }
}

void GameEvent::unlock() {
    if (locked) {
        locked = false;
        set_direction(prelock_direction);
    }
    if (fc_follower_id > -1) {
        if (GameFollower* f = game_player().followers().follower(fc_follower_id)) f->unlock();
        fc_follower_id = -1;
    }
}

void GameEvent::update_stop() {
    if (locked) reset_stop_count();
    character_update_stop();
    if (!is_move_route_forcing()) update_self_movement();
}

bool GameEvent::prevent_self_movement() const {
    if (move_type == 0) return true;
    if (game_temp().stop_map_events) return true;
    if (params().stop_msg && game_message().is_busy()) return true;
    if (params().stop_event && game_map().is_event_run_quick()) return true;
    return false;
}

void GameEvent::update_self_movement() {
    if (!always_update_movement_checked) {
        always_update_movement_checked = true;
        always_update_movement = re::search("<ALWAYS UPDATE MOVEMENT>", "i", note());
    }
    if (prevent_self_movement()) return;
    update_chase_distance();
    update_flee_distance();
    update_chase_movement();
}

void GameEvent::base_update_self_movement() {
    if (!locked && is_near_the_screen() && check_stop(stop_count_threshold())) {
        switch (move_type) {
        case 1: move_type_random(); break;
        case 2: move_type_toward_player(); break;
        case 3: update_routine_move(); break;
        }
    }
}

void GameEvent::move_type_random() {
    switch (rng::below(6)) {
    case 0:
    case 1: move_random(); break;
    case 2:
    case 3:
    case 4: move_forward(); break;
    case 5: reset_stop_count(); break;
    }
}

void GameEvent::move_type_toward_player() {
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

bool GameEvent::is_near_the_player() const {
    double sx = std::fabs(delta_x_from(game_player().x_));
    double sy = std::fabs(delta_y_from(game_player().y_));
    return sx + sy < 20;
}

bool GameEvent::is_trigger_in(const std::vector<int>& triggers) const {
    if (trigger_null) return false;
    return std::find(triggers.begin(), triggers.end(), trigger) != triggers.end();
}

void GameEvent::start() {
    const Value* l = list();
    if (is_encounter_direction_locked()) {
        if (l && l->size() > 1) {
            starting = true;
            if (is_trigger_in({0, 1, 2})) encounter_lock();
        }
    } else {
        if (l && l->size() > 1) {
            starting = true;
            if (is_trigger_in({0, 1, 2})) lock();
        }
    }
}

void GameEvent::erase() {
    erased = true;
    refresh();
}

void GameEvent::refresh() {
    if (!eventcon_index_defined || eventcon_index == 0) {
        GameEventData& ed = game_event_data();
        std::string key = std::to_string(map_id) + "," + std::to_string(event_id_);
        if (ed.has_category("eventcon")) {
            js::Val info = ed.value("eventcon", key);
            if (info.is_array() && info.o->items.size() >= 2) setup_eventcon(info.o->items[0].to_str(), info.o->items[1].to_int());
        } else {
            re::Groups g;
            if (re::search("(?:INDICATOR):[ ](.+)[,](\\d+)", "i", note(), &g)) {
                std::string img = g[1];
                int index = g.num(2);
                setup_eventcon(img, index);
                if (!ed.has_category("eventcon")) ed.add_category("eventcon");
                if (map_id && event_id_) ed.set_value("eventcon", key, js::make_array({js::Val::str(img), js::Val::num(index)}));
            }
        }
    }
    int new_page = erased ? -1 : find_proper_page_index();
    if (page_index != new_page) {
        page_index = new_page;
        setup_page();
    }
}

int GameEvent::find_proper_page_index() {
    const Value& pages = event()["pages"];
    for (int i = (int)pages.size() - 1; i >= 0; i--) {
        if (meets_conditions(pages[i])) return i;
    }
    return -1;
}

static bool compare_op(int op, const js::Val& a, const js::Val& b) {
    js::Vm& vm = js::vm();
    switch (op) {
    case 1: return !vm.binop(JB_LE, a, b).truthy();
    case 2: return js::loose_equals(a, b);
    case 3: return vm.binop(JB_LT, a, b).truthy();
    case 4: return vm.binop(JB_LE, a, b).truthy();
    case 5: return !js::strict_equals(a, b);
    default: return !vm.binop(JB_LT, a, b).truthy();
    }
}

bool GameEvent::meets_conditions(const Value& page) {
    const Value& c = page["conditions"];
    if (c["switch1Valid"].truthy() && !game_switches().value(c["switch1Id"].as_int())) return false;
    if (c["switch2Valid"].truthy() && !game_switches().value(c["switch2Id"].as_int())) return false;
    if (c["variableValid"].truthy()) {
        js::Val v = game_variables().value(c["variableId"].as_int());
        js::Val want = js::value_to_js(c["variableValue"]);
        const Value* opv = c.find("variableOperator");
        int op = opv && opv->is_num() ? opv->as_int() : 0;
        if (!compare_op(op, v, want)) return false;
    }
    if (c["selfSwitchValid"].truthy()) {
        if (!game_self_switches().value(map_id, event_id_, c["selfSwitchCh"].string())) return false;
    }
    if (c["selfVariableValid"].truthy()) {
        std::string key = std::to_string(map_id) + "," + std::to_string(event_id_) + "," + js::value_to_js(c["selfVariableId"]).to_str();
        js::Val v = game_self_variables().value(key);
        js::Val want = js::value_to_js(c["selfVariableValue"]);
        int op = c["selfVariableOperator"].as_int();
        if (op >= 0 && op <= 5 && !compare_op(op == 0 ? 0 : op, v, want)) return false;
    }
    if (c["itemValid"].truthy()) {
        if (!game_party().has_item(ItemRef::item(c["itemId"].as_int()), false)) return false;
    }
    if (c["actorValid"].truthy()) {
        GameActor* a = game_actors().actor(c["actorId"].as_int());
        auto m = game_party().members();
        if (std::find(m.begin(), m.end(), a) == m.end()) return false;
    }
    if (c["scriptValid"].truthy()) return false;
    return true;
}

void GameEvent::setup_page() {
    initial_auto_trigger_bypass = true;
    if (page_index >= 0) setup_page_settings();
    else clear_page_settings();
    refresh_bush_depth();
    clear_starting_flag();
    check_event_trigger_auto();
    has_hitbox = true;
    hitbox_up = hitbox_left = hitbox_right = hitbox_down = 0;
    setup_hitbox_resize();
    initial_auto_trigger_bypass = false;
    setup_event_proximity_settings();
    clear_chase_settings();
}

void GameEvent::clear_page_settings() {
    set_image("", 0);
    move_type = 0;
    trigger = 0;
    trigger_null = true;
    retire_interpreter(std::move(interpreter));
    set_through(true);
}

void GameEvent::setup_page_settings() {
    const Value& pg = *page();
    const Value& image = pg["image"];
    if (image["tileId"].as_int() > 0) set_tile_image(image["tileId"].as_int());
    else set_image(image["characterName"].string(), image["characterIndex"].as_int());
    int dir = image["direction"].as_int();
    if (original_direction != dir) {
        original_direction = dir;
        prelock_direction = 0;
        set_direction_fix(false);
        set_direction(dir);
    }
    int pat = image["pattern"].as_int();
    if (original_pattern != pat) {
        original_pattern = pat;
        set_pattern(pat);
    }
    set_move_speed(pg["moveSpeed"].num());
    set_move_frequency(pg["moveFrequency"].num());
    set_priority_type(pg["priorityType"].as_int());
    set_walk_anime(pg["walkAnime"].truthy());
    set_step_anime(pg["stepAnime"].truthy());
    set_direction_fix(pg["directionFix"].truthy());
    set_through(pg["through"].truthy());
    set_move_route(route_from(pg["moveRoute"], event_keep()));
    move_type = pg["moveType"].as_int();
    trigger = pg["trigger"].as_int();
    trigger_null = false;
    retire_interpreter(std::move(interpreter));
    if (trigger == 4) interpreter.reset(new GameInterpreter());
    if (is_emf) original_pattern = -1;
    reset_pattern();
    setup_sprite_offsets();
    setup_step_animation_options();
    if (game_temp().load_location_direction) {
        set_direction(game_temp().load_location_direction);
        game_temp().load_location_direction = 0;
    }
    set_detect_stuff();
}

void GameEvent::base_check_event_trigger_touch(double x, double y) {
    GameMap& m = game_map();
    if (!m.is_event_running()) {
        if (!trigger_null && trigger == 2 && game_player().pos(x, y)) {
            if (!is_jumping() && is_normal_priority()) start();
        }
    }
    if (m.is_event_running()) return;
    if (trigger_null || trigger != 2) return;
    if (!is_follower_trigger_touch()) return;
    if (is_jumping()) return;
    if (!is_normal_priority()) return;
    for (GameFollower* f : game_player().followers().visible_followers()) {
        if (f && f->pos(x, y)) start();
    }
}

void GameEvent::check_event_trigger_touch(double x, double y) {
    base_check_event_trigger_touch(x, y);
    if (is_event_all_touch_trigger() && !is_starting()) {
        if (!game_map().is_event_running()) {
            if (game_player().pos(x, y)) {
                if (!is_jumping() && is_normal_priority()) start();
            }
        }
    }
}

void GameEvent::check_event_trigger_auto() {
    if (trigger_null || trigger != 3) return;
    if (initial_auto_trigger_bypass) return;
    if (!meet_event_proximity_conditions(false)) return;
    start();
}

void GameEvent::update() {
    GameCharacterBase::update();
    check_event_trigger_auto();
    update_parallel();
    update_alert();
    update_return_phase();
}

void GameEvent::update_parallel() {
    if (!interpreter) return;
    if (!meet_event_proximity_conditions(true)) return;
    if (!interpreter->is_running()) interpreter->setup(list(), event_id_, event_keep());
    interpreter->update();
}

void GameEvent::base_locate_event(double x, double y) {
    GameCharacterBase::locate(x, y);
    prelock_direction = 0;
}

void GameEvent::locate(double x, double y) {
    base_locate_event(x, y);
    if (!game_temp().bypass_load_location) load_location();
    save_location();
}

void GameEvent::force_move_route(const RoutePtr& r) {
    GameCharacter::force_move_route(r);
    prelock_direction = 0;
}

void GameEvent::setup_copy_event() {
    const Value& ev = event();
    std::string n = ev["note"].string();
    if (n.empty()) return;
    int mid = 0, eid = 0;
    re::Groups g;
    if (re::search("<(?:COPY EVENT):[ ]MAP[ ](\\d+),[ ]EVENT[ ](\\d+)>", "i", n, &g)) {
        mid = g.num(1);
        eid = g.num(2);
    } else if (re::search("<(?:COPY EVENT):[ ](\\d+),[ ](\\d+)>", "i", n, &g)) {
        mid = g.num(1);
        eid = g.num(2);
    } else if (re::search("<(?:COPY EVENT):[ ](.*)>", "i", n, &g)) {
        std::string name = to_upper(g[1]);
        auto it = params().copier_templates.find(name);
        if (it == params().copier_templates.end()) return;
        mid = it->second.map_id;
        eid = it->second.event_id;
    } else {
        return;
    }
    mid = std::min(std::max(mid, 1), 999);
    std::shared_ptr<const void> keep;
    if (params().preloaded_maps.count(mid)) {
        const Value* src = preloaded_event(mid, eid, &keep);
        if (!src || !src->is_obj()) return;
        copied_event = true;
        copied_map_id = mid;
        copied_event_id = eid;
        page_index = -2;
        find_proper_page_index();
        setup_page();
        refresh();
    } else {
        return;
    }
    if (copied_event) all_touch_trigger = note().find("<AllTouchTrigger>") != std::string::npos;
}

void GameEvent::encounter_lock() {
    prelock_direction = direction();
    locked = true;
}

bool GameEvent::is_encounter_direction_locked() const {
    for (auto& line : re::split_lines(note())) {
        if (re::search("<(?:ENCOUNTER LOCK|ENCOUNTER DIRECTION LOCK)>", "i", line)) return true;
    }
    return false;
}

bool GameEvent::is_follower_trigger_touch() const {
    for (auto& line : re::split_lines(note())) {
        if (re::search("<(?:FOLLOWER TRIGGER|FOLLOWER TOUCH)>", "i", line)) return true;
    }
    return false;
}

void GameEvent::init_event_morpher(int mid, int eid) {
    morphed = false;
    morph_map_id = mid;
    morph_event_id = eid;
    const MorphTarget* p = game_system().get_preserved_event_morph(mid, eid);
    if (p) {
        const Value& orig = game_map().data()["events"][eid];
        double px = orig["x"].num(), py = orig["y"].num();
        morphed = true;
        morph_map_id = p->map_id;
        morph_event_id = p->event_id;
        template_positions()[{p->map_id, p->event_id}] = {px, py};
    }
}

void GameEvent::morph_into(int mid, int eid) {
    morphed = true;
    morph_map_id = mid;
    morph_event_id = eid;
    page_index = -2;
    find_proper_page_index();
    setup_page();
    refresh();
}

void GameEvent::remove_morph() {
    game_system().remove_preserved_event_morph(game_map().map_id(), event_id());
    morphed = false;
    page_index = -2;
    find_proper_page_index();
    setup_page();
    refresh();
}

void GameEvent::setup_sprite_offsets() {
    init_sprite_offsets();
    static const char* n1 = "<SPRITE OFFSET X:[ ]([\\+\\-]\\d+)>";
    static const char* n2 = "<SPRITE OFFSET Y:[ ]([\\+\\-]\\d+)>";
    static const char* n3 = "<SPRITE OFFSET:[ ]([\\+\\-]\\d+),[ ]([\\+\\-]\\d+)>";
    auto apply = [&](const std::string& text) {
        re::Groups g;
        if (re::search(n1, "i", text, &g)) sprite_offset_x = js_parse_int(g[1]);
        if (re::search(n2, "i", text, &g)) sprite_offset_y = js_parse_int(g[1]);
        if (re::search(n3, "i", text, &g)) {
            sprite_offset_x = js_parse_int(g[1]);
            sprite_offset_y = js_parse_int(g[2]);
        }
    };
    std::string n = note();
    if (!n.empty()) apply(n);
    const Value* l = list();
    if (!l) return;
    for (size_t i = 0; i < l->size(); i++) {
        if (is_comment((*l)[i])) apply(comment_text((*l)[i]));
    }
}

void GameEvent::setup_step_animation_options() {
    step_behavior_set = true;
    step_animation_behavior = "LOOP";
    re::Groups g;
    std::string n = note();
    if (!n.empty() && re::search("<STEP ANIMATION:[ ](.*)>", "i", n, &g)) step_animation_behavior = trim(to_upper(g[1]));
    const Value* l = list();
    if (!l) return;
    for (size_t i = 0; i < l->size(); i++) {
        if (is_comment((*l)[i]) && re::search("<STEP ANIMATION:[ ](.*)>", "i", comment_text((*l)[i]), &g)) step_animation_behavior = trim(to_upper(g[1]));
    }
}

void GameEvent::setup_hitbox_resize() {
    std::string n = note();
    re::Groups g;
    if (!n.empty()) {
        if (re::search("<Hitbox (?:HEIGHT|UP):[ ](\\d+)>", "i", n, &g)) hitbox_up = g.num(1);
        if (re::search("<Hitbox Left:[ ](\\d+)>", "i", n, &g)) hitbox_left = g.num(1);
        if (re::search("<Hitbox Right:[ ](\\d+)>", "i", n, &g)) hitbox_right = g.num(1);
        if (re::search("<Hitbox Down:[ ](\\d+)>", "i", n, &g)) hitbox_down = g.num(1);
    }
    const Value* l = list();
    if (!l) return;
    for (size_t i = 0; i < l->size(); i++) {
        if (!is_comment((*l)[i])) continue;
        std::string t = comment_text((*l)[i]);
        if (re::search("<Hitbox (?:HEIGHT|UP):[ ](\\d+)>", "i", t, &g)) hitbox_up = g.num(1);
        else if (re::search("<Hitbox Left:[ ](\\d+)>", "i", t, &g)) hitbox_left = g.num(1);
        else if (re::search("<Hitbox Right:[ ](\\d+)>", "i", t, &g)) hitbox_right = g.num(1);
        else if (re::search("<Hitbox Down:[ ](\\d+)>", "i", t, &g)) hitbox_down = g.num(1);
    }
}

void GameEvent::setup_event_proximity_settings() {
    activation_dist = 0;
    activation_type = "none";
    const Value* l = list();
    if (!l) return;
    re::Groups g;
    for (size_t i = 0; i < l->size(); i++) {
        if (!is_comment((*l)[i])) continue;
        std::string t = comment_text((*l)[i]);
        if (re::search("<ACTIVATION SQUARE: (\\d+)>", "i", t, &g)) {
            activation_dist = g.num(1);
            activation_type = "square";
        } else if (re::search("<ACTIVATION (?:RADIUS|PROXIMITY): (\\d+)>", "i", t, &g)) {
            activation_dist = g.num(1);
            activation_type = "radius";
        } else if (re::search("<ACTIVATION (?:ROW|X|HORIZONTAL): (\\d+)>", "i", t, &g)) {
            activation_dist = g.num(1);
            activation_type = "row";
        } else if (re::search("<ACTIVATION (?:COLUMN|Y|VERTICAL): (\\d+)>", "i", t, &g)) {
            activation_dist = g.num(1);
            activation_type = "column";
        }
    }
}

void GameEvent::event_proximity_increase_steps() {
    if (!game_map().is_event_running() && !game_map().is_any_event_starting()) game_player().start_proximity_event({2}, is_normal_priority());
}

bool GameEvent::meet_event_proximity_conditions(bool parallel) {
    if (!parallel && game_map().is_event_running()) return false;
    if (!parallel && game_map().is_any_event_starting()) return false;
    if (activation_type.empty() || activation_type == "none") return true;
    return game_player().meet_player_proximity_conditions(*this);
}

bool GameEvent::can_play_footsteps() {
    if (!game_system().footsteps_enabled) return false;
    if (foot_can_play >= 0) return foot_can_play != 0;
    bool v = params().foot_event_enable;
    if (re::search("<NO FOOTSTEPS>", "i", note())) v = false;
    foot_can_play = v ? 1 : 0;
    return v;
}

void GameEvent::clear_chase_settings() {
    const PluginParams& p = params();
    alert_balloon = (int)p.ecp_alert_balloon;
    alert_common_event = (int)p.ecp_alert_event;
    alert_lock = 0;
    alert_player = false;
    alert_sound = p.ecp_alert_sound;
    alert_sound_vol = 100;
    alert_sound_pitch = 100;
    alert_sound_pan = 0;
    alert_timer = 0;
    chase_player = false;
    chase_range = 0;
    chase_speed = move_speed_;
    default_speed = move_speed_;
    flee_player = false;
    flee_range = 0;
    flee_speed = move_speed_;
    see_player = p.ecp_see_player;
    sight_lock = p.ecp_sight_lock;
    return_after = p.ecp_return;
    return_wait = p.ecp_return_wait;
    return_phase = false;
    return_frames = 0;
    stagger_count = 0;
    start_location_x = x_;
    start_location_y = y_;
    start_location_dir = direction_;
}

bool GameEvent::can_see_player() {
    if (!see_player) return false;
    double sx = delta_x_from(game_player().x_);
    double sy = delta_y_from(game_player().y_);
    int d;
    if (std::fabs(sx) > std::fabs(sy)) d = sx > 0 ? 4 : 6;
    else d = sy > 0 ? 8 : 2;
    if (d == direction()) {
        alert_lock = sight_lock;
        return true;
    }
    return false;
}

void GameEvent::update_chase_distance() {
    if (erased) return;
    if (chase_range <= 0) return;
    double dis = std::fabs(delta_x_from(game_player().x_)) + std::fabs(delta_y_from(game_player().y_));
    if (chase_conditions(dis)) start_event_chase();
    else if (chase_player) end_event_chase();
}

bool GameEvent::chase_conditions(double dis) {
    if (dis <= chase_range && non_see_player()) {
        alert_lock = sight_lock;
        return true;
    }
    if (alert_lock > 0) return true;
    if (dis <= chase_range && can_see_player()) return true;
    return false;
}

bool GameEvent::non_see_player() { return !see_player; }

void GameEvent::start_event_chase() {
    chase_player = true;
    set_move_speed(chase_speed);
}

void GameEvent::end_event_chase() {
    chase_player = false;
    set_move_speed(default_speed);
    if (alert_timer <= 0) alert_player = false;
    start_return_phase();
}

void GameEvent::update_flee_distance() {
    if (erased) return;
    if (flee_range <= 0) return;
    double dis = std::fabs(delta_x_from(game_player().x_)) + std::fabs(delta_y_from(game_player().y_));
    if (flee_conditions(dis)) start_event_flee();
    else if (flee_player) end_event_flee();
}

bool GameEvent::flee_conditions(double dis) {
    if (alert_lock > 0) return true;
    if (dis <= flee_range && can_see_player()) return true;
    if (dis <= flee_range && !see_player) {
        alert_lock = sight_lock;
        return true;
    }
    return false;
}

void GameEvent::start_event_flee() {
    flee_player = true;
    set_move_speed(flee_speed);
}

void GameEvent::end_event_flee() {
    flee_player = false;
    set_move_speed(default_speed);
    if (alert_timer <= 0) alert_player = false;
    start_return_phase();
}

void GameEvent::update_chase_movement() {
    if (map_character_tag_active()) return;
    if (stagger_count > 0) {
        stagger_count--;
        return;
    }
    if (stop_count > 0 && chase_player) {
        int direction = find_direction_to(game_player().x_, game_player().y_);
        if (direction > 0) {
            double x = x_, y = y_;
            move_straight(direction);
            if (x == x_ && y == y_) stagger_count = 20;
        }
    } else if (stop_count > 0 && flee_player) {
        update_flee_movement();
    } else if (return_phase) {
        update_move_return_after();
    } else {
        base_update_self_movement();
    }
}

void GameEvent::update_flee_movement() {
    switch (rng::below(6)) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 4: move_away_from_player(); break;
    case 5: move_random(); break;
    }
}

void GameEvent::update_alert() {
    if (erased) return;
    alert_lock--;
    if (alert_conditions()) activate_alert();
    if (alert_player) alert_timer--;
}

void GameEvent::activate_alert() {
    if (alert_balloon >= 0) request_balloon(alert_balloon);
    alert_player = true;
    alert_timer = params().ecp_alert_timer;
    if (!alert_sound.empty()) {
        AudioObj se;
        se.valid = true;
        se.name = alert_sound;
        se.volume = alert_sound_vol;
        se.pitch = alert_sound_pitch;
        se.pan = alert_sound_pan;
        audio_manager::play_se(se);
    }
    if (alert_common_event > 0) game_temp().reserve_common_event(alert_common_event);
}

void GameEvent::start_return_phase() {
    if (!return_after) return;
    return_phase = true;
    return_frames = return_wait;
}

void GameEvent::update_move_return_after() {
    if (return_frames > 0) return;
    double sx = delta_x_from(start_location_x);
    double sy = delta_y_from(start_location_y);
    if (std::fabs(sx) > std::fabs(sy)) {
        if (rng::below(6) <= 4) {
            move_straight(sx > 0 ? 4 : 6);
            if (!is_movement_succeeded() && sy != 0) move_straight(sy > 0 ? 8 : 2);
        } else {
            move_random();
        }
    } else if (sy != 0) {
        if (rng::below(6) <= 4) {
            move_straight(sy > 0 ? 8 : 2);
            if (!is_movement_succeeded() && sx != 0) move_straight(sx > 0 ? 4 : 6);
        } else {
            move_random();
        }
    }
    if (sx == 0 && sy == 0) {
        return_phase = false;
        return_frames = 0;
        direction_ = start_location_dir;
    }
}

void GameEvent::update_move() {
    GameCharacterBase::update_move();
    save_location();
}

bool GameEvent::is_save_location() const {
    if (game_map().is_save_event_locations()) return true;
    const Value* ev = &event();
    int map_id = game_map().map_id();
    if (save_location_note_ != ev || save_location_map_ != map_id) {
        save_location_note_ = ev;
        save_location_map_ = map_id;
        save_location_tag_ = false;
        for (auto& line : re::split_lines(note())) {
            if (re::search("<(?:SAVE EVENT LOCATION|save event locations)>", "i", line)) {
                save_location_tag_ = true;
                break;
            }
        }
    }
    return save_location_tag_;
}

void GameEvent::save_location() {
    if (!is_save_location()) return;
    game_system().save_event_location(game_map().map_id(), event_id(), (int)x_, (int)y_, direction_);
}

bool GameEvent::is_load_location() const {
    if (!is_save_location()) return false;
    return game_system().is_saved_event_location(game_map().map_id(), event_id());
}

void GameEvent::load_location() {
    if (!is_load_location()) return;
    std::string key = std::to_string(game_map().map_id()) + "," + std::to_string(event_id());
    const SavedLocation& loc = game_system().saved_event_locations[key];
    set_position(loc.x, loc.y);
    game_temp().load_location_direction = loc.d;
}

void GameEvent::reset_location() {
    const Value& e = event();
    base_locate_event(e["x"].num(), e["y"].num());
    set_direction(original_direction);
    save_location();
}

bool GameEvent::is_smart_jump_blocked() {
    for (auto& line : re::split_lines(note())) {
        if (re::search("<(?:ILLEGAL JUMP)>", "i", line)) return true;
    }
    return false;
}

void GameEvent::set_detect_stuff() {
    detect_balloon = 0;
    const Value* pg = page();
    block_los = false;
    if (pg) {
        const Value& l = (*pg)["list"];
        for (size_t i = 0; i < l.size(); i++) {
            if (l[i]["code"].as_int() == 108 && comment_text(l[i]).find("<block_los>") != std::string::npos) {
                block_los = true;
                break;
            }
        }
        orig_move_speed = (*pg)["moveSpeed"].num();
        orig_move_frequency = (*pg)["moveFrequency"].num();
        orig_direction = direction_;
        orig_x = x_;
        orig_y = y_;
    }
}

bool GameEvent::is_event_all_touch_trigger() const {
    if (all_touch_trigger) return true;
    std::string v;
    if (meta_has(note(), "AllTouchTrigger", &v)) return true;
    return false;
}

void GameEvent::setup_tkm_filters() {
    std::string n = note();
    std::vector<std::string> filters, set_filters, speeds;
    size_t from = 0;
    re::Groups g;
    while (re::search("<([^<>:]+)(:?)([^>]*)>", "", n, &g, from)) {
        if (g[2] == ":") {
            if (g[1] == "Filter") filters.push_back(g[3]);
            else if (g[1] == "SetFilter") set_filters.push_back(g[3]);
            else if (g[1] == "SetFilterSpeed") speeds.push_back(g[3]);
        }
        from = g.index + std::max<size_t>(g.length, 1);
    }
    if (filters.empty()) return;
    auto split = [](const std::string& s) {
        std::vector<std::string> out;
        size_t start = 0;
        while (true) {
            size_t c = s.find(',', start);
            out.push_back(s.substr(start, c == std::string::npos ? std::string::npos : c - start));
            if (c == std::string::npos) break;
            start = c + 1;
        }
        return out;
    };
    for (auto& f0 : filters) {
        auto f = split(f0);
        double t = f.size() > 2 ? js_parse_int(f[2]) : NAN;
        int target = std::isnan(t) ? 0 : (int)t;
        if (target == 4000) target += event_id_;
        game_map().create_filter(f[0], f.size() > 1 ? f[1] : "", target, event_id_, true);
    }
    if (set_filters.empty()) return;
    for (auto& f0 : set_filters) {
        auto f = split(f0);
        std::vector<std::string> rest(f.begin() + 1, f.end());
        FilterArgs a = filter_param_process(rest);
        if (FilterController* fcn = game_map().get_filter_controller(f[0])) fcn->set(a.values, a.has);
    }
    if (speeds.empty()) return;
    for (auto& f0 : speeds) {
        auto f = split(f0);
        double sp = f.size() > 1 ? js_number(f[1]) : NAN;
        if (std::isnan(sp) || sp == 0) sp = 0.01;
        if (game_map().get_filter_controller(f[0])) game_map().set_filter_addi_time(f[0], sp);
    }
}

}
