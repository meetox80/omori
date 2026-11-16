#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace g2d {

struct GlyphCell {
    uint16_t x = 0, y = 0;
    int16_t w = 0, h = 0;
    int16_t ox = 0, oy = 0;
};

struct GlyphInfo {
    float adv = 0;
    GlyphCell phase[3];
};

struct FontPage {
    int y = 0, h = 0;
    uint32_t off = 0, csize = 0;
    std::vector<uint16_t> px;
    uint64_t last_use = 0;
    bool failed = false;
};

class FontFace {
public:
    bool load(const std::string& base);
    int size = 28;
    bool outline = false;
    float ascent = 0;
    float descent = 0;
    std::string base;
    int atlas_w = 0, atlas_h = 0;
    bool legacy = false;
    std::vector<FontPage> pages;
    std::vector<std::pair<uint32_t, GlyphInfo>> glyphs;
    const GlyphInfo* glyph(uint32_t cp) const;
    float advance(uint32_t cp) const;
    const uint16_t* cell_pixels(const GlyphCell& q);
    size_t page_of(int y) const;
    size_t atlas_bytes() const;

private:
    bool load_page(size_t p);
};

FontFace* font_for(const std::string& face, int size, bool outline);
int font_atlas_size(int size);
void release_font_atlases();
void prefetch_font(const std::string& face, int size, bool outline, const std::string& text = std::string());
void set_font_atlas_budget(size_t bytes);
float measure_text(const std::string& face, int size, const std::string& text);
uint32_t decode_utf8(const std::string& s, size_t& i);
std::vector<uint32_t> utf8_codepoints(const std::string& s);

}
