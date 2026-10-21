#pragma once

#include <cstddef>

#include "../layout.h"

namespace profile {

constexpr int SCREEN_W = 640;
constexpr int SCREEN_H = 480;
constexpr int HALF_W = 322;
constexpr int HALF_H = 242;
constexpr float SCALE = 1.0f;
constexpr float OFFSET_X = 0.0f;
constexpr float OFFSET_Y = 0.0f;
constexpr bool WIDE_565 = false;
constexpr int MOVIE_MAX = 1024;
constexpr TexLayout TEX_LAYOUT = TexLayout::Argb;
constexpr bool NINTENDO_BUTTONS = false;
constexpr bool CANVAS_HEAP = false;
constexpr size_t HEAP_RESERVE = 5u << 20;
constexpr int AUDIO_RATE = 44100;
constexpr int AUDIO_AHEAD = 0;

}
