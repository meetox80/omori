#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "../js/bind.h"
#include "character.h"
#include "filter.h"

namespace rpg {

class GameInterpreter;
class FilterController;

class GameCommonEvent {
public:
    explicit GameCommonEvent(int id);
    ~GameCommonEvent();
    int common_event_id = 0;
    std::unique_ptr<GameInterpreter> interpreter;
    const Value& event() const;
    const Value* list() const;
    void refresh();
    bool is_active() const;
    void update();
};

struct MapLevel {
    std::vector<uint8_t> collision;
    std::vector<uint8_t> arrows;
    std::vector<uint8_t> regions;
    std::vector<int8_t> change;
    bool valid = false;
};

struct Reflection {
    double x = 0, y = 0, w = 0, h = 0;
    double cast = 0;
    double opacity = 255;
    double offset = 0;
};

struct FootprintSound {
    std::string name;
    double volume = 10, pitch = 150;
};

struct FootsoundRect {
    double x = 0, y = 0, w = 0, h = 0;
    AudioObj sound;
    bool contains(double px, double py) const { return w > 0 && h > 0 && px >= x && px < x + w && py >= y && py < y + h; }
};

class GameMapFog : public js::JsObject {
public:
    GameMapFog();
    js::Val id;
    std::string name;
    double opacity = 255;
    int blend_mode = 0;
    double scale_x = 1, scale_y = 1;
    double move_x = 0, move_y = 0;
    double width = 640, height = 480;
    bool map_bind = true;
    bool visible = true;
    bool deactivate_on_invisible = true;
    bool active = true;
    double priority = 0;
    double origin_x = 0, origin_y = 0;
    double move_px = 0, move_py = 0;
    js::Val origin_js, move_js;
    void show() {
        visible = true;
        active = true;
    }
    void sync_points();
    js::HostClass* js_class() const override;
};

class GameFootprint {
public:
    double x = 0, y = 0;
    int rows = 4, columns = 3;
    std::string graphics_name = "footprints_turn";
    int index = 0;
    int frame = 0;
    int direction = 0;
    int last_direction = 0;
    int duration = 60;
    double opacity = 255;
    double fade_speed = 20;
    bool removed = false;
    bool is_finished() const { return opacity <= 0 || removed; }
    void update() {
        if (duration > 0) duration--;
        else opacity -= fade_speed;
    }
};

class GameMap : public js::JsObject {
public:
    GameMap();
    ~GameMap() override;

    std::unique_ptr<GameInterpreter> interpreter;
    int map_id_ = 0;
    int tileset_id = 0;
    std::vector<std::shared_ptr<GameEvent>> events_;
    std::vector<std::unique_ptr<GameCommonEvent>> common_events;
    std::vector<std::unique_ptr<GameVehicle>> vehicles_;
    double display_x_ = 0, display_y_ = 0;
    bool name_display = true;
    int scroll_direction = 2;
    double scroll_rest = 0;
    double scroll_speed = 4;
    std::string parallax_name_;
    bool parallax_zero = false;
    bool parallax_loop_x = false, parallax_loop_y = false;
    double parallax_sx = 0, parallax_sy = 0, parallax_x = 0, parallax_y = 0;
    bool has_battleback1 = false, has_battleback2 = false;
    std::string battleback1_name_, battleback2_name_;
    bool needs_refresh = false;
    std::map<int, int> region_move_directions;
    std::map<std::string, std::shared_ptr<GameMapFog>> map_fogs;
    std::map<std::string, std::unique_ptr<FilterController>> filter_con;
    std::vector<std::string> filter_con_order;
    std::vector<std::shared_ptr<GameFootprint>> footprints_;
    std::vector<FootsoundRect> footsounds;
    std::vector<GameEvent*> tile_events;
    std::vector<std::shared_ptr<GameEvent>> spawned_events;
    bool recorded_morphed_events = false;
    std::map<int, std::pair<int, int>> recorded_morphed_event_data;
    bool has_scroll_target = false;
    double scroll_start_x = 0, scroll_start_y = 0, scroll_target_x = 0, scroll_target_y = 0;
    double scroll_duration = 0, scrolling_time = 0;
    bool has_override_display_name = false;
    std::string override_display_name;
    CharRef cam_target;
    bool cam_norm = false;
    bool cam_point = false;
    double cam_px = 0, cam_py = 0;
    double cam_speed = 800;
    double zoom_x = 1, zoom_y = 1;
    bool has_saved_cam = false;
    std::vector<std::string> saved_cam_args;
    void cam_control(const std::vector<std::string>& args);
    double cam_screen_x() const;
    double cam_screen_y() const;
    std::vector<MapLevel> levels;
    std::vector<Reflection> reflections;
    std::map<int, FootprintSound> region_footstep_sounds;
    std::map<int, int> region_common_events;
    std::vector<int> restrict_player_regions, restrict_event_regions, allow_player_regions, allow_event_regions;
    bool save_event_locations = false;
    std::shared_ptr<Document> doc;
    void refresh_dims();

    const Value& data() const;
    const Value& tiled() const;
    bool is_tiled_map() const;
    std::shared_ptr<const void> keep() const { return doc; }

    void setup(int map_id);
    bool load_map_data(int map_id);
    bool is_event_running() const;
    double tile_width() const;
    double tile_height() const;
    int map_id() const { return map_id_; }
    double display_x() const;
    double display_y() const;
    const std::string& parallax_name() const { return parallax_name_; }
    std::string battleback1_name() const;
    std::string battleback2_name() const;
    void request_refresh() { needs_refresh = true; }
    bool is_name_display_enabled() const { return name_display; }
    void disable_name_display() { name_display = false; }
    void enable_name_display() { name_display = true; }
    void create_vehicles();
    void referesh_vehicles();
    GameVehicle* vehicle(const std::string& type) const;
    GameVehicle* boat() const { return vehicles_[0].get(); }
    GameVehicle* ship() const { return vehicles_[1].get(); }
    GameVehicle* airship() const { return vehicles_[2].get(); }
    void setup_events();
    std::vector<GameEvent*> events() const;
    GameEvent* event(int id) const;
    void erase_event(int id);
    void setup_scroll();
    void setup_parallax();
    void setup_battleback();
    void set_display_pos(double x, double y);
    double parallax_ox() const;
    double parallax_oy() const;
    const Value& tileset() const;
    std::string display_name() const;
    int width() const;
    int height() const;
    bool is_loop_horizontal() const;
    bool is_loop_vertical() const;
    bool is_dash_disabled() const;
    const Value& encounter_list() const;
    double encounter_step() const;
    double screen_tile_x() const;
    double screen_tile_y() const;
    double adjust_x(double x) const;
    double adjust_y(double y) const;
    double round_x(double x) const;
    double round_y(double y) const;
    double x_with_direction(double x, int d) const { return x + (d == 6 ? 1 : d == 4 ? -1 : 0); }
    double y_with_direction(double y, int d) const { return y + (d == 2 ? 1 : d == 8 ? -1 : 0); }
    double round_x_with_direction(double x, int d) const { return round_x(x_with_direction(x, d)); }
    double round_y_with_direction(double y, int d) const { return round_y(y_with_direction(y, d)); }
    double delta_x(double x1, double x2) const;
    double delta_y(double y1, double y2) const;
    double distance(double x1, double y1, double x2, double y2) const;
    void autoplay();
    void refresh_if_needed();
    void refresh();
    void refresh_tile_events();
    std::vector<GameEvent*> events_xy(double x, double y) const;
    std::vector<GameEvent*> events_xy_nt(double x, double y) const;
    std::vector<GameEvent*> tile_events_xy(double x, double y) const;
    int event_id_xy(double x, double y) const;
    void scroll_down(double d);
    void scroll_left(double d);
    void scroll_right(double d);
    void scroll_up(double d);
    bool is_valid(double x, double y) const;
    bool check_passage(double x, double y, int bit) const;
    int tile_id(double x, double y, int z) const;
    bool check_layered_tiles_flags(double x, double y, int bit) const;
    bool is_passable(double x, double y, int d) const;
    bool is_boat_passable(double x, double y) const { return check_passage(x, y, 0x0200); }
    bool is_ship_passable(double x, double y) const { return check_passage(x, y, 0x0400); }
    bool is_airship_land_ok(double x, double y) const { return check_passage(x, y, 0x0800) && check_passage(x, y, 0x0f); }
    bool is_ladder(double x, double y) const;
    bool is_bush(double x, double y) const;
    bool is_counter(double x, double y) const;
    bool is_damage_floor(double x, double y) const;
    int terrain_tag(double x, double y) const;
    int region_id(double x, double y) const;
    void start_scroll(int direction, double distance, double speed);
    bool is_scrolling() const { return scroll_rest > 0; }
    void update(bool scene_active);
    void update_scroll();
    void base_update_scroll();
    double scroll_distance() const;
    void do_scroll(int direction, double distance);
    void update_events();
    void update_vehicles();
    void update_parallax();
    void change_tileset(int id);
    void change_battleback(const std::string& b1, const std::string& b2);
    void change_parallax(const std::string& name, bool loop_x, bool loop_y, double sx, double sy);
    void update_interpreter();
    void unlock_event(int id);
    bool setup_starting_event();
    bool setup_starting_map_event();
    bool setup_autorun_common_event();
    bool is_any_event_starting() const;
    void random_pos(int event_id, int region_id);
    bool is_save_event_locations() const { return save_event_locations; }
    void setup_tiled();
    void setup_tiled_levels();
    void restore_begin(int map_id);
    void restore_end();
    bool is_on_reflection(const GameCharacterBase& c) const;
    std::vector<int> get_reflections(const GameCharacterBase& c) const;
    bool is_half_tile() const { return false; }
    int current_map_level() const;
    void set_current_map_level(int level);
    bool check_map_level_changing(double x, double y);
    void check_reload_for_morphed_events(int map_id);
    void record_morphed_events();
    void process_morphed_events();
    void setup_spawned_events(int map_id);
    void restore_spawned_events();
    void spawn_event(int map_id, int event_id, double x, double y, bool preserved);
    void despawn_event_id(int id);
    std::vector<std::pair<int, int>> valid_spawn_points(const std::vector<int>& regions);
    GameEvent* first_spawned_event() const;
    GameEvent* last_spawned_event() const;
    bool is_region_event(double x, double y) const;
    int get_unique_region_common_event(int region) const;
    bool region_event_tag(double x, double y) const;
    bool move_after_common_event();
    std::string get_region_battleback_name(int type) const;
    void reset_all_event_locations();
    bool is_slippery(double x, double y) const;
    bool is_event_run_quick() const;
    bool is_any_event_starting_quick() const;
    void cam_target_set(const CharRef& target, double speed);
    void on_map_loaded_camera();
    void setup_common_event(int id);
    void stop_current_event();
    void set_region_move_direction(int region, int dir) { region_move_directions[region] = dir; }
    int position_auto_move_direction(double x, double y) const;
    void scroll_to_target(double x, double y, double duration);
    void update_lerp_scrolling();
    void clear_map_fogs() { map_fogs.clear(); }
    std::shared_ptr<GameMapFog> get_map_fog(const std::string& name) const;
    void add_map_fog(const std::string& name, std::shared_ptr<GameMapFog> fog) { map_fogs[name] = std::move(fog); }
    void remove_map_fog(const std::string& name) { map_fogs.erase(name); }
    void create_filter(const std::string& id, const std::string& filter, int target, int ch, bool map_only);
    void update_filter_con_arr();
    FilterController* get_filter_controller(const std::string& id) const;
    bool set_filter(const std::string& id, const FilterArgs& target);
    bool enable_filter(const std::string& id, bool enabled);
    bool move_filter(const std::string& id, const FilterArgs& target, int d);
    bool move_filter_queue(const std::string& id, const FilterArgs& target, int d);
    bool erase_filter(const std::string& id);
    bool erase_filter_after_move(const std::string& id);
    bool set_filter_addi_time(const std::string& id, double t);
    void setup_tkm_filters();
    void override_display_name_to(const std::string& t) {
        has_override_display_name = true;
        override_display_name = t;
    }
    void clear_override_display_name() {
        has_override_display_name = false;
        override_display_name.clear();
    }
    std::string display_name_dim_color(int which) const;
    void init_footprints() { footprints_.clear(); }
    void add_footprint(std::shared_ptr<GameFootprint> f) {
        if (f) footprints_.push_back(std::move(f));
    }
    void remove_footprint(const GameFootprint* f);
    const AudioObj* get_footsound(double x, double y) const;
    void process_footstep_sound();
    void process_map_notetags();
    std::string note() const;
    js::HostClass* js_class() const override;

private:
    struct Dims {
        bool tiled = false;
        int width = 0, height = 0;
        double tile_w = 48, tile_h = 48;
        double data_w = 0, data_h = 0;
        int scroll_type = 0;
    };
    Dims dims_;
    std::vector<std::shared_ptr<GameEvent>> update_group_;
    mutable int tileset_cache_id_ = -1;
    mutable const Value* tileset_ = nullptr;
    mutable const Value* tileset_flags_ = nullptr;
    mutable std::vector<int> slip_tags_;
    void refresh_tileset_cache() const;
};

const Value* preloaded_event(int map_id, int event_id, std::shared_ptr<const void>* keep);
std::shared_ptr<Document> load_map_doc(int id);

}
