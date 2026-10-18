#pragma once
#include <memory>

#include "../rpg/scene.h"
#include "fx.h"
#include "map_extras.h"
#include "message.h"
#include "spriteset_map.h"
#include "window_base.h"

namespace ui {

class WindowSkipCutscene;

class SceneMap : public rpg::Scene {
public:
    SceneMap();
    ~SceneMap() override;
    const char* name() const override { return "Scene_Map"; }
    void create() override;
    bool is_ready() override;
    void on_map_loaded();
    void start() override;
    void update() override;
    void update_destination();
    void update_main_multiply();
    void update_main();
    bool is_fast_forward();
    void stop() override;
    bool is_busy() override;
    void terminate() override;
    bool needs_fade_in();
    bool needs_slow_fade_out();
    bool update_wait_count();
    bool is_scene_change_ok();
    void update_scene();
    void check_gameover();
    void create_display_objects();
    void create_spriteset();
    void create_map_name_window();
    void create_all_windows();
    void create_message_window();
    void create_scroll_text_window();
    void update_transfer_player();
    void update_encounter();
    void update_call_menu();
    void base_update_call_menu();
    bool is_menu_enabled();
    bool is_menu_called();
    void call_menu();
    void fade_in_for_transfer();
    void fade_out_for_transfer();
    void launch_battle();
    void stop_audio_on_battle_start();
    void start_encounter_effect();
    void update_encounter_effect();
    int encounter_effect_speed();
    void refresh_all_mini_labels();
    void update_map_menu_calls();
    bool is_skip_cutscene();
    void update_skip_cutscene();
    void process_skip_cutscene();
    void update_tkm_filters();
    void remove_filter_controller(rpg::FilterController* f) override;
    bool js_get(uint32_t key, js::Val& out) override;
    bool js_set(uint32_t key, const js::Val& v) override;

    int wait_count = 0;
    int encounter_effect_duration = 0;
    bool map_loaded = false;
    bool transfer = false;
    bool menu_calling = false;
    double skip_cutscene_duration = 0;
    std::shared_ptr<SpritesetMap> spriteset;
    std::shared_ptr<WindowBase> map_name_window;
    std::shared_ptr<WindowMessage> message_window;
    std::shared_ptr<WindowScrollText> scroll_text_window;
    std::shared_ptr<WindowSkipCutscene> skip_cutscene_window;
    std::unique_ptr<MapExtras> extras;
    fx::TkmFilters tkm_filters;
};

SceneMap* current_map_scene();

}
