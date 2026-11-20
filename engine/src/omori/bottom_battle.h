#pragma once

#include "../gfx2d/display.h"

namespace ui {
namespace bottom {
namespace battle {

bool update();
bool changed();
void render(const g2d::RenderState& rs);
void reset();

}
}
}
