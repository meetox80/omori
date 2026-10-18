#include "scenes.h"

namespace ui {

SceneRegistrar::SceneRegistrar(const char* name, rpg::SceneFactory factory) { rpg::scene_manager::register_scene(name, std::move(factory)); }

}
