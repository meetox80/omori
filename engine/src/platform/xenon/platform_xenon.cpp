#include "../audio_out.h"
#include "../gfx.h"
#include "../platform.h"
#include "../common/testing.h"
#include "../../core/fs.h"

#include <diskio/ata.h>
#include <errno.h>
#include <input/input.h>
#include <iso9660/iso9660.h>
#include <libfat/fat.h>
#include <malloc.h>
#include <reent.h>
#include <setjmp.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <usb/usbmain.h>
#include <xenon_smc/xenon_smc.h>
#include <xenon_soc/xenon_power.h>
#include <xenon_sound/sound.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "io_xenon.h"

extern "C" {
extern DISC_INTERFACE xenon_atapi_ops;
extern unsigned char heap_begin;
extern unsigned char* heap_ptr;
}

namespace plat {

constexpr uint64_t TIMEBASE = 3192000000ull / 64ull;
constexpr uintptr_t HEAP_END = 0x9DA00000u;
constexpr size_t THREAD_STACK = 512 * 1024;
constexpr int THREAD_ORDER[] = {2, 4, 3, 5, 1};
constexpr int MOUNT_WAIT_MS = 1500;
constexpr int MOUNT_RETRY_MS = 6000;
constexpr int STICK_DEAD = 16384;
static const char* const FAT_DEVICES[] = {"uda", "udb", "udc", "sda"};

static Options g_opt;
static volatile bool g_running = true;
static uint32_t g_buttons = 0;
static uint64_t g_frame = 0;
static FILE* g_log = nullptr;
static uint64_t g_start = 0;
static uint64_t g_last_tick = 0;
static controller_data_s g_pad[4];

static bool g_thread_used[6] = {};
static ThreadFn g_thread_fn[6] = {};
static jmp_buf g_thread_exit[6];
static volatile bool g_quit = false;

static uint64_t ticks() {
    uint32_t hi, lo, hi2;
    do {
        asm volatile("mftbu %0" : "=r"(hi));
        asm volatile("mftbl %0" : "=r"(lo));
        asm volatile("mftbu %0" : "=r"(hi2));
    } while (hi != hi2);
    return ((uint64_t)hi << 32) | lo;
}

static uint64_t ticks_to_us(uint64_t t) { return (t / TIMEBASE) * 1000000ull + (t % TIMEBASE) * 1000000ull / TIMEBASE; }

static void* limited_sbrk(struct _reent* r, ptrdiff_t incr) {
    if (!heap_ptr) heap_ptr = &heap_begin;
    unsigned char* res = heap_ptr;
    if (incr > 0 && (ptrdiff_t)(HEAP_END - (uintptr_t)res) < incr) {
        if (r) r->_errno = ENOMEM;
        return (void*)-1;
    }
    heap_ptr += incr;
    return res;
}

size_t heap_limit() { return HEAP_END - (uintptr_t)&heap_begin; }

static bool exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

static void poll_usb_for(int ms) {
    uint64_t end = ticks() + (uint64_t)ms * (TIMEBASE / 1000);
    while (ticks() < end) {
        usb_do_poll();
        sleep_ms(5);
    }
}

static std::string find_base(std::string& writable) {
    std::string found;
    uint64_t deadline = ticks() + (uint64_t)MOUNT_RETRY_MS * (TIMEBASE / 1000);
    for (;;) {
        fatInitDefault();
        writable.clear();
        for (const char* d : FAT_DEVICES) {
            if (FindDevice((std::string(d) + ":").c_str()) < 0) continue;
            if (writable.empty()) writable = std::string(d) + ":/omori";
            std::string base = std::string(d) + ":/omori";
            if (found.empty() && exists(base + "/omori.pak")) {
                found = base;
                writable = base;
            }
        }
        if (!found.empty() || ticks() >= deadline) break;
        poll_usb_for(250);
    }
    if (found.empty() && ISO9660_Mount("dvd", &xenon_atapi_ops) && exists("dvd:/omori/omori.pak")) found = "dvd:/omori";
    return found;
}

bool init(int argc, char** argv) {
    (void)argc;
    (void)argv;
    __syscalls.sbrk_r = limited_sbrk;
    g_start = ticks();
    xenon_make_it_faster(XENON_SPEED_FULL);
    std::memset(g_pad, 0, sizeof(g_pad));
    usb_init();
    poll_usb_for(MOUNT_WAIT_MS);
    xenon_ata_init();
    xenon_atapi_init();
    std::string writable;
    std::string base = find_base(writable);
    if (base.empty()) base = writable.empty() ? std::string("uda:/omori") : writable;
    if (writable.empty()) writable = base;
    mkdir(writable.c_str(), 0777);
    g_opt.data_root = base + "/omori.pak";
    g_opt.save_root = writable + "/saves";
    make_dir(g_opt.save_root);
    g_log = std::fopen((writable + "/log.txt").c_str(), "w");
    g_opt.shot_prefix = writable + "/shot";
    std::vector<std::string> args = testing::read_args_file(base + "/args.txt");
    for (auto& a : args) {
        if (!a.empty() && a[0] != '-' && a.find('/') == std::string::npos && a.find('.') != std::string::npos) a = writable + "/" + a;
    }
    testing::parse_args(g_opt, args);
    testing::setup(g_opt);
    log("data %s, saves %s", g_opt.data_root.c_str(), g_opt.save_root.c_str());
    fs::init_data(g_opt.data_root);
    gfx::init();
    return true;
}

static void retire_threads() {
    g_quit = true;
    uint64_t end = ticks() + 3 * TIMEBASE;
    for (int t = 1; t < 6; t++) {
        while (g_thread_used[t] && xenon_is_thread_task_running(t) && ticks() < end) {
            asm volatile("or 1,1,1");
            asm volatile("or 2,2,2");
        }
    }
}

static bool threads_idle() {
    for (int t = 1; t < 6; t++) {
        if (g_thread_used[t] && xenon_is_thread_task_running(t)) return false;
    }
    return true;
}

void shutdown() {
    audio_out::stop();
    gfx::shutdown();
    retire_threads();
    if (g_log) std::fclose(g_log);
    g_log = nullptr;
    if (!threads_idle()) xenon_smc_power_reboot();
    std::exit(0);
}

bool running() { return g_running; }
void request_exit() { g_running = false; }

static uint32_t stick(int x, int y) {
    if (x * x + y * y < STICK_DEAD * STICK_DEAD) return 0;
    uint32_t b = 0;
    if (std::abs(x) * 5 > std::abs(y) * 2) b |= x < 0 ? BTN_LEFT : BTN_RIGHT;
    if (std::abs(y) * 5 > std::abs(x) * 2) b |= y > 0 ? BTN_UP : BTN_DOWN;
    return b;
}

void poll() {
    uint32_t b = 0;
    {
        xenon_io::Guard g;
        usb_do_poll();
        for (int i = 0; i < 4; i++) {
            controller_data_s d;
            if (get_controller_data(&d, i)) g_pad[i] = d;
        }
    }
    for (const controller_data_s& p : g_pad) {
        uint32_t dir = 0;
        if (p.up) dir |= BTN_UP;
        if (p.down) dir |= BTN_DOWN;
        if (p.left) dir |= BTN_LEFT;
        if (p.right) dir |= BTN_RIGHT;
        b |= dir ? dir : stick(p.s1_x, p.s1_y);
        if (p.a) b |= BTN_OK;
        if (p.b) b |= BTN_CANCEL;
        if (p.y) b |= BTN_MENU;
        if (p.x) b |= BTN_DASH;
        if (p.lb || p.lt > 128) b |= BTN_PAGEUP;
        if (p.rb || p.rt > 128) b |= BTN_PAGEDOWN;
        if (p.start) b |= BTN_START;
        if (p.back) b |= BTN_SELECT;
        if (p.logo) g_running = false;
    }
    b |= testing::buttons(g_frame);
    g_buttons = b;
}

uint32_t buttons() { return g_buttons; }
uint64_t frame_count() { return g_frame; }

uint64_t time_us() { return ticks_to_us(ticks() - g_start); }

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
    xenon_io::Guard g;
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

static void check_quit() {
    int id = xenon_io::thread_id();
    if (id != 0 && g_quit) longjmp(g_thread_exit[id], 1);
}

struct Mutex {
    alignas(128) volatile unsigned int word = 0;
};

static void thread_entry() {
    int id = xenon_io::thread_id();
    if (setjmp(g_thread_exit[id]) == 0 && g_thread_fn[id]) g_thread_fn[id]();
}

bool start_thread(ThreadFn fn) {
    for (int t : THREAD_ORDER) {
        if (g_thread_used[t] || xenon_is_thread_task_running(t)) continue;
        uint8_t* stack = (uint8_t*)memalign(128, THREAD_STACK);
        if (!stack) return false;
        g_thread_used[t] = true;
        g_thread_fn[t] = fn;
        if (xenon_run_thread_task(t, stack + THREAD_STACK - 256, (void*)thread_entry) != 0) {
            g_thread_used[t] = false;
            std::free(stack);
            continue;
        }
        return true;
    }
    log("no free hardware thread");
    return false;
}

Mutex* mutex_create() { return new Mutex(); }

void mutex_lock(Mutex* m) {
    while (__atomic_exchange_n(&m->word, 1u, __ATOMIC_ACQUIRE)) {
        asm volatile("or 1,1,1");
        while (m->word) check_quit();
        asm volatile("or 2,2,2");
    }
}

void mutex_unlock(Mutex* m) { __atomic_store_n(&m->word, 0u, __ATOMIC_RELEASE); }

void sleep_ms(int ms) {
    check_quit();
    uint64_t end = ticks() + (uint64_t)(ms > 0 ? ms : 0) * (TIMEBASE / 1000);
    asm volatile("or 1,1,1");
    while (ticks() < end) check_quit();
    asm volatile("or 2,2,2");
}

size_t free_memory() {
    size_t used = heap_used();
    size_t limit = heap_limit();
    return limit > used ? limit - used : 0;
}

}

namespace audio_out {

constexpr int OUT_RATE = 48000;
constexpr int CHUNK = 512;
constexpr int QUEUE_BYTES = CHUNK * 4 * 4;

static MixFn g_fn = nullptr;
static plat::Mutex* g_lock = nullptr;
static volatile bool g_run = false;
static volatile bool g_done = true;
static int16_t g_mix[BLOCK * 2];
static int g_mix_pos = BLOCK;
static double g_phase = 0;
static int16_t g_cur[2] = {0, 0};
static int16_t g_nxt[2] = {0, 0};
static bool g_primed = false;
static uint16_t g_out[CHUNK * 2];

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

static inline uint16_t little(int v) {
    uint16_t u = (uint16_t)(int16_t)std::clamp(v, -32768, 32767);
    return (uint16_t)((u >> 8) | (u << 8));
}

static void fill() {
    const double step = (double)RATE / OUT_RATE;
    if (!g_primed) {
        next_frame(g_cur);
        next_frame(g_nxt);
        g_primed = true;
    }
    for (int i = 0; i < CHUNK; i++) {
        while (g_phase >= 1.0) {
            g_cur[0] = g_nxt[0];
            g_cur[1] = g_nxt[1];
            next_frame(g_nxt);
            g_phase -= 1.0;
        }
        float t = (float)g_phase;
        g_out[i * 2] = little((int)((float)g_cur[0] + ((float)g_nxt[0] - (float)g_cur[0]) * t));
        g_out[i * 2 + 1] = little((int)((float)g_cur[1] + ((float)g_nxt[1] - (float)g_cur[1]) * t));
        g_phase += step;
    }
}

static void audio_thread() {
    while (g_run) {
        if (xenon_sound_get_unplayed() >= QUEUE_BYTES) {
            plat::sleep_ms(1);
            continue;
        }
        fill();
        xenon_sound_submit(g_out, (int)sizeof(g_out));
    }
    g_done = true;
}

bool start(MixFn fn) {
    g_fn = fn;
    if (!g_lock) g_lock = plat::mutex_create();
    xenon_sound_init();
    g_run = true;
    g_done = false;
    if (!plat::start_thread(audio_thread)) {
        g_run = false;
        g_done = true;
        return false;
    }
    return true;
}

bool start_feeder(void (*fn)()) { return plat::start_thread(fn); }

void stop() {
    if (!g_run) return;
    g_run = false;
    uint64_t end = plat::time_us() + 500000;
    while (!g_done && plat::time_us() < end) {
    }
}

void lock() {
    if (g_lock) plat::mutex_lock(g_lock);
}

void unlock() {
    if (g_lock) plat::mutex_unlock(g_lock);
}

}

static void (*g_low_memory)() = nullptr;
static bool g_in_low_memory = false;

void plat::set_low_memory_handler(void (*fn)()) { g_low_memory = fn; }

static void* checked_alloc(size_t size) {
    if (size == 0) size = 1;
    void* p = std::malloc(size);
    if (!p && g_low_memory && !g_in_low_memory && xenon_io::thread_id() == 0) {
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
