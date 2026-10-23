#include "loader.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../platform/gfx.h"
#include "../platform/platform.h"
#include "image.h"

namespace loader {

static const char* const FOLDER = "img/system";
static const char* const NAME = "Loading_2";
constexpr int AREA_X = 576, AREA_Y = 416, AREA = 64;
constexpr uint64_t DELAY_US = 1000000 / 3;
constexpr uint64_t FADE_US = 500000;
constexpr uint64_t STEP_US = 125000;
constexpr double PERIOD = 2.0;

static gfx::Texture* g_tex = nullptr;
static int g_tw = 0, g_th = 0;
static bool g_tried = false;
static bool g_active = false;
static bool g_busy = false;
static int g_hold = 0;
static uint64_t g_start = 0;
static uint64_t g_last = 0;
static bool g_shown = false;

static bool build() {
    Image* img = images::get(FOLDER, NAME);
    if (!img) return false;
    int x0 = (int)std::lround(AREA_X * img->fx), y0 = (int)std::lround(AREA_Y * img->fy);
    int x1 = (int)std::lround((AREA_X + AREA) * img->fx), y1 = (int)std::lround((AREA_Y + AREA) * img->fy);
    int w = std::min(x1, img->w) - x0, h = std::min(y1, img->h) - y0;
    if (w <= 0 || h <= 0) {
        images::drop(FOLDER, NAME);
        return false;
    }
    std::vector<uint32_t> art((size_t)w * h);
    bool lit = false;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t c = 0xFFFFFFFFu;
            images::read_pixel(img, x0 + x, y0 + y, &c);
            int a = (255 - (int)((c >> 8) & 255)) * (int)(c >> 24) / 255;
            art[(size_t)y * w + x] = gfx::rgba(a, a, a, a);
            lit |= a > 0;
        }
    }
    images::drop(FOLDER, NAME);
    if (!lit) return false;
    uint32_t* px = (uint32_t*)gfx::alloc(art.size() * 4);
    if (!px) return false;
    std::memcpy(px, art.data(), art.size() * 4);
    g_tex = gfx::texture_wrap(w, h, w, px);
    if (!g_tex) {
        gfx::release(px);
        return false;
    }
    g_tw = w;
    g_th = h;
    return true;
}

static void draw(uint64_t age) {
    double in = std::min(1.0, (double)(age - DELAY_US) / FADE_US);
    double t = age > DELAY_US + FADE_US ? (double)(age - DELAY_US - FADE_US) / 1e6 : 0;
    double glow = 0.65 + 0.35 * std::cos(t * 2 * M_PI / PERIOD);
    int a = (int)std::lround(255 * in * glow);
    float x0 = render::sx(AREA_X), y0 = render::sy(AREA_Y);
    float x1 = render::sx(AREA_X + AREA), y1 = render::sy(AREA_Y + AREA);
    bool unscaled = std::fabs(x1 - x0 - g_tw) < 0.05f && std::fabs(y1 - y0 - g_th) < 0.05f;
    if (unscaled) {
        x0 = std::round(x0);
        y0 = std::round(y0);
        x1 = x0 + g_tw;
        y1 = y0 + g_th;
    }
    gfx::sprite(g_tex, 0, 0, (float)g_tw, (float)g_th, x0, y0, x1, y1, gfx::rgba(a, a, a, a), gfx::Blend::Normal, !unscaled);
}

void begin() {
    if (g_active) return;
    if (!g_tried) {
        g_tried = true;
        g_busy = true;
        if (!build()) plat::log("loader: no loading art");
        g_busy = false;
    }
    if (!g_tex) return;
    g_active = true;
    g_shown = false;
    g_start = plat::time_us();
}

void end() { g_active = false; }

void hold(bool on) { g_hold += on ? 1 : -1; }

void pump() {
    if (!g_active || g_busy || g_hold) return;
    uint64_t now = plat::time_us();
    uint64_t age = now - g_start;
    if (age < DELAY_US || (g_shown && now - g_last < STEP_US)) return;
    g_busy = true;
    g_last = now;
    g_shown = true;
    gfx::present();
    gfx::begin_frame(0xFF000000u);
    draw(age);
    plat::frame_show();
    g_busy = false;
}

}
