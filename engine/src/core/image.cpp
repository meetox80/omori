#include "image.h"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>
#include <unordered_map>

#include "../platform/platform.h"
#include "fs.h"

Image::~Image() {
    for (auto& c : chunks) {
        if (c.tex) gfx::texture_destroy(c.tex);
    }
}

namespace images {

struct Entry {
    Image* img = nullptr;
    uint64_t last = 0;
    bool pinned = false;
    bool missing = false;
};

static std::unordered_map<std::string, Entry> g_cache;
static uint64_t g_generation = 1;
static size_t g_used = 0;
static size_t g_budget = 20u << 20;
static uint64_t g_tick = 0;

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

struct Decoded {
    struct Chunk {
        int x, y, w, h, stride, rows;
        void* pixels;
        uint32_t* palette;
        uint32_t offset, csize, rsize;
    };
    int w = 0, h = 0, ow = 0, oh = 0, fmt = 0, flags = 0;
    std::vector<Chunk> chunks;
    std::vector<uint32_t> palette;
    std::string source;
    bool lazy = false;
    bool ok = false;
    bool oom = false;
};

static void free_decoded(Decoded& d) {
    for (auto& c : d.chunks) {
        gfx::release(c.pixels);
        gfx::release(c.palette);
    }
    d.chunks.clear();
}

static bool inflate_range(fs::DataFile* f, uint32_t off, uint32_t csize, void* dst, uint32_t rsize) {
    if (fs::data_seek(f, off, SEEK_SET) != 0) return false;
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) return false;
    uint8_t in[16384];
    zs.next_out = (Bytef*)dst;
    zs.avail_out = rsize;
    uint32_t left = csize;
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        if (zs.avail_in == 0) {
            if (left == 0) break;
            size_t want = std::min<size_t>(sizeof(in), left);
            size_t got = fs::data_read(f, in, want);
            if (got == 0) break;
            left -= (uint32_t)got;
            zs.next_in = in;
            zs.avail_in = (uInt)got;
        }
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) break;
    }
    bool ok = ret == Z_STREAM_END && zs.total_out == rsize;
    inflateEnd(&zs);
    return ok;
}

static void make_room(size_t need, bool force);

static Decoded decode_data(const std::string& rel, bool main_thread) {
    Decoded out;
    fs::DataFile* f = fs::open_data(rel);
    if (!f) return out;
    uint8_t hd[20];
    if (fs::data_read(f, hd, 20) != 20 || std::memcmp(hd, "TTEX", 4) != 0) {
        fs::data_close(f);
        return out;
    }
    out.fmt = hd[5];
    out.flags = hd[6];
    out.w = rd16(&hd[8]);
    out.h = rd16(&hd[10]);
    out.ow = rd16(&hd[12]);
    out.oh = rd16(&hd[14]);
    int count = rd16(&hd[16]);
    uint8_t pal[1024];
    bool has_pal = (out.flags & 2) != 0;
    if (has_pal && fs::data_read(f, pal, 1024) != 1024) {
        fs::data_close(f);
        return out;
    }
    std::vector<uint8_t> table((size_t)count * 24);
    if (count > 0 && fs::data_read(f, table.data(), table.size()) != table.size()) {
        fs::data_close(f);
        return out;
    }
    if (count > 1) {
        out.lazy = true;
        out.source = rel;
        if (has_pal) out.palette.assign((const uint32_t*)pal, (const uint32_t*)pal + 256);
        for (int k = 0; k < count; k++) {
            const uint8_t* e = &table[(size_t)k * 24];
            Decoded::Chunk c{};
            c.x = rd16(e);
            c.y = rd16(e + 2);
            c.w = rd16(e + 4);
            c.h = rd16(e + 6);
            c.stride = rd16(e + 8);
            c.rows = rd16(e + 10);
            c.offset = rd32(e + 12);
            c.csize = rd32(e + 16);
            c.rsize = rd32(e + 20);
            out.chunks.push_back(c);
        }
        fs::data_close(f);
        out.ok = true;
        return out;
    }
    if (main_thread) {
        size_t need = 0;
        for (int k = 0; k < count; k++) need += rd32(&table[(size_t)k * 24 + 20]) + (has_pal ? 1024 : 0);
        make_room(need, false);
    }
    for (int k = 0; k < count; k++) {
        const uint8_t* e = &table[(size_t)k * 24];
        Decoded::Chunk c{};
        c.x = rd16(e);
        c.y = rd16(e + 2);
        c.w = rd16(e + 4);
        c.h = rd16(e + 6);
        c.stride = rd16(e + 8);
        c.rows = rd16(e + 10);
        uint32_t off = rd32(e + 12), csize = rd32(e + 16), rsize = rd32(e + 20);
        c.pixels = gfx::alloc(rsize);
        if (!c.pixels && main_thread) {
            make_room(rsize, true);
            c.pixels = gfx::alloc(rsize);
        }
        if (!c.pixels || !inflate_range(f, off, csize, c.pixels, rsize)) {
            if (!c.pixels) out.oom = true;
            gfx::release(c.pixels);
            free_decoded(out);
            fs::data_close(f);
            return out;
        }
        c.palette = nullptr;
        if (has_pal) {
            c.palette = (uint32_t*)gfx::alloc(1024);
            std::memcpy(c.palette, pal, 1024);
        }
        out.chunks.push_back(c);
    }
    fs::data_close(f);
    out.ok = true;
    return out;
}

static Image* finish(Decoded& d) {
    if (!d.ok) return nullptr;
    Image* img = new Image();
    img->w = d.w;
    img->h = d.h;
    img->ow = d.ow;
    img->oh = d.oh;
    img->fx = img->ow ? (float)img->w / img->ow : 1.0f;
    img->fy = img->oh ? (float)img->h / img->oh : 1.0f;
    img->fmt = d.fmt;
    img->swizzled = (d.flags & 1) != 0;
    img->lazy = d.lazy;
    img->source = d.source;
    img->palette = std::move(d.palette);
    for (auto& c : d.chunks) {
        ImageChunk ic{};
        ic.x = c.x;
        ic.y = c.y;
        ic.w = c.w;
        ic.h = c.h;
        ic.offset = c.offset;
        ic.csize = c.csize;
        ic.rsize = c.rsize;
        ic.stride = (uint16_t)c.stride;
        ic.rows = (uint16_t)c.rows;
        ic.tex = d.lazy ? nullptr : gfx::texture_create(c.w, c.h, c.stride, c.rows, (gfx::Format)d.fmt, img->swizzled, c.pixels, c.palette);
        if (ic.tex) img->bytes += gfx::texture_bytes(ic.tex);
        img->chunks.push_back(ic);
    }
    d.chunks.clear();
    return img;
}

static Image* load_data(const std::string& rel, bool& oom) {
    Decoded d = decode_data(rel, true);
    oom = d.oom;
    return finish(d);
}

Image* load_data(const std::string& rel) {
    bool oom = false;
    return load_data(rel, oom);
}

std::string lower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) {
        if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    }
    return r;
}

std::string resolve(const std::string& folder, const std::string& name, const char* ext) {
    (void)ext;
    std::string f = folder;
    while (!f.empty() && f.back() == '/') f.pop_back();
    return lower(f + "/" + name);
}

bool exists(const std::string& folder, const std::string& name) { return fs::data_exists(resolve(folder, name, ".png") + ".tex"); }

bool read_pixel(const Image* img, int px, int py, uint32_t* out) {
    if (!img) return false;
    for (size_t i = 0; i < img->chunks.size(); i++) {
        const ImageChunk& c = img->chunks[i];
        if (px >= c.x && py >= c.y && px < c.x + c.w && py < c.y + c.h) {
            gfx::Texture* t = chunk_texture(img, i);
            return t && gfx::texture_read(t, px - c.x, py - c.y, out);
        }
    }
    return false;
}

static void make_room(size_t need, bool force);

gfx::Texture* chunk_texture(const Image* cimg, size_t index) {
    Image* img = const_cast<Image*>(cimg);
    if (!img || index >= img->chunks.size()) return nullptr;
    ImageChunk& c = img->chunks[index];
    if (c.tex || !img->lazy) return c.tex;
    size_t need = c.rsize + (img->palette.empty() ? 0 : 1024);
    make_room(need, false);
    fs::DataFile* f = fs::open_data(img->source);
    if (!f) return nullptr;
    void* pixels = gfx::alloc(c.rsize);
    if (!pixels) {
        make_room(need, true);
        pixels = gfx::alloc(c.rsize);
    }
    bool ok = pixels && inflate_range(f, c.offset, c.csize, pixels, c.rsize);
    fs::data_close(f);
    if (!ok) {
        gfx::release(pixels);
        return nullptr;
    }
    uint32_t* pal = nullptr;
    if (!img->palette.empty()) {
        pal = (uint32_t*)gfx::alloc(1024);
        if (!pal) {
            gfx::release(pixels);
            return nullptr;
        }
        std::memcpy(pal, img->palette.data(), 1024);
    }
    c.tex = gfx::texture_create(c.w, c.h, c.stride, c.rows, (gfx::Format)img->fmt, img->swizzled, pixels, pal);
    size_t bytes = gfx::texture_bytes(c.tex);
    img->bytes += bytes;
    g_used += bytes;
    return c.tex;
}

constexpr size_t HEAP_RESERVE = 5u << 20;

static bool over_heap(size_t need, size_t freed) {
    size_t limit = plat::heap_limit();
    if (!limit) return false;
    size_t used = plat::heap_used();
    used = used > freed ? used - freed : 0;
    return used + need + HEAP_RESERVE > limit;
}

static void evict(size_t need) {
    size_t freed = 0;
    while (g_used + need > g_budget || (g_used > 0 && over_heap(need, freed))) {
        auto victim = g_cache.end();
        for (auto it = g_cache.begin(); it != g_cache.end(); ++it) {
            if (!it->second.img || it->second.pinned || it->second.last + 2 > g_tick) continue;
            if (victim == g_cache.end() || it->second.last < victim->second.last) victim = it;
        }
        if (victim == g_cache.end()) return;
        g_used -= victim->second.img->bytes;
        freed += victim->second.img->bytes;
        delete victim->second.img;
        g_cache.erase(victim);
        g_generation++;
        if (over_heap(need, 0) && gfx::reclaim_memory()) freed = 0;
    }
}

static void make_room(size_t need, bool force) {
    if (!force) {
        evict(need);
        return;
    }
    size_t saved = g_budget;
    g_budget = 0;
    evict(need);
    g_budget = saved;
    gfx::reclaim_memory();
}

static plat::Mutex* g_mutex = nullptr;
static std::vector<std::string> g_queue;
static std::string g_working;
static std::vector<std::pair<std::string, Decoded>> g_ready;
static bool g_worker = false;

static void worker() {
    for (;;) {
        std::string job;
        plat::mutex_lock(g_mutex);
        if (!g_queue.empty()) {
            job = g_queue.front();
            g_queue.erase(g_queue.begin());
            g_working = job;
        }
        plat::mutex_unlock(g_mutex);
        if (job.empty()) {
            plat::sleep_ms(4);
            continue;
        }
        Decoded d = decode_data(job + ".tex", false);
        plat::mutex_lock(g_mutex);
        g_ready.emplace_back(job, std::move(d));
        g_working.clear();
        plat::mutex_unlock(g_mutex);
    }
}

static void ensure_worker() {
    if (g_worker) return;
    g_worker = true;
    g_mutex = plat::mutex_create();
    plat::start_thread(worker);
}

static void insert(const std::string& rel, Image* img) {
    Entry e;
    e.img = img;
    e.last = g_tick;
    e.missing = img == nullptr;
    if (img) {
        evict(img->bytes);
        g_used += img->bytes;
    } else {
        plat::log("missing image %s", rel.c_str());
    }
    g_cache[rel] = e;
}

static void collect_ready(const std::string* want) {
    if (!g_worker) return;
    std::vector<std::pair<std::string, Decoded>> ready;
    plat::mutex_lock(g_mutex);
    ready.swap(g_ready);
    plat::mutex_unlock(g_mutex);
    for (auto& r : ready) {
        if (g_cache.count(r.first) || !r.second.ok) {
            free_decoded(r.second);
            continue;
        }
        insert(r.first, finish(r.second));
    }
    (void)want;
}

void prefetch_path(const std::string& rel) {
    if (g_cache.count(rel)) return;
    ensure_worker();
    plat::mutex_lock(g_mutex);
    bool queued = g_working == rel;
    for (auto& q : g_queue) {
        if (q == rel) queued = true;
    }
    for (auto& r : g_ready) {
        if (r.first == rel) queued = true;
    }
    if (!queued && g_queue.size() < 16) g_queue.push_back(rel);
    plat::mutex_unlock(g_mutex);
}

void prefetch(const std::string& folder, const std::string& name) {
    if (name.empty()) return;
    prefetch_path(resolve(folder, name, ".png"));
}

static bool wait_pending(const std::string& rel) {
    if (!g_worker) return false;
    for (;;) {
        bool pending = false;
        plat::mutex_lock(g_mutex);
        if (g_working == rel) pending = true;
        for (auto it = g_queue.begin(); it != g_queue.end(); ++it) {
            if (*it == rel) {
                g_queue.erase(it);
                break;
            }
        }
        bool ready = false;
        for (auto& r : g_ready) {
            if (r.first == rel) ready = true;
        }
        plat::mutex_unlock(g_mutex);
        if (ready) {
            collect_ready(&rel);
            return true;
        }
        if (!pending) return false;
        plat::sleep_ms(1);
    }
}

Image* get_path(const std::string& rel) {
    auto it = g_cache.find(rel);
    if (it != g_cache.end()) {
        it->second.last = g_tick;
        return it->second.img;
    }
    if (wait_pending(rel)) {
        it = g_cache.find(rel);
        if (it != g_cache.end()) {
            it->second.last = g_tick;
            return it->second.img;
        }
    }
    bool oom = false;
    Image* img = load_data(rel + ".tex", oom);
    if (!img && oom) {
        plat::log("image %s skipped: out of memory", rel.c_str());
        return nullptr;
    }
    insert(rel, img);
    return img;
}

uint64_t generation() { return g_generation; }

Image* get_path_cached(const std::string& rel, CacheHandle& handle) {
    if (handle.entry && handle.generation == g_generation) {
        Entry* e = (Entry*)handle.entry;
        e->last = g_tick;
        return e->img;
    }
    Image* img = get_path(rel);
    auto it = g_cache.find(rel);
    handle.entry = it != g_cache.end() ? &it->second : nullptr;
    handle.generation = g_generation;
    return img;
}

Image* get(const std::string& folder, const std::string& name) {
    if (name.empty()) return nullptr;
    return get_path(resolve(folder, name, ".png"));
}

void preload(const std::string& folder, const std::string& name) { get(folder, name); }

bool logical_size(const std::string& folder, const std::string& name, int& w, int& h) {
    static std::unordered_map<std::string, std::pair<int, int>> sizes;
    w = h = 0;
    if (name.empty()) return false;
    std::string rel = resolve(folder, name, ".png");
    auto it = sizes.find(rel);
    if (it == sizes.end()) {
        std::pair<int, int> wh(0, 0);
        auto c = g_cache.find(rel);
        if (c != g_cache.end() && c->second.img) {
            wh = {c->second.img->ow, c->second.img->oh};
        } else if (fs::DataFile* f = fs::open_data(rel + ".tex")) {
            uint8_t hd[16];
            if (fs::data_read(f, hd, 16) == 16 && std::memcmp(hd, "TTEX", 4) == 0) wh = {rd16(&hd[12]), rd16(&hd[14])};
            fs::data_close(f);
        }
        it = sizes.emplace(rel, wh).first;
    }
    w = it->second.first;
    h = it->second.second;
    return w > 0;
}

void pin(const std::string& folder, const std::string& name, bool pinned) {
    if (name.empty()) return;
    std::string key = resolve(folder, name, ".png");
    get_path(key);
    auto it = g_cache.find(key);
    if (it != g_cache.end()) it->second.pinned = pinned;
}

void set_budget(size_t bytes) { g_budget = bytes; }
void trim() { evict(0); }
void tick() {
    g_tick++;
    collect_ready(nullptr);
}

void release_memory() {
    make_room(SIZE_MAX / 2, true);
}

void forget_recent() {
    for (auto& kv : g_cache) kv.second.last = 0;
}

void clear() {
    g_generation++;
    for (auto& kv : g_cache) {
        if (!kv.second.pinned) delete kv.second.img;
    }
    for (auto it = g_cache.begin(); it != g_cache.end();) {
        if (!it->second.pinned) it = g_cache.erase(it);
        else ++it;
    }
    g_used = 0;
    for (auto& kv : g_cache) {
        if (kv.second.img) g_used += kv.second.img->bytes;
    }
}

size_t used_bytes() { return g_used; }

}

namespace render {

static inline bool near_int(float v) { return std::fabs(v - std::round(v)) < 0.01f; }

void blit_screen(const Image* img, float srcx, float srcy, float srcw, float srch, float x0, float y0, float x1, float y1, uint32_t color,
                 gfx::Blend blend, bool smooth) {
    if (!img || srcw <= 0 || srch <= 0) return;
    float tx0 = srcx * img->fx, ty0 = srcy * img->fy;
    float tx1 = (srcx + srcw) * img->fx, ty1 = (srcy + srch) * img->fy;
    float kx = (x1 - x0) / (tx1 - tx0), ky = (y1 - y0) / (ty1 - ty0);
    for (const ImageChunk& c : img->chunks) {
        float cx0 = std::max(tx0, (float)c.x), cy0 = std::max(ty0, (float)c.y);
        float cx1 = std::min(tx1, (float)(c.x + c.w)), cy1 = std::min(ty1, (float)(c.y + c.h));
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        float dx0 = x0 + (cx0 - tx0) * kx, dx1 = x0 + (cx1 - tx0) * kx;
        float dy0 = y0 + (cy0 - ty0) * ky, dy1 = y0 + (cy1 - ty0) * ky;
        gfx::sprite(c.tex, cx0 - c.x, cy0 - c.y, cx1 - c.x, cy1 - c.y, dx0, dy0, dx1, dy1, color, blend, smooth);
    }
}

void blit(const Image* img, float srcx, float srcy, float srcw, float srch, float dx, float dy, float scalex, float scaley, uint32_t color,
          gfx::Blend blend) {
    if (!img) return;
    float x0 = sx(dx), y0 = sy(dy);
    float x1 = sx(dx + srcw * scalex), y1 = sy(dy + srch * scaley);
    float pw = std::fabs(x1 - x0), ph = std::fabs(y1 - y0);
    float tw = srcw * img->fx, th = srch * img->fy;
    bool unscaled = std::fabs(pw - tw) < 0.05f && std::fabs(ph - th) < 0.05f;
    bool smooth = true;
    if (unscaled) {
        float rx = std::round(x0), ry = std::round(y0);
        x1 += rx - x0;
        y1 += ry - y0;
        x0 = rx;
        y0 = ry;
        smooth = !(near_int(srcx * img->fx) && near_int(srcy * img->fy));
    }
    blit_screen(img, srcx, srcy, srcw, srch, x0, y0, x1, y1, color, blend, smooth);
}

void blit_rotated(const Image* img, float srcx, float srcy, float srcw, float srch, float dx, float dy, float ox, float oy, float scalex,
                  float scaley, float angle, uint32_t color, gfx::Blend blend) {
    if (!img) return;
    if (std::fabs(angle) < 1e-4f) {
        blit(img, srcx, srcy, srcw, srch, dx - ox * scalex, dy - oy * scaley, scalex, scaley, color, blend);
        return;
    }
    float c = std::cos(angle), s = std::sin(angle);
    float tx0 = srcx * img->fx, ty0 = srcy * img->fy;
    float tx1 = (srcx + srcw) * img->fx, ty1 = (srcy + srch) * img->fy;
    for (const ImageChunk& ch : img->chunks) {
        float cx0 = std::max(tx0, (float)ch.x), cy0 = std::max(ty0, (float)ch.y);
        float cx1 = std::min(tx1, (float)(ch.x + ch.w)), cy1 = std::min(ty1, (float)(ch.y + ch.h));
        if (cx1 <= cx0 || cy1 <= cy0) continue;
        float lx[4] = {cx0, cx1, cx1, cx0}, ly[4] = {cy0, cy0, cy1, cy1};
        float xy[8], uv[8];
        for (int k = 0; k < 4; k++) {
            float ox_l = (lx[k] - tx0) / img->fx - ox, oy_l = (ly[k] - ty0) / img->fy - oy;
            float px = ox_l * scalex, py = oy_l * scaley;
            float rx = px * c - py * s, ry = px * s + py * c;
            xy[k * 2] = sx(dx + rx);
            xy[k * 2 + 1] = sy(dy + ry);
            uv[k * 2] = lx[k] - ch.x;
            uv[k * 2 + 1] = ly[k] - ch.y;
        }
        gfx::draw_quad(ch.tex, uv, xy, color, blend, true);
    }
}

void fill(float x, float y, float w, float h, uint32_t color, gfx::Blend blend) {
    gfx::fill(std::round(sx(x)), std::round(sy(y)), std::round(sx(x + w)), std::round(sy(y + h)), color, blend);
}

void fill_screen(uint32_t color, gfx::Blend blend) { gfx::fill(0, 0, gfx::SCREEN_W, gfx::SCREEN_H, color, blend); }

void tone(int r, int g, int b, int gray) {
    if (gray > 0) gfx::desaturate(gray / 255.0f);
    int ar = std::max(0, r), ag = std::max(0, g), ab = std::max(0, b);
    int sr = std::max(0, -r), sg = std::max(0, -g), sb = std::max(0, -b);
    if (ar || ag || ab) gfx::fill(0, 0, gfx::SCREEN_W, gfx::SCREEN_H, gfx::rgba(ar, ag, ab, 255), gfx::Blend::Add);
    if (sr || sg || sb) gfx::fill(0, 0, gfx::SCREEN_W, gfx::SCREEN_H, gfx::rgba(sr, sg, sb, 255), gfx::Blend::Subtract);
}

uint32_t color(int alpha) {
    alpha = std::clamp(alpha, 0, 255);
    return gfx::rgba(alpha, alpha, alpha, alpha);
}

uint32_t color_rgba(int r, int g, int b, int a) {
    a = std::clamp(a, 0, 255);
    return gfx::premul(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255), a);
}

}
