#include <cmath>
#include <memory>
#include <string>

#include "../js/value.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/map.h"
#include "map_effects.h"
#include "ui_util.h"

namespace ui {

namespace {

constexpr const char* kPlugin = "Olivia_MapDisplayNameCore";

double olivia_num(const char* key, double def) {
    std::string s = data::plugin_param(kPlugin, key, "");
    if (s.empty()) return def;
    return js::string_to_number(s);
}

struct OliviaConfig {
    double text_distance = 80;
    double window_height = 36;
    double y_starting = 0, y_middle = 0, y_ending = 0;
    double fade_in = 1, fade_middle = 1, fade_out = 1;
};

const OliviaConfig& olivia() {
    static OliviaConfig c;
    static bool loaded = false;
    if (loaded) return c;
    loaded = true;
    c.text_distance = olivia_num("Text Distance", 80);
    c.window_height = olivia_num("Window Height", 36);
    c.y_starting = olivia_num("Starting Y", 0);
    c.y_middle = olivia_num("Middle Y", 0);
    c.y_ending = olivia_num("Ending Y", 0);
    c.fade_in = olivia_num("Fade In Time", 1);
    c.fade_middle = olivia_num("Middle Time", 1);
    c.fade_out = olivia_num("Fade Out Time", 1);
    return c;
}

enum class Step { FadeIn, FadeMiddle, FadeOut };

class WindowMapName : public WindowBase {
public:
    void setup() {
        double x = 0;
        double y = olivia().y_starting;
        double width = window_width();
        double height = window_height();
        setup_fade_rate();
        setup_display_name();
        WindowBase::initialize(x, y, width, height);
        step_ = Step::FadeIn;
        set_window_opacity(0);
        set_contents_opacity(0);
        show_count_ = 0;
        refresh();
    }

    double window_width() { return 640; }
    double window_height() { return olivia().window_height; }
    int standard_padding() override { return 0; }

    void update() override {
        WindowBase::update();
        if (is_open() && show_count_ > 0 && rpg::game_map().is_name_display_enabled()) {
            update_step();
        } else {
            update_fade_out();
        }
    }

    void open() override {
        step_ = Step::FadeIn;
        refresh();
        show_count_ = olivia().fade_in;
    }

    void close() override { show_count_ = 0; }

private:
    std::string text_;
    double fade_in_rate_ = 0, fade_out_rate_ = 0;
    Step step_ = Step::FadeIn;
    double show_count_ = 0;

    void setup_fade_rate() {
        fade_in_rate_ = std::ceil(255 / olivia().fade_in);
        fade_out_rate_ = std::ceil(255 / olivia().fade_out);
    }

    void setup_display_name() {
        rpg::GameMap& map = rpg::game_map();
        text_ = map.display_name();
        map.clear_override_display_name();
    }

    void update_step() {
        const OliviaConfig& c = olivia();
        show_count_--;
        double d = show_count_;
        double target_y = 0;
        if (step_ == Step::FadeIn) {
            update_fade_in();
            target_y = c.y_middle;
            if (show_count_ <= 0) {
                show_count_ = c.fade_middle;
                step_ = Step::FadeMiddle;
            }
        } else if (step_ == Step::FadeMiddle) {
            set_contents_opacity(255);
            target_y = c.y_middle;
            if (show_count_ <= 0) {
                show_count_ = c.fade_out;
                step_ = Step::FadeOut;
            }
        } else {
            update_fade_out();
            target_y = c.y_ending;
            if (show_count_ <= 0) close();
        }
        if (d > 0) y = (y * (d - 1) + target_y) / d;
    }

    void update_fade_in() { set_contents_opacity(contents_opacity() + fade_in_rate_); }
    void update_fade_out() { set_contents_opacity(contents_opacity() - fade_out_rate_); }

    void refresh() {
        c().clear();
        if (!text_.empty()) {
            double width = draw_text_ex(text_, 0, c().height());
            double distance = olivia().text_distance;
            width += distance * 2;
            draw_background(0, 0, width, window_height());
            draw_text_ex(text_, distance, 0);
        }
    }

    void draw_background(double bx, double by, double width, double height) {
        rpg::GameMap& map = rpg::game_map();
        std::string color1 = map.display_name_dim_color(1);
        std::string color2 = map.display_name_dim_color(2);
        c().gradient_fill_rect(bx, by, width / 2, height, color2, color1, false);
        c().gradient_fill_rect(bx + width / 2, by, width / 2, height, color1, color2, false);
    }
};

}

std::shared_ptr<WindowBase> create_map_name_window() {
    std::shared_ptr<WindowMapName> w(new WindowMapName());
    w->setup();
    return w;
}

void map_name_window_open(WindowBase* w) {
    if (w) w->open();
}

void map_name_window_close(WindowBase* w) {
    if (w) w->close();
}

}
