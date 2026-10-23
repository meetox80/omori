#include "../audio_out.h"
#include "../gfx.h"
#include "../platform.h"
#include "../common/testing.h"
#include "../../core/fs.h"
#include "sys3ds.h"

#include <3ds.h>
#include <citro3d.h>
#include <malloc.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

extern "C" {
extern u32 __ctru_heap;
extern u32 __ctru_heap_size;
extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;
extern char* fake_heap_start;
extern char* fake_heap_end;
void mappableInit(u32 addrMin, u32 addrMax);

void __system_allocateHeaps(void) {
    Handle reslimit = 0;
    if (R_FAILED(svcGetResourceLimit(&reslimit, CUR_PROCESS_HANDLE))) svcBreak(USERBREAK_PANIC);
    s64 max_commit = 0, cur_commit = 0;
    ResourceLimitType type = RESLIMIT_COMMIT;
    svcGetResourceLimitLimitValues(&max_commit, reslimit, &type, 1);
    svcGetResourceLimitCurrentValues(&cur_commit, reslimit, &type, 1);
    svcCloseHandle(reslimit);
    u32 remaining = (u32)(max_commit - cur_commit) & ~0xFFFu;
    __ctru_heap_size = (remaining / 100 * 45) & ~0xFFFu;
    __ctru_linear_heap_size = remaining - __ctru_heap_size;
    if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0x0, __ctru_heap_size, MEMOP_ALLOC, (MemPerm)(MEMPERM_READ | MEMPERM_WRITE))))
        svcBreak(USERBREAK_PANIC);
    if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0x0, 0x0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR, (MemPerm)(MEMPERM_READ | MEMPERM_WRITE))))
        svcBreak(USERBREAK_PANIC);
    mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
    fake_heap_start = (char*)__ctru_heap;
    fake_heap_end = fake_heap_start + __ctru_heap_size;
}
}

namespace plat {

static Options g_opt;
static volatile bool g_running = true;
static uint32_t g_buttons = 0;
static bool g_touching = false;
static int g_touch_x = 0, g_touch_y = 0;
static uint64_t g_frame = 0;
static FILE* g_log = nullptr;
static volatile bool g_resumed = false;
static aptHookCookie g_apt_cookie;
static bool g_new3ds = false;
static s32 g_main_prio = 0x30;

static void apt_hook(APT_HookType hook, void*) {
    if (hook == APTHOOK_ONRESTORE || hook == APTHOOK_ONWAKEUP) g_resumed = true;
    if (hook == APTHOOK_ONEXIT) g_running = false;
}

size_t heap_limit() { return (size_t)__ctru_heap_size + (size_t)__ctru_linear_heap_size; }

size_t heap_used() {
    struct mallinfo mi = mallinfo();
    size_t linear_free = gfx::arena_free_bytes();
    size_t linear_used = __ctru_linear_heap_size > linear_free ? __ctru_linear_heap_size - linear_free : 0;
    return (size_t)mi.uordblks + linear_used;
}

size_t free_memory() {
    size_t used = heap_used(), limit = heap_limit();
    return limit > used ? limit - used : 0;
}

static std::string dir_of(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

bool init(int argc, char** argv) {
    osSetSpeedupEnable(true);
    APT_SetAppCpuTimeLimit(80);
    bool n3ds = false;
    APT_CheckNew3DS(&n3ds);
    g_new3ds = n3ds;
    svcGetThreadPriority(&g_main_prio, CUR_THREAD_HANDLE);
    aptHook(&g_apt_cookie, apt_hook, nullptr);
    std::string self = argc > 0 && argv[0] && std::strstr(argv[0], ".3dsx") ? std::string(argv[0]) : std::string("sdmc:/3ds/omori/omori.3dsx");
    std::string base = dir_of(self);
    if (base.empty()) base = "sdmc:/3ds/omori";
    mkdir("sdmc:/3ds", 0777);
    mkdir(base.c_str(), 0777);
    g_opt.data_root = self;
    g_opt.save_root = base + "/saves";
    make_dir(g_opt.save_root);
    g_log = std::fopen((base + "/log.txt").c_str(), "w");
    g_opt.shot_prefix = base + "/shot";
    std::vector<std::string> args = testing::read_args_file(base + "/args.txt");
    for (auto& a : args) {
        if (!a.empty() && a[0] != '-' && a.find(':') == std::string::npos && a.find('/') == std::string::npos && a.find('.') != std::string::npos) a = base + "/" + a;
    }
    testing::parse_args(g_opt, args);
    testing::setup(g_opt);
    if (g_opt.ballast_mb > 0) {
        size_t total = (size_t)g_opt.ballast_mb << 20;
        void* heap = std::malloc(total / 100 * 45);
        void* linear = gfx::alloc(total - total / 100 * 45);
        log("ballast: holding %d MB (%s)", g_opt.ballast_mb, heap && linear ? "ok" : "partial");
    }
    log("omori for 3ds: %s, heap %u KB, linear %u KB", g_new3ds ? "new 3ds" : "old 3ds", (unsigned)(__ctru_heap_size / 1024),
        (unsigned)(__ctru_linear_heap_size / 1024));
    fs::init_data(g_opt.data_root);
    gfx::init();
    return true;
}

void shutdown() {
    audio_out::stop();
    gfx::shutdown();
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
    aptUnhook(&g_apt_cookie);
    std::exit(0);
}

bool running() { return g_running; }
void request_exit() { g_running = false; }

void poll() {
    if (!aptMainLoop()) g_running = false;
    hidScanInput();
    u32 k = hidKeysHeld();
    uint32_t b = 0;
    if (k & KEY_UP) b |= BTN_UP;
    if (k & KEY_DOWN) b |= BTN_DOWN;
    if (k & KEY_LEFT) b |= BTN_LEFT;
    if (k & KEY_RIGHT) b |= BTN_RIGHT;
    if (k & KEY_A) b |= BTN_OK;
    if (k & KEY_B) b |= BTN_CANCEL;
    if (k & KEY_X) b |= BTN_MENU;
    if (k & KEY_Y) b |= BTN_DASH;
    if (k & (KEY_L | KEY_ZL)) b |= BTN_PAGEUP;
    if (k & (KEY_R | KEY_ZR)) b |= BTN_PAGEDOWN;
    if (k & KEY_START) b |= BTN_START;
    if (k & KEY_SELECT) b |= BTN_SELECT;
    b |= testing::buttons(g_frame);
    g_buttons = b;
    g_touching = false;
    if (k & KEY_TOUCH) {
        touchPosition t;
        hidTouchRead(&t);
        g_touch_x = t.px;
        g_touch_y = t.py;
        g_touching = true;
    }
    if (testing::touch(g_frame, g_touch_x, g_touch_y)) g_touching = true;
}

uint32_t buttons() { return g_buttons; }

bool touch(int& x, int& y) {
    if (!g_touching) return false;
    x = g_touch_x;
    y = g_touch_y;
    return true;
}
uint64_t frame_count() { return g_frame; }

double time_seconds() { return (double)svcGetSystemTick() / (double)SYSCLOCK_ARM11; }

uint64_t time_us() { return (uint64_t)((double)svcGetSystemTick() * 1000000.0 / (double)SYSCLOCK_ARM11); }

static void write_shot() {
    std::vector<uint32_t> px((size_t)gfx::SCREEN_W * gfx::SCREEN_H);
    if (!gfx::capture(px.data(), gfx::SCREEN_W)) return;
    testing::write_bmp(testing::shot_path(g_opt, g_frame, "bmp"), px.data(), gfx::SCREEN_W, gfx::SCREEN_H, gfx::SCREEN_W);
}

static u32 g_last_vblank = 0;
static bool g_have_vblank = false;

static bool scripted() { return g_opt.max_frames > 0 || !g_opt.input_script.empty() || !g_opt.shot_frames.empty(); }

int pending_ticks() {
    u32 now = C3D_FrameCounter(0);
    u32 last = g_last_vblank;
    bool had = g_have_vblank;
    g_last_vblank = now;
    g_have_vblank = true;
    if (!had || scripted()) return 1;
    return std::clamp((int)(now - last), 1, 4);
}

void frame_end() {
    gfx::end_frame();
    if (testing::shot_due(g_frame)) write_shot();
    g_frame++;
    if (g_opt.max_frames > 0 && g_frame >= (uint64_t)g_opt.max_frames) g_running = false;
    if (testing::exit_due(g_frame)) g_running = false;
}

const Options& options() { return g_opt; }
const std::string& data_root() { return g_opt.data_root; }
const std::string& save_root() { return g_opt.save_root; }

bool make_dir(const std::string& path) { return mkdir(path.c_str(), 0777) == 0; }

void log(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_log, fmt, ap);
    va_end(ap);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}

bool debug_warp(int& map, int& x, int& y, int& d) { return testing::warp_due(g_frame, map, x, y, d); }
bool debug_battle(int& troop) { return testing::battle_due(g_frame, troop); }
bool debug_eval(std::string& src) { return testing::eval_due(g_frame, src); }
bool debug_reset() { return testing::reset_due(g_frame); }

bool consume_resume() {
    if (!g_resumed) return false;
    g_resumed = false;
    return true;
}

bool circle_confirms() { return false; }

struct Mutex {
    LightLock lock;
};

static void thread_entry(void* arg) {
    ThreadFn fn = (ThreadFn)arg;
    if (fn) fn();
}

bool spawn(void (*entry)(void*), void* arg, size_t stack, int prio, bool prefer_far_core) {
    int cores[3];
    int n = 0;
    if (prefer_far_core && g_new3ds) cores[n++] = 2;
    cores[n++] = 1;
    cores[n++] = -2;
    prio = std::clamp(prio, 0x18, 0x3F);
    for (int i = 0; i < n; i++) {
        if (threadCreate(entry, arg, stack, prio, cores[i], true)) return true;
    }
    return false;
}

bool start_thread(ThreadFn fn) { return spawn(thread_entry, (void*)fn, 64 * 1024, g_main_prio + 1, false); }

int main_priority() { return g_main_prio; }

Mutex* mutex_create() {
    Mutex* m = new Mutex();
    LightLock_Init(&m->lock);
    return m;
}

void mutex_lock(Mutex* m) { LightLock_Lock(&m->lock); }
void mutex_unlock(Mutex* m) { LightLock_Unlock(&m->lock); }
void sleep_ms(int ms) { svcSleepThread((s64)ms * 1000000LL); }

}

namespace audio_out {

constexpr int BUFFERS = 4;

static MixFn g_fn = nullptr;
static LightLock g_lock = 1;
static LightEvent g_event;
static ndspWaveBuf g_wave[BUFFERS];
static int16_t* g_pcm = nullptr;
static volatile bool g_run = false;
static volatile bool g_done = true;
static bool g_ndsp = false;

static void ndsp_callback(void*) { LightEvent_Signal(&g_event); }

static void fill(ndspWaveBuf& wb) {
    int16_t* out = wb.data_pcm16;
    lock();
    if (g_fn) g_fn(out, BLOCK);
    else std::memset(out, 0, (size_t)BLOCK * 4);
    unlock();
    DSP_FlushDataCache(out, (u32)BLOCK * 4);
    ndspChnWaveBufAdd(0, &wb);
}

static void audio_thread(void*) {
    while (g_run) {
        for (auto& wb : g_wave) {
            if (wb.status == NDSP_WBUF_DONE || wb.status == NDSP_WBUF_FREE) fill(wb);
        }
        LightEvent_Wait(&g_event);
    }
    g_done = true;
}

bool start(MixFn fn) {
    g_fn = fn;
    if (R_FAILED(ndspInit())) {
        plat::log("audio: ndspInit failed; sound needs sdmc:/3ds/dspfirm.cdc (dump it with DSP1)");
        return false;
    }
    g_ndsp = true;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {1.0f, 1.0f};
    ndspChnSetMix(0, mix);
    g_pcm = (int16_t*)gfx::alloc((size_t)BLOCK * 4 * BUFFERS);
    if (!g_pcm) return false;
    std::memset(g_pcm, 0, (size_t)BLOCK * 4 * BUFFERS);
    std::memset(g_wave, 0, sizeof(g_wave));
    for (int i = 0; i < BUFFERS; i++) {
        g_wave[i].data_pcm16 = g_pcm + (size_t)i * BLOCK * 2;
        g_wave[i].nsamples = BLOCK;
    }
    LightEvent_Init(&g_event, RESET_ONESHOT);
    ndspSetCallback(ndsp_callback, nullptr);
    g_run = true;
    g_done = false;
    if (!plat::spawn(audio_thread, nullptr, 32 * 1024, plat::main_priority() - 2, true)) {
        g_run = false;
        g_done = true;
        plat::log("audio: cannot start the mixing thread");
        return false;
    }
    return true;
}

static void feeder_entry(void* arg) { ((void (*)())arg)(); }

bool start_feeder(void (*fn)()) { return plat::spawn(feeder_entry, (void*)fn, 64 * 1024, plat::main_priority() - 1, true); }

void stop() {
    if (!g_ndsp) return;
    if (g_run) {
        g_run = false;
        LightEvent_Signal(&g_event);
        for (int i = 0; i < 100 && !g_done; i++) svcSleepThread(2000000LL);
    }
    ndspChnWaveBufClear(0);
    ndspExit();
    g_ndsp = false;
}

void lock() { LightLock_Lock(&g_lock); }
void unlock() { LightLock_Unlock(&g_lock); }

}

static void (*g_low_memory)() = nullptr;
static u32 g_main_thread = 0;
static bool g_in_low_memory = false;

static u32 current_thread_id() {
    u32 id = 0;
    svcGetThreadId(&id, CUR_THREAD_HANDLE);
    return id;
}

void plat::set_low_memory_handler(void (*fn)()) {
    g_low_memory = fn;
    g_main_thread = current_thread_id();
}

constexpr size_t LARGE_BLOCK = 64 * 1024;

static void* try_alloc(size_t size) {
    void* p = nullptr;
    if (size >= LARGE_BLOCK) {
        p = gfx::arena_try_alloc(size);
        if (!p) p = std::malloc(size);
    } else {
        p = std::malloc(size);
        if (!p) p = gfx::arena_try_alloc(size);
    }
    return p;
}

bool plat::relieve_memory() {
    if (!g_low_memory || g_in_low_memory || current_thread_id() != g_main_thread) return false;
    g_in_low_memory = true;
    g_low_memory();
    g_in_low_memory = false;
    return true;
}

static void* checked_alloc(size_t size) {
    if (size == 0) size = 1;
    void* p = try_alloc(size);
    if (!p && plat::relieve_memory()) {
        p = try_alloc(size);
        if (p) plat::log("low memory: freed caches for %u bytes", (unsigned)size);
    }
    if (!p) {
        plat::log("out of memory: %u bytes requested, heap used %u of %u KB", (unsigned)size, (unsigned)(plat::heap_used() / 1024),
                  (unsigned)(plat::heap_limit() / 1024));
        throw std::bad_alloc();
    }
    return p;
}

void* operator new(size_t size) { return checked_alloc(size); }
void* operator new[](size_t size) { return checked_alloc(size); }
void operator delete(void* p) noexcept { gfx::release(p); }
void operator delete[](void* p) noexcept { gfx::release(p); }
void operator delete(void* p, size_t) noexcept { gfx::release(p); }
void operator delete[](void* p, size_t) noexcept { gfx::release(p); }
