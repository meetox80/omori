#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../gfx2d/window.h"
#include "../js/value.h"

namespace rpg {

class FilterController;

class Scene : public g2d::Container {
public:
    virtual ~Scene();
    virtual const char* name() const = 0;
    virtual void create() {}
    virtual bool is_ready() { return true; }
    virtual void start() { active_ = true; }
    void update() override;
    virtual void stop() { active_ = false; }
    virtual bool is_busy() { return fade_duration_ > 0; }
    virtual void terminate() {}
    bool is_active() const { return active_; }
    void create_window_layer();
    void add_window(g2d::DisplayPtr w);
    void start_fade_in(int duration = 0, bool white = false);
    void start_fade_out(int duration = 0, bool white = false);
    void create_fade_sprite(bool white);
    void update_fade();
    void update_children();
    void fade_out_all();
    void pop_scene();
    static int fade_speed() { return 24; }
    static int slow_fade_speed() { return fade_speed() * 2; }
    virtual bool js_get(uint32_t key, js::Val& out) { (void)key, (void)out; return false; }
    virtual bool js_set(uint32_t key, const js::Val& v) { (void)key, (void)v; return false; }
    virtual void remove_filter_controller(FilterController* f) { (void)f; }
    js::Val to_js();
    js::Expando js_expando;
    js::Obj* js_obj = nullptr;
    std::shared_ptr<g2d::WindowLayer> window_layer;
    std::shared_ptr<g2d::ScreenSprite> fade_sprite;
    int fade_sign = 0;
    int fade_duration_ = 0;

protected:
    bool active_ = false;
};

struct SceneFactory {
    std::string name;
    std::function<std::unique_ptr<Scene>()> make;
    explicit operator bool() const { return (bool)make; }
};

template <class T>
SceneFactory scene_of(const char* name) {
    return SceneFactory{name, []() -> std::unique_ptr<Scene> { return std::unique_ptr<Scene>(new T()); }};
}

namespace scene_manager {

void run(SceneFactory first);
void goto_scene(SceneFactory next);
void push(SceneFactory next);
void pop();
void exit_game();
bool is_scene_changing();
bool is_current_scene_busy();
bool is_current_scene_started();
bool is_next_scene(const char* name);
bool is_previous_scene(const char* name);
const char* previous_scene_name();
Scene* scene();
void clear_stack();
void snap_for_background();
g2d::BitmapPtr background_bitmap();
g2d::BitmapPtr snap();
void register_scene(const char* name, SceneFactory f);
SceneFactory factory(const char* name);
std::vector<std::string> registered_names();
void stop();
uint64_t frame_count();
void set_frame_count(uint64_t f);
bool meet_fps_condition();

}

}
