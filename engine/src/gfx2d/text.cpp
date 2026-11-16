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
#include "../core/workers.h"
#include "../platform/gfx.h"
#include "../platform/platform.h"

namespace g2d {

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

constexpr int FONT_FORMAT = 5;

static bool inflate_page(const std::string& rel, uint32_t off, uint32_t csize, size_t count, std::vector<uint16_t>& out) {
    fs::DataFile* f = fs::open_data(rel);
    if (!f) return false;
    if (fs::data_seek(f, off, SEEK_SET) != 0) {
        fs::data_close(f);
        return false;
    }
    out.assign(count, 0);
    uint32_t rsize = (uint32_t)(count * 2);
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) {
        fs::data_close(f);
        return false;
    }
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
    fs::data_close(f);
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    for (uint16_t& v : out) v = (uint16_t)((v >> 8) | (v << 8));
#endif
    return ok;
}

static bool load_legacy(const std::string& rel, int w, int h, std::vector<uint16_t>& out) {
    std::vector<uint8_t> d;
    if (!fs::read_data(rel, d) || d.size() < 20 || std::memcmp(d.data(), "TTEX", 4) != 0) return false;
    int fmt = d[5], flags = d[6];
    if (fmt != 0 || rd16(&d[8]) != w || rd16(&d[10]) != h) return false;
    int count = rd16(&d[16]);
    out.assign((size_t)w * h, 0);
    size_t pos = 20;
    if (flags & 2) pos += 1024;
    for (int k = 0; k < count; k++) {
        if (pos + (size_t)(k + 1) * 24 > d.size()) return false;
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

static bool read_page(const std::string& rel, bool legacy, int w, const FontPage& pg, std::vector<uint16_t>& out) {
    if (legacy) return load_legacy(rel, w, pg.h, out);
    return inflate_page(rel, pg.off, pg.csize, (size_t)w * pg.h, out);
}

bool FontFace::load(const std::string& base) {
    {
        Document doc;
        if (!doc.load_data(base + ".fnt")) return false;
        const Value& r = doc.root();
        size = r["size"].as_int(28);
        outline = r["outline"].as_int() != 0;
        ascent = (float)r["ascent"].num();
        descent = (float)r["descent"].num();
        const Value& gl = r["glyphs"];
        glyphs.reserve(gl.size());
        for (const Member& m : gl.members()) {
            GlyphInfo g;
            const Value& a = m.v;
            g.adv = (float)a[0].num();
            for (int ph = 0; ph < 3; ph++) {
                const Value& q = a[1 + ph];
                g.phase[ph].x = (uint16_t)q[0].as_int();
                g.phase[ph].y = (uint16_t)q[1].as_int();
                g.phase[ph].w = (int16_t)q[2].as_int();
                g.phase[ph].h = (int16_t)q[3].as_int();
                g.phase[ph].ox = (int16_t)std::lround(q[4].num());
                g.phase[ph].oy = (int16_t)std::lround(q[5].num());
            }
            glyphs.emplace_back((uint32_t)std::strtoul(m.key, nullptr, 10), g);
        }
        std::sort(glyphs.begin(), glyphs.end(), [](const std::pair<uint32_t, GlyphInfo>& a, const std::pair<uint32_t, GlyphInfo>& b) { return a.first < b.first; });
    }
    this->base = base;
    if (fs::DataFile* f = fs::open_data(base + ".tex")) {
        uint8_t hd[20];
        if (fs::data_read(f, hd, 20) == 20 && std::memcmp(hd, "TTEX", 4) == 0) {
            int w = rd16(&hd[8]), h = rd16(&hd[10]);
            if (hd[5] == FONT_FORMAT) {
                int count = rd16(&hd[16]);
                std::vector<uint8_t> table((size_t)count * 24);
                if (count > 0 && fs::data_read(f, table.data(), table.size()) == table.size()) {
                    for (int k = 0; k < count; k++) {
                        const uint8_t* e = &table[(size_t)k * 24];
                        FontPage pg;
                        pg.y = rd16(e + 2);
                        pg.h = rd16(e + 6);
                        pg.off = rd32(e + 12);
                        pg.csize = rd32(e + 16);
                        if (rd16(e + 4) != w || rd16(e + 8) != w || rd16(e + 10) != pg.h || rd32(e + 20) != (uint32_t)w * pg.h * 2 ||
                            (!pages.empty() && pg.y != pages.back().y + pages.back().h)) {
                            pages.clear();
                            break;
                        }
                        pages.push_back(std::move(pg));
                    }
                }
            } else if (hd[5] == 0) {
                legacy = true;
                FontPage pg;
                pg.h = h;
                pages.push_back(std::move(pg));
            }
            if (!pages.empty()) {
                atlas_w = w;
                atlas_h = h;
            }
        }
        fs::data_close(f);
    }
    if (pages.empty()) plat::log("missing font atlas %s", base.c_str());
    return true;
}

#if defined(PSP) || defined(__3DS__) || defined(__PS2__)
static size_t g_atlas_budget = 2u << 20;
#else
static size_t g_atlas_budget = 8u << 20;
#endif

struct Resident {
    FontFace* face;
    size_t page;
};

static std::vector<Resident> g_resident;
static size_t g_resident_bytes = 0;
static uint64_t g_atlas_clock = 0;

static void drop_page(size_t r) {
    FontPage& pg = g_resident[r].face->pages[g_resident[r].page];
    g_resident_bytes -= pg.px.size() * sizeof(uint16_t);
    std::vector<uint16_t>().swap(pg.px);
    g_resident[r] = g_resident.back();
    g_resident.pop_back();
}

static void make_room(size_t need) {
    while (!g_resident.empty() && (g_resident_bytes + need > g_atlas_budget || plat::heap_tight(1u << 20))) {
        size_t oldest = 0;
        for (size_t r = 1; r < g_resident.size(); r++) {
            if (g_resident[r].face->pages[g_resident[r].page].last_use < g_resident[oldest].face->pages[g_resident[oldest].page].last_use) oldest = r;
        }
        drop_page(oldest);
    }
}

struct AtlasLoad {
    FontFace* face = nullptr;
    size_t page = 0;
    std::string path;
    bool legacy = false;
    int w = 0;
    FontPage spec;
    bool started = false;
    bool done = false;
    bool ok = false;
    uint64_t finished = 0;
    std::vector<uint16_t> px;
};

constexpr size_t MAX_ATLAS_LOADS = 64;
constexpr uint64_t ATLAS_LOAD_KEEP_US = 10000000;

static plat::Mutex* g_load_mutex = nullptr;
static std::vector<std::shared_ptr<AtlasLoad>> g_loads;
static bool g_loader = false;

static void atlas_loader() {
    while (workers::live()) {
        std::shared_ptr<AtlasLoad> job;
        plat::mutex_lock(g_load_mutex);
        for (auto& l : g_loads) {
            if (l->started) continue;
            l->started = true;
            job = l;
            break;
        }
        plat::mutex_unlock(g_load_mutex);
        if (!job) {
            plat::sleep_ms(4);
            continue;
        }
        std::vector<uint16_t> px;
        plat::mutex_lock(g_load_mutex);
        px.swap(job->px);
        plat::mutex_unlock(g_load_mutex);
        bool ok = false;
        try {
            ok = read_page(job->path, job->legacy, job->w, job->spec, px);
        } catch (const std::bad_alloc&) {
            std::vector<uint16_t>().swap(px);
        }
        plat::mutex_lock(g_load_mutex);
        job->px.swap(px);
        job->ok = ok;
        job->done = true;
        job->finished = plat::time_us();
        plat::mutex_unlock(g_load_mutex);
    }
    workers::leave();
}

static void expire_loads_locked() {
    uint64_t now = plat::time_us();
    g_loads.erase(std::remove_if(g_loads.begin(), g_loads.end(),
                                 [now](const std::shared_ptr<AtlasLoad>& l) { return l->done && now - l->finished > ATLAS_LOAD_KEEP_US; }),
                  g_loads.end());
}

static bool take_prefetched(FontFace* face, size_t page, std::vector<uint16_t>& out) {
    if (!g_loader) return false;
    for (;;) {
        plat::mutex_lock(g_load_mutex);
        auto it = std::find_if(g_loads.begin(), g_loads.end(), [face, page](const std::shared_ptr<AtlasLoad>& l) { return l->face == face && l->page == page; });
        if (it == g_loads.end()) {
            plat::mutex_unlock(g_load_mutex);
            return false;
        }
        std::shared_ptr<AtlasLoad> l = *it;
        if (l->started && !l->done) {
            plat::mutex_unlock(g_load_mutex);
            plat::sleep_ms(1);
            continue;
        }
        g_loads.erase(it);
        plat::mutex_unlock(g_load_mutex);
        out.swap(l->px);
        return l->done && l->ok;
    }
}

size_t FontFace::atlas_bytes() const { return (size_t)atlas_w * atlas_h * sizeof(uint16_t); }

size_t FontFace::page_of(int y) const {
    size_t lo = 0, hi = pages.size();
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (pages[mid].y <= y) lo = mid + 1;
        else hi = mid;
    }
    return lo == 0 ? pages.size() : lo - 1;
}

bool FontFace::load_page(size_t p) {
    FontPage& pg = pages[p];
    if (pg.failed) return false;
    std::vector<uint16_t> loaded;
    try {
        if (!take_prefetched(this, p, loaded) && !read_page(base + ".tex", legacy, atlas_w, pg, loaded)) {
            plat::log("missing font atlas page %s %d", base.c_str(), (int)p);
            pg.failed = true;
            return false;
        }
    } catch (const std::bad_alloc&) {
        plat::log("font atlas %s skipped: out of memory", base.c_str());
        return false;
    }
    size_t need = loaded.size() * sizeof(uint16_t);
    make_room(need);
    pg.px = std::move(loaded);
    g_resident.push_back(Resident{this, p});
    g_resident_bytes += need;
    return true;
}

const uint16_t* FontFace::cell_pixels(const GlyphCell& q) {
    size_t p = page_of(q.y);
    if (p >= pages.size()) return nullptr;
    FontPage& pg = pages[p];
    if (q.x + q.w > atlas_w || q.y + q.h > pg.y + pg.h) return nullptr;
    pg.last_use = ++g_atlas_clock;
    if (pg.px.empty() && !load_page(p)) return nullptr;
    return pg.px.data() + (size_t)(q.y - pg.y) * atlas_w + q.x;
}

void prefetch_font(const std::string& face, int size, bool outline, const std::string& text) {
    FontFace* f = font_for(face, size, outline);
    if (!f || f->pages.empty()) return;
    std::vector<size_t> want;
    if (text.empty() || f->legacy || f->atlas_bytes() <= g_atlas_budget / 2) {
        for (size_t p = 0; p < f->pages.size(); p++) want.push_back(p);
    } else {
        for (size_t i = 0; i < text.size();) {
            const GlyphInfo* g = f->glyph(decode_utf8(text, i));
            if (!g) continue;
            for (const GlyphCell& q : g->phase) {
                if (q.w <= 0 || q.h <= 0) continue;
                size_t p = f->page_of(q.y);
                if (p < f->pages.size() && std::find(want.begin(), want.end(), p) == want.end()) want.push_back(p);
            }
        }
    }
    want.erase(std::remove_if(want.begin(), want.end(), [f](size_t p) { return !f->pages[p].px.empty() || f->pages[p].failed; }), want.end());
    if (want.empty()) return;
    if (!g_loader) {
        g_loads.reserve(MAX_ATLAS_LOADS);
        g_load_mutex = plat::mutex_create();
        if (!g_load_mutex || !workers::start(plat::start_thread, atlas_loader)) return;
        g_loader = true;
    }
    for (size_t p : want) {
        const FontPage& pg = f->pages[p];
        size_t bytes = (size_t)f->atlas_w * pg.h * sizeof(uint16_t);
        plat::mutex_lock(g_load_mutex);
        expire_loads_locked();
        size_t flight = 0;
        bool known = false;
        for (const auto& l : g_loads) {
            flight += (size_t)l->w * l->spec.h * sizeof(uint16_t);
            if (l->face == f && l->page == p) known = true;
        }
        bool room = g_loads.size() < MAX_ATLAS_LOADS && flight + bytes <= std::max(g_atlas_budget / 2, bytes);
        plat::mutex_unlock(g_load_mutex);
        if (known) continue;
        if (!room) return;
        auto l = std::make_shared<AtlasLoad>();
        l->face = f;
        l->page = p;
        l->path = f->base + ".tex";
        l->legacy = f->legacy;
        l->w = f->atlas_w;
        l->spec.y = pg.y;
        l->spec.h = pg.h;
        l->spec.off = pg.off;
        l->spec.csize = pg.csize;
        if (profile::BACKGROUND_DECODE) {
            try {
                l->px.reserve(bytes / sizeof(uint16_t));
            } catch (const std::bad_alloc&) {
                return;
            }
        }
        plat::mutex_lock(g_load_mutex);
        if (g_loads.size() < MAX_ATLAS_LOADS) g_loads.push_back(std::move(l));
        plat::mutex_unlock(g_load_mutex);
    }
}

void set_font_atlas_budget(size_t bytes) { g_atlas_budget = bytes; }

void release_font_atlases() {
    while (!g_resident.empty()) drop_page(g_resident.size() - 1);
    if (!g_loader) return;
    plat::mutex_lock(g_load_mutex);
    g_loads.erase(std::remove_if(g_loads.begin(), g_loads.end(), [](const std::shared_ptr<AtlasLoad>& l) { return l->done; }), g_loads.end());
    plat::mutex_unlock(g_load_mutex);
}

const GlyphInfo* FontFace::glyph(uint32_t cp) const {
    auto find = [this](uint32_t c) -> const GlyphInfo* {
        auto it = std::lower_bound(glyphs.begin(), glyphs.end(), c, [](const std::pair<uint32_t, GlyphInfo>& e, uint32_t v) { return e.first < v; });
        return it != glyphs.end() && it->first == c ? &it->second : nullptr;
    };
    const GlyphInfo* g = find(cp);
    return g ? g : find('?');
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

int font_atlas_size(int size) {
    int best = SIZES[0];
    for (int s : SIZES) {
        if (std::abs(s - size) < std::abs(best - size)) best = s;
    }
    return best;
}

FontFace* font_for(const std::string& face, int size, bool outline) {
    static std::map<std::string, std::unique_ptr<FontFace>> cache;
    static std::map<std::string, bool> missing;
    int best = font_atlas_size(size);
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
