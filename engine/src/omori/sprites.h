#pragma once
#include <memory>
#include <string>
#include <vector>

#include "../core/value.h"
#include "../gfx2d/display.h"
#include "../rpg/battler.h"

namespace ui {

class SpriteAnimation;

class SpriteBase : public g2d::Sprite {
public:
    SpriteBase();
    explicit SpriteBase(g2d::BitmapPtr b);
    void update() override;
    void hide_sprite();
    void show_sprite();
    virtual void update_visibility();
    void update_animation_sprites();
    virtual void start_animation(const Value* animation, bool mirror, double delay);
    bool is_animation_playing() const { return !animation_sprites_.empty(); }
    virtual rpg::GameBattler* anim_battler() { return nullptr; }
    virtual double anim_float_jump_rate() { return 0; }
    virtual double anim_texture_height() { return height(); }
    bool hiding() const { return hiding_; }

    g2d::Sprite* effect_target = nullptr;

protected:
    std::vector<std::shared_ptr<SpriteAnimation>> animation_sprites_;
    bool hiding_ = false;
};

class SpriteAnimation : public g2d::Sprite {
public:
    SpriteAnimation();
    void setup(g2d::Sprite* target, const Value* animation, bool mirror, double delay);
    void remove();
    void update() override;
    bool is_playing() const { return duration_ > 0; }
    bool is_ready() const { return true; }
    g2d::Sprite* target() const { return target_; }
    const Value* animation() const { return animation_; }
    static void begin_frame_checks();

private:
    g2d::Sprite* target_ = nullptr;
    const Value* animation_ = nullptr;
    bool mirror_ = false;
    double delay_ = 0;
    int rate_ = 4;
    int duration_ = 0;
    double flash_color_[4] = {0, 0, 0, 0};
    int flash_duration_ = 0;
    int screen_flash_duration_ = 0;
    int hiding_duration_ = 0;
    g2d::BitmapPtr bitmap1_, bitmap2_;
    std::vector<g2d::SpritePtr> cell_sprites_;
    std::shared_ptr<g2d::ScreenSprite> screen_flash_sprite_;
    bool duplicated_ = false;

    void setup_rate();
    void setup_duration();
    void load_bitmaps();
    void create_sprites();
    void create_cell_sprites();
    void create_screen_flash_sprite();
    void update_main();
    virtual void update_position();
    void update_battler_position();
    void update_sve_position();
    void update_frame();
    int current_frame_index() const;
    void update_all_cell_sprites(const Value& frame);
    void update_cell_sprite(g2d::Sprite* sprite, const Value& cell);
    void process_timing_data(const Value& timing);
    void base_process_timing_data(const Value& timing);
    void start_flash(const Value& color, int duration);
    void start_screen_flash(const Value& color, int duration);
    void start_hiding(int duration);
    void update_flash();
    void update_screen_flash();
    void update_hiding();
    double absolute_x() const;
    double absolute_y() const;
    void target_show();
    void target_hide();
    void target_blend(const double c[4]);
};

}
