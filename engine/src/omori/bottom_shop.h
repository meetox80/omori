#pragma once

#include "../gfx2d/display.h"

namespace ui {
namespace bottom {
namespace shop {

bool counting();
bool update();
bool changed();
void render(const g2d::RenderState& rs);
void hold_touch();
void reset();

}
}
}
