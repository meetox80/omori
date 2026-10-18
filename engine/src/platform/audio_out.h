#pragma once
#include <cstdint>

namespace audio_out {

constexpr int RATE = 44100;
constexpr int BLOCK = 1024;

using MixFn = void (*)(int16_t* out, int frames);

bool start(MixFn fn);
void stop();
void lock();
void unlock();

}
