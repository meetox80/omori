#pragma once
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "../js/bind.h"
#include "state.h"

namespace rpg {

class GameActor;
class GameCharacterBase;
class GameCharacter;
class GameEvent;
class GameInterpreter;

enum RouteCode {
    ROUTE_END = 0,
    ROUTE_MOVE_DOWN = 1,
    ROUTE_MOVE_LEFT = 2,
    ROUTE_MOVE_RIGHT = 3,
    ROUTE_MOVE_UP = 4,
    ROUTE_MOVE_LOWER_L = 5,
    ROUTE_MOVE_LOWER_R = 6,
    ROUTE_MOVE_UPPER_L = 7,
    ROUTE_MOVE_UPPER_R = 8,
    ROUTE_MOVE_RANDOM = 9,
    ROUTE_MOVE_TOWARD = 10,
    ROUTE_MOVE_AWAY = 11,
    ROUTE_MOVE_FORWARD = 12,
    ROUTE_MOVE_BACKWARD = 13,
    ROUTE_JUMP = 14,
    ROUTE_WAIT = 15,
    ROUTE_TURN_DOWN = 16,
    ROUTE_TURN_LEFT = 17,
    ROUTE_TURN_RIGHT = 18,
    ROUTE_TURN_UP = 19,
    ROUTE_TURN_90D_R = 20,
    ROUTE_TURN_90D_L = 21,
    ROUTE_TURN_180D = 22,
    ROUTE_TURN_90D_R_L = 23,
    ROUTE_TURN_RANDOM = 24,
    ROUTE_TURN_TOWARD = 25,
    ROUTE_TURN_AWAY = 26,
    ROUTE_SWITCH_ON = 27,
    ROUTE_SWITCH_OFF = 28,
    ROUTE_CHANGE_SPEED = 29,
    ROUTE_CHANGE_FREQ = 30,
    ROUTE_WALK_ANIME_ON = 31,
    ROUTE_WALK_ANIME_OFF = 32,
    ROUTE_STEP_ANIME_ON = 33,
    ROUTE_STEP_ANIME_OFF = 34,
    ROUTE_DIR_FIX_ON = 35,
    ROUTE_DIR_FIX_OFF = 36,
    ROUTE_THROUGH_ON = 37,
    ROUTE_THROUGH_OFF = 38,
    ROUTE_TRANSPARENT_ON = 39,
    ROUTE_TRANSPARENT_OFF = 40,
    ROUTE_CHANGE_IMAGE = 41,
    ROUTE_CHANGE_OPACITY = 42,
    ROUTE_CHANGE_BLEND_MODE = 43,
    ROUTE_PLAY_SE = 44,
    ROUTE_SCRIPT = 45
};

struct RouteCmd {
    int code = 0;
    const Value* params = nullptr;
    double num = 0;
    bool synthetic = false;
    double param_num(size_t i, double def = 0) const;
    std::string param_str(size_t i) const;
};

struct MoveRoute {
    std::vector<RouteCmd> list;
    bool repeat = false;
    bool skippable = false;
    bool wait = false;
    std::shared_ptr<const void> keep;
};

using RoutePtr = std::shared_ptr<MoveRoute>;

RoutePtr route_from(const Value& v, std::shared_ptr<const void> keep);
RoutePtr route_shared(const Value& v, std::shared_ptr<const void> keep);
RoutePtr route_copy(const RoutePtr& r);
void route_cache_clear();

struct CharRef {
    enum Kind : uint8_t { None, Player, Event, Follower, Vehicle };
    Kind kind = None;
    int id = 0;
    static CharRef of(const GameCharacterBase* c);
    GameCharacter* get() const;
    bool valid() const { return kind != None; }
    bool operator==(const CharRef& o) const { return kind == o.kind && id == o.id; }
};

struct EmfState {
    int frame_num = 3;
    int idle_frame = 0;
    std::vector<int> pattern{2, 1, 0, 1};
};

struct Bobble {
    double base = 0.5, speed = 0.05, amplitude = 1.25;
};

struct InputIcon {
    std::string input;
    js::Val key;
    js::Val gamepad;
    bool has_scale = false;
    double scale = 0;
};

struct ShadowData {
    double opacity = 160, angle = 180, x = 0, y = 0, scale_y = 1;
};

struct FrameAnimStep {
    std::string name;
    int index = 0;
    int direction = 2;
    int frame = 0;
    int duration = 1;
    AudioObj se;
};

struct CustomFrameAnimation {
    int index = 0;
    int count = 0;
    int repeat = 0;
    std::vector<FrameAnimStep> list;
};

struct TrembleInfo {
    bool active = false;
    double ang = 0, offsetx = 0;
    bool has_power = false;
    double power = 0, speed = 0;
    bool has_stop = false;
    double stop_cycle = 0, stop_ang = 0;
};

struct MovementGraphic {
    std::string name;
    int index = 0;
    bool set = false;
};

struct MovementGraphics {
    MovementGraphic idle, walking, running, climbing;
    MovementGraphic* get(const std::string& n);
};

class GameCharacterBase : public js::JsObject {
public:
    GameCharacterBase();
    ~GameCharacterBase() override = default;
    virtual void init_members();

    double x_ = 0, y_ = 0;
    double real_x = 0, real_y = 0;
    double move_speed_ = 4;
    double move_frequency_ = 6;
    double opacity_ = 255;
    int blend_mode_ = 0;
    int direction_ = 2;
    int pattern_ = 1;
    int priority_type = 1;
    int tile_id_ = 0;
    std::string character_name_;
    int character_index_ = 0;
    bool is_object_character_ = false;
    bool walk_anime = true;
    bool step_anime = false;
    bool direction_fix = false;
    bool through = false;
    bool transparent = false;
    double bush_depth_ = 0;
    int animation_id_ = 0;
    int balloon_id_ = 0;
    bool animation_playing = false;
    bool balloon_playing = false;
    double animation_count = 0;
    int stop_count = 0;
    int jump_count = 0;
    int jump_peak = 0;
    bool movement_success = true;
    std::vector<int> reflections;
    bool is_emf = false;
    EmfState emf;
    bool has_custom_frame_x = false, has_custom_frame_y = false, has_custom_frame_w = false, has_custom_frame_h = false;
    double custom_frame_x = 0, custom_frame_y = 0, custom_frame_w = 0, custom_frame_h = 0;
    bool has_sprite_offsets = false;
    double sprite_offset_x = 0, sprite_offset_y = 0;
    CharRef target;
    bool has_target_xy = false;
    double target_x = 0, target_y = 0;
    bool single_frame = false;
    bool has_bobble = false;
    Bobble bobble;
    bool has_input_icon = false;
    InputIcon input_icon;
    int spin_count = 0, spin_delay = 0, spin_delay_count = 0;
    bool has_shadow = false;
    ShadowData shadow;
    std::unique_ptr<CustomFrameAnimation> custom_frame_animation;
    bool disable_footprints = true;
    double footprint_x = 0, footprint_y = 0;
    int last_direction = 2;
    js::Val horror_filters;
    bool step_behavior_set = false;
    std::string step_animation_behavior;
    bool has_hitbox = false;
    int hitbox_up = 0, hitbox_left = 0, hitbox_right = 0, hitbox_down = 0;
    bool pattern_move_route_locked = false;
    bool gstop = false;
    bool always_update_movement_checked = false;
    bool always_update_movement = false;
    int foot_can_play = -1;
    double sprite_height_cache = -1;
    std::string eventcon_img;
    int eventcon_index = -1;
    bool eventcon_playing = false;
    bool has_eventcon = false;
    bool eventcon_index_defined = false;
    TrembleInfo tremble;
    void start_tremble(double power, double speed, double stop_cycle);
    bool stop_tremble();
    void update_tremble();

    double x() const { return x_; }
    double y() const { return y_; }
    virtual bool pos(double x, double y) const;
    bool pos_nt(double x, double y) const { return pos(x, y) && !is_through(); }
    double move_speed() const { return move_speed_; }
    void set_move_speed(double s) { move_speed_ = s; }
    double move_frequency() const { return move_frequency_; }
    void set_move_frequency(double f) { move_frequency_ = f; }
    double opacity() const { return opacity_; }
    void set_opacity(double o) { opacity_ = o; }
    int blend_mode() const { return blend_mode_; }
    void set_blend_mode(int b) { blend_mode_ = b; }
    bool is_normal_priority() const { return priority_type == 1; }
    void set_priority_type(int p) { priority_type = p; }
    bool is_moving() const { return real_x != x_ || real_y != y_; }
    bool is_jumping() const { return jump_count > 0; }
    double jump_height() const;
    virtual bool is_stopping() const { return !is_moving() && !is_jumping(); }
    bool check_stop(double threshold) const { return stop_count > threshold; }
    void reset_stop_count() { stop_count = 0; }
    virtual double real_move_speed() const;
    double distance_per_frame() const;
    virtual bool is_dashing() const { return false; }
    virtual bool is_debug_through() const { return false; }
    void straighten();
    int reverse_dir(int d) const { return 10 - d; }
    virtual bool can_pass(double x, double y, int d);
    virtual bool can_pass_diagonally(double x, double y, int horz, int vert);
    virtual bool is_map_passable(double x, double y, int d);
    bool base_is_map_passable(double x, double y, int d);
    virtual bool is_collided_with_characters(double x, double y);
    bool base_is_collided_with_characters(double x, double y);
    virtual bool is_collided_with_events(double x, double y);
    bool is_collided_with_vehicles(double x, double y);
    void set_position(double x, double y);
    void copy_position(const GameCharacterBase& c);
    virtual void locate(double x, double y);
    void base_locate(double x, double y);
    int direction() const { return direction_; }
    virtual void set_direction(int d);
    bool is_tile() const { return tile_id_ > 0 && priority_type == 0; }
    bool is_object_character() const { return is_object_character_; }
    double shift_y() const { return is_object_character() ? 0 : 6; }
    double scrolled_x() const;
    double scrolled_y() const;
    virtual double screen_x() const;
    virtual double screen_y() const;
    double screen_z() const;
    bool is_near_the_screen() const;
    virtual void update();
    void base_update();
    virtual void update_stop();
    void update_jump();
    virtual void update_move();
    void update_animation();
    double animation_wait() const;
    void update_animation_count();
    virtual void update_pattern();
    void base_update_pattern();
    int max_pattern() const;
    int pattern() const;
    void set_pattern(int p) { pattern_ = p; }
    virtual bool is_original_pattern() const;
    virtual void reset_pattern();
    void refresh_bush_depth();
    bool is_on_ladder() const;
    bool is_on_bush() const;
    int terrain_tag() const;
    int region_id() const;
    virtual void increase_steps();
    void base_increase_steps();
    int tile_id() const { return tile_id_; }
    const std::string& character_name() const { return character_name_; }
    int character_index() const { return character_index_; }
    void set_image(const std::string& name, int index);
    void set_tile_image(int tile_id);
    void check_event_trigger_touch_front(int d);
    virtual void check_event_trigger_touch(double x, double y) { (void)x, (void)y; }
    bool is_movement_succeeded() const { return movement_success; }
    void set_movement_success(bool s) { movement_success = s; }
    virtual void move_straight(int d);
    void base_move_straight(int d);
    virtual void move_diagonally(int horz, int vert);
    void base_move_diagonally(int horz, int vert);
    virtual void jump(double x_plus, double y_plus);
    void base_jump(double x_plus, double y_plus);
    bool has_walk_anime() const { return walk_anime; }
    void set_walk_anime(bool v) { walk_anime = v; }
    bool has_step_anime() const { return step_anime; }
    void set_step_anime(bool v) { step_anime = v; }
    bool is_direction_fixed() const { return direction_fix; }
    void set_direction_fix(bool v) { direction_fix = v; }
    bool is_through() const { return through; }
    void set_through(bool v) { through = v; }
    bool is_transparent() const;
    double bush_depth() const { return bush_depth_; }
    void set_transparent(bool v) { transparent = v; }
    void request_animation(double id);
    void request_balloon(int id) { balloon_id_ = id; }
    int animation_id() const { return animation_id_; }
    int balloon_id() const { return balloon_id_; }
    void start_animation() {
        animation_id_ = 0;
        animation_playing = true;
    }
    void start_balloon() {
        balloon_id_ = 0;
        balloon_playing = true;
    }
    bool is_animation_playing() const { return animation_id_ > 0 || animation_playing; }
    bool is_balloon_playing() const { return balloon_id_ > 0 || balloon_playing; }
    void end_animation() { animation_playing = false; }
    void end_balloon() { balloon_playing = false; }
    void update_reflection();
    void clear_emf_state();
    void setup_emf_character();
    virtual bool can_update_movement_graphics() { return false; }
    virtual bool should_use_running_graphics() const { return false; }
    bool is_climbing() const { return region_id() == 90; }
    virtual GameActor* movement_graphics_source() const { return nullptr; }
    virtual void update_movement_graphics();
    void update_toast();
    void set_custom_frame_xy(const js::Val& x, const js::Val& y);
    void set_custom_frame_wh(const js::Val& w, const js::Val& h);
    bool is_facing_towards(const GameCharacterBase& ev) const;
    bool is_facing_away(const GameCharacterBase& ev) const;
    bool is_facing_sideways(const GameCharacterBase& ev) const;
    bool is_position_front_of(double ex, double ey) const;
    bool is_position_back_of(double ex, double ey) const;
    bool is_position_side_of(double ex, double ey) const;
    void init_sprite_offsets() {
        has_sprite_offsets = true;
        sprite_offset_x = 0;
        sprite_offset_y = 0;
    }
    double sprite_offset_x_value() const { return has_sprite_offsets ? sprite_offset_x : 0; }
    double sprite_offset_y_value() const { return has_sprite_offsets ? sprite_offset_y : 0; }
    void update_step_animation_behavior();
    bool is_within_hitboxes(double x, double y) const;
    virtual void event_proximity_increase_steps() {}
    virtual bool can_play_footsteps();
    virtual void process_footstep_sound();
    void play_footstep_sound(double volume, double pitch, double pan);
    virtual bool is_event() const { return false; }
    virtual bool is_player() const { return false; }
    bool is_event_region_forbid(double x, double y, int d);
    bool is_player_region_forbid(double x, double y, int d);
    bool is_event_region_allow(double x, double y, int d);
    bool is_player_region_allow(double x, double y, int d);
    int get_region_id(double x, double y, int d) const;
    virtual void save_location() {}
    bool on_slippery_floor() const;
    bool slippery_pose() const;
    void smart_jump(int distance);
    void setup_smart_jump(int distance);
    int get_smart_jump_distance(int distance);
    bool is_smart_jump_illegal_region(double x, double y);
    int calc_smart_jump_distance(int distance);
    bool is_smart_jump_valid(double x, double y);
    virtual bool is_smart_jump_blocked() { return false; }
    void set_target(const CharRef& t, double tx, double ty, bool has_xy);
    void clear_target() {
        target = CharRef();
        has_target_xy = false;
    }
    double sprite_height();
    void setup_eventcon(const std::string& img, int index);
    void start_eventcon() { eventcon_playing = true; }
    bool is_eventcon_playing() const { return eventcon_index > 0 || eventcon_playing; }
    void remove_eventcon() {
        eventcon_index_defined = true;
        eventcon_img.clear();
        has_eventcon = false;
        eventcon_index = -1;
        eventcon_playing = false;
    }
    void end_eventcon() { eventcon_playing = false; }
    bool is_single_frame() const { return single_frame; }
    int position_auto_move_direction() const;
    ShadowData& setup_shadow() {
        has_shadow = true;
        shadow = ShadowData();
        return shadow;
    }
    void set_bobble(const std::vector<js::Val>& args);
    void setup_input_icon(const std::string& input, const js::Val& key, const js::Val& gamepad);
    void clear_spin() { spin_count = spin_delay = spin_delay_count = 0; }
    void start_spin(int times, int delay = 10) {
        spin_count = times;
        spin_delay = delay;
        spin_delay_count = delay;
    }
    void update_spinning();
    void start_custom_frame_animation(const std::vector<FrameAnimStep>& list, int repeats);
    void update_custom_frame_animation();
    void footprint_movement();
    virtual bool can_create_footprints() const { return !disable_footprints; }
    virtual std::string footprint_graphics_name() const { return "footprints_turn"; }
    virtual void clear_horror_effects();
    virtual void create_horror_effect(const std::string& type);
    virtual void remove_horror_effect(const std::string& type);
    virtual void set_horror_effect_to_value(const std::string& type, const std::string& key, const js::Val& v);
    virtual int event_id() const { return 0; }
    js::HostClass* js_class() const override;
};

class GameCharacter : public GameCharacterBase {
public:
    GameCharacter();
    void init_members() override;
    bool move_route_forcing = false;
    RoutePtr move_route;
    int move_route_index = 0;
    RoutePtr original_move_route;
    int original_move_route_index = 0;
    int wait_count = 0;
    std::vector<int> detecting;
    bool has_search_xy = false;
    double search_x = 0, search_y = 0;
    int detect_pause = 0;
    int detect_pause_time = 40;
    double orig_move_frequency = 6, orig_move_speed = 4, orig_x = 0, orig_y = 0;
    int orig_direction = 2;
    bool has_orig_direction = false;
    int detect_balloon = 0;
    int search_turn_index = -1;
    bool block_los = false;
    double d_target_x = 0, d_target_y = 0;
    int repeats = 0;
    int repeat_start_index = 0;

    void memorize_move_route();
    void restore_move_route();
    bool is_move_route_forcing() const { return move_route_forcing; }
    virtual void set_move_route(const RoutePtr& r);
    virtual void force_move_route(const RoutePtr& r);
    void queue_move_route(const RoutePtr& r) {
        original_move_route = r;
        original_move_route_index = 0;
    }
    void update_stop() override;
    void character_update_stop();
    void update_routine_move();
    void process_move_command(const RouteCmd& cmd);
    void process_yep_move_command(const RouteCmd& cmd);
    double delta_x_from(double x) const;
    double delta_y_from(double y) const;
    void move_random();
    void move_toward_xy(double x, double y);
    void move_away_from_xy(double x, double y);
    void turn_toward_xy(double x, double y);
    void turn_away_from_xy(double x, double y);
    void move_toward_character(const GameCharacterBase& c) { move_toward_xy(c.x_, c.y_); }
    void move_away_from_character(const GameCharacterBase& c) { move_away_from_xy(c.x_, c.y_); }
    void turn_toward_character(const GameCharacterBase& c) { turn_toward_xy(c.x_, c.y_); }
    void turn_away_from_character(const GameCharacterBase& c) { turn_away_from_xy(c.x_, c.y_); }
    void turn_toward_player();
    void turn_away_from_player();
    void move_toward_player();
    void move_away_from_player();
    void move_forward() { move_straight(direction()); }
    void move_backward();
    void process_route_end();
    void advance_move_route_index();
    void turn_right_90();
    void turn_left_90();
    void turn_180() { set_direction(reverse_dir(direction())); }
    void turn_right_or_left_90();
    void turn_random();
    void swap(GameCharacterBase& c);
    int find_direction_to(double gx, double gy);
    int find_direction_to_grid(int goal_x, int goal_y);
    int find_direction_to_slow(double goal_x, double goal_y);
    int search_limit() const;
    void process_move_route_script(const RouteCmd& cmd);
    void process_move_route_eval(const Value& ast);
    void process_move_route_self_switch(const std::string& key, const std::string& setting);
    void process_move_route_self_variable(const std::string& key, const Value& ast);
    void jump_forward(double distance);
    void jump_to_point(double x, double y);
    void jump_to_event(int id);
    void move_repeat(int direction, int times);
    void move_to_event(int id, bool collision);
    void pattern_lock(double index);
    void pattern_unlock() { pattern_move_route_locked = false; }
    void step_away_from_point(double x, double y);
    void step_away_from_event(int id);
    void step_toward_point(double x, double y);
    void step_toward_event(int id);
    void teleport_to_point(double x, double y);
    void teleport_to_event(int id);
    void turn_away_from_point(double x, double y);
    void turn_away_from_event(int id);
    void turn_toward_point(double x, double y);
    void turn_toward_event(int id);
    void move_to_point(double x, double y, bool collision);
    void return_to_saved_xy();
    void fade_to(double opacity, double frames);
    void repeat_begin(int times) {
        repeats = times - 1;
        repeat_start_index = move_route_index;
    }
    void repeat_end() {
        if (repeats > 0) {
            move_route_index = repeat_start_index;
            repeats -= 1;
        }
    }
    void fade(double speed);
    void jump_forward_galv(double count);
    void jump_to_galv(const std::vector<js::Val>& args);
    void sswitch(const std::string& letter, const js::Val& status, int eid, int mapid);
    void rwait(int low, int high);
    void step_rand(const std::vector<js::Val>& regions);
    void set_frame(const std::string& name, int index, int pattern, int dir);
    void restore_frame() { gstop = false; }
    bool dist_detect(double range, bool los, int id);
    bool is_detecting(int id) const;
    void do_detected(int id);
    void do_undetected(int id);
    void detector(int id);
    void do_detect_move(int id, int type);
    void set_detect_vars();
    void move_toward_last_detect();
    void get_tool_graphic(int shelf_item);
    void get_random_npc_graphic();
    js::HostClass* js_class() const override;
};

class GameFollower;

class GameFollowers : public js::JsObject {
public:
    GameFollowers();
    bool visible = true;
    bool gathering = false;
    std::vector<std::unique_ptr<GameFollower>> data;
    bool is_visible() const { return visible; }
    void show() { visible = true; }
    void hide() { visible = false; }
    GameFollower* follower(int index);
    void refresh();
    void update();
    void update_move();
    void jump_all();
    void synchronize(double x, double y, int d);
    void gather() { gathering = true; }
    bool are_gathering() const { return gathering; }
    std::vector<GameFollower*> visible_followers();
    bool are_moving();
    bool are_gathered();
    bool is_someone_collided(double x, double y);
    int next_extra_follower_slot();
    int last_extra_follower_slot();
    void clear_extra_followers();
    void add_extra_follower(const js::Val& id, const std::string& name, int index, int slot);
    void remove_extra_follower(int index);
    void remove_follower_by_id(const js::Val& id);
    void realign_extra_followers();
    GameFollower* get_follower_by_id(const js::Val& id);
    std::vector<GameFollower*> all();
    js::HostClass* js_class() const override;
};

struct ExtraGraphics {
    js::Val id;
    std::string name;
    int index = 0;
};

class GameFollower : public GameCharacter {
public:
    explicit GameFollower(int member_index);
    int member_index = 0;
    int fc_event_id = -1;
    bool fc_event_lock = false;
    int fc_prelock_direction = 0;
    bool fc_retain_attrib = false;
    std::unique_ptr<MovementGraphics> character_movement_graphics;
    bool has_character_movement_graphics = false;
    std::unique_ptr<ExtraGraphics> graphics_data;

    void refresh();
    GameActor* actor() const;
    bool is_visible() const;
    void update() override;
    void chase_character(const GameCharacterBase& c);
    void update_stop() override;
    void lock();
    void unlock();
    void update_self_movement();
    double stop_count_threshold() const { return 30 * (5 - move_frequency()); }
    void move_type_random();
    void move_type_toward_player();
    bool is_near_the_player() const;
    bool is_collided_with_characters(double x, double y) override;
    bool can_update_movement_graphics() override;
    GameActor* movement_graphics_source() const override;
    bool should_use_running_graphics() const override;
    void set_movement_graphics(const js::Val& idle, const js::Val& walking, const js::Val& running);
    void clear_movement_graphics();
    void set_movement_graphic_data(const std::string& name, const js::Val& data);
    js::Val movement_graphics_js() const;
    void set_movement_graphics_js(const js::Val& v);
    void update_movement_graphics() override;
    bool can_play_footsteps() override;
    bool update_position_auto_move();
    bool can_create_footprints() const override { return false; }
    void clear_horror_effects() override;
    void create_horror_effect(const std::string& type) override;
    void remove_horror_effect(const std::string& type) override;
    void set_horror_effect_to_value(const std::string& type, const std::string& key, const js::Val& v) override;
    int move_type = 0;
    js::HostClass* js_class() const override;
};

class GameVehicle;

struct AmbienceData {
    std::string name;
    double x = 0, y = 0, opacity = 255;
    bool use_custom_position = false;
};

class GamePlayer : public GameCharacter {
public:
    GamePlayer();
    void init_members() override;
    std::string vehicle_type = "walk";
    bool vehicle_getting_on = false;
    bool vehicle_getting_off = false;
    bool dashing = false;
    bool needs_map_reload = false;
    bool transferring = false;
    int new_map_id = 0;
    double new_x = 0, new_y = 0;
    int new_direction = 0;
    int fade_type = 0;
    GameFollowers followers_;
    double encounter_count = 0;
    std::vector<int> blocked_movement_inputs;
    AmbienceData ambience;
    bool footstep_disabled = false;
    js::Val footstep_sound;

    void clear_transfer_info();
    GameFollowers& followers() { return followers_; }
    void refresh();
    bool is_stopping() const override;
    void reserve_transfer(int map_id, double x, double y, int d, int fade);
    void request_map_reload() { needs_map_reload = true; }
    bool is_transferring() const { return transferring; }
    void perform_transfer();
    bool is_map_passable(double x, double y, int d) override;
    GameVehicle* vehicle() const;
    bool is_in_boat() const { return vehicle_type == "boat"; }
    bool is_in_ship() const { return vehicle_type == "ship"; }
    bool is_in_airship() const { return vehicle_type == "airship"; }
    bool is_in_vehicle() const { return is_in_boat() || is_in_ship() || is_in_airship(); }
    bool is_normal() const { return vehicle_type == "walk" && !is_move_route_forcing(); }
    bool is_dashing() const override;
    bool is_collided(double x, double y);
    double center_x() const;
    double center_y() const;
    void center(double x, double y);
    void locate(double x, double y) override;
    void increase_steps() override;
    void make_encounter_count();
    int make_encounter_troop_id();
    bool meets_encounter_conditions(const Value& encounter) const;
    bool execute_encounter();
    void start_map_event(double x, double y, const std::vector<int>& triggers, bool normal);
    void move_by_input();
    bool can_move();
    int get_input_direction();
    void execute_move(int d) { move_straight(d); }
    void update_scene(bool scene_active);
    void update_dashing();
    bool is_dash_button_pressed() const;
    void update_scroll(double last_x, double last_y);
    void update_vehicle();
    void update_vehicle_get_on();
    void update_vehicle_get_off();
    void update_nonmoving(bool was_moving);
    bool trigger_action();
    bool trigger_button_action();
    void update_encounter_count();
    bool can_encounter() const;
    double encounter_progress_value() const;
    void check_event_trigger_here(const std::vector<int>& triggers);
    void check_event_trigger_there(const std::vector<int>& triggers);
    void check_event_trigger_touch(double x, double y) override;
    bool can_start_local_events() const;
    bool get_on_off_vehicle();
    bool get_on_vehicle();
    bool get_off_vehicle();
    void force_move_forward();
    bool is_on_damage_floor() const;
    void move_straight(int d) override;
    void move_diagonally(int horz, int vert) override;
    void jump(double x_plus, double y_plus) override;
    void show_followers() { followers_.show(); }
    void hide_followers() { followers_.hide(); }
    void gather_followers() { followers_.gather(); }
    bool are_followers_gathering() const { return followers_.are_gathering(); }
    bool are_followers_gathered() { return followers_.are_gathered(); }
    bool is_collided_with_characters(double x, double y) override;
    bool is_collided_with_followers(double x, double y);
    bool can_update_movement_graphics() override;
    GameActor* movement_graphics_source() const override;
    bool should_use_running_graphics() const override { return is_dashing(); }
    void start_proximity_event(const std::vector<int>& triggers, bool normal);
    bool meet_player_proximity_conditions(const GameEvent& ev) const;
    bool can_play_footsteps() override;
    void process_footstep_sound() override;
    void process_region_event();
    bool is_player() const override { return true; }
    void update_slippery();
    void clear_blocked_movement_inputs() { blocked_movement_inputs.clear(); }
    void block_movement_inputs(const std::vector<int>& dirs);
    void unblock_movement_inputs(const std::vector<int>& dirs);
    void clear_ambience_data() { ambience = AmbienceData(); }
    double ambience_screen_x() const;
    double ambience_screen_y() const;
    bool can_use_character_tag() const;
    void clear_horror_effects() override;
    void create_horror_effect(const std::string& type) override;
    void remove_horror_effect(const std::string& type) override;
    void set_horror_effect_to_value(const std::string& type, const std::string& key, const js::Val& v) override;
    bool can_create_footprints() const override;
    std::string footprint_graphics_name() const override;
    bool is_foot_sound_enabled() const { return !footstep_disabled; }
    bool can_play_footsound() const;
    void play_foot_sound();
    int get_footsound_frequency() const;
    void update_pattern() override;
    js::HostClass* js_class() const override;
};

struct SpawnData {
    int base_map_id = 0;
    int spawn_id = 0;
    int map_id = 0;
    int event_id = 0;
    double x = 0, y = 0;
    bool preserved = false;
};

class GameEvent : public GameCharacter {
public:
    GameEvent(int map_id, int event_id, const SpawnData* spawn = nullptr);
    void init_members() override;
    int map_id = 0;
    int event_id_ = 0;
    int move_type = 0;
    int trigger = 0;
    bool trigger_null = false;
    bool starting = false;
    bool erased = false;
    int page_index = -2;
    int original_pattern = 1;
    int original_direction = 2;
    int prelock_direction = 0;
    bool locked = false;
    int fc_follower_id = -1;
    std::unique_ptr<GameInterpreter> interpreter;
    bool copied_event = false;
    int copied_map_id = 0, copied_event_id = 0;
    bool all_touch_trigger = false;
    bool morphed = false;
    int morph_map_id = 0, morph_event_id = 0;
    bool spawned = false;
    int spawn_id = 0, spawn_map_id = 0, spawn_event_id = 0;
    bool spawn_preserved = false;
    SpawnData spawn_data;
    bool initial_auto_trigger_bypass = false;
    int activation_dist = 0;
    std::string activation_type = "none";
    int alert_balloon = 1;
    int alert_common_event = 0;
    double alert_lock = 0;
    bool alert_player = false;
    std::string alert_sound;
    double alert_sound_vol = 100, alert_sound_pitch = 100, alert_sound_pan = 0;
    double alert_timer = 0;
    bool chase_player = false;
    double chase_range = 0;
    double chase_speed = 4, default_speed = 4;
    bool flee_player = false;
    double flee_range = 0;
    double flee_speed = 4;
    bool see_player = true;
    double sight_lock = 300;
    bool return_after = true;
    double return_wait = 180;
    bool return_phase = false;
    double return_frames = 0;
    int stagger_count = 0;
    double start_location_x = 0, start_location_y = 0;
    int start_location_dir = 2;

    int event_id() const override;
    const Value& event() const;
    std::shared_ptr<const void> event_keep() const;
    const Value* page() const;
    const Value* list() const;
    bool is_collided_with_characters(double x, double y) override;
    bool is_collided_with_events(double x, double y) override;
    bool is_collided_with_player_characters(double x, double y);
    void lock();
    void unlock();
    void update_stop() override;
    void update_self_movement();
    void base_update_self_movement();
    double stop_count_threshold() const { return 30 * (5 - move_frequency()); }
    void move_type_random();
    void move_type_toward_player();
    bool is_near_the_player() const;
    bool is_starting() const { return starting; }
    void clear_starting_flag() { starting = false; }
    bool is_trigger_in(const std::vector<int>& triggers) const;
    void start();
    void erase();
    void refresh();
    int find_proper_page_index();
    bool meets_conditions(const Value& page);
    void setup_page();
    void clear_page_settings();
    void setup_page_settings();
    bool is_original_pattern() const override { return pattern() == original_pattern; }
    void reset_pattern() override { set_pattern(original_pattern); }
    void check_event_trigger_touch(double x, double y) override;
    void base_check_event_trigger_touch(double x, double y);
    void check_event_trigger_auto();
    void update() override;
    void update_parallel();
    void locate(double x, double y) override;
    void base_locate_event(double x, double y);
    void force_move_route(const RoutePtr& r) override;
    void setup_copy_event();
    void encounter_lock();
    bool is_encounter_direction_locked() const;
    bool is_follower_trigger_touch() const;
    void init_event_morpher(int map_id, int event_id);
    void morph_into(int map_id, int event_id);
    void remove_morph();
    void setup_sprite_offsets();
    void setup_step_animation_options();
    void setup_hitbox_resize();
    void setup_event_proximity_settings();
    void event_proximity_increase_steps() override;
    bool meet_event_proximity_conditions(bool parallel);
    bool can_play_footsteps() override;
    bool is_event() const override { return true; }
    void clear_chase_settings();
    bool can_see_player();
    void update_chase_distance();
    bool chase_conditions(double dis);
    bool non_see_player();
    void start_event_chase();
    void end_event_chase();
    void update_flee_distance();
    bool flee_conditions(double dis);
    void start_event_flee();
    void end_event_flee();
    void update_chase_movement();
    void update_flee_movement();
    void update_alert();
    bool alert_conditions() const { return (chase_player || flee_player) && !alert_player; }
    void activate_alert();
    void start_return_phase();
    void update_return_phase() {
        if (return_phase) return_frames--;
    }
    void update_move_return_after();
    void update_move() override;
    bool is_save_location() const;
    mutable const Value* save_location_note_ = nullptr;
    mutable int save_location_map_ = -1;
    mutable bool save_location_tag_ = false;
    void save_location() override;
    bool is_load_location() const;
    void load_location();
    void reset_location();
    bool is_smart_jump_blocked() override;
    bool prevent_self_movement() const;
    void set_detect_stuff();
    bool is_event_all_touch_trigger() const;
    void setup_tkm_filters();
    std::string note() const;
    js::HostClass* js_class() const override;
};

class GameVehicle : public GameCharacter {
public:
    explicit GameVehicle(const std::string& type);
    void init_members() override;
    std::string type;
    int map_id = 0;
    double altitude = 0;
    bool driving = false;
    AudioObj bgm;
    bool is_boat() const { return type == "boat"; }
    bool is_ship() const { return type == "ship"; }
    bool is_airship() const { return type == "airship"; }
    void reset_direction() { set_direction(4); }
    void init_move_speed();
    const Value& vehicle() const;
    void load_system_settings();
    void refresh();
    void set_location(int map_id, double x, double y);
    bool pos(double x, double y) const override;
    bool is_map_passable(double x, double y, int d) override;
    void get_on();
    void get_off();
    void play_bgm();
    void sync_with_player();
    double screen_y() const override;
    double shadow_x() const { return screen_x(); }
    double shadow_y() const { return screen_y() + altitude; }
    double shadow_opacity() const { return 255 * altitude / max_altitude(); }
    bool can_move() const { return is_airship() ? is_highest() : true; }
    void update() override;
    void update_airship();
    double max_altitude() const { return 48; }
    bool is_lowest() const { return altitude <= 0; }
    bool is_highest() const { return altitude >= max_altitude(); }
    bool is_takeoff_ok();
    bool is_land_ok(double x, double y, int d);
    js::HostClass* js_class() const override;
};

namespace fc {
extern bool follower_control;
extern int follower_control_id;
extern bool follower_stop;
extern bool follower_collision;
}

namespace horror {
void clear(js::Val& hf);
void create(js::Val& hf, const std::string& type);
void remove(js::Val& hf, const std::string& type);
void set_value(js::Val& hf, const std::string& type, const std::string& key, const js::Val& v);
js::Val copy(const js::Val& hf);
}

}
