#pragma once
#include <cstddef>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../gfx2d/window.h"
#include "../js/value.h"
#include "../rpg/battler.h"

namespace ui {

struct TextState {
    std::string text;
    size_t index = 0;
    double x = 0, y = 0, left = 0, height = 0;
};

constexpr size_t kNoEnd = std::numeric_limits<size_t>::max();

struct LetterEffect {
    std::string name;
    bool active = true;
    int tag = 0;
    size_t start_index = 0;
    size_t end_index = kNoEnd;
    bool has_timer = false;
    double timer = 0;
    std::map<std::string, double> num;
    std::map<std::string, bool> flag;
    g2d::Sprite* synch_sprite = nullptr;
    double n(const char* k) const {
        auto it = num.find(k);
        return it == num.end() ? 0 : it->second;
    }
    bool f(const char* k) const {
        auto it = flag.find(k);
        return it != flag.end() && it->second;
    }
};

struct LetterSprite {
    g2d::SpritePtr sprite;
    double origin_x = 0, origin_y = 0;
    double offset_x = 0, offset_y = 0;
    size_t index = 0;
    size_t position = 0;
    int tag = 0;
};

class WindowBase : public g2d::Window {
public:
    static constexpr int kIconWidth = 32;
    static constexpr int kIconHeight = 32;
    static constexpr int kFaceWidth = 106;
    static constexpr int kFaceHeight = 106;

    WindowBase();
    ~WindowBase() override;
    virtual void initialize(double x, double y, double w, double h);

    template <class T, class... A>
    static std::shared_ptr<T> make(A&&... a) {
        std::shared_ptr<T> w(new T());
        w->initialize(std::forward<A>(a)...);
        return w;
    }

    virtual double line_height();
    virtual std::string standard_font_face();
    virtual int standard_font_size();
    virtual int standard_padding();
    virtual int text_padding();
    virtual int standard_back_opacity();
    virtual void load_windowskin();
    virtual void update_padding();
    virtual void update_back_opacity();
    virtual double contents_width();
    virtual double contents_height();
    double fitting_height(int lines);
    virtual void update_tone();
    virtual void create_contents();
    virtual void reset_font_settings();
    void reset_text_color();
    void update() override;
    virtual void update_open();
    virtual void update_close();
    virtual void open();
    virtual void close();
    bool is_opening() const { return opening_; }
    bool is_closing() const { return closing_; }
    virtual void show() { visible = true; }
    virtual void hide() { visible = false; }
    virtual void activate() { active = true; }
    virtual void deactivate() { active = false; }
    virtual int standard_openness_type() { return 0; }
    virtual int open_speed() { return 32; }
    virtual int close_speed() { return 32; }

    std::string text_color(int n);
    std::string normal_color();
    std::string system_color();
    std::string crisis_color();
    std::string death_color();
    std::string gauge_back_color();
    std::string hp_gauge_color1();
    std::string hp_gauge_color2();
    std::string mp_gauge_color1();
    std::string mp_gauge_color2();
    std::string mp_cost_color();
    std::string power_up_color();
    std::string power_down_color();
    std::string tp_gauge_color1();
    std::string tp_gauge_color2();
    std::string tp_cost_color();
    std::string pending_color();
    int translucent_opacity() { return 160; }
    void change_text_color(const std::string& c);
    void change_paint_opacity(bool enabled);
    virtual void draw_text(const std::string& text, double x, double y, double max_width, const std::string& align = "left");
    double text_width(const std::string& text);
    virtual double draw_text_ex(const std::string& text, double x, double y);
    double base_draw_text_ex(const std::string& text, double x, double y);
    virtual std::string convert_escape_characters(std::string text);
    std::string actor_name(int n);
    std::string party_member_name(int n);
    virtual void process_character(TextState& ts);
    virtual void process_normal_character(TextState& ts);
    void base_process_normal_character(TextState& ts);
    virtual void process_new_line(TextState& ts);
    virtual void process_new_page(TextState& ts);
    std::string obtain_escape_code(TextState& ts);
    js::Val obtain_escape_param(TextState& ts);
    int obtain_escape_param_int(TextState& ts);
    std::string obtain_escape_string(TextState& ts);
    std::string obtain_color_string(TextState& ts);
    std::vector<std::string> obtain_escape_params_array(TextState& ts);
    virtual void process_escape_character(const std::string& code, TextState& ts);
    void base_process_escape_character(const std::string& code, TextState& ts);
    virtual void process_draw_icon(int icon_index, TextState& ts);
    void process_draw_input_icon(const std::string& input, TextState& ts);
    virtual void make_font_bigger();
    virtual void make_font_smaller();
    virtual double calc_text_height(const TextState& ts, bool all);
    void prefetch_text_fonts(const std::string& text);
    void draw_icon(int icon_index, double x, double y);
    void draw_face(const std::string& face_name, int face_index, double x, double y, double width = 0, double height = 0);
    void draw_character(const std::string& name, int index, double x, double y);
    virtual void draw_gauge(double x, double y, double width, double rate, const std::string& c1, const std::string& c2);
    double gauge_height();
    std::string hp_color(rpg::GameBattler* a);
    std::string mp_color(rpg::GameBattler* a);
    std::string tp_color(rpg::GameBattler* a);
    void draw_actor_character(rpg::GameActor* a, double x, double y);
    void draw_actor_face(rpg::GameActor* a, double x, double y, double w = 0, double h = 0);
    void draw_actor_name(rpg::GameBattler* a, double x, double y, double w = 168);
    void draw_actor_class(rpg::GameActor* a, double x, double y, double w = 168);
    void draw_actor_nickname(rpg::GameActor* a, double x, double y, double w = 270);
    void draw_actor_level(rpg::GameActor* a, double x, double y);
    void draw_actor_icons(rpg::GameBattler* a, double x, double y, double w = 144);
    void draw_current_and_max(double current, double max, double x, double y, double width, const std::string& c1, const std::string& c2);
    void draw_actor_hp(rpg::GameBattler* a, double x, double y, double w = 186);
    void draw_actor_mp(rpg::GameBattler* a, double x, double y, double w = 186);
    void draw_actor_tp(rpg::GameBattler* a, double x, double y, double w = 96);
    void draw_actor_simple_status(rpg::GameActor* a, double x, double y, double w);
    void draw_item_name(const rpg::ItemRef& item, double x, double y, double w = 312);
    void draw_currency_value(double value, const std::string& unit, double x, double y, double w);
    std::string param_change_text_color(double change);
    void set_background_type(int type);
    void show_background_dimmer();
    void hide_background_dimmer();
    void update_background_dimmer();
    void refresh_dimmer_bitmap();
    double text_width_ex(const std::string& text);
    bool using_gold_icon(const std::string& unit);
    void draw_actor_icons_turns(rpg::GameBattler* a, double x, double y, double w);
    void draw_state_turns(rpg::GameBattler* a, const Value& state, double x, double y);
    void draw_state_counter(rpg::GameBattler* a, const Value& state, double x, double y);
    void draw_buff_turns(rpg::GameBattler* a, int param_id, double x, double y);
    void draw_buff_rate(rpg::GameBattler* a, int param_id, double x, double y);
    std::string set_word_wrap(std::string text);
    virtual std::string convert_extra_escape_characters(std::string text);
    virtual bool check_word_wrap(TextState& ts);
    virtual double wordwrap_width();
    void save_current_window_settings();
    void restore_current_window_settings();
    double text_width_ex_check(const std::string& text);
    double text_height_ex(const std::string& text);
    double get_text_ex_height(const std::string& text, double x, double y);
    std::string convert_macro_text(std::string text);
    void draw_black_letter_text(const std::string& text, double x, double y, double space = 32, double offset = 0);
    int item_color_index(const rpg::ItemRef& item);
    void draw_hp_icon(double x, double y);
    void draw_mp_icon(double x, double y);
    void draw_short_actor_hp(rpg::GameBattler* a, double x, double y);
    void draw_short_actor_mp(rpg::GameBattler* a, double x, double y);
    void draw_item_icon(const rpg::ItemRef& item, double x, double y, double rate = 1.0);
    int world_gold_icon();
    std::string world_currency_unit();
    bool can_line_break(int type, const std::string& ch);
    bool is_eastern_locale();

    void init_letter_effects();
    void create_letter_effect_container();
    bool can_create_letter_effect_sprite();
    bool is_ignoring_letter_effect_escape_codes() { return check_word_wrap_mode_; }
    void clear_letter_effect_sprites();
    void setup_letter_effect(const std::string& code, int index, TextState& ts);
    void set_letter_effect_end_index(const std::string& name, TextState& ts);
    void create_letter_effect_sprite(TextState& ts);
    void update_letter_effects();
    void apply_letter_effect_to_sprite(LetterEffect& effect, LetterSprite& ls);

    g2d::BitmapPtr contents_bitmap() const { return contents(); }
    g2d::Bitmap& c() const { return *contents(); }
    double opacity_value() const { return window_opacity(); }
    void set_window_alpha(double v) { set_window_opacity(v); }
    void set_frame_opacity(double v);
    bool active_flag() const { return active; }

    bool word_wrap_ = false;
    bool check_word_wrap_mode_ = false;

protected:
    bool opening_ = false;
    bool closing_ = false;
    std::shared_ptr<g2d::Sprite> dimmer_sprite_;
    std::shared_ptr<g2d::Sprite> letter_effect_container_;
    std::vector<LetterSprite> letter_sprites_;
    std::vector<LetterEffect> letter_effects_;
    bool letter_effects_active_ = false;
    int letter_effect_tag_ = 0;
    bool has_letter_effect_timer_ = false;
    double letter_effect_timer_ = 0;
    bool has_letter_effect_synch_ = false;
    int letter_effect_synch_index_ = 0;
    struct SavedFont {
        bool saved = false;
        std::string face, color, outline_color;
        int size = 28, outline_width = 4;
        bool bold = false, italic = false;
    } saved_font_;
    std::map<int, std::string> color_cache_;
    g2d::Bitmap* color_cache_skin_ = nullptr;
};

using WindowBasePtr = std::shared_ptr<WindowBase>;

}
