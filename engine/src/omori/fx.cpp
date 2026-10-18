#include "fx.h"

#include <algorithm>
#include <cmath>

#include "../js/random.h"
#include "../rpg/data.h"
#include "../rpg/filter.h"
#include "../rpg/game.h"
#include "../rpg/screen.h"
#include "fx_filters.h"
#include "spriteset.h"

namespace ui {
namespace fx {

static double js_num(const std::string& s, double def) {
    if (s.empty()) return def;
    double d = js::string_to_number(s);
    return d;
}

static double horror_num(const char* key, double def) { return js_num(data::plugin_param("Olivia_HorrorEffects", key, ""), def); }

static bool horror_bool(const char* key) {
    std::string v = data::plugin_param("Olivia_HorrorEffects", key, "true");
    size_t a = v.find_first_not_of(" \t"), b = v.find_last_not_of(" \t");
    v = a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
    for (auto& c : v) c = (char)std::tolower((unsigned char)c);
    return v == "true";
}

static double random_int(double n) { return std::floor(n * rng::next()); }

static void push_filter(g2d::DisplayObject& obj, const FxPtr& f) { obj.filters.push_back(f); }

static void remove_horror_filter(g2d::DisplayObject& obj, const FxPtr& f) {
    auto it = std::find(obj.filters.begin(), obj.filters.end(), f);
    if (it != obj.filters.end()) obj.filters.erase(it);
    else if (!obj.filters.empty()) obj.filters.pop_back();
}

static Scope scope_for(g2d::DisplayObject& obj) { return dynamic_cast<SpritesetBase*>(&obj) ? Scope::Screen : Scope::Object; }

struct Horror::Impl {
    std::shared_ptr<NoiseFx> noise;
    bool noise_animated = false;
    std::shared_ptr<GlitchFx> glitch;
    double slice_min = 0, slice_max = 0, ani_frequency = 0, ani_strength = 0;
    bool glitch_animated = false, refresh_request = false;
    std::shared_ptr<CrtFx> tv;
    bool tv_animated = false;
    double tv_speed = 0;
    bool special = false;

    void effect(GlitchFx& g) {
        if (glitch_animated) {
            double frequency = std::fmod((double)rpg::frame_count(), ani_frequency);
            double r = random_int(ani_strength);
            if (frequency < r + 1) {
                double number = random_int(slice_max - slice_min) + slice_max;
                g.set_slices(number);
            } else {
                g.set_slices(0);
            }
        } else if (g.slices() == 0) {
            double number = random_int(slice_max - slice_min) + slice_max;
            g.set_slices(number);
        } else if (refresh_request) {
            refresh_request = false;
            g.refresh();
        }
    }

    std::shared_ptr<GlitchFx> clone_glitch(g2d::Sprite& sprite) {
        auto g = std::make_shared<GlitchFx>();
        g->scope = glitch->scope;
        g->set_slices(glitch->slices());
        g->offset = glitch->offset;
        return g;
    }

    void replace_glitch(g2d::Sprite& sprite, const std::shared_ptr<GlitchFx>& g) {
        auto it = std::find(sprite.filters.begin(), sprite.filters.end(), std::static_pointer_cast<g2d::Filter>(glitch));
        if (it != sprite.filters.end()) *it = g;
        glitch = g;
    }
};

Horror::Horror() : impl_(new Impl()) {}

Horror::~Horror() {}

void Horror::create_noise(g2d::Sprite& sprite) {
    if (!impl_->noise) {
        auto n = std::make_shared<NoiseFx>();
        n->noise = 0.5;
        n->seed = rng::next();
        n->scope = scope_for(sprite);
        impl_->noise = n;
        push_filter(sprite, n);
    }
    impl_->noise->noise = horror_num("Noise Default", 0.5);
    impl_->noise_animated = horror_bool("Noise Animated");
}

void Horror::remove_noise(g2d::Sprite& sprite) {
    if (!impl_->noise) return;
    remove_horror_filter(sprite, impl_->noise);
    impl_->noise.reset();
}

void Horror::set_noise_rate(double value) {
    if (impl_->noise) impl_->noise->noise = value;
}

void Horror::set_noise_animated(bool value) {
    if (impl_->noise) impl_->noise_animated = value;
}

void Horror::create_glitch(g2d::Sprite& sprite) {
    if (!impl_->glitch) {
        auto g = std::make_shared<GlitchFx>();
        g->scope = scope_for(sprite);
        impl_->glitch = g;
        push_filter(sprite, g);
    }
    double slices = horror_num("Glitch Slices", 10);
    impl_->glitch->set_slices(slices);
    impl_->glitch->offset = horror_num("Glitch Offset", 100);
    impl_->slice_min = std::ceil(slices / 2);
    impl_->slice_max = slices;
    impl_->glitch_animated = horror_bool("Glitch Animated");
    impl_->ani_frequency = horror_num("Glitch Animated Frequency", 300);
    impl_->ani_strength = horror_num("Glitch Animated Strength", 30);
}

void Horror::remove_glitch(g2d::Sprite& sprite) {
    if (!impl_->glitch) return;
    remove_horror_filter(sprite, impl_->glitch);
    impl_->glitch.reset();
}

void Horror::set_glitch_slices(double value) {
    if (!impl_->glitch) return;
    impl_->slice_min = std::ceil(value / 2);
    impl_->slice_max = value;
    impl_->glitch->set_slices(value);
    impl_->glitch->refresh();
}

void Horror::set_glitch_offset(double value) {
    if (impl_->glitch) impl_->glitch->offset = value;
}

void Horror::set_glitch_animated(bool value) {
    if (impl_->glitch) impl_->glitch_animated = value;
}

void Horror::set_glitch_frequency(double value) {
    if (impl_->glitch) impl_->ani_frequency = value;
}

void Horror::set_glitch_strength(double value) {
    if (impl_->glitch) impl_->ani_strength = value;
}

void Horror::create_tv(g2d::Sprite& sprite) {
    if (!impl_->tv) {
        auto t = std::make_shared<CrtFx>();
        t->scope = scope_for(sprite);
        impl_->tv = t;
        push_filter(sprite, t);
    }
    impl_->tv->line_width = horror_num("TV Line Thickness", 5);
    impl_->tv->vignetting = horror_num("TV Corner Size", 0.3);
    impl_->tv_animated = horror_bool("TV Animated");
    impl_->tv_speed = horror_num("TV Speed", 0.25);
}

void Horror::remove_tv(g2d::Sprite& sprite) {
    if (!impl_->tv) return;
    remove_horror_filter(sprite, impl_->tv);
    impl_->tv.reset();
}

void Horror::set_tv_line_thickness(double value) {
    if (impl_->tv) impl_->tv->line_width = value;
}

void Horror::set_tv_corner_size(double value) {
    if (impl_->tv) impl_->tv->vignetting = value;
}

void Horror::set_tv_animated(bool value) {
    if (impl_->tv) impl_->tv_animated = value;
}

void Horror::set_tv_speed(double value) {
    if (impl_->tv) impl_->tv_speed = value;
}

static js::Val* field(const js::Val* obj, const char* key) {
    if (!obj || !obj->is_obj()) return nullptr;
    return obj->o->find(js::atom(key));
}

static double num_of(const js::Val* obj, const char* key) {
    js::Val* v = field(obj, key);
    return v ? v->to_num() : std::nan("");
}

static bool truthy_of(const js::Val* obj, const char* key) {
    js::Val* v = field(obj, key);
    return v && v->truthy();
}

static bool take_update(js::Val* f) {
    js::Val* nu = field(f, "needUpdate");
    if (!nu || !nu->truthy()) return false;
    f->o->set(js::atom("needUpdate"), js::Val::boolean(false));
    return true;
}

void Horror::sync(g2d::Sprite& sprite, const js::Val* source, bool glitch_special) {
    impl_->special = glitch_special;
    if (!source || !source->truthy() || !source->is_obj()) return;
    js::Val* nf = field(source, "noiseFilter");
    if (nf && nf->truthy()) {
        if (!impl_->noise) create_noise(sprite);
        if (take_update(nf)) {
            impl_->noise->noise = num_of(nf, "noise");
            impl_->noise_animated = truthy_of(nf, "animated");
        }
    } else if (impl_->noise) {
        remove_noise(sprite);
    }
    js::Val* gf = field(source, "glitchFilter");
    if (gf && gf->truthy()) {
        if (!impl_->glitch) create_glitch(sprite);
        if (take_update(gf)) {
            impl_->glitch->set_slices(num_of(gf, "slices"));
            impl_->glitch->offset = num_of(gf, "offset");
            impl_->slice_min = num_of(gf, "sliceMin");
            impl_->slice_max = num_of(gf, "sliceMax");
            impl_->glitch_animated = truthy_of(gf, "animated");
            impl_->ani_frequency = num_of(gf, "aniFrequency");
            impl_->ani_strength = num_of(gf, "aniStrength");
            impl_->refresh_request = truthy_of(gf, "refreshRequest");
            gf->o->set(js::atom("refreshRequest"), js::Val::boolean(false));
        }
    } else if (impl_->glitch) {
        remove_glitch(sprite);
    }
    js::Val* tf = field(source, "tvFilter");
    if (tf && tf->truthy()) {
        if (!impl_->tv) create_tv(sprite);
        if (take_update(tf)) {
            impl_->tv->line_width = num_of(tf, "lineWidth");
            impl_->tv->vignetting = num_of(tf, "vignetting");
            impl_->tv_animated = truthy_of(tf, "animated");
            impl_->tv_speed = num_of(tf, "aniSpeed");
        }
    } else if (impl_->tv) {
        remove_tv(sprite);
    }
}

void Horror::update(g2d::Sprite& sprite) {
    Impl& s = *impl_;
    if (s.noise && s.noise_animated) s.noise->seed = rng::next() * 3;
    if (s.glitch) {
        if (s.special && s.glitch_animated) {
            s.refresh_request = false;
            auto g = s.clone_glitch(sprite);
            s.replace_glitch(sprite, g);
            s.effect(*g);
        }
        if (s.special && s.refresh_request) {
            s.refresh_request = false;
            s.glitch_animated = false;
            auto g = s.clone_glitch(sprite);
            g->refresh();
            s.replace_glitch(sprite, g);
            s.effect(*g);
        } else {
            s.effect(*s.glitch);
        }
    }
    if (s.tv && s.tv_animated) s.tv->time += s.tv_speed;
}

struct TkmFilters::Impl {
    struct Entry {
        FxPtr filter;
        std::string type;
    };
    std::map<std::string, Entry> filters;
    std::shared_ptr<DisplacementMap> map;
};

TkmFilters::TkmFilters() : impl_(new Impl()) {}

TkmFilters::~TkmFilters() {}

static std::vector<g2d::DisplayObject*> tkm_targets(int t, const TkmTargets& tg) {
    std::vector<g2d::DisplayObject*> out;
    if (t >= 3990 && t <= 4999) {
        if (!tg.spriteset || tg.character_sprites.empty()) return out;
        const auto& cs = tg.character_sprites;
        int index = t - 4000;
        if (index < 0) {
            long k = (long)cs.size() + index;
            if (k >= 0 && k < (long)cs.size() && cs[(size_t)k].first) out.push_back(cs[(size_t)k].first);
        } else if (index > 0) {
            if ((size_t)(index - 1) < cs.size() && cs[(size_t)(index - 1)].first && cs[(size_t)(index - 1)].second == index) {
                out.push_back(cs[(size_t)(index - 1)].first);
            } else {
                for (const auto& e : cs) {
                    if (e.first && e.second == index) {
                        out.push_back(e.first);
                        break;
                    }
                }
            }
        }
        return out;
    }
    if (t > 5000 && t <= 5999) {
        if (!tg.spriteset || !tg.picture_container) return out;
        const auto& ch = tg.picture_container->children;
        int pic = rpg::game_screen().real_picture_id(t - 5000);
        if (pic >= 0 && (size_t)pic < ch.size() && ch[(size_t)pic] && pic - 1 >= 0) out.push_back(ch[(size_t)pic - 1].get());
        return out;
    }
    switch (t) {
        case 0: out.push_back(tg.spriteset); break;
        case 1: out.push_back(tg.scene); break;
        case 2:
            if (tg.spriteset) out.push_back(tg.tilemap);
            break;
        case 21:
            if (tg.spriteset) out.push_back(tg.base_sprite);
            break;
        case 3:
            if (tg.spriteset && tg.tilemap) out = tg.tile_layers;
            break;
        case 31:
        case 4:
            if (tg.spriteset) {
                for (const auto& e : tg.character_sprites) out.push_back(e.first);
            }
            break;
        case 5:
            if (tg.spriteset) out.push_back(tg.picture_container);
            break;
        default: break;
    }
    return out;
}

static double cpv(const std::vector<double>& cp, size_t i) { return i < cp.size() ? cp[i] : std::nan(""); }

static FxPtr create_tkm(const std::string& type, std::shared_ptr<DisplacementMap>& map) {
    if (type == "bulgepinch") return std::make_shared<BulgePinchFx>();
    if (type == "radialblur") return std::make_shared<RadialBlurFx>();
    if (type == "godray") return std::make_shared<GodrayFx>();
    if (type == "godray-np") {
        auto g = std::make_shared<GodrayFx>();
        g->parallel = false;
        return g;
    }
    if (type == "ascii") return std::make_shared<AsciiFx>();
    if (type == "crosshatch") return std::make_shared<CrossHatchFx>();
    if (type == "dot") return std::make_shared<DotFx>();
    if (type == "emboss") return std::make_shared<EmbossFx>();
    if (type == "shockwave") return std::make_shared<ShockwaveFx>();
    if (type == "twist") return std::make_shared<TwistFx>();
    if (type == "zoomblur") return std::make_shared<ZoomBlurFx>();
    if (type == "noise") {
        auto n = std::make_shared<NoiseFx>();
        n->noise = 0.5;
        n->seed = rng::next();
        return n;
    }
    if (type == "blur") return std::make_shared<BlurFx>();
    if (type == "oldfilm") {
        auto o = std::make_shared<OldFilmFx>();
        o->vignetting = 0;
        o->vignetting_alpha = 0;
        o->vignetting_blur = 0;
        o->scratch = 0.7;
        return o;
    }
    if (type == "rgbsplit") return std::make_shared<RgbSplitFx>();
    if (type == "bloom") return std::make_shared<BloomFx>();
    if (type == "adjustment") return std::make_shared<AdjustmentFx>();
    if (type == "pixelate") return std::make_shared<PixelateFx>();
    if (type == "crt") {
        auto c = std::make_shared<CrtFx>();
        c->vignetting = 0;
        c->vignetting_alpha = 0;
        c->vignetting_blur = 0;
        return c;
    }
    if (type == "reflection-m") return std::make_shared<ReflectionFx>();
    if (type == "reflection-w") {
        auto r = std::make_shared<ReflectionFx>();
        r->mirror = false;
        return r;
    }
    if (type == "motionblur") {
        auto m = std::make_shared<MotionBlurFx>();
        m->kernel_size = 9;
        return m;
    }
    if (type == "glow") return std::make_shared<GlowFx>();
    if (type == "displacement") {
        if (!map) {
            map = std::make_shared<DisplacementMap>();
            map->load(data::plugin_param("FilterController", "displacementImage", ""));
        }
        return std::make_shared<DisplacementFx>(map);
    }
    return nullptr;
}

static int32_t to_int32(double v) {
    if (!std::isfinite(v)) return 0;
    double m = std::fmod(std::trunc(v), 4294967296.0);
    if (m < 0) m += 4294967296.0;
    if (m >= 2147483648.0) m -= 4294967296.0;
    return (int32_t)m;
}

static void update_tkm(rpg::FilterController& fc, const std::string& type, FxFilter& filter) {
    filter.enabled = rpg::filters_enabled_all() && fc.enabled;
    const std::vector<double>& cp = fc.current_params;
    double lx = 0, ly = 0;
    if (type == "bulgepinch") {
        auto& f = static_cast<BulgePinchFx&>(filter);
        fc.char_loc(lx, ly);
        f.center[0] = (lx + cpv(cp, 0)) / core::SCREEN_LW;
        f.center[1] = (ly + cpv(cp, 1)) / core::SCREEN_LH;
        f.radius = cpv(cp, 2);
        f.strength = cpv(cp, 3);
    } else if (type == "radialblur") {
        auto& f = static_cast<RadialBlurFx&>(filter);
        fc.char_loc(lx, ly);
        f.center[0] = lx + cpv(cp, 0);
        f.center[1] = ly + cpv(cp, 1);
        f.radius = cpv(cp, 2) < 0 ? -1 : cpv(cp, 2);
        f.angle = cpv(cp, 3);
        f.kernel_size = (int)std::floor(cpv(cp, 4) + 0.5);
    } else if (type == "godray") {
        auto& f = static_cast<GodrayFx&>(filter);
        f.angle = cpv(cp, 0);
        f.gain = cpv(cp, 1);
        f.lacunarity = cpv(cp, 2);
        f.strength = cpv(cp, 3);
        f.time = fc.time;
    } else if (type == "godray-np") {
        auto& f = static_cast<GodrayFx&>(filter);
        fc.char_loc(lx, ly);
        f.center[0] = lx + cpv(cp, 0);
        f.center[1] = ly + cpv(cp, 1);
        f.gain = cpv(cp, 2);
        f.lacunarity = cpv(cp, 3);
        f.strength = cpv(cp, 4);
        f.time = fc.time;
    } else if (type == "ascii") {
        static_cast<AsciiFx&>(filter).size = cpv(cp, 0);
    } else if (type == "dot") {
        auto& f = static_cast<DotFx&>(filter);
        f.angle = cpv(cp, 0);
        f.scale = cpv(cp, 1);
    } else if (type == "emboss") {
        static_cast<EmbossFx&>(filter).strength = cpv(cp, 0);
    } else if (type == "shockwave") {
        auto& f = static_cast<ShockwaveFx&>(filter);
        fc.char_loc(lx, ly);
        f.center[0] = lx + cpv(cp, 0);
        f.center[1] = ly + cpv(cp, 1);
        f.radius = cpv(cp, 2);
        f.amplitude = cpv(cp, 3);
        f.wavelength = cpv(cp, 4);
        f.brightness = cpv(cp, 5);
        f.time = fc.time;
        if (fc.time > 10) fc.erase();
    } else if (type == "twist") {
        auto& f = static_cast<TwistFx&>(filter);
        fc.char_loc(lx, ly);
        f.offset[0] = lx + cpv(cp, 0);
        f.offset[1] = ly + cpv(cp, 1);
        f.radius = cpv(cp, 2);
        f.angle = cpv(cp, 3);
    } else if (type == "zoomblur") {
        auto& f = static_cast<ZoomBlurFx&>(filter);
        fc.char_loc(lx, ly);
        f.center[0] = lx + cpv(cp, 0);
        f.center[1] = ly + cpv(cp, 1);
        f.inner_radius = cpv(cp, 2);
        f.strength = cpv(cp, 3);
    } else if (type == "noise") {
        auto& f = static_cast<NoiseFx&>(filter);
        f.noise = cpv(cp, 0);
        if (fc.time > 1) {
            f.seed = rng::next() * 3;
            fc.time = 0;
        }
    } else if (type == "blur") {
        auto& f = static_cast<BlurFx&>(filter);
        f.blur = cpv(cp, 0);
        double q = cpv(cp, 1);
        if (q == q && q != 0) f.quality = q;
    } else if (type == "oldfilm") {
        auto& f = static_cast<OldFilmFx&>(filter);
        f.sepia = cpv(cp, 0);
        f.noise = cpv(cp, 1);
        f.scratch_density = cpv(cp, 2);
        if (fc.time > 1) {
            f.seed = rng::next();
            fc.time = 0;
        }
    } else if (type == "rgbsplit") {
        auto& f = static_cast<RgbSplitFx&>(filter);
        double r = cpv(cp, 0), sita = cpv(cp, 1);
        real* ch[3] = {f.red, f.green, f.blue};
        for (int i = 0; i < 3; i++) {
            ch[i][0] = r * std::sin(core::PI / 180 * sita);
            ch[i][1] = r * std::cos(core::PI / 180 * sita);
            sita += 120;
        }
    } else if (type == "bloom") {
        auto& f = static_cast<BloomFx&>(filter);
        f.blur = cpv(cp, 0);
        f.bloom_scale = cpv(cp, 1);
        f.threshold = cpv(cp, 2);
        f.brightness = cpv(cp, 3);
    } else if (type == "adjustment") {
        auto& f = static_cast<AdjustmentFx&>(filter);
        f.gamma = cpv(cp, 0);
        f.saturation = cpv(cp, 1);
        f.contrast = cpv(cp, 2);
        f.brightness = cpv(cp, 3);
        f.red = cpv(cp, 4);
        f.green = cpv(cp, 5);
        f.blue = cpv(cp, 6);
        f.alpha = cpv(cp, 7);
    } else if (type == "pixelate") {
        auto& f = static_cast<PixelateFx&>(filter);
        f.size[0] = cpv(cp, 0);
        f.size[1] = cpv(cp, 1);
    } else if (type == "crt") {
        auto& f = static_cast<CrtFx&>(filter);
        f.curvature = cpv(cp, 0);
        f.line_width = cpv(cp, 1);
        f.line_contrast = cpv(cp, 2);
        f.noise = cpv(cp, 3);
        f.noise_size = cpv(cp, 4);
        f.seed = rng::next() * 3;
        f.time = fc.time;
    } else if (type == "reflection-m" || type == "reflection-w") {
        auto& f = static_cast<ReflectionFx&>(filter);
        fc.char_loc(lx, ly);
        f.boundary = cpv(cp, 0) + ly / core::SCREEN_LH;
        f.amplitude[0] = cpv(cp, 1);
        f.amplitude[1] = cpv(cp, 2);
        f.wave_length[0] = cpv(cp, 3);
        f.wave_length[1] = cpv(cp, 4);
        f.alpha[0] = cpv(cp, 5);
        f.alpha[1] = cpv(cp, 6);
        f.time = fc.time;
    } else if (type == "motionblur") {
        auto& f = static_cast<MotionBlurFx&>(filter);
        f.velocity[0] = cpv(cp, 0);
        f.velocity[1] = cpv(cp, 1);
    } else if (type == "glow") {
        auto& f = static_cast<GlowFx&>(filter);
        f.inner_strength = cpv(cp, 0);
        f.outer_strength = cpv(cp, 1);
        f.color = (uint32_t)(to_int32(cpv(cp, 2)) * 65536 + to_int32(cpv(cp, 3)) * 256 + to_int32(cpv(cp, 4))) & 0xFFFFFFu;
    } else if (type == "displacement") {
        auto& f = static_cast<DisplacementFx&>(filter);
        f.mask_x += cpv(cp, 0);
        f.mask_y += cpv(cp, 1);
        f.scale_x = f.scale_y = cpv(cp, 2);
    }
}

static void remove_from_targets(const FxPtr& filter, int target_obj, const TkmTargets& targets) {
    for (g2d::DisplayObject* t : tkm_targets(target_obj, targets)) {
        if (!t) continue;
        auto it = std::find(t->filters.begin(), t->filters.end(), std::static_pointer_cast<g2d::Filter>(filter));
        if (it != t->filters.end()) t->filters.erase(it);
    }
}

void TkmFilters::update(const TkmTargets& targets, bool battle) {
    rpg::GameMap& map = rpg::game_map();
    std::vector<std::string> order = map.filter_con_order;
    for (const std::string& id : order) {
        auto it = map.filter_con.find(id);
        if (it == map.filter_con.end() || !it->second) continue;
        rpg::FilterController* fc = it->second.get();
        if (battle && fc->is_map_event_only()) continue;
        auto e = impl_->filters.find(id);
        if (fc->erase_flag) {
            if (e != impl_->filters.end()) {
                if (e->second.filter) remove_from_targets(e->second.filter, fc->target_obj, targets);
                if (!battle) impl_->filters.erase(e);
            }
            map.filter_con.erase(it);
            auto o = std::find(map.filter_con_order.begin(), map.filter_con_order.end(), id);
            if (o != map.filter_con_order.end()) map.filter_con_order.erase(o);
            continue;
        }
        if (e == impl_->filters.end()) {
            Impl::Entry entry;
            entry.type = fc->filter_type;
            entry.filter = create_tkm(fc->filter_type, impl_->map);
            if (entry.filter) {
                int t = fc->target_obj;
                entry.filter->scope = (t == 0 || t == 1 || t == 21) ? Scope::Screen : Scope::Object;
                entry.filter->full_area = t == 2 || t == 3 || t == 21 || t == 31 || fc->filter_type == "bulgepinch";
                for (g2d::DisplayObject* target : tkm_targets(t, targets)) {
                    if (target) target->filters.push_back(entry.filter);
                }
            }
            e = impl_->filters.emplace(id, entry).first;
        }
        if (e->second.filter) update_tkm(*fc, e->second.type, *e->second.filter);
    }
}

void TkmFilters::remove(rpg::FilterController* controller, const TkmTargets& targets) {
    if (!controller) return;
    auto e = impl_->filters.find(controller->id);
    if (e == impl_->filters.end()) return;
    if (e->second.filter) remove_from_targets(e->second.filter, controller->target_obj, targets);
    impl_->filters.erase(e);
}

struct ScreenEffects::Impl {
    std::shared_ptr<BlurFx> blur;
    std::shared_ptr<ZoomBlurFx> zblur;
    std::shared_ptr<BloomFx> glow;
    std::shared_ptr<ShockwaveFx> wave;
    std::shared_ptr<GlitchFx> dist;
    std::shared_ptr<BloomFx> weather;
};

ScreenEffects::ScreenEffects() : impl_(new Impl()) {}

ScreenEffects::~ScreenEffects() {}

void ScreenEffects::create(g2d::Sprite& spriteset) {
    Impl& s = *impl_;
    s.blur = std::make_shared<BlurFx>();
    s.blur->blur = 0;
    s.zblur = std::make_shared<ZoomBlurFx>();
    s.zblur->center[0] = core::SCREEN_LW / 2;
    s.zblur->center[1] = core::SCREEN_LH / 2;
    s.zblur->strength = 0;
    s.glow = std::make_shared<BloomFx>();
    s.glow->bloom_scale = 0;
    s.glow->blur = 0;
    s.glow->threshold = 1;
    s.wave = std::make_shared<ShockwaveFx>();
    s.wave->center[0] = 0.5;
    s.wave->center[1] = 0.5;
    s.wave->amplitude = 0;
    s.wave->brightness = 1.1;
    s.wave->time = 0;
    s.dist = std::make_shared<GlitchFx>();
    s.dist->enabled = false;
    s.dist->average = true;
    s.dist->set_slices(2);
    s.dist->offset = 0;
    s.weather = std::make_shared<BloomFx>();
    s.weather->enabled = false;
    s.weather->brightness = 0.5;
    s.weather->bloom_scale = 0.667;
    s.weather->blur = 0;
    s.weather->threshold = 0.05;
    s.wave->enabled = false;
    FxPtr all[6] = {s.blur, s.zblur, s.glow, s.wave, s.dist, s.weather};
    for (auto& f : all) {
        f->scope = Scope::Screen;
        spriteset.filters.push_back(f);
    }
}

void ScreenEffects::update() {
    Impl& s = *impl_;
    if (!s.blur) return;
    rpg::GameScreen& sc = rpg::game_screen();
    if (sc.blur_strength > 0) {
        s.blur->enabled = true;
        s.blur->blur = 4 * sc.blur_strength;
    } else {
        s.blur->enabled = false;
        s.blur->blur = 0;
    }
    if (sc.zblur_strength > 0) {
        s.zblur->enabled = true;
        s.zblur->strength = sc.zblur_strength * 0.5;
        s.zblur->center[0] = sc.zblur_origin_x;
        s.zblur->center[1] = sc.zblur_origin_y;
        s.zblur->inner_radius = sc.zblur_nradius;
    } else {
        s.zblur->enabled = false;
        s.zblur->strength = 0;
    }
    if (sc.glow_power > 0) {
        s.glow->enabled = true;
        s.glow->bloom_scale = 10 * sc.glow_power;
        s.glow->brightness = 1 + 0.3 * sc.glow_power;
        s.glow->blur = 10 * sc.glow_size;
        s.glow->threshold = sc.glow_threshold;
    } else {
        s.glow->enabled = false;
        s.glow->bloom_scale = 0;
        s.glow->blur = 0;
        s.glow->threshold = 1;
    }
    if (200 * sc.wave_amp > 0) {
        s.wave->enabled = true;
        s.wave->amplitude = 200 * sc.wave_amp;
        s.wave->wavelength = 200 * sc.wave_length;
        double r = sc.wave_duration / std::max<double>(sc.wave_initial_duration, 1);
        s.wave->time = 1.5 * (1 - r * r);
        s.wave->center[0] = sc.wave_origin_x;
        s.wave->center[1] = sc.wave_origin_y;
    } else {
        s.wave->enabled = false;
        s.wave->amplitude = 0;
    }
    if (sc.dist_duration > 0) {
        s.dist->enabled = true;
        s.dist->set_slices(2 * sc.dist_density);
        s.dist->offset = 30 * sc.dist_power;
        real* ch[3] = {s.dist->red, s.dist->green, s.dist->blue};
        for (auto* c : ch) {
            c[0] = 1 + sc.dist_offset + 2 * random_int(sc.dist_offset);
            c[1] = 0;
        }
        s.dist->refresh();
    } else {
        s.dist->enabled = false;
    }
    BloomFx& w = *s.weather;
    double power = sc.weather_power;
    const std::string& type = sc.weather_type;
    if (type == "rain") {
        w.enabled = true;
        w.bloom_scale = 0.035 * power;
        w.brightness = 1 - 0.025 * power;
        w.blur = 0.1 * power;
        w.threshold = 0.075;
    } else if (type == "storm") {
        w.enabled = true;
        w.bloom_scale = 0.05 * power;
        w.brightness = 1 - 0.06 * power;
        w.blur = 0.2 * power;
        w.threshold = 0.075;
    } else if (type == "snow") {
        w.enabled = true;
        w.bloom_scale = 0.1 + 0.005 * power;
        w.brightness = 1 + 0.001 * power;
        w.blur = 0.5 + 0.1 * power;
        w.threshold = 0.667;
    } else if (type == "embers") {
        w.enabled = true;
        w.bloom_scale = 0.2 + 0.015 * power;
        w.brightness = 1;
        w.blur = 6 + 0.1 * power;
        w.threshold = 0.75;
    } else {
        w.enabled = false;
        w.bloom_scale = 1;
        w.brightness = 1;
        w.blur = 0;
        w.threshold = 1;
    }
}

}
}
