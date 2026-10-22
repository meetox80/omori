#include "text.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>

#include "../core/fs.h"
#include "../core/value.h"
#include "../platform/gfx.h"
#include "../platform/platform.h"

namespace g2d {

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

constexpr int FONT_FORMAT = 5;

static bool load_font_format(fs::DataFile* f, const uint8_t* hd, int& w, int& h, std::vector<uint16_t>& out) {
    uint8_t e[24];
    if (rd16(&hd[16]) != 1 || fs::data_read(f, e, 24) != 24) return false;
    w = rd16(&hd[8]);
    h = rd16(&hd[10]);
    uint32_t off = rd32(e + 12), csize = rd32(e + 16), rsize = rd32(e + 20);
    if (rd16(e + 8) != w || rd16(e + 10) != h || rsize != (uint32_t)w * h * 2 || fs::data_seek(f, off, SEEK_SET) != 0) return false;
    out.assign((size_t)w * h, 0);
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) return false;
    uint8_t in[16384];
    zs.next_out = (Bytef*)out.data();
    zs.avail_out = rsize;
    int ret = Z_OK;
    while (ret != Z_STREAM_END && csize > 0) {
        if (zs.avail_in == 0) {
            size_t got = fs::data_read(f, in, std::min<size_t>(sizeof(in), csize));
            if (got == 0) break;
            csize -= (uint32_t)got;
            zs.next_in = in;
            zs.avail_in = (uInt)got;
        }
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) break;
    }
    bool ok = ret == Z_STREAM_END && zs.total_out == rsize;
    inflateEnd(&zs);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    for (uint16_t& v : out) v = (uint16_t)((v >> 8) | (v << 8));
#endif
    return ok;
}

static bool load_linear(const std::string& rel, int& w, int& h, std::vector<uint16_t>& out) {
    if (fs::DataFile* f = fs::open_data(rel)) {
        uint8_t hd[20];
        bool font = fs::data_read(f, hd, 20) == 20 && std::memcmp(hd, "TTEX", 4) == 0 && hd[5] == FONT_FORMAT;
        bool ok = font && load_font_format(f, hd, w, h, out);
        fs::data_close(f);
        if (font) return ok;
    }
    std::vector<uint8_t> d;
    if (!fs::read_data(rel, d) || d.size() < 20 || std::memcmp(d.data(), "TTEX", 4) != 0) return false;
    int fmt = d[5], flags = d[6];
    if (fmt != 0) return false;
    w = rd16(&d[8]);
    h = rd16(&d[10]);
    int count = rd16(&d[16]);
    out.assign((size_t)w * h, 0);
    size_t pos = 20;
    if (flags & 2) pos += 1024;
    for (int k = 0; k < count; k++) {
        const uint8_t* e = &d[pos + (size_t)k * 24];
        int cx = rd16(e), cy = rd16(e + 2), cw = rd16(e + 4), ch = rd16(e + 6), stride = rd16(e + 8), rows = rd16(e + 10);
        uint32_t off = rd32(e + 12), csize = rd32(e + 16), rsize = rd32(e + 20);
        std::vector<uint8_t> raw(rsize);
        uLongf len = rsize;
        if (off + csize > d.size() || uncompress(raw.data(), &len, &d[off], csize) != Z_OK || len != rsize) return false;
        bool swz = (flags & 1) != 0;
        for (int y = 0; y < ch && y < rows; y++) {
            for (int x = 0; x < cw; x++) {
                uint32_t c = swz ? gfx::stored_texel(raw.data(), x, y, stride, rows) : gfx::rgba(raw[((size_t)y * stride + x) * 4], 0, 0, raw[((size_t)y * stride + x) * 4 + 3]);
                out[(size_t)(cy + y) * w + cx + x] = (uint16_t)((c & 255) | ((c >> 24) << 8));
            }
        }
    }
    return true;
}

bool FontFace::load(const std::string& base) {
    Document doc;
    if (!doc.load_data(base + ".fnt")) return false;
    const Value& r = doc.root();
    size = r["size"].as_int(28);
    outline = r["outline"].as_int() != 0;
    ascent = (float)r["ascent"].num();
    descent = (float)r["descent"].num();
    const Value& gl = r["glyphs"];
    for (const Member& m : gl.members()) {
        GlyphInfo g;
        const Value& a = m.v;
        g.adv = (float)a[0].num();
        for (int ph = 0; ph < 3; ph++) {
            const Value& q = a[1 + ph];
            g.phase[ph].x = (int16_t)q[0].as_int();
            g.phase[ph].y = (int16_t)q[1].as_int();
            g.phase[ph].w = (int16_t)q[2].as_int();
            g.phase[ph].h = (int16_t)q[3].as_int();
            g.phase[ph].ox = (float)q[4].num();
            g.phase[ph].oy = (float)q[5].num();
        }
        glyphs[(uint32_t)std::strtoul(m.key, nullptr, 10)] = g;
    }
    this->base = base;
    return true;
}

#if defined(PSP) || defined(__3DS__) || defined(__PS2__)
static size_t g_atlas_budget = 2u << 20;
#else
static size_t g_atlas_budget = 8u << 20;
#endif

static std::vector<FontFace*>& resident_faces() {
    static std::vector<FontFace*> faces;
    return faces;
}

static uint64_t g_atlas_clock = 0;

static void drop_atlas(FontFace* f) {
    std::vector<uint16_t>().swap(f->atlas);
    std::vector<FontFace*>& faces = resident_faces();
    faces.erase(std::remove(faces.begin(), faces.end(), f), faces.end());
}

const uint16_t* FontFace::pixels() {
    last_use = ++g_atlas_clock;
    if (!atlas.empty()) return atlas.data();
    if (atlas_failed) return nullptr;
    std::vector<uint16_t> loaded;
    int w = 0, h = 0;
    try {
        if (!load_linear(base + ".tex", w, h, loaded)) {
            plat::log("missing font atlas %s", base.c_str());
            atlas_failed = true;
            return nullptr;
        }
    } catch (const std::bad_alloc&) {
        plat::log("font atlas %s skipped: out of memory", base.c_str());
        return nullptr;
    }
    std::vector<FontFace*>& faces = resident_faces();
    size_t used = 0;
    for (FontFace* f : faces) used += f->atlas.size() * sizeof(uint16_t);
    size_t need = loaded.size() * sizeof(uint16_t);
    while (!faces.empty() && used + need > g_atlas_budget) {
        FontFace* oldest = *std::min_element(faces.begin(), faces.end(), [](FontFace* a, FontFace* b) { return a->last_use < b->last_use; });
        used -= oldest->atlas.size() * sizeof(uint16_t);
        drop_atlas(oldest);
    }
    atlas_w = w;
    atlas_h = h;
    atlas = std::move(loaded);
    faces.push_back(this);
    return atlas.data();
}

void set_font_atlas_budget(size_t bytes) { g_atlas_budget = bytes; }

void release_font_atlases() {
    std::vector<FontFace*> faces = resident_faces();
    for (FontFace* f : faces) drop_atlas(f);
}

const GlyphInfo* FontFace::glyph(uint32_t cp) const {
    auto it = glyphs.find(cp);
    if (it != glyphs.end()) return &it->second;
    it = glyphs.find('?');
    return it != glyphs.end() ? &it->second : nullptr;
}

float FontFace::advance(uint32_t cp) const {
    const GlyphInfo* g = glyph(cp);
    return g ? g->adv : 0;
}

static const int SIZES[] = {12, 14, 16, 18, 20, 22, 23, 24, 25, 26, 28, 30, 32, 34, 36, 38, 40, 48, 72};

static std::string family_of(const std::string& face) {
    std::string f;
    for (char c : face) f += (char)tolower((unsigned char)c);
    size_t comma = f.find(',');
    std::string first = comma == std::string::npos ? f : f.substr(0, comma);
    while (!first.empty() && (first[0] == ' ' || first[0] == '"' || first[0] == '\'')) first.erase(first.begin());
    while (!first.empty() && (first.back() == ' ' || first.back() == '"' || first.back() == '\'')) first.pop_back();
    if (first == "omori_game") return "omori";
    return "game";
}

FontFace* font_for(const std::string& face, int size, bool outline) {
    static std::map<std::string, std::unique_ptr<FontFace>> cache;
    static std::map<std::string, bool> missing;
    int best = SIZES[0];
    for (int s : SIZES) {
        if (std::abs(s - size) < std::abs(best - size)) best = s;
    }
    std::string fam = family_of(face);
    char key[96];
    std::snprintf(key, sizeof(key), "fonts/%s_%d%s", fam.c_str(), best, outline ? "_o" : "");
    auto it = cache.find(key);
    if (it != cache.end()) return it->second.get();
    if (missing.count(key)) return fam == "game" ? nullptr : font_for("GameFont", size, outline);
    std::unique_ptr<FontFace> f(new FontFace());
    if (!f->load(key)) {
        plat::log("missing font %s", key);
        missing[key] = true;
        return fam == "game" ? nullptr : font_for("GameFont", size, outline);
    }
    FontFace* p = f.get();
    cache[key] = std::move(f);
    return p;
}

uint32_t decode_utf8(const std::string& s, size_t& i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra && i < s.size(); k++) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp;
}

std::vector<uint32_t> utf8_codepoints(const std::string& s) {
    std::vector<uint32_t> out;
    for (size_t i = 0; i < s.size();) out.push_back(decode_utf8(s, i));
    return out;
}

float measure_text(const std::string& face, int size, const std::string& text) {
    FontFace* f = font_for(face, size, false);
    if (!f) return 0;
    float scale = (float)size / f->size;
    float w = 0;
    for (size_t i = 0; i < text.size();) w += f->advance(decode_utf8(text, i));
    return w * scale;
}

}
