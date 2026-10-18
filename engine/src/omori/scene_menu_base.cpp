#include "scene_menu_base.h"

#include "../rpg/game.h"
#include "../rpg/party.h"
#include "scenes.h"

namespace ui {

void SceneMenuBase::create() {
    rpg::Scene::create();
    create_background();
    update_actor();
    create_window_layer();
}

void SceneMenuBase::update_actor() { actor_ = rpg::game_party().menu_actor(); }

void SceneMenuBase::create_background() {
    background_sprite = std::make_shared<g2d::Sprite>(rpg::scene_manager::background_bitmap());
    add_child(background_sprite);
}

void SceneMenuBase::set_background_opacity(double opacity) { background_sprite->set_opacity(opacity); }

void SceneMenuBase::create_help_window() {
    help_window = WindowBase::make<WindowHelp>(3);
    add_window(help_window);
}

void SceneMenuBase::next_actor() {
    rpg::game_party().make_menu_actor_next();
    update_actor();
    on_actor_change();
}

void SceneMenuBase::previous_actor() {
    rpg::game_party().make_menu_actor_previous();
    update_actor();
    on_actor_change();
}

void SceneMenuBase::check_gameover() {
    rpg::GameActor* omori = rpg::game_actors().actor(1);
    bool dead = omori->is_dead() || omori->hp() <= 0;
    if (!rpg::party_in_battle() && dead && omori->index() > -1) {
        rpg::scene_manager::goto_scene(gameover_scene());
        return;
    }
    if (rpg::game_party().is_all_dead()) rpg::scene_manager::goto_scene(gameover_scene());
}

void SceneMenuBase::terminate() {
    rpg::Scene::terminate();
    remove_children();
}

}
