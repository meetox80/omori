#include "scene.h"

#include "audio_manager.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include "../core/audio.h"
#include "../core/fs.h"
#include "../core/image.h"
#include "../omori/bottom_screen.h"
#include "../omori/video.h"
#include "../platform/audio_out.h"
#include "../platform/common/testing.h"
#include "../platform/gfx.h"
#include "../platform/platform.h"
#include "data.h"
#include "input.h"
#include "params.h"
#include "perf.h"
#include "bind_util.h"
#include "game.h"
#include "interpreter.h"
#include "map.h"
#include "timers.h"

namespace rpg {

namespace {

js::HostClass& scene_class() {
    static js::HostClass c;
    if (!c.name) {
        c.name = "Scene";
        c.get = [](js::Vm&, js::Obj* self, uint32_t key, js::Val& out) {
            Scene* s = static_cast<Scene*>(self->ptr);
            if (!s) return true;
            if (s->js_get(key, out)) return true;
            if (js::Val* v = s->js_expando.find(key)) out = *v;
            return true;
        };
        c.set = [](js::Vm&, js::Obj* self, uint32_t key, const js::Val& v) {
            Scene* s = static_cast<Scene*>(self->ptr);
            if (!s) return true;
            if (!s->js_set(key, v)) s->js_expando.set(key, v);
            return true;
        };
    }
    return c;
}

}

Scene::~Scene() {
    if (js_obj) {
        js_obj->ptr = nullptr;
        js::obj_release(js_obj);
        js_obj = nullptr;
    }
}

js::Val Scene::to_js() {
    if (!js_obj) {
        js::Val v = js::make_host(&scene_class(), this);
        js_obj = v.o;
        js::obj_retain(js_obj);
        return v;
    }
    return js::Val::obj(js_obj);
}

void Scene::update() {
    update_fade();
    update_children();
}

void Scene::update_children() {
    size_t length = children.size();
    for (size_t i = 0; i < length; ++i) {
        if (i >= children.size()) continue;
        g2d::DisplayPtr child = children[i];
        if (!child) continue;
        perf::Scope ps(child.get() == window_layer.get() ? perf::Windows : perf::Spriteset);
        child->update();
    }
}

void Scene::create_window_layer() {
    window_layer = std::make_shared<g2d::WindowLayer>();
    window_layer->move(0, 0, 640, 480);
    add_child(window_layer);
}

void Scene::add_window(g2d::DisplayPtr w) {
    if (window_layer) window_layer->add_child(std::move(w));
}

void Scene::create_fade_sprite(bool white) {
    if (!fade_sprite) {
        fade_sprite = std::make_shared<g2d::ScreenSprite>();
        add_child(fade_sprite);
    }
    if (white) fade_sprite->set_white();
    else fade_sprite->set_black();
}

void Scene::start_fade_in(int duration, bool white) {
    create_fade_sprite(white);
    fade_sign = 1;
    fade_duration_ = duration ? duration : 30;
    fade_sprite->set_opacity(255);
}

void Scene::start_fade_out(int duration, bool white) {
    create_fade_sprite(white);
    fade_sign = -1;
    fade_duration_ = duration ? duration : 30;
    fade_sprite->set_opacity(0);
}

void Scene::update_fade() {
    if (fade_duration_ > 0) {
        double d = fade_duration_;
        double op = fade_sprite->opacity();
        if (fade_sign > 0) op -= op / d;
        else op += (255 - op) / d;
        fade_sprite->set_opacity(op);
        fade_duration_--;
    }
}

void Scene::fade_out_all() {
    double time = slow_fade_speed() / 60.0;
    audio::fadeout_bgm(time);
    audio::fadeout_bgs(time);
    audio::fadeout_me(time);
    start_fade_out(slow_fade_speed());
}

void Scene::pop_scene() { scene_manager::pop(); }

namespace scene_manager {

static std::unique_ptr<Scene> g_scene;
static SceneFactory g_current_factory;
static SceneFactory g_next;
static std::vector<SceneFactory> g_stack;
static std::string g_previous;
static bool g_started = false;
static bool g_exiting = false;
static bool g_stopped = false;
static uint64_t g_frames = 0;
static g2d::BitmapPtr g_background;
static std::map<std::string, SceneFactory>& registry() {
    static std::map<std::string, SceneFactory> r;
    return r;
}
static std::string g_next_name;

Scene* scene() { return g_scene.get(); }
uint64_t frame_count() { return g_frames; }
void set_frame_count(uint64_t f) { g_frames = f; }

static double g_moment_fps = 60;
static double g_mean_fps[3] = {60, 60, 60};
static int g_mean_count = 0;
static double g_last_frame_time = -1;

static void record_fps() {
    double now = plat::time_seconds();
    if (g_last_frame_time >= 0 && now > g_last_frame_time) {
        g_moment_fps = 1.0 / (now - g_last_frame_time);
        int frames = (int)std::max(1.0, js_number(data::plugin_param("WaitFPS", "meanFPSFrames", "3")));
        if (frames > 3) frames = 3;
        g_mean_fps[g_mean_count % frames] = g_moment_fps;
        g_mean_count++;
    }
    g_last_frame_time = now;
}

bool meet_fps_condition() {
    int frames = (int)std::max(1.0, js_number(data::plugin_param("WaitFPS", "meanFPSFrames", "3")));
    if (frames > 3) frames = 3;
    int n = std::min(frames, std::max(1, g_mean_count));
    double mean = 0;
    for (int i = 0; i < n; i++) mean += g_mean_fps[i];
    mean /= n;
    double moment_threshold = js_number(data::plugin_param("WaitFPS", "momentFPSThreshold", "0"));
    double mean_threshold = js_number(data::plugin_param("WaitFPS", "meanFPSThreshold", "0"));
    return g_moment_fps > moment_threshold && mean > mean_threshold;
}

void register_scene(const char* name, SceneFactory f) { registry()[name] = std::move(f); }

std::vector<std::string> registered_names() {
    std::vector<std::string> out;
    for (auto& kv : registry()) out.push_back(kv.first);
    return out;
}

SceneFactory factory(const char* name) {
    auto it = registry().find(name);
    return it == registry().end() ? SceneFactory() : it->second;
}

bool is_scene_changing() { return g_exiting || (bool)g_next; }
bool is_current_scene_busy() { return g_scene && g_scene->is_busy(); }
bool is_current_scene_started() { return g_scene && g_started; }

bool is_next_scene(const char* name) { return g_next && g_next_name == name; }
bool is_previous_scene(const char* name) { return g_previous == name; }
const char* previous_scene_name() { return g_previous.c_str(); }

void goto_scene(SceneFactory next) {
    if (next) {
        g_next_name = next.name;
        g_next = std::move(next);
    }
    if (g_scene) g_scene->stop();
}

void push(SceneFactory next) {
    g_stack.push_back(g_current_factory);
    goto_scene(std::move(next));
}

void pop() {
    if (!g_stack.empty()) {
        SceneFactory f = g_stack.back();
        g_stack.pop_back();
        goto_scene(f);
    } else {
        exit_game();
    }
}

void clear_stack() { g_stack.clear(); }

void exit_game() {
    g_next = SceneFactory();
    g_exiting = true;
    if (g_scene) g_scene->stop();
}

void stop() { g_stopped = true; }

static void change_scene() {
    if (is_scene_changing() && !is_current_scene_busy()) {
        if (g_scene) {
            g_scene->terminate();
            g_previous = g_scene->name();
        }
        g_scene.reset();
        images::forget_recent();
        if (g_next) {
            g_current_factory = g_next;
            g_scene = g_next.make();
            g_next = SceneFactory();
            g_next_name.clear();
            g_started = false;
            if (g_scene) g_scene->create();
        }
        if (g_exiting) plat::request_exit();
    }
}

static void update_scene() {
    if (g_scene) {
        if (!g_started && g_scene->is_ready()) {
            g_scene->start();
            g_started = true;
        }
        if (is_current_scene_started()) g_scene->update();
    }
}

static g2d::RenderState root_state() {
    g2d::RenderState rs;
    rs.clip = true;
    rs.cx0 = (int)images::OFFSET_X;
    rs.cy0 = (int)images::OFFSET_Y;
    rs.cx1 = (int)images::OFFSET_X + (int)std::lround(640 * images::SCALE);
    rs.cy1 = (int)images::OFFSET_Y + (int)std::lround(480 * images::SCALE);
    return rs;
}

struct PerfStats {
    double update_sum = 0, update_max = 0, render_sum = 0, render_max = 0, frame_sum = 0, frame_max = 0, present_sum = 0, present_max = 0;
    uint64_t sections[perf::Count] = {};
    int frames = 0;
};

static PerfStats g_perf;

static void perf_add(double& sum, double& mx, double v) {
    sum += v;
    if (v > mx) mx = v;
}

static void perf_spike(const uint64_t* before, double seconds) {
    char line[320];
    size_t off = 0;
    for (int i = 0; i < perf::Count; i++) {
        uint64_t d = g_perf.sections[i] - before[i];
        if (d < 1000) continue;
        int w = std::snprintf(line + off, sizeof(line) - off, "%s%s %.2f", off ? ", " : "", perf::name(i), (double)d / 1000);
        if (w < 0 || (size_t)w >= sizeof(line) - off) break;
        off += (size_t)w;
    }
    line[off] = 0;
    plat::log("perf spike frame %llu: %.2f ms (%s)", (unsigned long long)g_frames, seconds * 1000, line);
}

static void perf_report() {
    if (g_perf.frames < 120) return;
    double n = g_perf.frames;
    plat::log("perf %s: update %.2f/%.2f ms, render %.2f/%.2f ms, present %.2f/%.2f ms, frame %.2f/%.2f ms, images %u KB, list peak %d KB",
              g_scene ? g_scene->name() : "-", g_perf.update_sum / n * 1000, g_perf.update_max * 1000, g_perf.render_sum / n * 1000,
              g_perf.render_max * 1000, g_perf.present_sum / n * 1000, g_perf.present_max * 1000,
              g_perf.frame_sum / n * 1000, g_perf.frame_max * 1000, (unsigned)(images::used_bytes() / 1024), gfx::list_peak_bytes() / 1024);
    char line[480];
    size_t off = 0;
    for (int i = 0; i < perf::Count; i++) {
        int w = std::snprintf(line + off, sizeof(line) - off, "%s%s %.2f", i ? ", " : "", perf::name(i), (double)g_perf.sections[i] / n / 1000);
        if (w < 0 || (size_t)w >= sizeof(line) - off) break;
        off += (size_t)w;
    }
    plat::log("perf sections: %s", line);
    plat::log("perf counts: objects %.1f, batches %.1f, heap used %u of %u KB, pixel arena free %u KB", g2d::take_render_count() / n,
              gfx::take_batch_count() / n, (unsigned)(plat::heap_used() / 1024), (unsigned)(plat::heap_limit() / 1024),
              (unsigned)(gfx::arena_free_bytes() / 1024));
    double mix_max = 0;
    int starved = 0;
    double mix_avg = audio::take_mix_stats(mix_max, starved);
    plat::log("perf audio: mix %.0f/%.0f us per %d frames, %d starved", mix_avg, mix_max, audio_out::BLOCK, starved);
    g_perf = PerfStats();
}

}

namespace perf {

static bool g_enabled = false;

bool enabled() { return g_enabled; }

void set_enabled(bool on) { g_enabled = on; }

void add(int section, uint64_t us) {
    if (section >= 0 && section < Count) scene_manager::g_perf.sections[section] += us;
}

const char* name(int section) {
    static const char* names[Count] = {"events", "player", "screen", "interp", "spriteset", "windows", "filters", "tilemap", "weather", "fog", "footprints", "load", "audio", "images", "timers"};
    return section >= 0 && section < Count ? names[section] : "?";
}

Scope::Scope(int section) : section_(section), start_(g_enabled ? plat::time_us() : 0) {}

Scope::~Scope() {
    if (g_enabled) add(section_, plat::time_us() - start_);
}

}

namespace scene_manager {

static void render_scene() {
    double tp = plat::time_seconds();
    gfx::present();
    double t0 = plat::time_seconds();
    if (plat::options().perf) perf_add(g_perf.present_sum, g_perf.present_max, t0 - tp);
    gfx::begin_frame(0xFF000000u);
    uint64_t frame = plat::frame_count();
    bool skip = frame < (uint64_t)std::max(0, plat::options().skip_render_until) && !testing::shot_due(frame);
    if (!skip && is_current_scene_started() && !ui::movie_covers_screen()) {
        g2d::RenderState rs = root_state();
        gfx::set_scissor(rs.cx0, rs.cy0, rs.cx1 - rs.cx0, rs.cy1 - rs.cy0);
        g_scene->render(rs);
        gfx::reset_scissor();
    }
    ui::render_movie_overlay();
    ui::bottom::render();
    if (plat::options().perf) {
        double r = plat::time_seconds() - t0;
        perf_add(g_perf.render_sum, g_perf.render_max, r);
    }
    plat::frame_end();
}

g2d::BitmapPtr snap() {
    g2d::BitmapPtr b = g2d::Bitmap::create(640, 480);
    if (g_scene) {
        gfx::begin_frame(0xFF000000u);
        g2d::RenderState rs = root_state();
        gfx::set_scissor(rs.cx0, rs.cy0, rs.cx1 - rs.cx0, rs.cy1 - rs.cy0);
        g_scene->render(rs);
        gfx::reset_scissor();
        gfx::end_frame();
    }
    b->fill_rows([](int y, uint32_t* row, int w) {
        if (!gfx::capture_row(y + (int)images::OFFSET_Y, (int)images::OFFSET_X, w, row)) {
            std::memset(row, 0, (size_t)w * 4);
            return;
        }
        for (int x = 0; x < w; x++) row[x] |= 0xFF000000u;
    });
    return b;
}

void snap_for_background() {
    g_background = snap();
    g_background->blur();
}

g2d::BitmapPtr background_bitmap() { return g_background; }

void run(SceneFactory first) {
    perf::set_enabled(plat::options().perf);
    goto_scene(std::move(first));
    while (plat::running() && !g_stopped) {
        plat::poll();
        if (plat::consume_resume()) {
            fs::drop_idle_handles();
            audio::on_resume();
        }
        record_fps();
        int ticks = plat::pending_ticks();
        double frame_start = plat::time_seconds();
        for (int i = 0; i < ticks && plat::running(); i++) {
            double u0 = plat::time_seconds();
            uint64_t before[perf::Count];
            std::memcpy(before, g_perf.sections, sizeof(before));
            input::update();
            bind::purge_held();
            audio_manager::clear_unique_check_se();
            {
                perf::Scope ps(perf::Timers);
                timers::run();
            }
            {
                perf::Scope ps(perf::Load);
                change_scene();
            }
            update_scene();
            ui::bottom::update();
            if (plat::debug_reset() && game_objects_exist()) {
                rpg::GameScreen& screen = game_screen();
                screen.clear_pictures();
                screen.clear_weather();
                screen.clear_tone();
                screen.clear_flash();
                screen.clear_shake();
                screen.clear_zoom();
                game_map().filter_con.clear();
                game_map().filter_con_order.clear();
            }
            std::string debug_src;
            if (plat::debug_eval(debug_src) && game_objects_exist()) game_map().interpreter->run_script(nullptr, debug_src, "debug eval", nullptr);
            int warp_map = 0, warp_x = 0, warp_y = 0, warp_d = 2;
            if (plat::debug_warp(warp_map, warp_x, warp_y, warp_d) && game_objects_exist()) {
                game_player().reserve_transfer(warp_map, warp_x, warp_y, warp_d, 0);
                if (!g_scene || std::strcmp(g_scene->name(), "Scene_Map") != 0) goto_scene(factory("Scene_Map"));
            }
            flush_retired_interpreters();
            ui::update_videos();
            {
                perf::Scope ps(perf::Audio);
                audio_manager::update();
            }
            {
                perf::Scope ps(perf::Images);
                images::tick();
            }
            g_frames++;
            if (plat::options().perf) {
                double du = plat::time_seconds() - u0;
                perf_add(g_perf.update_sum, g_perf.update_max, du);
                if (du > 0.05) perf_spike(before, du);
            }
        }
        render_scene();
        if (plat::options().perf) {
            perf_add(g_perf.frame_sum, g_perf.frame_max, plat::time_seconds() - frame_start);
            g_perf.frames++;
            perf_report();
        }
    }
}

}

}
