#include <algorithm>
#include <cmath>
#include <map>

#include "../js/compile.h"
#include "../js/vm.h"
#include "../rpg/audio_manager.h"
#include "../rpg/bind_util.h"
#include "../rpg/data.h"
#include "../rpg/evalctx.h"
#include "../rpg/game.h"
#include "../rpg/params.h"
#include "../rpg/party.h"
#include "menu_common.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {
namespace menu {

class SceneOmoMenuItem;
class WindowOmoMenuItemList;

struct ShowHideLayout {
    double hide_y = 0, hide_height = 0, show_y = 0, show_height = 0;
};

class WindowOmoMenuItemCategory : public WindowCommand {
public:
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 10; }
    double window_width() override { return 384; }
    int max_cols() override { return 3; }
    double line_height() override { return 24; }
    int standard_font_size() override { return blue("Window_OmoMenuItemCategory")["standardFontSize"].as_int(); }
    double custom_cursor_rect_text_x_offset() override { return 25; }
    void make_command_list() override;
    ItemRect item_rect(int index) override;
    void select(int index) override;
    void update_category();

    WindowOmoMenuItemList* item_window = nullptr;
};

class WindowOmoMenuItemList : public WindowItemList {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowItemList::initialize(x, y, w, h); }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 4; }
    virtual double window_width() { return 383 - 147; }
    virtual double window_height() { return 144 - 10; }
    int max_cols() override { return 1; }
    double item_height() override { return 26; }
    double item_width() override { return 200; }
    double custom_cursor_rect_x_offset() override { return -2; }
    bool is_enabled(const rpg::ItemRef&) override { return true; }
    double custom_cursor_rect_text_x_offset() override { return 24; }
    double contents_width() override { return window_width() - standard_padding() * 2; }
    double contents_height() override { return window_height() - standard_padding() * 2; }
    void set_category(const std::string& category) override;
    bool includes(const rpg::ItemRef& item) override;
    void make_item_list() override;
    ItemRect item_rect(int index) override;
    void clear_item(int index) override;
    void draw_item(int index) override;

protected:
    void update_transform_hook() override;
};

class WindowOmoMenuItemConfirmation : public WindowCommand {
public:
    void initialize(double x, double y) override;
    using WindowCommand::initialize;
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 5; }
    double window_width() override { return 115; }
    double window_height() override { return 68; }
    double line_height() override { return 24; }
    int standard_font_size() override { return blue("Window_OmoMenuItemConfirmation")["standardFontSize"].as_int(); }
    int spacing() override { return 0; }
    double custom_cursor_rect_text_x_offset() override { return 30; }
    void set_item(const rpg::ItemRef& item);
    void make_command_list() override;

    SceneOmoMenuItem* scene = nullptr;

private:
    rpg::ItemRef item_;
};

class WindowOmoMenuItemTrashPromptWindow : public WindowCommand {
public:
    void initialize(double x, double y) override;
    using WindowCommand::initialize;
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    double window_height() override { return 84 - 16; }
    double window_width() override { return 125 + 50; }
    int max_cols() override { return 2; }
    int spacing() override { return 0; }
    int standard_padding() override { return 0; }
    double item_height() override { return 20; }
    double item_width() override { return 75 + 10; }
    int standard_font_size() override { return blue("Window_OmoMenuItemTrashPromptWindow")["standardFontSize"].as_int(); }
    void make_command_list() override;
    void refresh() override;
    ItemRect item_rect(int index) override;
    ItemRect item_rect_for_text(int index) override;

private:
    std::string prompt_text_;
    bool has_prompt_text_ = false;
};

class SceneOmoMenuItem : public SceneOmoMenuBase {
public:
    const char* name() const override { return "Scene_OmoMenuItem"; }
    void create() override;
    bool is_busy() override;
    void start() override;
    void terminate() override;
    void create_status_windows() override;
    void create_help_window() override;
    void create_category_window();
    void create_item_list_window();
    void create_item_confirmation_window();
    void create_item_trash_prompt_window();
    rpg::ItemRef item() override;
    rpg::GameBattler* user() override;
    void play_se_for_item() override;
    void on_item_category_ok();
    void on_item_category_cancel();
    void on_item_list_ok();
    void on_item_list_cancel();
    void on_item_confirmation_use();
    void on_item_confirmation_trash();
    void on_item_confirmation_cancel();
    void on_item_trash_prompt_ok();
    void on_item_trash_prompt_cancel();
    void on_status_window_ok();
    void on_status_window_cancel();
    void show_window(g2d::Window* obj, int duration);
    void hide_window(g2d::Window* obj, int duration);
    void run_before_apply_eval(const std::string& code, const rpg::ItemRef& item, const Value& text, int text_index);

    std::shared_ptr<WindowOmoMenuItemCategory> item_category_window;
    std::shared_ptr<WindowOmoMenuItemList> item_list_window;
    std::shared_ptr<WindowOmoMenuItemConfirmation> item_confirmation_window;
    std::shared_ptr<WindowOmoMenuItemTrashPromptWindow> item_trash_prompt_window;

private:
    std::map<const g2d::Window*, ShowHideLayout> layouts_;
    void base_on_status_window_ok();
    void base_on_item_trash_prompt_ok();
};

void WindowOmoMenuItemCategory::make_command_list() {
    const Value& text = plugin_data("itemMenu", "categories");
    rpg::GameParty& party = rpg::game_party();
    add_command(data::js_string(text[0]), "consumables", party.has_consumable_items());
    add_command(data::js_string(text[1]), "toys", party.has_toy_items());
    add_command(data::js_string(text[2]), "important", party.has_key_items());
}

ItemRect WindowOmoMenuItemCategory::item_rect(int index) {
    ItemRect r = WindowCommand::item_rect(index);
    r.width += 20;
    r.y -= 3;
    if (index == 1) r.x += 5;
    return r;
}

void WindowOmoMenuItemCategory::select(int index) {
    WindowCommand::select(index);
    update_category();
}

void WindowOmoMenuItemList::initialize() {
    WindowItemList::initialize(0, 0, window_width(), window_height());
    set_category("consumables");
    deselect();
    deactivate();
    set_openness(0);
}

void WindowOmoMenuItemList::set_category(const std::string& category) {
    if (category_ != category) {
        category_ = category;
        refresh();
    }
}

bool pocket_includes(const rpg::ItemRef& item, const std::string& category) {
    if (item.valid() && item.is_item()) {
        if (item_meta_has(item, "HideInMenu")) return false;
        const Value& d = item.data();
        if (category == "consumables") return rpg::is_consumable_item(d);
        if (category == "toys") return rpg::is_toy_item(d);
        if (category == "important") return rpg::is_key_item(d);
    }
    return false;
}

void sort_pocket_items(std::vector<rpg::ItemRef>& items, const std::string& category) {
    auto list_index = [](const rpg::ItemRef& it) -> double {
        const Value& meta = it.data()["meta"];
        const Value* li = meta.find("ListIndex");
        if (!li) return it.id;
        if (li->is_num()) return li->num();
        if (li->t == Value::Bool) return li->b ? 1 : 0;
        return js::string_to_number(li->string());
    };
    std::stable_sort(items.begin(), items.end(), [&](const rpg::ItemRef& a, const rpg::ItemRef& b) { return list_index(a) - list_index(b) < 0; });
    if (category == "important") {
        auto priority = [](const rpg::ItemRef& it) -> double {
            const Value& meta = it.data()["meta"];
            const Value* p = meta.find("ItemPriority");
            if (!p || !p->truthy()) return 0;
            if (p->is_num()) return std::trunc(p->num());
            return rpg::js_parse_int(data::js_string(*p));
        };
        std::stable_sort(items.begin(), items.end(), [&](const rpg::ItemRef& a, const rpg::ItemRef& b) { return priority(b) - priority(a) < 0; });
    }
}

bool WindowOmoMenuItemList::includes(const rpg::ItemRef& item) { return pocket_includes(item, category_); }

void WindowOmoMenuItemList::make_item_list() {
    WindowItemList::make_item_list();
    sort_pocket_items(data_, category_);
}

ItemRect WindowOmoMenuItemList::item_rect(int index) {
    ItemRect r = WindowItemList::item_rect(index);
    r.x += 12;
    r.y += 8;
    return r;
}

void WindowOmoMenuItemList::update_transform_hook() {
    double w = width(), h = height();
    double p = 24, q = p / 2;
    down_arrow_sprite()->move(w - q, h - q);
    up_arrow_sprite()->move(w - q, q);
    down_arrow_sprite()->visible = down_arrow_sprite()->visible && active;
    up_arrow_sprite()->visible = up_arrow_sprite()->visible && active;
}

void WindowOmoMenuItemList::clear_item(int index) {
    ItemRect r = item_rect(index);
    c().clear_rect(r.x, r.y, r.width + 5, r.height);
}

void WindowOmoMenuItemList::draw_item(int index) {
    ItemRect r = item_rect_for_text(index);
    if (index < 0 || index >= (int)data_.size()) return;
    rpg::ItemRef it = data_[(size_t)index];
    if (!it.valid()) return;
    const Value& loc = blue("Window_OmoMenuItemList");
    c().font_size = loc["drawItem_contents_fontsize"].as_int();
    r.width -= 20;
    c().draw_text(item_name(it), r.x, r.y, r.width, r.height, "left");
    r.width += 5;
    const Value& pos = loc["drawItem_drawText_position"];
    js::Val rect = js_rect(r);
    double px = lang_num(pos[0], {{"rect", rect}});
    double py = lang_num(pos[1], {{"rect", rect}});
    c().draw_text("x" + num_str(rpg::game_party().num_items(it)), px, py, r.width, r.height, "left");
}

void WindowOmoMenuItemConfirmation::initialize(double x, double y) {
    WindowCommand::initialize(x, y);
    deactivate();
    set_openness(0);
    item_ = rpg::ItemRef();
    refresh();
}

void WindowOmoMenuItemConfirmation::set_item(const rpg::ItemRef& item) {
    if (item != item_ || item.valid() != item_.valid()) {
        item_ = item;
        refresh();
    }
}

void WindowOmoMenuItemTrashPromptWindow::initialize(double x, double y) {
    WindowCommand::initialize(x, y);
    prompt_text_ = data::js_string(plugin_data("itemMenu", "itemTrash")["text"]);
    has_prompt_text_ = true;
    refresh();
    set_openness(0);
    deactivate();
}

void WindowOmoMenuItemTrashPromptWindow::make_command_list() {
    const Value& text = plugin_data("itemMenu", "itemTrash")["commands"];
    add_command(data::js_string(text[0]), "yes");
    add_command(data::js_string(text[1]), "cancel");
}

void WindowOmoMenuItemTrashPromptWindow::refresh() {
    WindowCommand::refresh();
    if (has_prompt_text_) draw_text(prompt_text_, 0, 0, c().width(), "center");
}

ItemRect WindowOmoMenuItemTrashPromptWindow::item_rect(int index) {
    ItemRect r = WindowCommand::item_rect(index);
    r.y += line_height() - 5;
    return r;
}

ItemRect WindowOmoMenuItemTrashPromptWindow::item_rect_for_text(int index) {
    ItemRect r = item_rect(index);
    js::Val self = js::make_object();
    js::vm().put(self, js::atom("textPadding"), js_const_fn(text_padding()));
    r.x += lang_num(blue("Window_OmoMenuItemTrashPromptWindow")["itemRectForText_rectx"], {}, self);
    r.y -= 10;
    r.width -= text_padding() * 2;
    return r;
}

void WindowOmoMenuItemCategory::update_category() {
    if (active && item_window) item_window->set_category(current_symbol());
}

static int item_command_text_index(const rpg::ItemRef& item) {
    if (!item.valid()) return 0;
    if (!item_meta_has(item, "ItemCommandTextIndex")) return 0;
    double n = js::string_to_number(item_meta_str(item, "ItemCommandTextIndex"));
    return std::isnan(n) ? 0 : (int)n;
}

void WindowOmoMenuItemConfirmation::make_command_list() {
    bool enable_use = false;
    if (item_.valid() && scene) {
        std::vector<rpg::GameBattler*> members;
        for (rpg::GameActor* a : rpg::game_party().members()) members.push_back(a);
        enable_use = scene->can_use(members);
    }
    bool enable_trash = item_.valid() ? !rpg::is_key_item(item_.data()) : false;
    const Value& text = plugin_data("itemMenu", "itemUse")["commands"];
    int text_index = item_command_text_index(item_);
    add_command(data::js_string(text[0][text_index]), "use", enable_use);
    add_command(data::js_string(text[1]), "trash", enable_trash);
}

void SceneOmoMenuItem::create() {
    SceneOmoMenuBase::create();
    create_item_confirmation_window();
    create_item_trash_prompt_window();
    create_help_window();
    create_status_windows();
    create_category_window();
    create_item_list_window();
    create_command_window();
}

bool SceneOmoMenuItem::is_busy() {
    if (move.is_moving()) return true;
    return SceneOmoMenuBase::is_busy();
}

void SceneOmoMenuItem::start() {
    clear_queue();
    clear_wait_mode();
    SceneOmoMenuBase::start();
    queue([this] { show_window(item_category_window.get(), 15); });
    queue_wait_mode("movement");
    queue([this] { item_category_window->activate(); });
}

void SceneOmoMenuItem::terminate() {
    SceneOmoMenuBase::terminate();
    if (command_window) command_window->remove_from_parent();
    if (status_window) status_window->remove_from_parent();
}

void SceneOmoMenuItem::create_status_windows() {
    SceneOmoMenuBase::create_status_windows();
    status_window->ok_sound_enabled = false;
    status_window->set_handler("ok", [this] { on_status_window_ok(); });
    status_window->set_handler("cancel", [this] { on_status_window_cancel(); });
    status_window->refresh();
}

void SceneOmoMenuItem::create_help_window() {
    SceneOmoMenuBase::create_help_window();
    help_window->set_width(384);
    help_window->set_height(90);
    help_window->x = 10;
    help_window->y = 64;
    help_window->hide_y = 64;
    help_window->hide_height = 0;
    help_window->show_y = 108;
    help_window->show_height = 90;
    layouts_[help_window.get()] = ShowHideLayout{64, 0, 108, 90};
    help_window->create_contents();
    help_window->set_height(0);
    help_window->icon_rate = 0.75;
}

void SceneOmoMenuItem::create_category_window() {
    item_category_window = WindowBase::make<WindowOmoMenuItemCategory>(0.0, 0.0);
    WindowOmoMenuItemCategory* w = item_category_window.get();
    w->x = 10;
    w->y = 20;
    layouts_[w] = ShowHideLayout{20, w->height(), 64, w->height()};
    w->update();
    w->deactivate();
    w->set_handler("ok", [this] { on_item_category_ok(); });
    w->set_handler("cancel", [this] { on_item_category_cancel(); });
    add_child(item_category_window);
}

void SceneOmoMenuItem::create_item_list_window() {
    item_list_window = WindowBase::make<WindowOmoMenuItemList>();
    WindowOmoMenuItemList* w = item_list_window.get();
    w->x = 394;
    w->y = 0;
    w->set_height(0);
    w->set_openness(255);
    layouts_[w] = ShowHideLayout{0, 0, 64, w->window_height()};
    w->set_handler("ok", [this] { on_item_list_ok(); });
    w->set_handler("cancel", [this] { on_item_list_cancel(); });
    w->set_help_window(help_window.get());
    item_category_window->item_window = w;
    item_category_window->call_update_help();
    item_category_window->update_category();
    add_child(item_list_window);
}

void SceneOmoMenuItem::create_item_confirmation_window() {
    item_confirmation_window = WindowBase::make<WindowOmoMenuItemConfirmation>(0.0, 0.0);
    WindowOmoMenuItemConfirmation* w = item_confirmation_window.get();
    w->scene = this;
    w->x = 10;
    w->y = 198 - w->height();
    layouts_[w] = ShowHideLayout{198 - w->height(), w->height(), 198, w->height()};
    w->set_openness(255);
    w->visible = false;
    w->set_handler("use", [this] { on_item_confirmation_use(); });
    w->set_handler("trash", [this] { on_item_confirmation_trash(); });
    w->set_handler("cancel", [this] { on_item_confirmation_cancel(); });
    add_child(item_confirmation_window);
}

void SceneOmoMenuItem::create_item_trash_prompt_window() {
    item_trash_prompt_window = WindowBase::make<WindowOmoMenuItemTrashPromptWindow>(0.0, 0.0);
    WindowOmoMenuItemTrashPromptWindow* w = item_trash_prompt_window.get();
    w->x = item_confirmation_window->x + item_confirmation_window->width();
    w->y = 198 - w->height();
    layouts_[w] = ShowHideLayout{198 - w->height(), w->height(), 198, w->height()};
    w->set_openness(255);
    w->visible = false;
    w->set_handler("ok", [this] { on_item_trash_prompt_ok(); });
    w->set_handler("cancel", [this] { on_item_trash_prompt_cancel(); });
    add_child(item_trash_prompt_window);
}

rpg::ItemRef SceneOmoMenuItem::item() { return item_list_window->item(); }

rpg::GameBattler* SceneOmoMenuItem::user() { return pocket_item_user(); }

rpg::GameBattler* pocket_item_user() {
    std::vector<rpg::GameBattler*> members = rpg::game_party().movable_members();
    if (members.empty()) return nullptr;
    rpg::GameBattler* best_actor = members[0];
    double best_pha = 0;
    for (rpg::GameBattler* m : members) {
        double pha = m->pha();
        if (pha > best_pha) {
            best_pha = pha;
            best_actor = m;
        }
    }
    return best_actor;
}

void SceneOmoMenuItem::play_se_for_item() { rpg::audio_manager::play_system_sound(22); }

void play_item_heal_se(const rpg::ItemRef& it) {
    int animation_id = it.valid() ? it.data()["animationId"].as_int() : 0;
    if (animation_id == 212) play_se("BA_Heart_Heal", 90, 100, 0);
    else if (animation_id == 213) play_se("BA_heal_juice", 90, 100, 0);
    else if (animation_id == 114) play_se("BA_Heart_Heal", 90, 100, 0);
}

void SceneOmoMenuItem::on_item_category_ok() {
    clear_queue();
    clear_wait_mode();
    queue([this] {
        WindowOmoMenuItemList* w = item_list_window.get();
        w->select(0);
        w->set_height(w->window_height());
        w->reset_scroll();
        w->refresh();
        w->set_height(0);
        w->update_help();
        show_window(help_window.get(), 15);
        show_window(w, 15);
    });
    queue_wait_mode("movement");
    queue([this] { item_list_window->activate(); });
}

void SceneOmoMenuItem::on_item_category_cancel() {
    clear_queue();
    clear_wait_mode();
    status_window->deselect();
    status_window->deactivate();
    queue([this] { hide_window(item_category_window.get(), 15); });
    queue_wait_mode("movement");
    queue([this] { pop_to_menu(); });
}

void SceneOmoMenuItem::on_item_list_ok() {
    clear_queue();
    clear_wait_mode();
    item_confirmation_window->select(0);
    item_confirmation_window->set_item(item_list_window->item());
    item_confirmation_window->visible = true;
    queue([this] { show_window(item_confirmation_window.get(), 15); });
    queue_wait_mode("movement");
    queue([this] { item_confirmation_window->activate(); });
}

void SceneOmoMenuItem::on_item_list_cancel() {
    clear_queue();
    clear_wait_mode();
    queue_wait_mode("movement");
    queue([this] {
        item_trash_prompt_window->visible = false;
        item_confirmation_window->visible = false;
        hide_window(help_window.get(), 15);
        hide_window(item_list_window.get(), 15);
    });
    queue_wait_mode("movement");
    queue([this] { item_category_window->activate(); });
}

void SceneOmoMenuItem::run_before_apply_eval(const std::string& code, const rpg::ItemRef& it, const Value& text, int text_index) {
    auto prog = js::compile_cached(rpg::trim(code));
    if (!prog) return;
    rpg::JsEval e(to_js());
    e.set("item", rpg::bind::item_js(it));
    e.set("text", rpg::bind::data_js(text));
    e.num("textIndex", text_index);
    e.run(prog.get(), "BeforeApplyEval");
}

void SceneOmoMenuItem::on_item_confirmation_use() {
    clear_queue();
    clear_wait_mode();
    rpg::ItemRef it = item();
    bool no_target = item_meta_has(it, "NoTarget");
    std::string before_apply_eval = item_meta_has(it, "BeforeApplyEval") ? item_meta_str(it, "BeforeApplyEval") : std::string();
    if (item_meta_has(it, "Bestiary")) {
        rpg::scene_manager::push(bestiary_scene());
        return;
    }
    if (item_meta_has(it, "AlbumGroup")) {
        rpg::scene_manager::push(photo_album_scene(it.id, 1));
        return;
    }
    queue([this] { hide_window(item_confirmation_window.get(), 15); });
    queue_wait_mode("movement");
    queue([this, it, no_target, before_apply_eval] {
        item_confirmation_window->visible = false;
        const Value& text = plugin_data("itemMenu", "itemUse")["confirm"];
        int text_index = item_command_text_index(it);
        if (!before_apply_eval.empty()) run_before_apply_eval(before_apply_eval, it, text, text_index);
        if (no_target) {
            on_status_window_ok();
            return;
        }
        if (is_item_for_all()) {
            status_window->set_cursor_text(data::js_string(text["all"][text_index]));
            status_window->set_cursor_all(true);
        } else {
            status_window->set_cursor_text(data::js_string(text["single"][text_index]));
            status_window->set_cursor_all(false);
        }
        status_window->select(0);
        status_window->activate();
    });
}

void SceneOmoMenuItem::on_item_confirmation_trash() {
    clear_queue();
    clear_wait_mode();
    item_trash_prompt_window->select(1);
    item_trash_prompt_window->visible = true;
    queue([this] { show_window(item_trash_prompt_window.get(), 15); });
    queue_wait_mode("movement");
    queue([this] { item_trash_prompt_window->activate(); });
}

void SceneOmoMenuItem::on_item_confirmation_cancel() {
    clear_queue();
    clear_wait_mode();
    item_list_window->refresh();
    int item_count = item_list_window->max_items();
    if (item_count > 0) {
        queue([this] { hide_window(item_confirmation_window.get(), 15); });
        queue_wait_mode("movement");
        queue([this] {
            item_confirmation_window->visible = false;
            item_list_window->activate();
            item_list_window->select_available();
        });
    } else {
        item_trash_prompt_window->close();
        item_confirmation_window->close();
        item_list_window->close();
        item_category_window->refresh();
        item_category_window->activate();
        help_window->close();
    }
}

void SceneOmoMenuItem::on_item_trash_prompt_ok() {
    rpg::game_system().unlock_achievement("LITTERING_IS_BAD_RECYCLING_IS_BETTER");
    clear_queue();
    clear_wait_mode();
    base_on_item_trash_prompt_ok();
}

void SceneOmoMenuItem::base_on_item_trash_prompt_ok() {
    rpg::ItemRef it = item();
    queue_wait(10);
    queue([] { play_se("GEN_multi1", 90, 100, 0); });
    queue_wait(20);
    queue([this, it] {
        rpg::game_party().lose_item(it, 1, false);
        if (rpg::game_party().num_items(it) <= 0) {
            item_list_window->refresh();
            int item_count = item_list_window->max_items();
            if (item_count > 0) {
                hide_window(item_trash_prompt_window.get(), 15);
                hide_window(item_confirmation_window.get(), 15);
                item_list_window->activate();
                item_list_window->select_available();
            } else {
                help_window->c().clear();
                item_category_window->refresh();
                queue_wait_mode("movement");
                queue([this] {
                    hide_window(item_trash_prompt_window.get(), 15);
                    hide_window(item_confirmation_window.get(), 15);
                });
                queue_wait_mode("movement");
                queue([this] {
                    item_trash_prompt_window->visible = false;
                    item_confirmation_window->visible = false;
                    hide_window(item_list_window.get(), 15);
                    hide_window(help_window.get(), 15);
                });
                queue([this] { item_category_window->activate(); });
            }
        } else {
            hide_window(item_trash_prompt_window.get(), 15);
            item_list_window->refresh();
            item_confirmation_window->activate();
        }
    });
}

void SceneOmoMenuItem::on_item_trash_prompt_cancel() {
    clear_queue();
    clear_wait_mode();
    queue([this] { hide_window(item_trash_prompt_window.get(), 15); });
    queue_wait_mode("movement");
    queue([this] {
        item_trash_prompt_window->visible = false;
        item_confirmation_window->activate();
    });
}

void SceneOmoMenuItem::on_status_window_ok() {
    clear_queue();
    clear_wait_mode();
    base_on_status_window_ok();
}

void SceneOmoMenuItem::base_on_status_window_ok() {
    rpg::ItemRef it = item();
    if (can_use()) {
        use_item();
        play_item_heal_se(it);
        item_list_window->refresh();
        double item_count = rpg::game_party().num_items(it);
        if (item_count <= 0) {
            item_list_window->refresh();
            int max_count = item_list_window->max_items();
            if (max_count > 0) {
                item_list_window->select_available();
                status_window->set_cursor_all(false);
                status_window->deselect();
                status_window->deactivate();
                item_list_window->refresh();
                item_list_window->activate();
            } else {
                status_window->set_cursor_all(false);
                status_window->deselect();
                status_window->deactivate();
                help_window->c().clear();
                item_category_window->refresh();
                queue_wait_mode("movement");
                queue([this] {
                    hide_window(item_list_window.get(), 15);
                    hide_window(help_window.get(), 15);
                });
                queue_wait_mode("movement");
                queue([this] { item_category_window->activate(); });
            }
            return;
        }
    } else {
        play_buzzer();
    }
    status_window->activate();
}

void SceneOmoMenuItem::on_status_window_cancel() {
    status_window->set_cursor_all(false);
    status_window->deselect();
    item_list_window->activate();
}

void SceneOmoMenuItem::show_window(g2d::Window* obj, int duration) {
    const ShowHideLayout& l = layouts_[obj];
    move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), l.show_y, duration}, {prop_height(obj), l.show_height, duration}}, ease_out_circ));
}

void SceneOmoMenuItem::hide_window(g2d::Window* obj, int duration) {
    const ShowHideLayout& l = layouts_[obj];
    move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), l.hide_y, duration}, {prop_height(obj), l.hide_height, duration}}, ease_in_circ));
}

rpg::SceneFactory omo_menu_item_scene() {
    return rpg::SceneFactory{"Scene_OmoMenuItem", [] { return std::unique_ptr<rpg::Scene>(new SceneOmoMenuItem()); }};
}

}

static SceneRegistrar r_scene_omo_menu_item("Scene_OmoMenuItem", menu::omo_menu_item_scene());

}
