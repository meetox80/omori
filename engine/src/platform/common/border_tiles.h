#pragma once

#include "../gfx.h"

namespace gfx {

struct BorderTiles {
    int period = 0, chunk_w = 0, chunks = 0, margin = 0, y0 = 0, rows = 0;
};

struct BorderSpan {
    int chunk;
    float x0, x1, u;
};

constexpr int BORDER_SPANS = 8;

BorderTiles border_tiles(const BorderArt& art, const BorderRect* bars, int nbars, int max_w, int margin);
int border_chunk_width(const BorderTiles& t, int k);
uint32_t border_mix(const BorderArt& art, int level);
void border_row(const BorderArt& art, const BorderTiles& t, int k, int y, uint8_t* out);
uint32_t* border_chunk_rgba(const BorderArt& art, const BorderTiles& t, int k);
int border_spans(const BorderTiles& t, const BorderRect& bar, float scroll, BorderSpan* out);

}
