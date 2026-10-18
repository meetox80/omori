#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "../js/dataview.h"
#include "../js/random.h"
#include "../js/vm.h"
#include "../rpg/bind_util.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/interpreter.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "../rpg/scene.h"
#include "../rpg/system.h"
#include "scenes.h"
#include "sprites.h"
#include "ui_util.h"
#include "window_base.h"

namespace ui {

namespace {

using rpg::bind::Args;
using js::Val;

bool gacha_item_desc_enable() { return js::string_to_number(data::plugin_param("Gacha", "Show Item Description", "0")) != 0; }

double meta_number(const Value& v) {
    if (v.is_null() || !v.truthy()) return 0;
    if (v.is_str()) return rpg::js_number(v.string());
    if (v.t == Value::Bool) return v.b ? 1 : 0;
    return v.num();
}

class SpriteGachaEffect : public g2d::Sprite, public js::JsObject {
public:
    js::HostClass* js_class() const override { return &rpg::bind::klass("Sprite_GachaEffect").hc; }

    void keep_display(bool v) { keep_display_ = v; }

    void update() override {
        g2d::Sprite::update();
        update_visibility();
        update_animation_sprites();
    }

    void hide_effect() {
        hiding_ = true;
        update_visibility();
    }

    void show_effect() {
        hiding_ = false;
        update_visibility();
    }

    void update_visibility() { visible = !hiding_; }

    void update_animation_sprites() {
        if (animation_sprites_.empty()) return;
        auto sprites = animation_sprites_;
        animation_sprites_.clear();
        for (auto& s : sprites) {
            if (s->is_playing()) {
                animation_sprites_.push_back(s);
            } else if (!keep_display_) {
                s->remove();
            } else {
                end_sprites_.push_back(s);
            }
        }
    }

    void start_animation(const Value* animation, bool mirror, double delay) {
        auto s = std::make_shared<SpriteAnimation>();
        s->setup(this, animation, mirror, delay);
        if (parent) parent->add_child(s);
        animation_sprites_.push_back(s);
    }

    bool is_animation_playing() const { return !animation_sprites_.empty(); }

    void all_remove() {
        if (!animation_sprites_.empty()) {
            auto sprites = animation_sprites_;
            animation_sprites_.clear();
            for (auto& s : sprites) s->remove();
        }
        if (!end_sprites_.empty()) {
            auto sprites = end_sprites_;
            end_sprites_.clear();
            for (auto& s : sprites) s->remove();
        }
    }

private:
    std::vector<std::shared_ptr<SpriteAnimation>> animation_sprites_;
    std::vector<std::shared_ptr<SpriteAnimation>> end_sprites_;
    bool hiding_ = false;
    bool keep_display_ = false;
};

class WindowGachaGet : public WindowBase, public js::JsObject {
public:
    js::HostClass* js_class() const override { return &rpg::bind::klass("Window_GachaGet").hc; }

    void initialize(double x, double y, double w, double h) override {
        WindowBase::initialize(x, y, w, h);
        set_window_alpha(0);
        item_ = rpg::ItemRef();
        item_desc_enable_ = true;
        gacha_sprite = std::make_shared<g2d::Sprite>();
        gacha_sprite->anchor_x = 0.5;
        gacha_sprite->anchor_y = 0.5;
        gacha_sprite->x = w / 2;
        gacha_sprite->y = 480 - h - gacha_sprite->height() - standard_padding();
        gacha_sprite->set_opacity(0);
        add_child_to_back(gacha_sprite);
        refresh();
    }

    void item_desc_enable(bool value) {
        if (item_desc_enable_ != value) {
            item_desc_enable_ = value;
            refresh();
        }
    }

    void set_item(const rpg::ItemRef& item) {
        if (item_ != item) {
            item_ = item;
            refresh();
        }
    }

    void update() override {
        WindowBase::update();
        g2d::BitmapPtr b = gacha_sprite->bitmap();
        if (b) {
            double bitmap_height = b->height();
            double contents_h = c().height();
            if (item_desc_enable_) contents_h -= line_height() * 3;
            double scale = 1;
            if (bitmap_height > contents_h) scale = contents_h / bitmap_height;
            gacha_sprite->scale_x = scale;
            gacha_sprite->scale_y = scale;
        }
    }

    void refresh() {
        c().clear();
        if (item_desc_enable_) {
            double y = contents_height() - line_height() * 3;
            draw_horz_line(y);
            draw_description(0, y + line_height());
        }
        if (!item_.valid() || !item_.data()["meta"]["gachaImage"].truthy()) {
            gacha_sprite->set_bitmap(nullptr);
            return;
        }
        g2d::BitmapPtr bitmap = load_picture(data::js_string(item_.data()["meta"]["gachaImage"]));
        gacha_sprite->set_bitmap(bitmap);
        bitmap->smooth = true;
        static const int shifted[] = {83, 90, 91, 92, 93};
        if (std::find(std::begin(shifted), std::end(shifted), item_.id) != std::end(shifted)) gacha_sprite->y = gacha_sprite->y - 32;
    }

    void draw_description(double x, double y) {
        if (item_.valid()) draw_text_ex(item_.data()["description"].string(), x, y);
    }

    void draw_horz_line(double y) {
        double line_y = y + line_height() / 2 - 1;
        c().paint_opacity = 48;
        c().fill_rect(0, line_y, contents_width(), 2, normal_color());
        c().paint_opacity = 255;
    }

    std::shared_ptr<g2d::Sprite> gacha_sprite;

private:
    rpg::ItemRef item_;
    bool item_desc_enable_ = true;
};

WindowGachaGet* gacha_window_of(const Val& v) { return js::host_cast<WindowGachaGet>(v); }
SpriteGachaEffect* gacha_effect_of(const Val& v) { return js::host_cast<SpriteGachaEffect>(v); }

int g_active_gacha = 0;

void add_lots(std::vector<Val>& lot, rpg::ItemRef::Kind kind, const Value& table) {
    rpg::GameSystem& sys = rpg::game_system();
    for (size_t i = 1; i < table.size(); i++) {
        rpg::ItemRef item{kind, (int)i};
        if (!item.valid()) continue;
        if (!sys.is_in_gacha(item.class_name(), item.id)) continue;
        int num_lot = (int)meta_number(item.data()["meta"]["gachaNumLot"]);
        for (int j = 0; j < num_lot; j++) lot.push_back(item.to_js());
    }
}

void init_gacha(rpg::GameInterpreter& in) {
    rpg::Scene* scene = rpg::scene_manager::scene();
    in.set_prop("_gachaItem", Val::null());
    std::vector<Val> lot;
    auto rank = std::make_shared<SpriteGachaEffect>();
    rank->keep_display(true);
    if (scene) scene->add_child(rank);
    in.set_prop("_rankSprite", rank->to_js());
    double wy = 120;
    double wh = 320;
    auto win = WindowBase::make<WindowGachaGet>(0.0, wy, 280.0, wh);
    win->item_desc_enable(gacha_item_desc_enable());
    win->hide();
    if (scene) scene->add_window(win);
    in.set_prop("_getWindow", win->to_js());
    add_lots(lot, rpg::ItemRef::Item, data::items());
    add_lots(lot, rpg::ItemRef::Weapon, data::weapons());
    add_lots(lot, rpg::ItemRef::Armor, data::armors());
    size_t pick = (size_t)(int)(rng::next() * (double)lot.size());
    Val chosen = pick < lot.size() ? lot[pick] : Val();
    in.set_prop("_gachaLot", js::make_array(lot));
    in.set_prop("_gachaItem", chosen);
    rpg::ItemRef item = rpg::bind::item_of(chosen);
    win->set_item(item);
    if (item.valid()) rpg::game_party().gain_item(item, 1);
    g_active_gacha++;
}

void display_gacha_item(js::Vm& vm, rpg::GameInterpreter& in) {
    WindowGachaGet* win = gacha_window_of(in.prop("_getWindow"));
    if (!win) vm.throw_type_error("Cannot read property '_gachaSprite' of null");
    win->gacha_sprite->set_opacity(0);
    in.set_prop("_moveGet", Val::boolean(true));
    win->x = (640 - win->width()) / 2;
    win->y = 120;
    win->show();
}

void gacha_update(rpg::GameInterpreter& in) {
    if (g_active_gacha <= 0) return;
    if (in.prop("_gachaClosing").truthy()) {
        Val win_val = in.prop("_getWindow");
        WindowGachaGet* win = gacha_window_of(win_val);
        SpriteGachaEffect* rank = gacha_effect_of(in.prop("_rankSprite"));
        if (win) win->gacha_sprite->set_opacity(win->gacha_sprite->opacity() - 20);
        if (rank) rank->set_opacity(rank->opacity() - 28);
        if (!win || win->gacha_sprite->opacity() <= 0) {
            if (win) win->close();
            in.set_prop("_gachaItem", Val::null());
            in.set_prop("_gachaLot", js::make_array());
            rpg::Scene* scene = rpg::scene_manager::scene();
            if (rank) {
                rank->all_remove();
                if (scene) scene->remove_child(rank);
            }
            if (win && scene) scene->remove_child(win);
            in.set_prop("_getWindow", Val::null());
            in.set_prop("_rankSprite", Val::null());
            in.set_prop("_moveGet", Val::null());
            in.set_prop("_gachaClosing", Val::null());
            if (!win_val.is_nullish()) g_active_gacha = std::max(0, g_active_gacha - 1);
        }
    }
    WindowGachaGet* win = gacha_window_of(in.prop("_getWindow"));
    if (win && win->gacha_sprite->opacity() < 255 && in.prop("_moveGet").truthy()) {
        win->gacha_sprite->set_opacity(win->gacha_sprite->opacity() + 4);
        win->y -= 5;
        if (win->y <= 32) win->y = 32;
        if (win->gacha_sprite->opacity() >= 255) win->gacha_sprite->set_opacity(255);
        if (win->gacha_sprite->opacity() == 255 && win->y == 32) in.set_prop("_moveGet", Val::boolean(false));
    }
}

void bind_gacha() {
    auto ki = js::klass_of<rpg::GameInterpreter>(rpg::bind::klass("Game_Interpreter"));
    ki.call("initGacha", [](rpg::GameInterpreter& in, Args&) { init_gacha(in); });
    ki.method("displayGachaItem", [](rpg::GameInterpreter& in, js::Vm& vm, Args&) {
        display_gacha_item(vm, in);
        return Val();
    });
    ki.call("removeGachaWindow", [](rpg::GameInterpreter& in, Args&) { in.set_prop("_gachaClosing", Val::boolean(true)); });

    auto ks = js::klass_of<SpriteGachaEffect>(rpg::bind::klass("Sprite_GachaEffect"));
    ks.prop("x", [](SpriteGachaEffect& s) { return Val::num(s.x); }, [](SpriteGachaEffect& s, const Val& v) { s.x = v.to_num(); });
    ks.prop("y", [](SpriteGachaEffect& s) { return Val::num(s.y); }, [](SpriteGachaEffect& s, const Val& v) { s.y = v.to_num(); });
    ks.prop(
        "opacity", [](SpriteGachaEffect& s) { return Val::num(s.opacity()); }, [](SpriteGachaEffect& s, const Val& v) { s.set_opacity(v.to_num()); });
    ks.prop("alpha", [](SpriteGachaEffect& s) { return Val::num(s.alpha); }, [](SpriteGachaEffect& s, const Val& v) { s.alpha = v.to_num(); });
    ks.prop("visible", [](SpriteGachaEffect& s) { return Val::boolean(s.visible); }, [](SpriteGachaEffect& s, const Val& v) { s.visible = v.truthy(); });
    ks.call("keepDisplay", [](SpriteGachaEffect& s, Args& a) { s.keep_display(js::arg(a, 0).truthy()); });
    ks.call("startAnimation", [](SpriteGachaEffect& s, Args& a) {
        s.start_animation(js::data_of(js::arg(a, 0)), js::arg(a, 1).truthy(), js::arg_num(a, 2));
    });
    ks.boolean("isAnimationPlaying", [](SpriteGachaEffect& s, Args&) { return s.is_animation_playing(); });
    ks.call("allRemove", [](SpriteGachaEffect& s, Args&) { s.all_remove(); });
    ks.call("show", [](SpriteGachaEffect& s, Args&) { s.show_effect(); });
    ks.call("hide", [](SpriteGachaEffect& s, Args&) { s.hide_effect(); });
    ks.call("update", [](SpriteGachaEffect& s, Args&) { s.update(); });

    auto kw = js::klass_of<WindowGachaGet>(rpg::bind::klass("Window_GachaGet"));
    kw.prop("x", [](WindowGachaGet& w) { return Val::num(w.x); }, [](WindowGachaGet& w, const Val& v) { w.x = v.to_num(); });
    kw.prop("y", [](WindowGachaGet& w) { return Val::num(w.y); }, [](WindowGachaGet& w, const Val& v) { w.y = v.to_num(); });
    kw.prop("width", [](WindowGachaGet& w) { return Val::num(w.width()); }, [](WindowGachaGet& w, const Val& v) { w.set_width(v.to_num()); });
    kw.prop("height", [](WindowGachaGet& w) { return Val::num(w.height()); }, [](WindowGachaGet& w, const Val& v) { w.set_height(v.to_num()); });
    kw.prop(
        "opacity", [](WindowGachaGet& w) { return Val::num(w.opacity_value()); }, [](WindowGachaGet& w, const Val& v) { w.set_window_alpha(v.to_num()); });
    kw.prop("visible", [](WindowGachaGet& w) { return Val::boolean(w.visible); }, [](WindowGachaGet& w, const Val& v) { w.visible = v.truthy(); });
    kw.prop("openness", [](WindowGachaGet& w) { return Val::num(w.openness()); }, [](WindowGachaGet& w, const Val& v) { w.set_openness(v.to_int()); });
    kw.num("contentsHeight", [](WindowGachaGet& w, Args&) { return w.contents_height(); });
    kw.num("contentsWidth", [](WindowGachaGet& w, Args&) { return w.contents_width(); });
    kw.num("lineHeight", [](WindowGachaGet& w, Args&) { return w.line_height(); });
    kw.num("standardPadding", [](WindowGachaGet& w, Args&) { return (double)w.standard_padding(); });
    kw.call("show", [](WindowGachaGet& w, Args&) { w.show(); });
    kw.call("hide", [](WindowGachaGet& w, Args&) { w.hide(); });
    kw.call("open", [](WindowGachaGet& w, Args&) { w.open(); });
    kw.call("close", [](WindowGachaGet& w, Args&) { w.close(); });
    kw.call("refresh", [](WindowGachaGet& w, Args&) { w.refresh(); });
    kw.call("setItem", [](WindowGachaGet& w, Args& a) { w.set_item(rpg::bind::item_of(js::arg(a, 0))); });
    kw.call("itemDescEnable", [](WindowGachaGet& w, Args& a) { w.item_desc_enable(js::arg(a, 0).truthy()); });
}

bool g_gacha_binder = rpg::bind::add_binder(bind_gacha);
bool g_gacha_hook = rpg::add_interpreter_update_hook(gacha_update);

}

rpg::SceneFactory gacha_scene() { return rpg::SceneFactory(); }

}
