#include "../movie_decode.h"

#include <psp2/kernel/threadmgr.h>

#include <cstring>

#include "../common/jpeg_soft.h"

namespace movie_decode {

bool decode(const uint8_t* jpeg, size_t len, uint32_t* dst, int stride, int rows) { return jpeg_soft_decode(jpeg, len, dst, stride, rows); }

static int thread_entry(SceSize args, void* argp) {
    void (*fn)() = nullptr;
    if (args >= sizeof(fn)) std::memcpy(&fn, argp, sizeof(fn));
    if (fn) fn();
    return 0;
}

bool start_thread(void (*fn)()) {
    SceUID th = sceKernelCreateThread("movie", thread_entry, 0x10000100 + 4, 0x40000, 0, SCE_KERNEL_CPU_MASK_USER_1, nullptr);
    if (th < 0) return false;
    return sceKernelStartThread(th, sizeof(fn), &fn) >= 0;
}

}
