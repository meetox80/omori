#include "image.h"

#include <zlib.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <list>
#include <unordered_map>
#include <unordered_set>

#include "../platform/platform.h"
#include "bytes.h"
#include "fs.h"
#include "loader.h"

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
static std::atomic<bool> g_urgent{false};
static bool g_compact = false;

constexpr uint32_t INFLATE_SLICE = 16384;
constexpr int INFLATE_PACE_MS = 4;
constexpr size_t PREFETCH_BYTES = 2u << 20;
constexpr size_t NEIGHBOR_BYTES = 4u << 20;
constexpr uint64_t PREFETCH_KEEP_TICKS = 600;
constexpr uint64_t HEADER_KEEP_TICKS = 3600;
constexpr uint64_t OOM_RETRY_TICKS = 30;
constexpr uint32_t SPLIT_BYTES = 256u << 10;
constexpr int SPLIT_SIZE = 256;

static std::unordered_map<std::string, uint64_t> g_oom_retry;

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

struct Decoded {
    struct Chunk {
        int x, y, w, h, stride, rows;
        int src_stride, src_rows, sx, sy;
        void* pixels;
        uint32_t* palette;
        uint32_t offset, csize, rsize;
    };
    int w = 0, h = 0, ow = 0, oh = 0, fmt = 0, flags = 0;
    std::vector<Chunk> chunks;
    std::vector<uint32_t> palette;
    std::string source;
    bool lazy = false;
    bool packed = false;
    bool ok = false;
    bool oom = false;
    size_t need = 0;
};

static void free_decoded(Decoded& d) {
    for (auto& c : d.chunks) {
        gfx::release(c.pixels);
        gfx::release(c.palette);
    }
    d.chunks.clear();
}

struct Pack {
    int fmt = -1;
    int stride = 0;
    int rows = 0;
    int src_stride = 0, src_rows = 0, sx = 0, sy = 0;
    bool swizzled = false;
};

static const uint8_t BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

static inline int dither(int v, int max, int d) { return (v * max * 32 + (2 * d + 1) * 255) / (255 * 32); }

static inline uint16_t pack_texel(const uint8_t* s, int fmt, int d) {
    int r = s[0], g = s[1], b = s[2], a = s[3];
    if (fmt == (int)gfx::Format::RGB565) return (uint16_t)(dither(r, 31, d) | (dither(g, 63, d) << 5) | (dither(b, 31, d) << 11));
    if (fmt == (int)gfx::Format::RGBA5551) {
        if (a * 32 <= (2 * d + 1) * 255) return 0;
        if (a < 255) {
            r = std::min(255, r * 255 / a);
            g = std::min(255, g * 255 / a);
            b = std::min(255, b * 255 / a);
        }
        return (uint16_t)(dither(r, 31, d) | (dither(g, 31, d) << 5) | (dither(b, 31, d) << 10) | 0x8000);
    }
    int a4 = (a * 15 + 127) / 255;
    int r4 = std::min(dither(r, 15, d), a4), g4 = std::min(dither(g, 15, d), a4), b4 = std::min(dither(b, 15, d), a4);
    return (uint16_t)(r4 | (g4 << 4) | (b4 << 8) | (a4 << 12));
}

static void pack_texels(const uint8_t* src, uint32_t len, uint32_t at, void* dst, const Pack& pack) {
    uint16_t* out = (uint16_t*)dst + at / 4;
    uint32_t stride = (uint32_t)pack.stride;
    if (!pack.swizzled) {
        uint32_t x = (at / 4) % stride, y = (at / 4) / stride;
        for (uint32_t k = 0; k < len / 4; k++) {
            out[k] = pack_texel(src + k * 4, pack.fmt, BAYER[((y & 3) << 2) | (x & 3)]);
            if (++x == stride) {
                x = 0;
                y++;
            }
        }
        return;
    }
    uint32_t units = (uint32_t)(pack.src_stride ? pack.src_stride : pack.stride) / 8;
    for (uint32_t u = 0; u < len / 256; u++) {
        uint32_t k = at / 256 + u;
        uint32_t y0 = (k / units) * 8;
        uint16_t* o = (uint16_t*)dst + (size_t)k * 64;
        if (pack.src_stride) {
            uint32_t x0 = (k % units) * 8;
            if (y0 < (uint32_t)pack.sy || y0 >= (uint32_t)(pack.sy + pack.rows) || x0 < (uint32_t)pack.sx || x0 >= (uint32_t)pack.sx + stride) continue;
            o = (uint16_t*)dst + (size_t)(((y0 - pack.sy) / 8) * (stride / 8) + (x0 - pack.sx) / 8) * 64;
        }
        const uint8_t* s = src + u * 256;
        for (int r = 0; r < 8; r++) {
            const uint8_t* row = BAYER + ((y0 + r) & 3) * 4;
            for (int i = 0; i < 8; i++) o[r * 8 + i] = pack_texel(s + (i < 4 ? r * 16 + i * 4 : 128 + r * 16 + (i - 4) * 4), pack.fmt, row[i & 3]);
        }
    }
}

static bool inflate_range(fs::DataFile* f, uint32_t off, uint32_t csize, void* dst, uint32_t bytes, bool paced, const Pack& pack = Pack()) {
    if (fs::data_seek(f, off, SEEK_SET) != 0) return false;
    bool packing = pack.fmt >= 0;
    uint32_t rsize = pack.src_stride ? (uint32_t)pack.src_stride * (uint32_t)pack.src_rows * 4 : packing ? bytes * 2 : bytes;
    uint32_t need = pack.src_stride ? (uint32_t)pack.src_stride * (uint32_t)(pack.sy + pack.rows) * 4 : UINT32_MAX;
    uint8_t* stage = nullptr;
    if (packing) {
        stage = (uint8_t*)std::malloc(INFLATE_SLICE);
        if (!stage) return false;
    }
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) {
        std::free(stage);
        return false;
    }
    uint8_t in[16384];
    uint32_t slice = paced || packing ? INFLATE_SLICE : rsize;
    zs.next_out = packing ? stage : (Bytef*)dst;
    zs.avail_out = std::min(rsize, slice);
    uint32_t packed = 0;
    uint32_t left = csize;
    int ret = Z_OK;
    while (ret != Z_STREAM_END && packed < need) {
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
        if (packing && (zs.avail_out == 0 || ret == Z_STREAM_END)) {
            uint32_t n = (uint32_t)zs.total_out - packed;
            pack_texels(stage, n, packed, dst, pack);
            packed += n;
            zs.next_out = stage;
        }
        if (zs.avail_out == 0 && zs.total_out < rsize) {
            if (paced && !g_urgent.load(std::memory_order_relaxed)) plat::sleep_ms(INFLATE_PACE_MS);
            zs.avail_out = std::min(rsize - (uint32_t)zs.total_out, slice);
        }
    }
    bool ok = pack.src_stride ? packed >= need : ret == Z_STREAM_END && zs.total_out == rsize;
    inflateEnd(&zs);
    std::free(stage);
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
    if (g_compact && out.fmt == (int)gfx::Format::RGBA8888 && (out.flags & 32) && !has_pal) {
        uint8_t at[4], head[4];
        if (fs::data_read(f, at, 4) == 4 && fs::data_seek(f, rd32(at), SEEK_SET) == 0 && fs::data_read(f, head, 4) == 4) {
            int alt_count = rd16(head);
            std::vector<uint8_t> alt((size_t)alt_count * 24);
            if (fs::data_read(f, pal, 1024) == 1024 && (alt_count == 0 || fs::data_read(f, alt.data(), alt.size()) == alt.size())) {
                count = alt_count;
                table.swap(alt);
                out.fmt = (int)gfx::Format::T8;
                has_pal = true;
            }
        }
    }
    if (g_compact && out.fmt == (int)gfx::Format::RGBA8888) {
        out.packed = true;
        out.fmt = (int)((out.flags & 4) ? gfx::Format::RGB565 : (out.flags & 24) ? gfx::Format::RGBA5551 : gfx::Format::RGBA4444);
    }
    uint32_t shift = out.packed ? 1 : 0;
    bool split = out.packed && (out.flags & 1);
    bool any_split = false;
    for (int k = 0; split && k < count; k++) any_split |= (rd32(&table[(size_t)k * 24 + 20]) >> shift) > SPLIT_BYTES;
    if (count > 1 || any_split) {
        out.lazy = true;
        out.source = rel;
        if (has_pal) {
            out.palette.resize(256);
            for (int k = 0; k < 256; k++) out.palette[(size_t)k] = bytes::le32(pal + k * 4);
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
            c.offset = rd32(e + 12);
            c.csize = rd32(e + 16);
            c.rsize = rd32(e + 20) >> shift;
            if (!split || c.rsize <= SPLIT_BYTES) {
                out.chunks.push_back(c);
                continue;
            }
            for (int oy = 0; oy < c.h; oy += SPLIT_SIZE) {
                for (int ox = 0; ox < c.w; ox += SPLIT_SIZE) {
                    Decoded::Chunk s = c;
                    s.x = c.x + ox;
                    s.y = c.y + oy;
                    s.w = std::min(SPLIT_SIZE, c.w - ox);
                    s.h = std::min(SPLIT_SIZE, c.h - oy);
                    s.stride = (s.w + 15) & ~15;
                    s.rows = (s.h + 7) & ~7;
                    s.rsize = (uint32_t)s.stride * (uint32_t)s.rows * 2;
                    s.src_stride = c.stride;
                    s.src_rows = c.rows;
                    s.sx = ox;
                    s.sy = oy;
                    out.chunks.push_back(s);
                }
            }
        }
        fs::data_close(f);
        out.ok = true;
        return out;
    }
    if (main_thread) {
        size_t need = 0;
        for (int k = 0; k < count; k++) need += (rd32(&table[(size_t)k * 24 + 20]) >> shift) + (has_pal ? 1024 : 0);
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
        uint32_t off = rd32(e + 12), csize = rd32(e + 16), rsize = rd32(e + 20) >> shift;
        Pack pack;
        if (out.packed) {
            pack.fmt = out.fmt;
            pack.stride = c.stride;
            pack.swizzled = (out.flags & 1) != 0;
        }
        c.pixels = main_thread ? alloc_pixels(rsize) : gfx::alloc(rsize);
        if (!c.pixels || !inflate_range(f, off, csize, c.pixels, rsize, !main_thread, pack)) {
            if (!c.pixels) {
                out.oom = true;
                out.need = rsize;
            }
            gfx::release(c.pixels);
            free_decoded(out);
            fs::data_close(f);
            return out;
        }
        c.palette = nullptr;
        if (has_pal) {
            c.palette = (uint32_t*)gfx::alloc(1024);
            if (!c.palette) {
                out.oom = true;
                out.need = 1024;
                gfx::release(c.pixels);
                free_decoded(out);
                fs::data_close(f);
                return out;
            }
            for (int k = 0; k < 256; k++) c.palette[k] = bytes::le32(pal + k * 4);
        }
        out.chunks.push_back(c);
        if (main_thread) loader::pump();
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
    img->packed = d.packed;
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
        ic.src_stride = (uint16_t)c.src_stride;
        ic.src_rows = (uint16_t)c.src_rows;
        ic.sx = (uint16_t)c.sx;
        ic.sy = (uint16_t)c.sy;
        ic.tex = nullptr;
        if (!d.lazy) {
            ic.tex = gfx::texture_create(c.w, c.h, c.stride, c.rows, (gfx::Format)d.fmt, img->swizzled, c.pixels, c.palette);
            c.pixels = nullptr;
            c.palette = nullptr;
            if (!ic.tex) {
                d.oom = true;
                break;
            }
            img->bytes += gfx::texture_bytes(ic.tex);
        }
        img->chunks.push_back(ic);
    }
    if (d.oom) {
        free_decoded(d);
        delete img;
        return nullptr;
    }
    d.chunks.clear();
    return img;
}

static Image* load_data(const std::string& rel, bool& oom) {
    Decoded d = decode_data(rel, true);
    Image* img = finish(d);
    oom = d.oom;
    return img;
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

static Pack chunk_pack(const Image* img, const ImageChunk& c) {
    Pack p;
    if (img->packed) {
        p.fmt = img->fmt;
        p.stride = c.stride;
        p.rows = c.rows;
        p.src_stride = c.src_stride;
        p.src_rows = c.src_rows;
        p.sx = c.sx;
        p.sy = c.sy;
        p.swizzled = img->swizzled;
    }
    return p;
}

static void make_room(size_t need, bool force);
static void* take_chunk(const std::string& source, size_t index);
static void prefetch_neighbors(const Image* img, size_t index);

static void log_chunk_skip(const Image* img, size_t index) {
    static std::unordered_set<std::string> logged;
    if (!logged.insert(img->source + "#" + std::to_string(index)).second) return;
    plat::log("image %s chunk %u skipped: out of memory", img->source.c_str(), (unsigned)index);
}

gfx::Texture* chunk_texture(const Image* cimg, size_t index) {
    Image* img = const_cast<Image*>(cimg);
    if (!img || index >= img->chunks.size()) return nullptr;
    ImageChunk& c = img->chunks[index];
    if (c.tex || !img->lazy) return c.tex;
    size_t need = c.rsize + (img->palette.empty() ? 0 : 1024);
    make_room(need, false);
    void* pixels = take_chunk(img->source, index);
    if (!pixels) {
        loader::pump();
        fs::DataFile* f = fs::open_data(img->source);
        if (!f) return nullptr;
        pixels = alloc_pixels(c.rsize);
        bool ok = pixels && inflate_range(f, c.offset, c.csize, pixels, c.rsize, false, chunk_pack(img, c));
        fs::data_close(f);
        if (!ok) {
            gfx::release(pixels);
            if (!pixels) log_chunk_skip(img, index);
            return nullptr;
        }
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
    prefetch_neighbors(img, index);
    return c.tex;
}

static bool over_heap(size_t need, size_t freed) {
    size_t limit = plat::heap_limit();
    if (!limit) return false;
    size_t used = plat::heap_used();
    used = used > freed ? used - freed : 0;
    return used + need + profile::HEAP_RESERVE > limit;
}

static void drop_chunks(const std::string* source);

static size_t drop_entry(std::unordered_map<std::string, Entry>::iterator it) {
    size_t bytes = it->second.img ? it->second.img->bytes : 0;
    g_used -= bytes;
    if (it->second.img && it->second.img->lazy) drop_chunks(&it->second.img->source);
    delete it->second.img;
    g_cache.erase(it);
    g_generation++;
    return bytes;
}

static size_t evict_one() {
    auto victim = g_cache.end();
    for (auto it = g_cache.begin(); it != g_cache.end(); ++it) {
        if (!it->second.img || it->second.pinned || it->second.last + 2 > g_tick) continue;
        if (!it->second.img->bytes && it->second.last + HEADER_KEEP_TICKS > g_tick) continue;
        if (victim == g_cache.end() || it->second.last < victim->second.last) victim = it;
    }
    if (victim == g_cache.end()) return 0;
    size_t bytes = drop_entry(victim);
    return bytes ? bytes : 1;
}

static void evict(size_t need) {
    size_t freed = 0;
    while (g_used + need > g_budget || (g_used > 0 && over_heap(need, freed))) {
        size_t bytes = evict_one();
        if (!bytes) return;
        freed += bytes;
        if (over_heap(need, 0) && gfx::reclaim_memory()) freed = 0;
    }
}

static void make_room(size_t need, bool force) {
    if (!force) {
        evict(need);
        return;
    }
    drop_chunks(nullptr);
    size_t saved = g_budget;
    g_budget = 0;
    evict(need);
    g_budget = saved;
    gfx::reclaim_memory();
}

void* alloc_pixels(size_t bytes) {
    void* p = gfx::alloc(bytes);
    if (p) return p;
    drop_chunks(nullptr);
    gfx::reclaim_memory();
    p = gfx::alloc(bytes);
    while (!p && evict_one()) {
        gfx::reclaim_memory();
        p = gfx::alloc(bytes);
    }
    return p;
}

static plat::Mutex* g_mutex = nullptr;
static std::vector<std::string> g_queue;
static std::string g_working;
static std::vector<std::pair<std::string, Decoded>> g_ready;
static bool g_worker = false;
static std::unordered_set<std::string> g_retried;

struct ChunkJob {
    std::string source;
    size_t index = 0;
    uint32_t offset = 0, csize = 0, rsize = 0;
    Pack pack;
};

struct ChunkReady {
    std::string source;
    size_t index;
    uint32_t rsize;
    void* pixels;
    uint64_t tick;
};

static std::vector<ChunkJob> g_chunk_queue;
static ChunkJob g_chunk_working;
static bool g_chunk_busy = false;
static std::vector<ChunkReady> g_chunk_ready;
static size_t g_chunk_bytes = 0;

static void* decode_chunk(const ChunkJob& job) {
    fs::DataFile* f = fs::open_data(job.source);
    if (!f) return nullptr;
    void* pixels = gfx::alloc(job.rsize);
    bool ok = pixels && inflate_range(f, job.offset, job.csize, pixels, job.rsize, true, job.pack);
    fs::data_close(f);
    if (ok) return pixels;
    gfx::release(pixels);
    return nullptr;
}

static void worker() {
    for (;;) {
        std::string job;
        ChunkJob chunk;
        bool has_chunk = false;
        plat::mutex_lock(g_mutex);
        if (!g_queue.empty()) {
            job = g_queue.front();
            g_queue.erase(g_queue.begin());
            g_working = job;
        } else if (!g_chunk_queue.empty()) {
            chunk = g_chunk_queue.front();
            g_chunk_queue.erase(g_chunk_queue.begin());
            g_chunk_working = chunk;
            g_chunk_busy = has_chunk = true;
        }
        plat::mutex_unlock(g_mutex);
        if (has_chunk) {
            void* pixels = decode_chunk(chunk);
            plat::mutex_lock(g_mutex);
            g_chunk_busy = false;
            if (pixels) g_chunk_ready.push_back({chunk.source, chunk.index, chunk.rsize, pixels, 0});
            else g_chunk_bytes -= chunk.rsize;
            plat::mutex_unlock(g_mutex);
            continue;
        }
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

static void* take_chunk(const std::string& source, size_t index) {
    if (!g_worker) return nullptr;
    void* pixels = nullptr;
    for (;;) {
        plat::mutex_lock(g_mutex);
        for (auto it = g_chunk_ready.begin(); it != g_chunk_ready.end(); ++it) {
            if (it->index == index && it->source == source) {
                pixels = it->pixels;
                g_chunk_bytes -= it->rsize;
                g_chunk_ready.erase(it);
                break;
            }
        }
        for (auto it = g_chunk_queue.begin(); it != g_chunk_queue.end(); ++it) {
            if (it->index == index && it->source == source) {
                g_chunk_bytes -= it->rsize;
                g_chunk_queue.erase(it);
                break;
            }
        }
        bool working = g_chunk_busy && g_chunk_working.index == index && g_chunk_working.source == source;
        plat::mutex_unlock(g_mutex);
        if (pixels || !working) break;
        g_urgent.store(true, std::memory_order_relaxed);
        plat::sleep_ms(1);
    }
    g_urgent.store(false, std::memory_order_relaxed);
    return pixels;
}

static bool chunk_known(const std::string& source, size_t index) {
    if (g_chunk_busy && g_chunk_working.index == index && g_chunk_working.source == source) return true;
    for (auto& j : g_chunk_queue) {
        if (j.index == index && j.source == source) return true;
    }
    for (auto& r : g_chunk_ready) {
        if (r.index == index && r.source == source) return true;
    }
    return false;
}

static void prefetch_neighbors(const Image* img, size_t index) {
    ensure_worker();
    const ImageChunk& c = img->chunks[index];
    plat::mutex_lock(g_mutex);
    for (size_t k = 0; k < img->chunks.size(); k++) {
        const ImageChunk& n = img->chunks[k];
        if (k == index || n.tex) continue;
        if (n.x > c.x + c.w || c.x > n.x + n.w || n.y > c.y + c.h || c.y > n.y + n.h) continue;
        if (chunk_known(img->source, k)) continue;
        if (g_chunk_bytes + n.rsize > NEIGHBOR_BYTES || over_heap(g_chunk_bytes + n.rsize, 0)) break;
        ChunkJob job;
        job.source = img->source;
        job.index = k;
        job.offset = n.offset;
        job.csize = n.csize;
        job.rsize = n.rsize;
        job.pack = chunk_pack(img, n);
        g_chunk_queue.insert(g_chunk_queue.begin(), job);
        g_chunk_bytes += n.rsize;
    }
    plat::mutex_unlock(g_mutex);
}

static void drop_chunks(const std::string* source) {
    if (!g_worker) return;
    plat::mutex_lock(g_mutex);
    for (auto it = g_chunk_ready.begin(); it != g_chunk_ready.end();) {
        if (source && it->source != *source) {
            ++it;
            continue;
        }
        gfx::release(it->pixels);
        g_chunk_bytes -= it->rsize;
        it = g_chunk_ready.erase(it);
    }
    for (auto it = g_chunk_queue.begin(); it != g_chunk_queue.end();) {
        if (source && it->source != *source) {
            ++it;
            continue;
        }
        g_chunk_bytes -= it->rsize;
        it = g_chunk_queue.erase(it);
    }
    plat::mutex_unlock(g_mutex);
}

struct WarmImage {
    std::string key;
    std::vector<ChunkJob> jobs;
};

static std::vector<WarmImage> g_warm;

static void warm_image(const std::string& rel, const Image* img) {
    WarmImage w;
    w.key = rel;
    for (size_t k = 0; k < img->chunks.size(); k++) {
        const ImageChunk& c = img->chunks[k];
        ChunkJob job;
        job.source = img->source;
        job.index = k;
        job.offset = c.offset;
        job.csize = c.csize;
        job.rsize = c.rsize;
        job.pack = chunk_pack(img, c);
        w.jobs.push_back(job);
    }
    g_warm.push_back(std::move(w));
}

static void pump_warm() {
    while (!g_warm.empty()) {
        WarmImage& w = g_warm.front();
        auto it = g_cache.find(w.key);
        Image* img = it != g_cache.end() ? it->second.img : nullptr;
        if (!img) {
            g_warm.erase(g_warm.begin());
            continue;
        }
        while (!w.jobs.empty()) {
            const ChunkJob& job = w.jobs.front();
            if (job.index >= img->chunks.size() || img->chunks[job.index].tex) {
                w.jobs.erase(w.jobs.begin());
                continue;
            }
            plat::mutex_lock(g_mutex);
            bool known = chunk_known(job.source, job.index);
            bool room = known || ((g_chunk_bytes == 0 || g_chunk_bytes + job.rsize <= PREFETCH_BYTES) && !over_heap(g_chunk_bytes + job.rsize, 0));
            if (!known && room) {
                g_chunk_queue.push_back(job);
                g_chunk_bytes += job.rsize;
            }
            plat::mutex_unlock(g_mutex);
            if (!room) return;
            w.jobs.erase(w.jobs.begin());
        }
        g_warm.erase(g_warm.begin());
    }
}

static void expire_chunks() {
    if (!g_worker) return;
    plat::mutex_lock(g_mutex);
    for (auto it = g_chunk_ready.begin(); it != g_chunk_ready.end();) {
        const std::string& src = it->source;
        bool cached = src.size() > 4 && g_cache.count(src.substr(0, src.size() - 4));
        if (!it->tick || cached) it->tick = g_tick;
        if (g_tick - it->tick <= PREFETCH_KEEP_TICKS) {
            ++it;
            continue;
        }
        gfx::release(it->pixels);
        g_chunk_bytes -= it->rsize;
        it = g_chunk_ready.erase(it);
    }
    plat::mutex_unlock(g_mutex);
}

static void insert(const std::string& rel, Image* img) {
    Entry e;
    e.img = img;
    e.last = g_tick;
    e.missing = img == nullptr;
    g_retried.erase(rel);
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
    std::vector<std::string> requeue;
    plat::mutex_lock(g_mutex);
    ready.swap(g_ready);
    plat::mutex_unlock(g_mutex);
    for (auto& r : ready) {
        if (g_cache.count(r.first) || !r.second.ok) {
            free_decoded(r.second);
            if (r.second.oom && !g_cache.count(r.first) && g_retried.insert(r.first).second) {
                void* room = alloc_pixels(r.second.need);
                if (room) {
                    gfx::release(room);
                    requeue.push_back(r.first);
                }
            }
            continue;
        }
        Image* img = finish(r.second);
        if (!img && r.second.oom) {
            if (g_retried.insert(r.first).second) requeue.push_back(r.first);
            continue;
        }
        insert(r.first, img);
        if (img && img->lazy) warm_image(r.first, img);
    }
    for (auto& rel : requeue) prefetch_path(rel);
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
            g_urgent.store(false, std::memory_order_relaxed);
            collect_ready(&rel);
            return true;
        }
        if (!pending) {
            g_urgent.store(false, std::memory_order_relaxed);
            return false;
        }
        g_urgent.store(true, std::memory_order_relaxed);
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
    auto retry = g_oom_retry.find(rel);
    if (retry != g_oom_retry.end() && retry->second > g_tick) return nullptr;
    bool oom = false;
    Image* img = load_data(rel + ".tex", oom);
    if (!img && oom) {
        if (retry == g_oom_retry.end()) plat::log("image %s skipped: out of memory", rel.c_str());
        g_oom_retry[rel] = g_tick + OOM_RETRY_TICKS;
        return nullptr;
    }
    if (retry != g_oom_retry.end()) {
        plat::log("image %s loaded after running out of memory", rel.c_str());
        g_oom_retry.erase(retry);
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
    w = h = 0;
    if (name.empty()) return false;
    return logical_size_path(resolve(folder, name, ".png"), w, h);
}

bool logical_size_path(const std::string& rel, int& w, int& h) {
    static std::unordered_map<std::string, std::pair<int, int>> sizes;
    w = h = 0;
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

void drop(const std::string& folder, const std::string& name) {
    auto it = g_cache.find(resolve(folder, name, ".png"));
    if (it != g_cache.end() && !it->second.pinned) drop_entry(it);
}

void set_budget(size_t bytes) { g_budget = bytes; }
void set_compact(bool on) { g_compact = on; }
void trim() { evict(0); }
bool evict_lru() { return evict_one() != 0; }
bool waiting_for_memory(const std::string& rel) { return g_oom_retry.count(rel) != 0; }
void tick() {
    g_tick++;
    collect_ready(nullptr);
    expire_chunks();
    pump_warm();
}

void release_memory() {
    make_room(SIZE_MAX / 2, true);
}

void forget_recent() {
    for (auto& kv : g_cache) kv.second.last = 0;
}

void clear() {
    g_generation++;
    g_warm.clear();
    g_retried.clear();
    g_oom_retry.clear();
    drop_chunks(nullptr);
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
