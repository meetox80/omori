#pragma once

#include <cstddef>

#include "../layout.h"

namespace profile {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;
constexpr int HALF_W = 162;
constexpr int HALF_H = 122;
constexpr float SCALE = 0.5f;
constexpr float OFFSET_X = 0.0f;
constexpr float OFFSET_Y = 0.0f;
constexpr bool WIDE_565 = false;
constexpr int MOVIE_MAX = 512;
constexpr TexLayout TEX_LAYOUT = TexLayout::GsStraight;
constexpr bool NINTENDO_BUTTONS = false;
constexpr bool BOTTOM_SCREEN = false;
constexpr bool CANVAS_HEAP = false;
constexpr size_t HEAP_RESERVE = 2u << 20;
constexpr int AUDIO_RATE = 48000;
constexpr int AUDIO_AHEAD = 32768;

}
