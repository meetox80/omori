#pragma once
#include <memory>
#include <string>

#include "../js/value.h"
#include "../rpg/scene.h"

namespace ui {

class MapExtras {
public:
    explicit MapExtras(rpg::Scene& scene);
    ~MapExtras();
    void create_qte_windows();
    void create_map_character_tag();
    void create_name_input_windows();
    void create_pizza_note();
    bool can_use_character_tag();
    void update_character_tag_input();
    bool character_tag_active();
    void terminate();
    void show_name_input_windows(const std::string& name, int max);
    void hide_name_input_windows();
    bool is_input_window_active();
    void start_qte_smash(const js::Val& segments, const js::Val& time, const js::Val& width);
    void start_qte_stop_the_arrow(const js::Val& direction, const js::Val& delay, const js::Val& x, const js::Val& y);
    void start_qte_hit_the_mark(const js::Val& speed, int index, const js::Val& x, const js::Val& y);
    void show_pizza_note(const std::string& text);
    bool pizza_note_visible();
    bool update_call_menu_blocked();
    bool js_get(uint32_t key, js::Val& out);
    bool js_set(uint32_t key, const js::Val& v);
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}
