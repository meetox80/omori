#include "../movie_decode.h"

#include <thread>

#include "../common/jpeg_soft.h"

namespace movie_decode {

bool decode(const uint8_t* jpeg, size_t len, uint32_t* dst, int stride, int rows) { return jpeg_soft_decode(jpeg, len, dst, stride, rows); }

bool start_thread(void (*fn)()) {
    std::thread(fn).detach();
    return true;
}

}
