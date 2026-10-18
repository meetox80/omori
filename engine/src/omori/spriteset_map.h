#pragma once
#include <memory>
#include <vector>

#include "display_js.h"
#include "fx.h"
#include "map_effects.h"
#include "map_sprites.h"
#include "spriteset.h"
#include "tilemap.h"

namespace rpg {
class GameEvent;
}

namespace ui {

class SpritesetMap : public SpritesetBase, public DisplayJsExtra {
public:
    SpritesetMap();
    ~SpritesetMap() override;
    void create_lower_layer() override;
    void create_parallax();
    void create_tilemap();
    void create_characters();
    void create_shadow();
    void create_weather();
    void create_map_fog_container();
    void create_footprints_layer();
    void create_pictures() override;
    void create_screen_sprites() override;
    void create_light_ambience_overlay_sprite();
    void setup_ambience_bitmaps(const std::string& name);
    void hide_light_ambience_overlay() { ambience_overlay->visible = false; }
    void show_light_ambience_overlay() { ambience_overlay->visible = true; }
    void update() override;
    void update_parallax();
    void update_tilemap();
    void update_shadow();
    void update_weather();
    void update_hide_on_level();
    void update_reflect_surface();
    void update_light_ambience_overlay();
    void hide_characters();
    void show_characters();
    void create_spawned_event(rpg::GameEvent* ev);
    void refresh_mini_labels();
    fx::TkmTargets tkm_targets(g2d::Container* scene);
    const js::Val* horror_filters_source() override;
    bool display_js_get(uint32_t key, js::Val& out) override;

    std::shared_ptr<g2d::TilingSprite> parallax;
    std::string parallax_name;
    bool parallax_name_set = false;
    std::shared_ptr<TiledTilemap> tilemap;
    std::shared_ptr<g2d::Sprite> reflect_surface_sprite;
    std::vector<std::shared_ptr<SpriteCharacter>> character_sprites;
    std::shared_ptr<g2d::Sprite> shadow_sprite;
    std::shared_ptr<WeatherLayer> weather;
    std::shared_ptr<MapFogContainer> map_fog_container;
    std::shared_ptr<FootprintsContainer> footprints_container;
    std::shared_ptr<g2d::Sprite> ambience_overlay;
    std::shared_ptr<g2d::Sprite> ambience_padding1;
    std::shared_ptr<g2d::Sprite> ambience_padding2;
};

}
