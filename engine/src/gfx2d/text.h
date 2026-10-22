#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace g2d {

struct GlyphCell {
    int16_t x = 0, y = 0, w = 0, h = 0;
    float ox = 0, oy = 0;
};

struct GlyphInfo {
    float adv = 0;
    GlyphCell phase[3];
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
    std::vector<uint16_t> atlas;
    uint64_t last_use = 0;
    bool atlas_failed = false;
    const uint16_t* pixels();
    std::unordered_map<uint32_t, GlyphInfo> glyphs;
    const GlyphInfo* glyph(uint32_t cp) const;
    float advance(uint32_t cp) const;
};

FontFace* font_for(const std::string& face, int size, bool outline);
void release_font_atlases();
void prefetch_font(const std::string& face, int size, bool outline);
void set_font_atlas_budget(size_t bytes);
float measure_text(const std::string& face, int size, const std::string& text);
uint32_t decode_utf8(const std::string& s, size_t& i);
std::vector<uint32_t> utf8_codepoints(const std::string& s);

}
