#include "../movie_decode.h"

#include <pspjpeg.h>
#include <pspkernel.h>
#include <psputility.h>

#include <cstring>

#include "../common/jpeg_soft.h"
#include "../platform.h"

namespace movie_decode {

static int g_state = 0;
static int g_width = 0;
static int g_height = 0;
static int g_failures = 0;

static bool hardware(int stride, int rows) {
    if (g_state == 0) {
        int r = sceUtilityLoadAvModule(PSP_AV_MODULE_AVCODEC);
        if (r < 0 && r != (int)0x80020139) {
            plat::log("movie: avcodec module unavailable (%08x), using software jpeg", r);
            g_state = -1;
        } else if ((r = sceJpegInitMJpeg()) < 0) {
            plat::log("movie: sceJpegInitMJpeg failed (%08x), using software jpeg", r);
            g_state = -1;
        } else {
            g_state = 1;
        }
    }
    if (g_state != 1) return false;
    if (g_width != stride || g_height != rows) {
        if (g_width) sceJpegDeleteMJpeg();
        g_width = g_height = 0;
        int r = sceJpegCreateMJpeg(stride, rows);
        if (r < 0) {
            plat::log("movie: sceJpegCreateMJpeg(%d, %d) failed (%08x), using software jpeg", stride, rows, r);
            sceJpegFinishMJpeg();
            g_state = -1;
            return false;
        }
        g_width = stride;
        g_height = rows;
    }
    return true;
}

bool decode(const uint8_t* jpeg, size_t len, uint32_t* dst, int stride, int rows) {
    unsigned int bytes = (unsigned int)((size_t)stride * rows * 4);
    if (hardware(stride, rows)) {
        sceKernelDcacheWritebackRange(jpeg, (unsigned int)len);
        int r = sceJpegDecodeMJpeg((u8*)jpeg, (SceSize)len, (u8*)dst, 0);
        sceKernelDcacheWritebackInvalidateRange(dst, bytes);
        if (r >= 0) return true;
        if (++g_failures <= 4) plat::log("movie: sceJpegDecodeMJpeg failed (%08x), decoding in software", r);
    }
    bool ok = jpeg_soft_decode(jpeg, len, dst, stride, rows);
    sceKernelDcacheWritebackRange(dst, bytes);
    return ok;
}

static int thread_entry(SceSize args, void* argp) {
    void (*fn)() = nullptr;
    if (args >= sizeof(fn)) std::memcpy(&fn, argp, sizeof(fn));
    if (fn) fn();
    return 0;
}

bool start_thread(void (*fn)()) {
    SceUID th = sceKernelCreateThread("movie", thread_entry, 0x1F, 0x10000, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, nullptr);
    if (th < 0) return false;
    return sceKernelStartThread(th, sizeof(fn), &fn) >= 0;
}

}
