#include "menu_common.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "../js/compile.h"
#include "../js/vm.h"
#include "../rpg/data.h"
#include "../rpg/evalctx.h"
#include "../rpg/game.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {
namespace menu {

double linear_tween(double t, double b, double c, double d) { return c * t / d + b; }

double ease_out_circ(double t, double b, double c, double d) {
    t /= d;
    t--;
    return c * std::sqrt(1 - t * t) + b;
}

double ease_in_circ(double t, double b, double c, double d) {
    t /= d;
    return -c * (std::sqrt(1 - t * t) - 1) + b;
}

Prop prop_x(g2d::DisplayObject* o) {
    return Prop{[o] { return o->x; }, [o](double v) { o->x = v; }};
}

Prop prop_y(g2d::DisplayObject* o) {
    return Prop{[o] { return o->y; }, [o](double v) { o->y = v; }};
}

Prop prop_opacity(g2d::DisplayObject* o) {
    return Prop{[o] { return o->opacity(); }, [o](double v) { o->set_opacity(v); }};
}

Prop prop_scale_x(g2d::DisplayObject* o) {
    return Prop{[o] { return o->scale_x; }, [o](double v) { o->scale_x = v; }};
}

Prop prop_scale_y(g2d::DisplayObject* o) {
    return Prop{[o] { return o->scale_y; }, [o](double v) { o->scale_y = v; }};
}

Prop prop_width(g2d::Window* w) {
    return Prop{[w] { return w->width(); }, [w](double v) { w->set_width(v); }};
}

Prop prop_height(g2d::Window* w) {
    return Prop{[w] { return w->height(); }, [w](double v) { w->set_height(v); }};
}

Prop prop_window_opacity(g2d::Window* w) {
    return Prop{[w] { return w->window_opacity(); }, [w](double v) { w->set_window_opacity(v); }};
}

Prop prop_contents_opacity(g2d::Window* w) {
    return Prop{[w] { return w->contents_opacity(); }, [w](double v) { w->set_contents_opacity(v); }};
}

Prop prop_openness(g2d::Window* w) {
    return Prop{[w] { return (double)w->openness(); }, [w](double v) { w->set_openness((int)v); }};
}

ObjectMovement::Move ObjectMovement::tween(const void* obj, std::initializer_list<TweenSpec> specs, Easing easing) {
    return tween(obj, std::vector<TweenSpec>(specs), easing);
}

ObjectMovement::Move ObjectMovement::tween(const void* obj, const std::vector<TweenSpec>& specs, Easing easing) {
    Move m;
    m.obj = obj;
    for (const auto& s : specs) {
        Track t;
        t.prop = s.prop;
        t.from = s.prop.get();
        t.to = s.to;
        t.duration = s.duration;
        t.easing = easing ? easing : linear_tween;
        m.tracks.push_back(std::move(t));
    }
    return m;
}

ObjectMovement::Move ObjectMovement::speeds(const void* obj, std::initializer_list<SpeedSpec> specs) {
    Move m;
    m.obj = obj;
    for (const auto& s : specs) {
        Track t;
        t.prop = s.prop;
        t.from = s.prop.get();
        t.to = s.to;
        t.speed = s.speed;
        t.easing = nullptr;
        m.tracks.push_back(std::move(t));
    }
    return m;
}

void ObjectMovement::start_move(Move data, bool uniq) {
    if (uniq) remove_move_with_object(data.obj);
    for (auto& t : data.tracks) {
        if (t.easing) {
            t.time = 0;
            t.to = t.to - t.from;
        }
    }
    data.id = next_id_++;
    list_.push_back(std::move(data));
}

void ObjectMovement::remove_move_with_object(const void* obj) {
    list_.erase(std::remove_if(list_.begin(), list_.end(), [obj](const Move& m) { return m.obj == obj; }), list_.end());
}

void ObjectMovement::update_move(Move& data) {
    bool finished = true;
    for (auto& t : data.tracks) {
        if (t.easing) {
            if (t.to == t.prop.get()) continue;
            int time = t.time;
            int dur = t.duration;
            if (time <= dur) {
                t.time++;
                t.prop.set(t.easing(time, t.from, t.to, dur));
                finished = false;
            }
        } else {
            double s = t.prop.get();
            double target = t.to;
            if (target < s) t.prop.set(std::max(target, s - t.speed));
            if (target > s) t.prop.set(std::min(target, s + t.speed));
            if (target != s) finished = false;
        }
    }
    if (finished) {
        int id = data.id;
        std::function<void()> fn = data.on_finish;
        remove_list_.push_back(id);
        if (fn) fn();
    }
}

void ObjectMovement::update() {
    for (size_t i = 0; i < list_.size(); i++) update_move(list_[i]);
    if (!remove_list_.empty()) {
        for (int id : remove_list_) {
            for (size_t i = 0; i < list_.size(); i++) {
                if (list_[i].id == id) {
                    list_.erase(list_.begin() + (long)i);
                    break;
                }
            }
        }
        remove_list_.clear();
    }
}

double lang_num(const Value& v, std::initializer_list<std::pair<const char*, js::Val>> vars, const js::Val& self) {
    if (v.is_num()) return v.num();
    if (!v.is_str()) return std::nan("");
    auto prog = js::compile_cached(v.string());
    if (!prog) return std::nan("");
    rpg::JsEval e(self.is_undef() ? js::make_object() : self);
    for (auto& kv : vars) e.set(kv.first, kv.second);
    if (!e.run(prog.get(), "LanguageManager eval")) return std::nan("");
    return e.completion().to_num();
}

js::Val js_rect(const ItemRect& r) {
    js::Val o = js::make_object();
    js::vm().put(o, js::atom("x"), js::Val::num(r.x));
    js::vm().put(o, js::atom("y"), js::Val::num(r.y));
    js::vm().put(o, js::atom("width"), js::Val::num(r.width));
    js::vm().put(o, js::atom("height"), js::Val::num(r.height));
    return o;
}

js::Val js_const_fn(double n) {
    return js::make_native([n](js::Vm&, const js::Val&, std::vector<js::Val>&) { return js::Val::num(n); });
}

std::string js_fmt(const std::string& fmt, std::initializer_list<std::string> args) { return rpg::js_format(fmt, args); }

std::string num_str(double n) { return js::number_to_string(n); }

g2d::BitmapPtr blurred_background() {
    static std::weak_ptr<g2d::Bitmap> source;
    static g2d::BitmapPtr blurred;
    g2d::BitmapPtr bg = rpg::scene_manager::background_bitmap();
    if (!bg) return bg;
    if (source.lock() == bg && blurred) return blurred;
    blurred = g2d::Bitmap::create(bg->width(), bg->height());
    blurred->blt(bg.get(), 0, 0, bg->width(), bg->height(), 0, 0);
    blurred->blur();
    source = bg;
    return blurred;
}

static const Value* meta_value(const rpg::ItemRef& item, const char* key) {
    if (!item.valid()) return nullptr;
    const Value& meta = item.data()["meta"];
    const Value* v = meta.find(key);
    return v;
}

bool item_meta_has(const rpg::ItemRef& item, const char* key) {
    const Value* v = meta_value(item, key);
    return v && v->truthy();
}

int item_meta_int(const rpg::ItemRef& item, const char* key, int def) {
    const Value* v = meta_value(item, key);
    if (!v || !v->truthy()) return def;
    if (v->is_num()) return v->as_int();
    if (v->t == Value::Bool) return 1;
    double n = js::string_to_number(v->string());
    return std::isnan(n) ? def : (int)n;
}

std::string item_meta_str(const rpg::ItemRef& item, const char* key) {
    const Value* v = meta_value(item, key);
    if (!v) return "";
    return data::js_string(*v);
}

std::string item_name(const rpg::ItemRef& item) { return item.valid() ? data::js_string(item.data()["name"]) : std::string(); }

std::string item_description(const rpg::ItemRef& item) { return item.valid() ? data::js_string(item.data()["description"]) : std::string(); }

const Value& lang_pinned(const char* file, const char* key) {
    struct Pin {
        std::string lang;
        std::shared_ptr<Document> doc;
    };
    static std::map<std::string, Pin> pins;
    Pin& p = pins[file];
    if (!p.doc || p.lang != data::language()) {
        p.doc = data::lang_file(file);
        p.lang = data::language();
    }
    return p.doc ? p.doc->root()[key] : Value::null_value();
}

const Value& blue(const char* key) { return lang_pinned("XX_BLUE", key); }

const Value& plugin_data(const char* type, const char* name) { return lang_pinned("System", "plugins")[type][name]; }

void SceneBaseEX::update() {
    rpg::Scene::update();
    update_queue_processing();
    move.update();
}

void SceneBaseEX::start_function_wait(std::function<bool()> fn) {
    set_wait_mode("function");
    wait_function_ = std::move(fn);
}

void SceneBaseEX::queue(std::function<void()> fn) {
    size_t idx = std::min((size_t)std::max(queue_index_, 0), queue_.size());
    queue_.insert(queue_.begin() + (long)idx, std::move(fn));
    queue_index_++;
}

void SceneBaseEX::queue_wait_mode(const std::string& mode) {
    queue([this, mode] { set_wait_mode(mode); });
}

void SceneBaseEX::queue_wait(int amount) {
    queue([this, amount] { wait(amount); });
}

void SceneBaseEX::call_next_queue_function() {
    if (!queue_.empty()) {
        std::function<void()> fn = std::move(queue_.front());
        queue_.erase(queue_.begin());
        if (fn) fn();
    }
}

bool SceneBaseEX::update_queue_processing() {
    if (update_wait()) return true;
    call_next_queue_function();
    return !queue_.empty();
}

bool SceneBaseEX::update_wait() { return update_wait_count() || update_wait_mode(); }

bool SceneBaseEX::update_wait_count() {
    if (wait_count_ > 0) {
        wait_count_--;
        if (wait_count_ < 0) wait_count_ = 0;
        return true;
    }
    return false;
}

bool SceneBaseEX::update_wait_mode() {
    bool waiting = false;
    if (wait_mode_ == "message") {
        waiting = rpg::game_message().is_busy();
    } else if (wait_mode_ == "movement") {
        waiting = move.is_moving();
    } else if (wait_mode_ == "function") {
        if (wait_function_) waiting = wait_function_();
        if (!waiting) wait_function_ = nullptr;
    }
    if (!waiting) wait_mode_.clear();
    return waiting;
}

SpriteOmoMenuStatusFace::SpriteOmoMenuStatusFace() {
    actor_ = nullptr;
    anim_row_ = 0;
    anim_delay_ = default_delay();
    anim_frame_ = 0;
    active_ = true;
    in_menu_ = false;
    face_width_ = 106;
    face_height_ = 106;
}

void SpriteOmoMenuStatusFace::set_actor(rpg::GameActor* a) {
    if (a != actor_) {
        actor_ = a;
        update_bitmap();
    }
}

void SpriteOmoMenuStatusFace::update_bitmap() {
    if (actor_) {
        std::string face_name;
        if (in_menu_) {
            face_name = actor_->menu_status_face_name();
            face_width_ = 124;
            face_height_ = 124;
        }
        if (face_name.empty()) {
            face_name = actor_->battle_status_face_name();
            face_width_ = 106;
            face_height_ = 106;
        }
        set_bitmap(load_face(face_name));
    } else {
        set_bitmap(nullptr);
    }
    update_frame();
}

void SpriteOmoMenuStatusFace::set_anim_row(int index) {
    anim_row_ = index;
    update_frame();
}

void SpriteOmoMenuStatusFace::update() {
    g2d::Sprite::update();
    if (active_) {
        if (anim_delay_ > 0) {
            anim_delay_--;
        } else {
            anim_delay_ = default_delay();
            anim_frame_ = (anim_frame_ + 1) % max_frames();
            update_frame();
        }
    }
}

void SpriteOmoMenuStatusFace::update_frame() {
    double fw = face_width(), fh = face_height();
    double fx = anim_frame_ * fw;
    double fy = anim_row_ * fh;
    set_frame(fx, fy, fw, fh);
}

void WindowOmoMenuHelp::initialize(double w, double h) {
    WindowBase::initialize(0, 0, w, h);
    icon_rate = 1.0;
    clear();
}

int WindowOmoMenuHelp::standard_font_size() { return blue("Window_OmoMenuHelp")["refresh_description_fontsize"].as_int(); }

void WindowOmoMenuHelp::set_item(const rpg::ItemRef& item) {
    bool has = item.valid();
    if (has != has_item_ || (has && item != item_)) {
        item_ = item;
        has_item_ = has;
        refresh();
    }
}

void WindowOmoMenuHelp::refresh() {
    c().clear();
    if (!has_item_) return;
    const Value& loc = blue("Window_OmoMenuHelp");
    std::string description = item_description(item_);
    if (is_eastern_locale()) description = "<WordWrap>" + description;
    c().font_size = loc["refresh_contents_fontsize"].as_int();
    draw_text(item_name(item_), 6, -6, 200);
    c().font_size = standard_font_size();
    const Value& pos = loc["refresh_position"];
    draw_text_ex(description, pos[0].num(), pos[1].num());
    double width = 106 * icon_rate;
    draw_item_icon(item_, c().width() - width, 0, icon_rate);
    std::string icon_name = item_meta_str(item_, "IconName");
    if (!icon_name.empty()) {
        g2d::BitmapPtr bitmap = load_system(rpg::trim(icon_name));
        double iw = bitmap->width() * icon_rate, ih = bitmap->height() * icon_rate;
        double x = c().width() - iw;
        double y = c().height() - ih;
        c().blt(bitmap.get(), 0, 0, bitmap->width(), bitmap->height(), x, y, iw, ih);
    }
}

double WindowOmoMenuHelp::wordwrap_width() {
    if (!is_eastern_locale()) return WindowHelp::wordwrap_width();
    double diff = 8;
    if (has_item_ && item_meta_has(item_, "IconIndex")) diff = 8 + (106 * icon_rate);
    return c().width() - diff;
}

void WindowOmoWindowIndexCursor::initialize() {
    text_.clear();
    WindowBase::initialize(0, 0, 100, 40);
    create_index_sprite();
    refresh();
}

void WindowOmoWindowIndexCursor::create_index_sprite() {
    index_sprite = std::make_shared<g2d::Sprite>(load_system("ACSArrows"));
    index_sprite->anchor_x = 0.5;
    index_sprite->anchor_y = 0.5;
    index_sprite->set_frame(0, 0, 32, 29);
    add_child(index_sprite);
}

void WindowOmoWindowIndexCursor::set_text(const std::string& text) {
    if (text != text_) {
        text_ = text;
        refresh();
    }
}

void WindowOmoWindowIndexCursor::refresh() {
    const Value& loc = blue("Window_OmoWindowIndexCursor");
    int font_size = loc["refresh_contents_fontsize"].as_int();
    c().font_size = font_size;
    set_width(text_width(text_) + (standard_padding() * 2) + 10);
    create_contents();
    c().clear();
    c().font_size = font_size;
    const Value& pos = loc["refresh_draw_text_position"];
    draw_text(text_, pos[0].num(), pos[1].num(), c().width(), "center");
    index_sprite->x = width() / 2;
    index_sprite->y = height() + 5;
}

void WindowGold::initialize(double x, double y) {
    double w = window_width();
    double h = window_height();
    WindowBase::initialize(x, y, w, h);
    refresh();
}

void WindowGold::refresh() {
    double x = text_padding();
    double width = c().width() - text_padding() * 2;
    c().clear();
    const Value& pos = blue("Window_Gold")["drawCurrencyValue_position"];
    double px = lang_num(pos[0], {{"x", js::Val::num(x)}, {"width", js::Val::num(width)}});
    draw_currency_value(value(), currency_unit(), px, pos[1].num(), width);
}

double WindowGold::value() { return rpg::game_party().gold(); }

std::string WindowGold::currency_unit() { return data::js_string(data::system()["currencyUnit"]); }

void WindowGold::open() {
    refresh();
    WindowBase::open();
}

std::string& WindowMenuCommand::last_command_symbol() {
    static std::string s;
    return s;
}

void WindowMenuCommand::init_command_position() { last_command_symbol().clear(); }

void WindowMenuCommand::initialize(double x, double y) {
    WindowCommand::initialize(x, y);
    select_last();
}

void WindowMenuCommand::select_last() { select_symbol(last_command_symbol()); }

int WindowMenuCommand::standard_font_size() { return blue("Window_MenuCommand")["standardFontSize"].as_int(); }

ItemRect WindowMenuCommand::item_rect_for_text(int index) {
    ItemRect r = WindowCommand::item_rect_for_text(index);
    r.y -= 3;
    return r;
}

void WindowMenuCommand::make_command_list() {
    const Value& text = plugin_data("mainMenu", "commands");
    std::string tag_text = data::js_string(text[0][0]);
    bool unlocked_tag = rpg::game_switches().value(17);
    bool stab_switch = rpg::game_switches().value(4);
    bool disable_switch = rpg::game_switches().value(5);
    if (unlocked_tag) tag_text = data::js_string(text[0][1]);
    if (stab_switch) tag_text = data::js_string(text[0][2]);
    add_command(tag_text, "talk", disable_switch);
    add_command(data::js_string(text[1]), "equip", rpg::game_party().size() > 0);
    add_command(data::js_string(text[2]), "item", rpg::game_party().has_valid_pocket_items());
    add_command(data::js_string(text[3]), "skill", rpg::game_party().size() > 0);
    add_command(data::js_string(text[4]), "options");
}

void WindowMenuCommand::draw_item(int index) {
    bool stab_switch = rpg::game_switches().value(4);
    if (stab_switch && index == 0) change_text_color(text_color(9));
    else reset_text_color();
    ItemRect r = item_rect_for_text(index);
    std::string align = item_text_align();
    change_paint_opacity(is_command_enabled(index));
    draw_text(command_name(index), r.x, r.y, r.width, align);
}

void WindowMenuCommand::process_ok() {
    last_command_symbol() = current_symbol();
    WindowCommand::process_ok();
}

void WindowOmoMainMenuActorStatus::initialize(int index) {
    actor_index = index;
    WindowBase::initialize(0, 0, window_width(), window_height());
    create_back_sprite();
    create_face_sprite();
    refresh();
    set_window_opacity(0);
}

void WindowOmoMainMenuActorStatus::set_contents_opacity_all(double value) {
    value = std::clamp(value, 0.0, 255.0);
    set_contents_opacity(value);
    if (face_sprite) face_sprite->set_opacity(value);
    if (back_sprite) back_sprite->set_opacity(value);
}

rpg::GameActor* WindowOmoMainMenuActorStatus::actor() {
    auto all = rpg::game_party().all_members();
    if (actor_index < 0 || actor_index >= (int)all.size()) return nullptr;
    return all[(size_t)actor_index];
}

void WindowOmoMainMenuActorStatus::create_back_sprite() {
    back_sprite = std::make_shared<g2d::Sprite>(load_system("newtagmenud"));
    back_sprite->set_frame(0, 0, window_width(), 109);
    add_child_to_back(back_sprite);
}

void WindowOmoMainMenuActorStatus::create_face_sprite() {
    face_sprite = std::make_shared<SpriteOmoMenuStatusFace>();
    face_sprite->y = -face_sprite->height();
    face_sprite->x = 143;
    face_sprite->deactivate();
    face_sprite->in_menu_ = true;
    face_sprite->anchor_x = 0.5;
    add_child(face_sprite);
}

void WindowOmoMainMenuActorStatus::refresh() {
    c().clear();
    rpg::GameActor* a = actor();
    if (!a) return;
    int world_index = current_world();
    face_sprite->set_actor(a);
    face_sprite->y = -face_sprite->height();
    face_sprite->x = std::floor(143 / 2.0);
    const Value& loc = blue("Window_OmoMainMenuActorStatus");
    c().text_color = "rgba(0, 0, 0, 1)";
    c().outline_width = 0;
    c().font_size = loc["refresh_contents_fontsize"].as_int();
    const Value& name_pos = loc["refresh_drawText_position"];
    draw_text(a->name(), name_pos[0].num(), name_pos[1].num(), c().width());
    c().font_size = loc["refresh_contents_fontsize2"].as_int();
    c().text_color = "rgba(255, 255, 255, 1)";
    if (world_index != 2) {
        double exp = a->current_level_exp();
        double current_exp = a->current_exp() - exp;
        double next_exp = a->next_level_exp() - exp;
        double rate = current_exp / next_exp;
        rate = std::min(rate, 1.0);
        c().gradient_fill_rect(2, 30, 151 * rate, 15, "rgba(51, 0, 196, 1)", "rgba(254, 145, 246, 1)", false);
        std::string lvl = data::js_string(blue("Omori_Mainmenu")["lvl"]);
        draw_text(js_fmt("%1 %2", {lvl, num_str(a->level)}), 12, 18, c().width());
    }
    g2d::BitmapPtr bitmap = load_system("newtagmenud");
    c().blt(bitmap.get(), 0, 109, 116 * a->hp_rate(), 14, 29, 56);
    const Value& hp_pos = loc["refresh_drawTextHP_position"];
    draw_text(num_str(a->hp()) + "/" + num_str(a->mhp()), hp_pos[0].num(), hp_pos[1].num(), c().width() - 26, "right");
    const Value& mp_pos = loc["refresh_drawTextMP_position"];
    c().blt(bitmap.get(), 0, 123, 116 * a->mp_rate(), 14, 29, 84);
    draw_text(num_str(a->mp()) + "/" + num_str(a->mmp()), mp_pos[0].num(), mp_pos[1].num(), c().width() - 26, "right");
}

void WindowOmoMainMenuActorStatus::update() { WindowBase::update(); }

void WindowOmoMainMenuPartyStatus::initialize() {
    WindowSelectable::initialize(0, 0, 1, 1);
    ok_sound_enabled = true;
    create_status_windows();
    create_cursor();
}

int WindowOmoMainMenuPartyStatus::max_items() { return rpg::game_party().size(); }

int WindowOmoMainMenuPartyStatus::max_cols() { return std::max(1, rpg::game_party().size()); }

rpg::GameActor* WindowOmoMainMenuPartyStatus::actor(int index) {
    if (index < 0 || index >= (int)status_windows.size()) return nullptr;
    return status_windows[(size_t)index]->actor();
}

void WindowOmoMainMenuPartyStatus::play_ok_sound() {
    if (ok_sound_enabled) WindowSelectable::play_ok_sound();
}

std::vector<rpg::GameActor*> WindowOmoMainMenuPartyStatus::party() {
    std::vector<rpg::GameActor*> list;
    for (auto& w : status_windows) list.push_back(w->actor());
    return list;
}

void WindowOmoMainMenuPartyStatus::update_transform_hook() {
    down_arrow_sprite()->visible = false;
    up_arrow_sprite()->visible = false;
}

void WindowOmoMainMenuPartyStatus::create_status_windows() {
    status_windows.clear();
    int n = rpg::game_party().size();
    for (int i = 0; i < n; i++) {
        auto win = std::shared_ptr<WindowOmoMainMenuActorStatus>(new WindowOmoMainMenuActorStatus());
        win->initialize(i);
        win->x = 10 + (i * (win->width() + 0));
        win->y = -win->height();
        win->origin_x_ = win->x;
        status_windows.push_back(win);
        add_child(win);
    }
}

void WindowOmoMainMenuPartyStatus::sort_status_windows(bool pos) {
    std::stable_sort(status_windows.begin(), status_windows.end(), [](const auto& a, const auto& b) { return a->actor_index < b->actor_index; });
    if (pos) {
        for (size_t i = 0; i < status_windows.size(); i++) {
            auto& win = status_windows[i];
            win->x = 10 + (i * (win->width() + 0));
        }
    }
}

void WindowOmoMainMenuPartyStatus::reorder_status_windows(int from, int to) {
    if (from < 0 || from >= (int)status_windows.size()) return;
    auto w = status_windows[(size_t)from];
    status_windows.erase(status_windows.begin() + from);
    size_t at = (size_t)std::clamp(to, 0, (int)status_windows.size());
    status_windows.insert(status_windows.begin() + (long)at, w);
    for (size_t i = 0; i < status_windows.size(); i++) {
        auto& win = status_windows[i];
        win->x = 10 + (i * (win->width() + 0));
    }
}

void WindowOmoMainMenuPartyStatus::create_cursor() {
    cursor_window = WindowBase::make<WindowOmoWindowIndexCursor>();
    cursor_window->visible = false;
    cursor_window->set_text(data::js_string(blue("Map_Character_Tag")["select_who"]));
    cursor_window->y = -285;
    add_child(cursor_window);
}

void WindowOmoMainMenuPartyStatus::set_cursor_text(const std::string& text) { cursor_window->set_text(text); }

void WindowOmoMainMenuPartyStatus::update_cursor() {
    if (!cursor_window) return;
    int idx = index();
    if (cursor_all()) {
        for (auto& w : status_windows) w->animate_face(true);
        cursor_window->x = (640 - cursor_window->width()) / 2;
        cursor_window->y = -279;
        cursor_window->visible = true;
    } else if (is_cursor_visible() && idx >= 0 && idx < (int)status_windows.size()) {
        for (size_t i = 0; i < status_windows.size(); i++) status_windows[i]->animate_face((int)i == idx);
        auto& win = status_windows[(size_t)idx];
        cursor_window->x = win->x + ((win->width() - cursor_window->width()) / 2);
        cursor_window->y = win->y - 170;
        cursor_window->visible = true;
    } else {
        for (auto& w : status_windows) w->animate_face(false);
        cursor_window->visible = false;
    }
}

void WindowOmoMainMenuPartyStatus::refresh() {
    for (auto& w : status_windows) w->refresh();
}

void WindowItemList::initialize(double x, double y, double w, double h) {
    WindowSelectable::initialize(x, y, w, h);
    category_ = "none";
    data_.clear();
    has_data_ = true;
}

void WindowItemList::set_category(const std::string& category) {
    if (category_ != category) {
        category_ = category;
        refresh();
        reset_scroll();
    }
}

rpg::ItemRef WindowItemList::item() {
    int idx = index();
    if (!has_data_ || idx < 0 || idx >= (int)data_.size()) return rpg::ItemRef();
    return data_[(size_t)idx];
}

bool WindowItemList::includes(const rpg::ItemRef& item) {
    if (!item.valid()) return false;
    const Value& d = item.data();
    if (category_ == "item") return item.is_item() && d["itypeId"].as_int() == 1;
    if (category_ == "weapon") return item.is_weapon();
    if (category_ == "armor") return item.is_armor();
    if (category_ == "keyItem") return item.is_item() && d["itypeId"].as_int() == 2;
    return false;
}

bool WindowItemList::is_enabled(const rpg::ItemRef& item) { return rpg::game_party().can_use(item); }

void WindowItemList::make_item_list() {
    data_.clear();
    has_data_ = true;
    for (const rpg::ItemRef& it : rpg::game_party().all_items()) {
        if (includes(it)) data_.push_back(it);
    }
    if (includes(rpg::ItemRef())) data_.push_back(rpg::ItemRef());
}

void WindowItemList::select_last() {
    rpg::ItemRef last = rpg::game_party().last_item_object();
    int idx = -1;
    for (size_t i = 0; i < data_.size(); i++) {
        if (data_[i].valid() && data_[i] == last) {
            idx = (int)i;
            break;
        }
    }
    select(idx >= 0 ? idx : 0);
}

void WindowItemList::draw_item(int index) {
    if (index < 0 || index >= (int)data_.size()) return;
    rpg::ItemRef it = data_[(size_t)index];
    if (!it.valid()) return;
    double nw = number_width();
    ItemRect r = item_rect(index);
    r.width -= text_padding();
    change_paint_opacity(is_enabled(it));
    draw_item_name(it, r.x, r.y, r.width - nw);
    draw_item_number(it, r.x, r.y, r.width);
    change_paint_opacity(true);
}

double WindowItemList::number_width() { return text_width("\u00d70,000"); }

void WindowItemList::draw_item_number(const rpg::ItemRef& item, double x, double y, double width) {
    if (!needs_number()) return;
    std::string num = group_digits(rpg::game_party().num_items(item));
    c().font_size = (int)yparam_num("ItemQuantitySize", 20);
    draw_text("\u00d7" + num, x, y, width, "right");
    reset_font_settings();
}

void WindowItemList::update_help() { set_help_window_item(item()); }

void WindowItemList::refresh() {
    make_item_list();
    create_contents();
    draw_all_items();
}

static Handoff& handoff_slot() {
    static Handoff h;
    return h;
}

void set_handoff(std::shared_ptr<WindowMenuCommand> cmd, std::shared_ptr<WindowOmoMainMenuPartyStatus> status) {
    handoff_slot().command_window = std::move(cmd);
    handoff_slot().status_window = std::move(status);
}

Handoff take_handoff() {
    Handoff h = std::move(handoff_slot());
    handoff_slot() = Handoff();
    return h;
}

void SceneOmoMenuBase::create() {
    Handoff h = take_handoff();
    if (h.command_window) {
        h.command_window->clear_handlers();
        command_window = h.command_window;
    }
    if (h.status_window) {
        h.status_window->clear_handlers();
        status_window = h.status_window;
    }
    SceneBaseEX::create();
    create_background();
}

void SceneOmoMenuBase::stop() {
    if (rpg::scene_manager::is_next_scene("Scene_Gameover")) start_fade_out(30);
    SceneBaseEX::stop();
}

void SceneOmoMenuBase::terminate() {
    SceneBaseEX::terminate();
    if (command_window) command_window->remove_from_parent();
    if (status_window) status_window->remove_from_parent();
}

void SceneOmoMenuBase::create_command_window() {
    if (!command_window) command_window = WindowBase::make<WindowMenuCommand>(10.0, 10.0);
    command_window->deactivate();
    add_child(command_window);
}

void SceneOmoMenuBase::create_status_windows() {
    if (!status_window) status_window = WindowBase::make<WindowOmoMainMenuPartyStatus>();
    status_window->y = 480;
    add_child(status_window);
}

void SceneOmoMenuBase::create_help_window() {
    help_window = WindowBase::make<WindowOmoMenuHelp>();
    help_window->x = 11;
    help_window->y = (480 - help_window->height()) - 11;
    add_child(help_window);
}

void SceneOmoMenuBase::create_background() {
    background_sprite = std::make_shared<g2d::Sprite>(blurred_background());
    add_child(background_sprite);
}

void SceneOmoMenuBase::hide_actor_status(int index, int duration) {
    if (index < 0 || index >= (int)status_window->status_windows.size()) return;
    WindowOmoMainMenuActorStatus* obj = status_window->status_windows[(size_t)index].get();
    Prop co{[obj] { return obj->contents_opacity(); }, [obj](double v) { obj->set_contents_opacity_all(v); }};
    move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), 110, duration}, {co, 0, duration}}, ease_in_circ));
}

void SceneOmoMenuBase::show_actor_status(int index, int duration) {
    if (index < 0 || index >= (int)status_window->status_windows.size()) return;
    WindowOmoMainMenuActorStatus* obj = status_window->status_windows[(size_t)index].get();
    Prop co{[obj] { return obj->contents_opacity(); }, [obj](double v) { obj->set_contents_opacity_all(v); }};
    move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), -obj->height(), duration}, {co, 255, duration}}, ease_out_circ));
}

void SceneOmoMenuBase::show_info_window(g2d::Window* obj, int duration) {
    double tx = 165, tw = 640 - 175;
    move.start_move(ObjectMovement::speeds(obj, {{prop_x(obj), tx, std::abs(obj->x - tx) / duration},
                                                  {prop_width(obj), tw, std::abs(obj->width() - tw) / duration}}));
}

void SceneOmoMenuBase::hide_info_window(g2d::Window* obj, int duration) {
    double tx = 10, tw = 0;
    move.start_move(ObjectMovement::speeds(obj, {{prop_x(obj), tx, std::abs(obj->x - tx) / duration},
                                                  {prop_width(obj), tw, std::abs(obj->width() - tw) / duration}}));
}

void SceneOmoMenuBase::show_help_window(int duration) { show_info_window(help_window.get(), duration); }

void SceneOmoMenuBase::hide_help_window(int duration) { hide_info_window(help_window.get(), duration); }

void SceneOmoMenuBase::use_item() {
    play_se_for_item();
    user()->use_item(item());
    apply_item();
    status_window->refresh();
    check_common_event();
    check_gameover();
}

bool SceneOmoMenuBase::is_item_for_all() {
    rpg::GameAction action(user());
    action.set_item_object(item());
    return action.is_for_all();
}

std::vector<rpg::GameBattler*> SceneOmoMenuBase::item_target_actors() {
    rpg::GameAction action(user());
    action.set_item_object(item());
    std::vector<rpg::GameBattler*> out;
    if (!action.is_for_friend()) return out;
    if (action.is_for_all()) {
        for (rpg::GameActor* a : status_window->party()) out.push_back(a);
        return out;
    }
    out.push_back(status_window->actor());
    return out;
}

bool SceneOmoMenuBase::can_use() { return can_use(item_target_actors()); }

bool SceneOmoMenuBase::can_use(const std::vector<rpg::GameBattler*>& targets) {
    rpg::GameBattler* u = user();
    if (!u) return false;
    return u->can_use(item()) && is_item_effects_valid(targets);
}

bool SceneOmoMenuBase::is_item_effects_valid(const std::vector<rpg::GameBattler*>& targets) {
    rpg::GameAction action(user());
    action.set_item_object(item());
    for (rpg::GameBattler* t : targets) {
        if (t && action.test_apply(t)) return true;
    }
    return false;
}

void SceneOmoMenuBase::apply_item() {
    rpg::GameAction action(user());
    action.set_item_object(item());
    for (rpg::GameBattler* target : item_target_actors()) {
        if (!target) continue;
        for (int i = 0; i < action.num_repeats(); i++) action.apply(target);
    }
    action.apply_global();
}

void SceneOmoMenuBase::check_common_event() {
    rpg::GameTemp& temp = rpg::game_temp();
    if (temp.is_common_event_reserved()) {
        if (temp.is_common_event_for_battle_only()) temp.clear_common_event();
        else rpg::scene_manager::goto_scene(map_scene());
    }
}

void SceneOmoMenuBase::check_gameover() {
    rpg::GameActor* omori = rpg::game_actors().actor(1);
    bool dead = omori->is_dead() || omori->hp() <= 0;
    if (!rpg::party_in_battle() && dead && omori->index() > -1) {
        rpg::scene_manager::goto_scene(gameover_scene());
        return;
    }
    if (rpg::game_party().is_all_dead()) rpg::scene_manager::goto_scene(gameover_scene());
}

void SceneOmoMenuBase::pop_to_menu() {
    pop_scene();
    set_handoff(command_window, status_window);
}

void SceneMenu::create() {
    Handoff h = take_handoff();
    if (h.command_window) {
        h.command_window->clear_handlers();
        command_window = h.command_window;
    }
    if (h.status_window) {
        h.status_window->clear_handlers();
        status_window = h.status_window;
    }
    SceneMenuBase::create();
    create_command_window();
    create_status_windows();
    create_gold_window();
}

void SceneMenu::create_background() {
    background_sprite = std::make_shared<g2d::Sprite>(blurred_background());
    add_child(background_sprite);
}

bool SceneMenu::is_busy() {
    if (gold_window && gold_window->is_closing()) return true;
    return rpg::Scene::is_busy();
}

void SceneMenu::start() { SceneMenuBase::start(); }

void SceneMenu::terminate() {
    SceneMenuBase::terminate();
    if (command_window) command_window->remove_from_parent();
    if (status_window) status_window->remove_from_parent();
    if (gold_window) gold_window->remove_from_parent();
}

void SceneMenu::create_command_window() {
    if (!command_window) command_window = WindowBase::make<WindowMenuCommand>(10.0, 10.0);
    else command_window->refresh();
    command_window->activate();
    command_window->set_handler("talk", [this] { command_talk(); });
    command_window->set_handler("item", [this] { on_personal_ok(); });
    command_window->set_handler("skill", [this] { command_personal(); });
    command_window->set_handler("equip", [this] { command_personal(); });
    command_window->set_handler("options", [this] { command_options(); });
    command_window->set_handler("cancel", [this] { close_main_menu(); });
    add_window(command_window);
}

void SceneMenu::create_status_windows() {
    if (!status_window) status_window = WindowBase::make<WindowOmoMainMenuPartyStatus>();
    status_window->ok_sound_enabled = true;
    status_window->y = 480;
    status_window->set_handler("ok", [this] { on_personal_ok(); });
    status_window->set_handler("cancel", [this] { on_personal_cancel(); });
    status_window->set_cursor_text(data::js_string(plugin_data("mainMenu", "status")["select"]));
    add_child(status_window);
}

void SceneMenu::create_gold_window() {
    gold_window = std::shared_ptr<WindowGold>(new WindowGold());
    gold_window->initialize(0.0, 0.0);
    gold_window->x = (640 - gold_window->width()) - 10;
    gold_window->y = (command_window->y + command_window->height()) + 8;
    gold_window->set_openness(0);
    gold_window->open();
    add_window(gold_window);
}

void SceneMenu::command_talk() {
    const Value& status = plugin_data("mainMenu", "status");
    if (rpg::game_switches().value(4)) status_window->set_cursor_text(data::js_string(status["stab"]));
    else status_window->set_cursor_text(data::js_string(status["tag"]));
    command_window->deactivate();
    status_window->select(0);
    status_window->activate();
}

void SceneMenu::command_personal() {
    status_window->set_cursor_text(data::js_string(plugin_data("mainMenu", "status")["select"]));
    command_window->deactivate();
    status_window->select(0);
    status_window->activate();
}

void SceneMenu::on_personal_cancel() {
    status_window->deselect();
    command_window->activate();
}

void SceneMenu::on_personal_ok() {
    gold_window->close();
    std::string symbol = command_window->current_symbol();
    if (symbol == "talk") {
        process_tag();
        return;
    }
    if (symbol == "item") rpg::scene_manager::push(omo_menu_item_scene());
    else if (symbol == "skill") rpg::scene_manager::push(omo_menu_skill_scene());
    else if (symbol == "equip") rpg::scene_manager::push(omo_menu_equip_scene());
    set_handoff(command_window, status_window);
}

void SceneMenu::command_options() {
    rpg::scene_manager::push(omo_menu_options_scene());
    set_handoff(command_window, status_window);
}

void SceneMenu::process_tag() {
    int idx = status_window->index();
    if (rpg::game_switches().value(4)) {
        if (idx == 0) {
            rpg::game_temp().reserve_common_event(34);
            rpg::scene_manager::goto_scene(map_scene());
        } else {
            play_buzzer();
            status_window->activate();
        }
        return;
    }
    if (idx > 0) {
        gold_window->open();
        auto members = rpg::game_party().members();
        int actor_id = idx < (int)members.size() ? members[(size_t)idx]->actor_id() : 0;
        rpg::game_variables().set_number(rpg::params().tag_selected_variable, actor_id);
        rpg::game_temp().reserve_common_event(rpg::params().tag_common_event);
    } else {
        play_buzzer();
        gold_window->open();
        status_window->activate();
        return;
    }
    rpg::scene_manager::goto_scene(map_scene());
}

void SceneMenu::close_main_menu() {
    pop_scene();
    if (rpg::params().uce_close_menu > 0) rpg::game_temp().reserve_common_event(rpg::params().uce_close_menu);
}

}

rpg::SceneFactory menu_scene() {
    return rpg::SceneFactory{"Scene_Menu", [] { return std::unique_ptr<rpg::Scene>(new menu::SceneMenu()); }};
}

static SceneRegistrar r_scene_menu("Scene_Menu", menu_scene());

}
