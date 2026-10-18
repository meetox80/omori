#pragma once
#include <memory>
#include <string>
#include <vector>

#include "../js/value.h"
#include "menu_common.h"
#include "window_base.h"

namespace ui {

class WindowOmoriMapQTESmash : public WindowBase {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 0; }
    double window_width() { return 300; }
    double window_height() { return 42 + 5; }
    void setup(const js::Val& segments, const js::Val& time, const js::Val& width);
    void refresh();
    void finish();
    void update() override;
    void update_smash_bar();
    void update_time_bar();

protected:
    void update_transform_hook() override;

private:
    int segment_ = 0;
    int press_count_ = 0;
    double timer_ = 300;
    double max_timer_ = 300;
    std::vector<double> segment_presses_;
    int alt_state_ = 0;
    int alt_delay_ = 0;
    int close_delay_ = 30;
    double segment_width_ = 0;
    std::shared_ptr<g2d::Sprite> smash_bar_sprite_;
    std::shared_ptr<g2d::Sprite> time_bar_sprite_;
    std::shared_ptr<g2d::TilingSprite> parallax_back_;
    void create_parallax_back();
    void create_bar_sprite();
};

class WindowOmoriStopTheArrow : public WindowBase {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 0; }
    double window_width() { return 96; }
    double window_height() { return 83; }
    void setup(const js::Val& direction, const js::Val& delay);
    void update() override;
    void update_arrows_visibility();
    void finish();

protected:
    void update_transform_hook() override;

private:
    int arrow_index_ = 0;
    double arrow_delay_ = 10;
    double arrow_delay_count_ = 0;
    int arrow_direction_ = 0;
    std::string facing_direction_ = "up";
    std::shared_ptr<g2d::Container> arrow_container_;
    std::shared_ptr<g2d::Sprite> background_sprite_;
    std::vector<std::shared_ptr<g2d::Sprite>> arrow_sprites_;
    void create_background_sprite();
    void create_arrow_sprites();
};

class WindowOmoriHitTheMark : public WindowBase {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 0; }
    double window_width() { return 216; }
    double window_height() { return 66; }
    void setup(const js::Val& speed, int index);
    void refresh();
    void finish();
    void update() override;

protected:
    void update_transform_hook() override;

private:
    double speed_ = 3;
    bool speed_is_number_ = true;
    int direction_ = 0;
    int target_index_ = 0;
    double target_width_ = 9;
    double target_x_ = 0;
    bool stop_ = false;
    std::shared_ptr<g2d::Sprite> background_sprite_;
    std::shared_ptr<g2d::Sprite> pointer_;
    void create_background_sprite();
    void create_pointer();
};

class SpriteMapCharacterTagFace : public g2d::Sprite {
public:
    SpriteMapCharacterTagFace();
    void set_text(const std::string& text);
    void show_text();
    void hide_text();
    std::shared_ptr<menu::SpriteOmoMenuStatusFace> face_sprite;

private:
    std::shared_ptr<g2d::Sprite> background_sprite_;
    std::shared_ptr<g2d::Sprite> text_sprite_;
    std::shared_ptr<g2d::Sprite> face_mask_;
};

class SpriteMapCharacterTag : public g2d::Sprite {
public:
    SpriteMapCharacterTag();
    void update() override;
    void show();
    bool finished() const { return finished_; }

private:
    int start_up_frames_ = 13;
    int moving_frames_ = 10;
    double ring_radius_ = 160;
    int steps_ = -1;
    double center_x_ = 0, center_y_ = 0;
    std::string anim_;
    int index_ = 0;
    bool released_ = false;
    bool finished_ = true;
    std::shared_ptr<g2d::Sprite> background_sprite_;
    std::shared_ptr<g2d::Sprite> party_sprites_container_;
    std::vector<std::shared_ptr<SpriteMapCharacterTagFace>> party_sprites_;
    std::shared_ptr<SpriteMapCharacterTagFace> leader_sprite_;

    void init_settings();
    void create_background();
    void create_party_sprites();
    void refresh_party_sprites();
    void reset_party_sprites();
    bool is_animating() const { return steps_ > -1; }
    bool is_inputable() const;
    int max_items() const;
    void move_left();
    void move_right();
    void on_finish();
    void update_animations();
    void update_input();
    void start_startup_anim();
    void update_startup_anim();
    void update_wait_anim();
    void update_move_anim(int mode);
};

}
