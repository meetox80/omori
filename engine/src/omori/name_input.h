#pragma once
#include <string>
#include <vector>

#include "window_selectable.h"

namespace ui {

class WindowOmoriNameInputName : public WindowBase {
public:
    void initialize(int max);
    void initialize() { initialize(7); }
    void initialize(double x, double y, double w, double h) override { WindowBase::initialize(x, y, w, h); }
    int standard_padding() override { return 4; }
    double window_width() { return max_characters * 26.0; }
    double window_height() { return 80; }
    int standard_openness_type() override { return 2; }
    void refresh();
    void clear_name(const std::string& name);
    std::string name() const;
    bool add(const std::string& character);
    bool back();
    void refresh_text();
    bool add_hangul(const std::string& character);
    bool back_hangul();

    int max_characters = 7;
    std::vector<std::string> text;
    int text_index = 0;
};

class VirtualKeyboard : public WindowSelectable {
public:
    void initialize();
    void initialize(double x, double y, double w, double h) override { WindowSelectable::initialize(x, y, w, h); }
    bool is_ok_enabled() override { return true; }
    bool is_using_custom_cursor_rect_sprite() override { return true; }
    std::string custom_cursor_rect_bitmap_name() override { return "name_cursor"; }
    double custom_cursor_rect_x_offset() override { return -12; }
    int standard_font_size() override { return 24; }
    bool add(const std::string& character);
    bool back();
    void confirm_entry();
    std::string language() const;
    void process_ok() override;
    void setup();
    void dispose();
    void refresh() override;
    void determine_layout_data();
    int max_items() override;
    int max_cols() override;
    std::string character(int index);
    std::string character() { return character(index()); }
    void draw_item(int index) override;

    WindowOmoriNameInputName* name_window = nullptr;

private:
    std::string layout_type_ = "default";
    bool is_caps_ = false;
    std::vector<std::vector<std::string>> current_layout_;
    int layout_cols_ = 0;
};

}
