#include "../movie_decode.h"

#include <gccore.h>
#include <turbojpeg.h>

#include "../platform.h"

namespace movie_decode {

static tjhandle g_tj = nullptr;
static int g_failures = 0;

bool decode(const uint8_t* jpeg, size_t len, uint32_t* dst, int stride, int rows) {
    if (!g_tj) g_tj = tjInitDecompress();
    if (!g_tj) return false;
    int w = 0, h = 0, sub = 0, space = 0;
    if (tjDecompressHeader3(g_tj, jpeg, (unsigned long)len, &w, &h, &sub, &space) != 0 || w > stride || h > rows) {
        if (++g_failures <= 4) plat::log("movie: bad jpeg frame (%s)", tjGetErrorStr2(g_tj));
        return false;
    }
    if (tjDecompress2(g_tj, jpeg, (unsigned long)len, (unsigned char*)dst, w, stride * 4, h, TJPF_ABGR, TJFLAG_FASTDCT | TJFLAG_FASTUPSAMPLE) != 0) {
        if (++g_failures <= 4) plat::log("movie: jpeg decode failed (%s)", tjGetErrorStr2(g_tj));
        return false;
    }
    return true;
}

static void* entry(void* arg) {
    void (*fn)() = (void (*)())arg;
    if (fn) fn();
    return nullptr;
}

bool start_thread(void (*fn)()) {
    lwp_t th;
    return LWP_CreateThread(&th, entry, (void*)fn, nullptr, 128 * 1024, 40) >= 0;
}

}
