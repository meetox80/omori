#include "map_extras.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "../js/bind.h"
#include "../rpg/bind_util.h"
#include "../rpg/game.h"
#include "../rpg/input.h"
#include "../rpg/interpreter.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "map_extras_parts.h"
#include "name_input.h"
#include "ui_util.h"

namespace ui {

namespace {

class WindowPizzaDeliveryNote;

}

struct MapExtras::Impl {
    explicit Impl(rpg::Scene& s) : scene(s) {}
    rpg::Scene& scene;
    std::shared_ptr<WindowOmoriMapQTESmash> qte_window_smash;
    std::shared_ptr<WindowOmoriStopTheArrow> qte_window_stop_the_arrow;
    std::shared_ptr<WindowOmoriHitTheMark> qte_window_hit_the_mark;
    std::shared_ptr<SpriteMapCharacterTag> map_character_tag;
    std::shared_ptr<WindowOmoriNameInputName> name_input_name_window;
    std::shared_ptr<VirtualKeyboard> virtual_keyboard;
    std::shared_ptr<WindowPizzaDeliveryNote> pizza_note;
    std::vector<std::weak_ptr<WindowPizzaDeliveryNote>> created_notes;
    bool pizza_note_assigned = false;
    void remove_current_pizza_note();
};

namespace {

class WindowPizzaDeliveryNote : public WindowBase, public js::JsObject {
public:
    void initialize(const std::string& text, MapExtras::Impl* owner) {
        owner_ = owner;
        double x = (640 - window_width()) / 2;
        double y = (480 - window_height()) / 3;
        WindowBase::initialize(x, y, window_width(), window_height());
        set_window_alpha(0);
        refresh(text);
    }
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }

    int standard_padding() override { return 0; }
    double window_width() { return 640 - 20; }
    double window_height() { return 480 - 40; }

    void refresh(const std::string& text) {
        c().clear();
        visible = true;
        g2d::BitmapPtr bitmap = load_picture("PIZZA-Background");
        c().blt(bitmap.get(), 0, 0, bitmap->width(), bitmap->height(), 0, 0);
        double next_x = 24;
        double next_y = 32;
        size_t from = 0;
        while (true) {
            size_t at = text.find(' ', from);
            std::string word = text.substr(from, at == std::string::npos ? std::string::npos : at - from);
            g2d::BitmapPtr piece = load_picture("PIZZA-" + word);
            if (next_x + piece->width() > 580) {
                next_x = 24;
                next_y += 64;
            }
            c().blt(piece.get(), 0, 0, piece->width(), piece->height(), next_x, next_y);
            next_x = next_x + piece->width() + 20;
            if (at == std::string::npos) break;
            from = at + 1;
        }
    }

    void update() override {
        WindowBase::update();
        if (input::is_triggered(input::OK)) close_note();
    }

    void close_note() {
        close();
        visible = false;
        if (owner_) owner_->remove_current_pizza_note();
    }

    js::HostClass* js_class() const override { return &klass().hc; }

    static js::Klass& klass() {
        static js::Klass* k = nullptr;
        if (!k) {
            k = &rpg::bind::klass("Window_PizzaDeliveryNote");
            auto kk = js::klass_of<WindowPizzaDeliveryNote>(*k);
            kk.prop(
                "visible", [](WindowPizzaDeliveryNote& w) { return js::Val::boolean(w.visible); },
                [](WindowPizzaDeliveryNote& w, const js::Val& v) { w.visible = v.truthy(); });
            kk.call("closeNote", [](WindowPizzaDeliveryNote& w, std::vector<js::Val>&) { w.close_note(); });
        }
        return *k;
    }

    void detach() { owner_ = nullptr; }

private:
    MapExtras::Impl* owner_ = nullptr;
};

uint32_t pizza_note_key() {
    static uint32_t key = js::atom("_pizzaNote");
    return key;
}

}

void MapExtras::Impl::remove_current_pizza_note() {
    if (pizza_note) scene.remove_child(pizza_note.get());
}

MapExtras::MapExtras(rpg::Scene& scene) : impl_(new Impl(scene)) {}

MapExtras::~MapExtras() {
    for (auto& weak : impl_->created_notes) {
        if (auto note = weak.lock()) note->detach();
    }
}

void MapExtras::create_qte_windows() {
    Impl& m = *impl_;
    m.qte_window_smash = WindowBase::make<WindowOmoriMapQTESmash>();
    m.scene.add_child(m.qte_window_smash);
    m.qte_window_stop_the_arrow = WindowBase::make<WindowOmoriStopTheArrow>();
    m.scene.add_child(m.qte_window_stop_the_arrow);
    m.qte_window_hit_the_mark = WindowBase::make<WindowOmoriHitTheMark>();
    m.scene.add_child(m.qte_window_hit_the_mark);
}

void MapExtras::create_map_character_tag() {
    Impl& m = *impl_;
    m.map_character_tag = std::make_shared<SpriteMapCharacterTag>();
    m.scene.add_child(m.map_character_tag);
}

void MapExtras::create_name_input_windows() {
    Impl& m = *impl_;
    m.name_input_name_window = WindowBase::make<WindowOmoriNameInputName>();
    m.name_input_name_window->x = 70;
    m.name_input_name_window->y = 60;
    m.scene.add_window(m.name_input_name_window);
    m.virtual_keyboard = WindowBase::make<VirtualKeyboard>();
    m.virtual_keyboard->name_window = m.name_input_name_window.get();
    m.scene.add_window(m.virtual_keyboard);
    m.virtual_keyboard->x = m.name_input_name_window->x;
    m.virtual_keyboard->y = m.name_input_name_window->y + m.name_input_name_window->height();
}

void MapExtras::create_pizza_note() {
    impl_->pizza_note.reset();
    impl_->pizza_note_assigned = false;
}

bool MapExtras::can_use_character_tag() {
    if (rpg::game_map().is_event_running()) return false;
    if (rpg::game_party().size() <= 1) return false;
    if (rpg::game_switches().value(rpg::params().tag_disable_switch)) return false;
    rpg::GamePlayer& player = rpg::game_player();
    bool moving = player.is_moving() || player.followers().are_moving() || input::is_pressed(input::LEFT) || input::is_pressed(input::RIGHT) ||
                  input::is_pressed(input::UP) || input::is_pressed(input::DOWN);
    if (moving) return false;
    return true;
}

void MapExtras::update_character_tag_input() {
    if (input::is_triggered(input::TAG)) {
        if (can_use_character_tag()) {
            SpriteMapCharacterTag* tag = impl_->map_character_tag.get();
            if (tag && tag->finished()) tag->show();
        }
    }
}

bool MapExtras::character_tag_active() { return impl_->map_character_tag ? impl_->map_character_tag->opacity() > 0 : false; }

void MapExtras::terminate() {
    if (impl_->map_character_tag) impl_->map_character_tag->visible = false;
}

void MapExtras::show_name_input_windows(const std::string& name, int max) {
    Impl& m = *impl_;
    if (!m.name_input_name_window || !m.virtual_keyboard) return;
    WindowOmoriNameInputName& w = *m.name_input_name_window;
    w.max_characters = max;
    w.set_width(w.window_width());
    w.create_contents();
    w.refresh();
    w.clear_name(name);
    w.open();
    m.virtual_keyboard->setup();
}

void MapExtras::hide_name_input_windows() {
    Impl& m = *impl_;
    if (!m.name_input_name_window || !m.virtual_keyboard) return;
    m.name_input_name_window->close();
    m.name_input_name_window->deactivate();
    m.virtual_keyboard->dispose();
}

bool MapExtras::is_input_window_active() {
    Impl& m = *impl_;
    if (!m.name_input_name_window || !m.virtual_keyboard) return false;
    return m.name_input_name_window->openness() > 0 || m.virtual_keyboard->active;
}

void MapExtras::start_qte_smash(const js::Val& segments, const js::Val& time, const js::Val& width) {
    WindowOmoriMapQTESmash* win = impl_->qte_window_smash.get();
    if (!win) return;
    win->open();
    win->activate();
    win->setup(segments, time, width);
}

void MapExtras::start_qte_stop_the_arrow(const js::Val& direction, const js::Val& delay, const js::Val& x, const js::Val& y) {
    WindowOmoriStopTheArrow* win = impl_->qte_window_stop_the_arrow.get();
    if (!win) return;
    win->open();
    win->activate();
    win->setup(direction, delay);
    if (!x.is_undef()) win->x = x.to_num();
    if (!y.is_undef()) win->y = y.to_num();
}

void MapExtras::start_qte_hit_the_mark(const js::Val& speed, int index, const js::Val& x, const js::Val& y) {
    WindowOmoriHitTheMark* win = impl_->qte_window_hit_the_mark.get();
    if (!win) return;
    win->open();
    win->activate();
    win->setup(speed, index);
    if (!x.is_undef()) win->x = x.to_num();
    if (!y.is_undef()) win->y = y.to_num();
}

void MapExtras::show_pizza_note(const std::string& text) {
    Impl& m = *impl_;
    auto note = std::make_shared<WindowPizzaDeliveryNote>();
    note->initialize(text, &m);
    m.created_notes.erase(std::remove_if(m.created_notes.begin(), m.created_notes.end(),
                                         [](const std::weak_ptr<WindowPizzaDeliveryNote>& w) { return w.expired(); }),
                          m.created_notes.end());
    m.created_notes.push_back(note);
    m.pizza_note = note;
    m.pizza_note_assigned = true;
    m.scene.add_child(note);
}

bool MapExtras::pizza_note_visible() { return impl_->pizza_note && impl_->pizza_note->visible; }

bool MapExtras::update_call_menu_blocked() {
    Impl& m = *impl_;
    if (rpg::game_player().can_move() && input::is_triggered(input::MENU) && rpg::game_switches().value(818)) {
        rpg::GameInterpreter* interpreter = rpg::game_map().interpreter.get();
        if (m.pizza_note && !m.pizza_note->visible) {
            m.pizza_note->close_note();
            if (interpreter) interpreter->show_pizza_note(rpg::game_variables().value(817));
            return true;
        } else if (!m.pizza_note) {
            if (interpreter) interpreter->show_pizza_note(rpg::game_variables().value(817));
        }
        return true;
    }
    return false;
}

bool MapExtras::js_get(uint32_t key, js::Val& out) {
    if (key == pizza_note_key()) {
        Impl& m = *impl_;
        if (m.pizza_note) out = m.pizza_note->to_js();
        else out = m.pizza_note_assigned ? js::Val::null() : js::Val::undef();
        return true;
    }
    return false;
}

bool MapExtras::js_set(uint32_t key, const js::Val& v) {
    if (key == pizza_note_key()) {
        Impl& m = *impl_;
        if (v.is_nullish()) {
            m.pizza_note.reset();
            m.pizza_note_assigned = v.is_null();
            return true;
        }
        if (WindowPizzaDeliveryNote* note = js::host_cast<WindowPizzaDeliveryNote>(v)) {
            std::shared_ptr<g2d::DisplayObject> shared = note->shared_from_this();
            m.pizza_note = std::static_pointer_cast<WindowPizzaDeliveryNote>(shared);
            m.pizza_note_assigned = true;
            return true;
        }
        return false;
    }
    return false;
}

}
