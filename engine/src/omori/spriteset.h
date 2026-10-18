#pragma once
#include <memory>
#include <string>
#include <vector>

#include "../gfx2d/display.h"
#include "fx.h"
#include "sprites.h"

namespace rpg {
class GamePicture;
}

namespace ui {

class SpritePicture : public SpriteBase {
public:
    explicit SpritePicture(int picture_id);
    rpg::GamePicture* picture() const;
    int picture_id() const { return picture_id_; }
    void update() override;
    void update_bitmap();
    void update_origin();
    void update_position();
    void update_scale();
    void update_tone();
    void update_other();
    void update_custom_frame();
    void reset_frame();
    void load_bitmap();
    bool horror_source = false;
    fx::Horror horror;

private:
    int picture_id_ = 0;
    std::string picture_name_;
    std::shared_ptr<g2d::TilingSprite> parallax_;
    void refresh_parallax();
};

class SpriteTimer : public g2d::Sprite {
public:
    SpriteTimer();
    void update() override;

private:
    int seconds_ = -1;
    std::string align_;
    void redraw();
    std::string timer_text() const;
};

class ToneLayer : public g2d::Sprite {
public:
    double tone[4] = {0, 0, 0, 0};
    void render(const g2d::RenderState& rs) override;
};

void apply_screen_tone(const double tone[4]);

class SpritesetBase : public g2d::Sprite {
public:
    SpritesetBase();
    virtual ~SpritesetBase();
    void initialize();
    virtual void create_lower_layer();
    virtual void create_upper_layer();
    virtual void create_base_sprite();
    virtual void create_videos();
    virtual void create_pictures();
    virtual void create_timer();
    virtual void create_screen_sprites();
    void update() override;
    virtual void update_screen_sprites();
    virtual void update_tone_changer();
    virtual void update_position();
    virtual void update_screen_effects();
    void render(const g2d::RenderState& rs) override;
    SpritePicture* picture_sprite(int index);
    void move_picture_below(int picture_id, g2d::Container* target, double z);

    std::shared_ptr<ToneLayer> base_sprite;
    std::shared_ptr<g2d::ScreenSprite> black_screen;
    std::shared_ptr<g2d::Sprite> picture_container;
    std::shared_ptr<g2d::Container> videos_container;
    std::shared_ptr<SpriteTimer> timer_sprite;
    std::shared_ptr<g2d::ScreenSprite> flash_sprite;
    std::shared_ptr<g2d::ScreenSprite> fade_sprite;
    double tone[4] = {0, 0, 0, 0};
    fx::Horror horror;
    fx::ScreenEffects screen_effects;
    virtual const js::Val* horror_filters_source() { return nullptr; }
    static SpritesetBase* current();
};

}
