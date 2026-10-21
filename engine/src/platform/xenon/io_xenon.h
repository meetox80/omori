#pragma once

namespace xenon_io {

void lock();
void unlock();
int thread_id();

struct Guard {
    Guard() { lock(); }
    ~Guard() { unlock(); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

}
