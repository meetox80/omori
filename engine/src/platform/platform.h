#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

enum Button : uint32_t {
    BTN_UP = 1u << 0,
    BTN_DOWN = 1u << 1,
    BTN_LEFT = 1u << 2,
    BTN_RIGHT = 1u << 3,
    BTN_OK = 1u << 4,
    BTN_CANCEL = 1u << 5,
    BTN_MENU = 1u << 6,
    BTN_DASH = 1u << 7,
    BTN_PAGEUP = 1u << 8,
    BTN_PAGEDOWN = 1u << 9,
    BTN_START = 1u << 10,
    BTN_SELECT = 1u << 11,
};

namespace plat {

struct Options {
    std::string data_root;
    std::string save_root;
    int scale = 2;
    bool headless = false;
    bool fast = false;
    int max_frames = 0;
    std::string input_script;
    std::string shot_prefix;
    std::string shot_frames;
    int skip_render_until = 0;
    bool perf = false;
    int dump_audio = 0;
    std::string debug_tint;
    std::string debug_start;
    int ballast_mb = 0;
};

bool init(int argc, char** argv);
int pending_ticks();
void shutdown();
bool running();
void request_exit();
void poll();
uint32_t buttons();
bool touch(int& x, int& y);
uint64_t frame_count();
double time_seconds();
uint64_t time_us();
size_t heap_used();
size_t heap_limit();
inline bool heap_tight(size_t margin) {
    size_t limit = heap_limit();
    return limit && heap_used() + margin > limit;
}
void set_low_memory_handler(void (*fn)());
void frame_end();
const Options& options();
const std::string& data_root();
const std::string& save_root();
bool make_dir(const std::string& path);
void log(const char* fmt, ...);
size_t free_memory();
bool consume_resume();
bool debug_warp(int& map, int& x, int& y, int& d);
bool debug_battle(int& troop);
bool debug_eval(std::string& src);
bool debug_reset();

struct File;
File* file_open(const std::string& path);
size_t file_read(File* f, void* dst, size_t size);
bool file_seek(File* f, int64_t pos);
int64_t file_size(File* f);
void file_close(File* f);

struct Mutex;
using ThreadFn = void (*)();
bool start_thread(ThreadFn fn);
Mutex* mutex_create();
void mutex_lock(Mutex* m);
void mutex_unlock(Mutex* m);
void sleep_ms(int ms);

}
