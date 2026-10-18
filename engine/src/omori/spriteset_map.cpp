#include "spriteset_map.h"

#include <cmath>

#include "../rpg/character.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/map.h"
#include "../rpg/params.h"
#include "../rpg/screen.h"
#include "../rpg/state.h"
#include "ui_util.h"

namespace ui {

SpritesetMap::SpritesetMap() {}

SpritesetMap::~SpritesetMap() {
    for (auto& s : character_sprites) s->detach_attachments();
    if (SpriteCharacter::label_layer == this) SpriteCharacter::label_layer = nullptr;
}

void SpritesetMap::create_lower_layer() {
    SpriteCharacter::label_layer = this;
    SpritesetBase::create_lower_layer();
    create_parallax();
    create_tilemap();
    create_characters();
    create_shadow();
    create_weather();
    create_map_fog_container();
    create_footprints_layer();
}

void SpritesetMap::create_parallax() {
    parallax = std::make_shared<g2d::TilingSprite>();
    parallax->move(0, 0, 640, 480);
    base_sprite->add_child(parallax);
}

void SpritesetMap::create_tilemap() {
    tilemap = std::make_shared<TiledTilemap>();
    tilemap->setup();
    rpg::GameMap& m = rpg::game_map();
    tilemap->horizontal_wrap = m.is_loop_horizontal();
    tilemap->vertical_wrap = m.is_loop_vertical();
    base_sprite->add_child(tilemap);
    if (m.is_tiled_map()) {
        reflect_surface_sprite = std::make_shared<g2d::Sprite>();
        add_child(reflect_surface_sprite);
    }
}

void SpritesetMap::create_characters() {
    rpg::GameMap& m = rpg::game_map();
    rpg::GamePlayer& p = rpg::game_player();
    std::vector<rpg::GameCharacterBase*> order;
    for (rpg::GameEvent* e : m.events()) order.push_back(e);
    for (auto& v : m.vehicles_) order.push_back(v.get());
    auto& followers = p.followers().data;
    for (size_t i = followers.size(); i-- > 0;) order.push_back(followers[i].get());
    order.push_back(&p);
    for (rpg::GameCharacterBase* c : order) {
        if (c) character_sprites.push_back(std::make_shared<SpriteCharacter>(c));
    }
    for (auto& s : character_sprites) tilemap->add_child(s);
    if (m.is_tiled_map()) {
        for (rpg::GameCharacterBase* c : order) {
            if (!c) continue;
            auto s = std::make_shared<SpriteCharacterReflect>(c);
            character_sprites.push_back(s);
            tilemap->add_child(s);
        }
    }
}

void SpritesetMap::create_shadow() {
    shadow_sprite = std::make_shared<g2d::Sprite>(load_system("Shadow1"));
    shadow_sprite->anchor_x = 0.5;
    shadow_sprite->anchor_y = 1;
    shadow_sprite->z = 6;
    tilemap->add_child(shadow_sprite);
}

void SpritesetMap::create_weather() {
    weather = std::make_shared<WeatherLayer>();
    add_child(weather);
}

void SpritesetMap::create_map_fog_container() {
    map_fog_container = std::make_shared<MapFogContainer>();
    add_child(map_fog_container);
}

void SpritesetMap::create_footprints_layer() {
    footprints_container = std::make_shared<FootprintsContainer>();
    footprints_container->z = 3;
    tilemap->add_child(footprints_container);
}

void SpritesetMap::create_pictures() {
    SpritesetBase::create_pictures();
    if (tilemap) {
        int pic_id = (int)rpg::js_parse_int(data::plugin_param("KEN_PictureBelowChars", "Picture ID", "1"));
        double pic_z = rpg::js_parse_int(data::plugin_param("KEN_PictureBelowChars", "Picture Z", "2"));
        move_picture_below(pic_id, tilemap.get(), pic_z);
    }
}

void SpritesetMap::create_screen_sprites() {
    create_light_ambience_overlay_sprite();
    SpritesetBase::create_screen_sprites();
}

void SpritesetMap::create_light_ambience_overlay_sprite() {
    ambience_overlay = std::make_shared<g2d::Sprite>();
    ambience_overlay->anchor_x = 0.5;
    ambience_overlay->anchor_y = 0.5;
    add_child(ambience_overlay);
    ambience_padding1 = std::make_shared<g2d::Sprite>();
    ambience_padding1->anchor_x = 0.5;
    ambience_padding1->anchor_y = 2;
    ambience_overlay->add_child(ambience_padding1);
    ambience_padding2 = std::make_shared<g2d::Sprite>();
    ambience_padding2->anchor_x = -1;
    ambience_padding2->anchor_y = 0.5;
    ambience_overlay->add_child(ambience_padding2);
    const std::string& name = rpg::game_player().ambience.name;
    if (!name.empty()) setup_ambience_bitmaps(name);
}

void SpritesetMap::setup_ambience_bitmaps(const std::string& name) {
    if (!name.empty()) {
        ambience_overlay->set_bitmap(load_picture(name));
        auto b1 = g2d::Bitmap::create(640, 240);
        b1->fill_all("rgba(0, 0, 0, 1)");
        ambience_padding1->set_bitmap(b1);
        auto b2 = g2d::Bitmap::create(320, 960);
        b2->fill_all("rgba(0, 0, 0, 1)");
        ambience_padding2->set_bitmap(b2);
    } else {
        ambience_overlay->set_bitmap(nullptr);
        ambience_padding1->set_bitmap(nullptr);
        ambience_padding2->set_bitmap(nullptr);
    }
}

const js::Val* SpritesetMap::horror_filters_source() { return &rpg::game_screen().horror_filters; }

void SpritesetMap::update() {
    SpritesetBase::update();
    update_parallax();
    update_tilemap();
    update_shadow();
    update_weather();
    update_hide_on_level();
    update_reflect_surface();
    update_light_ambience_overlay();
}

void SpritesetMap::update_parallax() {
    rpg::GameMap& m = rpg::game_map();
    if (!parallax_name_set || parallax_name != m.parallax_name()) {
        parallax_name_set = true;
        parallax_name = m.parallax_name();
        parallax->bitmap = parallax_name.empty() ? nullptr : load_parallax(parallax_name);
    }
    if (parallax->bitmap) {
        parallax->origin_x = m.parallax_ox();
        parallax->origin_y = m.parallax_oy();
    }
}

void SpritesetMap::update_tilemap() {
    rpg::GameMap& m = rpg::game_map();
    tilemap->origin_x = m.display_x() * m.tile_width();
    tilemap->origin_y = m.display_y() * m.tile_height();
}

void SpritesetMap::update_shadow() {
    rpg::GameVehicle* a = rpg::game_map().vehicles_.size() > 2 ? rpg::game_map().airship() : nullptr;
    if (!a) return;
    shadow_sprite->x = a->shadow_x();
    shadow_sprite->y = a->shadow_y();
    shadow_sprite->set_opacity(a->shadow_opacity());
}

void SpritesetMap::update_weather() {
    rpg::GameScreen& s = rpg::game_screen();
    rpg::GameMap& m = rpg::game_map();
    weather->type = s.weather_type;
    weather->power = s.weather_power;
    weather->origin_x = m.display_x() * m.tile_width();
    weather->origin_y = m.display_y() * m.tile_height();
}

void SpritesetMap::update_hide_on_level() { tilemap->hide_on_level(rpg::game_map().current_map_level()); }

void SpritesetMap::update_reflect_surface() {
    if (!reflect_surface_sprite) return;
    rpg::GameMap& m = rpg::game_map();
    reflect_surface_sprite->x = -m.display_x() * m.tile_width();
    reflect_surface_sprite->y = -m.display_y() * m.tile_height();
}

void SpritesetMap::update_light_ambience_overlay() {
    if (!ambience_overlay->bitmap()) return;
    rpg::GamePlayer& p = rpg::game_player();
    ambience_overlay->x = p.ambience_screen_x();
    ambience_overlay->y = p.ambience_screen_y();
    ambience_overlay->set_opacity(p.ambience.opacity);
    ambience_padding1->anchor_y = ambience_overlay->y >= 240 ? 2 : -1;
    ambience_padding2->anchor_x = ambience_overlay->x >= 320 ? 2 : -1;
}

void SpritesetMap::hide_characters() {
    for (auto& s : character_sprites) {
        if (!s->is_tile()) s->hide_sprite();
    }
}

void SpritesetMap::show_characters() {
    for (auto& s : character_sprites) {
        if (!s->is_tile()) s->show_sprite();
    }
}

void SpritesetMap::create_spawned_event(rpg::GameEvent* ev) {
    auto s = std::make_shared<SpriteCharacter>(ev);
    character_sprites.push_back(s);
    s->update();
    tilemap->add_child(s);
}

void SpritesetMap::refresh_mini_labels() {
    for (auto& s : character_sprites) s->refresh_mini_label();
}

fx::TkmTargets SpritesetMap::tkm_targets(g2d::Container* scene) {
    fx::TkmTargets t;
    t.scene = scene;
    t.spriteset = this;
    t.tilemap = tilemap.get();
    t.base_sprite = base_sprite.get();
    t.parallax = parallax.get();
    for (const auto& l : tilemap->layers()) {
        if (l.sprite) t.tile_layers.push_back(l.sprite.get());
    }
    for (auto& s : character_sprites) t.character_sprites.emplace_back(s.get(), s->character()->event_id());
    t.picture_container = picture_container.get();
    return t;
}

}

namespace ui {

bool SpritesetMap::display_js_get(uint32_t key, js::Val& out) {
    if (key == js::atom("_weather")) {
        out = display_js(weather.get());
        return true;
    }
    if (key == js::atom("_baseSprite")) {
        out = display_js(base_sprite.get());
        return true;
    }
    if (key == js::atom("_pictureContainer")) {
        out = display_js(picture_container.get());
        return true;
    }
    if (key == js::atom("_tilemap")) {
        out = display_js(tilemap.get());
        return true;
    }
    if (key == js::atom("_parallax")) {
        out = display_js(parallax.get());
        return true;
    }
    if (key == js::atom("_characterSprites")) {
        js::Val arr = js::make_array();
        for (auto& s : character_sprites) arr.o->items.push_back(display_js(s.get()));
        out = arr;
        return true;
    }
    return false;
}

}
