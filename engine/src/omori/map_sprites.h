#pragma once
#include <memory>
#include <string>

#include "fx.h"
#include "sprites.h"
#include "window_base.h"

namespace rpg {
class GameCharacterBase;
}

namespace ui {

class SpriteBalloon : public SpriteBase {
public:
    SpriteBalloon();
    void setup(int balloon_id);
    void update() override;
    void update_frame();
    int speed() const { return 8; }
    int wait_time() const { return 0; }
    int frame_index() const;
    bool is_playing() const { return duration_ > 0; }

private:
    int balloon_id_ = 0;
    int duration_ = 0;
};

class SpriteEventCon : public SpriteBase {
public:
    SpriteEventCon();
    void setup(const std::string& img, int index);
    void update() override;
    void update_frame();
    int frame_index() const;
    bool is_playing() const { return duration_ > 0; }

private:
    int eventcon_id_ = 0;
    std::string eventcon_img_;
    int duration_ = 0;
    int speed_ = 16;
    int wait_time_ = 64;
    double width_ = 32, height_ = 24;
};

class SpriteInputButton : public g2d::Sprite {
public:
    SpriteInputButton();
    void activate() { active_ = true; }
    void deactivate();
    void reset_animation();
    void refresh_frame();
    void setup_key(const std::string& key);
    void setup_input(const std::string& input);
    void update() override;
    void update_animation();

private:
    int icon_frame_delay_max_ = 20;
    int icon_frame_ = 0;
    int icon_frame_delay_ = 20;
    bool active_ = false;
    double up_[4] = {0, 0, 24, 24};
    double down_[4] = {24, 0, 24, 24};
    void apply_key(const std::string& key);
};

class WindowEventMiniLabel : public WindowBase {
public:
    void initialize_label();
    int standard_font_size() override { return font_size_; }
    double window_height_value();
    double line_height() override { return standard_font_size() + 8; }
    double buffer_x() const { return buffer_x_; }
    double buffer_y() const { return buffer_y_; }
    void set_character(rpg::GameCharacterBase* character);
    void gather_display_data();
    void extract_notedata(const std::string& comment);
    void set_text(const std::string& text);
    void refresh();
    void force_refresh();
    void update() override;
    void update_page();
    void update_opacity();
    void show_label();
    void hide_label();
    bool show_mini_label();
    bool within_range();
    bool meets_facing_requirements();
    const std::string& text() const { return text_; }

private:
    double buffer_x_ = 0, buffer_y_ = 36;
    int font_size_ = 20;
    bool always_show_ = false;
    double range_ = 500;
    bool req_facing_ = false;
    rpg::GameCharacterBase* character_ = nullptr;
    int page_index_ = -2;
    std::string text_;
};

class ShadowSilhouette : public g2d::Sprite {
public:
    explicit ShadowSilhouette(g2d::BitmapPtr b) : g2d::Sprite(std::move(b)) {}
    void render(const g2d::RenderState& rs) override;
};

class SpriteCharacter : public SpriteBase {
public:
    explicit SpriteCharacter(rpg::GameCharacterBase* character);
    ~SpriteCharacter() override;
    rpg::GameCharacterBase* character() const { return character_; }
    void update() override;
    void update_visibility() override;
    bool is_tile() const { return false; }
    void update_bitmap();
    bool is_image_changed();
    void set_character_bitmap();
    void update_frame();
    void update_character_frame();
    double character_block_x();
    double character_block_y();
    double character_pattern_x();
    double character_pattern_y();
    double pattern_width();
    double pattern_height();
    void update_half_body_sprites();
    void create_half_body_sprites();
    virtual void update_position();
    void update_char_animation();
    void update_other();
    void setup_animation();
    void setup_balloon();
    void start_balloon();
    void update_balloon();
    void end_balloon();
    bool is_balloon_playing() const { return (bool)balloon_sprite_; }
    void update_mini_label();
    void setup_mini_label();
    void position_mini_label();
    void update_mini_label_zoom();
    static bool may_have_mini_label(rpg::GameCharacterBase* character);
    void refresh_mini_label();
    void setup_eventcon();
    void start_eventcon();
    void update_eventcon();
    void end_eventcon();
    bool is_eventcon_playing() const { return (bool)eventcon_sprite_; }
    void update_bobble();
    void update_input_icon();
    void update_shadow();
    void detach_attachments();
    rpg::GameBattler* anim_battler() override { return nullptr; }

    static g2d::Container* label_layer;
    fx::Horror horror;

protected:
    rpg::GameCharacterBase* character_ = nullptr;
    int tileset_id_ = -1;
    int tile_id_ = -1;
    std::string character_name_;
    int character_index_ = -1;
    bool has_image_ = false;
    bool is_big_character_ = false;
    double bush_depth_ = 0;
    std::shared_ptr<g2d::Sprite> upper_body_, lower_body_;
    std::shared_ptr<SpriteBalloon> balloon_sprite_;
    std::shared_ptr<SpriteEventCon> eventcon_sprite_;
    int current_eventcon_id_ = 0;
    std::string current_eventcon_img_;
    std::shared_ptr<WindowEventMiniLabel> mini_label_;
    bool has_label_delay_ = false;
    bool mini_label_absent_ = false;
    int label_delay_ = 0;
    std::shared_ptr<SpriteInputButton> input_icon_sprite_;
    double input_icon_scale_ = 0;
    std::shared_ptr<ShadowSilhouette> shadow_sprite_;
};

class SpriteCharacterReflect : public SpriteCharacter {
public:
    explicit SpriteCharacterReflect(rpg::GameCharacterBase* character);
    void update() override;
    void update_reflect();
    void update_position() override;
};

int map_encounter_effect_duration();

}
