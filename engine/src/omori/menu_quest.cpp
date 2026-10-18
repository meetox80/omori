#include <algorithm>
#include <cmath>

#include "../js/vm.h"
#include "../rpg/data.h"
#include "../rpg/game.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "menu_common.h"
#include "message.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {
namespace menu {

namespace {

int quest_world_variable_id() {
    std::string raw = data::plugin_param("Omori Quest Menu", "World Variable ID", "");
    double v = raw.empty() ? 1 : rpg::js_number(raw);
    return std::isnan(v) ? 0 : (int)v;
}

std::string quest_opening_message() { return data::plugin_param("Omori Quest Menu", "Opening Message", ""); }

js::Val jget(const js::Val& o, const char* k) { return js::vm().get(o, js::atom(k)); }

}

class WindowOmoriQuestHeader : public WindowBase {
public:
    void initialize() {
        WindowBase::initialize(12, 12, window_width(), window_height());
        refresh();
    }
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 0; }
    double window_width() { return 164; }
    double window_height() { return 44; }
    void refresh() {
        c().clear();
        c().draw_text(data::js_string(plugin_data("questMenu", "header")), 0, -6, c().width(), c().height(), "center");
    }
};

class WindowOmoriQuestList : public WindowCommand {
public:
    void initialize(double x, double y) override {
        quest_list_.clear();
        WindowCommand::initialize(x, y);
        deactivate();
        deselect();
    }
    using WindowCommand::initialize;
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    double window_width() override { return 360; }
    double window_height() override { return 177; }
    int standard_padding() override { return 8; }
    double custom_cursor_rect_text_x_offset() override { return 35; }
    double line_height() override { return 25; }
    int standard_font_size() override { return 24; }

    void set_quest_list(const std::vector<js::Val>& list) {
        quest_list_ = list;
        refresh();
        reset_scroll();
    }

    std::vector<std::string> selected_quest_messages() {
        std::vector<std::string> out;
        int idx = index();
        if (idx < 0 || idx >= (int)quest_list_.size()) return out;
        const js::Val& quest = quest_list_[(size_t)idx];
        std::string id = jget(quest, "id").to_str();
        int message_index = jget(quest, "messageIndex").to_int();
        const Value& texts = data::quests()["quests"][id.c_str()]["text"][message_index];
        for (size_t i = 0; i < texts.size(); i++) out.push_back(data::js_string(texts[i]));
        return out;
    }

    void make_command_list() override {
        const std::string& language = data::language();
        for (const js::Val& quest : quest_list_) {
            std::string id = jget(quest, "id").to_str();
            const Value* loc = blue("Quest_Names").find(id.c_str());
            const Value& qd = data::quests()["quests"][id.c_str()];
            std::string name = (loc && loc->truthy()) ? data::js_string(*loc) : data::js_string(qd["name"][language.c_str()]);
            add_command(name, "quest", true, quest);
        }
    }

    void draw_item(int index) override {
        WindowCommand::draw_item(index);
        item_rect(index);
    }

    void process_cursor_move() override {
        if (rpg::game_message().is_busy()) return;
        WindowCommand::process_cursor_move();
    }

    void process_handling() override {
        if (rpg::game_message().is_busy()) return;
        WindowCommand::process_handling();
    }

    void update_custom_cursor_rect_sprite(SpriteWindowCustomCursor* sprite, int idx, bool use_index) override {
        sprite = custom_cursor_sprite_.get();
        if (sprite && rpg::game_message().is_busy()) {
            static const double tone[4] = {-80, -80, -80, 255};
            sprite->set_color_tone(tone);
            sprite->active_ = false;
            return;
        }
        WindowCommand::update_custom_cursor_rect_sprite(sprite, idx, use_index);
    }

protected:
    void update_transform_hook() override {
        double w = width(), h = height();
        double p = 24;
        double q = (p / 2) + 5;
        down_arrow_sprite()->move(w - q, h - q);
        up_arrow_sprite()->move(w - q, q);
    }

private:
    std::vector<js::Val> quest_list_;
};

class WindowOmoriQuestTypes : public WindowCommand {
public:
    void initialize(double x, double y) override {
        WindowCommand::initialize(x, y);
        activate();
        select(0);
    }
    using WindowCommand::initialize;
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    double window_width() override { return 184; }
    double window_height() override { return 81; }
    int standard_padding() override { return 8; }
    double custom_cursor_rect_text_x_offset() override { return 35; }
    double custom_cursor_rect_x_offset() override { return 10; }
    double line_height() override { return 28; }
    int standard_font_size() override { return 24; }

    int current_world() { return rpg::game_variables().value(quest_world_variable_id()).to_int(); }

    void make_command_list() override {
        int world = current_world();
        const Value& states = plugin_data("questMenu", "questStates");
        add_command(data::js_string(states[0]), "incomplete", !rpg::game_party().incomplete_quest_list(world).empty());
        add_command(data::js_string(states[1]), "complete", !rpg::game_party().completed_quest_list(world).empty());
    }

    void draw_item(int index) override {
        ItemRect r = item_rect_for_text(index);
        std::string align = item_text_align();
        reset_text_color();
        change_text_color(index == 1 ? "rgba(0, 161, 4, 1)" : "rgba(228, 50, 14, 1)");
        change_paint_opacity(is_command_enabled(index));
        draw_text(command_name(index), r.x, r.y, r.width, align);
    }

    void select(int index) override {
        WindowCommand::select(index);
        update_quest_list();
    }

    void update_quest_list() {
        if (quest_list_window) {
            int world = current_world();
            if (this->index() == 0) quest_list_window->set_quest_list(rpg::game_party().incomplete_quest_list(world));
            else quest_list_window->set_quest_list(rpg::game_party().completed_quest_list(world));
        }
    }

    void process_cursor_move() override {
        if (rpg::game_message().is_busy()) return;
        WindowCommand::process_cursor_move();
    }

    void process_handling() override {
        if (rpg::game_message().is_busy()) return;
        WindowCommand::process_handling();
    }

    void update_custom_cursor_rect_sprite(SpriteWindowCustomCursor* sprite, int idx, bool use_index) override {
        sprite = custom_cursor_sprite_.get();
        if (sprite && rpg::game_message().is_busy()) {
            static const double tone[4] = {-80, -80, -80, 255};
            sprite->set_color_tone(tone);
            sprite->active_ = false;
            return;
        }
        WindowCommand::update_custom_cursor_rect_sprite(sprite, idx, use_index);
    }

    WindowOmoriQuestList* quest_list_window = nullptr;
};


class QuestMessageController {
public:
    void create(rpg::Scene& scene) {
        clear_message_list();
        set_standby_message_ = false;
        window = create_message_window(scene);
        MessageHooks hooks;
        hooks.terminate_message = [this] {
            terminate_message();
            return true;
        };
        set_message_hooks(window.get(), std::move(hooks));
    }
    void clear_message_list() { message_list_.clear(); }
    void add_message(const std::string& message) { message_list_.push_back(message); }
    void set_stand_by_message() { set_standby_message_ = true; }
    void terminate_message() {
        rpg::game_message().clear();
        if (!message_list_.empty()) {
            std::string message = message_list_.front();
            message_list_.erase(message_list_.begin());
            rpg::game_message().show_language_message(message);
            return;
        }
        if (set_standby_message_) {
            const js::Val& standby = rpg::game_party().quest_stand_by_message;
            if (standby.truthy()) rpg::game_message().show_language_message(standby.to_str());
            set_standby_message_ = false;
        }
    }

    std::shared_ptr<WindowMessage> window;

private:
    std::vector<std::string> message_list_;
    bool set_standby_message_ = false;
};

class SceneOmoriQuest : public rpg::Scene {
public:
    const char* name() const override { return "Scene_OmoriQuest"; }

    void create() override {
        rpg::Scene::create();
        update_window_cursors_ = false;
        create_background();
        create_quest_windows();
        create_message_window();
    }

    void start() override {
        rpg::Scene::start();
        std::string opening = quest_opening_message();
        if (!opening.empty()) rpg::game_message().show_language_message(opening);
        update_window_cursors_ = true;
        update_cursor_sprites();
    }

    void terminate() override {
        rpg::Scene::terminate();
        if (message_.window) {
            set_message_hooks(message_.window.get(), MessageHooks());
            forget_message_window(message_.window.get());
        }
    }

    void create_background() {
        background_sprite_ = std::make_shared<g2d::Sprite>(rpg::scene_manager::background_bitmap());
        add_child(background_sprite_);
    }

    void create_quest_windows() {
        header_window_ = WindowBase::make<WindowOmoriQuestHeader>();
        add_child(header_window_);
        types_window_ = WindowBase::make<WindowOmoriQuestTypes>(12.0, 0.0);
        types_window_->y = header_window_->y + header_window_->height() + 2;
        types_window_->set_handler("ok", [this] { on_quest_types_ok(); });
        types_window_->set_handler("cancel", [this] { pop_scene(); });
        add_child(types_window_);
        list_window_ = WindowBase::make<WindowOmoriQuestList>(12.0, 0.0);
        list_window_->x = types_window_->x + types_window_->width() + 2;
        list_window_->y = types_window_->y;
        list_window_->set_handler("ok", [this] { on_quest_list_ok(); });
        list_window_->set_handler("cancel", [this] { on_quest_list_cancel(); });
        add_child(list_window_);
        types_window_->quest_list_window = list_window_.get();
        types_window_->call_update_help();
        types_window_->update_quest_list();
    }

    void create_message_window() {
        create_window_layer();
        message_.create(*this);
        std::shared_ptr<g2d::Sprite> face_box = message_face_box_container(message_.window.get());
        if (face_box) add_child(face_box);
    }

    void on_quest_types_ok() {
        list_window_->select(0);
        list_window_->activate();
    }

    void on_quest_list_ok() {
        list_window_->activate();
        message_.set_stand_by_message();
        std::vector<std::string> messages = list_window_->selected_quest_messages();
        if (!messages.empty()) rpg::game_message().show_language_message(messages[0]);
        message_.clear_message_list();
        for (size_t i = 1; i < messages.size(); i++) message_.add_message(messages[i]);
        update_window_cursors_ = true;
        update_cursor_sprites();
    }

    void on_quest_list_cancel() {
        types_window_->activate();
        list_window_->deselect();
    }

    void update() override {
        rpg::Scene::update();
        if (update_window_cursors_ && !rpg::game_message().is_busy()) {
            update_window_cursors_ = false;
            update_cursor_sprites();
        }
    }

    void update_cursor_sprites() {
        list_window_->update_custom_cursor_rect_sprite(nullptr, 0, false);
        types_window_->update_custom_cursor_rect_sprite(nullptr, 0, false);
    }

private:
    bool update_window_cursors_ = false;
    std::shared_ptr<g2d::Sprite> background_sprite_;
    std::shared_ptr<WindowOmoriQuestHeader> header_window_;
    std::shared_ptr<WindowOmoriQuestTypes> types_window_;
    std::shared_ptr<WindowOmoriQuestList> list_window_;
    QuestMessageController message_;
};

}

rpg::SceneFactory quest_scene() {
    return rpg::SceneFactory{"Scene_OmoriQuest", [] { return std::unique_ptr<rpg::Scene>(new menu::SceneOmoriQuest()); }};
}

static SceneRegistrar r_scene_omori_quest("Scene_OmoriQuest", quest_scene());

}
