#pragma once
#include <cstdint>

namespace ps2 {

uint32_t vsync_count();
int refresh_hz();
int spawn(void (*entry)(void*), void* arg, int stack_size, int priority);

}
