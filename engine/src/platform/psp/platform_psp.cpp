#include "../audio_out.h"
#include "../gfx.h"
#include "../platform.h"
#include "../common/testing.h"
#include "../../core/fs.h"

#include <malloc.h>
#include <pspaudio.h>
#include <pspctrl.h>
#include <pspdisplay.h>
#include <pspiofilemgr.h>
#include <pspkernel.h>
#include <psppower.h>
#include <psprtc.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <vector>

PSP_MODULE_INFO("omori", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_THRESHOLD_SIZE_KB(1024);
PSP_HEAP_SIZE_KB(-3072);
PSP_MAIN_THREAD_STACK_SIZE_KB(1024);

namespace plat {

static Options g_opt;
static volatile bool g_running = true;
static uint32_t g_buttons = 0;
static uint64_t g_frame = 0;
static FILE* g_log = nullptr;

static volatile bool g_resumed = false;

static int exit_callback(int, int, void*) {
    g_running = false;
    return 0;
}

static int power_callback(int, int info, void*) {
    if (info & PSP_POWER_CB_RESUME_COMPLETE) g_resumed = true;
    return 0;
}

static int callback_thread(SceSize, void*) {
    int cbid = sceKernelCreateCallback("exit", exit_callback, nullptr);
    sceKernelRegisterExitCallback(cbid);
    int pcb = sceKernelCreateCallback("power", power_callback, nullptr);
    scePowerRegisterCallback(-1, pcb);
    sceKernelSleepThreadCB();
    return 0;
}

static size_t g_heap_limit = 0;

static void measure_heap() {
    std::vector<void*> blocks;
    blocks.reserve(1024);
    while (blocks.size() < 1024) {
        void* b = std::malloc(64 * 1024);
        if (!b) break;
        blocks.push_back(b);
    }
    struct mallinfo mi = mallinfo();
    g_heap_limit = (size_t)mi.arena;
    for (void* b : blocks) std::free(b);
}

size_t heap_limit() { return g_heap_limit; }

bool init(int argc, char** argv) {
    measure_heap();
    int th = sceKernelCreateThread("callbacks", callback_thread, 0x11, 0xFA0, 0, nullptr);
    if (th >= 0) sceKernelStartThread(th, 0, nullptr);
    scePowerSetClockFrequency(333, 333, 166);
    std::string base = "ms0:/PSP/GAME/OMORI";
    if (argc > 0 && argv[0]) {
        std::string a = argv[0];
        size_t slash = a.find_last_of('/');
        if (slash != std::string::npos) base = a.substr(0, slash);
    }
    g_opt.data_root = argc > 0 && argv[0] && std::string(argv[0]).find(".PBP") != std::string::npos ? std::string(argv[0]) : base + "/EBOOT.PBP";
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
    fs::init_data(g_opt.data_root);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);
    gfx::init();
    return true;
}

void shutdown() {
    audio_out::stop();
    gfx::shutdown();
    if (g_log) std::fclose(g_log);
    sceKernelExitGame();
}

bool running() { return g_running; }
void request_exit() { g_running = false; }

void poll() {
    SceCtrlData pad;
    sceCtrlPeekBufferPositive(&pad, 1);
    uint32_t b = 0;
    if (pad.Buttons & PSP_CTRL_UP) b |= BTN_UP;
    if (pad.Buttons & PSP_CTRL_DOWN) b |= BTN_DOWN;
    if (pad.Buttons & PSP_CTRL_LEFT) b |= BTN_LEFT;
    if (pad.Buttons & PSP_CTRL_RIGHT) b |= BTN_RIGHT;
    if (pad.Buttons & PSP_CTRL_CROSS) b |= BTN_OK;
    if (pad.Buttons & PSP_CTRL_CIRCLE) b |= BTN_CANCEL;
    if (pad.Buttons & PSP_CTRL_TRIANGLE) b |= BTN_MENU;
    if (pad.Buttons & PSP_CTRL_SQUARE) b |= BTN_DASH;
    if (pad.Buttons & PSP_CTRL_LTRIGGER) b |= BTN_PAGEUP;
    if (pad.Buttons & PSP_CTRL_RTRIGGER) b |= BTN_PAGEDOWN;
    if (pad.Buttons & PSP_CTRL_START) b |= BTN_START;
    if (pad.Buttons & PSP_CTRL_SELECT) b |= BTN_SELECT;
    b |= testing::buttons(g_frame);
    g_buttons = b;
}

uint32_t buttons() { return g_buttons; }
uint64_t frame_count() { return g_frame; }

double time_seconds() { return (double)sceKernelGetSystemTimeWide() / 1000000.0; }

uint64_t time_us() { return (uint64_t)sceKernelGetSystemTimeWide(); }

size_t heap_used() {
    struct mallinfo mi = mallinfo();
    size_t used = (size_t)mi.uordblks, idle = gfx::arena_free_bytes();
    return used > idle ? used - idle : 0;
}


static void write_shot() {
    std::vector<uint32_t> px((size_t)gfx::SCREEN_W * gfx::SCREEN_H);
    if (!gfx::capture(px.data(), gfx::SCREEN_W)) return;
    testing::write_bmp(testing::shot_path(g_opt, g_frame, "bmp"), px.data(), gfx::SCREEN_W, gfx::SCREEN_H, gfx::SCREEN_W);
}

static unsigned g_last_vcount = 0;

static bool scripted() { return g_opt.max_frames > 0 || !g_opt.input_script.empty() || !g_opt.shot_frames.empty(); }

int pending_ticks() {
    unsigned now = sceDisplayGetVcount();
    unsigned last = g_last_vcount;
    g_last_vcount = now;
    if (last == 0 || scripted()) return 1;
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

bool make_dir(const std::string& path) { return sceIoMkdir(path.c_str(), 0777) >= 0; }

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

struct Mutex {
    SceUID sema;
};

static int thread_entry(SceSize size, void* args) {
    ThreadFn fn = nullptr;
    if (size == sizeof(fn)) std::memcpy(&fn, args, sizeof(fn));
    if (fn) fn();
    return 0;
}

bool start_thread(ThreadFn fn) {
    SceUID th = sceKernelCreateThread("worker", thread_entry, 0x28, 0x10000, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, nullptr);
    if (th < 0) return false;
    return sceKernelStartThread(th, sizeof(fn), &fn) >= 0;
}

Mutex* mutex_create() {
    Mutex* m = new Mutex();
    m->sema = sceKernelCreateSema("mutex", 0, 1, 1, nullptr);
    return m;
}

void mutex_lock(Mutex* m) { sceKernelWaitSema(m->sema, 1, nullptr); }
void mutex_unlock(Mutex* m) { sceKernelSignalSema(m->sema, 1); }
void sleep_ms(int ms) { sceKernelDelayThread(ms * 1000); }

size_t free_memory() {
    struct mallinfo mi = mallinfo();
    return (size_t)mi.fordblks;
}

}

namespace audio_out {

static MixFn g_fn = nullptr;
static int g_channel = -1;
static SceUID g_thread = -1;
static SceUID g_sema = -1;
static volatile bool g_run = false;
static int16_t __attribute__((aligned(64))) g_buf[2][BLOCK * 2];

static int audio_thread(SceSize, void*) {
    int cur = 0;
    while (g_run) {
        lock();
        if (g_fn) g_fn(g_buf[cur], BLOCK);
        else std::memset(g_buf[cur], 0, sizeof(g_buf[cur]));
        unlock();
        sceAudioOutputPannedBlocking(g_channel, PSP_AUDIO_VOLUME_MAX, PSP_AUDIO_VOLUME_MAX, g_buf[cur]);
        cur ^= 1;
    }
    return 0;
}

bool start(MixFn fn) {
    g_fn = fn;
    g_sema = sceKernelCreateSema("audio", 0, 1, 1, nullptr);
    g_channel = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, BLOCK, PSP_AUDIO_FORMAT_STEREO);
    if (g_channel < 0) return false;
    g_run = true;
    g_thread = sceKernelCreateThread("audio", audio_thread, 0x12, 0x40000, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, nullptr);
    if (g_thread < 0) return false;
    sceKernelStartThread(g_thread, 0, nullptr);
    return true;
}

bool start_feeder(void (*fn)()) {
    SceUID th = sceKernelCreateThread("feeder", plat::thread_entry, 0x18, 0x20000, PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU, nullptr);
    if (th < 0) return false;
    return sceKernelStartThread(th, sizeof(fn), &fn) >= 0;
}

void stop() {
    if (!g_run) return;
    g_run = false;
    sceKernelWaitThreadEnd(g_thread, nullptr);
    sceKernelDeleteThread(g_thread);
    sceAudioChRelease(g_channel);
    sceKernelDeleteSema(g_sema);
}

void lock() {
    if (g_sema >= 0) sceKernelWaitSema(g_sema, 1, nullptr);
}

void unlock() {
    if (g_sema >= 0) sceKernelSignalSema(g_sema, 1);
}

}

static void (*g_low_memory)() = nullptr;
static SceUID g_main_thread = -1;
static bool g_in_low_memory = false;

void plat::set_low_memory_handler(void (*fn)()) {
    g_low_memory = fn;
    g_main_thread = sceKernelGetThreadId();
}

constexpr size_t LARGE_BLOCK = 64 * 1024;

static void* checked_alloc(size_t size) {
    if (size == 0) size = 1;
    void* p = size >= LARGE_BLOCK ? gfx::arena_try_alloc(size) : nullptr;
    if (!p) p = std::malloc(size);
    if (!p && g_low_memory && !g_in_low_memory && sceKernelGetThreadId() == g_main_thread) {
        g_in_low_memory = true;
        g_low_memory();
        g_in_low_memory = false;
        p = size >= LARGE_BLOCK ? gfx::arena_try_alloc(size) : nullptr;
        if (!p) p = std::malloc(size);
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
