#pragma once
#include <memory>

#include "../gfx2d/display.h"

namespace ui {

std::shared_ptr<g2d::Container> video_container();
void update_videos();
bool movie_covers_screen();
void render_movie_overlay();

}
