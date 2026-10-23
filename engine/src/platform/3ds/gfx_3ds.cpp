#include "../gfx.h"
#include "../common/border_tiles.h"

#include <3ds.h>
#include <citro3d.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../platform.h"
#include "sprite_shbin.h"
#include "sys3ds.h"

extern "C" {
extern u32 __ctru_linear_heap;
extern u32 __ctru_linear_heap_size;
}

namespace gfx {

struct Texture {
    int w = 0;
    int h = 0;
    int tw = 0;
    int th = 0;
    C3D_Tex tex{};
    uint32_t* data = nullptr;
    const uint32_t* src = nullptr;
    int src_stride = 0;
    size_t bytes = 0;
    bool owned = true;
    bool wrapped = false;
    bool dirty = false;
    int surface = -1;
};

struct Vtx {
    float x, y, u, v;
    uint32_t color;
};

enum class Env : uint8_t { None, Tex, Fill, Flat, Desat };

constexpr size_t VTX_BYTES = 768u << 10;
constexpr int VTX_MAX = (int)(VTX_BYTES / sizeof(Vtx));
constexpr int CMDBUF_BYTES = 512 << 10;
constexpr int SURFACE_COUNT = 7;
constexpr u32 DISPLAY_FLAGS = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                              GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
                              GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

static LightLock g_alloc_lock = 1;

struct SurfaceInfo {
    int w, h, tw, th;
    GPU_TEXCOLOR fmt;
    bool stencil;
};

static const SurfaceInfo SURFACES[SURFACE_COUNT] = {{SCREEN_W, SCREEN_H, 512, 256, GPU_RGBA8, true},  {SCREEN_W, SCREEN_H, 512, 256, GPU_RGBA8, true},
                                                    {HALF_W, HALF_H, 256, 128, GPU_RGBA8, true},       {HALF_W, HALF_H, 256, 128, GPU_RGBA8, true},
                                                    {HALF_W, HALF_H, 256, 128, GPU_RGBA8, true},       {SCREEN_W, SCREEN_H, 512, 256, GPU_RGB565, false},
                                                    {SCREEN_W, SCREEN_H, 512, 256, GPU_RGBA8, true}};

static Texture g_surface_tex[SURFACE_COUNT];
static C3D_RenderTarget* g_targets[SURFACE_COUNT];
static bool g_surface_gpu_dirty[SURFACE_COUNT];
static C3D_RenderTarget* g_top = nullptr;
static C3D_RenderTarget* g_bottom = nullptr;
static bool g_bottom_drawn = false;

static DVLB_s* g_dvlb = nullptr;
static shaderProgram_s g_program;
static int g_loc_proj = -1;
static int g_loc_scale = -1;
static C3D_Mtx g_proj[SURFACE_COUNT];
static C3D_Mtx g_top_proj;
static std::vector<Texture*> g_border;
static BorderTiles g_border_tiles;
static std::vector<BorderRect> g_border_bars, g_border_frame;
static uint32_t g_border_line = 0;
static int g_border_scroll = 0;
static C3D_Mtx g_bottom_proj;

static Vtx* g_vtx = nullptr;
static int g_vtx_used = 0;
static int g_vtx_flushed = 0;
static int g_batch_start = 0;
static int g_count = 0;

static bool g_in_frame = false;
static bool g_submitted = false;
static Texture* g_tex = nullptr;
static Blend g_blend = Blend::Normal;
static bool g_smooth = false;
static bool g_flat = false;
static uint32_t g_flat_rgb = 0;

static Texture* g_bound_tex = nullptr;
static bool g_bound_smooth = false;
static bool g_bound_wrap = false;
static Env g_bound_env = Env::None;
static uint32_t g_bound_flat_rgb = 0xFFFFFFFFu;
static int g_bound_blend = -1;
static float g_bound_scale[2] = {0, 0};

static Surface g_target = Surface::Screen;
static int g_sc[4] = {0, 0, SCREEN_W, SCREEN_H};
static bool g_sc_full = true;
static bool g_sc_empty = false;
static uint32_t g_protect = 0;
static bool g_wrap = false;
static Stencil g_stencil = Stencil::Off;
static int g_batches = 0;
static int g_list_peak = 0;

static std::vector<Texture*> g_garbage;
static std::vector<void*> g_scratch;

static uint8_t g_morton_x[64];
static uint8_t g_morton_y[64];

static inline bool in_linear(const void* p) {
    uintptr_t a = (uintptr_t)p;
    return a >= __ctru_linear_heap && a < __ctru_linear_heap + __ctru_linear_heap_size;
}

static void* linear_alloc(size_t bytes) {
    LightLock_Lock(&g_alloc_lock);
    void* p = linearMemAlign(bytes ? bytes : 16, 128);
    LightLock_Unlock(&g_alloc_lock);
    return p;
}

void* alloc(size_t bytes) {
    void* p = linear_alloc(bytes);
    if (!p && plat::relieve_memory()) {
        p = linear_alloc(bytes);
        if (p) plat::log("low memory: freed caches for %u bytes of gpu memory", (unsigned)bytes);
    }
    return p;
}

void release(void* p) {
    if (!p) return;
    if (in_linear(p)) {
        LightLock_Lock(&g_alloc_lock);
        linearFree(p);
        LightLock_Unlock(&g_alloc_lock);
    } else {
        std::free(p);
    }
}

size_t arena_free_bytes() {
    LightLock_Lock(&g_alloc_lock);
    size_t n = linearSpaceFree();
    LightLock_Unlock(&g_alloc_lock);
    return n;
}

void* arena_try_alloc(size_t bytes) {
    if (LightLock_TryLock(&g_alloc_lock) != 0) return nullptr;
    void* p = linearMemAlign(bytes ? bytes : 16, 128);
    LightLock_Unlock(&g_alloc_lock);
    return p;
}

static int pow2(int n) {
    int p = 8;
    while (p < n) p <<= 1;
    return p;
}

static inline uint32_t to_pica(uint32_t c) { return __builtin_bswap32(c); }

static inline size_t tile_index(int x, int row, int tw) { return ((size_t)(row >> 3) * (size_t)(tw >> 3) + (size_t)(x >> 3)) * 64; }

static inline uint32_t tiled_at(const uint32_t* data, int tw, int th, int x, int y) { return data[tiled_index(x, y, tw, th)]; }

static void tex_setup(Texture* t, uint32_t* data, int tw, int th, GPU_TEXCOLOR fmt) {
    std::memset(&t->tex, 0, sizeof(t->tex));
    t->tex.data = data;
    t->tex.width = (u16)tw;
    t->tex.height = (u16)th;
    t->tex.fmt = fmt;
    t->tex.size = (u32)tw * th * (fmt == GPU_RGBA8 ? 4 : 2);
    t->tex.param = GPU_TEXTURE_MODE(GPU_TEX_2D) | GPU_TEXTURE_MAG_FILTER(GPU_NEAREST) | GPU_TEXTURE_MIN_FILTER(GPU_NEAREST) |
                   GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE);
    t->tex.border = 0;
    t->tex.lodBias = 0;
    t->tex.maxLevel = 0;
    t->tex.minLevel = 0;
    t->tw = tw;
    t->th = th;
    t->data = data;
}

static void tile_from(uint32_t* dst, int tw, int th, int w, int h, const uint32_t* src, int stride, int x0, int y0, int x1, int y1) {
    int rows0 = th - y1, rows1 = th - y0;
    int trow0 = rows0 & ~7, trow1 = (rows1 + 7) & ~7;
    int tcol0 = x0 & ~7, tcol1 = (x1 + 7) & ~7;
    if (tcol1 > tw) tcol1 = tw;
    if (trow1 > th) trow1 = th;
    for (int tr = trow0; tr < trow1; tr += 8) {
        for (int tc = tcol0; tc < tcol1; tc += 8) {
            uint32_t* out = dst + tile_index(tc, tr, tw);
            for (int i = 0; i < 64; i++) {
                int x = tc + g_morton_x[i];
                int y = th - 1 - (tr + g_morton_y[i]);
                if (x >= w) x = w - 1;
                if (y >= h) y = h - 1;
                if (y < 0) y = 0;
                out[i] = to_pica(src[(size_t)y * stride + x]);
            }
        }
    }
}

static void flush_cache(const void* p, size_t bytes) {
    if (p && bytes) GSPGPU_FlushDataCache(p, bytes);
}

static uint32_t expand(Format f, const uint8_t* p, const uint32_t* pal) {
    switch (f) {
        case Format::RGBA8888: return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        case Format::T8: return pal ? pal[p[0]] : 0;
        case Format::RGBA4444: {
            uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
            return rgba((v & 15) * 17, ((v >> 4) & 15) * 17, ((v >> 8) & 15) * 17, ((v >> 12) & 15) * 17);
        }
        case Format::RGB565: {
            uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
            return rgba((v & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, ((v >> 11) & 31) * 255 / 31, 255);
        }
        case Format::RGBA5551: {
            uint16_t v = (uint16_t)(p[0] | (p[1] << 8));
            return rgba((v & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, ((v >> 10) & 31) * 255 / 31, (v >> 15) ? 255 : 0);
        }
    }
    return 0;
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
    if (swizzled && fmt == Format::RGBA8888) {
        tex_setup(t, (uint32_t*)pixels, stride, rows, GPU_RGBA8);
        t->bytes = (size_t)stride * rows * 4;
        flush_cache(pixels, t->bytes);
        release(palette);
        return t;
    }
    int tw = pow2(w), th = pow2(h);
    uint32_t* data = (uint32_t*)alloc((size_t)tw * th * 4);
    if (!data) {
        release(pixels);
        release(palette);
        delete t;
        return nullptr;
    }
    std::vector<uint32_t> lin((size_t)w * h);
    int bpp = bpp_of(fmt);
    const uint8_t* s = (const uint8_t*)pixels;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) lin[(size_t)y * w + x] = expand(fmt, s + ((size_t)y * stride + x) * bpp, palette);
    }
    release(pixels);
    release(palette);
    tex_setup(t, data, tw, th, GPU_RGBA8);
    tile_from(data, tw, th, w, h, lin.data(), w, 0, 0, tw, th);
    t->bytes = (size_t)tw * th * 4;
    flush_cache(data, t->bytes);
    return t;
}

Texture* texture_wrap(int w, int h, int stride, uint32_t* pixels) {
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    t->src = pixels;
    t->src_stride = stride;
    t->wrapped = true;
    t->dirty = true;
    t->tw = pow2(w);
    t->th = pow2(h);
    t->bytes = 0;
    return t;
}

void texture_touch(Texture* t) {
    if (t && t->wrapped) t->dirty = true;
}

static bool refresh(Texture* t) {
    if (t->wrapped && !t->data) {
        uint32_t* data = (uint32_t*)alloc((size_t)t->tw * t->th * 4);
        if (!data) return false;
        std::memset(data, 0, (size_t)t->tw * t->th * 4);
        tex_setup(t, data, t->tw, t->th, GPU_RGBA8);
        t->dirty = true;
    }
    if (!t->dirty || !t->src) return true;
    t->dirty = false;
    tile_from(t->data, t->tw, t->th, t->w, t->h, t->src, t->src_stride, 0, 0, t->w, t->h);
    flush_cache(t->data, (size_t)t->tw * t->th * 4);
    return true;
}

static void sync_gpu();
static void wait_idle();

bool texture_read(const Texture* t, int x, int y, uint32_t* out) {
    if (!t || x < 0 || y < 0 || x >= t->w || y >= t->h) return false;
    if (t->wrapped && t->src) {
        *out = t->src[(size_t)y * t->src_stride + x];
        return true;
    }
    if (t->surface >= 0) {
        if (g_in_frame) {
            if (g_surface_gpu_dirty[t->surface]) sync_gpu();
        } else {
            wait_idle();
        }
        if (SURFACES[t->surface].fmt != GPU_RGBA8) return false;
    }
    if (!t->data) return false;
    *out = to_pica(tiled_at(t->data, t->tw, t->th, x, y));
    return true;
}

void texture_destroy(Texture* t) {
    if (!t) return;
    g_garbage.push_back(t);
}

static void collect_garbage() {
    for (Texture* t : g_garbage) {
        if (g_bound_tex == t) g_bound_tex = nullptr;
        if (g_tex == t) g_tex = nullptr;
        if (t->owned) release(t->data);
        delete t;
    }
    g_garbage.clear();
    for (void* p : g_scratch) release(p);
    g_scratch.clear();
}

size_t texture_bytes(const Texture* t) { return t ? t->bytes : 0; }
int texture_width(const Texture* t) { return t ? t->w : 0; }
int texture_height(const Texture* t) { return t ? t->h : 0; }

static void set_env(Env e, uint32_t flat_rgb) {
    if (g_bound_env == e && (e != Env::Flat || g_bound_flat_rgb == flat_rgb)) return;
    for (int i = 0; i < 6; i++) {
        C3D_TexEnv* env = C3D_GetTexEnv(i);
        C3D_TexEnvInit(env);
    }
    C3D_TexEnv* e0 = C3D_GetTexEnv(0);
    switch (e) {
        case Env::Tex:
            C3D_TexEnvSrc(e0, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(e0, C3D_Both, GPU_MODULATE);
            break;
        case Env::Fill:
            C3D_TexEnvSrc(e0, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(e0, C3D_Both, GPU_REPLACE);
            break;
        case Env::Flat:
            C3D_TexEnvSrc(e0, C3D_RGB, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
            C3D_TexEnvFunc(e0, C3D_RGB, GPU_REPLACE);
            C3D_TexEnvSrc(e0, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(e0, C3D_Alpha, GPU_MODULATE);
            C3D_TexEnvColor(e0, flat_rgb | 0xFF000000u);
            break;
        case Env::Desat:
        case Env::None: break;
    }
    g_bound_env = e;
    g_bound_flat_rgb = flat_rgb;
}

static void set_desat_env(int level) {
    for (int i = 0; i < 6; i++) C3D_TexEnvInit(C3D_GetTexEnv(i));
    uint32_t third = rgba(85, 85, 85, 255);
    C3D_TexEnv* e0 = C3D_GetTexEnv(0);
    C3D_TexEnvSrc(e0, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_CONSTANT);
    C3D_TexEnvOpRgb(e0, GPU_TEVOP_RGB_SRC_R, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(e0, C3D_RGB, GPU_MODULATE);
    C3D_TexEnvSrc(e0, C3D_Alpha, GPU_TEXTURE0, GPU_TEXTURE0, GPU_TEXTURE0);
    C3D_TexEnvFunc(e0, C3D_Alpha, GPU_REPLACE);
    C3D_TexEnvColor(e0, third);
    C3D_TexEnv* e1 = C3D_GetTexEnv(1);
    C3D_TexEnvSrc(e1, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(e1, GPU_TEVOP_RGB_SRC_G, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(e1, C3D_RGB, GPU_MULTIPLY_ADD);
    C3D_TexEnvColor(e1, third);
    C3D_TexEnv* e2 = C3D_GetTexEnv(2);
    C3D_TexEnvSrc(e2, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_PREVIOUS);
    C3D_TexEnvOpRgb(e2, GPU_TEVOP_RGB_SRC_B, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR);
    C3D_TexEnvFunc(e2, C3D_RGB, GPU_MULTIPLY_ADD);
    C3D_TexEnvColor(e2, third);
    C3D_TexEnv* e3 = C3D_GetTexEnv(3);
    C3D_TexEnvSrc(e3, C3D_RGB, GPU_PREVIOUS, GPU_TEXTURE0, GPU_CONSTANT);
    C3D_TexEnvFunc(e3, C3D_RGB, GPU_INTERPOLATE);
    C3D_TexEnvColor(e3, rgba(level, level, level, 255));
    g_bound_env = Env::Desat;
}

static int write_mask() {
    int m = 0;
    if (!(g_protect & 0x0000FFu)) m |= GPU_WRITE_RED;
    if (!(g_protect & 0x00FF00u)) m |= GPU_WRITE_GREEN;
    if (!(g_protect & 0xFF0000u)) m |= GPU_WRITE_BLUE;
    return m;
}

static void apply_blend(Blend b, bool flat) {
    int key = (int)b | (flat ? 0x100 : 0);
    if (key == g_bound_blend) return;
    g_bound_blend = key;
    if (flat) {
        switch (b) {
            case Blend::Add: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE); return;
            case Blend::Subtract: C3D_AlphaBlend(GPU_BLEND_REVERSE_SUBTRACT, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE); return;
            default: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ZERO, GPU_ONE); return;
        }
    }
    switch (b) {
        case Blend::Normal: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA, GPU_ZERO, GPU_ONE); break;
        case Blend::Add: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE); break;
        case Blend::Multiply: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ONE_MINUS_SRC_ALPHA, GPU_ZERO, GPU_ONE); break;
        case Blend::Screen: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ONE_MINUS_SRC_COLOR, GPU_ZERO, GPU_ONE); break;
        case Blend::Subtract: C3D_AlphaBlend(GPU_BLEND_REVERSE_SUBTRACT, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE); break;
        case Blend::Copy: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ZERO, GPU_ONE); break;
        case Blend::MulSrc: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ZERO, GPU_SRC_COLOR, GPU_ZERO, GPU_ONE); break;
        case Blend::MulAdd: C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ONE, GPU_ZERO, GPU_ONE); break;
        case Blend::Max: C3D_AlphaBlend(GPU_BLEND_MAX, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE); break;
        case Blend::Min: C3D_AlphaBlend(GPU_BLEND_MIN, GPU_BLEND_ADD, GPU_ONE, GPU_ONE, GPU_ZERO, GPU_ONE); break;
        case Blend::MulSub: C3D_AlphaBlend(GPU_BLEND_REVERSE_SUBTRACT, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ONE, GPU_ZERO, GPU_ONE); break;
    }
}

static bool target_has_stencil(Surface s) { return SURFACES[(int)s].stencil; }

static void apply_stencil(Stencil mode) {
    bool st = target_has_stencil(g_target);
    switch (mode) {
        case Stencil::Off:
            C3D_StencilTest(false, GPU_ALWAYS, 0, 0xFF, 0);
            C3D_AlphaTest(false, GPU_ALWAYS, 0);
            break;
        case Stencil::Write:
            C3D_StencilTest(st, GPU_ALWAYS, 0xFF, 0xFF, 0xFF);
            C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_REPLACE);
            C3D_AlphaTest(true, GPU_GREATER, 0);
            break;
        case Stencil::Test:
            C3D_StencilTest(st, GPU_EQUAL, 0xFF, 0xFF, 0);
            C3D_StencilOp(GPU_STENCIL_KEEP, GPU_STENCIL_KEEP, GPU_STENCIL_KEEP);
            C3D_AlphaTest(false, GPU_ALWAYS, 0);
            break;
    }
}

static void apply_scissor() {
    if (g_sc_full || g_sc_empty) C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    else C3D_SetScissor(GPU_SCISSOR_NORMAL, (u32)g_sc[0], (u32)g_sc[1], (u32)g_sc[2], (u32)g_sc[3]);
}

static void bind_target(Surface s) {
    C3D_FrameDrawOn(g_targets[(int)s]);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_loc_proj, &g_proj[(int)s]);
    g_bound_tex = nullptr;
}

static void flush_vertices() {
    if (g_vtx_used > g_vtx_flushed) {
        flush_cache(g_vtx + g_vtx_flushed, (size_t)(g_vtx_used - g_vtx_flushed) * sizeof(Vtx));
        g_vtx_flushed = g_vtx_used;
    }
}

static void sync_gpu() {
    if (!g_in_frame) return;
    flush_vertices();
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    C3D_FrameBegin(0);
    g_vtx_used = g_vtx_flushed = 0;
    g_batch_start = 0;
    for (bool& d : g_surface_gpu_dirty) d = false;
    collect_garbage();
    bind_target(g_target);
    apply_scissor();
}

static void wait_idle() {
    if (g_in_frame) return;
    C3D_FrameBegin(0);
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    for (bool& d : g_surface_gpu_dirty) d = false;
}

static void flush() {
    if (!g_count) return;
    int count = g_count;
    int start = g_batch_start;
    g_count = 0;
    g_batch_start = g_vtx_used;
    bool drop = g_sc_empty || (g_stencil == Stencil::Test && !target_has_stencil(g_target));
    if (drop) return;
    g_batches++;
    Texture* t = g_tex;
    if (t && !refresh(t)) return;
    if (t) {
        bool wrap = g_wrap;
        if (g_bound_tex != t || g_bound_smooth != g_smooth || g_bound_wrap != wrap) {
            GPU_TEXTURE_FILTER_PARAM f = g_smooth ? GPU_LINEAR : GPU_NEAREST;
            GPU_TEXTURE_WRAP_PARAM w = wrap ? GPU_REPEAT : GPU_CLAMP_TO_EDGE;
            C3D_TexSetFilter(&t->tex, f, f);
            C3D_TexSetWrap(&t->tex, w, w);
            C3D_TexBind(0, &t->tex);
            g_bound_tex = t;
            g_bound_smooth = g_smooth;
            g_bound_wrap = wrap;
        }
        float sx = 1.0f / t->tw, sy = 1.0f / t->th;
        if (g_bound_scale[0] != sx || g_bound_scale[1] != sy) {
            C3D_FVUnifSet(GPU_VERTEX_SHADER, g_loc_scale, sx, sy, 1.0f, 1.0f);
            g_bound_scale[0] = sx;
            g_bound_scale[1] = sy;
        }
        set_env(g_flat ? Env::Flat : Env::Tex, g_flat_rgb);
    } else {
        set_env(Env::Fill, 0);
    }
    apply_blend(g_blend, g_flat && t);
    C3D_DrawArrays(GPU_TRIANGLES, start, count);
    g_surface_gpu_dirty[(int)g_target] = true;
    if ((int)gpuCmdBufOffset * 4 > g_list_peak) g_list_peak = (int)gpuCmdBufOffset * 4;
    if (gpuCmdBufOffset + 1024 > gpuCmdBufSize) sync_gpu();
}

static Vtx* prepare(Texture* tex, Blend blend, bool smooth, int verts, bool flat = false, uint32_t flat_rgb = 0) {
    if (g_count && (g_tex != tex || g_blend != blend || g_smooth != smooth || g_flat != flat || (flat && g_flat_rgb != flat_rgb))) flush();
    if (g_vtx_used + verts > VTX_MAX) {
        flush();
        sync_gpu();
    }
    g_tex = tex;
    g_blend = blend;
    g_smooth = smooth;
    g_flat = flat;
    g_flat_rgb = flat_rgb;
    if (!g_count) g_batch_start = g_vtx_used;
    Vtx* v = g_vtx + g_vtx_used;
    g_vtx_used += verts;
    g_count += verts;
    return v;
}

static void init_morton() {
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            size_t m = tiled_index(x, 7 - y, 8, 8);
            g_morton_x[m] = (uint8_t)x;
            g_morton_y[m] = (uint8_t)y;
        }
    }
}

void init() {
    init_morton();
    gfxInitDefault();
    gfxSet3D(false);
    C3D_Init(CMDBUF_BYTES);
    g_top = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, C3D_DEPTHTYPE(GPU_RB_DEPTH24_STENCIL8));
    C3D_RenderTargetSetOutput(g_top, GFX_TOP, GFX_LEFT, DISPLAY_FLAGS);
    for (int i = 0; i < SURFACE_COUNT; i++) {
        const SurfaceInfo& si = SURFACES[i];
        Texture& t = g_surface_tex[i];
        bool bottom = i == (int)Surface::Bottom;
        if (!C3D_TexInitVRAM(&t.tex, (u16)si.tw, (u16)si.th, si.fmt) && bottom) continue;
        t.w = si.w;
        t.h = si.h;
        t.tw = si.tw;
        t.th = si.th;
        t.data = (uint32_t*)t.tex.data;
        t.owned = false;
        t.surface = i;
        g_targets[i] = C3D_RenderTargetCreateFromTex(&t.tex, GPU_TEXFACE_2D, 0, C3D_DEPTHTYPE(GPU_RB_DEPTH24_STENCIL8));
        Mtx_Ortho(&g_proj[i], 0.0f, (float)si.tw, 0.0f, (float)si.th, -1.0f, 1.0f, true);
        if (bottom) g_bottom = g_targets[i] ? C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, C3D_DEPTHTYPE(-1)) : nullptr;
        if (bottom && !g_bottom) {
            if (g_targets[i]) C3D_RenderTargetDelete(g_targets[i]);
            g_targets[i] = nullptr;
            C3D_TexDelete(&t.tex);
        }
    }
    if (g_bottom) C3D_RenderTargetSetOutput(g_bottom, GFX_BOTTOM, GFX_LEFT, DISPLAY_FLAGS);
    plat::log("gfx: bottom screen %s, %u KB vram free", g_bottom ? "on" : "off", (unsigned)(vramSpaceFree() / 1024));
    Mtx_OrthoTilt(&g_top_proj, 0.0f, 400.0f, 240.0f, 0.0f, -1.0f, 1.0f, true);
    Mtx_OrthoTilt(&g_bottom_proj, 0.0f, 320.0f, 240.0f, 0.0f, -1.0f, 1.0f, true);
    g_dvlb = DVLB_ParseFile((u32*)sprite_shbin, sprite_shbin_size);
    shaderProgramInit(&g_program);
    shaderProgramSetVsh(&g_program, &g_dvlb->DVLE[0]);
    C3D_BindProgram(&g_program);
    g_loc_proj = shaderInstanceGetUniformLocation(g_program.vertexShader, "projection");
    g_loc_scale = shaderInstanceGetUniformLocation(g_program.vertexShader, "texscale");
    C3D_AttrInfo* ai = C3D_GetAttrInfo();
    AttrInfo_Init(ai);
    AttrInfo_AddLoader(ai, 0, GPU_FLOAT, 2);
    AttrInfo_AddLoader(ai, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(ai, 2, GPU_UNSIGNED_BYTE, 4);
    g_vtx = (Vtx*)alloc(VTX_BYTES);
    C3D_BufInfo* bi = C3D_GetBufInfo();
    BufInfo_Init(bi);
    BufInfo_Add(bi, g_vtx, sizeof(Vtx), 3, 0x210);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_DepthTest(false, GPU_ALWAYS, (GPU_WRITEMASK)write_mask());
    C3D_EarlyDepthTest(false, GPU_EARLYDEPTH_GREATER, 0);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, g_loc_scale, 1.0f, 1.0f, 1.0f, 1.0f);
    C3D_FrameBegin(0);
    for (int i = 0; i < SURFACE_COUNT; i++) {
        if (g_targets[i]) C3D_RenderTargetClear(g_targets[i], C3D_CLEAR_ALL, 0x000000FF, 0xFF000000u);
    }
    C3D_RenderTargetClear(g_top, C3D_CLEAR_ALL, 0x000000FF, 0);
    C3D_FrameEnd(0);
    for (int i = 0; i < 2; i++) {
        u16 fw = 0, fh = 0;
        u8* fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fw, &fh);
        if (fb) std::memset(fb, 0, (size_t)fw * fh * 3);
        gfxFlushBuffers();
        gfxScreenSwapBuffers(GFX_BOTTOM, false);
        gspWaitForVBlank();
    }
}

void shutdown() {
    if (g_in_frame) end_frame();
    wait_idle();
    collect_garbage();
    for (int i = 0; i < SURFACE_COUNT; i++) {
        if (!g_targets[i]) continue;
        C3D_RenderTargetDelete(g_targets[i]);
        C3D_TexDelete(&g_surface_tex[i].tex);
    }
    if (g_top) C3D_RenderTargetDelete(g_top);
    if (g_bottom) C3D_RenderTargetDelete(g_bottom);
    shaderProgramFree(&g_program);
    if (g_dvlb) DVLB_Free(g_dvlb);
    C3D_Fini();
    gfxExit();
}

static void reset_state() {
    g_bound_tex = nullptr;
    g_bound_env = Env::None;
    g_bound_blend = -1;
    g_bound_scale[0] = g_bound_scale[1] = 0;
    g_protect = 0;
    g_wrap = false;
    g_stencil = Stencil::Off;
    C3D_DepthTest(false, GPU_ALWAYS, (GPU_WRITEMASK)write_mask());
    apply_stencil(Stencil::Off);
}

void begin_frame(uint32_t clear_color) {
    if (g_in_frame) end_frame();
    C3D_FrameBegin(0);
    g_in_frame = true;
    g_vtx_used = g_vtx_flushed = 0;
    g_batch_start = 0;
    g_count = 0;
    for (bool& d : g_surface_gpu_dirty) d = false;
    collect_garbage();
    uint32_t c = clear_color;
    uint32_t pica = ((c & 0xFF) << 24) | (((c >> 8) & 0xFF) << 16) | (((c >> 16) & 0xFF) << 8) | 0xFF;
    C3D_RenderTargetClear(g_targets[(int)Surface::Screen], C3D_CLEAR_ALL, pica, 0xFF000000u);
    g_target = Surface::Screen;
    bind_target(Surface::Screen);
    reset_state();
    reset_scissor();
}

static void present_begin(C3D_RenderTarget* to, const C3D_Mtx* proj) {
    C3D_FrameDrawOn(to);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_loc_proj, proj);
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    apply_stencil(Stencil::Off);
    g_bound_blend = -1;
    apply_blend(Blend::Copy, false);
}

static void present_rect(Texture* s, float u0, float v0, float x0, float y0, float w, float h) {
    set_env(Env::Tex, 0);
    C3D_TexSetFilter(&s->tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&s->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexBind(0, &s->tex);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, g_loc_scale, 1.0f / s->tw, 1.0f / s->th, 1.0f, 1.0f);
    if (g_vtx_used + 6 > VTX_MAX) sync_gpu();
    Vtx* v = g_vtx + g_vtx_used;
    float x1 = x0 + w, y1 = y0 + h, u1 = u0 + w, v1 = v0 + h;
    v[0] = {x0, y0, u0, v0, 0xFFFFFFFFu};
    v[1] = {x1, y0, u1, v0, 0xFFFFFFFFu};
    v[2] = {x0, y1, u0, v1, 0xFFFFFFFFu};
    v[3] = {x0, y1, u0, v1, 0xFFFFFFFFu};
    v[4] = {x1, y0, u1, v0, 0xFFFFFFFFu};
    v[5] = {x1, y1, u1, v1, 0xFFFFFFFFu};
    C3D_DrawArrays(GPU_TRIANGLES, g_vtx_used, 6);
    g_vtx_used += 6;
}

static void present_fill(const BorderRect& r, uint32_t color) {
    set_env(Env::Fill, 0);
    if (g_vtx_used + 6 > VTX_MAX) sync_gpu();
    Vtx* v = g_vtx + g_vtx_used;
    float x0 = (float)r.x0, y0 = (float)r.y0, x1 = (float)r.x1, y1 = (float)r.y1;
    v[0] = {x0, y0, 0, 0, color};
    v[1] = {x1, y0, 0, 0, color};
    v[2] = {x0, y1, 0, 0, color};
    v[3] = {x0, y1, 0, 0, color};
    v[4] = {x1, y0, 0, 0, color};
    v[5] = {x1, y1, 0, 0, color};
    C3D_DrawArrays(GPU_TRIANGLES, g_vtx_used, 6);
    g_vtx_used += 6;
}

static void present_border() {
    BorderSpan spans[BORDER_SPANS];
    for (const BorderRect& b : g_border_bars) {
        float v0 = (float)(b.y0 - g_border_tiles.y0), h = (float)(b.y1 - b.y0);
        int n = border_spans(g_border_tiles, b, (float)g_border_scroll, spans);
        for (int i = 0; i < n; i++) present_rect(g_border[spans[i].chunk], spans[i].u, v0, spans[i].x0, (float)b.y0, spans[i].x1 - spans[i].x0, h);
    }
    for (const BorderRect& f : g_border_frame) present_fill(f, g_border_line);
}

void border_canvas(float& x0, float& y0, float& x1, float& y1) {
    x0 = 0;
    y0 = 0;
    x1 = (float)SCREEN_W;
    y1 = (float)SCREEN_H;
}

static uint16_t to_565(uint32_t c) {
    uint32_t r = ((c & 255) * 31 + 127) / 255, g = (((c >> 8) & 255) * 63 + 127) / 255, b = (((c >> 16) & 255) * 31 + 127) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static Texture* border_chunk(const BorderArt& art, int k) {
    const BorderTiles& bt = g_border_tiles;
    int w = border_chunk_width(bt, k), h = bt.rows, tw = pow2(w), th = pow2(h);
    uint16_t* data = (uint16_t*)alloc((size_t)tw * th * 2);
    if (!data) return nullptr;
    uint16_t lut[256];
    for (int i = 0; i < 256; i++) lut[i] = to_565(border_mix(art, i));
    std::vector<uint8_t> row((size_t)w);
    for (int y = 0; y < th; y++) {
        if (y < h) border_row(art, bt, k, y, row.data());
        for (int x = 0; x < tw; x++) data[tiled_index(x, y, tw, th)] = lut[row[std::min(x, w - 1)]];
    }
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    tex_setup(t, (uint32_t*)data, tw, th, GPU_RGB565);
    t->bytes = (size_t)tw * th * 2;
    flush_cache(data, t->bytes);
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
    g_border_tiles = border_tiles(*art, bars, nbars, 1024, 0);
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

void set_border_scroll(float x) { g_border_scroll = (int)std::floor(x); }

void end_frame() {
    if (!g_in_frame) return;
    flush();
    present_begin(g_top, &g_top_proj);
    Texture* screen = &g_surface_tex[(int)Surface::Screen];
    if (!g_border.empty()) {
        float x0 = profile::OFFSET_X, y0 = profile::OFFSET_Y, w = 640 * profile::SCALE, h = 480 * profile::SCALE;
        present_rect(screen, x0, y0, x0, y0, w, h);
        present_border();
    } else {
        present_rect(screen, 0, 0, 0, 0, (float)SCREEN_W, (float)SCREEN_H);
    }
    if (g_bottom_drawn) {
        present_begin(g_bottom, &g_bottom_proj);
        present_rect(&g_surface_tex[(int)Surface::Bottom], (SCREEN_W - 320) / 2.0f, 0, 0, 0, 320.0f, (float)SCREEN_H);
    }
    g_bottom_drawn = false;
    flush_vertices();
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    g_in_frame = false;
    g_submitted = true;
    g_target = Surface::Screen;
    g_bound_tex = nullptr;
    g_bound_env = Env::None;
    g_bound_blend = -1;
    g_bound_scale[0] = g_bound_scale[1] = 0;
}

void present() {
    if (!g_submitted) return;
    g_submitted = false;
    C3D_FrameSync();
    wait_idle();
}

int list_peak_bytes() { return g_list_peak; }

double take_batch_count() {
    double n = g_batches;
    g_batches = 0;
    return n;
}

bool reclaim_memory() {
    if (g_in_frame) return false;
    wait_idle();
    collect_garbage();
    return true;
}

void set_evict_handler(bool (*)()) {}

const uint32_t* framebuffer() { return nullptr; }

static uint32_t read_surface(int s, int x, int y) {
    const Texture& t = g_surface_tex[s];
    return to_pica(tiled_at(t.data, t.tw, t.th, x, y));
}

bool capture_row(int y, int x, int w, uint32_t* out) {
    if (y < 0 || y >= SCREEN_H || x < 0 || x + w > SCREEN_W) return false;
    if (g_in_frame) sync_gpu();
    else wait_idle();
    for (int i = 0; i < w; i++) out[i] = read_surface((int)Surface::Screen, x + i, y);
    return true;
}

bool capture(uint32_t* out, int out_stride) {
    if (g_in_frame) sync_gpu();
    else wait_idle();
    for (int y = 0; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) out[(size_t)y * out_stride + x] = read_surface((int)Surface::Screen, x, y);
    }
    return true;
}

void set_scissor(int x, int y, int w, int h) {
    flush();
    const SurfaceInfo& si = SURFACES[(int)g_target];
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > si.w) x1 = si.w;
    if (y1 > si.h) y1 = si.h;
    g_sc_empty = x1 <= x || y1 <= y;
    g_sc_full = !g_sc_empty && x == 0 && y == 0 && x1 == si.w && y1 == si.h;
    g_sc[0] = x;
    g_sc[1] = y;
    g_sc[2] = x1;
    g_sc[3] = y1;
    apply_scissor();
}

void reset_scissor() {
    flush();
    const SurfaceInfo& si = SURFACES[(int)g_target];
    g_sc[0] = 0;
    g_sc[1] = 0;
    g_sc[2] = si.w;
    g_sc[3] = si.h;
    g_sc_empty = false;
    g_sc_full = true;
    apply_scissor();
}

static inline void clamp_uv(const Texture* t, bool smooth, float& u, float& v) {
    if (!smooth) return;
    float mu = (float)t->w - 0.5f, mv = (float)t->h - 0.5f;
    if (u > mu) u = mu;
    if (v > mv) v = mv;
}

static inline void put_quad(Vtx* v, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t c) {
    v[0] = {x0, y0, u0, v0, c};
    v[1] = {x1, y0, u1, v0, c};
    v[2] = {x0, y1, u0, v1, c};
    v[3] = {x0, y1, u0, v1, c};
    v[4] = {x1, y0, u1, v0, c};
    v[5] = {x1, y1, u1, v1, c};
}

void sprite(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    clamp_uv(tex, smooth, u0, v0);
    clamp_uv(tex, smooth, u1, v1);
    put_quad(prepare(tex, blend, smooth, 6), x0, y0, x1, y1, u0, v0, u1, v1, color);
}

static void quad_vertices(Vtx* v, const Texture* tex, const float uv[8], const float xy[8], uint32_t c, bool smooth) {
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 6; i++) {
        int k = idx[i];
        float u = uv[k * 2], vv = uv[k * 2 + 1];
        clamp_uv(tex, smooth, u, vv);
        v[i] = {xy[k * 2], xy[k * 2 + 1], u, vv, c};
    }
}

void draw_quad(Texture* tex, const float uv[8], const float xy[8], uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    quad_vertices(prepare(tex, blend, smooth, 6), tex, uv, xy, color, smooth);
}

void sprite_flat(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t rgb, int alpha, Blend blend,
                 bool smooth) {
    if (!tex || alpha <= 0) return;
    clamp_uv(tex, smooth, u0, v0);
    clamp_uv(tex, smooth, u1, v1);
    uint32_t c = (rgb & 0xFFFFFFu) | ((uint32_t)(alpha > 255 ? 255 : alpha) << 24);
    put_quad(prepare(tex, blend, smooth, 6, true, rgb & 0xFFFFFFu), x0, y0, x1, y1, u0, v0, u1, v1, c);
}

void quad_flat(Texture* tex, const float uv[8], const float xy[8], uint32_t rgb, int alpha, Blend blend, bool smooth) {
    if (!tex || alpha <= 0) return;
    uint32_t c = (rgb & 0xFFFFFFu) | ((uint32_t)(alpha > 255 ? 255 : alpha) << 24);
    quad_vertices(prepare(tex, blend, smooth, 6, true, rgb & 0xFFFFFFu), tex, uv, xy, c, smooth);
}

void fill(float x0, float y0, float x1, float y1, uint32_t color, Blend blend) {
    put_quad(prepare(nullptr, blend, false, 6), x0, y0, x1, y1, 0, 0, 0, 0, color);
}

static void copy_surface(Surface from, Surface to) {
    flush();
    Surface keep = g_target;
    bool full = g_sc_full, empty = g_sc_empty;
    int sc[4] = {g_sc[0], g_sc[1], g_sc[2], g_sc[3]};
    Stencil st = g_stencil;
    uint32_t protect = g_protect;
    bool wrap = g_wrap;
    if (st != Stencil::Off) apply_stencil(Stencil::Off);
    if (protect) C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    g_target = to;
    bind_target(to);
    g_sc_full = true;
    g_sc_empty = false;
    apply_scissor();
    g_stencil = Stencil::Off;
    g_protect = 0;
    g_wrap = false;
    Texture* src = &g_surface_tex[(int)from];
    put_quad(prepare(src, Blend::Copy, false, 6), 0, 0, (float)src->w, (float)src->h, 0, 0, (float)src->w, (float)src->h, 0xFFFFFFFFu);
    flush();
    g_target = keep;
    bind_target(keep);
    g_sc_full = full;
    g_sc_empty = empty;
    std::memcpy(g_sc, sc, sizeof(sc));
    apply_scissor();
    g_stencil = st;
    g_protect = protect;
    g_wrap = wrap;
    if (st != Stencil::Off) apply_stencil(st);
    C3D_DepthTest(false, GPU_ALWAYS, (GPU_WRITEMASK)write_mask());
}

void desaturate(float amount) {
    if (amount <= 0) return;
    if (amount > 1) amount = 1;
    copy_surface(Surface::Screen, Surface::Grab);
    Texture* src = &g_surface_tex[(int)Surface::Grab];
    C3D_TexSetFilter(&src->tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&src->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    C3D_TexBind(0, &src->tex);
    g_bound_tex = nullptr;
    C3D_FVUnifSet(GPU_VERTEX_SHADER, g_loc_scale, 1.0f / src->tw, 1.0f / src->th, 1.0f, 1.0f);
    g_bound_scale[0] = g_bound_scale[1] = 0;
    set_desat_env((int)(amount * 255.0f + 0.5f));
    apply_blend(Blend::Copy, false);
    if (!g_sc_empty) {
        if (g_vtx_used + 6 > VTX_MAX) sync_gpu();
        Vtx* v = g_vtx + g_vtx_used;
        put_quad(v, 0, 0, (float)SCREEN_W, (float)SCREEN_H, 0, 0, (float)SCREEN_W, (float)SCREEN_H, 0xFFFFFFFFu);
        C3D_DrawArrays(GPU_TRIANGLES, g_vtx_used, 6);
        g_vtx_used += 6;
        g_batch_start = g_vtx_used;
        g_surface_gpu_dirty[(int)g_target] = true;
    }
}

Texture* grab_screen() {
    copy_surface(Surface::Screen, Surface::Grab);
    return &g_surface_tex[(int)Surface::Grab];
}

void set_target(Surface s) {
    flush();
    if (s == Surface::Bottom) {
        if (!g_bottom) s = Surface::Screen;
        else if (!g_bottom_drawn) {
            C3D_RenderTargetClear(g_targets[(int)s], C3D_CLEAR_ALL, 0x000000FF, 0xFF000000u);
            g_bottom_drawn = true;
        }
    }
    g_target = s;
    bind_target(s);
    if (g_stencil != Stencil::Off) apply_stencil(g_stencil);
    reset_scissor();
}

Surface current_target() { return g_target; }

Texture* surface_texture(Surface s) { return &g_surface_tex[(int)s]; }
int surface_width(Surface s) { return SURFACES[(int)s].w; }
int surface_height(Surface s) { return SURFACES[(int)s].h; }

void set_color_mask(uint32_t protect_rgb) {
    flush();
    g_protect = protect_rgb & 0xFFFFFFu;
    C3D_DepthTest(false, GPU_ALWAYS, (GPU_WRITEMASK)write_mask());
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
    if (!target_has_stencil(g_target) || x1 <= x0 || y1 <= y0) return;
    flush();
    if (g_sc_empty) return;
    C3D_StencilTest(true, GPU_ALWAYS, 0, 0xFF, 0xFF);
    C3D_StencilOp(GPU_STENCIL_REPLACE, GPU_STENCIL_REPLACE, GPU_STENCIL_REPLACE);
    C3D_AlphaTest(false, GPU_ALWAYS, 0);
    C3D_DepthTest(false, GPU_ALWAYS, (GPU_WRITEMASK)0);
    set_env(Env::Fill, 0);
    apply_blend(Blend::Copy, false);
    if (g_vtx_used + 6 > VTX_MAX) sync_gpu();
    Vtx* v = g_vtx + g_vtx_used;
    put_quad(v, (float)x0, (float)y0, (float)x1, (float)y1, 0, 0, 0, 0, 0xFFFFFFFFu);
    C3D_DrawArrays(GPU_TRIANGLES, g_vtx_used, 6);
    g_vtx_used += 6;
    g_batch_start = g_vtx_used;
    C3D_DepthTest(false, GPU_ALWAYS, (GPU_WRITEMASK)write_mask());
    apply_stencil(g_stencil);
}

void draw_triangles(Texture* tex, const Vertex* v, int count, Blend blend, bool smooth) {
    count -= count % 3;
    int done = 0;
    while (done < count) {
        int n = count - done;
        if (n > 3072) n = 3072;
        Vtx* out = prepare(tex, blend, smooth, n);
        for (int i = 0; i < n; i++) {
            const Vertex& s = v[done + i];
            out[i] = {s.x, s.y, s.u, s.v, s.color};
        }
        done += n;
    }
}

void draw_grid(Texture* tex, const Vertex* v, int cols, int rows, Blend blend, bool smooth) {
    if (cols <= 0 || rows <= 0) return;
    int stride = cols + 1;
    for (int r = 0; r < rows; r++) {
        Vtx* out = prepare(tex, blend, smooth, cols * 6);
        for (int c = 0; c < cols; c++) {
            const Vertex& p00 = v[(size_t)r * stride + c];
            const Vertex& p10 = v[(size_t)r * stride + c + 1];
            const Vertex& p01 = v[(size_t)(r + 1) * stride + c];
            const Vertex& p11 = v[(size_t)(r + 1) * stride + c + 1];
            Vtx* q = out + c * 6;
            q[0] = {p00.x, p00.y, p00.u, p00.v, p00.color};
            q[1] = {p01.x, p01.y, p01.u, p01.v, p01.color};
            q[2] = {p10.x, p10.y, p10.u, p10.v, p10.color};
            q[3] = {p10.x, p10.y, p10.u, p10.v, p10.color};
            q[4] = {p01.x, p01.y, p01.u, p01.v, p01.color};
            q[5] = {p11.x, p11.y, p11.u, p11.v, p11.color};
        }
    }
}

void sprite_lut(Texture* tex, int channel, const uint32_t* lut, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, Blend blend) {
    if (!tex || !lut) return;
    if (tex->surface >= 0 && SURFACES[tex->surface].fmt != GPU_RGBA8) return;
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    flush();
    if (tex->surface >= 0) {
        if (g_surface_gpu_dirty[tex->surface]) sync_gpu();
    } else if (!refresh(tex)) {
        return;
    }
    int sx0 = std::max(0, (int)std::floor(std::min(u0, u1))) & ~7;
    int sy0 = std::max(0, (int)std::floor(std::min(v0, v1))) & ~7;
    int sx1 = std::min(tex->tw, ((int)std::ceil(std::max(u0, u1)) + 7) & ~7);
    int sy1 = std::min(tex->th, ((int)std::ceil(std::max(v0, v1)) + 7) & ~7);
    if (sx1 <= sx0 || sy1 <= sy0) return;
    int rw = sx1 - sx0, rh = sy1 - sy0;
    int tw = pow2(rw), th = pow2(rh);
    uint32_t* data = (uint32_t*)alloc((size_t)tw * th * 4);
    if (!data) return;
    uint32_t table[256];
    for (int i = 0; i < 256; i++) table[i] = to_pica(lut[i]);
    int shift = 24 - std::clamp(channel, 0, 3) * 8;
    int src_row0 = tex->th - sy1;
    int dst_row0 = th - rh;
    for (int tr = 0; tr < rh; tr += 8) {
        for (int tc = 0; tc < rw; tc += 8) {
            const uint32_t* s = tex->data + tile_index(sx0 + tc, src_row0 + tr, tex->tw);
            uint32_t* d = data + tile_index(tc, dst_row0 + tr, tw);
            for (int i = 0; i < 64; i++) d[i] = table[(s[i] >> shift) & 255];
        }
    }
    flush_cache(data, (size_t)tw * th * 4);
    Texture* tmp = new Texture();
    tex_setup(tmp, data, tw, th, GPU_RGBA8);
    tmp->w = rw;
    tmp->h = rh;
    tmp->bytes = (size_t)tw * th * 4;
    Vtx* v = prepare(tmp, blend, false, 6);
    put_quad(v, x0, y0, x1, y1, u0 - sx0, v0 - sy0, u1 - sx0, v1 - sy0, 0xFFFFFFFFu);
    flush();
    g_garbage.push_back(tmp);
}

}
