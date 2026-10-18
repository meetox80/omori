#include <algorithm>
#include <cmath>
#include <functional>

#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "menu_common.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {
namespace menu {

namespace {

std::unique_ptr<rpg::GameActor> copy_actor(rpg::GameActor* a) {
    auto t = std::unique_ptr<rpg::GameActor>(new rpg::GameActor(a->actor_id()));
    static_cast<rpg::GameBattlerBase&>(*t) = static_cast<const rpg::GameBattlerBase&>(*a);
    t->speed_ = a->speed_;
    if (t->result_ && a->result_) *t->result_ = *a->result_;
    t->action_state = a->action_state;
    t->last_target_index = a->last_target_index;
    t->effect_type = a->effect_type;
    t->motion_type = a->motion_type;
    t->weapon_image_id = a->weapon_image_id;
    t->selected = a->selected;
    t->free_state_turn = a->free_state_turn;
    t->self_turn_count = a->self_turn_count;
    t->counters = a->counters;
    t->flinched = a->flinched;
    t->no_effect_message = a->no_effect_message;
    t->no_state_message = a->no_state_message;
    t->has_group_defeat_cache = a->has_group_defeat_cache;
    t->group_defeat_cache = a->group_defeat_cache;
    t->has_counter_total_cache = a->has_counter_total_cache;
    t->counter_total_cache = a->counter_total_cache;
    t->counter_skills_ = a->counter_skills_;
    t->has_counter_skills = a->has_counter_skills;
    t->collapse_sound_played = a->collapse_sound_played;
    t->actor_id_ = a->actor_id_;
    t->name_ = a->name_;
    t->nickname_ = a->nickname_;
    t->profile_ = a->profile_;
    t->class_id = a->class_id;
    t->level = a->level;
    t->character_name_ = a->character_name_;
    t->character_index_ = a->character_index_;
    t->face_name_ = a->face_name_;
    t->face_index_ = a->face_index_;
    t->battler_name_ = a->battler_name_;
    t->exp_ = a->exp_;
    t->skills_ = a->skills_;
    t->equips_ = a->equips_;
    t->action_input_index = a->action_input_index;
    t->last_menu_skill = a->last_menu_skill;
    t->last_battle_skill = a->last_battle_skill;
    t->last_command_symbol = a->last_command_symbol;
    t->state_steps = a->state_steps;
    t->character_movement_graphics = a->character_movement_graphics;
    t->equipped_skills = a->equipped_skills;
    t->locked_equipment_slots = a->locked_equipment_slots;
    t->disabled_battle_commands = a->disabled_battle_commands;
    t->faraway_actor_id = a->faraway_actor_id;
    t->use_victory_face = a->use_victory_face;
    t->battle_face_name = a->battle_face_name;
    t->locked_equipped_skills = a->locked_equipped_skills;
    t->locked_skills = a->locked_skills;
    t->battle_commands_list = a->battle_commands_list;
    t->has_anchor_x = a->has_anchor_x;
    t->has_anchor_y = a->has_anchor_y;
    t->anchor_x_cache = a->anchor_x_cache;
    t->anchor_y_cache = a->anchor_y_cache;
    t->disable_dashing_cache = a->disable_dashing_cache;
    t->played_collapse_sound = a->played_collapse_sound;
    t->level_up_data = a->level_up_data;
    return t;
}

}

class WindowOmoMenuEquipStatus;

class WindowOmoMenuActorEquip : public WindowSelectable {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 0; }
    virtual double window_width() { return 155; }
    virtual double window_height() { return 135; }
    int max_items() override { return 2; }
    int max_cols() override { return 1; }
    double item_height() override { return 26; }
    double item_width() override { return window_width() - 50; }
    int spacing() override { return 0; }
    double custom_cursor_rect_x_offset() override { return -35; }
    double custom_cursor_rect_y_offset() override { return 10; }
    void set_actor_index(int index);
    rpg::GameActor* actor();
    bool is_current_item_enabled() override { return is_item_enabled(index()); }
    bool is_item_enabled(int index);
    int slot_id_at_index(int index);
    int slot_id_at_index() { return slot_id_at_index(index()); }
    rpg::ItemRef equipment_at_index(int index);
    rpg::ItemRef equipment_at_index() { return equipment_at_index(index()); }
    ItemRect item_rect(int index) override;
    void refresh() override;
    void draw_item(int index) override;
    void update_help() override;

    std::function<void()> on_equip_cursor_change;
    WindowOmoMenuEquipStatus* status_window = nullptr;

private:
    int actor_index_ = 0;
};

class WindowOmoMenuEquipStatus : public WindowBase {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    void create_bubble_sprites();
    void close() override;
    void update_open() override;
    void update_close() override;
    g2d::BitmapPtr make_bubble_bitmap(int size);
    int standard_openness_type() override { return 2; }
    int standard_padding() override { return 4; }
    virtual double window_width() { return 237; }
    virtual double window_height() { return 168; }
    void set_actor(rpg::GameActor* actor);
    void set_temp_actor(std::unique_ptr<rpg::GameActor> temp_actor);
    double actor_param_value(rpg::GameActor* actor, int param);
    void refresh();
    void update() override;
    void update_bubble_sprites();

private:
    struct Param {
        int index;
        int sub;
    };
    std::vector<g2d::SpritePtr> bubble_sprites_;
    std::vector<double> bubble_origin_y_;
    std::vector<Param> params_;
    rpg::GameActor* actor_ = nullptr;
    std::unique_ptr<rpg::GameActor> temp_actor_;
};

class WindowOmoMenuActorEquipItem : public WindowItemList {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowItemList::initialize(x, y, w, h); }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 4; }
    virtual double window_width() { return 170 + 277 + 18; }
    virtual double window_height() { return 135; }
    int max_cols() override { return 1 + 1; }
    double item_height() override { return 32 + 3 - 3; }
    double item_width() override { return 160; }
    double custom_cursor_rect_x_offset() override { return 0; }
    double custom_cursor_rect_y_offset() override { return 5; }
    double custom_cursor_rect_text_x_offset() override { return 25; }
    bool is_enabled(const rpg::ItemRef&) override { return true; }
    void play_ok_sound() override {}
    double contents_width() override { return window_width() - standard_padding() * 2; }
    double contents_height() override { return window_height() - standard_padding() * 2; }
    void set_actor(rpg::GameActor* actor);
    void set_slot_id(int slot_id);
    void set_status_window(WindowOmoMenuEquipStatus* w);
    bool includes(const rpg::ItemRef& item) override;
    ItemRect item_rect(int index) override;
    void refresh() override;
    void draw_item(int index) override;
    void update_help() override;

protected:
    void update_transform_hook() override;

private:
    rpg::GameActor* actor_ = nullptr;
    int slot_id_ = 0;
    WindowOmoMenuEquipStatus* status_window_ = nullptr;
};

class SceneOmoMenuEquip : public SceneOmoMenuBase {
public:
    const char* name() const override { return "Scene_OmoMenuEquip"; }
    void create() override;
    void start() override;
    void terminate() override;
    void create_status_windows() override;
    void create_help_window() override;
    void create_actor_equip_window();
    void create_actor_equip_status_window();
    void create_actor_equip_item_window();
    void on_status_window_ok();
    void on_status_window_cancel();
    void on_actor_equip_window_ok();
    void on_actor_equip_window_cancel();
    void on_actor_equip_item_window_ok();
    void on_actor_equip_item_window_cancel();
    void on_equip_cursor_change();
    void show_item_list_window(int duration = 15);
    void hide_item_list_window(int duration = 15);
    void show_actor_equip_window(int index);
    void hide_actor_equip_window(int index);

    std::shared_ptr<WindowOmoMenuActorEquip> actor_equip_window;
    std::shared_ptr<WindowOmoMenuEquipStatus> actor_equip_status;
    std::shared_ptr<WindowOmoMenuActorEquipItem> actor_equip_item_window;

private:
    int selected_index_ = 0;
    void base_show_actor_equip_window(int index);
    void base_hide_actor_equip_window(int index);
    WindowOmoMainMenuActorStatus* status_at(int index);
};

void WindowOmoMenuActorEquip::initialize() {
    WindowSelectable::initialize(0, 0, window_width(), window_height());
    actor_index_ = 0;
    deactivate();
    refresh();
}

void WindowOmoMenuActorEquip::set_actor_index(int index) {
    if (actor_index_ != index) {
        actor_index_ = index;
        refresh();
    }
}

rpg::GameActor* WindowOmoMenuActorEquip::actor() {
    auto members = rpg::game_party().members();
    if (actor_index_ < 0 || actor_index_ >= (int)members.size()) return nullptr;
    return members[(size_t)actor_index_];
}

bool WindowOmoMenuActorEquip::is_item_enabled(int index) {
    rpg::GameActor* a = actor();
    if (!a) return false;
    rpg::ItemRef equipped = equipment_at_index(index);
    int slot_index = slot_id_at_index(index);
    if (!a->is_equip_change_ok(slot_index)) return false;
    if (equipped.valid()) return true;
    std::vector<rpg::ItemRef> list = rpg::game_party().equip_items();
    if (list.empty()) return false;
    std::vector<int> slots = a->equip_slots();
    if (slot_index < 0 || slot_index >= (int)slots.size()) return false;
    int slot_id = slots[(size_t)slot_index];
    for (const rpg::ItemRef& it : list) {
        if (it.data()["etypeId"].as_int() == slot_id && a->can_equip(it)) return true;
    }
    return false;
}

int WindowOmoMenuActorEquip::slot_id_at_index(int index) {
    if (index == 0) return 0;
    if (index == 1) return 1;
    return -1;
}

rpg::ItemRef WindowOmoMenuActorEquip::equipment_at_index(int index) {
    rpg::GameActor* a = actor();
    if (!a) return rpg::ItemRef();
    int slot = slot_id_at_index(index);
    std::vector<rpg::ItemRef> eq = a->equips();
    if (slot < 0 || slot >= (int)eq.size()) return rpg::ItemRef();
    return eq[(size_t)slot];
}

ItemRect WindowOmoMenuActorEquip::item_rect(int index) {
    ItemRect r = WindowSelectable::item_rect(index);
    r.x += 42;
    r.y = 26 + (index * 60);
    return r;
}

void WindowOmoMenuActorEquip::refresh() {
    WindowSelectable::refresh();
    reset_font_settings();
    c().font_size = 20;
    change_paint_opacity(true);
    c().fill_rect(4, 28, width() - 8, 2, "rgba(255, 255, 255, 1)");
    c().fill_rect(4, 65, width() - 8, 2, "rgba(255, 255, 255, 1)");
    c().fill_rect(4, 90, width() - 8, 2, "rgba(255, 255, 255, 1)");
    draw_text(data::js_string(plugin_data("equipMenu", "weapon")), 0, -4, width(), "center");
    draw_text(data::js_string(plugin_data("equipMenu", "charm")), 0, 58, width(), "center");
}

void WindowOmoMenuActorEquip::draw_item(int index) {
    ItemRect r = item_rect(index);
    rpg::ItemRef equip = equipment_at_index(index);
    std::string text = equip.valid() ? item_name(equip) : std::string("------------");
    bool enabled = is_item_enabled(index);
    if (enabled) {
        change_paint_opacity(true);
        c().text_color = "rgba(255, 255, 255, 1)";
    } else {
        change_paint_opacity(false);
        c().text_color = "rgba(140, 140, 140, 1)";
    }
    c().font_size = 24;
    c().draw_text(text, r.x, r.y + 5, r.width, r.height, "left");
    change_paint_opacity(true);
}

void WindowOmoMenuActorEquip::update_help() {
    WindowSelectable::update_help();
    help_window->set_item(equipment_at_index());
    if (on_equip_cursor_change) on_equip_cursor_change();
    if (status_window) status_window->set_actor(actor());
}

void WindowOmoMenuEquipStatus::initialize() {
    WindowBase::initialize(0, 0, window_width(), window_height());
    create_bubble_sprites();
    params_ = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {6, 0}, {7, 0}, {100, 8}};
    actor_ = nullptr;
    temp_actor_.reset();
    set_openness(0);
    refresh();
}

void WindowOmoMenuEquipStatus::create_bubble_sprites() {
    bubble_sprites_.clear();
    bubble_origin_y_.clear();
    static const double positions[2][3] = {{9, -45, 110}, {13, -20, 90}};
    for (const auto& pos : positions) {
        auto sprite = std::make_shared<g2d::Sprite>(make_bubble_bitmap((int)pos[0]));
        sprite->anchor_x = 0.5;
        sprite->anchor_y = 0.5;
        sprite->scale_x = 0;
        sprite->scale_y = 0;
        sprite->x = pos[1];
        sprite->y = pos[2];
        bubble_origin_y_.push_back(sprite->y);
        bubble_sprites_.push_back(sprite);
        add_child(sprite);
    }
}

void WindowOmoMenuEquipStatus::close() {
    WindowBase::close();
    closing_ = true;
}

void WindowOmoMenuEquipStatus::update_open() {
    if (opening_) {
        bool prev_done = false;
        for (size_t i = 0; i < bubble_sprites_.size(); i++) {
            g2d::Sprite* sprite = bubble_sprites_[i].get();
            if (i == 0 || prev_done) {
                sprite->scale_x = std::min(sprite->scale_x + 0.2, 1.0);
                sprite->scale_y = std::min(sprite->scale_x + 0.2, 1.0);
            }
            prev_done = sprite->scale_x == 1;
        }
        if (prev_done) set_openness(openness() + 32);
        if (is_open()) opening_ = false;
    }
}

void WindowOmoMenuEquipStatus::update_close() {
    if (closing_) {
        set_openness(openness() - 32);
        if (is_closed()) {
            bool prev_done = false;
            for (int i = (int)bubble_sprites_.size() - 1; i >= 0; i--) {
                g2d::Sprite* sprite = bubble_sprites_[(size_t)i].get();
                if (i == (int)bubble_sprites_.size() - 1 || prev_done) {
                    sprite->scale_x = std::max(sprite->scale_x - 0.2, 0.0);
                    sprite->scale_y = std::max(sprite->scale_x - 0.2, 0.0);
                }
                prev_done = sprite->scale_x == 0;
            }
            if (prev_done) closing_ = false;
        }
    }
}

g2d::BitmapPtr WindowOmoMenuEquipStatus::make_bubble_bitmap(int size) {
    g2d::BitmapPtr bitmap = g2d::Bitmap::create(size * 2, size * 2);
    double x = size, y = size;
    bitmap->draw_circle(x, y, size, "rgba(0, 0, 0, 1)");
    bitmap->draw_circle(x, y, size - 1, "rgba(255, 255, 255, 1)");
    bitmap->draw_circle(x, y, size - 3, "rgba(0, 0, 0, 1)");
    return bitmap;
}

void WindowOmoMenuEquipStatus::set_actor(rpg::GameActor* actor) {
    if (actor_ != actor) {
        actor_ = actor;
        refresh();
    }
}

void WindowOmoMenuEquipStatus::set_temp_actor(std::unique_ptr<rpg::GameActor> temp_actor) {
    if (temp_actor_ || temp_actor) {
        temp_actor_ = std::move(temp_actor);
        refresh();
    }
}

double WindowOmoMenuEquipStatus::actor_param_value(rpg::GameActor* actor, int param) {
    if (param >= 200) return actor->sparam(param - 200);
    if (param >= 100 && param < 200) return actor->xparam(param - 100) * 100;
    return actor->param(param);
}

void WindowOmoMenuEquipStatus::refresh() {
    c().clear();
    rpg::GameActor* a = actor_;
    if (!a) return;
    g2d::BitmapPtr bitmap = load_system("equip_arrow");
    for (size_t i = 0; i < params_.size(); i++) {
        int param_index = params_[i].index;
        int param_sub = params_[i].sub;
        double value1 = actor_param_value(a, param_index);
        std::string param_name = system_text("param", std::to_string(param_sub ? param_sub : param_index));
        if (rpg::to_lower(param_name) == "max hp") param_name = "HEART";
        if (rpg::to_lower(param_name) == "max mp") param_name = "JUICE";
        c().font_size = 20;
        double y = -5 + (double)i * 21;
        draw_text(rpg::to_upper(param_name) + ":", 8, y, 100);
        draw_text(num_str(value1), 132, y, 100);
        c().blt(bitmap.get(), 0, 0, bitmap->width(), bitmap->height(), 173, 13 + (double)i * 21);
        std::string value2_text;
        if (temp_actor_) {
            double value2 = actor_param_value(temp_actor_.get(), param_index);
            reset_text_color();
            if (value1 < value2) c().text_color = "#69ff90";
            if (value1 > value2) c().text_color = "#ff2b2b";
            value2_text = num_str(value2);
        } else {
            value2_text = "---";
        }
        draw_text(value2_text, 132 + 56, y, 100);
        reset_text_color();
    }
}

void WindowOmoMenuEquipStatus::update() {
    WindowBase::update();
    update_bubble_sprites();
}

void WindowOmoMenuEquipStatus::update_bubble_sprites() {
    if (!bubble_sprites_.empty() && openness() > 0) {
        double s = std::sin((double)rpg::frame_count() * 0.05) * 5;
        for (size_t i = 0; i < bubble_sprites_.size(); i++) {
            g2d::Sprite* sprite = bubble_sprites_[i].get();
            if (i == 0) sprite->y = bubble_origin_y_[i] + 0.5 + s;
            else sprite->y = bubble_origin_y_[i] + 0.5 - s;
        }
    }
}

void WindowOmoMenuActorEquipItem::initialize() {
    WindowItemList::initialize(0, 0, window_width(), window_height());
    actor_ = nullptr;
    slot_id_ = 0;
    deselect();
    deactivate();
}

void WindowOmoMenuActorEquipItem::set_actor(rpg::GameActor* actor) {
    if (actor_ != actor) {
        actor_ = actor;
        refresh();
    }
}

void WindowOmoMenuActorEquipItem::set_slot_id(int slot_id) {
    if (slot_id_ != slot_id) {
        slot_id_ = slot_id;
        refresh();
    }
}

void WindowOmoMenuActorEquipItem::set_status_window(WindowOmoMenuEquipStatus* w) {
    status_window_ = w;
    call_update_help();
}

bool WindowOmoMenuActorEquipItem::includes(const rpg::ItemRef& item) {
    if (!item.valid()) return true;
    if (!actor_) return false;
    std::vector<int> slots = actor_->equip_slots();
    if (slot_id_ < 0 || slot_id_ >= (int)slots.size() || item.data()["etypeId"].as_int() != slots[(size_t)slot_id_]) return false;
    return actor_->can_equip(item);
}

ItemRect WindowOmoMenuActorEquipItem::item_rect(int index) {
    ItemRect r = WindowItemList::item_rect(index);
    r.x += 8;
    r.y += 22;
    return r;
}

void WindowOmoMenuActorEquipItem::update_transform_hook() {
    double w = width(), h = height();
    double p = 24, q = p / 2;
    down_arrow_sprite()->move(w - q, h - q);
    up_arrow_sprite()->move(w - q, q + 24);
    down_arrow_sprite()->visible = down_arrow_sprite()->visible && x > 10;
    up_arrow_sprite()->visible = up_arrow_sprite()->visible && x > 10;
}

void WindowOmoMenuActorEquipItem::refresh() {
    WindowItemList::refresh();
    c().font_size = 22;
    draw_text(data::js_string(plugin_data("equipMenu", "replace")), 8, -8, c().width());
}

void WindowOmoMenuActorEquipItem::draw_item(int index) {
    ItemRect r = item_rect_for_text(index);
    rpg::ItemRef it = index >= 0 && index < (int)data_.size() ? data_[(size_t)index] : rpg::ItemRef();
    std::string text = it.valid() ? item_name(it) : std::string("------------");
    c().font_size = 24;
    c().draw_text(text, r.x, r.y, r.width, r.height, "left");
}

void WindowOmoMenuActorEquipItem::update_help() {
    WindowItemList::update_help();
    if (actor_ && status_window_) {
        std::unique_ptr<rpg::GameActor> a = copy_actor(actor_);
        a->force_change_equip(slot_id_, item());
        status_window_->set_temp_actor(std::move(a));
    }
}

void SceneOmoMenuEquip::create() {
    SceneOmoMenuBase::create();
    create_help_window();
    create_command_window();
    create_status_windows();
    create_actor_equip_window();
    create_actor_equip_status_window();
    create_actor_equip_item_window();
}

void SceneOmoMenuEquip::start() {
    SceneOmoMenuBase::start();
    int index = status_window->index();
    if (index >= 0) {
        show_actor_equip_window(index);
    } else {
        status_window->select(0);
        status_window->activate();
    }
    selected_index_ = index >= 0 ? index : 0;
}

void SceneOmoMenuEquip::terminate() {
    SceneOmoMenuBase::terminate();
    if (command_window) command_window->remove_from_parent();
    if (status_window) status_window->remove_from_parent();
}

void SceneOmoMenuEquip::create_status_windows() {
    SceneOmoMenuBase::create_status_windows();
    status_window->set_handler("ok", [this] { on_status_window_ok(); });
    status_window->set_handler("cancel", [this] { on_status_window_cancel(); });
}

void SceneOmoMenuEquip::create_help_window() {
    SceneOmoMenuBase::create_help_window();
    help_window->set_width(640 - 175);
    help_window->set_height(109);
    help_window->x = 10;
    help_window->y = 480 - 244;
    help_window->create_contents();
    help_window->set_width(0);
}

void SceneOmoMenuEquip::create_actor_equip_window() {
    actor_equip_window = WindowBase::make<WindowOmoMenuActorEquip>();
    actor_equip_window->x = 10;
    actor_equip_window->y = 480;
    actor_equip_window->set_handler("ok", [this] { on_actor_equip_window_ok(); });
    actor_equip_window->set_handler("cancel", [this] { on_actor_equip_window_cancel(); });
    actor_equip_window->set_help_window(help_window.get());
    add_child(actor_equip_window);
    actor_equip_window->select(0);
    actor_equip_window->deactivate();
}

void SceneOmoMenuEquip::create_actor_equip_status_window() {
    actor_equip_status = WindowBase::make<WindowOmoMenuEquipStatus>();
    actor_equip_status->x = 180;
    actor_equip_status->y = 66;
    add_child(actor_equip_status);
    actor_equip_window->status_window = actor_equip_status.get();
    actor_equip_window->update_help();
}

void SceneOmoMenuEquip::create_actor_equip_item_window() {
    actor_equip_item_window = WindowBase::make<WindowOmoMenuActorEquipItem>();
    actor_equip_item_window->x = 0;
    actor_equip_item_window->y = 480 - actor_equip_item_window->height();
    actor_equip_item_window->set_width(0);
    actor_equip_item_window->set_handler("ok", [this] { on_actor_equip_item_window_ok(); });
    actor_equip_item_window->set_handler("cancel", [this] { on_actor_equip_item_window_cancel(); });
    actor_equip_item_window->set_help_window(help_window.get());
    actor_equip_item_window->set_status_window(actor_equip_status.get());
    add_child_at(actor_equip_item_window, 3);
}

WindowOmoMainMenuActorStatus* SceneOmoMenuEquip::status_at(int index) {
    if (index < 0 || index >= (int)status_window->status_windows.size()) return nullptr;
    return status_window->status_windows[(size_t)index].get();
}

void SceneOmoMenuEquip::on_status_window_ok() {
    selected_index_ = status_window->index();
    show_actor_equip_window(selected_index_);
}

void SceneOmoMenuEquip::on_status_window_cancel() {
    status_window->deselect();
    status_window->deactivate();
    pop_to_menu();
}

void SceneOmoMenuEquip::on_actor_equip_window_ok() {
    rpg::GameActor* a = actor_equip_window->actor();
    int slot_id = actor_equip_window->slot_id_at_index();
    actor_equip_item_window->open();
    actor_equip_item_window->set_actor(a);
    actor_equip_item_window->set_slot_id(slot_id);
    actor_equip_item_window->select(0);
    actor_equip_item_window->activate();
    show_help_window(15);
    show_item_list_window(15);
}

void SceneOmoMenuEquip::on_actor_equip_window_cancel() {
    if (rpg::GameActor* a = actor_equip_window->actor()) selected_index_ = a->index();
    hide_actor_equip_window(selected_index_);
}

void SceneOmoMenuEquip::on_actor_equip_item_window_ok() {
    rpg::GameActor* a = actor_equip_window->actor();
    int slot_id = actor_equip_window->slot_id_at_index() + 1;
    if (!a || a->is_equip_type_locked(slot_id)) {
        play_buzzer();
        actor_equip_item_window->activate();
        return;
    }
    play_equip();
    a->change_equip(slot_id - 1, actor_equip_item_window->item());
    actor_equip_item_window->refresh();
    actor_equip_item_window->activate();
    actor_equip_status->refresh();
    actor_equip_window->refresh();
    status_window->refresh();
}

void SceneOmoMenuEquip::on_actor_equip_item_window_cancel() {
    actor_equip_window->activate();
    actor_equip_item_window->deselect();
    actor_equip_status->set_temp_actor(nullptr);
    hide_item_list_window(15);
    if (!actor_equip_window->equipment_at_index().valid()) hide_help_window(15);
}

void SceneOmoMenuEquip::on_equip_cursor_change() {
    if (actor_equip_window->equipment_at_index().valid()) show_help_window(15);
    else hide_help_window(15);
}

void SceneOmoMenuEquip::show_item_list_window(int duration) { show_info_window(actor_equip_item_window.get(), duration); }

void SceneOmoMenuEquip::hide_item_list_window(int duration) { hide_info_window(actor_equip_item_window.get(), duration); }

void SceneOmoMenuEquip::show_actor_equip_window(int index) {
    clear_queue();
    clear_wait_mode();
    base_show_actor_equip_window(index);
    help_window->set_item(actor_equip_window->equipment_at_index(0));
}

void SceneOmoMenuEquip::base_show_actor_equip_window(int index) {
    actor_equip_window->select(0);
    actor_equip_window->update();
    actor_equip_window->deactivate();
    actor_equip_window->on_equip_cursor_change = [this] { on_equip_cursor_change(); };
    set_function_list_index(0);
    actor_equip_window->set_actor_index(index);
    status_window->deactivate();
    status_window->deselect();
    queue([this, index] {
        for (size_t i2 = 0; i2 < status_window->status_windows.size(); i2++) hide_actor_status((int)i2);
        show_actor_status(index, 10);
        if (WindowOmoMainMenuActorStatus* w = status_at(index)) w->animate_face(true);
    });
    queue_wait_mode("movement");
    rpg::ItemRef equip = actor_equip_window->equipment_at_index();
    int duration = 15;
    if (index > 0) {
        queue([this, index, duration] {
            WindowOmoMainMenuActorStatus* obj = status_at(index);
            if (!obj) return;
            Prop co{[obj] { return obj->contents_opacity(); }, [obj](double v) { obj->set_contents_opacity_all(v); }};
            move.start_move(ObjectMovement::tween(obj, {{prop_x(obj), 10, duration}, {prop_y(obj), -obj->height(), duration}, {co, 255, duration}},
                                                  ease_out_circ));
        });
        queue_wait_mode("movement");
    }
    queue([this, index, duration] {
        if (WindowOmoMainMenuActorStatus* obj = status_at(index)) {
            move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), -244, duration}}, ease_in_circ));
        }
        WindowOmoMenuActorEquip* w = actor_equip_window.get();
        move.start_move(ObjectMovement::tween(w, {{prop_y(w), 480 - w->height(), duration}}, ease_in_circ));
    });
    queue_wait_mode("movement");
    if (equip.valid()) queue([this] { show_help_window(15); });
    queue_wait_mode("movement");
    queue([this] {
        actor_equip_window->activate();
        actor_equip_status->open();
    });
    clear_function_list_index();
}

void SceneOmoMenuEquip::hide_actor_equip_window(int index) {
    clear_queue();
    clear_wait_mode();
    base_hide_actor_equip_window(index);
}

void SceneOmoMenuEquip::base_hide_actor_equip_window(int index) {
    actor_equip_window->on_equip_cursor_change = nullptr;
    if (WindowOmoMainMenuActorStatus* w = status_at(index)) w->animate_face(false);
    set_function_list_index(0);
    actor_equip_status->close();
    queue([this] { hide_help_window(15); });
    queue_wait_mode("movement");
    queue([this] { start_function_wait([this] { return actor_equip_status->is_closing(); }); });
    int duration = 15;
    queue([this, index, duration] {
        if (WindowOmoMainMenuActorStatus* obj = status_at(index)) {
            move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), -obj->height(), duration}}, ease_out_circ));
        }
        WindowOmoMenuActorEquip* w = actor_equip_window.get();
        move.start_move(ObjectMovement::tween(w, {{prop_y(w), 480, duration}}, ease_out_circ));
    });
    queue_wait_mode("movement");
    queue([this, index, duration] {
        WindowOmoMainMenuActorStatus* obj = status_at(index);
        if (!obj) return;
        Prop co{[obj] { return obj->contents_opacity(); }, [obj](double v) { obj->set_contents_opacity_all(v); }};
        move.start_move(ObjectMovement::tween(obj, {{prop_x(obj), obj->origin_x_, duration}, {prop_y(obj), -obj->height(), duration}, {co, 255, duration}},
                                              ease_in_circ));
    });
    queue_wait_mode("movement");
    queue([this] {
        for (size_t i2 = 0; i2 < status_window->status_windows.size(); i2++) show_actor_status((int)i2);
    });
    queue_wait_mode("movement");
    queue([this, index] {
        status_window->select(index);
        status_window->activate();
    });
    queue_wait_mode("movement");
    clear_function_list_index();
}

rpg::SceneFactory omo_menu_equip_scene() {
    return rpg::SceneFactory{"Scene_OmoMenuEquip", [] { return std::unique_ptr<rpg::Scene>(new SceneOmoMenuEquip()); }};
}

}

static SceneRegistrar r_scene_omo_menu_equip("Scene_OmoMenuEquip", menu::omo_menu_equip_scene());

}
