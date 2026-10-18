#include <memory>

#include "../rpg/filter.h"
#include "../rpg/game.h"
#include "../rpg/hooks.h"
#include "../rpg/map.h"
#include "../rpg/scene.h"
#include "menu_common.h"
#include "scene_map.h"
#include "scenes.h"
#include "spriteset.h"
#include "spriteset_map.h"

namespace rpg {

namespace {

ui::SceneMap* map_scene_now() { return dynamic_cast<ui::SceneMap*>(scene_manager::scene()); }

ui::SpritesetMap* map_spriteset() {
    ui::SceneMap* s = map_scene_now();
    return s ? s->spriteset.get() : nullptr;
}

}

namespace scenes {

bool is_map() {
    Scene* s = scene_manager::scene();
    return s && std::string(s->name()) == "Scene_Map";
}

bool is_battle() {
    Scene* s = scene_manager::scene();
    return s && std::string(s->name()) == "Scene_Battle";
}

void goto_title() { scene_manager::goto_scene(ui::title_scene()); }
void goto_gameover() { scene_manager::goto_scene(ui::gameover_scene()); }
void push_menu() { scene_manager::push(ui::menu_scene()); }
void push_save() { scene_manager::push(ui::omori_file_scene(true, false)); }
void push_shop(const Value& goods, std::shared_ptr<const void> keep, bool purchase_only) {
    scene_manager::push(ui::item_shop_scene(&goods, std::move(keep), purchase_only));
}
void push_name_input(int actor_id, int max_chars) { scene_manager::push(ui::name_input_scene(actor_id, max_chars)); }
void push_battle() { scene_manager::push(ui::battle_scene()); }
void goto_battle() { scene_manager::goto_scene(ui::battle_scene()); }
void push_omori_file(bool save, bool load) { scene_manager::push(ui::omori_file_scene(save, load)); }
void push_photo_album(int item_id, int interface_mode) { scene_manager::push(ui::photo_album_scene(item_id, interface_mode)); }
void push_black_letter_menu(bool forced) { scene_manager::push(ui::black_letter_menu_scene(forced)); }
void push_quest_menu() { scene_manager::push(ui::quest_scene()); }
void push_slot_machine() { scene_manager::push(ui::slot_machine_scene()); }

void push_gacha() {
    SceneFactory f = ui::gacha_scene();
    if (f) scene_manager::push(std::move(f));
}

void push_gacha_book() { scene_manager::push(ui::gacha_book_scene()); }

bool is_input_window_active() {
    ui::SceneMap* s = map_scene_now();
    return s && s->extras && s->extras->is_input_window_active();
}

void show_name_input_windows(const std::string& name, int max) {
    ui::SceneMap* s = map_scene_now();
    if (s && s->extras) s->extras->show_name_input_windows(name, max);
}

void start_qte_smash(const js::Val& segments, const js::Val& time, const js::Val& width) {
    ui::SceneMap* s = map_scene_now();
    if (s && s->extras) s->extras->start_qte_smash(segments, time, width);
}

void start_qte_stop_the_arrow(const js::Val& direction, const js::Val& delay, const js::Val& x, const js::Val& y) {
    ui::SceneMap* s = map_scene_now();
    if (s && s->extras) s->extras->start_qte_stop_the_arrow(direction, delay, x, y);
}

void start_qte_hit_the_mark(const js::Val& speed, int index, const js::Val& x, const js::Val& y) {
    ui::SceneMap* s = map_scene_now();
    if (s && s->extras) s->extras->start_qte_hit_the_mark(speed, index, x, y);
}

void show_pizza_note(const std::string& text) {
    ui::SceneMap* s = map_scene_now();
    if (s && s->extras) s->extras->show_pizza_note(text);
}

void refresh_mini_labels() {
    ui::SceneMap* s = map_scene_now();
    if (s) s->refresh_all_mini_labels();
}

void reset_skip_cutscene_duration() {
    Scene* s = scene_manager::scene();
    if (!s) return;
    if (ui::SceneMap* m = dynamic_cast<ui::SceneMap*>(s)) m->skip_cutscene_duration = 0;
    else s->js_expando.set(js::atom("_skipCutsceneDuration"), js::Val::num(0));
}

void add_map_fog(const std::string& id) {
    ui::SpritesetMap* sp = map_spriteset();
    if (sp && sp->map_fog_container) sp->map_fog_container->add_fog(id);
}

void setup_ambience_bitmaps(const std::string& name) {
    ui::SpritesetMap* sp = map_spriteset();
    if (sp) sp->setup_ambience_bitmaps(name);
}

void update_picture_custom_frame(int picture_id) {
    ui::SpritesetBase* sp = ui::SpritesetBase::current();
    if (!sp) return;
    ui::SpritePicture* pic = sp->picture_sprite(picture_id);
    if (pic) pic->update_custom_frame();
}

void snap_for_shop() { ui::menu::snap_for_shop(); }

void remove_filter(FilterController* f) {
    Scene* s = scene_manager::scene();
    if (s) s->remove_filter_controller(f);
}

}

bool map_character_tag_active() {
    ui::SceneMap* s = map_scene_now();
    return s && s->extras && s->extras->character_tag_active();
}

bool map_pizza_note_visible() {
    ui::SceneMap* s = map_scene_now();
    return s && s->extras && s->extras->pizza_note_visible();
}

bool map_can_use_character_tag() {
    ui::SceneMap* s = map_scene_now();
    return s && s->extras && s->extras->can_use_character_tag();
}

void map_create_spawned_event(GameEvent* ev) {
    ui::SpritesetMap* sp = map_spriteset();
    if (sp && ev) sp->create_spawned_event(ev);
}

void scene_add_footprint(std::shared_ptr<GameFootprint> f) {
    ui::SceneMap* s = map_scene_now();
    if (!s) return;
    game_map().add_footprint(f);
    if (s->spriteset && s->spriteset->footprints_container) s->spriteset->footprints_container->add_footprint(std::move(f));
}

}
