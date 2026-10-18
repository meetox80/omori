#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../rpg/scene.h"
#include "window_base.h"

namespace ui {

class WindowMessage;
class WindowScrollText;

std::shared_ptr<WindowMessage> create_message_window(rpg::Scene& scene);
std::shared_ptr<WindowScrollText> create_scroll_text_window(rpg::Scene& scene);
WindowMessage* current_message_window();
bool message_window_is_closing(WindowMessage* w);
bool message_window_is_open(WindowMessage* w);
bool message_window_is_any_sub_window_active(WindowMessage* w);
void message_window_terminate(WindowMessage* w);
g2d::Window* message_window_as_window(WindowMessage* w);
std::shared_ptr<g2d::Sprite> message_face_box_container(WindowMessage* w);
void forget_message_window(WindowMessage* w);

struct MessageHooks {
    std::function<bool()> terminate_message;
    std::function<void()> after_terminate_message;
    std::function<bool(double&)> new_line_x;
    std::function<std::string(const std::string&)> convert_escape_characters;
    std::function<void(const std::string&, TextState&)> process_escape_character;
};
void set_message_hooks(WindowMessage* w, MessageHooks hooks);
WindowBase* message_window_base(WindowMessage* w);
void message_window_draw_text_page(WindowMessage* w, const std::string& text, double x, double y);
void message_window_reopen_name_box(WindowMessage* w);

}
