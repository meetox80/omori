#include "spriteset.h"

#include <cmath>

#include "../core/image.h"
#include "../platform/gfx.h"
#include "../rpg/config.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/map.h"
#include "../rpg/screen.h"
#include "../rpg/state.h"
#include "ui_util.h"
#include "video.h"

namespace rpg {
int shake_type();
}

namespace ui {

SpritePicture::SpritePicture(int picture_id) : picture_id_(picture_id) {
    rpg::GamePicture* pic = picture();
    if (pic && pic->frame_data) pic->frame_data->refresh = true;
    is_picture = true;
    update();
}

rpg::GamePicture* SpritePicture::picture() const { return rpg::game_screen().picture(picture_id_); }

void SpritePicture::update() {
    rpg::GamePicture* src = horror_source ? picture() : nullptr;
    horror.sync(*this, src ? &src->horror_filters : nullptr, false);
    SpriteBase::update();
    horror.update(*this);
    update_bitmap();
    if (visible) {
        update_origin();
        update_position();
        update_scale();
        update_tone();
        update_other();
    }
}

void SpritePicture::load_bitmap() {
    set_bitmap(load_picture(picture_name_));
    refresh_parallax();
}

void SpritePicture::refresh_parallax() {
    rpg::GamePicture* pic = picture();
    if (!pic) return;
    if (pic->use_parallax) {
        if (!parallax_) {
            parallax_ = std::make_shared<g2d::TilingSprite>();
            add_child(parallax_);
        }
        if (bitmap()) parallax_->bitmap = bitmap();
        parallax_->move(x, y, pic->parallax_rect.w, pic->parallax_rect.h);
        set_bitmap(nullptr);
    } else if (parallax_) {
        remove_child(parallax_.get());
    }
}

void SpritePicture::update_bitmap() {
    rpg::GamePicture* pic = picture();
    if (pic) {
        if (picture_name_ != pic->name) {
            picture_name_ = pic->name;
            load_bitmap();
        }
        visible = true;
    } else {
        picture_name_.clear();
        set_bitmap(nullptr);
        visible = false;
    }
    if (pic) {
        if (pic->frame_data) update_custom_frame();
        else reset_frame();
    }
    if (visible && !horror_source) horror_source = picture() != nullptr;
    else horror_source = false;
}

void SpritePicture::update_custom_frame() {
    rpg::GamePicture* pic = picture();
    if (pic && pic->needs_frame_refresh()) {
        rpg::FrameRect r = pic->custom_frame();
        set_frame(r.x, r.y, r.w, r.h);
        pic->frame_data->refresh = false;
    }
}

void SpritePicture::reset_frame() {
    if (bitmap()) set_frame(0, 0, bitmap()->width(), bitmap()->height());
    else set_frame(0, 0, 0, 0);
}

void SpritePicture::update_origin() {
    rpg::GamePicture* pic = picture();
    if (pic->origin == 0) {
        anchor_x = 0;
        anchor_y = 0;
    } else {
        anchor_x = 0.5;
        anchor_y = 0.5;
    }
}

void SpritePicture::update_position() {
    rpg::GamePicture* pic = picture();
    x = std::floor(pic->x);
    y = std::floor(pic->y);
    if (pic->pinned_to_map) {
        rpg::GameMap& m = rpg::game_map();
        x = pic->x - m.display_x() * m.tile_width();
        y = pic->y - m.display_y() * m.tile_height();
    }
    if (pic->follow_shake) y -= std::round(2 * rpg::game_screen().shake);
}

void SpritePicture::update_scale() {
    rpg::GamePicture* pic = picture();
    scale_x = pic->scale_x / 100.0;
    scale_y = pic->scale_y / 100.0;
}

void SpritePicture::update_tone() {
    rpg::GamePicture* pic = picture();
    if (pic->has_tone) {
        set_color_tone(pic->tone);
    } else {
        static const double zero[4] = {0, 0, 0, 0};
        set_color_tone(zero);
    }
}

void SpritePicture::update_other() {
    rpg::GamePicture* pic = picture();
    set_opacity(pic->opacity);
    blend_mode = pic->blend_mode;
    rotation = pic->angle() * M_PI / 180.0;
    update_custom_frame();
    if (parallax_) {
        parallax_->origin_x += pic->parallax_rect.x;
        parallax_->origin_y += pic->parallax_rect.y;
    }
}

SpriteTimer::SpriteTimer() {
    align_ = data::plugin_param("YEP_EventTimerControl", "TextAlign", "right");
    auto b = g2d::Bitmap::create(144, 48);
    b->font_size = 32;
    set_bitmap(b);
    update();
}

void SpriteTimer::update() {
    g2d::Sprite::update();
    int s = rpg::game_timer().seconds();
    if (seconds_ != s) {
        seconds_ = s;
        redraw();
    }
    x = 640 - bitmap()->width() - 12;
    y = 0;
    visible = rpg::game_timer().is_working();
}

std::string SpriteTimer::timer_text() const {
    int hour = (int)std::floor(seconds_ / 60.0 / 60.0);
    int min = (int)std::floor(seconds_ / 60.0) % 60;
    int sec = seconds_ % 60;
    char buf[16];
    snprintf(buf, sizeof buf, "%02d:%02d", min, sec);
    std::string text = buf;
    if (hour > 0) text = group_digits(hour) + ":" + text;
    return text;
}

void SpriteTimer::redraw() {
    g2d::Bitmap* b = bitmap().get();
    b->clear();
    b->draw_text(timer_text(), 0, 0, b->width(), b->height(), align_);
}

void apply_screen_tone(const double t[4]) {
    if (!t[0] && !t[1] && !t[2] && !t[3]) return;
    if (t[3] > 0) gfx::desaturate((float)(std::min(255.0, t[3]) / 255.0));
    float x0 = images::OFFSET_X, y0 = images::OFFSET_Y;
    float x1 = x0 + 640 * images::SCALE, y1 = y0 + 480 * images::SCALE;
    int ar = std::max(0, (int)std::lround(t[0])), ag = std::max(0, (int)std::lround(t[1])), ab = std::max(0, (int)std::lround(t[2]));
    int sr = std::max(0, (int)std::lround(-t[0])), sg = std::max(0, (int)std::lround(-t[1])), sb = std::max(0, (int)std::lround(-t[2]));
    if (ar || ag || ab) gfx::fill(x0, y0, x1, y1, gfx::rgba(std::min(ar, 255), std::min(ag, 255), std::min(ab, 255), 255), gfx::Blend::Add);
    if (sr || sg || sb) gfx::fill(x0, y0, x1, y1, gfx::rgba(std::min(sr, 255), std::min(sg, 255), std::min(sb, 255), 255), gfx::Blend::Subtract);
}

void ToneLayer::render(const g2d::RenderState& rs) {
    g2d::Sprite::render(rs);
    apply_screen_tone(tone);
}

static SpritesetBase* g_current_spriteset = nullptr;

SpritesetBase* SpritesetBase::current() { return g_current_spriteset; }

SpritesetBase::SpritesetBase() { g_current_spriteset = this; }

SpritesetBase::~SpritesetBase() {
    if (g_current_spriteset == this) g_current_spriteset = nullptr;
}

void SpritesetBase::initialize() {
    set_frame(0, 0, 640, 480);
    create_lower_layer();
    create_upper_layer();
    update();
    screen_effects.create(*this);
}

void SpritesetBase::create_lower_layer() { create_base_sprite(); }

void SpritesetBase::create_upper_layer() {
    create_videos();
    create_pictures();
    create_timer();
    create_screen_sprites();
}

void SpritesetBase::create_base_sprite() {
    base_sprite = std::make_shared<ToneLayer>();
    base_sprite->set_frame(0, 0, 640, 480);
    black_screen = std::make_shared<g2d::ScreenSprite>();
    black_screen->set_opacity(255);
    add_child(base_sprite);
    base_sprite->add_child(black_screen);
}

void SpritesetBase::create_videos() {
    videos_container = video_container();
    add_child(videos_container);
}

void SpritesetBase::create_pictures() {
    picture_container = std::make_shared<g2d::Sprite>();
    picture_container->set_frame(0, 0, 640, 480);
    for (int i = 1; i <= rpg::GameScreen::max_pictures(); i++) picture_container->add_child(std::make_shared<SpritePicture>(i));
    add_child(picture_container);
}

void SpritesetBase::move_picture_below(int picture_id, g2d::Container* target, double z_value) {
    if (!picture_container || !target) return;
    size_t index = (size_t)(picture_id - 1);
    if (index >= picture_container->children.size()) return;
    g2d::DisplayPtr sprite = picture_container->remove_child_at(index);
    sprite->z = z_value;
    target->add_child(sprite);
}

SpritePicture* SpritesetBase::picture_sprite(int index) {
    if (!picture_container || index < 0 || (size_t)index >= picture_container->children.size()) return nullptr;
    return dynamic_cast<SpritePicture*>(picture_container->children[(size_t)index].get());
}

void SpritesetBase::create_timer() { timer_sprite = std::make_shared<SpriteTimer>(); }

void SpritesetBase::create_screen_sprites() {
    flash_sprite = std::make_shared<g2d::ScreenSprite>();
    fade_sprite = std::make_shared<g2d::ScreenSprite>();
    add_child(flash_sprite);
    add_child(fade_sprite);
}

void SpritesetBase::update() {
    horror.sync(*this, horror_filters_source(), false);
    g2d::Sprite::update();
    horror.update(*this);
    update_screen_sprites();
    update_tone_changer();
    update_position();
    if (rpg::config().screen_effects_enabled) update_screen_effects();
}

void SpritesetBase::update_screen_sprites() {
    rpg::GameScreen& s = rpg::game_screen();
    flash_sprite->set_color((int)s.flash_color[0], (int)s.flash_color[1], (int)s.flash_color[2]);
    flash_sprite->set_opacity(s.flash_color[3]);
    fade_sprite->set_opacity(255 - s.brightness);
}

void SpritesetBase::update_tone_changer() {
    rpg::GameScreen& s = rpg::game_screen();
    for (int i = 0; i < 4; i++) tone[i] = s.tone[i];
    for (int i = 0; i < 4; i++) base_sprite->tone[i] = std::clamp(tone[i], -255.0, 255.0);
    base_sprite->tone[3] = std::clamp(tone[3], 0.0, 255.0);
}

void SpritesetBase::update_position() {
    rpg::GameScreen& s = rpg::game_screen();
    double scale = s.zoom_scale;
    scale_x = scale;
    scale_y = scale;
    x = std::round(-s.zoom_x * (scale - 1));
    y = std::round(-s.zoom_y * (scale - 1));
    switch (rpg::shake_type()) {
    case 1:
        x += std::round(s.shake_vector_x);
        y += std::round(s.shake_vector_y);
        break;
    case 2: x += std::round(2 * s.shake); break;
    case 3: y += std::round(2 * s.shake); break;
    default: x += std::round(s.shake); break;
    }
}

void SpritesetBase::update_screen_effects() { screen_effects.update(); }

void SpritesetBase::render(const g2d::RenderState& rs) { g2d::Sprite::render(rs); }

}
