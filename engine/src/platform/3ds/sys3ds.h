#pragma once
#include <cstddef>

namespace plat {

bool spawn(void (*entry)(void*), void* arg, size_t stack, int prio, bool prefer_far_core);
int main_priority();
bool relieve_memory();

}
