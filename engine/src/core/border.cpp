#include "border.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "../platform/gfx.h"
#include "../platform/platform.h"
#include "image.h"

namespace border {

static const char* const FOLDER = "img/parallaxes";
static const char* const NAME = "Space_parallax";
constexpr float FRAME = 4.0f;
constexpr float SPEED = 0.25f;

struct Layout {
    gfx::BorderRect bars[4], frame[4];
    int nbars = 0, nframe = 0;
};

static bool g_on = false;
static float g_scroll = 0;
static int g_period = 0;

static void push_clipped(gfx::BorderRect* out, int& n, gfx::BorderRect r, const gfx::BorderRect& c) {
    r.x0 = std::max(r.x0, c.x0);
    r.y0 = std::max(r.y0, c.y0);
    r.x1 = std::min(r.x1, c.x1);
    r.y1 = std::min(r.y1, c.y1);
    if (r.x0 < r.x1 && r.y0 < r.y1) out[n++] = r;
}

static Layout layout() {
    Layout l;
    float cx0, cy0, cx1, cy1;
    gfx::border_canvas(cx0, cy0, cx1, cy1);
    gfx::BorderRect g{(int)images::OFFSET_X, (int)images::OFFSET_Y, 0, 0};
    g.x1 = g.x0 + (int)std::lround(640 * images::SCALE);
    g.y1 = g.y0 + (int)std::lround(480 * images::SCALE);
    gfx::BorderRect c{(int)std::floor(cx0), (int)std::floor(cy0), (int)std::ceil(cx1), (int)std::ceil(cy1)};
    c.y0 = std::min(c.y0, g.y0);
    c.y1 = std::max(c.y1, g.y1);
    if (c.x0 < g.x0) l.bars[l.nbars++] = {c.x0, c.y0, g.x0, c.y1};
    if (c.x1 > g.x1) l.bars[l.nbars++] = {g.x1, c.y0, c.x1, c.y1};
    if (c.y0 < g.y0) l.bars[l.nbars++] = {g.x0, c.y0, g.x1, g.y0};
    if (c.y1 > g.y1) l.bars[l.nbars++] = {g.x0, g.y1, g.x1, c.y1};
    if (!l.nbars) return l;
    int t = std::max(1, (int)std::lround(FRAME * images::SCALE));
    push_clipped(l.frame, l.nframe, {g.x0 - t, g.y0 - t, g.x1 + t, g.y0}, c);
    push_clipped(l.frame, l.nframe, {g.x0 - t, g.y1, g.x1 + t, g.y1 + t}, c);
    push_clipped(l.frame, l.nframe, {g.x0 - t, g.y0, g.x0, g.y1}, c);
    push_clipped(l.frame, l.nframe, {g.x1, g.y0, g.x1 + t, g.y1}, c);
    return l;
}

static bool build() {
    Layout l = layout();
    if (!l.nbars) return false;
    Image* img = images::get(FOLDER, NAME);
    if (!img || img->w <= 0 || img->h <= 0) return false;
    uint8_t* levels = (uint8_t*)std::calloc((size_t)img->w * img->h, 1);
    if (!levels) return false;
    const uint32_t back = gfx::rgba(108, 14, 255), line = gfx::rgba(255, 255, 255);
    int lo = (int)((back >> 8) & 255), hi = (int)((line >> 8) & 255);
    uint8_t lut[256];
    for (int g = 0; g < 256; g++) lut[g] = (uint8_t)std::clamp((g - lo) * 255 / (hi - lo), 0, 255);
    for (size_t i = 0; i < img->chunks.size(); i++) {
        const ImageChunk& c = img->chunks[i];
        gfx::Texture* t = images::chunk_texture(img, i);
        if (!t) continue;
        int w = std::min(c.w, img->w - c.x), h = std::min(c.h, img->h - c.y);
        for (int y = 0; y < h; y++) {
            uint8_t* row = levels + (size_t)(c.y + y) * img->w + c.x;
            for (int x = 0; x < w; x++) {
                uint32_t px = 0;
                if (gfx::texture_read(t, x, y, &px)) row[x] = lut[(px >> 8) & 255];
            }
        }
    }
    int ox = (int)std::lround(images::OFFSET_X + 320 * images::SCALE - img->w * 0.5f);
    int oy = (int)std::lround(images::OFFSET_Y + 240 * images::SCALE - img->h * 0.5f);
    gfx::BorderArt art{levels, img->w, img->h, ox, oy, back, line};
    bool ok = gfx::set_border(&art, l.bars, l.nbars, l.frame, l.nframe);
    std::free(levels);
    if (!ok) {
        plat::log("border: out of memory");
        return false;
    }
    g_period = img->w;
    if (g_scroll >= g_period) g_scroll = 0;
    gfx::set_border_scroll(g_scroll);
    return true;
}

bool available() {
    Layout l = layout();
    return l.nbars > 0 && images::exists(FOLDER, NAME);
}

void set_enabled(bool on) {
    if (on == g_on) return;
    if (!on) {
        gfx::set_border(nullptr, nullptr, 0, nullptr, 0);
        g_on = false;
        return;
    }
    g_on = build();
}

void update() {
    if (!g_on) return;
    g_scroll += SPEED * images::SCALE;
    if (g_scroll >= g_period) g_scroll -= g_period;
    gfx::set_border_scroll(g_scroll);
}

}
