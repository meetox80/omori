#include "../gfx.h"
#include "../platform.h"

#include <malloc.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <pspgu.h>
#include <pspkernel.h>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gfx {

struct Texture {
    int w = 0;
    int h = 0;
    int stride = 0;
    int rows = 0;
    int tw = 0;
    int th = 0;
    int psm = 0;
    bool swizzled = false;
    void* pixels = nullptr;
    uint32_t* palette = nullptr;
    size_t bytes = 0;
    bool owned = true;
};

struct Vtx {
    float u, v;
    uint32_t color;
    float x, y, z;
};

constexpr int BUF_W = 512;
constexpr int MAX_VTX = 4096;

constexpr int LIST_WORDS = 65536;
static unsigned int __attribute__((aligned(64))) g_lists[2][LIST_WORDS];
static int g_list_index = 0;
static bool g_gpu_busy = false;
static bool g_frame_pending = false;
static bool g_list_open = false;
static int g_list_peak = 0;
static int g_batches = 0;
static void wait_gpu();
void present();
static Vtx g_batch[MAX_VTX];
static int g_count = 0;
static int g_prim = GU_SPRITES;
static bool g_flat = false;
static uint32_t g_flat_rgb = 0;
static bool g_bound_flat = false;
static uint32_t g_bound_flat_rgb = 0xFFFFFFFFu;
static Texture* g_tex = nullptr;
static Blend g_blend = Blend::Normal;
static bool g_smooth = false;
static bool g_state_valid = false;
static Texture* g_bound_tex = nullptr;
static bool g_textured = true;
static bool g_bound_smooth = false;
static Blend g_bound_blend = Blend::Copy;
static void* g_fbp = nullptr;
static const uint32_t* g_dummy_fb = nullptr;
static std::vector<Texture*> g_garbage;
static int g_draw_offset = 0;
static uint32_t __attribute__((aligned(64))) g_clut[3][256];
static int g_clut_level = -1;
constexpr int TEMP_OFFSET = BUF_W * SCREEN_H * 4 * 2;
constexpr int HALF_STRIDE = 256;
constexpr int HALF_BYTES = HALF_STRIDE * HALF_H * 4;
constexpr int HALF0_OFFSET = TEMP_OFFSET + BUF_W * SCREEN_H * 4;
constexpr int HALF1_OFFSET = HALF0_OFFSET + HALF_BYTES;
constexpr int HALF2_OFFSET = HALF1_OFFSET + HALF_BYTES;
constexpr int WIDE_OFFSET = HALF1_OFFSET;
constexpr int VRAM_END = HALF2_OFFSET + HALF_BYTES;
static_assert(VRAM_END <= 2 * 1024 * 1024, "fx surfaces exceed VRAM");
static_assert(WIDE_OFFSET + BUF_W * SCREEN_H * 2 <= 2 * 1024 * 1024, "wide surface exceeds VRAM");

struct SurfaceInfo {
    int offset;
    int psm;
    int stride;
    int w;
    int h;
};

static Texture g_surface_tex[6];
static SurfaceInfo g_surface[6];
static Surface g_target = Surface::Screen;
static uint32_t g_protect = 0;
static bool g_wrap = false;
static Stencil g_stencil = Stencil::Off;
static const ScePspIMatrix4 g_dither = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};

constexpr size_t ARENA_BYTES = 20u << 20;
constexpr size_t SMALL_ARENA_BYTES = 7u << 20;
constexpr size_t SMALL_HEAP = 32u << 20;
constexpr uint32_t ARENA_UNIT = 64;

struct Arena {
    uint8_t* base = nullptr;
    uint32_t size = 0;
    uint32_t free_bytes = 0;
    std::map<uint32_t, uint32_t> free_blocks;
    std::unordered_map<uint32_t, uint32_t> used_blocks;
    SceUID lock = -1;
};

static Arena g_arena;
static volatile SceUID g_arena_owner = -1;

static void arena_init() {
    if (g_arena.base) return;
    size_t bytes = plat::heap_limit() < SMALL_HEAP ? SMALL_ARENA_BYTES : ARENA_BYTES;
    g_arena.base = (uint8_t*)memalign(ARENA_UNIT, bytes);
    if (!g_arena.base) return;
    g_arena.size = bytes;
    g_arena.free_bytes = bytes;
    g_arena.free_blocks[0] = bytes;
    g_arena.lock = sceKernelCreateSema("arena", 0, 1, 1, nullptr);
}

static void* arena_alloc(size_t bytes) {
    if (!g_arena.base) return nullptr;
    uint32_t need = (uint32_t)((bytes + ARENA_UNIT - 1) / ARENA_UNIT * ARENA_UNIT);
    if (need == 0) need = ARENA_UNIT;
    sceKernelWaitSema(g_arena.lock, 1, nullptr);
    g_arena_owner = sceKernelGetThreadId();
    auto best = g_arena.free_blocks.end();
    for (auto it = g_arena.free_blocks.begin(); it != g_arena.free_blocks.end(); ++it) {
        if (it->second >= need && (best == g_arena.free_blocks.end() || it->second < best->second)) {
            best = it;
            if (it->second == need) break;
        }
    }
    void* out = nullptr;
    if (best != g_arena.free_blocks.end()) {
        uint32_t at = best->first, len = best->second;
        g_arena.free_blocks.erase(best);
        if (len > need) g_arena.free_blocks[at + need] = len - need;
        g_arena.used_blocks[at] = need;
        g_arena.free_bytes -= need;
        out = g_arena.base + at;
    }
    g_arena_owner = -1;
    sceKernelSignalSema(g_arena.lock, 1);
    return out;
}

static bool arena_owns(const void* p) { return g_arena.base && p >= g_arena.base && p < g_arena.base + g_arena.size; }

static void arena_release(void* p) {
    uint32_t at = (uint32_t)((uint8_t*)p - g_arena.base);
    sceKernelWaitSema(g_arena.lock, 1, nullptr);
    g_arena_owner = sceKernelGetThreadId();
    auto u = g_arena.used_blocks.find(at);
    if (u != g_arena.used_blocks.end()) {
        uint32_t len = u->second;
        g_arena.used_blocks.erase(u);
        g_arena.free_bytes += len;
        auto next = g_arena.free_blocks.lower_bound(at);
        if (next != g_arena.free_blocks.end() && next->first == at + len) {
            len += next->second;
            next = g_arena.free_blocks.erase(next);
        }
        if (next != g_arena.free_blocks.begin()) {
            auto prev = std::prev(next);
            if (prev->first + prev->second == at) {
                prev->second += len;
                len = 0;
            }
        }
        if (len) g_arena.free_blocks[at] = len;
    }
    g_arena_owner = -1;
    sceKernelSignalSema(g_arena.lock, 1);
}

size_t arena_free_bytes() { return g_arena.free_bytes; }

void* arena_try_alloc(size_t bytes) {
    if (g_arena_owner == sceKernelGetThreadId()) return nullptr;
    arena_init();
    return arena_alloc(bytes);
}

void* alloc(size_t bytes) {
    arena_init();
    if (void* p = arena_alloc(bytes)) return p;
    return memalign(64, bytes ? bytes : 16);
}

void release(void* p) {
    if (!p) return;
    if (arena_owns(p)) arena_release(p);
    else free(p);
}

static int pow2(int n) {
    int p = 8;
    while (p < n) p <<= 1;
    return p;
}

static int psm_of(Format f) {
    switch (f) {
        case Format::RGBA8888: return GU_PSM_8888;
        case Format::RGBA4444: return GU_PSM_4444;
        case Format::RGB565: return GU_PSM_5650;
        case Format::RGBA5551: return GU_PSM_5551;
        case Format::T8: return GU_PSM_T8;
    }
    return GU_PSM_8888;
}

static int bpp_of(Format f) {
    switch (f) {
        case Format::RGBA8888: return 4;
        case Format::T8: return 1;
        default: return 2;
    }
}

Texture* texture_create(int w, int h, int stride, int rows, Format fmt, bool swizzled, void* pixels, uint32_t* palette) {
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    t->stride = stride;
    t->rows = rows;
    t->tw = pow2(w);
    t->th = pow2(h);
    t->psm = psm_of(fmt);
    t->swizzled = swizzled;
    t->pixels = pixels;
    t->palette = palette;
    t->bytes = (size_t)stride * rows * bpp_of(fmt) + (palette ? 1024 : 0);
    sceKernelDcacheWritebackRange(pixels, (unsigned int)((size_t)stride * rows * bpp_of(fmt)));
    if (palette) sceKernelDcacheWritebackRange(palette, 1024);
    return t;
}

Texture* texture_wrap(int w, int h, int stride, uint32_t* pixels) {
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    t->stride = stride;
    t->rows = h;
    t->tw = pow2(w);
    t->th = pow2(h);
    t->psm = GU_PSM_8888;
    t->swizzled = false;
    t->pixels = pixels;
    t->owned = false;
    t->bytes = 0;
    return t;
}

void texture_touch(Texture* t) {
    if (!t || !t->pixels) return;
    sceKernelDcacheWritebackRange(t->pixels, (unsigned int)((size_t)t->stride * t->rows * 4));
}

bool texture_read(const Texture* t, int x, int y, uint32_t* out) {
    if (!t || !t->pixels || x < 0 || y < 0 || x >= t->w || y >= t->h) return false;
    if (!t->owned && g_gpu_busy) wait_gpu();
    const uint8_t* p = (const uint8_t*)t->pixels;
    if (t->psm == GU_PSM_8888) {
        int bw = t->stride * 4;
        size_t off;
        int xb = x * 4;
        if (t->swizzled) off = ((size_t)(y / 8) * (bw / 16) + (xb / 16)) * 128 + (y % 8) * 16 + (xb % 16);
        else off = (size_t)y * bw + xb;
        *out = (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8) | ((uint32_t)p[off + 2] << 16) | ((uint32_t)p[off + 3] << 24);
        return true;
    }
    if (t->psm == GU_PSM_5650 || t->psm == GU_PSM_5551 || t->psm == GU_PSM_4444) {
        int bw = t->stride * 2;
        size_t off;
        int xb = x * 2;
        if (t->swizzled) off = ((size_t)(y / 8) * (bw / 16) + (xb / 16)) * 128 + (y % 8) * 16 + (xb % 16);
        else off = (size_t)y * bw + xb;
        uint32_t v = (uint32_t)p[off] | ((uint32_t)p[off + 1] << 8);
        uint32_t r, g, b, a;
        if (t->psm == GU_PSM_5650) {
            r = v & 31;
            g = (v >> 5) & 63;
            b = v >> 11;
            r = (r << 3) | (r >> 2);
            g = (g << 2) | (g >> 4);
            b = (b << 3) | (b >> 2);
            a = 255;
        } else if (t->psm == GU_PSM_5551) {
            r = v & 31;
            g = (v >> 5) & 31;
            b = (v >> 10) & 31;
            r = (r << 3) | (r >> 2);
            g = (g << 3) | (g >> 2);
            b = (b << 3) | (b >> 2);
            a = (v & 0x8000) ? 255 : 0;
        } else {
            r = (v & 15) * 17;
            g = ((v >> 4) & 15) * 17;
            b = ((v >> 8) & 15) * 17;
            a = (v >> 12) * 17;
        }
        *out = r | (g << 8) | (b << 16) | (a << 24);
        return true;
    }
    if (t->psm == GU_PSM_T8 && t->palette) {
        int bw = t->stride;
        size_t off;
        if (t->swizzled) off = ((size_t)(y / 8) * (bw / 16) + (x / 16)) * 128 + (y % 8) * 16 + (x % 16);
        else off = (size_t)y * bw + x;
        *out = t->palette[p[off]];
        return true;
    }
    return false;
}

void texture_destroy(Texture* t) {
    if (!t) return;
    g_garbage.push_back(t);
}

static void collect_garbage() {
    for (Texture* t : g_garbage) {
        if (g_bound_tex == t) g_bound_tex = nullptr;
        if (g_tex == t) g_tex = nullptr;
        if (t->owned) {
            release(t->pixels);
            release(t->palette);
        }
        delete t;
    }
    g_garbage.clear();
}

size_t texture_bytes(const Texture* t) { return t ? t->bytes : 0; }
int texture_width(const Texture* t) { return t ? t->w : 0; }
int texture_height(const Texture* t) { return t ? t->h : 0; }

static void setup_surfaces() {
    const SurfaceInfo info[6] = {{0, GU_PSM_8888, BUF_W, SCREEN_W, SCREEN_H},
                                 {TEMP_OFFSET, GU_PSM_8888, BUF_W, SCREEN_W, SCREEN_H},
                                 {HALF0_OFFSET, GU_PSM_8888, HALF_STRIDE, HALF_W, HALF_H},
                                 {HALF1_OFFSET, GU_PSM_8888, HALF_STRIDE, HALF_W, HALF_H},
                                 {HALF2_OFFSET, GU_PSM_8888, HALF_STRIDE, HALF_W, HALF_H},
                                 {WIDE_OFFSET, GU_PSM_5650, BUF_W, SCREEN_W, SCREEN_H}};
    uint8_t* vram = (uint8_t*)sceGeEdramGetAddr();
    for (int i = 0; i < 6; i++) {
        g_surface[i] = info[i];
        Texture& t = g_surface_tex[i];
        t.w = info[i].w;
        t.h = info[i].h;
        t.stride = info[i].stride;
        t.rows = info[i].h;
        t.tw = pow2(info[i].w);
        t.th = pow2(info[i].h);
        t.psm = info[i].psm;
        t.swizzled = false;
        t.pixels = vram + info[i].offset;
        t.palette = nullptr;
        t.bytes = 0;
        t.owned = false;
    }
}

void init() {
    sceGuInit();
    sceGuStart(GU_DIRECT, g_lists[0]);
    sceGuDrawBuffer(GU_PSM_8888, (void*)0, BUF_W);
    sceGuDispBuffer(SCREEN_W, SCREEN_H, (void*)(BUF_W * SCREEN_H * 4), BUF_W);
    sceGuOffset(2048 - (SCREEN_W / 2), 2048 - (SCREEN_H / 2));
    sceGuViewport(2048, 2048, SCREEN_W, SCREEN_H);
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_CULL_FACE);
    sceGuShadeModel(GU_SMOOTH);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuSetDither(&g_dither);
    sceGuDisable(GU_DITHER);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    setup_surfaces();
}

static void wait_gpu() {
    if (!g_gpu_busy) return;
    sceGuSync(0, 0);
    g_gpu_busy = false;
    collect_garbage();
}

void present() {
    if (!g_frame_pending) return;
    wait_gpu();
    sceDisplayWaitVblankStart();
    g_fbp = sceGuSwapBuffers();
    g_draw_offset = g_draw_offset ? 0 : BUF_W * SCREEN_H * 4;
    g_frame_pending = false;
}

int list_peak_bytes() { return g_list_peak; }

bool reclaim_memory() {
    if (g_list_open) return false;
    if (g_gpu_busy) {
        sceGuSync(0, 0);
        g_gpu_busy = false;
    }
    collect_garbage();
    return true;
}

void set_evict_handler(bool (*)()) {}

double take_batch_count() {
    double n = g_batches;
    g_batches = 0;
    return n;
}

void shutdown() {
    present();
    sceGuTerm();
}

static void apply_blend(Blend b) {
    switch (b) {
        case Blend::Normal: sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_ALPHA, 0xFFFFFF, 0); break;
        case Blend::Add: sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF); break;
        case Blend::Multiply: sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_ONE_MINUS_SRC_ALPHA, 0, 0); break;
        case Blend::Screen: sceGuBlendFunc(GU_ADD, GU_FIX, GU_ONE_MINUS_SRC_COLOR, 0xFFFFFF, 0); break;
        case Blend::Subtract: sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF); break;
        case Blend::Copy: sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0); break;
        case Blend::MulSrc: sceGuBlendFunc(GU_ADD, GU_FIX, GU_SRC_COLOR, 0, 0); break;
        case Blend::MulAdd: sceGuBlendFunc(GU_ADD, GU_DST_COLOR, GU_FIX, 0, 0xFFFFFF); break;
        case Blend::Max: sceGuBlendFunc(GU_MAX, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF); break;
        case Blend::Min: sceGuBlendFunc(GU_MIN, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF); break;
        case Blend::MulSub: sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_DST_COLOR, GU_FIX, 0, 0xFFFFFF); break;
    }
}

static void apply_flat_blend(Blend b) {
    switch (b) {
        case Blend::Add: sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFF); break;
        case Blend::Subtract: sceGuBlendFunc(GU_REVERSE_SUBTRACT, GU_SRC_ALPHA, GU_FIX, 0, 0xFFFFFF); break;
        default: sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0); break;
    }
}

static void flush() {
    if (!g_count) return;
    g_batches++;
    if (!g_state_valid || g_bound_blend != g_blend || g_bound_flat != g_flat) {
        if (g_flat) apply_flat_blend(g_blend);
        else apply_blend(g_blend);
        g_bound_blend = g_blend;
    }
    if (!g_state_valid || g_bound_flat != g_flat || (g_flat && g_bound_flat_rgb != g_flat_rgb)) {
        if (g_flat) {
            sceGuTexFunc(GU_TFX_BLEND, GU_TCC_RGBA);
            sceGuTexEnvColor(g_flat_rgb);
        } else {
            sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
        }
        g_bound_flat = g_flat;
        g_bound_flat_rgb = g_flat_rgb;
    }
    if (g_tex) {
        if (!g_textured || !g_state_valid) {
            sceGuEnable(GU_TEXTURE_2D);
            g_textured = true;
        }
        if (g_bound_tex != g_tex || !g_state_valid) {
            Texture* t = g_tex;
            if (t->palette) {
                sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
                sceGuClutLoad(256 / 8, t->palette);
            }
            sceGuTexMode(t->psm, 0, 0, t->swizzled ? 1 : 0);
            sceGuTexImage(0, t->tw, t->th, t->stride, t->pixels);
            g_bound_tex = t;
        }
        if (g_bound_smooth != g_smooth || !g_state_valid) {
            sceGuTexFilter(g_smooth ? GU_LINEAR : GU_NEAREST, g_smooth ? GU_LINEAR : GU_NEAREST);
            g_bound_smooth = g_smooth;
        }
    } else if (g_textured || !g_state_valid) {
        sceGuDisable(GU_TEXTURE_2D);
        g_textured = false;
    }
    g_state_valid = true;
    Vtx* mem = (Vtx*)sceGuGetMemory(g_count * sizeof(Vtx));
    std::memcpy(mem, g_batch, g_count * sizeof(Vtx));
    sceGuDrawArray(g_prim, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, g_count, 0, mem);
    g_count = 0;
}

static void prepare(Texture* tex, Blend blend, bool smooth, int prim, int verts, bool flat = false, uint32_t flat_rgb = 0) {
    if (g_count && (g_tex != tex || g_blend != blend || g_smooth != smooth || g_prim != prim || g_flat != flat || (flat && g_flat_rgb != flat_rgb) ||
                    g_count + verts > MAX_VTX))
        flush();
    g_tex = tex;
    g_blend = blend;
    g_smooth = smooth;
    g_prim = prim;
    g_flat = flat;
    g_flat_rgb = flat_rgb;
}

void begin_frame(uint32_t clear_color) {
    present();
    sceGuStart(GU_DIRECT, g_lists[g_list_index]);
    g_list_index ^= 1;
    g_list_open = true;
    sceGuTexFlush();
    g_state_valid = false;
    g_bound_tex = nullptr;
    g_target = Surface::Screen;
    sceGuDrawBufferList(GU_PSM_8888, (void*)g_draw_offset, BUF_W);
    sceGuDisable(GU_DITHER);
    sceGuDisable(GU_STENCIL_TEST);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuPixelMask(0);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    g_protect = 0;
    g_wrap = false;
    g_stencil = Stencil::Off;
    sceGuScissor(0, 0, SCREEN_W, SCREEN_H);
    sceGuClearColor(clear_color);
    sceGuClear(GU_COLOR_BUFFER_BIT);
}

void end_frame() {
    flush();
    if (g_target != Surface::Screen) set_target(Surface::Screen);
    if (g_stencil != Stencil::Off) set_stencil(Stencil::Off);
    if (g_protect) set_color_mask(0);
    sceKernelDcacheWritebackAll();
    g_list_peak = std::max(g_list_peak, sceGuCheckList());
    sceGuFinish();
    g_list_open = false;
    g_gpu_busy = true;
    g_frame_pending = true;
}

const uint32_t* framebuffer() { return g_dummy_fb; }

bool capture_row(int y, int x, int w, uint32_t* out) {
    if (y < 0 || y >= SCREEN_H || x < 0 || x + w > SCREEN_W) return false;
    void* top = nullptr;
    int bw = 0, fmt = 0;
    if (g_frame_pending) {
        wait_gpu();
        top = (uint8_t*)sceGeEdramGetAddr() + g_draw_offset;
        bw = BUF_W;
    } else {
        sceDisplayGetFrameBuf(&top, &bw, &fmt, PSP_DISPLAY_SETBUF_NEXTFRAME);
    }
    if (!top) return false;
    const uint32_t* px = (const uint32_t*)((uintptr_t)top | 0x40000000u);
    std::memcpy(out, px + (size_t)y * bw + x, (size_t)w * 4);
    return true;
}

bool capture(uint32_t* out, int out_stride) {
    void* top = nullptr;
    int bw = 0, fmt = 0;
    if (g_frame_pending) {
        wait_gpu();
        top = (uint8_t*)sceGeEdramGetAddr() + g_draw_offset;
        bw = BUF_W;
    } else {
        sceDisplayGetFrameBuf(&top, &bw, &fmt, PSP_DISPLAY_SETBUF_NEXTFRAME);
    }
    if (!top) return false;
    const uint32_t* px = (const uint32_t*)((uintptr_t)top | 0x40000000u);
    for (int y = 0; y < SCREEN_H; y++) std::memcpy(out + (size_t)y * out_stride, px + (size_t)y * bw, SCREEN_W * 4);
    return true;
}

void set_scissor(int x, int y, int w, int h) {
    flush();
    int sw = g_surface[(int)g_target].w, sh = g_surface[(int)g_target].h;
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > sw) x1 = sw;
    if (y1 > sh) y1 = sh;
    if (x1 <= x || y1 <= y) {
        sceGuScissor(sw, 0, 1, 1);
        return;
    }
    sceGuScissor(x, y, x1 - x, y1 - y);
}

void reset_scissor() {
    flush();
    sceGuScissor(0, 0, g_surface[(int)g_target].w, g_surface[(int)g_target].h);
}

static inline void clamp_uv(const Texture* t, bool smooth, float& u, float& v) {
    if (!smooth) return;
    float mu = (float)t->w - 0.5f, mv = (float)t->h - 0.5f;
    if (u > mu) u = mu;
    if (v > mv) v = mv;
}

void sprite(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    clamp_uv(tex, smooth, u0, v0);
    clamp_uv(tex, smooth, u1, v1);
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    prepare(tex, blend, smooth, GU_SPRITES, 2);
    Vtx* v = &g_batch[g_count];
    v[0] = {u0, v0, color, x0, y0, 0};
    v[1] = {u1, v1, color, x1, y1, 0};
    g_count += 2;
}

void draw_quad(Texture* tex, const float uv[8], const float xy[8], uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    prepare(tex, blend, smooth, GU_TRIANGLES, 6);
    Vtx* v = &g_batch[g_count];
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        int k = idx[i];
        float u = uv[k * 2], vv = uv[k * 2 + 1];
        clamp_uv(tex, smooth, u, vv);
        v[i] = {u, vv, color, xy[k * 2], xy[k * 2 + 1], 0};
    }
    g_count += 6;
}

void sprite_flat(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t rgb, int alpha, Blend blend,
                 bool smooth) {
    if (!tex || alpha <= 0) return;
    clamp_uv(tex, smooth, u0, v0);
    clamp_uv(tex, smooth, u1, v1);
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    uint32_t c = (rgb & 0xFFFFFFu) | ((uint32_t)(alpha > 255 ? 255 : alpha) << 24);
    prepare(tex, blend, smooth, GU_SPRITES, 2, true, rgb & 0xFFFFFFu);
    Vtx* v = &g_batch[g_count];
    v[0] = {u0, v0, c, x0, y0, 0};
    v[1] = {u1, v1, c, x1, y1, 0};
    g_count += 2;
}

void quad_flat(Texture* tex, const float uv[8], const float xy[8], uint32_t rgb, int alpha, Blend blend, bool smooth) {
    if (!tex || alpha <= 0) return;
    uint32_t c = (rgb & 0xFFFFFFu) | ((uint32_t)(alpha > 255 ? 255 : alpha) << 24);
    prepare(tex, blend, smooth, GU_TRIANGLES, 6, true, rgb & 0xFFFFFFu);
    Vtx* v = &g_batch[g_count];
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        int k = idx[i];
        float u = uv[k * 2], vv = uv[k * 2 + 1];
        clamp_uv(tex, smooth, u, vv);
        v[i] = {u, vv, c, xy[k * 2], xy[k * 2 + 1], 0};
    }
    g_count += 6;
}

void fill(float x0, float y0, float x1, float y1, uint32_t color, Blend blend) {
    prepare(nullptr, blend, false, GU_SPRITES, 2);
    Vtx* v = &g_batch[g_count];
    v[0] = {0, 0, color, x0, y0, 0};
    v[1] = {0, 0, color, x1, y1, 0};
    g_count += 2;
}

void desaturate(float amount) {
    if (amount <= 0) return;
    if (amount > 1) amount = 1;
    flush();
    int level = (int)(amount * 255.0f + 0.5f);
    if (level != g_clut_level) {
        g_clut_level = level;
        for (int v = 0; v < 256; v++) {
            uint32_t c = (uint32_t)((v * level) / (255 * 3));
            uint32_t e = c | (c << 8) | (c << 16) | 0xFF000000u;
            g_clut[0][v] = e;
            g_clut[1][v] = e;
            g_clut[2][v] = e;
        }
        sceKernelDcacheWritebackRange(g_clut, sizeof(g_clut));
    }
    uint8_t* vram = (uint8_t*)sceGeEdramGetAddr();
    void* fb = vram + g_draw_offset;
    void* tmp = vram + TEMP_OFFSET;
    sceGuCopyImage(GU_PSM_8888, 0, 0, SCREEN_W, SCREEN_H, BUF_W, fb, 0, 0, BUF_W, tmp);
    sceGuTexSync();
    sceGuTexFlush();
    int keep = 255 - level;
    sceGuDisable(GU_TEXTURE_2D);
    sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0, (unsigned)(keep | (keep << 8) | (keep << 16)));
    Vtx* f = (Vtx*)sceGuGetMemory(2 * sizeof(Vtx));
    f[0] = {0, 0, 0xFFFFFFFFu, 0, 0, 0};
    f[1] = {0, 0, 0xFFFFFFFFu, (float)SCREEN_W, (float)SCREEN_H, 0};
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, f);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexMode(GU_PSM_T32, 0, 0, 0);
    sceGuTexImage(0, 512, 512, BUF_W, tmp);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0xFFFFFF, 0xFFFFFF);
    for (int ch = 0; ch < 3; ch++) {
        sceGuClutMode(GU_PSM_8888, ch * 8, 0xFF, 0);
        sceGuClutLoad(256 / 8, g_clut[ch]);
        Vtx* v = (Vtx*)sceGuGetMemory(2 * sizeof(Vtx));
        v[0] = {0, 0, 0xFFFFFFFFu, 0, 0, 0};
        v[1] = {(float)SCREEN_W, (float)SCREEN_H, 0xFFFFFFFFu, (float)SCREEN_W, (float)SCREEN_H, 0};
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
    }
    g_state_valid = false;
    g_bound_tex = nullptr;
}

Texture* grab_screen() {
    flush();
    uint8_t* vram = (uint8_t*)sceGeEdramGetAddr();
    void* fb = vram + g_draw_offset;
    void* tmp = vram + TEMP_OFFSET;
    sceGuCopyImage(GU_PSM_8888, 0, 0, SCREEN_W, SCREEN_H, BUF_W, fb, 0, 0, BUF_W, tmp);
    sceGuTexSync();
    sceGuTexFlush();
    g_state_valid = false;
    g_bound_tex = nullptr;
    return &g_surface_tex[(int)Surface::Grab];
}

void set_target(Surface s) {
    flush();
    const SurfaceInfo& info = g_surface[(int)s];
    sceGuDrawBufferList(info.psm, (void*)(s == Surface::Screen ? g_draw_offset : info.offset), info.stride);
    if (info.psm == GU_PSM_8888) sceGuDisable(GU_DITHER);
    else sceGuEnable(GU_DITHER);
    sceGuScissor(0, 0, info.w, info.h);
    sceGuTexSync();
    sceGuTexFlush();
    g_target = s;
    g_state_valid = false;
    g_bound_tex = nullptr;
}

Surface current_target() { return g_target; }

Texture* surface_texture(Surface s) { return &g_surface_tex[(int)s]; }
int surface_width(Surface s) { return g_surface[(int)s].w; }
int surface_height(Surface s) { return g_surface[(int)s].h; }

void set_color_mask(uint32_t protect_rgb) {
    flush();
    g_protect = protect_rgb & 0xFFFFFFu;
    sceGuPixelMask(g_protect);
}

void set_wrap(bool repeat) {
    if (repeat == g_wrap) return;
    flush();
    g_wrap = repeat;
    sceGuTexWrap(repeat ? GU_REPEAT : GU_CLAMP, repeat ? GU_REPEAT : GU_CLAMP);
}

static void apply_stencil(Stencil mode) {
    switch (mode) {
        case Stencil::Off:
            sceGuDisable(GU_STENCIL_TEST);
            sceGuDisable(GU_ALPHA_TEST);
            break;
        case Stencil::Write:
            sceGuEnable(GU_STENCIL_TEST);
            sceGuStencilFunc(GU_ALWAYS, 0xFF, 0xFF);
            sceGuStencilOp(GU_KEEP, GU_KEEP, GU_REPLACE);
            sceGuEnable(GU_ALPHA_TEST);
            sceGuAlphaFunc(GU_GREATER, 0, 0xFF);
            break;
        case Stencil::Test:
            sceGuEnable(GU_STENCIL_TEST);
            sceGuStencilFunc(GU_EQUAL, 0xFF, 0xFF);
            sceGuStencilOp(GU_KEEP, GU_KEEP, GU_KEEP);
            sceGuDisable(GU_ALPHA_TEST);
            break;
    }
}

void set_stencil(Stencil mode) {
    flush();
    g_stencil = mode;
    apply_stencil(mode);
}

void stencil_clear(int x0, int y0, int x1, int y1) {
    if (g_surface[(int)g_target].psm != GU_PSM_8888 || x1 <= x0 || y1 <= y0) return;
    flush();
    sceGuEnable(GU_STENCIL_TEST);
    sceGuStencilFunc(GU_ALWAYS, 0, 0xFF);
    sceGuStencilOp(GU_REPLACE, GU_REPLACE, GU_REPLACE);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuPixelMask(0x00FFFFFF);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuBlendFunc(GU_ADD, GU_FIX, GU_FIX, 0, 0xFFFFFF);
    Vtx* v = (Vtx*)sceGuGetMemory(2 * sizeof(Vtx));
    v[0] = {0, 0, 0xFFFFFFFFu, (float)x0, (float)y0, 0};
    v[1] = {0, 0, 0xFFFFFFFFu, (float)x1, (float)y1, 0};
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
    sceGuPixelMask(g_protect);
    apply_stencil(g_stencil);
    g_state_valid = false;
    g_bound_tex = nullptr;
}

void draw_triangles(Texture* tex, const Vertex* v, int count, Blend blend, bool smooth) {
    count -= count % 3;
    int done = 0;
    while (done < count) {
        int n = count - done;
        if (n > MAX_VTX - 3) n = (MAX_VTX - 3) / 3 * 3;
        prepare(tex, blend, smooth, GU_TRIANGLES, n);
        if (g_count + n > MAX_VTX) flush();
        Vtx* out = &g_batch[g_count];
        for (int i = 0; i < n; i++) {
            const Vertex& s = v[done + i];
            out[i] = {s.u, s.v, s.color, s.x, s.y, 0};
        }
        g_count += n;
        done += n;
    }
}

void draw_grid(Texture* tex, const Vertex* v, int cols, int rows, Blend blend, bool smooth) {
    if (cols <= 0 || rows <= 0) return;
    int stride = cols + 1;
    int n = stride * 2;
    if (n > MAX_VTX) return;
    for (int r = 0; r < rows; r++) {
        flush();
        prepare(tex, blend, smooth, GU_TRIANGLE_STRIP, n);
        Vtx* out = &g_batch[0];
        for (int c = 0; c < stride; c++) {
            const Vertex& a = v[(size_t)r * stride + c];
            const Vertex& b = v[(size_t)(r + 1) * stride + c];
            out[c * 2] = {a.u, a.v, a.color, a.x, a.y, 0};
            out[c * 2 + 1] = {b.u, b.v, b.color, b.x, b.y, 0};
        }
        g_count = n;
        flush();
    }
}

void sprite_lut(Texture* tex, int channel, const uint32_t* lut, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, Blend blend) {
    if (!tex || !lut || tex->psm != GU_PSM_8888) return;
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    flush();
    uint8_t* raw = (uint8_t*)sceGuGetMemory(1024 + 64);
    uint32_t* clut = (uint32_t*)(((uintptr_t)raw + 63) & ~(uintptr_t)63);
    std::memcpy(clut, lut, 1024);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuClutMode(GU_PSM_8888, (unsigned)(channel < 0 ? 0 : channel > 3 ? 24 : channel * 8), 0xFF, 0);
    sceGuClutLoad(256 / 8, clut);
    sceGuTexMode(GU_PSM_T32, 0, 0, 0);
    sceGuTexImage(0, tex->tw, tex->th, tex->stride, tex->pixels);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    apply_blend(blend);
    float du = (u1 - u0) / (x1 - x0);
    for (float sx = x0; sx < x1; sx += 64.0f) {
        float ex = sx + 64.0f < x1 ? sx + 64.0f : x1;
        Vtx* v = (Vtx*)sceGuGetMemory(2 * sizeof(Vtx));
        v[0] = {u0 + (sx - x0) * du, v0, 0xFFFFFFFFu, sx, y0, 0};
        v[1] = {u0 + (ex - x0) * du, v1, 0xFFFFFFFFu, ex, y1, 0};
        sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
    }
    g_state_valid = false;
    g_bound_tex = nullptr;
}

}
