#include "../audio_out.h"
#include "../gfx.h"
#include "../platform.h"
#include "../common/testing.h"
#include "../../core/fs.h"

#include <audio/audio.h>
#include <io/pad.h>
#include <lv2/systime.h>
#include <malloc.h>
#include <sys/mutex.h>
#include <sys/process.h>
#include <sys/stat.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include <sysutil/sysutil.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

SYS_PROCESS_PARAM(1001, 0x100000);

namespace plat {

static Options g_opt;
static volatile bool g_running = true;
static uint32_t g_buttons = 0;
static uint64_t g_frame = 0;
static FILE* g_log = nullptr;
static size_t g_heap_limit = 0;
static uint64_t g_start = 0;
static uint64_t g_last_tick = 0;
static padData g_pad[7];

static void sysutil_callback(u64 status, u64, void*) {
    if (status == SYSUTIL_EXIT_GAME) g_running = false;
}

static void measure_heap() {
    std::vector<void*> blocks;
    blocks.reserve(256);
    while (blocks.size() < 256) {
        void* b = std::malloc(1u << 20);
        if (!b) break;
        blocks.push_back(b);
    }
    g_heap_limit = blocks.size() << 20;
    for (void* b : blocks) std::free(b);
}

size_t heap_limit() { return g_heap_limit; }

bool init(int argc, char** argv) {
    g_start = (uint64_t)sysGetSystemTime();
    sysUtilRegisterCallback(0, sysutil_callback, nullptr);
    ioPadInit(7);
    std::memset(g_pad, 0, sizeof(g_pad));
    std::string base = "/dev_hdd0/game/OMRI00001/USRDIR";
    if (argc > 0 && argv && argv[0]) {
        std::string a = argv[0];
        size_t slash = a.find_last_of('/');
        if (slash != std::string::npos) base = a.substr(0, slash);
    }
    g_opt.data_root = base + "/omori.pak";
    g_opt.save_root = base + "/saves";
    make_dir(g_opt.save_root);
    g_log = std::fopen((base + "/log.txt").c_str(), "w");
    g_opt.shot_prefix = base + "/shot";
    std::vector<std::string> args = testing::read_args_file(base + "/args.txt");
    for (auto& a : args) {
        if (!a.empty() && a[0] != '-' && a.find('/') == std::string::npos && a.find('.') != std::string::npos) a = base + "/" + a;
    }
    testing::parse_args(g_opt, args);
    testing::setup(g_opt);
    fs::init_data(g_opt.data_root);
    gfx::init();
    measure_heap();
    return true;
}

void shutdown() {
    audio_out::stop();
    gfx::shutdown();
    if (g_log) std::fclose(g_log);
    ioPadEnd();
    sysUtilUnregisterCallback(0);
    std::exit(0);
}

bool running() { return g_running; }
void request_exit() { g_running = false; }

static uint32_t stick(int x, int y) {
    int dx = x - 128, dy = y - 128;
    if (dx * dx + dy * dy < 64 * 64) return 0;
    uint32_t b = 0;
    if (std::abs(dx) * 5 > std::abs(dy) * 2) b |= dx < 0 ? BTN_LEFT : BTN_RIGHT;
    if (std::abs(dy) * 5 > std::abs(dx) * 2) b |= dy < 0 ? BTN_UP : BTN_DOWN;
    return b;
}

void poll() {
    sysUtilCheckCallback();
    padInfo info;
    ioPadGetInfo(&info);
    uint32_t b = 0;
    for (int i = 0; i < 7; i++) {
        if (!info.status[i]) {
            std::memset(&g_pad[i], 0, sizeof(g_pad[i]));
            continue;
        }
        padData d;
        if (ioPadGetData((u32)i, &d) == 0 && d.len > 0) g_pad[i] = d;
        const padData& p = g_pad[i];
        if (p.len <= 0) continue;
        uint32_t dir = 0;
        if (p.BTN_UP) dir |= BTN_UP;
        if (p.BTN_DOWN) dir |= BTN_DOWN;
        if (p.BTN_LEFT) dir |= BTN_LEFT;
        if (p.BTN_RIGHT) dir |= BTN_RIGHT;
        b |= dir ? dir : stick((int)p.ANA_L_H, (int)p.ANA_L_V);
        if (p.BTN_CROSS) b |= BTN_OK;
        if (p.BTN_CIRCLE) b |= BTN_CANCEL;
        if (p.BTN_TRIANGLE) b |= BTN_MENU;
        if (p.BTN_SQUARE) b |= BTN_DASH;
        if (p.BTN_L1 || p.BTN_L2) b |= BTN_PAGEUP;
        if (p.BTN_R1 || p.BTN_R2) b |= BTN_PAGEDOWN;
        if (p.BTN_START) b |= BTN_START;
        if (p.BTN_SELECT) b |= BTN_SELECT;
    }
    b |= testing::buttons(g_frame);
    g_buttons = b;
}

uint32_t buttons() { return g_buttons; }
uint64_t frame_count() { return g_frame; }

uint64_t time_us() { return (uint64_t)sysGetSystemTime() - g_start; }

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

bool consume_resume() { return false; }

struct Mutex {
    sys_mutex_t m;
};

static void thread_entry(void* arg) {
    ThreadFn fn = (ThreadFn)arg;
    if (fn) fn();
    sysThreadExit(0);
}

bool start_thread(ThreadFn fn) {
    sys_ppu_thread_t id;
    char name[] = "worker";
    return sysThreadCreate(&id, thread_entry, (void*)fn, 1500, 256 * 1024, 0, name) == 0;
}

Mutex* mutex_create() {
    Mutex* m = new Mutex();
    sys_mutex_attr_t attr;
    std::memset(&attr, 0, sizeof(attr));
    sysMutexAttrInitialize(attr);
    sysMutexCreate(&m->m, &attr);
    return m;
}

void mutex_lock(Mutex* m) { sysMutexLock(m->m, 0); }
void mutex_unlock(Mutex* m) { sysMutexUnlock(m->m); }
void sleep_ms(int ms) { sysUsleep((u32)ms * 1000); }

size_t free_memory() {
    struct mallinfo mi = mallinfo();
    size_t used = (size_t)mi.uordblks;
    return g_heap_limit > used ? g_heap_limit - used : 0;
}

}

namespace audio_out {

constexpr int OUT_RATE = 48000;

static MixFn g_fn = nullptr;
static u32 g_port = 0;
static audioPortConfig g_config;
static sys_ppu_thread_t g_thread;
static sys_mutex_t g_lock;
static bool g_has_lock = false;
static volatile bool g_run = false;
static int16_t g_mix[BLOCK * 2];
static int g_mix_pos = BLOCK;
static double g_phase = 0;
static int16_t g_cur[2] = {0, 0};
static int16_t g_nxt[2] = {0, 0};
static bool g_primed = false;

static void next_frame(int16_t* out) {
    if (g_mix_pos >= BLOCK) {
        lock();
        if (g_fn) g_fn(g_mix, BLOCK);
        else std::memset(g_mix, 0, sizeof(g_mix));
        unlock();
        g_mix_pos = 0;
    }
    out[0] = g_mix[g_mix_pos * 2];
    out[1] = g_mix[g_mix_pos * 2 + 1];
    g_mix_pos++;
}

static void fill(float* dst) {
    const double step = (double)RATE / OUT_RATE;
    if (!g_primed) {
        next_frame(g_cur);
        next_frame(g_nxt);
        g_primed = true;
    }
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
        while (g_phase >= 1.0) {
            g_cur[0] = g_nxt[0];
            g_cur[1] = g_nxt[1];
            next_frame(g_nxt);
            g_phase -= 1.0;
        }
        float t = (float)g_phase;
        dst[i * 2] = ((float)g_cur[0] + ((float)g_nxt[0] - (float)g_cur[0]) * t) / 32768.0f;
        dst[i * 2 + 1] = ((float)g_cur[1] + ((float)g_nxt[1] - (float)g_cur[1]) * t) / 32768.0f;
        g_phase += step;
    }
}

static void audio_thread(void*) {
    u64* read_index = (u64*)(u64)g_config.readIndex;
    float* start = (float*)(u64)g_config.audioDataStart;
    u32 blocks = (u32)g_config.numBlocks;
    u32 write = 1;
    while (g_run) {
        u32 current = (u32)*read_index;
        if (write == current) {
            sysUsleep(1000);
            continue;
        }
        fill(start + (size_t)g_config.channelCount * AUDIO_BLOCK_SAMPLES * write);
        write = (write + 1) % blocks;
    }
    sysThreadExit(0);
}

bool start(MixFn fn) {
    g_fn = fn;
    sys_mutex_attr_t attr;
    std::memset(&attr, 0, sizeof(attr));
    sysMutexAttrInitialize(attr);
    g_has_lock = sysMutexCreate(&g_lock, &attr) == 0;
    if (audioInit() != 0) return false;
    audioPortParam params;
    std::memset(&params, 0, sizeof(params));
    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks = AUDIO_BLOCK_8;
    params.attrib = 0;
    params.level = 1.0f;
    if (audioPortOpen(&params, &g_port) != 0 || audioGetPortConfig(g_port, &g_config) != 0) return false;
    std::memset((void*)(u64)g_config.audioDataStart, 0, g_config.portSize);
    audioPortStart(g_port);
    g_run = true;
    char name[] = "audio";
    if (sysThreadCreate(&g_thread, audio_thread, nullptr, 900, 64 * 1024, THREAD_JOINABLE, name) != 0) {
        g_run = false;
        return false;
    }
    return true;
}

void stop() {
    if (!g_run) return;
    g_run = false;
    u64 ret;
    sysThreadJoin(g_thread, &ret);
    audioPortStop(g_port);
    audioPortClose(g_port);
    audioQuit();
}

void lock() {
    if (g_has_lock) sysMutexLock(g_lock, 0);
}

void unlock() {
    if (g_has_lock) sysMutexUnlock(g_lock);
}

}

static void (*g_low_memory)() = nullptr;
static sys_ppu_thread_t g_main_thread = 0;
static bool g_in_low_memory = false;

static sys_ppu_thread_t self_id() {
    sys_ppu_thread_t id = 0;
    sysThreadGetId(&id);
    return id;
}

void plat::set_low_memory_handler(void (*fn)()) {
    g_low_memory = fn;
    g_main_thread = self_id();
}

static void* checked_alloc(size_t size) {
    if (size == 0) size = 1;
    void* p = std::malloc(size);
    if (!p && g_low_memory && !g_in_low_memory && self_id() == g_main_thread) {
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
