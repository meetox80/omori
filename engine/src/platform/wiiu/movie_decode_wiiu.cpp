#include "../movie_decode.h"

#include <coreinit/thread.h>
#include <turbojpeg.h>

#include "../platform.h"

namespace wiiu {
bool spawn(void (*fn)(), uint32_t stack_size, int priority, OSThreadAttributes cores, const char* name);
}

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
    if (tjDecompress2(g_tj, jpeg, (unsigned long)len, (unsigned char*)dst, w, stride * 4, h, TJPF_ABGR, 0) != 0) {
        if (++g_failures <= 4) plat::log("movie: jpeg decode failed (%s)", tjGetErrorStr2(g_tj));
        return false;
    }
    return true;
}

bool start_thread(void (*fn)()) { return wiiu::spawn(fn, 256 * 1024, 18, OS_THREAD_ATTRIB_AFFINITY_CPU2, "omori movie"); }

}
