#pragma once
#include <cstdint>

namespace rng {

void seed(uint64_t s);
double next();
int below(int n);

}
