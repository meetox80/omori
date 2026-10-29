#include "workers.h"

#include <atomic>

#include "../platform/platform.h"

namespace workers {

static std::atomic<int> g_count{0};
static std::atomic<bool> g_live{true};

bool start(bool (*launch)(Fn), Fn fn) {
    g_count.fetch_add(1, std::memory_order_acq_rel);
    if (launch(fn)) return true;
    g_count.fetch_sub(1, std::memory_order_acq_rel);
    return false;
}

bool live() { return g_live.load(std::memory_order_acquire); }

void leave() { g_count.fetch_sub(1, std::memory_order_acq_rel); }

void stop() {
    g_live.store(false, std::memory_order_release);
    for (int i = 0; i < 1500 && g_count.load(std::memory_order_acquire) > 0; i++) plat::sleep_ms(2);
}

}
