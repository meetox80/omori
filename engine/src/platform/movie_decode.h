#pragma once
#include <cstddef>
#include <cstdint>

namespace movie_decode {

bool decode(const uint8_t* jpeg, size_t len, uint32_t* dst, int stride, int rows);
bool start_thread(void (*fn)());

}
