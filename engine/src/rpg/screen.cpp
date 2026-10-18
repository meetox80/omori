#include "screen.h"

#include <cmath>
#include <cstring>

#include "../js/random.h"
#include "config.h"
#include "data.h"
#include "game.h"
#include "hooks.h"

namespace rpg {

static double e_linear(double t, double b, double c, double d) { return c * (t / d) + b; }
static double e_quad_in(double t, double b, double c, double d) { t /= d; return c * t * t + b; }
static double e_quad_out(double t, double b, double c, double d) { t /= d; return -c * t * (t - 2) + b; }
static double e_quad_inout(double t, double b, double c, double d) {
    t /= d / 2;
    if (t < 1) return c / 2 * t * t + b;
    t -= 1;
    return -c / 2 * (t * (t - 2) - 1) + b;
}
static double e_cubic_in(double t, double b, double c, double d) { t /= d; return c * t * t * t + b; }
static double e_cubic_out(double t, double b, double c, double d) { t = t / d - 1; return c * (t * t * t + 1) + b; }
static double e_cubic_inout(double t, double b, double c, double d) {
    t /= d / 2;
    if (t < 1) return c / 2 * t * t * t + b;
    t -= 2;
    return c / 2 * (t * t * t + 2) + b;
}
static double e_quart_in(double t, double b, double c, double d) { t /= d; return c * t * t * t * t + b; }
static double e_quart_out(double t, double b, double c, double d) { t = t / d - 1; return -c * (t * t * t * t - 1) + b; }
static double e_quart_inout(double t, double b, double c, double d) {
    t /= d / 2;
    if (t < 1) return c / 2 * t * t * t * t + b;
    t -= 2;
    return -c / 2 * (t * t * t * t - 2) + b;
}
static double e_quint_in(double t, double b, double c, double d) { t /= d; return c * t * t * t * t * t + b; }
static double e_quint_out(double t, double b, double c, double d) { t = t / d - 1; return c * (t * t * t * t * t + 1) + b; }
static double e_quint_inout(double t, double b, double c, double d) {
    t /= d / 2;
    if (t < 1) return c / 2 * t * t * t * t * t + b;
    t -= 2;
    return c / 2 * (t * t * t * t * t + 2) + b;
}
static double e_sine_in(double t, double b, double c, double d) { return -c * std::cos(t / d * (M_PI / 2)) + c + b; }
static double e_sine_out(double t, double b, double c, double d) { return c * std::sin(t / d * (M_PI / 2)) + b; }
static double e_sine_inout(double t, double b, double c, double d) { return -c / 2 * (std::cos(M_PI * t / d) - 1) + b; }
static double e_expo_in(double t, double b, double c, double d) { return t == 0 ? b : c * std::pow(2, 10 * (t / d - 1)) + b; }
static double e_expo_out(double t, double b, double c, double d) { return t == d ? b + c : c * (-std::pow(2, -10 * t / d) + 1) + b; }
static double e_expo_inout(double t, double b, double c, double d) {
    if (t == 0) return b;
    if (t == d) return b + c;
    t /= d / 2;
    if (t < 1) return c / 2 * std::pow(2, 10 * (t - 1)) + b;
    t -= 1;
    return c / 2 * (-std::pow(2, -10 * t) + 2) + b;
}
static double e_circ_in(double t, double b, double c, double d) { t /= d; return -c * (std::sqrt(1 - t * t) - 1) + b; }
static double e_circ_out(double t, double b, double c, double d) { t = t / d - 1; return c * std::sqrt(1 - t * t) + b; }
static double e_circ_inout(double t, double b, double c, double d) {
    t /= d / 2;
    if (t < 1) return -c / 2 * (std::sqrt(1 - t * t) - 1) + b;
    t -= 2;
    return c / 2 * (std::sqrt(1 - t * t) + 1) + b;
}
static double elastic_s(double a, double c, double p) {
    if (a < std::fabs(c)) return p / 4;
    return p / (2 * M_PI) * std::asin(c / a);
}
static double e_elastic_in(double t, double b, double c, double d) {
    double a = c;
    if (t == 0) return b;
    t /= d;
    if (t == 1) return b + c;
    double p = d * .3;
    double s = elastic_s(a, c, p);
    if (a < std::fabs(c)) a = c;
    t -= 1;
    return -(a * std::pow(2, 10 * t) * std::sin((t * d - s) * (2 * M_PI) / p)) + b;
}
static double e_elastic_out(double t, double b, double c, double d) {
    double a = c;
    if (t == 0) return b;
    t /= d;
    if (t == 1) return b + c;
    double p = d * .3;
    double s = elastic_s(a, c, p);
    if (a < std::fabs(c)) a = c;
    return a * std::pow(2, -10 * t) * std::sin((t * d - s) * (2 * M_PI) / p) + c + b;
}
static double e_elastic_inout(double t, double b, double c, double d) {
    double a = c;
    if (t == 0) return b;
    t /= d / 2;
    if (t == 2) return b + c;
    double p = d * (.3 * 1.5);
    double s = elastic_s(a, c, p);
    if (a < std::fabs(c)) a = c;
    if (t < 1) {
        t -= 1;
        return -.5 * (a * std::pow(2, 10 * t) * std::sin((t * d - s) * (2 * M_PI) / p)) + b;
    }
    t -= 1;
    return a * std::pow(2, -10 * t) * std::sin((t * d - s) * (2 * M_PI) / p) * .5 + c + b;
}
static double e_back_in(double t, double b, double c, double d) {
    double s = 1.70158;
    t /= d;
    return c * t * t * ((s + 1) * t - s) + b;
}
static double e_back_out(double t, double b, double c, double d) {
    double s = 1.70158;
    t = t / d - 1;
    return c * (t * t * ((s + 1) * t + s) + 1) + b;
}
static double e_back_inout(double t, double b, double c, double d) {
    double s = 1.70158 * 1.525;
    t /= d / 2;
    if (t < 1) return c / 2 * (t * t * ((s + 1) * t - s)) + b;
    t -= 2;
    return c / 2 * (t * t * ((s + 1) * t + s) + 2) + b;
}
static double e_bounce_out(double t, double b, double c, double d) {
    t /= d;
    if (t < (1 / 2.75)) return c * (7.5625 * t * t) + b;
    if (t < (2 / 2.75)) {
        t -= 1.5 / 2.75;
        return c * (7.5625 * t * t + .75) + b;
    }
    if (t < (2.5 / 2.75)) {
        t -= 2.25 / 2.75;
        return c * (7.5625 * t * t + .9375) + b;
    }
    t -= 2.625 / 2.75;
    return c * (7.5625 * t * t + .984375) + b;
}
static double e_bounce_in(double t, double b, double c, double d) { return c - e_bounce_out(d - t, 0, c, d) + b; }
static double e_bounce_inout(double t, double b, double c, double d) {
    if (t < d / 2) return e_bounce_in(t * 2, 0, c, d) * .5 + b;
    return e_bounce_out(t * 2 - d, 0, c, d) * .5 + c * .5 + b;
}

namespace curves {

static EasingSet g_next;

EaseFn by_name(const std::string& n) {
    static const struct {
        const char* name;
        EaseFn fn;
    } T[] = {{"QuadIn", e_quad_in},           {"QuadOut", e_quad_out},       {"QuadInOut", e_quad_inout},     {"CubicIn", e_cubic_in},
             {"CubicOut", e_cubic_out},       {"CubicInOut", e_cubic_inout}, {"QuartIn", e_quart_in},         {"QuartOut", e_quart_out},
             {"QuartInOut", e_quart_inout},   {"QuintIn", e_quint_in},       {"QuintOut", e_quint_out},       {"QuintInOut", e_quint_inout},
             {"SineIn", e_sine_in},           {"SineOut", e_sine_out},       {"SineInOut", e_sine_inout},     {"ExpoIn", e_expo_in},
             {"ExpoOut", e_expo_out},         {"ExpoInOut", e_expo_inout},   {"CircIn", e_circ_in},           {"CircOut", e_circ_out},
             {"CircInOut", e_circ_inout},     {"ElasticIn", e_elastic_in},   {"ElasticOut", e_elastic_out},   {"ElasticInOut", e_elastic_inout},
             {"BackIn", e_back_in},           {"BackOut", e_back_out},       {"BackInOut", e_back_inout},     {"BounceIn", e_bounce_in},
             {"BounceOut", e_bounce_out},     {"BounceInOut", e_bounce_inout}, {"Linear", e_linear},          {nullptr, nullptr}};
    for (int i = 0; T[i].name; i++) {
        if (n == T[i].name) return T[i].fn;
    }
    return nullptr;
}

void set_next(const std::string& fn, const std::string& prop0) {
    if (!by_name(fn)) return;
    std::string prop = prop0.empty() ? "general" : prop0;
    g_next.any = true;
    if (prop == "general") g_next.general = fn;
    else if (prop == "x") g_next.x = fn;
    else if (prop == "y") g_next.y = fn;
    else if (prop == "scaleX") g_next.scale_x = fn;
    else if (prop == "scaleY") g_next.scale_y = fn;
    else if (prop == "opacity") g_next.opacity = fn;
    else if (prop == "red") g_next.red = fn;
    else if (prop == "green") g_next.green = fn;
    else if (prop == "blue") g_next.blue = fn;
    else if (prop == "gray") g_next.gray = fn;
}

bool pluck(EasingSet& out) {
    if (!g_next.any) return false;
    out = g_next;
    g_next = EasingSet();
    return true;
}

}

double EasingSet::apply(const std::string& prop, double t, double b, double c, double d) const {
    const std::string* pick = nullptr;
    if (prop == "x" && !x.empty()) pick = &x;
    else if (prop == "y" && !y.empty()) pick = &y;
    else if (prop == "scaleX" && !scale_x.empty()) pick = &scale_x;
    else if (prop == "scaleY" && !scale_y.empty()) pick = &scale_y;
    else if (prop == "opacity" && !opacity.empty()) pick = &opacity;
    else if (prop == "red" && !red.empty()) pick = &red;
    else if (prop == "green" && !green.empty()) pick = &green;
    else if (prop == "blue" && !blue.empty()) pick = &blue;
    else if (prop == "gray" && !gray.empty()) pick = &gray;
    else if (!general.empty()) pick = &general;
    EaseFn fn = pick ? curves::by_name(*pick) : e_linear;
    return (fn ? fn : e_linear)(t, b, c, d);
}

GamePicture::GamePicture() {
    init_basic();
    init_target();
    init_tone();
    init_rotation();
    horror_filters = js::make_object();
}

double GamePicture::angle() const {
    if (is_frame_set_mode()) return angle_ + max_angle * ((double)frame / max_frame);
    return angle_;
}

void GamePicture::init_basic() {
    name.clear();
    origin = 0;
    x = y = 0;
    scale_x = scale_y = 100;
    opacity = 255;
    blend_mode = 0;
    frame_data.reset();
    pinned_to_map = false;
    use_parallax = false;
    parallax_rect = FrameRect();
}

void GamePicture::init_target() {
    target_x = x;
    target_y = y;
    target_scale_x = scale_x;
    target_scale_y = scale_y;
    target_opacity = opacity;
    duration = 0;
}

void GamePicture::init_tone() {
    has_tone = false;
    std::memset(tone, 0, sizeof(tone));
    std::memset(tone_target, 0, sizeof(tone_target));
    tone_duration = 0;
}

void GamePicture::init_rotation() {
    angle_ = 0;
    rotation_speed = 0;
    max_frame = frame = 0;
    max_angle = 0;
}

void GamePicture::show(const std::string& n, int org, double px, double py, double sx, double sy, double op, int blend) {
    name = n;
    origin = org;
    x = px;
    y = py;
    scale_x = sx;
    scale_y = sy;
    opacity = op;
    blend_mode = blend;
    init_target();
    init_tone();
    init_rotation();
}

void GamePicture::move(int org, double px, double py, double sx, double sy, double op, int blend, int dur) {
    origin = org;
    target_x = px;
    target_y = py;
    target_scale_x = sx;
    target_scale_y = sy;
    target_opacity = op;
    blend_mode = blend;
    duration = dur;
    EasingSet ease;
    move_curves = curves::pluck(ease);
    if (move_curves) {
        move_ease = ease;
        double from[5] = {x, y, scale_x, scale_y, opacity};
        double to[5] = {px, py, sx, sy, op};
        for (int i = 0; i < 5; i++) {
            move_from[i] = from[i];
            move_to[i] = to[i];
            move_active[i] = to[i] != from[i];
        }
        move_total = dur;
    }
}

void GamePicture::tint(const double t[4], int dur) {
    if (!has_tone) {
        has_tone = true;
        std::memset(tone, 0, sizeof(tone));
    }
    std::memcpy(tone_target, t, sizeof(tone_target));
    tone_duration = dur;
    if (tone_duration == 0) std::memcpy(tone, tone_target, sizeof(tone));
    EasingSet ease;
    tone_curves = curves::pluck(ease);
    if (tone_curves) {
        tone_ease = ease;
        for (int i = 0; i < 4; i++) {
            tone_from[i] = tone[i];
            tone_to[i] = t[i];
            tone_active[i] = t[i] != tone[i];
        }
        tone_total = dur;
    }
}

void GamePicture::erase() {
    name.clear();
    origin = 0;
    init_target();
    init_tone();
    init_rotation();
    horror_filters = js::make_object();
}

void GamePicture::update() {
    update_move();
    update_tone();
    update_rotation();
    update_frame_animation();
}

void GamePicture::update_move() {
    if (move_curves) {
        if (duration < 0) return;
        double time = move_total - duration;
        static const char* props[5] = {"x", "y", "scaleX", "scaleX", "opacity"};
        double* fields[5] = {&x, &y, &scale_x, &scale_y, &opacity};
        for (int i = 0; i < 5; i++) {
            if (move_active[i]) *fields[i] = move_ease.apply(props[i], time, move_from[i], move_to[i] - move_from[i], move_total);
        }
        duration--;
        return;
    }
    if (duration > 0) {
        double d = duration;
        x = (x * (d - 1) + target_x) / d;
        y = (y * (d - 1) + target_y) / d;
        scale_x = (scale_x * (d - 1) + target_scale_x) / d;
        scale_y = (scale_y * (d - 1) + target_scale_y) / d;
        opacity = (opacity * (d - 1) + target_opacity) / d;
        duration--;
    }
}

void GamePicture::update_tone() {
    if (tone_curves) {
        if (tone_duration < 0) return;
        double time = tone_total - tone_duration;
        static const char* keys[4] = {"red", "green", "blue", "gray"};
        for (int i = 0; i < 4; i++) {
            if (tone_active[i]) tone[i] = tone_ease.apply(keys[i], time, tone_from[i], tone_to[i] - tone_from[i], tone_total);
        }
        tone_duration--;
        return;
    }
    if (tone_duration > 0) {
        double d = tone_duration;
        for (int i = 0; i < 4; i++) tone[i] = (tone[i] * (d - 1) + tone_target[i]) / d;
        tone_duration--;
    }
}

void GamePicture::update_rotation() {
    if (is_frame_set_mode()) {
        if (++frame == max_frame) {
            angle_ += max_angle;
            max_frame = frame = 0;
            max_angle = 0;
        }
        return;
    }
    if (rotation_speed != 0) angle_ += rotation_speed / 2;
}

void GamePicture::setup_custom_frame(const std::vector<FrameRect>& frames) {
    frame_data.reset(new FrameData());
    frame_data->frames = frames;
}

void GamePicture::set_custom_frame_animation(const FrameAnim& anim) {
    if (!frame_data) return;
    frame_data->has_anim = true;
    frame_data->anim = anim;
    if (anim.index >= 0 && anim.index < (int)anim.frames.size()) set_custom_frame_index(anim.frames[anim.index].frame);
}

void GamePicture::set_custom_frame_index(int index) {
    if (!frame_data) return;
    frame_data->frame_index = index;
    frame_data->refresh = true;
}

FrameRect GamePicture::custom_frame() const {
    if (!frame_data || frame_data->frame_index < 0 || frame_data->frame_index >= (int)frame_data->frames.size()) return FrameRect();
    return frame_data->frames[frame_data->frame_index];
}

void GamePicture::update_frame_animation() {
    if (!frame_data || !frame_data->has_anim) return;
    FrameAnim& anim = frame_data->anim;
    if (anim.frames.empty()) return;
    FrameAnim::F& fa = anim.frames[anim.index];
    if (fa.delay <= 0) {
        fa.delay = fa.max_delay;
        int max_frames = (int)anim.frames.size();
        if (anim.index >= max_frames - 1) {
            if (anim.loops > 0) {
                anim.loops--;
                anim.index = (anim.index + 1) % max_frames;
            } else {
                frame_data->has_anim = false;
            }
        } else {
            anim.index = (anim.index + 1) % max_frames;
        }
        set_custom_frame_index(anim.frames[anim.index].frame);
    } else {
        fa.delay--;
    }
}

static int g_shake_type = 3;

int shake_type() { return g_shake_type; }
void set_shake_type(int t) { g_shake_type = t; }

GameScreen::GameScreen() {
    std::string st = data::plugin_param("Aries001_AnimationScreenEffects", "Shake Type", "Standard");
    for (auto& c : st) c = (char)toupper((unsigned char)c);
    if (st == "STANDARD") g_shake_type = 0;
    else if (st == "RANDOM") g_shake_type = 1;
    else if (st == "HORIZONTAL") g_shake_type = 2;
    else if (st == "VERTICAL") g_shake_type = 3;
    clear();
}

void GameScreen::clear() {
    clear_fade();
    clear_tone();
    clear_flash();
    clear_shake();
    clear_zoom();
    clear_weather();
    clear_pictures();
    horror_filters = js::make_object();
    clear_effects();
}

void GameScreen::clear_fade() {
    brightness = 255;
    fade_out_duration = 0;
    fade_in_duration = 0;
}

void GameScreen::clear_tone() {
    std::memset(tone, 0, sizeof(tone));
    std::memset(tone_target, 0, sizeof(tone_target));
    tone_duration = 0;
}

void GameScreen::clear_flash() {
    std::memset(flash_color, 0, sizeof(flash_color));
    flash_duration = 0;
}

void GameScreen::clear_shake() {
    shake_power = 0;
    shake_speed = 0;
    shake_duration = 0;
    shake_direction = 1;
    shake = 0;
    shake_vector_x = shake_vector_y = 0;
}

void GameScreen::clear_zoom() {
    zoom_x = zoom_y = 0;
    zoom_scale = 1;
    zoom_scale_target = 1;
    zoom_duration = 0;
    camera_duration = 0;
}

void GameScreen::clear_weather() {
    weather_type = "none";
    weather_power = 0;
    weather_power_target = 0;
    weather_power_target_text.clear();
    weather_duration = 0;
    weather_screen_clear(*this);
}

void GameScreen::clear_effects() {
    blur_duration = 0;
    blur_strength = 0;
    blur_delta = 0;
    zblur_duration = 0;
    zblur_strength = 0;
    zblur_delta = 0;
    zblur_nradius = 0;
    zblur_origin_x = zblur_origin_y = 0;
    glow_duration = 0;
    glow_power = 0;
    glow_delta = 0;
    glow_size = 0;
    glow_threshold = 0;
    wave_duration = 0;
    wave_initial_duration = 0;
    wave_delta = 0;
    wave_amp = 0;
    wave_length = 0;
    wave_origin_x = wave_origin_y = 0;
    dist_power = 0;
    dist_delta[0] = dist_delta[1] = 0;
    dist_density = 0;
    dist_offset = 0;
    dist_duration = 0;
    dist_initial_duration = 0;
}

void GameScreen::on_battle_start() {
    clear_fade();
    clear_flash();
    clear_shake();
    clear_zoom();
    erase_battle_pictures();
    clear_effects();
}

void GameScreen::erase_battle_pictures() {
    if ((int)pictures.size() > max_pictures() + 1) pictures.resize((size_t)max_pictures() + 1);
}

int GameScreen::real_picture_id(int id) const { return party_in_battle() ? id + max_pictures() : id; }

GamePicture* GameScreen::picture(int id) {
    int real = real_picture_id(id);
    if (real < 0 || real >= (int)pictures.size()) return nullptr;
    return pictures[real].get();
}

void GameScreen::start_fade_out(int d) {
    fade_out_duration = d;
    fade_in_duration = 0;
}

void GameScreen::start_fade_in(int d) {
    fade_in_duration = d;
    fade_out_duration = 0;
}

void GameScreen::start_tint(const double t[4], int d) {
    std::memcpy(tone_target, t, sizeof(tone_target));
    tone_duration = d;
    if (d == 0) std::memcpy(tone, tone_target, sizeof(tone));
}

void GameScreen::start_flash(const double c[4], int d) {
    std::memcpy(flash_color, c, sizeof(flash_color));
    flash_duration = d;
}

void GameScreen::start_shake(double power, double speed, int d) {
    shake_power = power;
    shake_speed = speed;
    shake_duration = d;
    shake_vector_x = shake_vector_y = 0;
}

void GameScreen::start_zoom(double x, double y, double scale, int d) {
    zoom_x = x;
    zoom_y = y;
    zoom_scale_target = scale;
    zoom_duration = d;
}

void GameScreen::set_zoom(double x, double y, double scale) {
    zoom_x = x;
    zoom_y = y;
    zoom_scale = scale;
}

void GameScreen::change_weather(const std::string& type, double power, int d) {
    if (type != "none" || d == 0) weather_type = type;
    weather_power_target = type == "none" ? 0 : power;
    weather_power_target_text.clear();
    weather_duration = d;
    if (d == 0) weather_power = weather_power_target;
}

void GameScreen::update() {
    update_fade_out();
    update_fade_in();
    update_tone();
    update_flash();
    update_shake();
    update_zoom();
    update_weather();
    update_pictures();
    update_battle_camera();
    if (config().screen_effects_enabled) update_effects();
    filter_con_update();
}

void GameScreen::update_fade_out() {
    if (fade_out_duration > 0) {
        double d = fade_out_duration;
        brightness = (brightness * (d - 1)) / d;
        fade_out_duration--;
    }
}

void GameScreen::update_fade_in() {
    if (fade_in_duration > 0) {
        double d = fade_in_duration;
        brightness = (brightness * (d - 1) + 255) / d;
        fade_in_duration--;
    }
}

void GameScreen::update_tone() {
    if (tone_duration > 0) {
        double d = tone_duration;
        for (int i = 0; i < 4; i++) tone[i] = (tone[i] * (d - 1) + tone_target[i]) / d;
        tone_duration--;
    }
}

void GameScreen::update_flash() {
    if (flash_wait > 0) {
        flash_wait--;
        return;
    }
    if (flash_duration > 0) {
        double d = flash_duration;
        flash_color[3] *= (d - 1) / d;
        flash_duration--;
    }
}

void GameScreen::update_shake_base() {
    if (shake_duration > 0 || shake != 0) {
        double delta = (shake_power * shake_speed * shake_direction) / 10;
        if (shake_duration <= 1 && shake * (shake + delta) < 0) shake = 0;
        else shake += delta;
        if (shake > shake_power * 2) shake_direction = -1;
        if (shake < -shake_power * 2) shake_direction = 1;
        shake_duration--;
    }
}

static int rand_int(double max) { return (int)std::floor(max * rng::next()); }

void GameScreen::update_shake() {
    switch (g_shake_type) {
    case 1:
        if (shake_duration > 0 || shake != 0) {
            double sx = (rand_int(shake_power) - rand_int(shake_power)) * (std::min(shake_duration, 30) * 0.5);
            double sy = (rand_int(shake_power) - rand_int(shake_power)) * (std::min(shake_duration, 30) * 0.5);
            shake = 0;
            if (shake_duration <= 1) {
                shake_vector_x = shake_vector_y = 0;
            } else {
                shake_vector_x = sx;
                shake_vector_y = sy;
            }
            shake_duration--;
        }
        break;
    case 2:
    case 3:
        if (shake_duration > 0 || shake != 0) {
            double delta = (shake_power * (2 * shake_speed) * shake_direction) / 5;
            if (shake_duration <= 1 && shake * (shake + delta) < 0) shake = 0;
            else shake += delta;
            if (shake > shake_power * 2) shake_direction = -1;
            if (shake < -shake_power * 2) shake_direction = 1;
            shake_power *= 0.9;
            shake_duration--;
        }
        break;
    default: update_shake_base(); break;
    }
}

void GameScreen::update_zoom() {
    if (zoom_duration > 0) {
        double d = zoom_duration;
        zoom_scale = (zoom_scale * (d - 1) + zoom_scale_target) / d;
        zoom_duration--;
    }
}

void GameScreen::update_weather() {
    if (weather_duration > 0) {
        double d = weather_duration;
        if (weather_power_target_text.empty()) {
            weather_power = (weather_power * (d - 1) + weather_power_target) / d;
        } else {
            weather_power = js::string_to_number(js::number_to_string(weather_power * (d - 1)) + weather_power_target_text) / d;
        }
        weather_duration--;
        if (weather_duration == 0 && weather_power_target == 0 && weather_power_target_text.empty()) weather_type = "none";
    }
    weather_screen_update(*this);
}

void GameScreen::update_pictures() {
    for (size_t i = 0; i < pictures.size(); i++) {
        if (pictures[i]) pictures[i]->update();
    }
}

void GameScreen::update_effects() {
    if (blur_duration > 0) {
        blur_strength -= blur_delta;
        blur_duration--;
    } else {
        blur_strength = 0;
    }
    if (zblur_duration > 0) {
        zblur_strength -= zblur_delta;
        zblur_duration--;
    } else {
        zblur_strength = 0;
    }
    if (glow_duration > 0) {
        glow_power -= glow_delta;
        glow_duration--;
    } else {
        glow_power = 0;
    }
    if (wave_duration > 0) {
        wave_amp -= wave_delta;
        wave_duration--;
    } else {
        wave_amp = 0;
    }
    if (dist_duration > 0) {
        dist_duration--;
        dist_power -= dist_delta[0];
        dist_offset -= dist_delta[1];
    } else {
        dist_power = 0;
        dist_offset = 0;
    }
}

void GameScreen::update_battle_camera() {
    if (!party_in_battle()) return;
    double tx = battle_camera_x(), ty = battle_camera_y();
    if (camera_duration > 0) {
        double d = camera_duration;
        zoom_x = (zoom_x * (d - 1) + tx) / d;
        zoom_y = (zoom_y * (d - 1) + ty) / d;
        camera_duration--;
    } else {
        zoom_x = tx;
        zoom_y = ty;
    }
}

void GameScreen::start_flash_for_damage() {
    double c[4] = {255, 0, 0, 128};
    start_flash(c, 8);
}

void GameScreen::show_picture(int id, const std::string& name, int origin, double x, double y, double sx, double sy, double op, int blend) {
    int real = real_picture_id(id);
    if (real < 0) return;
    if ((int)pictures.size() <= real) pictures.resize((size_t)real + 1);
    pictures[real].reset(new GamePicture());
    pictures[real]->show(name, origin, x, y, sx, sy, op, blend);
}

void GameScreen::move_picture(int id, int origin, double x, double y, double sx, double sy, double op, int blend, int dur) {
    if (GamePicture* p = picture(id)) p->move(origin, x, y, sx, sy, op, blend, dur);
}

void GameScreen::rotate_picture(int id, double speed) {
    if (GamePicture* p = picture(id)) p->rotate(speed);
}

void GameScreen::tint_picture(int id, const double t[4], int dur) {
    if (GamePicture* p = picture(id)) p->tint(t, dur);
}

void GameScreen::erase_picture(int id) {
    int real = real_picture_id(id);
    if (real >= 0 && real < (int)pictures.size()) pictures[real].reset();
}

double GameScreen::effects_intensity() const { return 0.01 * config().screen_effects_intensity; }

void GameScreen::start_blur(double power, int d) {
    blur_duration = d;
    blur_strength = power * effects_intensity();
    blur_delta = d ? blur_strength / d : 0;
}

void GameScreen::start_zblur(double power, int d, double nradius, double ox, double oy) {
    zblur_duration = d;
    zblur_strength = power * effects_intensity();
    zblur_delta = d ? zblur_strength / d : 0;
    zblur_nradius = nradius;
    zblur_origin_x = ox;
    zblur_origin_y = oy;
}

void GameScreen::start_glow(double power, double size, double threshold, int d) {
    glow_duration = d;
    glow_power = power * effects_intensity();
    glow_delta = d ? glow_power / d : 0;
    glow_size = size;
    glow_threshold = threshold;
}

void GameScreen::start_wave(double amp, double len, int d, double ox, double oy) {
    wave_duration = d;
    wave_initial_duration = d;
    wave_amp = amp * effects_intensity();
    wave_delta = d ? wave_amp / d : 0;
    wave_length = len;
    wave_origin_x = ox;
    wave_origin_y = oy;
}

void GameScreen::start_distortion(double power, double density, double offset, int d) {
    dist_power = power * effects_intensity();
    dist_density = density;
    dist_offset = offset;
    dist_duration = d;
    dist_delta[0] = d ? dist_power / d : 0;
    dist_delta[1] = d ? offset / d : 0;
    dist_initial_duration = d;
}

}
