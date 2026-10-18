#include "../rpg/perf.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "../core/image.h"
#include "../js/random.h"
#include "../js/value.h"
#include "../rpg/audio_manager.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/hooks.h"
#include "../rpg/screen.h"
#include "../rpg/state.h"
#include "map_effects.h"
#include "ui_util.h"

namespace {

constexpr const char* kPlugin = "Aries003_WeatherControl";
constexpr double kGraphicsWidth = 640;
constexpr double kGraphicsHeight = 480;

struct AriesConfig {
    std::string rain_image, storm_image, snow_image, leaf_image, heat_image, mystic_image;
    double rain_min = 0, rain_max = 0, rain_life = 0, rain_speed = 0, rain_angle_c = 0, rain_alpha_c = 0, rain_angle_v = 0, rain_alpha_v = 0,
           rain_size_v = 0;
    double storm_min = 0, storm_max = 0, storm_life = 0, storm_speed = 0, storm_angle_c = 0, storm_alpha_c = 0, storm_angle_v = 0,
           storm_alpha_v = 0, storm_size_v = 0;
    bool storm_thunder = false, storm_thunder_b = false;
    std::string thunder_sound;
    double thunder_se_vol = 0, thunder_se_pitch = 0, thunder_se_pitch_v = 0, thunder_wait = 0, thunder_wait_v = 0, thunder_flash = 0,
           thunder_flash_v = 0;
    double snow_min = 0, snow_max = 0, snow_life = 0, snow_alpha_c = 0, snow_alpha_v = 0, snow_size_v = 0;
    double leaf_min = 0, leaf_max = 0, leaf_life = 0, leaf_speed_v = 0, leaf_size_v = 0;
    double heat_min = 0, heat_max = 0, heat_life = 0, heat_speed_v = 0, heat_size_v = 0;
    double mystic_min = 0, mystic_max = 0, mystic_life = 0, mystic_alpha_c = 0, mystic_alpha_v = 0, mystic_size_v = 0;
};

std::string pstr(const char* key) { return data::plugin_param(kPlugin, key, ""); }
double pnum(const char* key) { return js::string_to_number(pstr(key)); }

AriesConfig& cfg() {
    static AriesConfig c;
    static bool loaded = false;
    if (loaded) return c;
    loaded = true;
    c.rain_image = pstr("Rain Image");
    c.rain_min = pnum("Raindrop Count Minimum");
    c.rain_max = pnum("Raindrop Count Maximum");
    c.rain_life = pnum("Raindrop Lifetime");
    c.rain_speed = pnum("Raindrop Speed");
    c.rain_angle_c = pnum("Raindrop Angle Base");
    c.rain_alpha_c = pnum("Raindrop Opacity Base");
    c.rain_angle_v = pnum("Raindrop Angle Variation");
    c.rain_alpha_v = pnum("Raindrop Opacity Variation");
    c.rain_size_v = pnum("Raindrop Size Variation");
    c.storm_image = pstr("Storm Image");
    c.storm_min = pnum("Storm Raindrop Count Minimum");
    c.storm_max = pnum("Storm Raindrop Count Maximum");
    c.storm_life = pnum("Storm Raindrop Lifetime");
    c.storm_speed = pnum("Storm Raindrop Speed");
    c.storm_angle_c = pnum("Storm Raindrop Angle Base");
    c.storm_alpha_c = pnum("Storm Raindrop Opacity Base");
    c.storm_angle_v = pnum("Storm Raindrop Angle Variation");
    c.storm_alpha_v = pnum("Storm Raindrop Opacity Variation");
    c.storm_size_v = pnum("Storm Raindrop Size Variation");
    c.storm_thunder = pstr("Storm Thunder") == "true";
    c.storm_thunder_b = pstr("Storm Thunder in Battle") == "true";
    c.thunder_sound = pstr("Thunder Sound");
    c.thunder_se_vol = pnum("Thunder Sound Volume");
    c.thunder_se_pitch = pnum("Thunder Sound Pitch");
    c.thunder_se_pitch_v = pnum("Thunder Sound Pitch Variation");
    c.thunder_wait = pnum("Thunder Wait Time");
    c.thunder_wait_v = pnum("Thunder Wait Time Variation");
    c.thunder_flash = pnum("Thunder Flash Power");
    c.thunder_flash_v = pnum("Thunder Flash Variation");
    c.snow_image = pstr("Snow Image");
    c.snow_min = pnum("Snow Count Minimum");
    c.snow_max = pnum("Snow Count Maximum");
    c.snow_life = pnum("Snowflake Lifetime");
    c.snow_alpha_c = pnum("Snowflake Opacity Base");
    c.snow_alpha_v = pnum("Snowflake Opacity Variation");
    c.snow_size_v = pnum("Snowflake Size Variation");
    c.leaf_image = pstr("Leaf Image");
    c.leaf_min = pnum("Leaf Count Minimum");
    c.leaf_max = pnum("Leaf Count Maximum");
    c.leaf_life = pnum("Leaf Lifetime");
    c.leaf_speed_v = pnum("Leaf Speed Variation");
    c.leaf_size_v = pnum("Leaf Size Variation");
    c.heat_image = pstr("Embers Image");
    c.heat_min = pnum("Embers Count Minimum");
    c.heat_max = pnum("Embers Count Maximum");
    c.heat_life = pnum("Embers Lifetime");
    c.heat_speed_v = pnum("Embers Speed Variation");
    c.heat_size_v = pnum("Embers Size Variation");
    c.mystic_image = pstr("Shine Image");
    c.mystic_min = pnum("Shine Count Minimum");
    c.mystic_max = pnum("Shine Count Maximum");
    c.mystic_life = pnum("Shine Lifetime");
    c.mystic_alpha_c = pnum("Shine Opacity Base");
    c.mystic_alpha_v = pnum("Shine Opacity Variation");
    c.mystic_size_v = pnum("Shine Size Variation");
    return c;
}

int g_image_generation = 0;

double random_int(double max) { return std::floor(max * rng::next()); }

std::string js_lower(const std::string& s) {
    std::string r = s;
    for (auto& ch : r) ch = (char)std::tolower((unsigned char)ch);
    return r;
}

enum WeatherKind { kNone, kRain, kStorm, kSnow, kLeaves, kEmbers, kShine, kOther };

WeatherKind kind_of(const std::string& t) {
    if (t == "none") return kNone;
    if (t == "rain") return kRain;
    if (t == "storm") return kStorm;
    if (t == "snow") return kSnow;
    if (t == "leaves") return kLeaves;
    if (t == "embers") return kEmbers;
    if (t == "shine") return kShine;
    return kOther;
}

class ToneBaker : public g2d::Sprite {
public:
    g2d::BitmapPtr bake(const g2d::BitmapPtr& src, const double tone[4]) {
        set_bitmap(src);
        set_color_tone(tone);
        build_tint();
        return tinted_;
    }
};

struct Particle {
    WeatherKind type = kNone;
    bool setup_done = false;
    double lifetime = 0, size = 0, anglev = 0, target_alpha = 0, snow_speed = 0, random_seed = 0;
    bool placed = false;
    double ax = 0, ay = 0;
    double x = 0, y = 0;
    double alpha = 1;
    double scale_x = 1, scale_y = 1, rotation = 0, anchor = 0;
    int blend = 0;
    double tone[3] = {0, 0, 0};
    g2d::BitmapPtr bitmap;
    g2d::BitmapPtr draw_bitmap;

    double opacity() const { return std::floor(alpha * 255); }
    void set_opacity(double v) { alpha = std::clamp(v, 0.0, 255.0) / 255.0; }
};

}

namespace ui {

struct WeatherLayer::Impl {
    std::vector<Particle> sprites;
    std::shared_ptr<g2d::ScreenSprite> dimmer;
    g2d::BitmapPtr rain_bitmap, storm_bitmap, snow_bitmap, leaf_bitmap, heat_bitmap, mystic_bitmap;
    std::map<std::tuple<const g2d::Bitmap*, int, int, int>, g2d::BitmapPtr> tone_cache;
    int generation = -1;

    static g2d::BitmapPtr picture(const std::string& name) {
        g2d::BitmapPtr b = load_picture(name);
        b->smooth = true;
        return b;
    }

    void create_bitmaps() {
        AriesConfig& c = cfg();
        tone_cache.clear();
        if (!c.rain_image.empty()) {
            rain_bitmap = picture(c.rain_image);
        } else {
            rain_bitmap = g2d::Bitmap::create(1, 60);
            rain_bitmap->fill_all("white");
        }
        if (!c.storm_image.empty()) {
            storm_bitmap = picture(c.storm_image);
        } else {
            storm_bitmap = g2d::Bitmap::create(2, 100);
            storm_bitmap->fill_all("white");
        }
        if (!c.snow_image.empty()) {
            snow_bitmap = picture(c.snow_image);
        } else {
            snow_bitmap = g2d::Bitmap::create(9, 9);
            snow_bitmap->draw_circle(4, 4, 4, "white");
        }
        if (!c.leaf_image.empty()) {
            leaf_bitmap = picture(c.leaf_image);
        } else {
            leaf_bitmap = g2d::Bitmap::create(9, 9);
            leaf_bitmap->draw_circle(4, 4, 4, "green");
        }
        if (!c.heat_image.empty()) {
            heat_bitmap = picture(c.heat_image);
        } else {
            heat_bitmap = g2d::Bitmap::create(7, 7);
            heat_bitmap->draw_circle(3, 3, 3, "black");
        }
        if (!c.mystic_image.empty()) {
            mystic_bitmap = picture(c.mystic_image);
        } else {
            mystic_bitmap = g2d::Bitmap::create(9, 9);
            mystic_bitmap->draw_circle(4, 4, 4, "white");
            mystic_bitmap->rotate_hue((int)random_int(359));
        }
        generation = g_image_generation;
    }

    g2d::BitmapPtr toned(const g2d::BitmapPtr& src, const double tone[3]) {
        int r = (int)std::floor(tone[0] + 0.5), g = (int)std::floor(tone[1] + 0.5), b = (int)std::floor(tone[2] + 0.5);
        if (!src || (r == 0 && g == 0 && b == 0)) return src;
        auto key = std::make_tuple(static_cast<const g2d::Bitmap*>(src.get()), r, g, b);
        auto it = tone_cache.find(key);
        if (it != tone_cache.end()) return it->second;
        double t[4] = {(double)r, (double)g, (double)b, 0};
        ToneBaker baker;
        g2d::BitmapPtr out = baker.bake(src, t);
        tone_cache.emplace(key, out);
        return out;
    }

    static void set_up(Particle& s, WeatherKind type, double life, double size, double getangle, double alpha, double snow_speed) {
        s.type = type;
        s.lifetime = life + random_int(life * 0.25);
        s.size = size;
        s.anglev = getangle;
        s.target_alpha = alpha;
        s.snow_speed = snow_speed;
        s.random_seed = random_int(16777216);
        if (type == kShine) {
            s.scale_x = s.scale_y = 0;
            s.anchor = 0.5;
            s.blend = 1;
            double r = random_int(2) * -255;
            double g = random_int(2) * -255;
            double b = random_int(2) * -255;
            s.tone[0] = r;
            s.tone[1] = g;
            s.tone[2] = b;
        } else if (type == kLeaves) {
            s.scale_x = s.scale_y = 1;
            s.anchor = 0.5;
            s.blend = 0;
            s.tone[0] = s.tone[1] = s.tone[2] = 0;
        } else if (type == kEmbers) {
            s.scale_x = s.scale_y = 1;
            s.anchor = 0.5;
            s.blend = 0;
            s.tone[0] = 255;
            s.tone[1] = 200;
            s.tone[2] = 0;
        } else {
            s.scale_x = s.scale_y = 1;
            s.anchor = 0;
            s.blend = 0;
            s.tone[0] = s.tone[1] = s.tone[2] = 0;
        }
        s.setup_done = true;
    }

    static void setup_for_type(Particle& s, WeatherKind type) {
        AriesConfig& c = cfg();
        switch (type) {
        case kRain: {
            double life = c.rain_life;
            double size = 0.01 * ((100 - c.rain_size_v) + random_int(1 + c.rain_size_v * 2));
            double getangle = (c.rain_angle_c - c.rain_angle_v) + random_int(1 + c.rain_angle_v * 2);
            double alpha = (c.rain_alpha_c - c.rain_alpha_v) + random_int(1 + c.rain_alpha_v * 2);
            set_up(s, kRain, life, size, getangle, alpha, 0);
            s.blend = 0;
            break;
        }
        case kStorm: {
            double life = c.storm_life;
            double size = 0.01 * ((100 - c.storm_size_v) + random_int(1 + c.storm_size_v * 2));
            double getangle = (c.storm_angle_c - c.storm_angle_v) + random_int(1 + c.storm_angle_v * 2);
            double alpha = (c.storm_alpha_c - c.storm_alpha_v) + random_int(1 + c.storm_alpha_v * 2);
            set_up(s, kStorm, life, size, getangle, alpha, 0);
            s.blend = 0;
            break;
        }
        case kSnow: {
            double life = c.snow_life;
            double size = 0.01 * ((100 - c.snow_size_v) + random_int(1 + c.snow_size_v * 2));
            double getangle = (-1 + random_int(3)) * 0.267;
            double alpha = (c.snow_alpha_c - c.snow_alpha_v) + random_int(1 + c.snow_alpha_v * 2);
            double snowspeed = 1.33 + random_int(2);
            set_up(s, kSnow, life, size, getangle, alpha, snowspeed);
            s.blend = 0;
            break;
        }
        case kLeaves: {
            double life = c.leaf_life;
            double size = 0.007 * ((100 - c.leaf_size_v) + random_int(1 + c.leaf_size_v * 2));
            double getangle = c.leaf_speed_v;
            double snowspeed = 0.83 + random_int(2);
            set_up(s, kLeaves, life, size, getangle, 255, snowspeed);
            s.blend = 0;
            break;
        }
        case kEmbers: {
            double life = c.heat_life;
            double size = 0.007 * ((100 - c.heat_size_v) + random_int(1 + c.heat_size_v * 2));
            double getangle = c.heat_speed_v;
            double snowspeed = 0.83 + random_int(2);
            set_up(s, kEmbers, life, size, getangle, 255, snowspeed);
            s.blend = 0;
            break;
        }
        case kShine: {
            double life = c.mystic_life;
            double size = 0.007 * ((100 - c.mystic_size_v) + random_int(1 + c.mystic_size_v * 2));
            double alpha = (c.mystic_alpha_c - c.mystic_alpha_v) + random_int(1 + c.mystic_alpha_v * 2);
            double snowspeed = 0.25 * (2 + random_int(3));
            set_up(s, kShine, life, size, 12, alpha, snowspeed);
            s.rotation = rng::next();
            s.set_opacity(0);
            break;
        }
        default:
            break;
        }
    }

    double max_sprite_count(WeatherKind type, double power) const {
        AriesConfig& c = cfg();
        switch (type) {
        case kRain: return c.rain_min + (c.rain_max * power * 0.1);
        case kStorm: return c.storm_min + (c.storm_max * power * 0.1);
        case kSnow: return c.snow_min + (c.snow_max * power * 0.1);
        case kLeaves: return c.leaf_min + (c.leaf_max * power * 0.1);
        case kEmbers: return c.heat_min + (c.heat_max * power * 0.1);
        case kShine: return c.mystic_min + (c.mystic_max * power * 0.1);
        default: return 0;
        }
    }

    void add_sprite(WeatherKind type) {
        sprites.emplace_back();
        Particle& s = sprites.back();
        setup_for_type(s, type);
        s.set_opacity(0);
    }

    static void update_particle_self(Particle& s) {
        if (!s.setup_done) return;
        if (s.lifetime > 0) {
            if (s.type == kSnow) {
                s.set_opacity(std::min(s.opacity() + 24, s.target_alpha));
                s.scale_x = s.size;
                s.scale_y = s.size;
            } else if (s.type == kShine) {
                s.set_opacity(std::min(s.opacity() + 48, s.target_alpha));
                s.scale_x += 0.09;
                s.scale_y += 0.09;
            } else if (s.type == kEmbers) {
                s.set_opacity(std::min(s.opacity() + 32, s.target_alpha));
                s.tone[0] *= 0.98;
                s.tone[1] *= 0.95;
                s.scale_x = s.size;
                s.scale_y = s.size;
            } else {
                s.set_opacity(std::min(s.opacity() + 72, s.target_alpha));
                s.scale_x = s.size;
                s.scale_y = s.size;
            }
            s.lifetime -= 1;
            s.random_seed -= 1;
        } else {
            if (s.type == kSnow) {
                s.set_opacity(s.opacity() - 16);
            } else if (s.type == kShine) {
                s.set_opacity(s.opacity() - 15);
                s.scale_x -= 0.09;
                s.scale_y -= 0.09;
            } else if (s.type == kLeaves || s.type == kEmbers) {
                s.scale_x -= 0.01;
                s.scale_y -= 0.01;
                s.set_opacity(s.opacity() - 8);
            } else {
                s.set_opacity(s.opacity() - 48);
            }
        }
    }

    static void velocity(const Particle& s, double speed, double& vx, double& vy) {
        float rad = (float)s.anglev * (float)(M_PI / 180);
        double speed_x = speed * std::cos(rad);
        double speed_y = -1 * speed * std::sin(rad);
        vx = speed_y;
        vy = speed_x;
    }

    void assign_bitmap(Particle& s, const g2d::BitmapPtr& b) {
        if (s.bitmap != b) s.bitmap = b;
    }

    void reborn(Particle& s, WeatherKind type, double ox, double oy) {
        s.ax = random_int(kGraphicsWidth + 500) - 200 + ox;
        s.ay = random_int(kGraphicsHeight + 600) - 400 + oy;
        s.placed = true;
        setup_for_type(s, type);
    }

    void update_sprite(Particle& s, WeatherKind type, double ox, double oy) {
        AriesConfig& c = cfg();
        update_particle_self(s);
        switch (type) {
        case kRain: {
            assign_bitmap(s, rain_bitmap);
            s.rotation = s.anglev * (M_PI / 180);
            double vx, vy;
            velocity(s, c.rain_speed, vx, vy);
            s.ax += vx;
            s.ay += vy;
            break;
        }
        case kStorm: {
            assign_bitmap(s, storm_bitmap);
            s.rotation = s.anglev * (M_PI / 180);
            double vx, vy;
            velocity(s, c.storm_speed, vx, vy);
            s.ax += vx;
            s.ay += vy;
            break;
        }
        case kSnow:
            assign_bitmap(s, snow_bitmap);
            s.ax += s.anglev;
            s.ay += s.snow_speed;
            break;
        case kLeaves:
            assign_bitmap(s, leaf_bitmap);
            s.rotation += 0.025 * s.anglev;
            s.ax += 2 * std::sin(0.0078f * (float)s.random_seed);
            s.ay += s.snow_speed;
            break;
        case kEmbers:
            assign_bitmap(s, heat_bitmap);
            s.ax += 2 * std::sin(0.0139f * (float)s.random_seed);
            s.ay -= s.snow_speed;
            break;
        case kShine:
            assign_bitmap(s, mystic_bitmap);
            break;
        default:
            break;
        }
        if (s.lifetime <= 0 && s.opacity() <= 6) reborn(s, type, ox, oy);
    }
};

WeatherLayer::WeatherLayer() : impl_(new Impl()) {
    impl_->create_bitmaps();
    impl_->dimmer = std::make_shared<g2d::ScreenSprite>();
    impl_->dimmer->set_color(255, 255, 255);
    add_child(impl_->dimmer);
}

WeatherLayer::~WeatherLayer() = default;

void WeatherLayer::update() {
    rpg::perf::Scope ps(rpg::perf::Weather);
    Impl& im = *impl_;
    if (im.generation != g_image_generation) im.create_bitmaps();
    im.dimmer->set_opacity(std::floor(power * 6));
    WeatherKind k = kind_of(type);
    double max_sprites = im.max_sprite_count(k, power);
    if ((double)im.sprites.size() < max_sprites) im.add_sprite(k);
    if ((double)im.sprites.size() > max_sprites) im.sprites.pop_back();
    for (Particle& s : im.sprites) {
        im.update_sprite(s, k, origin_x, origin_y);
        s.x = s.ax - origin_x;
        s.y = s.ay - origin_y;
        s.draw_bitmap = im.toned(s.bitmap, s.tone);
    }
}

void WeatherLayer::render(const g2d::RenderState& rs) {
    render_children(rs);
    for (const Particle& s : impl_->sprites) {
        if (!s.placed || s.alpha <= 0 || !s.draw_bitmap) continue;
        g2d::Bitmap* b = s.draw_bitmap.get();
        double w = s.bitmap ? s.bitmap->width() : 0, h = s.bitmap ? s.bitmap->height() : 0;
        if (w <= 0 || h <= 0) continue;
        double alpha = rs.alpha * s.alpha;
        if (alpha <= 0) continue;
        b->touch();
        Image* img = b->image();
        if (!img) continue;
        g2d::Mat local;
        float rot = (float)s.rotation;
        double cr = std::cos(rot), sr = std::sin(rot);
        local.a = cr * s.scale_x;
        local.b = sr * s.scale_x;
        local.c = -sr * s.scale_y;
        local.d = cr * s.scale_y;
        local.tx = s.x;
        local.ty = s.y;
        g2d::Mat off;
        off.tx = -s.anchor * w;
        off.ty = -s.anchor * h;
        g2d::Mat m = rs.world.mul(local).mul(off);
        bool smooth = s.bitmap ? s.bitmap->smooth : false;
        g2d::draw_image(img, 0, 0, w, h, m, alpha, s.blend, smooth, rs);
    }
}

}

namespace rpg {

void weather_plugin_command(const std::string& command, const std::vector<std::string>& args) {
    auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string("undefined"); };
    AriesConfig& c = cfg();
    if (command == "AriesToggleStormThunder") {
        c.storm_thunder = arg(0) == "true";
    } else if (command == "AriesWeather") {
        GameScreen& s = game_screen();
        std::string type = js_lower(arg(0));
        if (type != "none") s.weather_type = type;
        if (type == "none") {
            s.weather_power_target = 0;
            s.weather_power_target_text.clear();
        } else {
            std::string power = arg(1);
            s.weather_power_target = js::string_to_number(power);
            s.weather_power_target_text = power;
        }
        double d = args.size() > 2 ? js::string_to_number(args[2]) : NAN;
        s.weather_duration = d > 0 ? (int)std::ceil(d) : 0;
    } else if (command == "AriesSetWeatherImage") {
        std::string weathertype = js_lower(arg(0));
        std::string image = js_lower(arg(1));
        if (weathertype == "rain") c.rain_image = image;
        else if (weathertype == "storm") c.storm_image = image;
        else if (weathertype == "snow") c.snow_image = image;
        else if (weathertype == "leaves") c.leaf_image = image;
        else if (weathertype == "embers") c.heat_image = image;
        else if (weathertype == "shine") c.mystic_image = image;
        g_image_generation++;
    }
}

void weather_screen_clear(GameScreen& screen) {
    AriesConfig& c = cfg();
    screen.storm_thunder_count = 4 * (c.thunder_wait + c.thunder_wait_v);
}

static void update_storm(GameScreen& screen) {
    AriesConfig& c = cfg();
    screen.storm_thunder_count -= (1 + (0.5 * screen.weather_power));
    if (screen.storm_thunder_count <= 0) {
        double alpha = std::min((c.thunder_flash - c.thunder_flash_v) + random_int(1 + c.thunder_flash_v * 2), 255.0);
        double color[4] = {255, 255, 255, alpha};
        screen.start_flash(color, 4);
        screen.start_glow(0.1 * std::min(2.0, 10.0), 0.1 * std::min(3.0, 10.0), 0.75 * 0.01, 18);
        screen.storm_thunder_count = 4 * ((c.thunder_wait - c.thunder_wait_v) + random_int(1 + c.thunder_wait_v * 2));
        AudioObj se;
        se.name = c.thunder_sound;
        se.pan = 0;
        se.pitch = (c.thunder_se_pitch - c.thunder_se_pitch_v) + random_int(1 + c.thunder_se_pitch_v * 2);
        se.volume = c.thunder_se_vol;
        se.valid = true;
        audio_manager::play_static_se(se);
    }
}

void weather_screen_update(GameScreen& screen) {
    if (screen.weather_type != "storm") return;
    AriesConfig& c = cfg();
    bool in_battle = party_in_battle();
    if (c.storm_thunder && !in_battle) {
        update_storm(screen);
    } else if (c.storm_thunder_b && in_battle) {
        update_storm(screen);
    }
}

}
