#pragma once
#include <memory>
#include <string>
#include <vector>

#include "menu_common.h"

namespace ui {
namespace menu {

void restore_default_config();

class WindowOmoMenuOptionsHelp : public WindowHelp {
public:
    void initialize(int) override { initialize(); }
    void initialize() { WindowHelp::initialize(10, 0, window_width(), window_height()); }
    void initialize(double x, double y, double w, double h) override { WindowHelp::initialize(x, y, w, h); }
    int standard_padding() override { return 8; }
    virtual double window_height() { return 44; }
    virtual double window_width() { return 640 - 20; }
    double line_height() override { return 24; }
    void clear() override;
    void set_text(const std::string& text) override;
    void refresh() override {}
};

class WindowOmoMenuOptionsCategory : public WindowCommand {
public:
    void initialize(double x, double y) override { WindowCommand::initialize(x, y); }
    using WindowCommand::initialize;
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 10; }
    double window_width() override { return 640 - 20; }
    int max_cols() override { return 4; }
    double line_height() override { return 24; }
    int standard_font_size() override;
    double custom_cursor_rect_y_offset() override { return 4; }
    double custom_cursor_rect_text_x_offset() override { return 25; }
    void make_command_list() override;
    ItemRect item_rect(int index) override;
    void select(int index) override;
    void call_update_help_ex();

    std::vector<g2d::Window*> option_windows;

private:
    void update_option_windows_visibility();
};

class WindowOmoMenuOptionsGeneral : public WindowSelectable {
public:
    struct Option {
        std::string name;
        std::string header;
        std::vector<std::string> options;
        std::string help_text;
        double spacing = 0;
        int index = 0;
    };

    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 8; }
    virtual double window_width() { return 640 - 20; }
    virtual double window_height() { return 318; }
    int max_items() override { return (int)options_list.size(); }
    int max_cols() override { return 1; }
    double item_height() override { return 75; }
    int spacing() override { return 5; }
    double custom_cursor_rect_x_offset() override { return 15; }
    double custom_cursor_rect_y_offset() override { return -18; }
    void create_option_cursors();
    void make_options_list();
    void draw_item(int index) override;
    void draw_option_segment(const Option& data, const ItemRect& rect);
    void select(int index) override;
    void cursor_right(bool wrap) override;
    void cursor_left(bool wrap) override;
    void update_cursor() override;
    void process_option_command();

    std::vector<Option> options_list;
    std::vector<std::shared_ptr<SpriteWindowCustomCursor>> option_cursors;

protected:
    void update_transform_hook() override;
};

class WindowOmoMenuOptionsAudio : public WindowSelectable {
public:
    struct Option {
        std::string header;
        std::string config;
        int volume = 0;
        std::string help_text;
    };

    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 8; }
    virtual double window_width() { return 640 - 20; }
    virtual double window_height() { return 318; }
    int max_items() override { return (int)options_list.size(); }
    int max_cols() override { return 1; }
    double item_height() override { return 75; }
    int spacing() override { return 5; }
    double custom_cursor_rect_x_offset() override { return 15; }
    double custom_cursor_rect_y_offset() override { return -18; }
    void create_volume_bars();
    void make_options_list();
    void draw_item(int index) override;
    void select(int index) override;
    void cursor_right(bool wrap) override;
    void cursor_left(bool wrap) override;
    void update_volume_bar(int index, int volume);

    std::vector<Option> options_list;
    std::vector<g2d::SpritePtr> volume_sprites;

protected:
    void update_transform_hook() override;
};

class WindowOmoMenuOptionsSystem : public WindowCommand {
public:
    void initialize(double x, double y) override;
    using WindowCommand::initialize;
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    int standard_padding() override { return 10; }
    double window_width() override { return 640 - 20; }
    double window_height() override { return 318; }
    int max_cols() override { return 1; }
    int standard_font_size() override { return 24; }
    double custom_cursor_rect_y_offset() override { return 4; }
    double custom_cursor_rect_text_x_offset() override { return 25; }
    void make_command_list() override;
    std::string command_help_text(const std::string& symbol);
    void select(int index) override;
};

class WindowOmoMenuOptionsExitPromptWindow : public WindowCommand {
public:
    void initialize(double x, double y) override;
    using WindowCommand::initialize;
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    double window_height() override { return 84 - 16; }
    double window_width() override { return type_ == 0 ? 200 : 232; }
    int max_cols() override { return 2; }
    int spacing() override { return 0; }
    int standard_padding() override { return 0; }
    double item_height() override { return 20; }
    double item_width() override { return 75 + 10; }
    int standard_font_size() override { return 24; }
    double custom_cursor_rect_x_offset() override;
    void make_command_list() override;
    void refresh() override { refresh(0); }
    void refresh(int type);
    ItemRect item_rect(int index) override;
    ItemRect item_rect_for_text(int index) override;
    int type() const { return type_; }

private:
    int type_ = 0;
    std::string prompt_text_;
};

class SceneOmoMenuOptions : public SceneOmoMenuBase {
public:
    const char* name() const override { return "Scene_OmoMenuOptions"; }
    void create() override;
    void start() override;
    void create_help_window() override;
    void create_gold_window();
    void create_option_categories_window();
    void create_general_options_window();
    void create_audio_options_window();
    void create_system_options_window();
    void create_exit_prompt_window();
    std::vector<WindowSelectable*> option_windows();
    void on_category_ok();
    void on_category_cancel();
    void on_option_window_cancel();
    void go_to_load();
    void start_exit_prompt(int type);
    void on_exit_prompt_cancel();
    void on_exit_prompt_yes();
    void show_windows();
    void hide_windows();

    std::shared_ptr<WindowOmoMenuOptionsHelp> options_help_window;
    std::shared_ptr<WindowGold> gold_window;
    std::shared_ptr<WindowOmoMenuOptionsCategory> option_categories_window;
    std::shared_ptr<WindowOmoMenuOptionsGeneral> general_options_window;
    std::shared_ptr<WindowOmoMenuOptionsAudio> audio_options_window;
    std::shared_ptr<WindowOmoMenuOptionsSystem> system_options_window;
    std::shared_ptr<WindowOmoMenuOptionsExitPromptWindow> exit_prompt_window;
};

}
}
