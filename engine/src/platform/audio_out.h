#pragma once
#include <cstdint>

#include "profile.h"

namespace audio_out {

constexpr int RATE = profile::AUDIO_RATE;
constexpr int BLOCK = 1024;

using MixFn = void (*)(int16_t* out, int frames);

bool start(MixFn fn);
bool start_feeder(void (*fn)());
void stop();
void lock();
void unlock();

}
