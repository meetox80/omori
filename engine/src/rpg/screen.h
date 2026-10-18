#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../js/bind.h"
#include "../js/value.h"

namespace rpg {

using EaseFn = double (*)(double t, double b, double c, double d);

struct EasingSet {
    std::string general, x, y, scale_x, scale_y, opacity, red, green, blue, gray;
    bool any = false;
    double apply(const std::string& prop, double t, double b, double c, double d) const;
};

namespace curves {
void set_next(const std::string& fn, const std::string& prop);
bool pluck(EasingSet& out);
EaseFn by_name(const std::string& name);
}

struct FrameRect {
    double x = 0, y = 0, w = 0, h = 0;
};

struct FrameAnim {
    struct F {
        int frame = 0;
        int delay = 0;
        int max_delay = 0;
    };
    std::vector<F> frames;
    double loops = 0;
    int index = 0;
};

struct FrameData {
    std::vector<FrameRect> frames;
    int frame_index = 0;
    bool refresh = true;
    bool has_anim = false;
    FrameAnim anim;
};

class GamePicture : public js::JsObject {
public:
    GamePicture();
    js::HostClass* js_class() const override;
    std::string name;
    int origin = 0;
    double x = 0, y = 0, scale_x = 100, scale_y = 100, opacity = 255;
    int blend_mode = 0;
    double target_x = 0, target_y = 0, target_scale_x = 100, target_scale_y = 100, target_opacity = 255;
    int duration = 0;
    bool has_tone = false;
    double tone[4] = {0, 0, 0, 0};
    double tone_target[4] = {0, 0, 0, 0};
    int tone_duration = 0;
    double angle_ = 0;
    double rotation_speed = 0;
    int max_frame = 0, frame = 0;
    double max_angle = 0;
    bool pinned_to_map = false;
    bool follow_shake = false;
    bool use_parallax = false;
    FrameRect parallax_rect;
    std::unique_ptr<FrameData> frame_data;
    bool move_curves = false;
    EasingSet move_ease;
    double move_from[5] = {0, 0, 0, 0, 0};
    bool move_active[5] = {false, false, false, false, false};
    double move_to[5] = {0, 0, 0, 0, 0};
    int move_total = 0;
    bool tone_curves = false;
    EasingSet tone_ease;
    double tone_from[4] = {0, 0, 0, 0};
    double tone_to[4] = {0, 0, 0, 0};
    bool tone_active[4] = {false, false, false, false};
    int tone_total = 0;
    js::Val horror_filters;

    double angle() const;
    void init_basic();
    void init_target();
    void init_tone();
    void init_rotation();
    void show(const std::string& n, int org, double px, double py, double sx, double sy, double op, int blend);
    void move(int org, double px, double py, double sx, double sy, double op, int blend, int dur);
    void rotate(double speed) { rotation_speed = speed; }
    void tint(const double t[4], int dur);
    void erase();
    void update();
    void update_move();
    void update_tone();
    void update_rotation();
    void update_frame_animation();
    bool has_animation() const { return frame_data && frame_data->has_anim; }
    void clear_custom_frame() { frame_data.reset(); }
    void setup_parallax(double xs, double ys, double w, double h) {
        use_parallax = true;
        parallax_rect.x = xs;
        parallax_rect.y = ys;
        parallax_rect.w = w;
        parallax_rect.h = h;
    }
    void clear_parallax() {
        use_parallax = false;
        parallax_rect = FrameRect();
    }
    void setup_custom_frame(const std::vector<FrameRect>& frames);
    void set_custom_frame_animation(const FrameAnim& anim);
    void set_custom_frame_index(int index);
    bool needs_frame_refresh() const { return frame_data && frame_data->refresh; }
    FrameRect custom_frame() const;
    void set_rotation_with_frame(double angle, int frames) {
        max_frame = frames;
        frame = 0;
        max_angle = angle;
    }
    bool is_frame_set_mode() const { return max_frame > 0; }
};

class GameScreen : public js::JsObject {
public:
    GameScreen();
    js::HostClass* js_class() const override;
    double brightness = 255;
    int fade_out_duration = 0, fade_in_duration = 0;
    double tone[4] = {0, 0, 0, 0};
    double tone_target[4] = {0, 0, 0, 0};
    int tone_duration = 0;
    double flash_color[4] = {0, 0, 0, 0};
    int flash_duration = 0;
    int flash_wait = 0;
    void set_flash_wait(int v) { flash_wait = v; }
    double shake_power = 0, shake_speed = 0;
    int shake_duration = 0;
    int shake_direction = 1;
    double shake = 0;
    double shake_vector_x = 0, shake_vector_y = 0;
    double zoom_x = 0, zoom_y = 0, zoom_scale = 1, zoom_scale_target = 1;
    int zoom_duration = 0;
    int camera_duration = 0;
    std::string weather_type = "none";
    double weather_power = 0, weather_power_target = 0;
    int weather_duration = 0;
    std::string weather_power_target_text;
    double storm_thunder_count = 0;
    std::vector<std::unique_ptr<GamePicture>> pictures;
    double blur_strength = 0, blur_delta = 0;
    int blur_duration = 0;
    double zblur_strength = 0, zblur_delta = 0, zblur_nradius = 0, zblur_origin_x = 0, zblur_origin_y = 0;
    int zblur_duration = 0;
    double glow_power = 0, glow_delta = 0, glow_size = 0, glow_threshold = 0;
    int glow_duration = 0;
    double wave_amp = 0, wave_delta = 0, wave_length = 0, wave_origin_x = 0, wave_origin_y = 0;
    int wave_duration = 0, wave_initial_duration = 0;
    double dist_power = 0, dist_density = 0, dist_offset = 0, dist_delta[2] = {0, 0};
    int dist_duration = 0, dist_initial_duration = 0;
    js::Val horror_filters;

    void clear();
    void clear_fade();
    void clear_tone();
    void clear_flash();
    void clear_shake();
    void clear_zoom();
    void clear_weather();
    void clear_pictures() { pictures.clear(); }
    void clear_effects();
    void on_battle_start();
    void erase_battle_pictures();
    static int max_pictures() { return 100; }
    int real_picture_id(int id) const;
    GamePicture* picture(int id);
    void start_fade_out(int d);
    void start_fade_in(int d);
    void start_tint(const double t[4], int d);
    void start_flash(const double c[4], int d);
    void start_shake(double power, double speed, int d);
    void start_zoom(double x, double y, double scale, int d);
    void set_zoom(double x, double y, double scale);
    void change_weather(const std::string& type, double power, int d);
    void update();
    void update_fade_out();
    void update_fade_in();
    void update_tone();
    void update_flash();
    void update_shake();
    void update_shake_base();
    void update_zoom();
    void update_weather();
    void update_pictures();
    void update_effects();
    void update_battle_camera();
    void start_flash_for_damage();
    void show_picture(int id, const std::string& name, int origin, double x, double y, double sx, double sy, double op, int blend);
    void move_picture(int id, int origin, double x, double y, double sx, double sy, double op, int blend, int dur);
    void rotate_picture(int id, double speed);
    void tint_picture(int id, const double t[4], int dur);
    void erase_picture(int id);
    void start_battle_zoom(double scale, int d) {
        zoom_scale_target = scale;
        zoom_duration = d;
    }
    bool is_zooming() const { return zoom_duration > 0; }
    void set_camera_duration(int d) { camera_duration = d; }
    double effects_intensity() const;
    void start_blur(double power, int d);
    void start_zblur(double power, int d, double nradius, double ox, double oy);
    void start_glow(double power, double size, double threshold, int d);
    void start_wave(double amp, double len, int d, double ox, double oy);
    void start_distortion(double power, double density, double offset, int d);
};

}
