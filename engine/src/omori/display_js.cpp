#include "display_js.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "../core/image.h"
#include "../gfx2d/window.h"
#include "../js/vm.h"

namespace ui {

namespace {

struct Rec {
    std::weak_ptr<g2d::DisplayObject> target;
};

struct PointRec {
    std::weak_ptr<g2d::DisplayObject> target;
    int kind = 0;
};

std::unordered_map<g2d::DisplayObject*, js::Val>& cache() {
    static std::unordered_map<g2d::DisplayObject*, js::Val> c;
    return c;
}

std::shared_ptr<g2d::DisplayObject> target_of(js::Obj* self) {
    Rec* r = self ? static_cast<Rec*>(self->ptr) : nullptr;
    return r ? r->target.lock() : nullptr;
}

const uint32_t& k(const char* name) {
    static std::unordered_map<const char*, uint32_t> m;
    auto it = m.find(name);
    if (it != m.end()) return it->second;
    return m.emplace(name, js::atom(name)).first->second;
}

double vnum(const js::Val& v) { return v.to_num(); }

void read_four(const js::Val& arr, double out[4]) {
    for (int i = 0; i < 4; i++) out[i] = 0;
    if (!arr.is_array()) return;
    for (size_t i = 0; i < 4 && i < arr.o->items.size(); i++) out[i] = arr.o->items[i].to_num();
}

js::HostClass& point_class() {
    static js::HostClass c;
    if (!c.name) {
        c.name = "ObservablePoint";
        c.finalize = [](js::Obj* self) { delete static_cast<PointRec*>(self->ptr); };
        c.get = [](js::Vm&, js::Obj* self, uint32_t key, js::Val& out) {
            PointRec* r = static_cast<PointRec*>(self->ptr);
            auto t = r ? r->target.lock() : nullptr;
            if (!t) return true;
            g2d::Sprite* s = dynamic_cast<g2d::Sprite*>(t.get());
            if (key == k("x")) {
                if (r->kind == 0) out = js::Val::num(t->scale_x);
                else if (s) out = js::Val::num(s->anchor_x);
                else if (auto* ts = dynamic_cast<g2d::TilingSprite*>(t.get())) out = js::Val::num(ts->origin_x);
            } else if (key == k("y")) {
                if (r->kind == 0) out = js::Val::num(t->scale_y);
                else if (s) out = js::Val::num(s->anchor_y);
                else if (auto* ts = dynamic_cast<g2d::TilingSprite*>(t.get())) out = js::Val::num(ts->origin_y);
            } else if (key == k("set")) {
                std::weak_ptr<g2d::DisplayObject> w = t;
                int kind = r->kind;
                out = js::make_native([w, kind](js::Vm&, const js::Val&, std::vector<js::Val>& a) {
                    auto o = w.lock();
                    if (!o) return js::Val();
                    double x = a.empty() ? 0 : a[0].to_num();
                    double y = a.size() > 1 ? a[1].to_num() : x;
                    g2d::Sprite* sp = dynamic_cast<g2d::Sprite*>(o.get());
                    if (kind == 0) {
                        o->scale_x = x;
                        o->scale_y = y;
                    } else if (sp) {
                        sp->anchor_x = x;
                        sp->anchor_y = y;
                    }
                    return js::Val();
                });
            }
            return true;
        };
        c.set = [](js::Vm&, js::Obj* self, uint32_t key, const js::Val& v) {
            PointRec* r = static_cast<PointRec*>(self->ptr);
            auto t = r ? r->target.lock() : nullptr;
            if (!t) return true;
            g2d::Sprite* s = dynamic_cast<g2d::Sprite*>(t.get());
            g2d::TilingSprite* ts = dynamic_cast<g2d::TilingSprite*>(t.get());
            double n = v.to_num();
            if (key == k("x")) {
                if (r->kind == 0) t->scale_x = n;
                else if (s) s->anchor_x = n;
                else if (ts) ts->origin_x = n;
            } else if (key == k("y")) {
                if (r->kind == 0) t->scale_y = n;
                else if (s) s->anchor_y = n;
                else if (ts) ts->origin_y = n;
            }
            return true;
        };
    }
    return c;
}

js::Val point_js(const std::shared_ptr<g2d::DisplayObject>& t, int kind) {
    PointRec* r = new PointRec();
    r->target = t;
    r->kind = kind;
    return js::make_host(&point_class(), r);
}

js::Val bitmap_js(const g2d::BitmapPtr& b) {
    if (!b) return js::Val::null();
    js::Val o = js::make_object();
    std::weak_ptr<g2d::Bitmap> w = b;
    js::vm().put(o, k("width"), js::Val::num(b->width()));
    js::vm().put(o, k("height"), js::Val::num(b->height()));
    js::vm().put(o, k("isReady"), js::make_native([w](js::Vm&, const js::Val&, std::vector<js::Val>&) {
                     auto bb = w.lock();
                     return js::Val::boolean(bb && bb->is_ready());
                 }));
    js::vm().put(o, k("isError"), js::make_native([w](js::Vm&, const js::Val&, std::vector<js::Val>&) {
                     auto bb = w.lock();
                     return js::Val::boolean(bb && bb->is_error());
                 }));
    return o;
}

g2d::BitmapPtr bitmap_of(g2d::DisplayObject* t) {
    if (auto* s = dynamic_cast<g2d::Sprite*>(t)) return s->bitmap();
    if (auto* ts = dynamic_cast<g2d::TilingSprite*>(t)) return ts->bitmap;
    return nullptr;
}

js::Val method(const std::shared_ptr<g2d::DisplayObject>& t, std::function<js::Val(g2d::DisplayObject&, std::vector<js::Val>&)> fn) {
    std::weak_ptr<g2d::DisplayObject> w = t;
    return js::make_native([w, fn](js::Vm&, const js::Val&, std::vector<js::Val>& a) {
        auto o = w.lock();
        if (!o) return js::Val();
        return fn(*o, a);
    });
}

bool generic_get(const std::shared_ptr<g2d::DisplayObject>& t, uint32_t key, js::Val& out) {
    g2d::DisplayObject& o = *t;
    g2d::Sprite* s = dynamic_cast<g2d::Sprite*>(&o);
    g2d::Window* w = dynamic_cast<g2d::Window*>(&o);
    if (key == k("x")) out = js::Val::num(o.x);
    else if (key == k("y")) out = js::Val::num(o.y);
    else if (key == k("z")) out = js::Val::num(o.z);
    else if (key == k("alpha")) out = js::Val::num(o.alpha);
    else if (key == k("opacity")) out = js::Val::num(w ? w->window_opacity() : o.opacity());
    else if (key == k("visible")) out = js::Val::boolean(o.visible);
    else if (key == k("rotation")) out = js::Val::num(o.rotation);
    else if (key == k("scale")) out = point_js(t, 0);
    else if (key == k("anchor") || key == k("origin")) out = point_js(t, 1);
    else if (key == k("width")) {
        if (s) out = js::Val::num(s->width());
        else if (w) out = js::Val::num(w->width());
        else if (auto* ts = dynamic_cast<g2d::TilingSprite*>(&o)) out = js::Val::num(ts->width);
        else out = js::Val::num(0);
    } else if (key == k("height")) {
        if (s) out = js::Val::num(s->height());
        else if (w) out = js::Val::num(w->height());
        else if (auto* ts = dynamic_cast<g2d::TilingSprite*>(&o)) out = js::Val::num(ts->height);
        else out = js::Val::num(0);
    } else if (key == k("bitmap")) {
        out = bitmap_js(bitmap_of(&o));
    } else if (key == k("children")) {
        std::vector<js::Val> items;
        if (auto* c = dynamic_cast<g2d::Container*>(&o)) {
            for (auto& ch : c->children) items.push_back(display_js(ch.get()));
        }
        out = js::make_array(std::move(items));
    } else if (key == k("parent")) {
        out = o.parent ? display_js(o.parent) : js::Val::null();
    } else if (key == k("filters") || key == k("_filters")) {
        js::Val* v = o.expando.find(k("_filters"));
        out = v ? *v : js::Val::null();
    } else if (key == k("setColorTone")) {
        out = method(t, [](g2d::DisplayObject& d, std::vector<js::Val>& a) {
            if (auto* sp = dynamic_cast<g2d::Sprite*>(&d)) {
                double c[4];
                read_four(a.empty() ? js::Val() : a[0], c);
                sp->set_color_tone(c);
            }
            return js::Val();
        });
    } else if (key == k("setBlendColor")) {
        out = method(t, [](g2d::DisplayObject& d, std::vector<js::Val>& a) {
            if (auto* sp = dynamic_cast<g2d::Sprite*>(&d)) {
                double c[4];
                read_four(a.empty() ? js::Val() : a[0], c);
                sp->set_blend_color(c);
            }
            return js::Val();
        });
    } else if (key == k("getColorTone") || key == k("getBlendColor")) {
        bool tone = key == k("getColorTone");
        out = method(t, [tone](g2d::DisplayObject& d, std::vector<js::Val>&) {
            std::vector<js::Val> items;
            if (auto* sp = dynamic_cast<g2d::Sprite*>(&d)) {
                const double* c = tone ? sp->color_tone() : sp->blend_color();
                for (int i = 0; i < 4; i++) items.push_back(js::Val::num(c[i]));
            }
            return js::make_array(std::move(items));
        });
    } else if (key == k("show")) {
        out = method(t, [](g2d::DisplayObject& d, std::vector<js::Val>&) {
            if (auto* sp = dynamic_cast<g2d::Sprite*>(&d)) sp->show();
            else d.visible = true;
            return js::Val();
        });
    } else if (key == k("hide")) {
        out = method(t, [](g2d::DisplayObject& d, std::vector<js::Val>&) {
            if (auto* sp = dynamic_cast<g2d::Sprite*>(&d)) sp->hide();
            else d.visible = false;
            return js::Val();
        });
    } else if (key == k("move")) {
        out = method(t, [](g2d::DisplayObject& d, std::vector<js::Val>& a) {
            d.x = a.empty() ? 0 : a[0].to_num();
            d.y = a.size() > 1 ? a[1].to_num() : 0;
            return js::Val();
        });
    } else if (key == k("setFrame")) {
        out = method(t, [](g2d::DisplayObject& d, std::vector<js::Val>& a) {
            if (auto* sp = dynamic_cast<g2d::Sprite*>(&d)) {
                auto n = [&](size_t i) { return i < a.size() ? a[i].to_num() : 0.0; };
                sp->set_frame(n(0), n(1), n(2), n(3));
            }
            return js::Val();
        });
    } else {
        return false;
    }
    return true;
}

bool generic_set(g2d::DisplayObject& o, uint32_t key, const js::Val& v) {
    g2d::Window* w = dynamic_cast<g2d::Window*>(&o);
    if (key == k("x")) o.x = vnum(v);
    else if (key == k("y")) o.y = vnum(v);
    else if (key == k("z")) o.z = vnum(v);
    else if (key == k("alpha")) o.alpha = std::clamp(vnum(v), 0.0, 1.0);
    else if (key == k("opacity")) {
        if (w) w->set_window_opacity(vnum(v));
        else o.set_opacity(vnum(v));
    } else if (key == k("visible")) o.visible = v.truthy();
    else if (key == k("rotation")) o.rotation = vnum(v);
    else if (key == k("filters") || key == k("_filters")) set_display_filters_js(&o, v);
    else return false;
    return true;
}

js::HostClass& display_class() {
    static js::HostClass c;
    if (!c.name) {
        c.name = "DisplayObject";
        c.finalize = [](js::Obj* self) { delete static_cast<Rec*>(self->ptr); };
        c.get = [](js::Vm&, js::Obj* self, uint32_t key, js::Val& out) {
            auto t = target_of(self);
            if (!t) return true;
            if (auto* extra = dynamic_cast<DisplayJsExtra*>(t.get())) {
                if (extra->display_js_get(key, out)) return true;
            }
            if (generic_get(t, key, out)) return true;
            if (js::Val* e = t->expando.find(key)) out = *e;
            return true;
        };
        c.set = [](js::Vm&, js::Obj* self, uint32_t key, const js::Val& v) {
            auto t = target_of(self);
            if (!t) return true;
            if (auto* extra = dynamic_cast<DisplayJsExtra*>(t.get())) {
                if (extra->display_js_set(key, v)) return true;
            }
            if (!generic_set(*t, key, v)) t->expando.set(key, v);
            return true;
        };
    }
    return c;
}

double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

}

js::Val display_js(g2d::DisplayObject* obj) {
    if (!obj) return js::Val::null();
    auto& c = cache();
    auto it = c.find(obj);
    if (it != c.end()) {
        Rec* r = it->second.is_obj() ? static_cast<Rec*>(it->second.o->ptr) : nullptr;
        auto t = r ? r->target.lock() : nullptr;
        if (t.get() == obj) return it->second;
        c.erase(it);
    }
    std::shared_ptr<g2d::DisplayObject> sp = obj->weak_from_this().lock();
    if (!sp) return js::Val::null();
    Rec* r = new Rec();
    r->target = sp;
    js::Val v = js::make_host(&display_class(), r);
    c[obj] = v;
    return v;
}

void release_display_proxies() { cache().clear(); }

ColorMatrixFilter::ColorMatrixFilter() {
    matrix.fill(0);
    matrix[0] = matrix[6] = matrix[12] = matrix[18] = 1;
}

void ColorMatrixFilter::set_grayscale(double s) {
    std::array<double, 20> g{s, s, s, 0, 0, s, s, s, 0, 0, s, s, s, 0, 0, 0, 0, 0, 1, 0};
    std::array<double, 20> out{};
    for (int r = 0; r < 4; r++) {
        for (int col = 0; col < 5; col++) {
            double v = 0;
            for (int i = 0; i < 4; i++) v += g[(size_t)(r * 5 + i)] * matrix[(size_t)(i * 5 + col)];
            if (col == 4) v += g[(size_t)(r * 5 + 4)];
            out[(size_t)(r * 5 + col)] = v;
        }
    }
    matrix = out;
    cache_.clear();
}

void ColorMatrixFilter::set_matrix(const std::array<double, 20>& m) {
    matrix = m;
    cache_.clear();
}

g2d::BitmapPtr ColorMatrixFilter::transformed(const g2d::BitmapPtr& src) {
    Cached& c = cache_[src.get()];
    if (c.result && c.source.lock() == src && c.version == src->version) return c.result;
    int w = src->width(), h = src->height();
    if (w <= 0 || h <= 0) return nullptr;
    g2d::BitmapPtr out = g2d::Bitmap::create(w, h);
    out->smooth = src->smooth;
    out->blt(src.get(), 0, 0, w, h, 0, 0);
    Image* img = out->image();
    if (img) {
        int pw = img->w, ph = img->h;
        std::vector<uint32_t> buf((size_t)pw * ph);
        const auto& m = matrix;
        for (int y = 0; y < ph; y++) {
            for (int x = 0; x < pw; x++) {
                uint32_t p = out->pixel_premul(x, y);
                double a = (p >> 24) / 255.0;
                double r = 0, g = 0, b = 0;
                if (a > 0) {
                    r = (p & 255) / 255.0 / a;
                    g = ((p >> 8) & 255) / 255.0 / a;
                    b = ((p >> 16) & 255) / 255.0 / a;
                }
                double nr = clamp01(m[0] * r + m[1] * g + m[2] * b + m[3] * a + m[4]);
                double ng = clamp01(m[5] * r + m[6] * g + m[7] * b + m[8] * a + m[9]);
                double nb = clamp01(m[10] * r + m[11] * g + m[12] * b + m[13] * a + m[14]);
                double na = clamp01(m[15] * r + m[16] * g + m[17] * b + m[18] * a + m[19]);
                uint32_t ir = (uint32_t)std::lround(nr * na * 255), ig = (uint32_t)std::lround(ng * na * 255), ib = (uint32_t)std::lround(nb * na * 255);
                uint32_t ia = (uint32_t)std::lround(na * 255);
                buf[(size_t)y * pw + x] = ir | (ig << 8) | (ib << 16) | (ia << 24);
            }
        }
        out->write_pixels(buf.data(), pw, ph);
    }
    c.source = src;
    c.version = src->version;
    c.result = out;
    return out;
}

namespace {

struct Swap {
    g2d::Sprite* sprite = nullptr;
    g2d::TilingSprite* tiling = nullptr;
    g2d::BitmapPtr original;
    g2d::Rect frame;
};

void collect(g2d::DisplayObject& o, std::vector<g2d::DisplayObject*>& out) {
    out.push_back(&o);
    if (auto* c = dynamic_cast<g2d::Container*>(&o)) {
        for (auto& ch : c->children) collect(*ch, out);
    }
}

}

void ColorMatrixFilter::apply(g2d::DisplayObject& obj, const g2d::RenderState& rs, const std::function<void(const g2d::RenderState&)>& draw) {
    std::vector<g2d::DisplayObject*> list;
    collect(obj, list);
    std::vector<Swap> swaps;
    for (g2d::DisplayObject* d : list) {
        if (auto* ts = dynamic_cast<g2d::TilingSprite*>(d)) {
            if (!ts->bitmap) continue;
            g2d::BitmapPtr t = transformed(ts->bitmap);
            if (!t) continue;
            Swap s;
            s.tiling = ts;
            s.original = ts->bitmap;
            ts->bitmap = t;
            swaps.push_back(s);
        } else if (auto* sp = dynamic_cast<g2d::Sprite*>(d)) {
            g2d::BitmapPtr b = sp->bitmap();
            if (!b) continue;
            g2d::BitmapPtr t = transformed(b);
            if (!t) continue;
            Swap s;
            s.sprite = sp;
            s.original = b;
            s.frame = sp->frame();
            sp->set_bitmap(t);
            sp->set_frame(s.frame.x, s.frame.y, s.frame.width, s.frame.height);
            swaps.push_back(s);
        }
    }
    draw(rs);
    for (auto& s : swaps) {
        if (s.tiling) {
            s.tiling->bitmap = s.original;
        } else {
            s.sprite->set_bitmap(s.original);
            s.sprite->set_frame(s.frame.x, s.frame.y, s.frame.width, s.frame.height);
        }
    }
}

g2d::FilterPtr filter_from_js(const js::Val& v) {
    if (!v.is_obj()) return nullptr;
    js::Val kind = js::vm().get(v, k("_colorMatrix"));
    if (!kind.is_str()) return nullptr;
    js::Val* holder = v.o->find(k("__g2dFilter"));
    std::shared_ptr<ColorMatrixFilter> f;
    if (holder && holder->is_obj() && holder->o->kind == js::Kind::Host && holder->o->ptr) {
        f = *static_cast<std::shared_ptr<ColorMatrixFilter>*>(holder->o->ptr);
    } else {
        static js::HostClass hc;
        if (!hc.name) {
            hc.name = "ColorMatrixFilterHandle";
            hc.finalize = [](js::Obj* self) { delete static_cast<std::shared_ptr<ColorMatrixFilter>*>(self->ptr); };
            hc.get = [](js::Vm&, js::Obj*, uint32_t, js::Val&) { return true; };
            hc.set = [](js::Vm&, js::Obj*, uint32_t, const js::Val&) { return true; };
        }
        f = std::make_shared<ColorMatrixFilter>();
        v.o->set(k("__g2dFilter"), js::make_host(&hc, new std::shared_ptr<ColorMatrixFilter>(f)));
    }
    ColorMatrixFilter fresh;
    if (kind.to_str() == "grayscale") fresh.set_grayscale(js::vm().get(v, k("_grayscale")).to_num());
    if (fresh.matrix != f->matrix) f->set_matrix(fresh.matrix);
    return f;
}

void set_display_filters_js(g2d::DisplayObject* obj, const js::Val& list) {
    if (!obj) return;
    obj->expando.set(k("_filters"), list);
    obj->filters.erase(std::remove_if(obj->filters.begin(), obj->filters.end(), [](const g2d::FilterPtr& f) { return dynamic_cast<ColorMatrixFilter*>(f.get()) != nullptr; }),
                       obj->filters.end());
    if (!list.is_array()) return;
    for (auto& item : list.o->items) {
        g2d::FilterPtr f = filter_from_js(item);
        if (f) obj->filters.push_back(f);
    }
}

}
