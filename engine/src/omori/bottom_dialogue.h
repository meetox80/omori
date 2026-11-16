#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "../gfx2d/display.h"
#include "../platform/gfx.h"

namespace ui {

class WindowMessage;

namespace dialogue {

constexpr bool enabled() { return profile::BOTTOM_SCREEN; }

void begin(WindowMessage* source, const std::string& speaker);
void page();
void glyph(const std::string& src, size_t at, size_t end, const std::string& color, double ratio);
void soft_space();
void line_break();
void icon(int index);
void key_icon(const std::string& key);
void choices(WindowMessage* source, const std::vector<std::string>& names, const std::vector<bool>& enabled);
void forget(WindowMessage* source);

bool update();
bool changed();
void render(const g2d::RenderState& rs);

}
}
