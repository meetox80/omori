#include "../gfx.h"
#include "../common/border_tiles.h"

#include <gccore.h>
#include <malloc.h>
#include <ogc/conf.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../platform.h"

namespace gfx {

constexpr int MAX_VTX = 4096;
constexpr size_t FIFO_BYTES = 512 * 1024;
constexpr size_t RING_BYTES = 6u << 20;

struct Texture {
    int w = 0;
    int h = 0;
    int tw = 0;
    int th = 0;
    uint8_t* data = nullptr;
    size_t bytes = 0;
    size_t report = 0;
    uint32_t* cpu = nullptr;
    int cpu_stride = 0;
    bool surface = false;
    bool owned = true;
    GXTexObj obj;
};

struct Vtx {
    float x, y, u, v;
    uint32_t color;
};

enum class Src : uint8_t { Color, Texture, Flat, Gray, Replace };
enum class Term : uint8_t { None, Zero, One, Alpha, ClearCopy, ClearAlpha, AddMasked };

struct SurfaceInfo {
    int w = 0;
    int h = 0;
    bool valid = false;
    Texture tex;
};

static GXRModeObj* g_mode = nullptr;
static void* g_xfb[2] = {nullptr, nullptr};
static int g_xfb_index = 0;
static void* g_fifo = nullptr;
static bool g_wide = false;
static SurfaceInfo g_surface[6];
static Texture g_scratch[2];
static Surface g_target = Surface::Screen;
static bool g_frame_open = false;
static bool g_frame_pending = false;
static bool g_invalidate = false;
static uint32_t g_protect = 0;
static bool g_wrap = false;
static Stencil g_stencil = Stencil::Off;
static int g_sx0 = 0, g_sy0 = 0, g_sx1 = SCREEN_W, g_sy1 = SCREEN_H;
static std::vector<Texture*> g_garbage;
static uint8_t* g_ring = nullptr;
static size_t g_ring_used = 0;
static size_t g_ring_peak = 0;
static int g_batches = 0;

static Vtx g_batch[MAX_VTX];
static int g_count = 0;
static bool g_quads = true;
static Texture* g_tex = nullptr;
static Blend g_blend = Blend::Normal;
static bool g_smooth = false;
static bool g_flat = false;

constexpr size_t ARENA_KEEP = 16u << 20;
constexpr size_t ARENA_MIN_BLOCK = 32u << 10;
constexpr uint32_t ARENA_UNIT = 32;
constexpr size_t ARENA_MAX_BLOCKS = 4096;

struct ArenaBlock {
    uint32_t at;
    uint32_t len;
    bool used;
};

struct Arena {
    uint8_t* base = nullptr;
    uint32_t size = 0;
    uint32_t free_bytes = 0;
    ArenaBlock blocks[ARENA_MAX_BLOCKS];
    size_t count = 0;
    mutex_t lock = LWP_MUTEX_NULL;
};

static Arena g_arena;

static void arena_init() {
    if (g_arena.base) return;
    u32 level = IRQ_Disable();
    uintptr_t lo = (uintptr_t)SYS_GetArena2Lo(), hi = (uintptr_t)SYS_GetArena2Hi() & ~(uintptr_t)(ARENA_UNIT - 1);
    size_t bytes = hi > lo + ARENA_KEEP ? (hi - lo - ARENA_KEEP) & ~(size_t)(ARENA_UNIT - 1) : 0;
    if (bytes) SYS_SetArena2Hi((void*)(hi - bytes));
    IRQ_Restore(level);
    if (!bytes) return;
    g_arena.base = (uint8_t*)(hi - bytes);
    g_arena.size = (uint32_t)bytes;
    g_arena.free_bytes = (uint32_t)bytes;
    g_arena.blocks[0] = {0, (uint32_t)bytes, false};
    g_arena.count = 1;
    LWP_MutexInit(&g_arena.lock, false);
}

static void* arena_alloc(size_t bytes) {
    if (!g_arena.base) return nullptr;
    uint32_t need = (uint32_t)((bytes + ARENA_UNIT - 1) / ARENA_UNIT * ARENA_UNIT);
    if (need == 0) need = ARENA_UNIT;
    LWP_MutexLock(g_arena.lock);
    size_t best = g_arena.count;
    for (size_t i = 0; i < g_arena.count; i++) {
        const ArenaBlock& b = g_arena.blocks[i];
        if (b.used || b.len < need) continue;
        if (best == g_arena.count || b.len < g_arena.blocks[best].len) best = i;
        if (b.len == need) break;
    }
    void* out = nullptr;
    if (best < g_arena.count) {
        ArenaBlock& b = g_arena.blocks[best];
        if (b.len > need && g_arena.count < ARENA_MAX_BLOCKS) {
            std::memmove(&g_arena.blocks[best + 2], &g_arena.blocks[best + 1], (g_arena.count - best - 1) * sizeof(ArenaBlock));
            g_arena.blocks[best + 1] = {b.at + need, b.len - need, false};
            g_arena.count++;
            b.len = need;
        }
        b.used = true;
        g_arena.free_bytes -= b.len;
        out = g_arena.base + b.at;
    }
    LWP_MutexUnlock(g_arena.lock);
    return out;
}

static bool arena_owns(const void* p) { return g_arena.base && p >= g_arena.base && p < g_arena.base + g_arena.size; }

static void arena_release(void* p) {
    uint32_t at = (uint32_t)((uint8_t*)p - g_arena.base);
    LWP_MutexLock(g_arena.lock);
    ArenaBlock* blocks = g_arena.blocks;
    size_t i = (size_t)(std::lower_bound(blocks, blocks + g_arena.count, at, [](const ArenaBlock& b, uint32_t v) { return b.at < v; }) - blocks);
    if (i < g_arena.count && blocks[i].at == at && blocks[i].used) {
        blocks[i].used = false;
        g_arena.free_bytes += blocks[i].len;
        size_t first = i > 0 && !blocks[i - 1].used ? i - 1 : i;
        size_t last = i + 1 < g_arena.count && !blocks[i + 1].used ? i + 1 : i;
        for (size_t k = first + 1; k <= last; k++) blocks[first].len += blocks[k].len;
        std::memmove(&blocks[first + 1], &blocks[last + 1], (g_arena.count - last - 1) * sizeof(ArenaBlock));
        g_arena.count -= last - first;
    }
    LWP_MutexUnlock(g_arena.lock);
}

void* alloc(size_t bytes) {
    size_t n = bytes ? (bytes + 31) & ~(size_t)31 : 32;
    if (n >= ARENA_MIN_BLOCK) {
        if (void* p = arena_alloc(n)) return p;
    }
    return memalign(32, n);
}

void release(void* p) {
    if (!p) return;
    if (arena_owns(p)) arena_release(p);
    else std::free(p);
}

size_t arena_free_bytes() { return g_arena.free_bytes; }

void* arena_try_alloc(size_t bytes) { return arena_alloc(bytes); }

static inline size_t tile_at(int tw, int x, int y) { return ((size_t)(y >> 2) * (size_t)(tw >> 2) + (size_t)(x >> 2)) * 64 + (size_t)((y & 3) * 4 + (x & 3)) * 2; }

static inline void put_texel(uint8_t* data, int tw, int x, int y, uint32_t c) {
    size_t at = tile_at(tw, x, y);
    data[at] = (uint8_t)(c >> 24);
    data[at + 1] = (uint8_t)c;
    data[at + 32] = (uint8_t)(c >> 8);
    data[at + 33] = (uint8_t)(c >> 16);
}

static inline uint32_t get_texel(const uint8_t* data, int tw, int x, int y) {
    size_t at = tile_at(tw, x, y);
    return rgba(data[at + 1], data[at + 32], data[at + 33], data[at]);
}

static void* ring_alloc(size_t bytes) {
    bytes = (bytes + 31) & ~(size_t)31;
    if (g_ring_used + bytes > RING_BYTES) {
        GX_DrawDone();
        g_ring_used = 0;
    }
    void* p = g_ring + g_ring_used;
    g_ring_used += bytes;
    g_ring_peak = std::max(g_ring_peak, g_ring_used);
    return p;
}

static void init_obj(Texture* t) {
    GX_InitTexObj(&t->obj, t->data, (u16)t->tw, (u16)t->th, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&t->obj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
}

static int bpp_of(Format f) {
    switch (f) {
        case Format::RGBA8888: return 4;
        case Format::T8: return 1;
        default: return 2;
    }
}

static uint32_t expand(Format f, const uint8_t* p, const uint32_t* pal) {
    switch (f) {
        case Format::RGBA8888: {
            uint32_t v;
            std::memcpy(&v, p, 4);
            return v;
        }
        case Format::T8: return pal ? pal[p[0]] : 0;
        case Format::RGBA4444: {
            int v = p[0] | (p[1] << 8);
            return rgba((v & 15) * 17, ((v >> 4) & 15) * 17, ((v >> 8) & 15) * 17, ((v >> 12) & 15) * 17);
        }
        case Format::RGB565: {
            int v = p[0] | (p[1] << 8);
            return rgba((v & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, ((v >> 11) & 31) * 255 / 31, 255);
        }
        case Format::RGBA5551: {
            int v = p[0] | (p[1] << 8);
            return rgba((v & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, ((v >> 10) & 31) * 255 / 31, (v >> 15) ? 255 : 0);
        }
    }
    return 0;
}

Texture* texture_create(int w, int h, int stride, int rows, Format fmt, bool swizzled, void* pixels, uint32_t* palette) {
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    if (swizzled && fmt == Format::RGBA8888 && ((uintptr_t)pixels & 31) == 0) {
        t->tw = stride;
        t->th = rows;
        t->data = (uint8_t*)pixels;
        t->bytes = (size_t)stride * rows * 4;
        release(palette);
    } else {
        t->tw = (w + 3) & ~3;
        t->th = (h + 3) & ~3;
        t->bytes = (size_t)t->tw * t->th * 4;
        t->data = (uint8_t*)alloc(t->bytes);
        if (!t->data) {
            release(pixels);
            release(palette);
            delete t;
            return nullptr;
        }
        const uint8_t* src = (const uint8_t*)pixels;
        int bpp = bpp_of(fmt);
        for (int y = 0; y < t->th; y++) {
            int sy = std::min(y, h - 1);
            for (int x = 0; x < t->tw; x++) {
                int sx = std::min(x, w - 1);
                uint32_t c = swizzled && fmt == Format::RGBA8888 ? stored_texel(src, sx, sy, stride, rows)
                                                                 : expand(fmt, src + ((size_t)sy * stride + sx) * bpp, palette);
                put_texel(t->data, t->tw, x, y, c);
            }
        }
        release(pixels);
        release(palette);
    }
    t->report = t->bytes;
    DCFlushRange(t->data, t->bytes);
    g_invalidate = true;
    init_obj(t);
    return t;
}

Texture* texture_wrap(int w, int h, int stride, uint32_t* pixels) {
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    t->tw = (w + 3) & ~3;
    t->th = (h + 3) & ~3;
    t->bytes = (size_t)t->tw * t->th * 4;
    t->data = (uint8_t*)alloc(t->bytes);
    t->cpu = pixels;
    t->cpu_stride = stride;
    if (!t->data) {
        delete t;
        return nullptr;
    }
    init_obj(t);
    texture_touch(t);
    return t;
}

void texture_touch(Texture* t) {
    if (!t || !t->cpu || !t->data) return;
    size_t tile_row = (size_t)(t->tw >> 2) * 64;
    for (int y = 0; y < t->th; y++) {
        const uint32_t* row = t->cpu + (size_t)std::min(y, t->h - 1) * t->cpu_stride;
        uint8_t* line = t->data + (size_t)(y >> 2) * tile_row + (size_t)(y & 3) * 8;
        for (int x = 0; x < t->tw; x += 4) {
            uint16_t* ar = (uint16_t*)(line + (size_t)(x >> 2) * 64);
            uint16_t* gb = ar + 16;
            for (int i = 0; i < 4; i++) {
                uint32_t c = row[std::min(x + i, t->w - 1)];
                ar[i] = (uint16_t)(((c >> 16) & 0xFF00) | (c & 0xFF));
                gb[i] = (uint16_t)((c & 0xFF00) | ((c >> 16) & 0xFF));
            }
        }
    }
    DCFlushRange(t->data, t->bytes);
    g_invalidate = true;
}

static void flush();
static void save_efb(Surface s);

bool texture_read(const Texture* t, int x, int y, uint32_t* out) {
    if (!t || x < 0 || y < 0 || x >= t->w || y >= t->h) return false;
    if (t->cpu) {
        *out = t->cpu[(size_t)y * t->cpu_stride + x];
        return true;
    }
    if (t->surface) {
        flush();
        if (g_frame_open && t == &g_surface[(int)g_target].tex) save_efb(g_target);
        GX_DrawDone();
        DCInvalidateRange(t->data, t->bytes);
    }
    *out = get_texel(t->data, t->tw, x, y);
    return true;
}

void texture_destroy(Texture* t) {
    if (!t) return;
    g_garbage.push_back(t);
}

size_t texture_bytes(const Texture* t) { return t ? t->report : 0; }
int texture_width(const Texture* t) { return t ? t->w : 0; }
int texture_height(const Texture* t) { return t ? t->h : 0; }

static void collect_garbage() {
    for (Texture* t : g_garbage) {
        if (g_tex == t) g_tex = nullptr;
        if (t->owned) release(t->data);
        delete t;
    }
    g_garbage.clear();
}

static bool setup_surface_tex(Texture& t, int w, int h) {
    t.w = w;
    t.h = h;
    t.tw = (w + 3) & ~3;
    t.th = (h + 3) & ~3;
    t.bytes = (size_t)t.tw * t.th * 4;
    t.data = (uint8_t*)alloc(t.bytes);
    t.surface = true;
    t.owned = false;
    if (!t.data) return false;
    std::memset(t.data, 0, t.bytes);
    for (int y = 0; y < t.th; y++) {
        for (int x = 0; x < t.tw; x++) put_texel(t.data, t.tw, x, y, 0xFF000000u);
    }
    DCFlushRange(t.data, t.bytes);
    init_obj(&t);
    return true;
}

static void viewport(int w, int h) {
    Mtx44 proj;
    guOrtho(proj, 0, (f32)h, 0, (f32)w, 0, 1);
    GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
    GX_SetViewport(0, 0, (f32)w, (f32)h, 0, 1);
}

void init() {
    arena_init();
    VIDEO_Init();
    g_mode = VIDEO_GetPreferredMode(nullptr);
    g_wide = CONF_GetAspectRatio() == CONF_ASPECT_16_9;
    VIDEO_Configure(g_mode);
    for (auto& x : g_xfb) {
        x = MEM_K0_TO_K1(SYS_AllocateFramebuffer(g_mode));
        VIDEO_ClearFrameBuffer(g_mode, x, COLOR_BLACK);
    }
    VIDEO_SetNextFramebuffer(g_xfb[0]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (g_mode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
    g_fifo = memalign(32, FIFO_BYTES);
    std::memset(g_fifo, 0, FIFO_BYTES);
    GX_Init(g_fifo, FIFO_BYTES);
    GXColor black = {0, 0, 0, 255};
    GX_SetCopyClear(black, GX_MAX_Z24);
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    GX_SetDither(GX_DISABLE);
    GX_SetDispCopySrc(0, 0, SCREEN_W, SCREEN_H);
    u32 xfb_h = GX_SetDispCopyYScale(GX_GetYScaleFactor(SCREEN_H, g_mode->xfbHeight));
    GX_SetDispCopyDst(g_mode->fbWidth, (u16)xfb_h);
    GX_SetCopyFilter(g_mode->aa, g_mode->sample_pattern, GX_TRUE, g_mode->vfilter);
    GX_SetFieldMode(g_mode->field_rendering, (g_mode->viHeight == 2 * g_mode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
    GX_SetDispCopyGamma(GX_GM_1_0);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetClipMode(GX_CLIP_DISABLE);
    GX_SetAlphaUpdate(GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
    GX_SetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
    GX_SetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
    GX_SetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA);
    Mtx mv;
    guMtxIdentity(mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);
    GX_SetCurrentMtx(GX_PNMTX0);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetZCompLoc(GX_TRUE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    const int sizes[6][2] = {{SCREEN_W, SCREEN_H}, {SCREEN_W, SCREEN_H}, {HALF_W, HALF_H}, {HALF_W, HALF_H}, {HALF_W, HALF_H}, {SCREEN_W, SCREEN_H}};
    for (int i = 0; i < 6; i++) {
        g_surface[i].w = sizes[i][0];
        g_surface[i].h = sizes[i][1];
        setup_surface_tex(g_surface[i].tex, sizes[i][0], sizes[i][1]);
    }
    setup_surface_tex(g_scratch[0], SCREEN_W, SCREEN_H);
    setup_surface_tex(g_scratch[1], SCREEN_W, SCREEN_H);
    g_ring = (uint8_t*)memalign(32, RING_BYTES);
    viewport(SCREEN_W, SCREEN_H);
    GX_SetScissor(0, 0, SCREEN_W, SCREEN_H);
}

void shutdown() {
    if (!g_mode) return;
    present();
    GX_DrawDone();
    GX_AbortFrame();
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
}

static void stage_order(int n, bool textured) {
    if (textured) GX_SetTevOrder((u8)n, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    else GX_SetTevOrder((u8)n, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GX_SetTevSwapMode((u8)n, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GX_SetTevColorOp((u8)n, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp((u8)n, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

static void setup_tev(Src src, Term term, uint32_t keep_rgb, int level) {
    int n = 0;
    switch (src) {
        case Src::Color:
            stage_order(n, false);
            GX_SetTevColorIn((u8)n, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
            GX_SetTevAlphaIn((u8)n, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
            n++;
            break;
        case Src::Texture:
            stage_order(n, true);
            GX_SetTevColorIn((u8)n, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
            GX_SetTevAlphaIn((u8)n, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
            n++;
            break;
        case Src::Replace:
            stage_order(n, true);
            GX_SetTevColorIn((u8)n, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
            GX_SetTevAlphaIn((u8)n, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
            n++;
            break;
        case Src::Flat:
            stage_order(n, true);
            GX_SetTevColorIn((u8)n, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
            GX_SetTevAlphaIn((u8)n, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
            n++;
            stage_order(n, false);
            GX_SetTevColorIn((u8)n, GX_CC_ZERO, GX_CC_RASC, GX_CC_APREV, GX_CC_ZERO);
            GX_SetTevAlphaIn((u8)n, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
            n++;
            break;
        case Src::Gray: {
            GXColor third = {85, 85, 85, 255};
            GXColor lv = {(u8)level, (u8)level, (u8)level, 255};
            GX_SetTevKColor(GX_KCOLOR0, third);
            GX_SetTevKColor(GX_KCOLOR1, lv);
            const u8 swaps[3] = {GX_TEV_SWAP1, GX_TEV_SWAP2, GX_TEV_SWAP3};
            for (int c = 0; c < 3; c++) {
                stage_order(n, true);
                GX_SetTevSwapMode((u8)n, GX_TEV_SWAP0, swaps[c]);
                GX_SetTevKColorSel((u8)n, GX_TEV_KCSEL_K0);
                GX_SetTevKAlphaSel((u8)n, GX_TEV_KASEL_K0_A);
                GX_SetTevColorIn((u8)n, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, c ? GX_CC_CPREV : GX_CC_ZERO);
                GX_SetTevAlphaIn((u8)n, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
                n++;
            }
            stage_order(n, true);
            GX_SetTevKColorSel((u8)n, GX_TEV_KCSEL_K1);
            GX_SetTevColorIn((u8)n, GX_CC_TEXC, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO);
            GX_SetTevAlphaIn((u8)n, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
            n++;
            break;
        }
    }
    if (term != Term::None) {
        GXColor m = {(u8)((keep_rgb & 0x0000FFu) ? 0 : 255), (u8)((keep_rgb & 0x00FF00u) ? 0 : 255), (u8)((keep_rgb & 0xFF0000u) ? 0 : 255), 255};
        GX_SetTevKColor(GX_KCOLOR2, m);
        stage_order(n, false);
        GX_SetTevKColorSel((u8)n, GX_TEV_KCSEL_K2);
        switch (term) {
            case Term::Zero: GX_SetTevColorIn((u8)n, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO); break;
            case Term::One: GX_SetTevColorIn((u8)n, GX_CC_ONE, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO); break;
            case Term::Alpha: GX_SetTevColorIn((u8)n, GX_CC_APREV, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO); break;
            case Term::ClearCopy: GX_SetTevColorIn((u8)n, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_KONST); break;
            case Term::ClearAlpha: GX_SetTevColorIn((u8)n, GX_CC_ZERO, GX_CC_KONST, GX_CC_APREV, GX_CC_ZERO); break;
            case Term::AddMasked: GX_SetTevColorIn((u8)n, GX_CC_ZERO, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO); break;
            default: break;
        }
        GX_SetTevAlphaIn((u8)n, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
        n++;
    }
    GX_SetNumTevStages((u8)n);
}

static void set_blend(Blend b) {
    switch (b) {
        case Blend::Normal: GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_INVSRCALPHA, GX_LO_CLEAR); break;
        case Blend::Add: GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR); break;
        case Blend::Multiply: GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_INVSRCALPHA, GX_LO_CLEAR); break;
        case Blend::Screen: GX_SetBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_INVSRCCLR, GX_LO_CLEAR); break;
        case Blend::Subtract: GX_SetBlendMode(GX_BM_SUBTRACT, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR); break;
        case Blend::MulSrc: GX_SetBlendMode(GX_BM_BLEND, GX_BL_ZERO, GX_BL_SRCCLR, GX_LO_CLEAR); break;
        case Blend::MulAdd: GX_SetBlendMode(GX_BM_BLEND, GX_BL_DSTCLR, GX_BL_ONE, GX_LO_CLEAR); break;
        case Blend::MulSub: GX_SetBlendMode(GX_BM_BLEND, GX_BL_ZERO, GX_BL_INVSRCCLR, GX_LO_CLEAR); break;
        default: GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR); break;
    }
}

static void apply_stencil(Stencil mode) {
    switch (mode) {
        case Stencil::Off:
            GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
            GX_SetZCompLoc(GX_TRUE);
            GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
            break;
        case Stencil::Write:
            GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
            GX_SetZCompLoc(GX_FALSE);
            GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
            break;
        case Stencil::Test:
            GX_SetZMode(GX_TRUE, GX_EQUAL, GX_FALSE);
            GX_SetZCompLoc(GX_TRUE);
            GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
            break;
    }
}

static void bind(Texture* t, bool smooth, bool repeat) {
    if (g_invalidate) {
        GX_InvalidateTexAll();
        g_invalidate = false;
    }
    bool pot = (t->tw & (t->tw - 1)) == 0 && (t->th & (t->th - 1)) == 0;
    u8 wrap = repeat && pot ? GX_REPEAT : GX_CLAMP;
    GX_InitTexObjWrapMode(&t->obj, wrap, wrap);
    GX_InitTexObjFilterMode(&t->obj, smooth ? GX_LINEAR : GX_NEAR, smooth ? GX_LINEAR : GX_NEAR);
    GX_LoadTexObj(&t->obj, GX_TEXMAP0);
}

static void emit(const Vtx* v, int n, bool quads, float z) {
    GX_Begin(quads ? GX_QUADS : GX_TRIANGLES, GX_VTXFMT0, (u16)n);
    for (int i = 0; i < n; i++) {
        GX_Position3f32(v[i].x, v[i].y, z);
        uint32_t c = v[i].color;
        GX_Color4u8((u8)c, (u8)(c >> 8), (u8)(c >> 16), (u8)(c >> 24));
        GX_TexCoord2f32(v[i].u, v[i].v);
    }
    GX_End();
}

static void screen_texgen(bool on, const Texture* t) {
    if (on) {
        Mtx m;
        guMtxIdentity(m);
        m[0][0] = 1.0f / (f32)t->tw;
        m[1][1] = 1.0f / (f32)t->th;
        GX_LoadTexMtxImm(m, GX_TEXMTX0, GX_MTX2x4);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_POS, GX_TEXMTX0);
    } else {
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    }
}

static void copy_target(Texture* dst) {
    const SurfaceInfo& si = g_surface[(int)g_target];
    GX_SetTexCopySrc(0, 0, (u16)si.w, (u16)si.h);
    GX_SetTexCopyDst((u16)si.w, (u16)si.h, GX_TF_RGBA8, GX_FALSE);
    GX_CopyTex(dst->data, GX_FALSE);
    GX_PixModeSync();
    g_invalidate = true;
}

static Term term_for(Blend b) {
    switch (b) {
        case Blend::MulSrc:
        case Blend::Min: return Term::One;
        case Blend::Multiply: return Term::Alpha;
        default: return Term::Zero;
    }
}

static void draw_pass(Src src, Texture* tex, Blend blend, Term term, bool smooth, const Vtx* v, int n, bool quads) {
    setup_tev(src, term, g_protect, 0);
    set_blend(blend);
    if (tex) bind(tex, smooth, g_wrap);
    emit(v, n, quads, 0.0f);
}

static void redraw_with(Texture* sample, Blend blend, const Vtx* v, int n, bool quads) {
    screen_texgen(true, sample);
    setup_tev(Src::Replace, Term::None, 0, 0);
    set_blend(blend);
    bind(sample, false, false);
    emit(v, n, quads, 0.0f);
    screen_texgen(false, nullptr);
}

static void draw_batch(Src src, Texture* tex, Blend blend, bool smooth, const Vtx* v, int n, bool quads) {
    g_batches++;
    uint32_t keep = g_protect & 0xFFFFFFu;
    if (keep == 0xFFFFFFu) {
        GX_SetColorUpdate(GX_FALSE);
        draw_pass(src, tex, blend == Blend::Max || blend == Blend::Min ? Blend::Copy : blend, Term::None, smooth, v, n, quads);
        GX_SetColorUpdate(GX_TRUE);
        return;
    }
    Term masked = keep ? term_for(blend) : Term::None;
    if (blend == Blend::Max) {
        Texture* a = &g_scratch[0];
        copy_target(a);
        draw_pass(src, tex, Blend::Copy, masked, smooth, v, n, quads);
        redraw_with(a, Blend::Subtract, v, n, quads);
        redraw_with(a, Blend::Add, v, n, quads);
        return;
    }
    if (blend == Blend::Min) {
        Texture* a = &g_scratch[0];
        Texture* b = &g_scratch[1];
        copy_target(a);
        draw_pass(src, tex, Blend::Subtract, masked, smooth, v, n, quads);
        copy_target(b);
        redraw_with(a, Blend::Copy, v, n, quads);
        redraw_with(b, Blend::Subtract, v, n, quads);
        return;
    }
    if (keep && (blend == Blend::Copy || blend == Blend::Normal)) {
        draw_pass(src, tex, Blend::MulSub, blend == Blend::Copy ? Term::ClearCopy : Term::ClearAlpha, smooth, v, n, quads);
        draw_pass(src, tex, Blend::Add, Term::AddMasked, smooth, v, n, quads);
        return;
    }
    draw_pass(src, tex, blend, masked, smooth, v, n, quads);
}

static bool scissor_empty() { return g_sx1 <= g_sx0 || g_sy1 <= g_sy0; }

static void flush() {
    if (!g_count) return;
    int n = g_count;
    g_count = 0;
    if (!g_frame_open || scissor_empty()) return;
    Src src = g_tex ? (g_flat ? Src::Flat : Src::Texture) : Src::Color;
    Blend b = g_blend;
    if (g_flat) b = b == Blend::Add ? Blend::Add : b == Blend::Subtract ? Blend::Subtract : Blend::Normal;
    draw_batch(src, g_tex, b, g_smooth, g_batch, n, g_quads);
}

static void prepare(Texture* tex, Blend blend, bool smooth, bool quads, int verts, bool flat) {
    if (g_count && (g_tex != tex || g_blend != blend || g_smooth != smooth || g_quads != quads || g_flat != flat || g_count + verts > MAX_VTX)) flush();
    g_tex = tex;
    g_blend = blend;
    g_smooth = smooth;
    g_quads = quads;
    g_flat = flat;
}

static void apply_scissor() {
    if (scissor_empty()) return;
    GX_SetScissor((u32)g_sx0, (u32)g_sy0, (u32)(g_sx1 - g_sx0), (u32)(g_sy1 - g_sy0));
}

static void fill_rect(int w, int h, uint32_t color, float z) {
    Vtx q[4] = {{0, 0, 0, 0, color}, {(float)w, 0, 0, 0, color}, {(float)w, (float)h, 0, 0, color}, {0, (float)h, 0, 0, color}};
    setup_tev(Src::Color, Term::None, 0, 0);
    set_blend(Blend::Copy);
    emit(q, 4, true, z);
}

static void save_efb(Surface s) {
    SurfaceInfo& si = g_surface[(int)s];
    GX_SetTexCopySrc(0, 0, (u16)si.w, (u16)si.h);
    GX_SetTexCopyDst((u16)si.w, (u16)si.h, GX_TF_RGBA8, GX_FALSE);
    GX_CopyTex(si.tex.data, GX_FALSE);
    GX_PixModeSync();
    si.valid = true;
    g_invalidate = true;
}

static void load_efb(Surface s) {
    SurfaceInfo& si = g_surface[(int)s];
    viewport(si.w, si.h);
    GX_SetScissor(0, 0, (u32)si.w, (u32)si.h);
    GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    GX_SetZCompLoc(GX_TRUE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    if (si.valid) {
        Vtx q[4] = {{0, 0, 0, 0, 0xFFFFFFFFu},
                    {(float)si.w, 0, (float)si.w / si.tex.tw, 0, 0xFFFFFFFFu},
                    {(float)si.w, (float)si.h, (float)si.w / si.tex.tw, (float)si.h / si.tex.th, 0xFFFFFFFFu},
                    {0, (float)si.h, 0, (float)si.h / si.tex.th, 0xFFFFFFFFu}};
        setup_tev(Src::Replace, Term::None, 0, 0);
        set_blend(Blend::Copy);
        bind(&si.tex, false, false);
        emit(q, 4, true, -1.0f);
    } else {
        fill_rect(si.w, si.h, 0xFF000000u, -1.0f);
    }
    apply_stencil(g_stencil);
}

static std::vector<Texture*> g_border;
static BorderTiles g_border_tiles;
static std::vector<BorderRect> g_border_bars, g_border_frame;
static uint32_t g_border_line = 0;
static float g_border_scroll = 0;

void border_canvas(float& x0, float& y0, float& x1, float& y1) {
    float sx = g_wide ? 0.75f : 1.0f, left = (SCREEN_W - SCREEN_W * sx) * 0.5f;
    x0 = -left / sx;
    y0 = 0;
    x1 = (SCREEN_W - left) / sx;
    y1 = (float)SCREEN_H;
}

static Texture* border_chunk(const BorderArt& art, int k) {
    const BorderTiles& bt = g_border_tiles;
    int w = border_chunk_width(bt, k), h = bt.rows;
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    t->tw = (w + 3) & ~3;
    t->th = (h + 3) & ~3;
    t->bytes = (size_t)t->tw * t->th * 2;
    t->data = (uint8_t*)alloc(t->bytes);
    if (!t->data) {
        delete t;
        return nullptr;
    }
    uint16_t lut[256];
    for (int i = 0; i < 256; i++) {
        uint32_t c = border_mix(art, i);
        uint32_t r = ((c & 255) * 31 + 127) / 255, g = (((c >> 8) & 255) * 63 + 127) / 255, b = (((c >> 16) & 255) * 31 + 127) / 255;
        lut[i] = (uint16_t)((r << 11) | (g << 5) | b);
    }
    std::vector<uint8_t> row((size_t)w);
    for (int y = 0; y < t->th; y++) {
        if (y < h) border_row(art, bt, k, y, row.data());
        for (int x = 0; x < t->tw; x++) {
            uint16_t v = lut[row[std::min(x, w - 1)]];
            size_t at = ((size_t)(y >> 2) * (size_t)(t->tw >> 2) + (size_t)(x >> 2)) * 32 + (size_t)((y & 3) * 4 + (x & 3)) * 2;
            t->data[at] = (uint8_t)(v >> 8);
            t->data[at + 1] = (uint8_t)v;
        }
    }
    t->report = t->bytes;
    DCFlushRange(t->data, t->bytes);
    g_invalidate = true;
    GX_InitTexObj(&t->obj, t->data, (u16)t->tw, (u16)t->th, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&t->obj, GX_LINEAR, GX_LINEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
    return t;
}

static void clear_border() {
    for (Texture* t : g_border) texture_destroy(t);
    g_border.clear();
    g_border_bars.clear();
    g_border_frame.clear();
}

bool set_border(const BorderArt* art, const BorderRect* bars, int nbars, const BorderRect* frame, int nframe) {
    clear_border();
    if (!art) return true;
    g_border_tiles = border_tiles(*art, bars, nbars, 1024, 1);
    for (int k = 0; k < g_border_tiles.chunks; k++) {
        Texture* t = border_chunk(*art, k);
        if (!t) {
            clear_border();
            return false;
        }
        g_border.push_back(t);
    }
    g_border_bars.assign(bars, bars + nbars);
    g_border_frame.assign(frame, frame + nframe);
    g_border_line = art->line;
    return true;
}

void set_border_scroll(float x) { g_border_scroll = x; }

static void present_border(float x0, float sx) {
    setup_tev(Src::Replace, Term::None, 0, 0);
    set_blend(Blend::Copy);
    BorderSpan spans[BORDER_SPANS];
    for (const BorderRect& b : g_border_bars) {
        float v0 = (float)(b.y0 - g_border_tiles.y0), v1 = v0 + (float)(b.y1 - b.y0), y0 = (float)b.y0, y1 = (float)b.y1;
        int n = border_spans(g_border_tiles, b, g_border_scroll, spans);
        for (int i = 0; i < n; i++) {
            const BorderSpan& p = spans[i];
            Texture* t = g_border[p.chunk];
            bind(t, true, false);
            float tw = (float)t->tw, th = (float)t->th;
            float px0 = x0 + p.x0 * sx, px1 = x0 + p.x1 * sx;
            float u0 = p.u / tw, u1 = (p.u + p.x1 - p.x0) / tw, tv0 = v0 / th, tv1 = v1 / th;
            Vtx q[4] = {{px0, y0, u0, tv0, 0xFFFFFFFFu}, {px1, y0, u1, tv0, 0xFFFFFFFFu}, {px1, y1, u1, tv1, 0xFFFFFFFFu}, {px0, y1, u0, tv1, 0xFFFFFFFFu}};
            emit(q, 4, true, 0.0f);
        }
    }
    setup_tev(Src::Color, Term::None, 0, 0);
    for (const BorderRect& f : g_border_frame) {
        float fx0 = x0 + f.x0 * sx, fx1 = x0 + f.x1 * sx, fy0 = (float)f.y0, fy1 = (float)f.y1;
        Vtx q[4] = {{fx0, fy0, 0, 0, g_border_line}, {fx1, fy0, 0, 0, g_border_line}, {fx1, fy1, 0, 0, g_border_line}, {fx0, fy1, 0, 0, g_border_line}};
        emit(q, 4, true, 0.0f);
    }
}

void present() {
    if (!g_frame_pending) return;
    VIDEO_SetNextFramebuffer(g_xfb[g_xfb_index]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    g_xfb_index ^= 1;
    g_frame_pending = false;
}

void begin_frame(uint32_t clear_color) {
    if (!g_mode) return;
    present();
    collect_garbage();
    g_ring_used = 0;
    g_count = 0;
    g_tex = nullptr;
    g_target = Surface::Screen;
    g_protect = 0;
    g_wrap = false;
    g_stencil = Stencil::Off;
    g_frame_open = true;
    viewport(SCREEN_W, SCREEN_H);
    g_sx0 = g_sy0 = 0;
    g_sx1 = SCREEN_W;
    g_sy1 = SCREEN_H;
    apply_scissor();
    GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    fill_rect(SCREEN_W, SCREEN_H, clear_color | 0xFF000000u, -1.0f);
    apply_stencil(Stencil::Off);
}

void end_frame() {
    if (!g_frame_open) return;
    flush();
    if (g_target != Surface::Screen) set_target(Surface::Screen);
    g_stencil = Stencil::Off;
    g_protect = 0;
    apply_stencil(Stencil::Off);
    save_efb(Surface::Screen);
    if (g_wide) {
        GX_SetScissor(0, 0, SCREEN_W, SCREEN_H);
        float w = SCREEN_W * 0.75f, x0 = (SCREEN_W - w) * 0.5f;
        if (!g_border.empty()) {
            present_border(x0, 0.75f);
        } else {
            fill_rect(SCREEN_W, SCREEN_H, 0xFF000000u, -1.0f);
        }
        Texture& t = g_surface[(int)Surface::Screen].tex;
        Vtx q[4] = {{x0, 0, 0, 0, 0xFFFFFFFFu},
                    {x0 + w, 0, (float)SCREEN_W / t.tw, 0, 0xFFFFFFFFu},
                    {x0 + w, (float)SCREEN_H, (float)SCREEN_W / t.tw, (float)SCREEN_H / t.th, 0xFFFFFFFFu},
                    {x0, (float)SCREEN_H, 0, (float)SCREEN_H / t.th, 0xFFFFFFFFu}};
        setup_tev(Src::Replace, Term::None, 0, 0);
        set_blend(Blend::Copy);
        bind(&t, true, false);
        emit(q, 4, true, 0.0f);
    }
    GX_CopyDisp(g_xfb[g_xfb_index], GX_TRUE);
    GX_DrawDone();
    g_frame_open = false;
    g_frame_pending = true;
}

int list_peak_bytes() { return (int)g_ring_peak; }

bool reclaim_memory() {
    flush();
    GX_DrawDone();
    collect_garbage();
    return true;
}

void set_evict_handler(bool (*)()) {}

double take_batch_count() {
    double n = g_batches;
    g_batches = 0;
    return n;
}

const uint32_t* framebuffer() { return nullptr; }

static const Texture* screen_ready() {
    Texture* t = &g_surface[(int)Surface::Screen].tex;
    if (g_frame_open) {
        flush();
        if (g_target == Surface::Screen) save_efb(Surface::Screen);
    }
    GX_DrawDone();
    DCInvalidateRange(t->data, t->bytes);
    return t;
}

bool capture_row(int y, int x, int w, uint32_t* out) {
    if (y < 0 || y >= SCREEN_H || x < 0 || x + w > SCREEN_W) return false;
    const Texture* t = screen_ready();
    for (int i = 0; i < w; i++) out[i] = get_texel(t->data, t->tw, x + i, y);
    return true;
}

bool capture(uint32_t* out, int out_stride) {
    const Texture* t = screen_ready();
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) out[(size_t)y * out_stride + x] = get_texel(t->data, t->tw, x, y);
    }
    return true;
}

void set_scissor(int x, int y, int w, int h) {
    flush();
    const SurfaceInfo& si = g_surface[(int)g_target];
    g_sx0 = std::max(0, x);
    g_sy0 = std::max(0, y);
    g_sx1 = std::min(si.w, x + w);
    g_sy1 = std::min(si.h, y + h);
    apply_scissor();
}

void reset_scissor() {
    flush();
    g_sx0 = 0;
    g_sy0 = 0;
    g_sx1 = g_surface[(int)g_target].w;
    g_sy1 = g_surface[(int)g_target].h;
    apply_scissor();
}

static inline float nu(const Texture* t, float u) { return t ? u / (float)t->tw : 0.0f; }
static inline float nv(const Texture* t, float v) { return t ? v / (float)t->th : 0.0f; }

static void push_rect(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color) {
    Vtx* v = &g_batch[g_count];
    v[0] = {x0, y0, u0, v0, color};
    v[1] = {x1, y0, u1, v0, color};
    v[2] = {x1, y1, u1, v1, color};
    v[3] = {x0, y1, u0, v1, color};
    g_count += 4;
}

static void push_triangle(const Vtx& a, const Vtx& b, const Vtx& c) {
    g_batch[g_count++] = a;
    g_batch[g_count++] = b;
    g_batch[g_count++] = c;
}

void sprite(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    prepare(tex, blend, smooth, true, 4, false);
    push_rect(x0, y0, x1, y1, nu(tex, u0), nv(tex, v0), nu(tex, u1), nv(tex, v1), color);
}

static void quad_common(Texture* tex, const float uv[8], const float xy[8], uint32_t color, Blend blend, bool smooth, bool flat) {
    prepare(tex, blend, smooth, false, 6, flat);
    Vtx c[4];
    for (int k = 0; k < 4; k++) c[k] = {xy[k * 2], xy[k * 2 + 1], nu(tex, uv[k * 2]), nv(tex, uv[k * 2 + 1]), color};
    push_triangle(c[0], c[1], c[2]);
    push_triangle(c[0], c[2], c[3]);
}

void draw_quad(Texture* tex, const float uv[8], const float xy[8], uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    quad_common(tex, uv, xy, color, blend, smooth, false);
}

void sprite_flat(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t rgb, int alpha, Blend blend,
                 bool smooth) {
    if (!tex || alpha <= 0) return;
    uint32_t c = (rgb & 0xFFFFFFu) | ((uint32_t)(alpha > 255 ? 255 : alpha) << 24);
    prepare(tex, blend, smooth, true, 4, true);
    push_rect(x0, y0, x1, y1, nu(tex, u0), nv(tex, v0), nu(tex, u1), nv(tex, v1), c);
}

void quad_flat(Texture* tex, const float uv[8], const float xy[8], uint32_t rgb, int alpha, Blend blend, bool smooth) {
    if (!tex || alpha <= 0) return;
    uint32_t c = (rgb & 0xFFFFFFu) | ((uint32_t)(alpha > 255 ? 255 : alpha) << 24);
    quad_common(tex, uv, xy, c, blend, smooth, true);
}

void fill(float x0, float y0, float x1, float y1, uint32_t color, Blend blend) {
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    prepare(nullptr, blend, false, true, 4, false);
    push_rect(x0, y0, x1, y1, 0, 0, 0, 0, color);
}

static Texture* grab_into(Texture* dst) {
    flush();
    if (g_target == Surface::Screen) {
        save_efb(Surface::Screen);
        GX_SetTexCopySrc(0, 0, SCREEN_W, SCREEN_H);
        GX_SetTexCopyDst(SCREEN_W, SCREEN_H, GX_TF_RGBA8, GX_FALSE);
        GX_CopyTex(dst->data, GX_FALSE);
        GX_PixModeSync();
    } else {
        GX_DrawDone();
        Texture& src = g_surface[(int)Surface::Screen].tex;
        DCInvalidateRange(src.data, src.bytes);
        std::memcpy(dst->data, src.data, std::min(src.bytes, dst->bytes));
        DCFlushRange(dst->data, dst->bytes);
    }
    g_invalidate = true;
    return dst;
}

void desaturate(float amount) {
    if (amount <= 0 || !g_frame_open || scissor_empty()) return;
    if (amount > 1) amount = 1;
    Texture* g = grab_into(&g_surface[(int)Surface::Grab].tex);
    g_surface[(int)Surface::Grab].valid = true;
    int level = (int)(amount * 255.0f + 0.5f);
    Vtx q[4] = {{0, 0, 0, 0, 0xFFFFFFFFu},
                {(float)SCREEN_W, 0, (float)SCREEN_W / g->tw, 0, 0xFFFFFFFFu},
                {(float)SCREEN_W, (float)SCREEN_H, (float)SCREEN_W / g->tw, (float)SCREEN_H / g->th, 0xFFFFFFFFu},
                {0, (float)SCREEN_H, 0, (float)SCREEN_H / g->th, 0xFFFFFFFFu}};
    setup_tev(Src::Gray, Term::None, 0, level);
    set_blend(Blend::Copy);
    bind(g, false, false);
    emit(q, 4, true, 0.0f);
    g_batches++;
}

Texture* grab_screen() {
    Texture* g = grab_into(&g_surface[(int)Surface::Grab].tex);
    g_surface[(int)Surface::Grab].valid = true;
    return g;
}

void set_target(Surface s) {
    flush();
    if (s != g_target && g_frame_open) {
        save_efb(g_target);
        g_target = s;
        load_efb(s);
    }
    g_target = s;
    g_sx0 = 0;
    g_sy0 = 0;
    g_sx1 = g_surface[(int)s].w;
    g_sy1 = g_surface[(int)s].h;
    apply_scissor();
}

Surface current_target() { return g_target; }

Texture* surface_texture(Surface s) {
    flush();
    if (g_frame_open && s == g_target) save_efb(s);
    return &g_surface[(int)s].tex;
}

int surface_width(Surface s) { return g_surface[(int)s].w; }
int surface_height(Surface s) { return g_surface[(int)s].h; }

void set_color_mask(uint32_t protect_rgb) {
    flush();
    g_protect = protect_rgb & 0xFFFFFFu;
}

void set_wrap(bool repeat) {
    if (repeat == g_wrap) return;
    flush();
    g_wrap = repeat;
}

void set_stencil(Stencil mode) {
    flush();
    g_stencil = mode;
    apply_stencil(mode);
}

void stencil_clear(int x0, int y0, int x1, int y1) {
    if (x1 <= x0 || y1 <= y0 || !g_frame_open || scissor_empty()) return;
    flush();
    GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
    GX_SetZCompLoc(GX_TRUE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetColorUpdate(GX_FALSE);
    Vtx q[4] = {{(float)x0, (float)y0, 0, 0, 0xFFFFFFFFu},
                {(float)x1, (float)y0, 0, 0, 0xFFFFFFFFu},
                {(float)x1, (float)y1, 0, 0, 0xFFFFFFFFu},
                {(float)x0, (float)y1, 0, 0, 0xFFFFFFFFu}};
    setup_tev(Src::Color, Term::None, 0, 0);
    set_blend(Blend::Copy);
    emit(q, 4, true, -1.0f);
    GX_SetColorUpdate(GX_TRUE);
    apply_stencil(g_stencil);
}

void draw_triangles(Texture* tex, const Vertex* v, int count, Blend blend, bool smooth) {
    count -= count % 3;
    for (int i = 0; i < count; i += 3) {
        prepare(tex, blend, smooth, false, 3, false);
        for (int k = 0; k < 3; k++) g_batch[g_count++] = {v[i + k].x, v[i + k].y, nu(tex, v[i + k].u), nv(tex, v[i + k].v), v[i + k].color};
    }
}

void draw_grid(Texture* tex, const Vertex* v, int cols, int rows, Blend blend, bool smooth) {
    if (cols <= 0 || rows <= 0) return;
    int stride = cols + 1;
    auto at = [&](int c, int r) {
        const Vertex& s = v[(size_t)r * stride + c];
        return Vtx{s.x, s.y, nu(tex, s.u), nv(tex, s.v), s.color};
    };
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            prepare(tex, blend, smooth, false, 6, false);
            Vtx p00 = at(c, r), p10 = at(c + 1, r), p01 = at(c, r + 1), p11 = at(c + 1, r + 1);
            push_triangle(p00, p01, p10);
            push_triangle(p10, p01, p11);
        }
    }
}

void sprite_lut(Texture* tex, int channel, const uint32_t* lut, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, Blend blend) {
    if (!tex || !lut || !g_frame_open) return;
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    flush();
    if (tex->surface && tex == &g_surface[(int)g_target].tex) save_efb(g_target);
    int tx0 = std::clamp((int)std::floor(std::min(u0, u1)), 0, tex->w), tx1 = std::clamp((int)std::ceil(std::max(u0, u1)), 0, tex->w);
    int ty0 = std::clamp((int)std::floor(std::min(v0, v1)), 0, tex->h), ty1 = std::clamp((int)std::ceil(std::max(v0, v1)), 0, tex->h);
    if (tx1 <= tx0 || ty1 <= ty0) return;
    GX_DrawDone();
    if (!tex->cpu) DCInvalidateRange(tex->data, tex->bytes);
    Texture lt;
    lt.w = tx1 - tx0;
    lt.h = ty1 - ty0;
    lt.tw = (lt.w + 3) & ~3;
    lt.th = (lt.h + 3) & ~3;
    lt.bytes = (size_t)lt.tw * lt.th * 4;
    lt.data = (uint8_t*)ring_alloc(lt.bytes);
    int shift = std::clamp(channel, 0, 3) * 8;
    for (int y = 0; y < lt.th; y++) {
        int sy = ty0 + std::min(y, lt.h - 1);
        for (int x = 0; x < lt.tw; x++) {
            int sx = tx0 + std::min(x, lt.w - 1);
            uint32_t c = tex->cpu ? tex->cpu[(size_t)sy * tex->cpu_stride + sx] : get_texel(tex->data, tex->tw, sx, sy);
            put_texel(lt.data, lt.tw, x, y, lut[(c >> shift) & 255]);
        }
    }
    DCFlushRange(lt.data, lt.bytes);
    g_invalidate = true;
    init_obj(&lt);
    float lu0 = (u0 - tx0) / lt.tw, lu1 = (u1 - tx0) / lt.tw, lv0 = (v0 - ty0) / lt.th, lv1 = (v1 - ty0) / lt.th;
    Vtx q[4] = {{x0, y0, lu0, lv0, 0xFFFFFFFFu}, {x1, y0, lu1, lv0, 0xFFFFFFFFu}, {x1, y1, lu1, lv1, 0xFFFFFFFFu}, {x0, y1, lu0, lv1, 0xFFFFFFFFu}};
    bool wrap = g_wrap;
    g_wrap = false;
    draw_batch(Src::Texture, &lt, blend, false, q, 4, true);
    g_wrap = wrap;
}

}
