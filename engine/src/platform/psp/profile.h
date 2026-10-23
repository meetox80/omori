#pragma once

#include <cstddef>

#include "../layout.h"

namespace profile {

constexpr int SCREEN_W = 480;
constexpr int SCREEN_H = 272;
constexpr int HALF_W = 240;
constexpr int HALF_H = 138;
constexpr float SCALE = 0.5625f;
constexpr float OFFSET_X = 60.0f;
constexpr float OFFSET_Y = 1.0f;
constexpr bool WIDE_565 = true;
constexpr int MOVIE_MAX = 512;
constexpr TexLayout TEX_LAYOUT = TexLayout::PspSwizzle;
constexpr bool NINTENDO_BUTTONS = false;
constexpr bool CONFIRM_SWAP = true;
constexpr bool BOTTOM_SCREEN = false;
constexpr bool CANVAS_HEAP = false;
constexpr size_t HEAP_RESERVE = 5u << 20;
constexpr int AUDIO_RATE = 44100;
constexpr int AUDIO_AHEAD = 32768;

}
