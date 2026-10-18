#include "menu_options.h"

#include <algorithm>
#include <cmath>

#include "../rpg/config.h"
#include "../rpg/data.h"
#include "../rpg/input.h"
#include "scenes.h"
#include "ui_util.h"

namespace ui {
namespace menu {

namespace {

const char* const kGeneralOptions[] = {"textSkip", "battleLogSpeed", "alwaysDash"};
const int kCategoryTextIndices[] = {0, 1, 3};

bool general_option_supported(const std::string& name) {
    for (const char* n : kGeneralOptions) {
        if (name == n) return true;
    }
    return false;
}

int general_config_value(const std::string& name) {
    rpg::Config& cfg = rpg::config();
    if (name == "textSkip") return cfg.text_skip ? 1 : 0;
    if (name == "battleLogSpeed") return cfg.battle_log_speed;
    if (name == "alwaysDash") return cfg.always_dash ? 1 : 0;
    return 0;
}

int* volume_field(const std::string& name) {
    rpg::Config& cfg = rpg::config();
    if (name == "bgmVolume") return &cfg.bgm_volume;
    if (name == "bgsVolume") return &cfg.bgs_volume;
    if (name == "meVolume") return &cfg.me_volume;
    if (name == "seVolume") return &cfg.se_volume;
    return nullptr;
}

std::string current_scene_name() {
    rpg::Scene* s = rpg::scene_manager::scene();
    return s ? std::string(s->name()) : std::string();
}

void arrows_hook(g2d::Window* w, bool active) {
    double ww = w->width(), h = w->height();
    double p = 28, q = p / 2;
    w->down_arrow_sprite()->move(ww - q, h - q);
    w->up_arrow_sprite()->move(ww - q, q);
    w->down_arrow_sprite()->visible = w->down_arrow_sprite()->visible && active;
    w->up_arrow_sprite()->visible = w->up_arrow_sprite()->visible && active;
}

}

void restore_default_config() {
    rpg::Config& cfg = rpg::config();
    cfg.character_strafe = true;
    cfg.character_turning = true;
    cfg.battle_animations = true;
    cfg.battle_animation_speed = 0;
    cfg.battle_log_speed = 1;
    cfg.menu_animations = true;
    cfg.gamepad_tips = false;
    cfg.always_dash = false;
    cfg.text_skip = false;
    cfg.bgm_volume = 70;
    cfg.bgs_volume = 90;
    cfg.me_volume = 90;
    cfg.se_volume = 90;
    rpg::config_apply_volumes();
    rpg::config_save();
}

void WindowOmoMenuOptionsHelp::clear() { c().clear(); }

void WindowOmoMenuOptionsHelp::set_text(const std::string& text) {
    text_ = text;
    c().clear();
    c().draw_text(text, 5, 0, c().width(), c().height() - 8, "left");
}

int WindowOmoMenuOptionsCategory::standard_font_size() { return blue("Window_OmoMenuOptionsCategory")["standardFontSize"].as_int(); }

void WindowOmoMenuOptionsCategory::make_command_list() {
    const Value& localization = blue("Omori_Mainmenu_Sceneoptions")["commands"];
    for (int i : kCategoryTextIndices) add_command(data::js_string(localization[i]), "ok");
}

ItemRect WindowOmoMenuOptionsCategory::item_rect(int index) {
    ItemRect r = WindowCommand::item_rect(index);
    r.y -= 3;
    return r;
}

void WindowOmoMenuOptionsCategory::select(int index) {
    WindowCommand::select(index);
    update_option_windows_visibility();
}

void WindowOmoMenuOptionsCategory::call_update_help_ex() {
    call_update_help();
    update_option_windows_visibility();
}

void WindowOmoMenuOptionsCategory::update_option_windows_visibility() {
    if (option_windows.empty()) return;
    int idx = index();
    for (size_t i = 0; i < option_windows.size(); i++) {
        if (option_windows[i]) option_windows[i]->visible = (int)i == idx;
    }
}

void WindowOmoMenuOptionsGeneral::initialize() {
    make_options_list();
    WindowSelectable::initialize(0, 0, window_width(), window_height());
    create_option_cursors();
    select(0);
    refresh();
}

void WindowOmoMenuOptionsGeneral::create_option_cursors() {
    option_cursors.clear();
    static const double off[4] = {-80, -80, -80, 255};
    for (int i = 0; i < 4; i++) {
        auto sprite = std::make_shared<SpriteWindowCustomCursor>(-1, custom_cursor_rect_bitmap_name());
        sprite->active_ = false;
        sprite->visible = false;
        sprite->set_color_tone(off);
        custom_cursor_container_->add_child(sprite);
        option_cursors.push_back(sprite);
    }
}

void WindowOmoMenuOptionsGeneral::make_options_list() {
    const Value& text = plugin_data("optionsMenu", "general");
    options_list.clear();
    for (const Member* m = text.begin_members(); m != text.end_members(); ++m) {
        std::string name = m->key;
        if (!general_option_supported(name)) continue;
        const Value& d = m->v;
        Option option;
        option.name = name;
        option.header = data::js_string(d["text"]) + ":";
        const Value& opts = d["options"];
        for (size_t i = 0; i < opts.size(); i++) option.options.push_back(data::js_string(opts[i]));
        option.help_text = data::js_string(d["help"]);
        option.spacing = d["spacing"].num();
        if (d["boolIndex"].truthy()) option.index = general_config_value(name) ? 0 : 1;
        else option.index = general_config_value(name);
        options_list.push_back(std::move(option));
    }
}

void WindowOmoMenuOptionsGeneral::draw_item(int index) {
    ItemRect r = item_rect(index);
    if (index < 0 || index >= (int)options_list.size()) return;
    draw_option_segment(options_list[(size_t)index], r);
}

void WindowOmoMenuOptionsGeneral::draw_option_segment(const Option& data, const ItemRect& rect) {
    c().draw_text(data.header, rect.x + 50, rect.y, rect.width, 24, "left");
    for (size_t i = 0; i < data.options.size(); i++) {
        c().draw_text(data.options[i], rect.x + (100 + (i * data.spacing)), rect.y + 35, rect.width, 24, "left");
    }
}

void WindowOmoMenuOptionsGeneral::select(int index) {
    WindowSelectable::select(index);
    int idx = this->index();
    if (help_window && idx >= 0 && idx < (int)options_list.size()) help_window->set_text(options_list[(size_t)idx].help_text);
}

void WindowOmoMenuOptionsGeneral::cursor_right(bool wrap) {
    WindowSelectable::cursor_right(wrap);
    int idx = index();
    if (idx < 0 || idx >= (int)options_list.size()) return;
    Option& data = options_list[(size_t)idx];
    if (data.options.empty()) return;
    data.index = (data.index + 1) % (int)data.options.size();
    process_option_command();
    update_cursor();
}

void WindowOmoMenuOptionsGeneral::cursor_left(bool wrap) {
    WindowSelectable::cursor_left(wrap);
    int idx = index();
    if (idx < 0 || idx >= (int)options_list.size()) return;
    Option& data = options_list[(size_t)idx];
    if (data.options.empty()) return;
    int max_items = (int)data.options.size();
    data.index = (data.index - 1 + max_items) % max_items;
    process_option_command();
    update_cursor();
}

void WindowOmoMenuOptionsGeneral::update_cursor() {
    WindowSelectable::update_cursor();
    if (option_cursors.empty()) return;
    int tr = top_row();
    int idx = index();
    static const double on[4] = {0, 0, 0, 0};
    static const double off[4] = {-80, -80, -80, 255};
    for (size_t i = 0; i < option_cursors.size(); i++) {
        auto& sprite = option_cursors[i];
        int real_index = tr + (int)i;
        bool selected = active ? real_index == idx : false;
        if (real_index >= 0 && real_index < (int)options_list.size()) {
            const Option& data = options_list[(size_t)real_index];
            ItemRect r = item_rect(real_index);
            sprite->set_color_tone(selected ? on : off);
            sprite->active_ = selected;
            sprite->x = (r.x + 65) + (data.index * data.spacing);
            sprite->y = r.y + 60;
            sprite->visible = height() >= sprite->y + sprite->height();
        } else {
            sprite->active_ = false;
            sprite->visible = false;
        }
    }
}

void WindowOmoMenuOptionsGeneral::process_option_command() {
    int idx = index();
    if (idx < 0 || idx >= (int)options_list.size()) return;
    const Option& data = options_list[(size_t)idx];
    rpg::Config& cfg = rpg::config();
    if (data.name == "textSkip") cfg.text_skip = data.index == 0;
    else if (data.name == "battleLogSpeed") cfg.battle_log_speed = data.index;
    else if (data.name == "alwaysDash") cfg.always_dash = data.index == 0;
}

void WindowOmoMenuOptionsGeneral::update_transform_hook() {
    arrows_hook(this, active);
    int tr = top_row();
    for (size_t i = 0; i < option_cursors.size(); i++) {
        int real_index = tr + (int)i;
        if (real_index < 0 || real_index >= (int)options_list.size()) continue;
        auto& sprite = option_cursors[i];
        sprite->visible = height() >= (sprite->y + sprite->height());
    }
}

void WindowOmoMenuOptionsAudio::initialize() {
    make_options_list();
    WindowSelectable::initialize(0, 0, window_width(), window_height());
    create_volume_bars();
    select(0);
    refresh();
}

void WindowOmoMenuOptionsAudio::create_volume_bars() {
    volume_sprites.clear();
    g2d::BitmapPtr bitmap = g2d::Bitmap::create(400, 40);
    for (int i = 0; i < 100; i++) {
        double x = i * 4;
        bitmap->fill_rect(x, 0, 2, 20, "rgba(100, 100, 100, 1)");
        bitmap->fill_rect(x, 20, 2, 20, "rgba(255, 255, 255, 1)");
    }
    for (int i = 0; i < 8; i++) {
        auto sprite = std::make_shared<g2d::Sprite>(bitmap);
        int index = i / 2;
        ItemRect r = item_rect(index);
        sprite->x = r.x + 60;
        sprite->y = r.y + 50;
        sprite->set_frame(0, (i % 2) * 20, bitmap->width(), 20);
        volume_sprites.push_back(sprite);
        add_child(sprite);
    }
}

void WindowOmoMenuOptionsAudio::make_options_list() {
    const Value& text = plugin_data("optionsMenu", "audio");
    options_list.clear();
    for (const Member* m = text.begin_members(); m != text.end_members(); ++m) {
        std::string name = m->key;
        const Value& d = m->v;
        Option option;
        option.header = data::js_string(d["text"]) + ":";
        option.config = name;
        int* field = volume_field(name);
        option.volume = field ? *field : 0;
        option.help_text = data::js_string(d["help"]);
        options_list.push_back(std::move(option));
    }
}

void WindowOmoMenuOptionsAudio::draw_item(int index) {
    ItemRect r = item_rect(index);
    if (index < 0 || index >= (int)options_list.size()) return;
    c().draw_text(options_list[(size_t)index].header, r.x + 50, r.y, r.width, 24, "left");
    update_volume_bar(index, options_list[(size_t)index].volume);
}

void WindowOmoMenuOptionsAudio::select(int index) {
    WindowSelectable::select(index);
    int idx = this->index();
    if (help_window && idx >= 0 && idx < (int)options_list.size()) help_window->set_text(options_list[(size_t)idx].help_text);
}

void WindowOmoMenuOptionsAudio::cursor_right(bool wrap) {
    WindowSelectable::cursor_right(wrap);
    int idx = index();
    if (idx < 0 || idx >= (int)options_list.size()) return;
    Option& data = options_list[(size_t)idx];
    int rate = input::is_long_pressed(input::RIGHT) ? 5 : 1;
    data.volume = std::min(data.volume + rate, 100);
    update_volume_bar(idx, data.volume);
}

void WindowOmoMenuOptionsAudio::cursor_left(bool wrap) {
    WindowSelectable::cursor_left(wrap);
    int idx = index();
    if (idx < 0 || idx >= (int)options_list.size()) return;
    Option& data = options_list[(size_t)idx];
    int rate = input::is_long_pressed(input::LEFT) ? 5 : 1;
    data.volume = std::max(data.volume - rate, 0);
    update_volume_bar(idx, data.volume);
}

void WindowOmoMenuOptionsAudio::update_volume_bar(int index, int volume) {
    if (index < 0 || index >= (int)options_list.size()) return;
    const Option& data = options_list[(size_t)index];
    size_t front_index = (size_t)(index * 2) + 1;
    if (front_index < volume_sprites.size()) {
        auto& front = volume_sprites[front_index];
        const g2d::Rect& f = front->frame();
        front->set_frame(f.x, f.y, volume * 4, f.height);
    }
    ItemRect r = item_rect(index);
    r.x += 415;
    r.y += 27;
    r.width = 100;
    r.height = 40;
    c().clear_rect(r.x, r.y, r.width, r.height);
    c().draw_text(num_str(volume) + "%", r.x, r.y, r.width, r.height, "right");
    if (int* field = volume_field(data.config)) {
        *field = volume;
        rpg::config_apply_volumes();
    }
}

void WindowOmoMenuOptionsAudio::update_transform_hook() {
    for (auto& sprite : volume_sprites) sprite->visible = height() >= (sprite->y + sprite->height());
}

void WindowOmoMenuOptionsSystem::initialize(double x, double y) {
    WindowCommand::initialize(x, y);
    deactivate();
}

void WindowOmoMenuOptionsSystem::make_command_list() {
    std::string scene = current_scene_name();
    bool is_options_scene = scene == "Scene_OmoMenuOptions";
    bool is_scene_title = scene == "Scene_OmoriTitleScreen";
    const Value& sys = plugin_data("optionsMenu", "system");
    add_command(data::js_string(sys["restoreConfig"]["text"]), "restoreConfig", is_scene_title);
    add_command(data::js_string(sys["load"]["text"]), "load", is_options_scene);
    add_command(data::js_string(sys["toTitleScreen"]["text"]), "toTitleScreen", is_options_scene);
    add_command(data::js_string(sys["exit"]["text"]), "exit");
}

std::string WindowOmoMenuOptionsSystem::command_help_text(const std::string& symbol) {
    const Value& sys = plugin_data("optionsMenu", "system");
    if (symbol == "restoreConfig") return data::js_string(sys["restoreConfig"]["help"]);
    if (symbol == "load") return data::js_string(sys["load"]["help"]);
    if (symbol == "toTitleScreen") return data::js_string(sys["toTitleScreen"]["help"]);
    if (symbol == "exit") return data::js_string(sys["exit"]["help"]);
    return "* ERROR!!! *";
}

void WindowOmoMenuOptionsSystem::select(int index) {
    WindowCommand::select(index);
    if (help_window) help_window->set_text(command_help_text(current_symbol()));
}

void WindowOmoMenuOptionsExitPromptWindow::initialize(double x, double y) {
    type_ = 0;
    WindowCommand::initialize(x, y);
    prompt_text_ = data::js_string(plugin_data("optionsMenu", "system")["exitPrompt"]["text"]);
    refresh();
    set_openness(0);
    deactivate();
}

double WindowOmoMenuOptionsExitPromptWindow::custom_cursor_rect_x_offset() {
    double base = WindowCommand::custom_cursor_rect_x_offset();
    return type_ == 0 ? base : base + 16;
}

void WindowOmoMenuOptionsExitPromptWindow::make_command_list() {
    const Value& sys = plugin_data("optionsMenu", "system");
    const Value& text = type_ == 0 ? sys["exitPrompt"]["commands"] : sys["toTitleScreenPrompt"]["commands"];
    add_command(data::js_string(text[0]), "yes");
    add_command(data::js_string(text[1]), "cancel");
}

void WindowOmoMenuOptionsExitPromptWindow::refresh(int type) {
    type_ = type;
    set_width(window_width());
    create_contents();
    WindowCommand::refresh();
    const Value& sys = plugin_data("optionsMenu", "system");
    prompt_text_ = data::js_string(type_ == 0 ? sys["exitPrompt"]["text"] : sys["toTitleScreenPrompt"]["text"]);
    draw_text(prompt_text_, 0, 0, c().width(), "center");
}

ItemRect WindowOmoMenuOptionsExitPromptWindow::item_rect(int index) {
    ItemRect r = WindowCommand::item_rect(index);
    r.y += line_height() - 5;
    return r;
}

ItemRect WindowOmoMenuOptionsExitPromptWindow::item_rect_for_text(int index) {
    ItemRect r = item_rect(index);
    r.x += text_padding() + 35;
    if (type_ != 0) r.x += 16;
    r.y -= 10;
    r.width -= text_padding() * 2;
    return r;
}

void SceneOmoMenuOptions::create() {
    SceneOmoMenuBase::create();
    create_help_window();
    create_status_windows();
    create_gold_window();
    create_general_options_window();
    create_audio_options_window();
    create_system_options_window();
    create_option_categories_window();
    create_command_window();
    create_exit_prompt_window();
}

void SceneOmoMenuOptions::start() {
    SceneOmoMenuBase::start();
    show_windows();
}

void SceneOmoMenuOptions::create_help_window() {
    options_help_window = WindowBase::make<WindowOmoMenuOptionsHelp>();
    options_help_window->x = 10;
    options_help_window->y = 10;
    add_child(options_help_window);
}

void SceneOmoMenuOptions::create_gold_window() {
    if (!command_window) command_window = WindowBase::make<WindowMenuCommand>(10.0, 10.0);
    gold_window = std::shared_ptr<WindowGold>(new WindowGold());
    gold_window->initialize(0.0, 0.0);
    gold_window->x = (640 - gold_window->width()) - 10;
    gold_window->y = (command_window->y + command_window->height()) + 8;
    gold_window->set_openness(255);
    gold_window->close();
    add_child(gold_window);
}

void SceneOmoMenuOptions::create_option_categories_window() {
    option_categories_window = WindowBase::make<WindowOmoMenuOptionsCategory>(0.0, 0.0);
    option_categories_window->x = 10;
    option_categories_window->y = 10;
    option_categories_window->deactivate();
    option_categories_window->set_handler("ok", [this] { on_category_ok(); });
    option_categories_window->set_handler("cancel", [this] { on_category_cancel(); });
    add_child(option_categories_window);
    std::vector<WindowSelectable*> windows = option_windows();
    option_categories_window->option_windows.assign(windows.begin(), windows.end());
    for (WindowSelectable* w : windows) w->help_window = options_help_window.get();
    option_categories_window->call_update_help_ex();
}

void SceneOmoMenuOptions::create_general_options_window() {
    general_options_window = WindowBase::make<WindowOmoMenuOptionsGeneral>();
    general_options_window->x = 10;
    general_options_window->y = 10;
    general_options_window->set_handler("cancel", [this] { on_option_window_cancel(); });
    general_options_window->set_height(0);
    general_options_window->visible = false;
    add_child(general_options_window);
}

void SceneOmoMenuOptions::create_audio_options_window() {
    audio_options_window = WindowBase::make<WindowOmoMenuOptionsAudio>();
    audio_options_window->x = 10;
    audio_options_window->y = 10;
    audio_options_window->set_handler("cancel", [this] { on_option_window_cancel(); });
    audio_options_window->set_height(0);
    audio_options_window->visible = false;
    add_child(audio_options_window);
}

void SceneOmoMenuOptions::create_system_options_window() {
    system_options_window = WindowBase::make<WindowOmoMenuOptionsSystem>(0.0, 0.0);
    system_options_window->x = 10;
    system_options_window->y = 10;
    system_options_window->set_handler("cancel", [this] { on_option_window_cancel(); });
    system_options_window->set_handler("restoreConfig", [this] {
        restore_default_config();
        general_options_window->make_options_list();
        audio_options_window->make_options_list();
        general_options_window->refresh();
        general_options_window->select(0);
        audio_options_window->refresh();
        system_options_window->refresh();
        system_options_window->activate();
        input::clear();
    });
    system_options_window->set_handler("load", [this] { go_to_load(); });
    system_options_window->set_handler("toTitleScreen", [this] { start_exit_prompt(1); });
    system_options_window->set_handler("exit", [this] { start_exit_prompt(0); });
    add_child(system_options_window);
}

void SceneOmoMenuOptions::create_exit_prompt_window() {
    exit_prompt_window = WindowBase::make<WindowOmoMenuOptionsExitPromptWindow>(0.0, 0.0);
    exit_prompt_window->x = (640 - exit_prompt_window->width()) / 2;
    exit_prompt_window->y = (480 - exit_prompt_window->height()) / 2;
    exit_prompt_window->set_handler("yes", [this] { on_exit_prompt_yes(); });
    exit_prompt_window->set_handler("cancel", [this] { on_exit_prompt_cancel(); });
    add_child(exit_prompt_window);
}

std::vector<WindowSelectable*> SceneOmoMenuOptions::option_windows() {
    return {general_options_window.get(), audio_options_window.get(), system_options_window.get()};
}

void SceneOmoMenuOptions::on_category_ok() {
    std::vector<WindowSelectable*> windows = option_windows();
    int idx = option_categories_window->index();
    WindowSelectable* category_window = idx >= 0 && idx < (int)windows.size() ? windows[(size_t)idx] : nullptr;
    if (category_window) {
        category_window->activate();
        category_window->select(0);
    } else {
        option_categories_window->activate();
    }
}

void SceneOmoMenuOptions::on_category_cancel() {
    rpg::config_save();
    hide_windows();
}

void SceneOmoMenuOptions::on_option_window_cancel() {
    std::vector<WindowSelectable*> windows = option_windows();
    int idx = option_categories_window->index();
    WindowSelectable* category_window = idx >= 0 && idx < (int)windows.size() ? windows[(size_t)idx] : nullptr;
    if (category_window) {
        category_window->select(0);
        category_window->refresh();
    }
    options_help_window->clear();
    option_categories_window->activate();
}

void SceneOmoMenuOptions::go_to_load() {
    rpg::config_save();
    rpg::scene_manager::goto_scene(omori_file_scene(false, true));
}

void SceneOmoMenuOptions::start_exit_prompt(int type) {
    exit_prompt_window->refresh(type);
    exit_prompt_window->select(1);
    exit_prompt_window->open();
    exit_prompt_window->activate();
}

void SceneOmoMenuOptions::on_exit_prompt_cancel() {
    exit_prompt_window->deactivate();
    exit_prompt_window->close();
    system_options_window->activate();
}

void SceneOmoMenuOptions::on_exit_prompt_yes() {
    fade_out_all();
    int type = exit_prompt_window->type();
    if (!type) rpg::scene_manager::exit_game();
    else rpg::scene_manager::goto_scene(title_scene());
}

void SceneOmoMenuOptions::show_windows() {
    clear_queue();
    clear_wait_mode();
    queue([this] {
        int duration = 15;
        for (auto& sw : status_window->status_windows) {
            WindowOmoMainMenuActorStatus* obj = sw.get();
            Prop co{[obj] { return obj->contents_opacity(); }, [obj](double v) { obj->set_contents_opacity_all(v); }};
            move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), 110, duration}, {co, 0, duration}}, ease_in_circ));
        }
        WindowOmoMenuOptionsCategory* obj = option_categories_window.get();
        move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), 64, 10}}, ease_out_circ));
    });
    queue_wait_mode("movement");
    queue([this] {
        int duration = 15;
        for (WindowSelectable* obj : option_windows()) {
            obj->y = option_categories_window->y;
            move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), 108, duration}, {prop_height(obj), 318, duration}}, ease_out_circ));
        }
    });
    queue_wait_mode("movement");
    queue([this] {
        WindowOmoMenuOptionsHelp* obj = options_help_window.get();
        obj->y = 480 - ((obj->height() * 2) + 10);
        move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), obj->y + obj->height(), 10}}, ease_out_circ));
    });
    queue_wait_mode("movement");
    queue([this] { option_categories_window->activate(); });
    queue_wait_mode("movement");
}

void SceneOmoMenuOptions::hide_windows() {
    clear_queue();
    clear_wait_mode();
    queue([this] {
        WindowOmoMenuOptionsHelp* obj = options_help_window.get();
        move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), obj->y - obj->height(), 10}}, ease_out_circ));
    });
    queue_wait_mode("movement");
    queue([this] {
        options_help_window->y = 10;
        int duration = 15;
        for (WindowSelectable* obj : option_windows()) {
            obj->help_window = nullptr;
            obj->deselect();
            move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), option_categories_window->y, duration}, {prop_height(obj), 0, duration}},
                                                  ease_out_circ));
        }
        for (auto& sw : status_window->status_windows) {
            WindowOmoMainMenuActorStatus* obj = sw.get();
            Prop co{[obj] { return obj->contents_opacity(); }, [obj](double v) { obj->set_contents_opacity_all(v); }};
            move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), -110, duration}, {co, 255, duration}}, ease_out_circ));
        }
    });
    queue_wait_mode("movement");
    queue([this] {
        WindowOmoMenuOptionsCategory* obj = option_categories_window.get();
        move.start_move(ObjectMovement::tween(obj, {{prop_y(obj), 10, 10}}, ease_out_circ));
    });
    queue_wait_mode("movement");
    queue([this] { pop_to_menu(); });
}

rpg::SceneFactory omo_menu_options_scene() {
    return rpg::SceneFactory{"Scene_OmoMenuOptions", [] { return std::unique_ptr<rpg::Scene>(new SceneOmoMenuOptions()); }};
}

static SceneRegistrar r_scene_omo_menu_options("Scene_OmoMenuOptions", omo_menu_options_scene());

}
}
