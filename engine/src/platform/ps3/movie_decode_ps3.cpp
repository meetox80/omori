#include "../movie_decode.h"

#include <sys/thread.h>

#include "../common/jpeg_soft.h"

namespace movie_decode {

bool decode(const uint8_t* jpeg, size_t len, uint32_t* dst, int stride, int rows) { return jpeg_soft_decode(jpeg, len, dst, stride, rows); }

static void entry(void* arg) {
    void (*fn)() = (void (*)())arg;
    if (fn) fn();
    sysThreadExit(0);
}

bool start_thread(void (*fn)()) {
    sys_ppu_thread_t id;
    char name[] = "movie";
    return sysThreadCreate(&id, entry, (void*)fn, 1200, 256 * 1024, 0, name) == 0;
}

}
