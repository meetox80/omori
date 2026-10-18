#include "random.h"

namespace rng {

static uint64_t g_state = 0x9E3779B97F4A7C15ull;

void seed(uint64_t s) { g_state = s ? s : 0x9E3779B97F4A7C15ull; }

static uint64_t step() {
    uint64_t x = g_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_state = x;
    return x * 0x2545F4914F6CDD1Dull;
}

double next() { return (double)(step() >> 11) * (1.0 / 9007199254740992.0); }

int below(int n) { return n <= 0 ? 0 : (int)(next() * n); }

}
