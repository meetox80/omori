#pragma once
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../rpg/action.h"
#include "../rpg/battler.h"
#include "scene_menu_base.h"
#include "window_selectable.h"

namespace ui {
namespace menu {

using Easing = double (*)(double t, double b, double c, double d);
double linear_tween(double t, double b, double c, double d);
double ease_out_circ(double t, double b, double c, double d);
double ease_in_circ(double t, double b, double c, double d);

struct Prop {
    std::function<double()> get;
    std::function<void(double)> set;
};

Prop prop_x(g2d::DisplayObject* o);
Prop prop_y(g2d::DisplayObject* o);
Prop prop_opacity(g2d::DisplayObject* o);
Prop prop_scale_x(g2d::DisplayObject* o);
Prop prop_scale_y(g2d::DisplayObject* o);
Prop prop_width(g2d::Window* w);
Prop prop_height(g2d::Window* w);
Prop prop_window_opacity(g2d::Window* w);
Prop prop_contents_opacity(g2d::Window* w);
Prop prop_openness(g2d::Window* w);

class ObjectMovement {
public:
    struct Track {
        Prop prop;
        double from = 0;
        double to = 0;
        int time = 0;
        int duration = 0;
        Easing easing = nullptr;
        double speed = 0;
    };
    struct Move {
        const void* obj = nullptr;
        std::vector<Track> tracks;
        std::function<void()> on_finish;
        int id = 0;
    };
    struct TweenSpec {
        Prop prop;
        double to;
        int duration;
    };
    struct SpeedSpec {
        Prop prop;
        double to;
        double speed;
    };

    static Move tween(const void* obj, std::initializer_list<TweenSpec> specs, Easing easing = nullptr);
    static Move tween(const void* obj, const std::vector<TweenSpec>& specs, Easing easing = nullptr);
    static Move speeds(const void* obj, std::initializer_list<SpeedSpec> specs);

    void clear() { list_.clear(); }
    bool is_moving() const { return !list_.empty(); }
    void activate() { active_ = true; }
    void deactivate() { active_ = false; }
    void start_move(Move data, bool uniq = true);
    void remove_move_with_object(const void* obj);
    void update();

private:
    std::vector<Move> list_;
    std::vector<int> remove_list_;
    bool active_ = true;
    int next_id_ = 1;
    void update_move(Move& data);
};

double lang_num(const Value& v, std::initializer_list<std::pair<const char*, js::Val>> vars = {}, const js::Val& self = js::Val::undef());
js::Val js_rect(const ItemRect& r);
js::Val js_const_fn(double n);
std::string js_fmt(const std::string& fmt, std::initializer_list<std::string> args);
std::string num_str(double n);
g2d::BitmapPtr blurred_background();
int item_meta_int(const rpg::ItemRef& item, const char* key, int def = 0);
std::string item_meta_str(const rpg::ItemRef& item, const char* key);
bool item_meta_has(const rpg::ItemRef& item, const char* key);
std::string item_name(const rpg::ItemRef& item);
std::string item_description(const rpg::ItemRef& item);
const Value& plugin_data(const char* type, const char* name);
const Value& blue(const char* key);
const Value& lang_pinned(const char* file, const char* key);

rpg::SceneFactory omo_menu_item_scene();
rpg::SceneFactory omo_menu_skill_scene();
rpg::SceneFactory omo_menu_equip_scene();
rpg::SceneFactory omo_menu_options_scene();
void snap_for_shop();

class SceneBaseEX : public rpg::Scene {
public:
    void update() override;
    void wait(int amount) { wait_count_ = amount; }
    void set_wait_mode(const std::string& mode) { wait_mode_ = mode; }
    void start_function_wait(std::function<bool()> fn);
    void set_function_list_index(int) { queue_index_ = 0; }
    void clear_function_list_index() { queue_index_ = 0; }
    void clear_function_list() { queue_.clear(); }
    void queue(std::function<void()> fn);
    void queue_wait_mode(const std::string& mode);
    void queue_wait(int amount);
    void call_next_queue_function();
    bool update_queue_processing();
    bool update_wait();
    bool update_wait_count();
    bool update_wait_mode();
    void clear_queue() {
        clear_function_list_index();
        clear_function_list();
    }
    void clear_wait_mode() { set_wait_mode(""); }

    ObjectMovement move;

protected:
    std::vector<std::function<void()>> queue_;
    int queue_index_ = 0;
    int wait_count_ = 0;
    std::string wait_mode_;
    std::function<bool()> wait_function_;
};

class SpriteOmoMenuStatusFace : public g2d::Sprite {
public:
    SpriteOmoMenuStatusFace();
    void update_bitmap();
    rpg::GameActor* actor() const { return actor_; }
    void set_actor(rpg::GameActor* a);
    virtual int max_frames() { return 3; }
    virtual int default_delay() { return 12; }
    double face_width() const { return face_width_; }
    double face_height() const { return face_height_; }
    void activate() { active_ = true; }
    void deactivate() { active_ = false; }
    void set_anim_row(int index);
    void update() override;
    void update_frame();

    bool active_ = true;
    bool in_menu_ = false;
    int anim_row_ = 0;
    int anim_delay_ = 12;
    int anim_frame_ = 0;

protected:
    rpg::GameActor* actor_ = nullptr;
    double face_width_ = 106;
    double face_height_ = 106;
};

class WindowOmoMenuHelp : public WindowHelp {
public:
    void initialize(double w, double h);
    void initialize() { initialize(window_width(), window_height()); }
    void initialize(int) override { initialize(window_width(), window_height()); }
    void initialize(double x, double y, double w, double h) override { WindowHelp::initialize(x, y, w, h); }
    int standard_padding() override { return 4; }
    virtual double window_width() { return 640 - 22; }
    virtual double window_height() { return 114 - 24; }
    int standard_font_size() override;
    double calc_text_height(const TextState&, bool) override { return 21; }
    void refresh() override;
    void clear() override { set_item(rpg::ItemRef()); }
    void set_item(const rpg::ItemRef& item) override;
    double wordwrap_width() override;
    const rpg::ItemRef& item() const { return item_; }

    double icon_rate = 1.0;
    double hide_y = 0, hide_height = 0, show_y = 0, show_height = 0;

protected:
    rpg::ItemRef item_;
    bool has_item_ = false;
};

class WindowOmoWindowIndexCursor : public WindowBase {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 10; }
    void create_index_sprite();
    void set_text(const std::string& text);
    void refresh();
    const std::string& text() const { return text_; }
    g2d::SpritePtr index_sprite;

protected:
    std::string text_;
};

class WindowGold : public WindowBase {
public:
    void initialize(double x, double y);
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    virtual double window_width() { return 108 + 20; }
    virtual double window_height() { return 40; }
    int standard_padding() override { return 0; }
    double line_height() override { return 28; }
    virtual void refresh();
    virtual double value();
    virtual std::string currency_unit();
    void open() override;
};

class WindowMenuCommand : public WindowCommand {
public:
    void initialize(double x, double y) override;
    using WindowCommand::initialize;
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    double line_height() override { return 24; }
    int standard_font_size() override;
    double window_width() override { return 640 - 20; }
    int spacing() override { return 12; }
    int standard_padding() override { return 15; }
    int num_visible_rows() override { return 1; }
    int max_cols() override { return max_items(); }
    double custom_cursor_rect_y_offset() override { return 1; }
    double custom_cursor_rect_text_x_offset() override { return 20; }
    ItemRect item_rect_for_text(int index) override;
    void make_command_list() override;
    void draw_item(int index) override;
    void process_ok() override;
    void select_last();
    void clear_handlers() { handlers_.clear(); }

    static std::string& last_command_symbol();
    static void init_command_position();
};

class WindowOmoMainMenuActorStatus : public WindowBase {
public:
    void initialize(int index);
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 0; }
    virtual double window_width() { return 155; }
    virtual double window_height() { return 109; }
    double contents_opacity_value() const { return contents_opacity(); }
    void set_contents_opacity_all(double value);
    rpg::GameActor* actor();
    void create_back_sprite();
    void create_face_sprite();
    void animate_face(bool b) { face_sprite->active_ = b; }
    void refresh();
    void update() override;

    int actor_index = 0;
    double origin_x_ = 0;
    std::shared_ptr<SpriteOmoMenuStatusFace> face_sprite;
    g2d::SpritePtr back_sprite;
};

class WindowOmoMainMenuPartyStatus : public WindowSelectable {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }
    int standard_padding() override { return 0; }
    virtual double window_width() { return 143; }
    virtual double window_height() { return 109; }
    int max_items() override;
    int max_cols() override;
    rpg::GameActor* actor(int index);
    rpg::GameActor* actor() { return actor(index()); }
    void play_ok_sound() override;
    std::vector<rpg::GameActor*> party();
    void create_status_windows();
    void sort_status_windows(bool pos = true);
    void reorder_status_windows(int from, int to);
    void create_cursor();
    void set_cursor_text(const std::string& text);
    void update_cursor() override;
    void refresh() override;
    void clear_handlers() { handlers_.clear(); }

    bool ok_sound_enabled = true;
    std::vector<std::shared_ptr<WindowOmoMainMenuActorStatus>> status_windows;
    std::shared_ptr<WindowOmoWindowIndexCursor> cursor_window;

protected:
    void update_transform_hook() override;
};

class WindowItemList : public WindowSelectable {
public:
    void initialize(double x, double y, double w, double h) override;
    virtual void set_category(const std::string& category);
    int max_cols() override { return 2; }
    int spacing() override { return 48; }
    int max_items() override { return has_data_ ? (int)data_.size() : 1; }
    virtual rpg::ItemRef item();
    bool is_current_item_enabled() override { return is_enabled(item()); }
    virtual bool includes(const rpg::ItemRef& item);
    virtual bool needs_number() { return true; }
    virtual bool is_enabled(const rpg::ItemRef& item);
    virtual void make_item_list();
    virtual void select_last();
    void draw_item(int index) override;
    virtual double number_width();
    virtual void draw_item_number(const rpg::ItemRef& item, double x, double y, double width);
    void update_help() override;
    void refresh() override;
    const std::string& category() const { return category_; }
    std::vector<rpg::ItemRef>& data() { return data_; }

protected:
    std::string category_ = "none";
    std::vector<rpg::ItemRef> data_;
    bool has_data_ = false;
};

struct Handoff {
    std::shared_ptr<WindowMenuCommand> command_window;
    std::shared_ptr<WindowOmoMainMenuPartyStatus> status_window;
};
void set_handoff(std::shared_ptr<WindowMenuCommand> cmd, std::shared_ptr<WindowOmoMainMenuPartyStatus> status);
Handoff take_handoff();

class SceneOmoMenuBase : public SceneBaseEX {
public:
    void create() override;
    void stop() override;
    void terminate() override;
    void set_command_window(std::shared_ptr<WindowMenuCommand> w) { command_window = std::move(w); }
    void set_status_window(std::shared_ptr<WindowOmoMainMenuPartyStatus> w) { status_window = std::move(w); }
    virtual void create_command_window();
    virtual void create_status_windows();
    virtual void create_help_window();
    virtual void create_background();
    void hide_actor_status(int index, int duration = 15);
    void show_actor_status(int index, int duration = 15);
    void show_info_window(g2d::Window* obj, int duration);
    void hide_info_window(g2d::Window* obj, int duration);
    void show_help_window(int duration = 15);
    void hide_help_window(int duration = 15);
    virtual rpg::ItemRef item() { return rpg::ItemRef(); }
    virtual rpg::GameBattler* user() { return nullptr; }
    virtual void play_se_for_item() {}
    void use_item();
    bool is_item_for_all();
    std::vector<rpg::GameBattler*> item_target_actors();
    bool can_use();
    bool can_use(const std::vector<rpg::GameBattler*>& targets);
    bool is_item_effects_valid(const std::vector<rpg::GameBattler*>& targets);
    void apply_item();
    void check_common_event();
    void check_gameover();
    void pop_to_menu();

    std::shared_ptr<g2d::Sprite> background_sprite;
    std::shared_ptr<WindowMenuCommand> command_window;
    std::shared_ptr<WindowOmoMainMenuPartyStatus> status_window;
    std::shared_ptr<WindowOmoMenuHelp> help_window;
};

class SceneMenu : public SceneMenuBase {
public:
    const char* name() const override { return "Scene_Menu"; }
    void create() override;
    void create_background() override;
    bool is_busy() override;
    void start() override;
    void terminate() override;
    void create_command_window();
    void create_status_windows();
    void create_gold_window();
    void command_talk();
    void command_personal();
    void on_personal_cancel();
    void on_personal_ok();
    void command_options();
    void process_tag();
    void close_main_menu();

    std::shared_ptr<WindowMenuCommand> command_window;
    std::shared_ptr<WindowOmoMainMenuPartyStatus> status_window;
    std::shared_ptr<WindowGold> gold_window;
};

}
}
