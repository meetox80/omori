#include "scene_map.h"

#include <algorithm>
#include <cmath>

#include "../platform/platform.h"
#include "../rpg/audio_manager.h"
#include "../rpg/battle.h"
#include "../rpg/character.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/hooks.h"
#include "../rpg/input.h"
#include "../rpg/interpreter.h"
#include "../rpg/map.h"
#include "../rpg/params.h"
#include "../rpg/perf.h"
#include "../rpg/party.h"
#include "../rpg/screen.h"
#include "../rpg/state.h"
#include "../rpg/system.h"
#include "display_js.h"
#include "map_effects.h"
#include "menu_common.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {

namespace {

SceneMap* g_map_scene = nullptr;

double skip_param(const char* key, double def) {
    std::string s = data::plugin_param("Olivia_SkipCutscene", key, "");
    if (s.empty()) return def;
    double v = rpg::js_number(s);
    return v == v ? v : def;
}

bool skipping_cutscene() {
    js::Val* v = rpg::game_temp().expando.find(js::atom("_skippingCutscene"));
    return v && v->truthy();
}

bool can_skip_cutscene() {
    js::Val* v = rpg::game_temp().expando.find(js::atom("_canSkipCutscene"));
    return v && v->truthy();
}

}

class WindowSkipCutscene : public WindowBase {
public:
    void initialize_window() {
        WindowBase::initialize(text_padding() * 4, 0, 640, fitting_height(1));
        std::string py = data::plugin_param("Olivia_SkipCutscene", "Gauge Position Y", "auto");
        if (py == "auto") y = 480 - 240;
        else y = rpg::js_number(py);
        set_window_opacity(0);
        set_contents_opacity(0);
        refresh();
    }
    int standard_padding() override { return 0; }
    std::string message() { return data::plugin_param("Olivia_SkipCutscene", "Skip Text", ""); }
    void create_child_sprite() {
        double width = draw_text_ex(message(), 0, c().height());
        width += text_padding() * 4;
        double height = line_height();
        gauge_rate_ = std::make_shared<g2d::Sprite>();
        auto rb = g2d::Bitmap::create((int)(width - 2), (int)(height - 2));
        rb->gradient_fill_rect(0, 0, width - 2, height - 2, data::plugin_param("Olivia_SkipCutscene", "Gauge Color 1", "#8781bd"),
                               data::plugin_param("Olivia_SkipCutscene", "Gauge Color 2", "#bd8cbf"), false);
        gauge_rate_->set_bitmap(rb);
        gauge_rate_->x = 1;
        gauge_rate_->y = 1;
        add_child_to_back(gauge_rate_);
        gauge_back_ = std::make_shared<g2d::Sprite>();
        auto bb = g2d::Bitmap::create((int)width, (int)height);
        bb->fill_rect(0, 0, width, height, gauge_back_color());
        gauge_back_->set_bitmap(bb);
        add_child_to_back(gauge_back_);
        std::string px = data::plugin_param("Olivia_SkipCutscene", "Gauge Position X", "auto");
        if (px == "auto") x = std::round((640 - width) / 2);
        else x = rpg::js_number(px);
    }
    void refresh() {
        draw_text_ex(message(), text_padding() * 2, 0);
        create_child_sprite();
    }
    void update() override {
        WindowBase::update();
        update_opacity();
        if (gauge_rate_) update_gauge_sprites();
    }
    void update_opacity() {
        double speed = skip_param("Fade Speed", 24);
        SceneMap* s = current_map_scene();
        double change = (can_skip_cutscene() && s && s->skip_cutscene_duration) ? speed : -speed;
        set_contents_opacity(contents_opacity() + change);
    }
    void update_gauge_sprites() {
        gauge_rate_->set_opacity(contents_opacity());
        gauge_back_->set_opacity(contents_opacity());
        double rate;
        if (skipping_cutscene()) {
            rate = 1.0;
        } else {
            SceneMap* s = current_map_scene();
            rate = (s ? s->skip_cutscene_duration : 0) / skip_param("Hold Duration", 180);
        }
        double w = rate * gauge_rate_->bitmap()->width();
        gauge_rate_->set_frame(0, 0, w, gauge_rate_->height());
    }

private:
    std::shared_ptr<g2d::Sprite> gauge_rate_, gauge_back_;
};

SceneMap* current_map_scene() { return g_map_scene; }

int map_encounter_effect_duration() {
    rpg::Scene* s = rpg::scene_manager::scene();
    SceneMap* m = dynamic_cast<SceneMap*>(s);
    return m ? m->encounter_effect_duration : 0;
}

SceneMap::SceneMap() {}

SceneMap::~SceneMap() {
    if (g_map_scene == this) g_map_scene = nullptr;
    if (message_window) forget_message_window(message_window.get());
}

void SceneMap::create() {
    g_map_scene = this;
    rpg::GamePlayer& p = rpg::game_player();
    transfer = p.is_transferring();
    int map_id = transfer ? p.new_map_id : rpg::game_map().map_id();
    rpg::game_map().load_map_data(map_id);
}

bool SceneMap::is_ready() {
    if (!map_loaded) {
        on_map_loaded();
        map_loaded = true;
    }
    return map_loaded;
}

void SceneMap::on_map_loaded() {
    if (transfer) rpg::game_player().perform_transfer();
    create_display_objects();
    rpg::game_screen().clear_zoom();
    rpg::GameMap& m = rpg::game_map();
    if (!m.cam_norm) {
        if (!m.has_saved_cam) {
            m.has_saved_cam = true;
            m.saved_cam_args = {"PLAYER"};
        }
        m.cam_control(m.saved_cam_args);
    }
}

void SceneMap::create_display_objects() {
    create_spriteset();
    create_map_name_window();
    create_window_layer();
    create_all_windows();
    if (spriteset->timer_sprite) add_child(spriteset->timer_sprite);
    extras->create_qte_windows();
    extras->create_map_character_tag();
}

void SceneMap::create_spriteset() {
    spriteset = std::make_shared<SpritesetMap>();
    spriteset->initialize();
    add_child(spriteset);
}

void SceneMap::create_map_name_window() {
    map_name_window = ui::create_map_name_window();
    add_child(map_name_window);
}

void SceneMap::create_all_windows() {
    extras.reset(new MapExtras(*this));
    create_message_window();
    create_scroll_text_window();
    extras->create_name_input_windows();
    extras->create_pizza_note();
    skip_cutscene_window = std::shared_ptr<WindowSkipCutscene>(new WindowSkipCutscene());
    skip_cutscene_window->initialize_window();
    add_child(skip_cutscene_window);
}

void SceneMap::create_message_window() {
    message_window = ui::create_message_window(*this);
    long index = index_of(window_layer.get());
    auto container = message_face_box_container(message_window.get());
    if (container && index >= 0) add_child_at(container, (size_t)std::max(0L, index - 1));
}

void SceneMap::create_scroll_text_window() { scroll_text_window = ui::create_scroll_text_window(*this); }

void SceneMap::start() {
    rpg::Scene::start();
    rpg::scene_manager::clear_stack();
    if (rpg::scene_manager::is_previous_scene("Scene_OmoriFile")) {
        start_fade_in(fade_speed(), false);
    } else if (transfer) {
        fade_in_for_transfer();
        map_name_window_open(map_name_window.get());
        rpg::game_map().autoplay();
    } else if (needs_fade_in()) {
        start_fade_in(fade_speed(), false);
    }
    menu_calling = false;
    rpg::game_party().refresh_members();
}

void SceneMap::update() {
    int debug_troop = 0;
    if (plat::debug_battle(debug_troop)) {
        rpg::battle::setup(debug_troop, true, true);
        rpg::game_player().make_encounter_count();
        rpg::scene_manager::push(battle_scene());
    }
    update_destination();
    update_main_multiply();
    if (is_scene_change_ok()) {
        update_scene();
    } else if (rpg::scene_manager::is_next_scene("Scene_Battle")) {
        update_encounter_effect();
    }
    update_wait_count();
    rpg::Scene::update();
    extras->update_character_tag_input();
}

void SceneMap::update_destination() {
    if (!(is_active() && rpg::game_player().can_move())) rpg::game_temp().clear_destination();
}

void SceneMap::update_main_multiply() {
    update_main();
    if (is_fast_forward()) update_main();
    if (can_skip_cutscene()) update_skip_cutscene();
}

void SceneMap::update_main() {
    bool active = is_active();
    rpg::game_map().update(active);
    {
        rpg::perf::Scope ps(rpg::perf::Player);
        rpg::game_player().update_scene(active);
    }
    rpg::game_timer().update(active);
    {
        rpg::perf::Scope ps(rpg::perf::Screen);
        rpg::game_screen().update();
    }
    rpg::GameMap& m = rpg::game_map();
    double w = m.tile_width() * 16777216.0;
    double h = m.tile_height() * 16777216.0;
    m.display_x_ = std::round(m.display_x_ * w) / w;
    m.display_y_ = std::round(m.display_y_ * h) / h;
    rpg::perf::Scope ps(rpg::perf::Filters);
    update_tkm_filters();
}

bool SceneMap::is_fast_forward() {
    if (rpg::tddp_ff::is_route_ff_disabled()) return false;
    return rpg::game_map().is_event_running() && !rpg::scene_manager::is_scene_changing() && input::is_long_pressed(input::OK);
}

void SceneMap::stop() {
    rpg::Scene::stop();
    rpg::game_player().straighten();
    map_name_window_close(map_name_window.get());
    if (needs_slow_fade_out()) {
        start_fade_out(slow_fade_speed(), false);
    } else if (rpg::scene_manager::is_next_scene("Scene_Map")) {
        fade_out_for_transfer();
    } else if (rpg::scene_manager::is_next_scene("Scene_Battle")) {
        launch_battle();
    }
}

bool SceneMap::is_busy() {
    return (message_window && message_window_is_closing(message_window.get())) || wait_count > 0 || encounter_effect_duration > 0 ||
           rpg::Scene::is_busy();
}

void SceneMap::terminate() {
    if (extras) extras->terminate();
    rpg::Scene::terminate();
    release_display_proxies();
    if (!rpg::scene_manager::is_next_scene("Scene_Battle")) {
        spriteset->update();
        map_name_window->hide();
        rpg::scene_manager::snap_for_background();
    }
    rpg::game_screen().clear_zoom();
    remove_children();
    if (rpg::scene_manager::is_next_scene("Scene_Map")) rpg::game_switches().set_value(950, false);
    rpg::game_map().init_footprints();
    if (spriteset && spriteset->footprints_container) spriteset->footprints_container->remove_all();
}

bool SceneMap::needs_fade_in() {
    if (rpg::scene_manager::is_previous_scene("Scene_Battle") && rpg::game_troop().troop_id == 445) return false;
    return rpg::scene_manager::is_previous_scene("Scene_Battle") || rpg::scene_manager::is_previous_scene("Scene_Load") ||
           rpg::scene_manager::is_previous_scene("Scene_OmoriBlackLetterMap") || rpg::scene_manager::is_previous_scene("Scene_OmoriPhotoAlbum") ||
           rpg::scene_manager::is_previous_scene("Scene_OmoBlackLetterMenu");
}

bool SceneMap::needs_slow_fade_out() {
    return rpg::scene_manager::is_next_scene("Scene_Title") || rpg::scene_manager::is_next_scene("Scene_Gameover");
}

bool SceneMap::update_wait_count() {
    if (wait_count > 0) {
        wait_count--;
        return true;
    }
    return false;
}

bool SceneMap::is_scene_change_ok() { return is_active() && !rpg::game_message().is_busy(); }

void SceneMap::update_scene() {
    check_gameover();
    if (!rpg::scene_manager::is_scene_changing()) update_transfer_player();
    if (!rpg::scene_manager::is_scene_changing()) update_encounter();
    if (!rpg::scene_manager::is_scene_changing()) update_call_menu();
    if (!rpg::scene_manager::is_scene_changing()) update_map_menu_calls();
}

void SceneMap::check_gameover() {
    rpg::GameActor* omori = rpg::game_actors().actor(1);
    bool dead = omori && (omori->is_dead() || omori->hp() <= 0);
    if (!rpg::party_in_battle() && dead && omori->index() > -1) {
        rpg::scene_manager::goto_scene(gameover_scene());
        return;
    }
    if (rpg::game_party().is_all_dead()) rpg::scene_manager::goto_scene(gameover_scene());
}

void SceneMap::update_transfer_player() {
    if (rpg::game_player().is_transferring()) rpg::scene_manager::goto_scene(map_scene());
}

void SceneMap::update_encounter() {
    if (rpg::game_player().execute_encounter()) rpg::scene_manager::push(battle_scene());
}

void SceneMap::update_call_menu() {
    if (extras->update_call_menu_blocked()) return;
    base_update_call_menu();
}

void SceneMap::base_update_call_menu() {
    if (is_menu_enabled()) {
        if (is_menu_called()) menu_calling = true;
        if (menu_calling && !rpg::game_player().is_moving()) call_menu();
    } else {
        menu_calling = false;
    }
}

bool SceneMap::is_menu_enabled() { return rpg::game_system().menu_enabled && !rpg::game_map().is_event_running(); }

bool SceneMap::is_menu_called() { return input::is_triggered(input::MENU); }

void SceneMap::call_menu() {
    play_ok();
    rpg::scene_manager::push(menu_scene());
    menu::WindowMenuCommand::init_command_position();
    rpg::game_temp().clear_destination();
    map_name_window->hide();
    wait_count = 2;
}

void SceneMap::fade_in_for_transfer() {
    int t = rpg::game_player().fade_type;
    if (t == 0 || t == 1) start_fade_in(fade_speed(), t == 1);
}

void SceneMap::fade_out_for_transfer() {
    int t = rpg::game_player().fade_type;
    if (t == 0 || t == 1) start_fade_out(fade_speed(), t == 1);
}

void SceneMap::launch_battle() {
    rpg::battle::save_bgm_and_bgs();
    stop_audio_on_battle_start();
    rpg::audio_manager::play_system_sound(7);
    start_encounter_effect();
    map_name_window->hide();
}

void SceneMap::stop_audio_on_battle_start() {
    rpg::AudioObj battle = rpg::game_system().battle_bgm();
    rpg::AudioObj cur = rpg::audio_manager::current_bgm();
    if (!(cur.valid && cur.name == battle.name)) rpg::audio_manager::stop_bgm();
    rpg::audio_manager::stop_bgs();
    rpg::audio_manager::stop_me();
    rpg::audio_manager::stop_se();
}

void SceneMap::start_encounter_effect() { encounter_effect_duration = encounter_effect_speed(); }

void SceneMap::update_encounter_effect() {
    if (encounter_effect_duration > 0) {
        encounter_effect_duration--;
        int speed = encounter_effect_speed();
        int n = speed - encounter_effect_duration;
        if (n == speed / 2) {
            rpg::battle::play_battle_bgm();
            if (!rpg::game_troop().is_white_transition_troop()) start_fade_out(fade_speed(), false);
        }
    }
}

int SceneMap::encounter_effect_speed() { return rpg::battle::st().show_encounter_effect ? 60 : 0; }

void SceneMap::refresh_all_mini_labels() {
    if (spriteset) spriteset->refresh_mini_labels();
}

void SceneMap::update_map_menu_calls() {
    if (!is_menu_enabled()) return;
    if (input::is_triggered(input::PAGEUP)) {
        switch (rpg::current_world_index()) {
        case 1:
            if (rpg::game_switches().value(1210)) rpg::scene_manager::push(black_letter_menu_scene(false));
            break;
        case 2:
        case 3:
            if (rpg::game_switches().value(1210)) rpg::scene_manager::push(photo_album_scene(914, 1));
            break;
        }
    }
    if (input::is_triggered(input::PAGEDOWN)) {
        switch (rpg::current_world_index()) {
        case 1:
            if (rpg::game_switches().value(1211)) rpg::scene_manager::push(black_letter_map_scene());
            break;
        case 2: break;
        case 3:
            if (rpg::game_switches().value(1210)) rpg::scene_manager::push(photo_album_scene(889, 1));
            break;
        }
    }
}

bool SceneMap::is_skip_cutscene() {
    return rpg::game_map().is_event_running() && !rpg::scene_manager::is_scene_changing() && input::is_long_pressed(input::CANCEL);
}

void SceneMap::update_skip_cutscene() {
    if (is_skip_cutscene()) {
        skip_cutscene_duration += skip_param("Filling Speed", 7);
        if (skip_cutscene_duration >= skip_param("Hold Duration", 180)) process_skip_cutscene();
    } else {
        skip_cutscene_duration = std::max(0.0, skip_cutscene_duration - skip_param("Emptying Speed", 4));
    }
}

void SceneMap::process_skip_cutscene() {
    rpg::game_switches().set_value(40, true);
    if (rpg::game_map().interpreter) rpg::game_map().interpreter->process_skip_cutscene();
}

void SceneMap::update_tkm_filters() {
    if (!spriteset || rpg::game_map().filter_con_order.empty()) return;
    tkm_filters.update(spriteset->tkm_targets(this), false);
}

void SceneMap::remove_filter_controller(rpg::FilterController* f) {
    if (!spriteset) return;
    tkm_filters.remove(f, spriteset->tkm_targets(this));
}

bool SceneMap::js_get(uint32_t key, js::Val& out) {
    if (key == js::atom("_skipCutsceneDuration")) {
        out = js::Val::num(skip_cutscene_duration);
        return true;
    }
    if (key == js::atom("_encounterEffectDuration")) {
        out = js::Val::num(encounter_effect_duration);
        return true;
    }
    if (key == js::atom("_waitCount")) {
        out = js::Val::num(wait_count);
        return true;
    }
    if (key == js::atom("_spriteset")) {
        out = spriteset ? display_js(spriteset.get()) : js::Val();
        return true;
    }
    if (key == js::atom("_messageWindow")) {
        js::Val o = js::make_object();
        WindowMessage* w = message_window.get();
        js::vm().put(o, js::atom("terminateMessage"), js::make_native([w](js::Vm&, const js::Val&, std::vector<js::Val>&) {
                         if (current_map_scene() && current_map_scene()->message_window.get() == w) message_window_terminate(w);
                         return js::Val();
                     }));
        out = o;
        return true;
    }
    if (extras && extras->js_get(key, out)) return true;
    if (key == js::atom("removeChild")) {
        out = js::make_native([](js::Vm&, const js::Val&, std::vector<js::Val>& a) { return js::arg(a, 0); });
        return true;
    }
    return false;
}

bool SceneMap::js_set(uint32_t key, const js::Val& v) {
    if (key == js::atom("_skipCutsceneDuration")) {
        skip_cutscene_duration = v.to_num();
        return true;
    }
    if (key == js::atom("_encounterEffectDuration")) {
        encounter_effect_duration = v.to_int();
        return true;
    }
    if (key == js::atom("_waitCount")) {
        wait_count = v.to_int();
        return true;
    }
    if (extras && extras->js_set(key, v)) return true;
    return false;
}

rpg::SceneFactory map_scene() { return rpg::scene_of<SceneMap>("Scene_Map"); }

static SceneRegistrar g_map_registrar("Scene_Map", map_scene());

}
