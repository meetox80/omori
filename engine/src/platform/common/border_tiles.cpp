#include "border_tiles.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gfx {

static int wrap(int v, int n) {
    v %= n;
    return v < 0 ? v + n : v;
}

BorderTiles border_tiles(const BorderArt& art, const BorderRect* bars, int nbars, int max_w, int margin) {
    BorderTiles t;
    if (nbars <= 0 || art.w <= 0 || art.h <= 0) return t;
    int y0 = bars[0].y0, y1 = bars[0].y1;
    for (int i = 1; i < nbars; i++) {
        y0 = std::min(y0, bars[i].y0);
        y1 = std::max(y1, bars[i].y1);
    }
    int room = std::max(1, max_w - 2 * margin);
    t.period = art.w;
    t.chunks = (art.w + room - 1) / room;
    t.chunk_w = (art.w + t.chunks - 1) / t.chunks;
    t.margin = margin;
    t.y0 = y0 - margin;
    t.rows = y1 - y0 + 2 * margin;
    return t;
}

int border_chunk_width(const BorderTiles& t, int k) { return std::min(t.chunk_w, t.period - k * t.chunk_w) + 2 * t.margin; }

uint32_t border_mix(const BorderArt& art, int level) {
    uint32_t c = 0xFF000000u;
    for (int s = 0; s < 24; s += 8) {
        int a = (int)((art.back >> s) & 255), b = (int)((art.line >> s) & 255);
        c |= (uint32_t)((a * (255 - level) + b * level + 127) / 255) << s;
    }
    return c;
}

void border_row(const BorderArt& art, const BorderTiles& t, int k, int y, uint8_t* out) {
    const uint8_t* row = art.levels + (size_t)wrap(t.y0 + y - art.oy, art.h) * art.w;
    int x = wrap(k * t.chunk_w - t.margin - art.ox, art.w), w = border_chunk_width(t, k);
    for (int i = 0; i < w; i++) {
        out[i] = row[x];
        if (++x == art.w) x = 0;
    }
}

uint32_t* border_chunk_rgba(const BorderArt& art, const BorderTiles& t, int k) {
    int w = border_chunk_width(t, k);
    uint32_t* px = (uint32_t*)alloc((size_t)w * t.rows * 4);
    if (!px) return nullptr;
    uint32_t lut[256];
    for (int i = 0; i < 256; i++) lut[i] = border_mix(art, i);
    std::vector<uint8_t> row((size_t)w);
    for (int y = 0; y < t.rows; y++) {
        border_row(art, t, k, y, row.data());
        uint32_t* d = px + (size_t)y * w;
        for (int x = 0; x < w; x++) d[x] = lut[row[x]];
    }
    return px;
}

int border_spans(const BorderTiles& t, const BorderRect& bar, float scroll, BorderSpan* out) {
    if (t.period <= 0) return 0;
    int n = 0;
    float x = (float)bar.x0, end = (float)bar.x1;
    float c = std::fmod(x + scroll, (float)t.period);
    if (c < 0) c += (float)t.period;
    while (x < end && n < BORDER_SPANS) {
        int k = std::min((int)(c / (float)t.chunk_w), t.chunks - 1);
        float edge = (float)std::min((k + 1) * t.chunk_w, t.period);
        float len = std::min(edge - c, end - x);
        if (len > 0) {
            out[n++] = {k, x, x + len, c - (float)(k * t.chunk_w) + (float)t.margin};
            x += len;
            c += len;
        } else {
            c = edge;
        }
        if (c >= (float)t.period) c -= (float)t.period;
    }
    return n;
}

}
