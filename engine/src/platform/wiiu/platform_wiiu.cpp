#include "../audio_out.h"
#include "../gfx.h"
#include "../platform.h"
#include "../common/testing.h"
#include "../../core/fs.h"

#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/memexpheap.h>
#include <coreinit/memheap.h>
#include <coreinit/memorymap.h>
#include <coreinit/mutex.h>
#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <malloc.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <sndcore2/core.h>
#include <sndcore2/voice.h>
#include <sys/stat.h>
#include <sysapp/launch.h>
#include <vpad/input.h>
#include <whb/gfx.h>
#include <whb/proc.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

static size_t g_sbrk_size = 0;

extern "C" {
void __init_wut_sbrk_heap(MEMHeapHandle heap);
void __init_wut_thread();
void __init_wut_defaultheap();

void __preinit_user(MEMHeapHandle*, MEMHeapHandle*, MEMHeapHandle* mem2) {
    g_sbrk_size = MEMGetAllocatableSizeForExpHeapEx(*mem2, 4);
    __init_wut_sbrk_heap(*mem2);
    __init_wut_thread();
    __init_wut_defaultheap();
}
}

namespace wiiu {

constexpr uint64_t HBL_TITLE_ID = 0x0005000013374842ull;

struct Thread {
    OSThread os;
    void (*fn)();
};

static int thread_entry(int, const char** argv) {
    Thread* t = (Thread*)argv;
    if (t->fn) t->fn();
    return 0;
}

static void thread_free(OSThread* os, void* stack) {
    std::free(stack);
    std::free(os);
}

bool spawn(void (*fn)(), uint32_t stack_size, int priority, OSThreadAttributes cores, const char* name) {
    Thread* t = (Thread*)memalign(16, sizeof(Thread));
    uint8_t* stack = (uint8_t*)memalign(16, stack_size);
    if (!t || !stack) {
        std::free(t);
        std::free(stack);
        return false;
    }
    std::memset(t, 0, sizeof(Thread));
    t->fn = fn;
    if (!OSCreateThread(&t->os, thread_entry, 0, (char*)t, stack + stack_size, stack_size, priority, cores)) {
        std::free(t);
        std::free(stack);
        return false;
    }
    OSSetThreadName(&t->os, name);
    OSSetThreadDeallocator(&t->os, thread_free);
    OSDetachThread(&t->os);
    OSResumeThread(&t->os);
    return true;
}

}

namespace plat {

static const char* BASE = "fs:/vol/external01/wiiu/apps/omori";

static Options g_opt;
static volatile bool g_running = true;
static bool g_from_hbl = false;
static bool g_proc_done = false;
static uint32_t g_buttons = 0;
static uint64_t g_frame = 0;
static FILE* g_log = nullptr;
static size_t g_heap_limit = 0;
static OSTime g_start = 0;
static uint64_t g_last_tick = 0;
static bool g_resumed = false;


size_t heap_limit() { return g_heap_limit; }

bool init(int argc, char** argv) {
    WHBProcInit();
    g_from_hbl = OSGetTitleID() == wiiu::HBL_TITLE_ID;
    g_start = OSGetSystemTime();
    g_heap_limit = g_sbrk_size;
    VPADInit();
    KPADInit();
    WPADEnableURCC(TRUE);
    std::string base = BASE;
    make_dir("fs:/vol/external01/wiiu");
    make_dir("fs:/vol/external01/wiiu/apps");
    make_dir(base);
    g_opt.data_root = base + "/omori.pak";
    g_opt.save_root = base + "/saves";
    make_dir(g_opt.save_root);
    g_log = std::fopen((base + "/log.txt").c_str(), "w");
    g_opt.shot_prefix = base + "/shot";
    std::vector<std::string> args = testing::read_args_file(base + "/args.txt");
    for (auto& a : args) {
        if (!a.empty() && a[0] != '-' && a.find(':') == std::string::npos && a.find('/') == std::string::npos && a.find('.') != std::string::npos) a = base + "/" + a;
    }
    (void)argc;
    (void)argv;
    testing::parse_args(g_opt, args);
    testing::setup(g_opt);
    log("data %s, saves %s, heap %u KB", g_opt.data_root.c_str(), g_opt.save_root.c_str(), (unsigned)(g_heap_limit / 1024));
    fs::init_data(g_opt.data_root);
    gfx::init();
    return true;
}

static void finish_proc() {
    if (g_proc_done) return;
    g_proc_done = true;
    if (g_from_hbl) {
        WHBProcStopRunning();
        WHBProcIsRunning();
        return;
    }
    SYSLaunchMenu();
    while (WHBProcIsRunning()) {
    }
}

void shutdown() {
    audio_out::stop();
    if (!g_proc_done) gfx::shutdown();
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
    finish_proc();
    KPADShutdown();
    WHBGfxShutdown();
    WHBProcShutdown();
    std::exit(0);
}

bool running() { return g_running; }
void request_exit() { g_running = false; }

static uint32_t stick(float x, float y) {
    if (x * x + y * y < 0.25f) return 0;
    uint32_t b = 0;
    if (std::fabs(x) * 5 > std::fabs(y) * 2) b |= x < 0 ? BTN_LEFT : BTN_RIGHT;
    if (std::fabs(y) * 5 > std::fabs(x) * 2) b |= y > 0 ? BTN_UP : BTN_DOWN;
    return b;
}

static uint32_t gamepad() {
    VPADStatus st;
    VPADReadError err = VPAD_READ_SUCCESS;
    if (VPADRead(VPAD_CHAN_0, &st, 1, &err) < 1 || err != VPAD_READ_SUCCESS) return 0;
    uint32_t h = st.hold, b = 0;
    if (h & VPAD_BUTTON_UP) b |= BTN_UP;
    if (h & VPAD_BUTTON_DOWN) b |= BTN_DOWN;
    if (h & VPAD_BUTTON_LEFT) b |= BTN_LEFT;
    if (h & VPAD_BUTTON_RIGHT) b |= BTN_RIGHT;
    if (h & VPAD_BUTTON_A) b |= BTN_OK;
    if (h & VPAD_BUTTON_B) b |= BTN_CANCEL;
    if (h & VPAD_BUTTON_X) b |= BTN_MENU;
    if (h & VPAD_BUTTON_Y) b |= BTN_DASH;
    if (h & (VPAD_BUTTON_L | VPAD_BUTTON_ZL)) b |= BTN_PAGEUP;
    if (h & (VPAD_BUTTON_R | VPAD_BUTTON_ZR)) b |= BTN_PAGEDOWN;
    if (h & VPAD_BUTTON_PLUS) b |= BTN_START;
    if (h & VPAD_BUTTON_MINUS) b |= BTN_SELECT;
    if (!(b & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT))) b |= stick(st.leftStick.x, st.leftStick.y);
    return b;
}

static uint32_t remote(int chan) {
    KPADStatus ks;
    KPADError err = KPAD_ERROR_OK;
    if (KPADReadEx((KPADChan)chan, &ks, 1, &err) < 1 || err != KPAD_ERROR_OK) return 0;
    uint32_t b = 0;
    if (ks.extensionType == WPAD_EXT_PRO_CONTROLLER) {
        uint32_t h = ks.pro.hold;
        if (h & WPAD_PRO_BUTTON_UP) b |= BTN_UP;
        if (h & WPAD_PRO_BUTTON_DOWN) b |= BTN_DOWN;
        if (h & WPAD_PRO_BUTTON_LEFT) b |= BTN_LEFT;
        if (h & WPAD_PRO_BUTTON_RIGHT) b |= BTN_RIGHT;
        if (h & WPAD_PRO_BUTTON_A) b |= BTN_OK;
        if (h & WPAD_PRO_BUTTON_B) b |= BTN_CANCEL;
        if (h & WPAD_PRO_BUTTON_X) b |= BTN_MENU;
        if (h & WPAD_PRO_BUTTON_Y) b |= BTN_DASH;
        if (h & (WPAD_PRO_TRIGGER_L | WPAD_PRO_TRIGGER_ZL)) b |= BTN_PAGEUP;
        if (h & (WPAD_PRO_TRIGGER_R | WPAD_PRO_TRIGGER_ZR)) b |= BTN_PAGEDOWN;
        if (h & WPAD_PRO_BUTTON_PLUS) b |= BTN_START;
        if (h & WPAD_PRO_BUTTON_MINUS) b |= BTN_SELECT;
        if (!(b & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT))) b |= stick(ks.pro.leftStick.x, ks.pro.leftStick.y);
        return b;
    }
    if (ks.extensionType == WPAD_EXT_CLASSIC || ks.extensionType == WPAD_EXT_MPLUS_CLASSIC) {
        uint32_t h = ks.classic.hold;
        if (h & WPAD_CLASSIC_BUTTON_UP) b |= BTN_UP;
        if (h & WPAD_CLASSIC_BUTTON_DOWN) b |= BTN_DOWN;
        if (h & WPAD_CLASSIC_BUTTON_LEFT) b |= BTN_LEFT;
        if (h & WPAD_CLASSIC_BUTTON_RIGHT) b |= BTN_RIGHT;
        if (h & WPAD_CLASSIC_BUTTON_A) b |= BTN_OK;
        if (h & WPAD_CLASSIC_BUTTON_B) b |= BTN_CANCEL;
        if (h & WPAD_CLASSIC_BUTTON_X) b |= BTN_MENU;
        if (h & WPAD_CLASSIC_BUTTON_Y) b |= BTN_DASH;
        if (h & (WPAD_CLASSIC_BUTTON_L | WPAD_CLASSIC_BUTTON_ZL)) b |= BTN_PAGEUP;
        if (h & (WPAD_CLASSIC_BUTTON_R | WPAD_CLASSIC_BUTTON_ZR)) b |= BTN_PAGEDOWN;
        if (h & WPAD_CLASSIC_BUTTON_PLUS) b |= BTN_START;
        if (h & WPAD_CLASSIC_BUTTON_MINUS) b |= BTN_SELECT;
        if (!(b & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT))) b |= stick(ks.classic.leftStick.x, ks.classic.leftStick.y);
        return b;
    }
    uint32_t h = ks.hold;
    if (h & WPAD_BUTTON_RIGHT) b |= BTN_UP;
    if (h & WPAD_BUTTON_LEFT) b |= BTN_DOWN;
    if (h & WPAD_BUTTON_UP) b |= BTN_LEFT;
    if (h & WPAD_BUTTON_DOWN) b |= BTN_RIGHT;
    if (h & WPAD_BUTTON_2) b |= BTN_OK;
    if (h & WPAD_BUTTON_1) b |= BTN_CANCEL;
    if (h & WPAD_BUTTON_A) b |= BTN_MENU;
    if (h & WPAD_BUTTON_B) b |= BTN_DASH;
    if (h & WPAD_BUTTON_PLUS) b |= BTN_START;
    if (h & WPAD_BUTTON_MINUS) b |= BTN_SELECT;
    if ((ks.extensionType == WPAD_EXT_NUNCHUK || ks.extensionType == WPAD_EXT_MPLUS_NUNCHUK) && !(b & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)))
        b |= stick(ks.nunchuk.stick.x, ks.nunchuk.stick.y);
    return b;
}

void poll() {
    OSTime before = OSGetSystemTime();
    if (!WHBProcIsRunning()) {
        g_proc_done = true;
        shutdown();
    }
    if (OSTicksToMilliseconds(OSGetSystemTime() - before) > 500) g_resumed = true;
    uint32_t b = gamepad();
    for (int i = 0; i < 4; i++) b |= remote(i);
    b |= testing::buttons(g_frame);
    g_buttons = b;
}

uint32_t buttons() { return g_buttons; }
uint64_t frame_count() { return g_frame; }

uint64_t time_us() { return (uint64_t)OSTicksToMicroseconds(OSGetSystemTime() - g_start); }

double time_seconds() { return (double)time_us() / 1000000.0; }

size_t heap_used() {
    struct mallinfo mi = mallinfo();
    return (size_t)mi.uordblks;
}

static void write_shot() {
    std::vector<uint32_t> px((size_t)gfx::SCREEN_W * gfx::SCREEN_H);
    if (!gfx::capture(px.data(), gfx::SCREEN_W)) return;
    testing::write_bmp(testing::shot_path(g_opt, g_frame, "bmp"), px.data(), gfx::SCREEN_W, gfx::SCREEN_H, gfx::SCREEN_W);
}

static bool scripted() { return g_opt.max_frames > 0 || !g_opt.input_script.empty() || !g_opt.shot_frames.empty(); }

int pending_ticks() {
    uint64_t tick = time_us() * 60 / 1000000;
    uint64_t last = g_last_tick;
    g_last_tick = tick;
    if (last == 0 || scripted()) return 1;
    return std::clamp((int)(tick - last), 1, 4);
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
    char line[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    OSReport("omori: %s\n", line);
    if (!g_log) return;
    std::fputs(line, g_log);
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
    OSMutex m;
};

bool start_thread(ThreadFn fn) {
    return wiiu::spawn(fn, 256 * 1024, 20, (OSThreadAttributes)(OS_THREAD_ATTRIB_AFFINITY_CPU0 | OS_THREAD_ATTRIB_AFFINITY_CPU2), "omori worker");
}

Mutex* mutex_create() {
    Mutex* m = new Mutex();
    OSInitMutex(&m->m);
    return m;
}

void mutex_lock(Mutex* m) { OSLockMutex(&m->m); }
void mutex_unlock(Mutex* m) { OSUnlockMutex(&m->m); }
void sleep_ms(int ms) { OSSleepTicks(OSMillisecondsToTicks(ms)); }

size_t free_memory() {
    size_t used = heap_used();
    return g_heap_limit > used ? g_heap_limit - used : 0;
}

}

namespace audio_out {

constexpr uint32_t RING = 16384;
constexpr uint32_t LEAD = 3 * BLOCK;

static MixFn g_fn = nullptr;
static OSMutex g_lock;
static bool g_lock_ready = false;
static volatile bool g_run = false;
static volatile bool g_done = true;
static int16_t* g_ring = nullptr;
static AXVoice* g_voice[2] = {nullptr, nullptr};
static uint32_t g_write = 0;
static int16_t g_mix[BLOCK * 2];

static int16_t* dsp_buffer(size_t bytes) {
    std::vector<void*> rejected;
    int16_t* found = nullptr;
    for (int i = 0; i < 32 && !found; i++) {
        void* p = memalign(64, bytes);
        if (!p) break;
        uint32_t start = OSEffectiveToPhysical((uint32_t)(uintptr_t)p) & 0x1FFFFFFFu;
        if (((start + (uint32_t)bytes) & 0xE0000000u) == 0) found = (int16_t*)p;
        else rejected.push_back(p);
    }
    for (void* p : rejected) std::free(p);
    return found;
}

static void store(uint32_t at, uint32_t n) {
    for (int c = 0; c < 2; c++) {
        int16_t* ch = g_ring + (size_t)c * RING;
        uint32_t first = std::min(n, RING - at);
        DCFlushRange(ch + at, first * 2);
        if (first < n) DCFlushRange(ch, (n - first) * 2);
    }
}

static void feed() {
    AXVoiceOffsets o;
    while (g_run) {
        AXGetVoiceOffsets(g_voice[0], &o);
        uint32_t play = o.currentOffset & (RING - 1);
        uint32_t ahead = (g_write - play) & (RING - 1);
        if (ahead > RING / 2) {
            g_write = (play + BLOCK / 4) & (RING - 1);
            ahead = BLOCK / 4;
        }
        if (ahead >= LEAD) {
            OSSleepTicks(OSMillisecondsToTicks(2));
            continue;
        }
        lock();
        if (g_fn) g_fn(g_mix, BLOCK);
        else std::memset(g_mix, 0, sizeof(g_mix));
        unlock();
        int16_t* l = g_ring;
        int16_t* r = g_ring + RING;
        for (int i = 0; i < BLOCK; i++) {
            uint32_t at = (g_write + (uint32_t)i) & (RING - 1);
            l[at] = g_mix[i * 2];
            r[at] = g_mix[i * 2 + 1];
        }
        store(g_write, BLOCK);
        g_write = (g_write + BLOCK) & (RING - 1);
    }
    g_done = true;
}

bool start(MixFn fn) {
    g_fn = fn;
    OSInitMutex(&g_lock);
    g_lock_ready = true;
    g_ring = dsp_buffer((size_t)RING * 2 * sizeof(int16_t));
    if (!g_ring) {
        plat::log("audio: no DSP-reachable memory for the output ring");
        return false;
    }
    std::memset(g_ring, 0, (size_t)RING * 2 * sizeof(int16_t));
    DCFlushRange(g_ring, (size_t)RING * 2 * sizeof(int16_t));
    if (!AXIsInit()) {
        AXInitParams params = {};
        params.renderer = AX_INIT_RENDERER_48KHZ;
        params.pipeline = AX_INIT_PIPELINE_SINGLE;
        AXInitWithParams(&params);
    }
    for (int c = 0; c < 2; c++) {
        g_voice[c] = AXAcquireVoice(31, nullptr, nullptr);
        if (!g_voice[c]) {
            plat::log("audio: no free AX voice");
            return false;
        }
    }
    AXVoiceBegin(g_voice[0]);
    AXVoiceBegin(g_voice[1]);
    for (int c = 0; c < 2; c++) {
        AXVoice* v = g_voice[c];
        AXSetVoiceType(v, 0);
        AXVoiceVeData ve = {0x8000, 0};
        AXSetVoiceVe(v, &ve);
        AXVoiceDeviceMixData mix[6];
        std::memset(mix, 0, sizeof(mix));
        mix[c].bus[0].volume = 0x8000;
        AXSetVoiceDeviceMix(v, AX_DEVICE_TYPE_TV, 0, mix);
        AXSetVoiceDeviceMix(v, AX_DEVICE_TYPE_DRC, 0, mix);
        AXSetVoiceSrcRatio(v, (float)RATE / (float)AXGetInputSamplesPerSec());
        AXSetVoiceSrcType(v, AX_VOICE_SRC_TYPE_LINEAR);
        AXVoiceOffsets o = {};
        o.dataType = AX_VOICE_FORMAT_LPCM16;
        o.loopingEnabled = AX_VOICE_LOOP_ENABLED;
        o.loopOffset = 0;
        o.endOffset = RING - 1;
        o.currentOffset = 0;
        o.data = g_ring + (size_t)c * RING;
        AXSetVoiceOffsets(v, &o);
    }
    AXSetVoiceState(g_voice[0], AX_VOICE_STATE_PLAYING);
    AXSetVoiceState(g_voice[1], AX_VOICE_STATE_PLAYING);
    AXVoiceEnd(g_voice[1]);
    AXVoiceEnd(g_voice[0]);
    g_write = 0;
    g_run = true;
    g_done = false;
    if (!wiiu::spawn(feed, 64 * 1024, 10, OS_THREAD_ATTRIB_AFFINITY_CPU0, "omori audio")) {
        g_run = false;
        g_done = true;
        return false;
    }
    return true;
}

bool start_feeder(void (*fn)()) { return plat::start_thread(fn); }

void stop() {
    if (g_run) {
        g_run = false;
        for (int i = 0; i < 200 && !g_done; i++) OSSleepTicks(OSMillisecondsToTicks(1));
    }
    for (AXVoice*& v : g_voice) {
        if (!v) continue;
        AXSetVoiceState(v, AX_VOICE_STATE_STOPPED);
        AXFreeVoice(v);
        v = nullptr;
    }
    if (AXIsInit()) AXQuit();
}

void lock() {
    if (g_lock_ready) OSLockMutex(&g_lock);
}

void unlock() {
    if (g_lock_ready) OSUnlockMutex(&g_lock);
}

}

static void (*g_low_memory)() = nullptr;
static OSThread* g_main_thread = nullptr;
static bool g_in_low_memory = false;

void plat::set_low_memory_handler(void (*fn)()) {
    g_low_memory = fn;
    g_main_thread = OSGetCurrentThread();
}

static void* checked_alloc(size_t size) {
    if (size == 0) size = 1;
    void* p = std::malloc(size);
    if (!p && g_low_memory && !g_in_low_memory && OSGetCurrentThread() == g_main_thread) {
        g_in_low_memory = true;
        g_low_memory();
        g_in_low_memory = false;
        p = std::malloc(size);
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
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }
