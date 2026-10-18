#pragma once
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "window_base.h"

namespace ui {

class SpriteWindowCustomCursor : public g2d::Sprite {
public:
    SpriteWindowCustomCursor(int index, const std::string& name);
    bool has_index = false;
    int index = 0;
    bool active_ = true;
    double angle_ = 0;
    void init_cursor_animation(double area = 0.1, double speed = 0.5, int duration = 20);
    void setup_bitmap(const std::string& name);
    void update() override;
    void update_cursor_animation();

private:
    double sine_index_ = 0;
    double speed_ = 0.5;
    std::vector<double> sine_list_;
};

struct ItemRect {
    double x = 0, y = 0, width = 0, height = 0;
};

class WindowHelp;

class WindowSelectable : public WindowBase {
public:
    void initialize(double x, double y, double w, double h) override;
    int index() const { return index_; }
    bool cursor_fixed() const { return cursor_fixed_; }
    void set_cursor_fixed(bool v) { cursor_fixed_ = v; }
    bool cursor_all() const { return cursor_all_; }
    void set_cursor_all(bool v) { cursor_all_ = v; }
    virtual int max_cols() { return 1; }
    virtual int max_items() { return 0; }
    virtual int spacing() { return 12; }
    virtual double item_width();
    virtual double item_height() { return line_height(); }
    int max_rows();
    void activate() override;
    void deactivate() override;
    virtual void select(int index);
    void deselect() { select(-1); }
    void reselect() { select(index_); }
    int row();
    virtual int top_row();
    int max_top_row();
    virtual void set_top_row(int row);
    void reset_scroll() { set_top_row(0); }
    virtual int max_page_rows();
    int max_page_items();
    bool is_horizontal() { return max_page_rows() == 1; }
    int bottom_row();
    void set_bottom_row(int row) { set_top_row(row - (max_page_rows() - 1)); }
    int top_index() { return top_row() * max_cols(); }
    virtual ItemRect item_rect(int index);
    virtual ItemRect item_rect_for_text(int index);
    void set_help_window(WindowHelp* w);
    void show_help_window();
    void hide_help_window();
    void set_handler(const std::string& symbol, std::function<void()> fn) { handlers_[symbol] = std::move(fn); }
    bool is_handled(const std::string& symbol) const;
    void call_handler(const std::string& symbol);
    bool is_open_and_active() { return is_open() && active; }
    virtual bool is_cursor_movable();
    virtual void cursor_down(bool wrap);
    virtual void cursor_up(bool wrap);
    virtual void cursor_right(bool wrap);
    virtual void cursor_left(bool wrap);
    virtual void cursor_pagedown();
    virtual void cursor_pageup();
    void scroll_down();
    void scroll_up();
    void update() override;
    void update_arrows();
    virtual void process_cursor_move();
    virtual void process_handling();
    virtual bool is_ok_enabled() { return is_handled("ok"); }
    virtual bool is_cancel_enabled() { return is_handled("cancel"); }
    virtual bool is_ok_triggered();
    virtual bool is_cancel_triggered();
    virtual void process_ok();
    virtual void play_ok_sound();
    virtual void play_buzzer_sound();
    virtual void call_ok_handler() { call_handler("ok"); }
    virtual void process_cancel();
    virtual void call_cancel_handler() { call_handler("cancel"); }
    virtual void process_pageup();
    virtual void process_pagedown();
    void update_input_data();
    virtual void update_cursor();
    bool is_cursor_visible();
    virtual void ensure_cursor_visible();
    void call_update_help();
    virtual void update_help();
    void set_help_window_item(const rpg::ItemRef& item);
    virtual bool is_current_item_enabled() { return true; }
    virtual void draw_all_items();
    virtual void draw_item(int index) { (void)index; }
    virtual void clear_item(int index);
    void redraw_item(int index);
    void redraw_current_item() { redraw_item(index_); }
    virtual void refresh();
    virtual bool is_using_custom_cursor_rect_sprite() { return false; }
    virtual bool is_custom_cursor_rect_sprite_visible();
    virtual double custom_cursor_rect_x_offset() { return 8; }
    virtual double custom_cursor_rect_y_offset() { return 4; }
    virtual double custom_cursor_rect_text_x_offset() { return 16; }
    virtual double custom_cursor_rect_text_y_offset() { return 0; }
    virtual double custom_cursor_rect_text_width_offset() { return 0; }
    virtual std::string custom_cursor_rect_bitmap_name() { return "cursor_menu"; }
    void init_custom_cursor_rect();
    void update_custom_cursor_rect_sprite_visibility();
    virtual void update_custom_cursor_rect_sprite(SpriteWindowCustomCursor* sprite, int index, bool use_index);
    void select_available(int index);
    void select_available() { select_available(index_); }

    bool input_lock = false;
    WindowHelp* help_window = nullptr;
    double scroll_x = 0, scroll_y = 0;
    int stay_count = 0;

protected:
    int index_ = -1;
    bool cursor_fixed_ = false;
    bool cursor_all_ = false;
    std::map<std::string, std::function<void()>> handlers_;
    std::shared_ptr<g2d::Sprite> custom_cursor_container_;
    std::shared_ptr<SpriteWindowCustomCursor> custom_cursor_sprite_;
    std::vector<std::shared_ptr<SpriteWindowCustomCursor>> custom_cursor_sprites_;
};

struct CommandItem {
    std::string name;
    std::string symbol;
    bool enabled = true;
    js::Val ext;
};

class WindowCommand : public WindowSelectable {
public:
    virtual void initialize(double x, double y);
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }
    virtual double window_width() { return 240; }
    virtual double window_height() { return fitting_height(num_visible_rows()); }
    virtual int num_visible_rows();
    int max_items() override { return (int)list_.size(); }
    void clear_command_list() { list_.clear(); }
    virtual void make_command_list() {}
    void add_command(const std::string& name, const std::string& symbol, bool enabled = true, const js::Val& ext = js::Val::null());
    std::string command_name(int index) const { return list_[(size_t)index].name; }
    std::string command_symbol(int index) const { return list_[(size_t)index].symbol; }
    bool is_command_enabled(int index) const { return list_[(size_t)index].enabled; }
    const CommandItem* current_data() const;
    bool is_current_item_enabled() override;
    std::string current_symbol() const;
    js::Val current_ext() const;
    int find_symbol(const std::string& symbol) const;
    void select_symbol(const std::string& symbol);
    int find_ext(const js::Val& ext) const;
    void select_ext(const js::Val& ext);
    void draw_item(int index) override;
    virtual std::string item_text_align();
    bool is_ok_enabled() override { return true; }
    void call_ok_handler() override;
    void refresh() override;
    std::vector<CommandItem>& list() { return list_; }

protected:
    std::vector<CommandItem> list_;
};

class WindowHorzCommand : public WindowCommand {
public:
    int num_visible_rows() override { return 1; }
    int max_cols() override { return 4; }
    std::string item_text_align() override { return "center"; }
};

class WindowHelp : public WindowBase {
public:
    virtual void initialize(int num_lines = 3);
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    virtual void set_text(const std::string& text);
    virtual void clear();
    virtual void set_item(const rpg::ItemRef& item);
    virtual void refresh();
    void set_battler(rpg::GameBattler* battler);
    bool special_selection_text(rpg::GameAction* action);
    virtual void draw_battler(rpg::GameBattler* battler);
    void draw_special_selection_text(rpg::GameAction* action);
    void draw_battler_with_icons(rpg::GameBattler* battler);
    const std::string& text() const { return text_; }

protected:
    std::string text_;
};

}
