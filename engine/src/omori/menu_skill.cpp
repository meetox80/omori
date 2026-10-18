#include <algorithm>
#include <cmath>

#include "../rpg/audio_manager.h"
#include "../rpg/config.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/party.h"
#include "menu_common.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {
namespace menu {

namespace {

Prop status_contents_opacity(WindowOmoMainMenuActorStatus* w) {
    return Prop{[w] { return w->contents_opacity(); }, [w](double v) { w->set_contents_opacity_all(v); }};
}

std::string sceneskill_text(const char* key) { return data::js_string(blue("Omori_Mainmenu_Sceneskill")[key]); }

}

class WindowSkillList : public WindowSelectable {
public:
    void initialize(double x, double y, double w, double h) override {
        WindowSelectable::initialize(x, y, w, h);
        actor_ = nullptr;
        stype_id_ = 0;
        data_.clear();
        has_data_ = true;
    }
    virtual void set_actor(rpg::GameActor* actor) {
        if (actor_ != actor) {
            actor_ = actor;
            refresh();
            reset_scroll();
        }
    }
    int max_cols() override { return (int)yparam_num("SCCWindowCol", 2); }
    int spacing() override { return 48; }
    int max_items() override { return has_data_ ? (int)data_.size() : 1; }
    virtual rpg::ItemRef item() {
        int idx = index();
        if (!has_data_ || idx < 0 || idx >= (int)data_.size()) return rpg::ItemRef();
        return data_[(size_t)idx];
    }
    bool is_current_item_enabled() override {
        int idx = index();
        rpg::ItemRef it = idx >= 0 && idx < (int)data_.size() ? data_[(size_t)idx] : rpg::ItemRef();
        return is_enabled(it);
    }
    virtual bool includes(const rpg::ItemRef& item) { return item.valid() && item.data()["stypeId"].as_int() == stype_id_; }
    virtual bool is_enabled(const rpg::ItemRef& item) { return actor_ && actor_->can_use(item); }
    virtual void make_item_list() {
        data_.clear();
        has_data_ = true;
        if (actor_) {
            for (const rpg::ItemRef& s : actor_->skills()) {
                if (includes(s)) data_.push_back(s);
            }
        }
    }
    void update_help() override { set_help_window_item(item()); }
    void refresh() override {
        make_item_list();
        create_contents();
        draw_all_items();
    }

protected:
    rpg::GameActor* actor_ = nullptr;
    int stype_id_ = 0;
    std::vector<rpg::ItemRef> data_;
    bool has_data_ = false;
};

class WindowOmoMenuActorSkillEquip : public WindowSelectable {
public:
    void initialize() {
        WindowSelectable::initialize(0, 0, window_width(), window_height());
        actor_index_ = 0;
        deactivate();
        refresh();
    }
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 0; }
    double window_width() { return 155; }
    double window_height() { return 135; }
    int max_items() override { return 4; }
    int max_cols() override { return 1; }
    double item_height() override { return 24; }
    double item_width() override { return window_width() - 50; }
    int spacing() override { return 0; }
    double custom_cursor_rect_x_offset() override { return -35; }
    double custom_cursor_rect_y_offset() override { return 10; }
    void set_actor_index(int index) {
        if (actor_index_ != index) {
            actor_index_ = index;
            refresh();
        }
    }
    rpg::GameActor* actor() {
        auto m = rpg::game_party().members();
        if (actor_index_ < 0 || actor_index_ >= (int)m.size()) return nullptr;
        return m[(size_t)actor_index_];
    }
    rpg::ItemRef skill_at_index(int index) {
        rpg::GameActor* a = actor();
        if (!a) return rpg::ItemRef();
        auto skills = a->skills();
        if (index < 0 || index >= (int)skills.size()) return rpg::ItemRef();
        return skills[(size_t)index];
    }
    rpg::ItemRef skill_at_index() { return skill_at_index(index()); }
    ItemRect item_rect(int index) override {
        ItemRect r = WindowSelectable::item_rect(index);
        r.x += 42;
        r.y += 24;
        return r;
    }
    bool is_item_enabled_at(int index) {
        rpg::GameActor* a = actor();
        if (!a) return false;
        rpg::ItemRef equipped = skill_at_index(index);
        if (a->is_equip_skill_slot_locked(index)) return false;
        if (equipped.valid()) return true;
        if (a->equipable_skills().empty()) return false;
        return true;
    }
    bool is_current_item_enabled() override { return is_item_enabled_at(index()); }
    void refresh() override {
        WindowSelectable::refresh();
        reset_font_settings();
        c().font_size = 20;
        change_paint_opacity(true);
        c().fill_rect(4, 28, width() - 8, 2, "rgba(255, 255, 255, 1)");
        draw_text(data::js_string(plugin_data("skillMenu", "skills")), 0, -4, width(), "center");
    }
    void draw_item(int index) override {
        ItemRect r = item_rect(index);
        rpg::ItemRef skill = skill_at_index(index);
        bool enabled = is_item_enabled_at(index);
        if (enabled) {
            change_paint_opacity(true);
            c().text_color = "rgba(255, 255, 255, 1)";
        } else {
            change_paint_opacity(false);
            c().text_color = "rgba(140, 140, 140, 1)";
        }
        std::string text = skill.valid() ? item_name(skill) : std::string("------------");
        c().font_size = blue("Window_OmoMenuActorSkillEquip")["skillsFontSize"].as_int();
        c().draw_text(text, r.x, r.y + 5, r.width, r.height, "left");
        change_paint_opacity(true);
    }
    void update_help() override {
        WindowSelectable::update_help();
        help_window->set_item(skill_at_index());
        call_handler("onCursorChange");
    }

private:
    int actor_index_ = 0;
};

class WindowOmoMenuSkillEquipControl : public WindowCommand {
public:
    void initialize() {
        WindowCommand::initialize(0.0, 0.0);
        create_bubble_sprites();
        set_openness(0);
        deactivate();
        refresh();
    }
    void initialize(double x, double y) override { WindowCommand::initialize(x, y); }
    void initialize(double x, double y, double w, double h) override { WindowCommand::initialize(x, y, w, h); }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_openness_type() override { return 2; }
    int standard_padding() override { return 4; }
    double window_width() override { return 115; }
    double window_height() override { return 95; }
    double line_height() override { return 26; }
    int standard_font_size() override { return 28; }
    int spacing() override { return 0; }
    double custom_cursor_rect_text_x_offset() override { return 32; }
    double custom_cursor_rect_y_offset() override { return 5; }
    void make_command_list() override {
        add_command(sceneskill_text("use"), "use");
        add_command(sceneskill_text("swap"), "swap");
    }
    void create_bubble_sprites() {
        bubble_sprites_.clear();
        static const double positions[2][3] = {{9, -45, 110}, {13, -20, 90}};
        for (int i = 0; i < 2; i++) {
            const double* pos = positions[i];
            auto sprite = std::make_shared<g2d::Sprite>(make_bubble_bitmap(pos[0]));
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
    void update_open() override {
        if (opening_) {
            bool prev_done = false;
            for (size_t i = 0; i < bubble_sprites_.size(); i++) {
                auto& sprite = bubble_sprites_[i];
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
    void update_close() override {
        if (closing_) {
            set_openness(openness() - 32);
            if (is_closed()) {
                bool prev_done = false;
                for (int i = (int)bubble_sprites_.size() - 1; i >= 0; i--) {
                    auto& sprite = bubble_sprites_[(size_t)i];
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
    g2d::BitmapPtr make_bubble_bitmap(double size) {
        g2d::BitmapPtr bitmap = g2d::Bitmap::create((int)(size * 2), (int)(size * 2));
        double x = size, y = size;
        bitmap->draw_circle(x, y, size, "rgba(0, 0, 0, 1)");
        bitmap->draw_circle(x, y, size - 1, "rgba(255, 255, 255, 1)");
        bitmap->draw_circle(x, y, size - 3, "rgba(0, 0, 0, 1)");
        return bitmap;
    }
    void refresh() override {
        WindowCommand::refresh();
        c().font_size = 24;
        draw_text(sceneskill_text("do_what"), 10, 0, c().width());
    }
    ItemRect item_rect(int index) override {
        ItemRect r = WindowCommand::item_rect(index);
        r.y += 24;
        return r;
    }
    void update() override {
        WindowCommand::update();
        update_bubble_sprites();
    }
    void update_bubble_sprites() {
        double s = std::sin(rpg::frame_count() * 0.05) * 5;
        for (size_t i = 0; i < bubble_sprites_.size(); i++) {
            auto& sprite = bubble_sprites_[i];
            if (i == 0) sprite->y = bubble_origin_y_[i] + 0.5 + s;
            else sprite->y = bubble_origin_y_[i] + 0.5 - s;
        }
    }

private:
    std::vector<g2d::SpritePtr> bubble_sprites_;
    std::vector<double> bubble_origin_y_;
};

class WindowOmoMenuActorSkillList : public WindowSkillList {
public:
    void initialize() {
        WindowSkillList::initialize(0, 0, window_width(), window_height());
        actor_ = nullptr;
        deselect();
        deactivate();
    }
    void initialize(double x, double y, double w, double h) override { WindowSkillList::initialize(x, y, w, h); }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 4; }
    double window_width() { return 170 + 277 + 18; }
    double window_height() { return 135; }
    int max_cols() override { return 1 + 1; }
    double item_height() override { return 32 + 3 - 3; }
    double item_width() override { return 160; }
    double custom_cursor_rect_x_offset() override { return 0; }
    double custom_cursor_rect_y_offset() override { return 5; }
    double custom_cursor_rect_text_x_offset() override { return 25; }
    void play_ok_sound() override {}
    double contents_width() override { return window_width() - standard_padding() * 2; }
    double contents_height() override { return window_height() - standard_padding() * 2; }
    void set_actor(rpg::GameActor* actor) override {
        if (actor_ != actor) {
            actor_ = actor;
            refresh();
        }
    }
    void make_item_list() override {
        data_.clear();
        has_data_ = true;
        if (actor_) {
            for (const rpg::ItemRef& s : actor_->equipable_skills()) {
                if (includes(s)) data_.push_back(s);
            }
        }
        data_.push_back(rpg::ItemRef());
    }
    bool includes(const rpg::ItemRef&) override { return true; }
    bool is_enabled(const rpg::ItemRef&) override { return true; }
    ItemRect item_rect(int index) override {
        ItemRect r = WindowSkillList::item_rect(index);
        r.x += 8;
        r.y += 22;
        return r;
    }
    void refresh() override {
        WindowSkillList::refresh();
        c().font_size = 22;
        draw_text(data::js_string(plugin_data("skillMenu", "replace")), 8, -8, c().width());
    }
    void draw_item(int index) override {
        ItemRect r = item_rect_for_text(index);
        rpg::ItemRef it = index >= 0 && index < (int)data_.size() ? data_[(size_t)index] : rpg::ItemRef();
        std::string text = it.valid() ? item_name(it) : std::string("------------");
        c().font_size = 24;
        c().draw_text(text, r.x, r.y, r.width, r.height, "left");
    }

protected:
    void update_transform_hook() override {
        double w = width(), h = height();
        double p = 24, q = p / 2;
        down_arrow_sprite()->move(w - q, h - q);
        up_arrow_sprite()->move(w - q, q + 24);
        down_arrow_sprite()->visible = down_arrow_sprite()->visible && x > 10;
        up_arrow_sprite()->visible = up_arrow_sprite()->visible && x > 10;
    }
};

class SceneOmoMenuSkill : public SceneOmoMenuBase {
public:
    const char* name() const override { return "Scene_OmoMenuSkill"; }

    void create() override {
        SceneOmoMenuBase::create();
        create_help_window();
        create_command_window();
        create_status_windows();
        create_actor_skill_equip_window();
        create_actor_skill_list_window();
        create_actor_skill_control_window();
    }

    void terminate() override {
        SceneOmoMenuBase::terminate();
        remove_children();
    }

    void start() override {
        skill_ = rpg::ItemRef();
        SceneOmoMenuBase::start();
        int index = status_window->index();
        if (index >= 0) {
            show_actor_skill_equip_window(index);
        } else {
            status_window->select(0);
            status_window->activate();
        }
        selected_index_ = index >= 0 ? index : 0;
    }

    void create_status_windows() override {
        SceneOmoMenuBase::create_status_windows();
        status_window->set_handler("ok", [this] { on_status_window_ok(); });
        status_window->set_handler("cancel", [this] { on_status_window_cancel(); });
        status_window->refresh();
    }

    void create_help_window() override {
        SceneOmoMenuBase::create_help_window();
        help_window->set_width(640 - 175);
        help_window->set_height(109);
        help_window->x = 10;
        help_window->y = 480 - 244;
        help_window->create_contents();
        help_window->set_width(0);
    }

    void create_actor_skill_equip_window() {
        equip_window_ = WindowBase::make<WindowOmoMenuActorSkillEquip>();
        equip_window_->x = 10;
        equip_window_->y = 480;
        equip_window_->set_handler("ok", [this] { on_actor_equip_skill_window_ok(); });
        equip_window_->set_handler("cancel", [this] { on_actor_equip_skill_window_cancel(); });
        equip_window_->set_help_window(help_window.get());
        add_child(equip_window_);
        equip_window_->select(0);
        equip_window_->deactivate();
        equip_window_->activate();
    }

    void create_actor_skill_control_window() {
        control_window_ = WindowBase::make<WindowOmoMenuSkillEquipControl>();
        control_window_->x = 180;
        control_window_->y = 75;
        control_window_->set_handler("use", [this] { on_actor_skill_control_use(); });
        control_window_->set_handler("swap", [this] { on_actor_skill_control_swap(); });
        control_window_->set_handler("cancel", [this] { on_actor_skill_control_cancel(); });
        add_child(control_window_);
    }

    void create_actor_skill_list_window() {
        list_window_ = WindowBase::make<WindowOmoMenuActorSkillList>();
        list_window_->x = 0;
        list_window_->y = 480 - list_window_->height();
        list_window_->set_width(0);
        list_window_->set_handler("ok", [this] { on_actor_skill_list_window_ok(); });
        list_window_->set_handler("cancel", [this] { on_actor_skill_list_window_cancel(); });
        list_window_->set_help_window(help_window.get());
        add_child_at(list_window_, 3);
    }

    rpg::ItemRef item() override { return skill_; }

    rpg::GameBattler* user() override { return status_window->actor(0); }

    void play_se_for_item() override {
        if (skill_.valid() && item_meta_has(skill_, "PlayCookSound")) {
            play_se("BA_Heart_Heal", rpg::config().se_volume, 100, 0);
            return;
        }
        rpg::audio_manager::play_system_sound(23);
    }

    void on_status_window_ok() {
        if (skill_.valid()) {
            if (can_use()) use_item();
            else play_buzzer();
            status_window->activate();
        } else {
            selected_index_ = status_window->index();
            show_actor_skill_equip_window(selected_index_);
        }
    }

    void on_status_window_cancel() {
        if (skill_.valid()) {
            auto& windows = status_window->status_windows;
            for (size_t i2 = 0; i2 < windows.size(); i2++) {
                if (i2 != 0) hide_actor_status((int)i2);
            }
            status_window->set_cursor_all(false);
            status_window->sort_status_windows(false);
            status_window->deselect();
            equip_window_->activate();
            skill_ = rpg::ItemRef();
        } else {
            status_window->deselect();
            status_window->deactivate();
            pop_to_menu();
        }
    }

    void on_actor_equip_skill_window_ok() {
        int index = equip_window_->index();
        rpg::ItemRef skill = equip_window_->skill_at_index();
        rpg::GameActor* actor = equip_window_->actor();
        skill_ = skill;
        std::vector<rpg::GameBattler*> members;
        for (rpg::GameActor* a : rpg::game_party().members()) members.push_back(a);
        if (skill.valid() && (can_use(members) || item_meta_has(skill, "AlwaysUsableInMenu"))) {
            control_window_->select(0);
            control_window_->open();
            control_window_->activate();
        } else {
            if (actor && actor->is_equip_skill_slot_locked(index)) equip_window_->activate();
            else on_actor_skill_control_swap();
        }
        skill_ = rpg::ItemRef();
    }

    void on_actor_equip_skill_window_cancel() {
        status_window->sort_status_windows(false);
        hide_actor_equip_window(selected_index_);
    }

    void on_actor_skill_control_use() {
        clear_queue();
        clear_wait_mode();
        control_window_->close();
        set_function_list_index(0);
        queue([this] {
            auto& windows = status_window->status_windows;
            for (size_t i2 = 0; i2 < windows.size(); i2++) {
                if ((int)i2 != selected_index_) show_actor_status((int)i2);
            }
            status_window->reorder_status_windows(selected_index_, 0);
            hide_help_window();
        });
        queue_wait_mode("movement");
        queue([this] {
            skill_ = equip_window_->skill_at_index();
            status_window->ok_sound_enabled = false;
            if (is_item_for_all()) {
                status_window->set_cursor_text(sceneskill_text("use_on_all"));
                status_window->set_cursor_all(true);
            } else {
                status_window->set_cursor_text(sceneskill_text("use_on_who"));
                status_window->set_cursor_all(false);
            }
            status_window->activate();
            status_window->select(0);
        });
        clear_function_list_index();
    }

    void on_actor_skill_control_swap() {
        rpg::GameActor* actor = equip_window_->actor();
        show_help_window();
        show_skill_list_window();
        control_window_->close();
        list_window_->set_actor(actor);
        list_window_->select(0);
        list_window_->activate();
    }

    void on_actor_skill_control_cancel() {
        control_window_->close();
        equip_window_->activate();
    }

    void on_actor_skill_list_window_ok() {
        rpg::GameActor* actor = equip_window_->actor();
        rpg::ItemRef skill = list_window_->item();
        int slot_index = equip_window_->index();
        play_equip();
        if (actor) {
            if (skill.valid()) {
                actor->unequip_skill(slot_index, true);
                actor->equip_skill(slot_index, skill.id);
            } else {
                actor->unequip_skill(slot_index, true);
            }
        }
        equip_window_->refresh();
        list_window_->refresh();
        list_window_->activate();
    }

    void on_actor_skill_list_window_cancel() {
        equip_window_->activate();
        list_window_->deselect();
        hide_skill_list_window(15);
        if (!equip_window_->skill_at_index().valid()) hide_help_window(15);
    }

    void on_skill_equip_cursor_change() {
        if (equip_window_->skill_at_index().valid()) show_help_window(15);
        else hide_help_window(15);
    }

    void show_skill_list_window(int duration = 15) { show_info_window(list_window_.get(), duration); }

    void hide_skill_list_window(int duration = 15) { hide_info_window(list_window_.get(), duration); }

    WindowOmoMainMenuActorStatus* status_at(int index) {
        auto& windows = status_window->status_windows;
        if (index < 0 || index >= (int)windows.size()) return nullptr;
        return windows[(size_t)index].get();
    }

    void show_actor_skill_equip_window(int index) {
        clear_queue();
        clear_wait_mode();
        equip_window_->select(0);
        equip_window_->update();
        equip_window_->deactivate();
        equip_window_->set_handler("onCursorChange", [this] { on_skill_equip_cursor_change(); });
        set_function_list_index(0);
        equip_window_->set_actor_index(index);
        status_window->deactivate();
        status_window->deselect();
        queue([this, index] {
            auto& windows = status_window->status_windows;
            for (size_t i2 = 0; i2 < windows.size(); i2++) hide_actor_status((int)i2);
            show_actor_status(index, 10);
            if (WindowOmoMainMenuActorStatus* w = status_at(index)) w->animate_face(true);
        });
        queue_wait_mode("movement");
        rpg::ItemRef skill = equip_window_->skill_at_index();
        int duration = 15;
        if (index > 0) {
            queue([this, index, duration] {
                WindowOmoMainMenuActorStatus* obj = status_at(index);
                if (!obj) return;
                move.start_move(ObjectMovement::tween(
                    obj, {{prop_x(obj), 10, duration}, {prop_y(obj), -obj->height(), duration}, {status_contents_opacity(obj), 255, duration}},
                    ease_out_circ));
            });
            queue_wait_mode("movement");
        }
        queue([this, index, duration] {
            if (WindowOmoMainMenuActorStatus* obj = status_at(index)) {
                move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), -244, duration}}, ease_in_circ));
            }
            WindowOmoMenuActorSkillEquip* ew = equip_window_.get();
            move.start_move(ObjectMovement::tween(ew, {{prop_y(ew), 480 - ew->height(), duration}}, ease_in_circ));
        });
        queue_wait_mode("movement");
        queue([this] { equip_window_->activate(); });
        if (skill.valid()) queue([this] { show_help_window(15); });
        queue_wait_mode("movement");
        clear_function_list_index();
    }

    void hide_actor_equip_window(int index) {
        clear_queue();
        clear_wait_mode();
        equip_window_->set_handler("onCursorChange", nullptr);
        if (WindowOmoMainMenuActorStatus* w = status_at(index)) w->animate_face(false);
        status_window->ok_sound_enabled = true;
        set_function_list_index(0);
        queue([this] { hide_help_window(15); });
        queue_wait_mode("movement");
        int duration = 15;
        queue([this, index, duration] {
            if (WindowOmoMainMenuActorStatus* obj = status_at(index)) {
                move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), -obj->height(), duration}}, ease_out_circ));
            }
            WindowOmoMenuActorSkillEquip* ew = equip_window_.get();
            move.start_move(ObjectMovement::tween(ew, {{prop_y(ew), 480, duration}}, ease_out_circ));
        });
        queue_wait_mode("movement");
        queue([this, index, duration] {
            WindowOmoMainMenuActorStatus* obj = status_at(index);
            if (!obj) return;
            move.start_move(ObjectMovement::tween(obj,
                                                  {{prop_x(obj), obj->origin_x_, duration},
                                                   {prop_y(obj), -obj->height(), duration},
                                                   {status_contents_opacity(obj), 255, duration}},
                                                  ease_in_circ));
        });
        queue_wait_mode("movement");
        queue([this] {
            status_window->sort_status_windows();
            auto& windows = status_window->status_windows;
            for (size_t i2 = 0; i2 < windows.size(); i2++) show_actor_status((int)i2);
        });
        queue_wait_mode("movement");
        queue([this, index] {
            status_window->set_cursor_text(data::js_string(blue("Omori_Mainmenu")["select_who"]));
            status_window->select(index);
            status_window->activate();
        });
        clear_function_list_index();
    }

private:
    rpg::ItemRef skill_;
    int selected_index_ = 0;
    std::shared_ptr<WindowOmoMenuActorSkillEquip> equip_window_;
    std::shared_ptr<WindowOmoMenuSkillEquipControl> control_window_;
    std::shared_ptr<WindowOmoMenuActorSkillList> list_window_;
};

rpg::SceneFactory omo_menu_skill_scene() {
    return rpg::SceneFactory{"Scene_OmoMenuSkill", [] { return std::unique_ptr<rpg::Scene>(new SceneOmoMenuSkill()); }};
}

static SceneRegistrar r_scene_omo_menu_skill("Scene_OmoMenuSkill", omo_menu_skill_scene());

}
}
