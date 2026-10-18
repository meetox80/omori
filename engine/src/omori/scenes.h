#pragma once
#include <memory>
#include <string>

#include "../core/value.h"
#include "../rpg/scene.h"

namespace ui {

struct SceneRegistrar {
    SceneRegistrar(const char* name, rpg::SceneFactory factory);
};

rpg::SceneFactory boot_scene();
rpg::SceneFactory splash_scene();
rpg::SceneFactory title_scene();
rpg::SceneFactory title_scene_prepared(int command_index);
rpg::SceneFactory map_scene();
rpg::SceneFactory battle_scene();
rpg::SceneFactory gameover_scene();

rpg::SceneFactory menu_scene();
rpg::SceneFactory omori_file_scene(bool save, bool load);
rpg::SceneFactory photo_album_scene(int item_id, int interface_mode);
rpg::SceneFactory black_letter_menu_scene(bool forced);
rpg::SceneFactory quest_scene();
rpg::SceneFactory item_shop_scene(const Value* goods, std::shared_ptr<const void> keep, bool purchase_only);
rpg::SceneFactory bestiary_scene();

rpg::SceneFactory slot_machine_scene();
rpg::SceneFactory gacha_scene();
rpg::SceneFactory gacha_book_scene();
rpg::SceneFactory space_invader_scene();
rpg::SceneFactory blackjack_scene();
rpg::SceneFactory snake_scene();
rpg::SceneFactory pet_rocks_scene();
rpg::SceneFactory sketchbook_scene();
rpg::SceneFactory black_letter_map_scene();

rpg::SceneFactory name_input_scene(int actor_id, int max_chars);

}
