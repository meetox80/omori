#include "testing.h"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>

namespace testing {

struct ScriptEvent {
    uint64_t frame;
    uint32_t mask;
    int duration;
};

static std::vector<ScriptEvent> g_script;
static std::set<uint64_t> g_shots;
static uint64_t g_exit_at = UINT64_MAX;

struct Warp {
    uint64_t frame;
    int map, x, y, d;
};

static std::vector<Warp> g_warps;
static std::vector<std::pair<uint64_t, int>> g_battles;
static std::set<uint64_t> g_resets;
static std::vector<std::pair<uint64_t, std::string>> g_evals;

static uint32_t parse_buttons(const std::string& s) {
    uint32_t m = 0;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, '+')) {
        if (tok == "up") m |= BTN_UP;
        else if (tok == "down") m |= BTN_DOWN;
        else if (tok == "left") m |= BTN_LEFT;
        else if (tok == "right") m |= BTN_RIGHT;
        else if (tok == "ok") m |= BTN_OK;
        else if (tok == "cancel") m |= BTN_CANCEL;
        else if (tok == "menu") m |= BTN_MENU;
        else if (tok == "dash") m |= BTN_DASH;
        else if (tok == "pageup") m |= BTN_PAGEUP;
        else if (tok == "pagedown") m |= BTN_PAGEDOWN;
        else if (tok == "start") m |= BTN_START;
        else if (tok == "select") m |= BTN_SELECT;
    }
    return m;
}

void parse_args(plat::Options& opt, const std::vector<std::string>& args) {
    for (size_t i = 0; i < args.size(); i++) {
        const std::string& a = args[i];
        auto next = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : ""; };
        if (a == "--data") opt.data_root = next();
        else if (a == "--saves") opt.save_root = next();
        else if (a == "--scale") opt.scale = std::atoi(next().c_str());
        else if (a == "--headless") opt.headless = true;
        else if (a == "--fast") opt.fast = true;
        else if (a == "--frames") opt.max_frames = std::atoi(next().c_str());
        else if (a == "--input") opt.input_script = next();
        else if (a == "--shot-prefix") opt.shot_prefix = next();
        else if (a == "--shots") opt.shot_frames = next();
        else if (a == "--skip-render") opt.skip_render_until = std::atoi(next().c_str());
        else if (a == "--perf") opt.perf = true;
        else if (a == "--dump-audio") opt.dump_audio = std::atoi(next().c_str());
        else if (a == "--tint") opt.debug_tint = next();
        else if (a == "--start") opt.debug_start = next();
        else if (a == "--ballast") opt.ballast_mb = std::atoi(next().c_str());
    }
}

std::vector<std::string> read_args_file(const std::string& path) {
    std::vector<std::string> out;
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return out;
    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        std::stringstream ss(line);
        std::string tok;
        while (ss >> tok) out.push_back(tok);
    }
    std::fclose(f);
    return out;
}

static void load_script(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return;
    char buf[512];
    uint64_t cursor = 0;
    while (std::fgets(buf, sizeof(buf), f)) {
        std::stringstream ss(buf);
        std::string a, b;
        int dur = 0;
        ss >> a >> b;
        if (a.empty() || a[0] == '#') continue;
        if (a == "wait") {
            cursor += std::strtoull(b.c_str(), nullptr, 10);
        } else if (a == "at") {
            cursor = std::strtoull(b.c_str(), nullptr, 10);
        } else if (a == "shot") {
            g_shots.insert(cursor);
        } else if (a == "exit") {
            g_exit_at = cursor;
        } else if (a == "warp") {
            Warp w{cursor, std::atoi(b.c_str()), 0, 0, 2};
            ss >> w.x >> w.y >> w.d;
            g_warps.push_back(w);
        } else if (a == "reset") {
            g_resets.insert(cursor);
        } else if (a == "battle") {
            g_battles.push_back({cursor, std::atoi(b.c_str())});
        } else if (a == "eval") {
            std::string line = buf;
            size_t at = line.find("eval");
            std::string src = line.substr(at + 4);
            while (!src.empty() && (src.back() == '\n' || src.back() == '\r')) src.pop_back();
            g_evals.push_back({cursor, src});
        } else if (a == "press" || a == "hold") {
            ss >> dur;
            if (dur <= 0) dur = (a == "press") ? 2 : 30;
            g_script.push_back({cursor, parse_buttons(b), dur});
            cursor += dur + (a == "press" ? 6 : 0);
        } else if (a == "repeat") {
            int count = 0, gap = 8;
            std::string btn = b;
            ss >> count >> gap;
            for (int k = 0; k < count; k++) {
                g_script.push_back({cursor, parse_buttons(btn), 2});
                cursor += 2 + gap;
            }
        }
    }
    std::fclose(f);
}

void setup(const plat::Options& opt) {
    if (!opt.input_script.empty()) load_script(opt.input_script);
    std::stringstream ss(opt.shot_frames);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (!tok.empty()) g_shots.insert(std::strtoull(tok.c_str(), nullptr, 10));
    }
}

uint32_t buttons(uint64_t frame) {
    uint32_t b = 0;
    for (const ScriptEvent& ev : g_script) {
        if (frame >= ev.frame && frame < ev.frame + (uint64_t)ev.duration) b |= ev.mask;
    }
    return b;
}

bool exit_due(uint64_t frame) { return frame > g_exit_at; }

bool warp_due(uint64_t frame, int& map, int& x, int& y, int& d) {
    for (const Warp& w : g_warps) {
        if (w.frame == frame) {
            map = w.map;
            x = w.x;
            y = w.y;
            d = w.d;
            return true;
        }
    }
    return false;
}
bool battle_due(uint64_t frame, int& troop) {
    for (const auto& b : g_battles) {
        if (b.first == frame) {
            troop = b.second;
            return true;
        }
    }
    return false;
}

bool eval_due(uint64_t frame, std::string& src) {
    std::string out;
    for (const auto& e : g_evals) {
        if (e.first == frame) out += e.second + ";\n";
    }
    if (out.empty()) return false;
    src = out;
    return true;
}

bool shot_due(uint64_t frame) { return g_shots.count(frame) != 0; }

bool reset_due(uint64_t frame) { return g_resets.count(frame) != 0; }

std::string shot_path(const plat::Options& opt, uint64_t frame, const char* ext) {
    char name[64];
    std::snprintf(name, sizeof(name), "_%06llu.%s", (unsigned long long)frame, ext);
    return opt.shot_prefix + name;
}

bool write_bmp(const std::string& path, const uint32_t* px, int w, int h, int stride) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    int row = (w * 3 + 3) & ~3;
    uint32_t size = 54 + (uint32_t)row * h;
    unsigned char hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, uint32_t v) {
        hdr[off] = v & 255;
        hdr[off + 1] = (v >> 8) & 255;
        hdr[off + 2] = (v >> 16) & 255;
        hdr[off + 3] = (v >> 24) & 255;
    };
    put32(2, size);
    put32(10, 54);
    put32(14, 40);
    put32(18, (uint32_t)w);
    put32(22, (uint32_t)h);
    hdr[26] = 1;
    hdr[28] = 24;
    put32(34, (uint32_t)row * h);
    std::fwrite(hdr, 1, 54, f);
    std::vector<unsigned char> line(row, 0);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint32_t p = px[(size_t)y * stride + x];
            line[x * 3] = (p >> 16) & 255;
            line[x * 3 + 1] = (p >> 8) & 255;
            line[x * 3 + 2] = p & 255;
        }
        std::fwrite(line.data(), 1, row, f);
    }
    std::fclose(f);
    return true;
}

}
