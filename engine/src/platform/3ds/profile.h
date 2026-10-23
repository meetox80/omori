#pragma once

#include <cstddef>

#include "../layout.h"

namespace profile {

constexpr int SCREEN_W = 400;
constexpr int SCREEN_H = 240;
constexpr int HALF_W = 200;
constexpr int HALF_H = 122;
constexpr float SCALE = 0.5f;
constexpr float OFFSET_X = 40.0f;
constexpr float OFFSET_Y = 0.0f;
constexpr bool WIDE_565 = true;
constexpr int MOVIE_MAX = 512;
constexpr TexLayout TEX_LAYOUT = TexLayout::PicaTiled;
constexpr bool NINTENDO_BUTTONS = true;
constexpr bool CONFIRM_SWAP = false;
constexpr bool BOTTOM_SCREEN = true;
constexpr bool CANVAS_HEAP = true;
constexpr size_t HEAP_RESERVE = 5u << 20;
constexpr int AUDIO_RATE = 44100;
constexpr int AUDIO_AHEAD = 32768;

}
