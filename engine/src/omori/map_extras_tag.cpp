#include <cmath>

#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "../rpg/scene.h"
#include "map_extras_parts.h"
#include "ui_util.h"

namespace ui {

SpriteMapCharacterTagFace::SpriteMapCharacterTagFace() {
    anchor_x = 0.5;
    anchor_y = 0.5;
    g2d::BitmapPtr bitmap = g2d::Bitmap::create(110, 110 + 34);
    bitmap->fill_all("rgba(0, 0, 0, 1)");
    bitmap->fill_rect(1, 1, bitmap->width() - 2, bitmap->height() - 2, "rgba(255, 255, 255, 1)");
    bitmap->fill_rect(4, 4, bitmap->width() - 8, 105, "rgba(0, 0, 0, 1)");
    bitmap->fill_rect(0, 113, bitmap->width(), 1, "rgba(0, 0, 0, 1)");
    bitmap->fill_rect(4, 117, bitmap->width() - 8, 22, "rgba(0, 0, 0, 1)");
    background_sprite_ = std::make_shared<g2d::Sprite>(bitmap);
    background_sprite_->x = -(bitmap->width() / 2.0);
    background_sprite_->y = -(110 / 2.0);
    add_child(background_sprite_);
    text_sprite_ = std::make_shared<g2d::Sprite>(g2d::Bitmap::create(110, 32));
    text_sprite_->anchor_x = 0.5;
    text_sprite_->anchor_y = 0;
    text_sprite_->y = (144 / 2.0) - 18;
    add_child(text_sprite_);
    face_sprite = std::make_shared<menu::SpriteOmoMenuStatusFace>();
    face_sprite->anchor_x = 0.5;
    face_sprite->anchor_y = 0.5;
    std::vector<rpg::GameActor*> members = rpg::game_party().members();
    face_sprite->set_actor(members.empty() ? nullptr : members[0]);
    face_sprite->deactivate();
    add_child(face_sprite);
    face_mask_ = std::make_shared<g2d::Sprite>(g2d::Bitmap::create(110, 110));
    face_mask_->x = -(110 / 2.0) + 2;
    face_mask_->y = -(110 / 2.0) + 2;
    face_mask_->bitmap()->fill_rect(0, 0, 106, 2, "rgba(255, 255, 255, 1)");
    face_mask_->bitmap()->fill_rect(104, 0, 2, 106, "rgba(255, 255, 255, 1)");
    add_child(face_mask_);
    hide_text();
}

void SpriteMapCharacterTagFace::set_text(const std::string& text) {
    g2d::Bitmap* bitmap = text_sprite_->bitmap().get();
    bitmap->font_size = menu::blue("Sprite_MapCharacterTagFace")["setText_fontsize"].as_int();
    bitmap->draw_text(text, 0, 0, bitmap->width(), bitmap->height(), "center");
}

void SpriteMapCharacterTagFace::show_text() {
    background_sprite_->set_frame(0, 0, 110, background_sprite_->bitmap()->height());
    text_sprite_->visible = true;
}

void SpriteMapCharacterTagFace::hide_text() {
    background_sprite_->set_frame(0, 0, 110, 114);
    text_sprite_->visible = false;
}

SpriteMapCharacterTag::SpriteMapCharacterTag() {
    init_settings();
    create_background();
    create_party_sprites();
}

void SpriteMapCharacterTag::init_settings() {
    start_up_frames_ = 13;
    moving_frames_ = 10;
    ring_radius_ = 160;
    steps_ = -1;
    center_x_ = 640 / 2.0;
    center_y_ = (480 / 2.0) - 10;
    anim_.clear();
    index_ = 0;
    set_opacity(0);
    released_ = false;
    finished_ = true;
}

void SpriteMapCharacterTag::create_background() {
    background_sprite_ = std::make_shared<g2d::Sprite>();
    add_child(background_sprite_);
}

void SpriteMapCharacterTag::create_party_sprites() {
    party_sprites_.clear();
    party_sprites_container_ = std::make_shared<g2d::Sprite>();
    party_sprites_container_->set_opacity(0);
    add_child(party_sprites_container_);
    const Value& loc = menu::blue("Map_Character_Tag");
    for (int i = 0; i < 4; i++) {
        auto sprite = std::make_shared<SpriteMapCharacterTagFace>();
        sprite->set_text(data::js_string(loc["leader"]));
        sprite->x = center_x_;
        sprite->y = center_y_;
        party_sprites_.push_back(sprite);
        party_sprites_container_->add_child(sprite);
    }
    leader_sprite_ = std::make_shared<SpriteMapCharacterTagFace>();
    leader_sprite_->x = center_x_;
    leader_sprite_->y = center_y_;
    leader_sprite_->show_text();
    leader_sprite_->set_text(data::js_string(loc["tag_who"]));
    add_child(leader_sprite_);
    refresh_party_sprites();
}

void SpriteMapCharacterTag::refresh_party_sprites() {
    std::vector<rpg::GameActor*> members = rpg::game_party().members();
    for (size_t i = 0; i < party_sprites_.size(); i++) {
        rpg::GameActor* actor = i < members.size() ? members[i] : nullptr;
        party_sprites_[i]->face_sprite->set_actor(actor);
        party_sprites_[i]->visible = actor != nullptr;
    }
    leader_sprite_->face_sprite->set_actor(rpg::game_party().leader());
}

void SpriteMapCharacterTag::reset_party_sprites() {
    for (auto& sprite : party_sprites_) {
        sprite->x = center_x_;
        sprite->y = center_y_;
    }
}

bool SpriteMapCharacterTag::is_inputable() const {
    if (is_animating()) return false;
    if (released_) return false;
    return true;
}

int SpriteMapCharacterTag::max_items() const { return rpg::game_party().size(); }

void SpriteMapCharacterTag::update() {
    g2d::Sprite::update();
    update_animations();
    update_input();
}

void SpriteMapCharacterTag::move_left() {
    steps_ = moving_frames_;
    int max = max_items();
    if (max > 0) index_ = (index_ - 1 + max) % max;
    anim_ = "moveLeft";
}

void SpriteMapCharacterTag::move_right() {
    steps_ = moving_frames_;
    int max = max_items();
    if (max > 0) index_ = (index_ + 1) % max;
    anim_ = "moveRight";
}

void SpriteMapCharacterTag::show() {
    index_ = 0;
    refresh_party_sprites();
    reset_party_sprites();
    g2d::BitmapPtr snap = rpg::scene_manager::snap();
    snap->blur();
    background_sprite_->set_bitmap(snap);
    background_sprite_->set_opacity(0);
    party_sprites_container_->set_opacity(0);
    start_startup_anim();
    released_ = false;
    finished_ = false;
}

void SpriteMapCharacterTag::on_finish() {
    anim_.clear();
    steps_ = -1;
    background_sprite_->set_bitmap(nullptr);
    released_ = false;
    finished_ = true;
    if (index_ > 0) {
        std::vector<rpg::GameActor*> members = rpg::game_party().members();
        if ((size_t)index_ < members.size()) {
            int actor_id = members[(size_t)index_]->actor_id();
            rpg::game_variables().set_value(rpg::params().tag_selected_variable, js::Val::num(actor_id));
            rpg::game_temp().reserve_common_event(rpg::params().tag_common_event);
        }
    }
    index_ = 0;
}

void SpriteMapCharacterTag::update_animations() {
    if (!released_) {
        if (!input::is_pressed(input::TAG)) {
            released_ = true;
            return;
        }
    } else if (!finished_ && released_) {
        set_opacity(opacity() - 30);
        if (opacity() <= 0) on_finish();
        return;
    }
    if (!released_ && steps_ > -1) {
        if (anim_ == "start") update_startup_anim();
        else if (anim_ == "moveLeft") update_move_anim(1);
        else if (anim_ == "moveRight") update_move_anim(0);
    }
}

void SpriteMapCharacterTag::update_input() {
    if (is_inputable()) {
        if (input::is_repeated(input::LEFT)) move_left();
        if (input::is_repeated(input::RIGHT)) move_right();
    }
}

void SpriteMapCharacterTag::start_startup_anim() {
    steps_ = start_up_frames_;
    anim_ = "start";
}

void SpriteMapCharacterTag::update_startup_anim() {
    if (opacity() < 255) {
        set_opacity(opacity() + 60);
        background_sprite_->set_opacity(background_sprite_->opacity() + 60);
        return;
    }
    int max = std::min(max_items(), (int)party_sprites_.size());
    double d1 = 2.0 * M_PI / max_items();
    double d2 = 1.0 * M_PI / start_up_frames_;
    double r = ring_radius_ - 1.0 * ring_radius_ * steps_ / start_up_frames_;
    for (int i = 0; i < max; i++) {
        SpriteMapCharacterTagFace* sprite = party_sprites_[(size_t)i].get();
        double d = d1 * i + d2 * steps_;
        sprite->x = center_x_ + (r * std::sin(d));
        sprite->y = center_y_ - (r * std::cos(d));
        sprite->hide_text();
        sprite->face_sprite->deactivate();
    }
    if (steps_ > 0) {
        party_sprites_container_->set_opacity((party_sprites_container_->opacity() * (steps_ - 1) + 255) / steps_);
    }
    steps_--;
    if (steps_ < 0) update_wait_anim();
}

void SpriteMapCharacterTag::update_wait_anim() {
    int max = std::min(max_items(), (int)party_sprites_.size());
    double d = 2.0 * M_PI / max_items();
    for (int i = 0; i < max; i++) {
        SpriteMapCharacterTagFace* sprite = party_sprites_[(size_t)i].get();
        int j = i - index_;
        sprite->x = (center_x_ + (ring_radius_ * std::sin(d * j)));
        sprite->y = center_y_ - ((ring_radius_ * std::cos(d * j)));
        if (i == index_) {
            sprite->show_text();
            sprite->face_sprite->activate();
        } else {
            sprite->face_sprite->deactivate();
            sprite->hide_text();
        }
    }
}

void SpriteMapCharacterTag::update_move_anim(int mode) {
    int max = std::min(max_items(), (int)party_sprites_.size());
    double d1 = 2.0 * M_PI / max_items();
    double d2 = d1 / moving_frames_;
    if (mode != 0) d2 *= -1;
    for (int i = 0; i < max; i++) {
        SpriteMapCharacterTagFace* sprite = party_sprites_[(size_t)i].get();
        int j = i - index_;
        double d = d1 * j + d2 * steps_;
        sprite->x = (center_x_ + (ring_radius_ * std::sin(d)));
        sprite->y = center_y_ - ((ring_radius_ * std::cos(d)));
        sprite->hide_text();
    }
    steps_--;
    if (steps_ < 0) update_wait_anim();
}

}
