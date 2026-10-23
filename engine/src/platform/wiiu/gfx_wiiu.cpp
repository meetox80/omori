#include "../gfx.h"

#include <coreinit/cache.h>
#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/draw.h>
#include <gx2/event.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <malloc.h>
#include <whb/gfx.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../common/embedded_shaders.h"
#include "../platform.h"

namespace gfx {

constexpr int MAX_VTX = 4096;
constexpr size_t RING_BYTES = 8u << 20;
constexpr int LUT_ROWS = 1024;
constexpr int SURFACES = 6;
constexpr uint32_t MASK_RGBA = GX2_CHANNEL_MASK_RGBA;
constexpr uint32_t MAP_BYTES = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
constexpr uint32_t MAP_WORDS = GX2_COMP_MAP(GX2_SQ_SEL_A, GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R);
constexpr GX2SurfaceFormat COLOR_FORMAT = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;

struct Texture {
    GX2Texture t;
    int w = 0;
    int h = 0;
    size_t report = 0;
    bool words = false;
    uint32_t* cpu = nullptr;
    int cpu_stride = 0;
    int surface = -1;
};

struct Vtx {
    float x, y, u, v;
    uint32_t color;
    float e[4];
};

enum Program { PROG_COLOR, PROG_TEXTURE, PROG_FLAT, PROG_LUT, PROG_GRAY, PROG_PRESENT, PROG_COUNT };
enum StencilState { ST_OFF, ST_WRITE, ST_TEST, ST_CLEAR, ST_UNKNOWN };

static const char* const PIXEL_NAMES[PROG_COUNT] = {"color_f", "texture_f", "flat_f", "lut_f", "gray_f", "present_f"};

struct SurfaceInfo {
    int w = 0;
    int h = 0;
    GX2ColorBuffer cb;
    GX2DepthBuffer db;
    Texture tex;
    bool dirty = false;
    uint32_t version = 0;
    GX2Surface staging;
    uint32_t staging_version = 0;
    bool staging_ok = false;
};

static bool g_ready = false;
static GX2ContextState* g_ctx = nullptr;
static GX2VertexShader* g_vs = nullptr;
static GX2PixelShader* g_ps[PROG_COUNT] = {};
static GX2FetchShader g_fetch;
static uint8_t* g_fetch_prog = nullptr;
static GX2Sampler g_sampler[2][2];
static GX2Sampler g_lut_sampler;
static uint8_t* g_ring = nullptr;
static size_t g_ring_used = 0;
static size_t g_ring_peak = 0;
static Texture g_lut;
static int g_lut_row = 0;
static float g_lut_coord[4] = {255.0f / 256.0f, 0.5f / 256.0f, 0.0f, 0.5f / LUT_ROWS};
static float g_present_size[8] = {(float)SCREEN_W, (float)SCREEN_H, 1.0f / SCREEN_W, 1.0f / SCREEN_H, 1.0f, 0.0f, 0.5f, 1.0f};
static bool g_gpu_busy = false;

static SurfaceInfo g_surface[SURFACES];
static Surface g_target = Surface::Screen;

static Vtx g_batch[MAX_VTX];
static int g_count = 0;
static bool g_quads = true;
static Texture* g_tex = nullptr;
static Blend g_blend = Blend::Normal;
static bool g_smooth = false;
static bool g_flat = false;
static int g_batches = 0;

static int g_applied_stencil = ST_UNKNOWN;
static int g_applied_blend[3] = {-1, -1, -1};
static int g_applied_alpha_test = -1;
static int g_applied_mask = -1;
static const GX2PixelShader* g_applied_ps = nullptr;
static bool g_applied_vs = false;

static bool g_frame_open = false;
static bool g_frame_pending = false;
static uint32_t g_protect = 0;
static bool g_wrap = false;
static Stencil g_stencil = Stencil::Off;
static int g_sx0 = 0, g_sy0 = 0, g_sx1 = SCREEN_W, g_sy1 = SCREEN_H;
static std::vector<Texture*> g_garbage;

void* alloc(size_t bytes) { return memalign(64, bytes ? bytes : 64); }

void release(void* p) { std::free(p); }

size_t arena_free_bytes() { return 0; }

void* arena_try_alloc(size_t) { return nullptr; }

static bool surface_alloc(GX2Surface& s, int w, int h, GX2SurfaceFormat format, GX2SurfaceUse use, GX2TileMode tile) {
    std::memset(&s, 0, sizeof(s));
    s.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    s.width = (uint32_t)w;
    s.height = (uint32_t)h;
    s.depth = 1;
    s.mipLevels = 1;
    s.format = format;
    s.aa = GX2_AA_MODE1X;
    s.use = use;
    s.tileMode = tile;
    GX2CalcSurfaceSizeAndAlignment(&s);
    s.image = memalign(std::max<uint32_t>(s.alignment, 64), s.imageSize);
    if (!s.image) return false;
    DCFlushRange(s.image, s.imageSize);
    return true;
}

static void surface_free(GX2Surface& s) {
    std::free(s.image);
    s.image = nullptr;
}

static void texture_regs(Texture* t, bool words) {
    t->words = words;
    t->t.viewFirstMip = 0;
    t->t.viewNumMips = 1;
    t->t.viewFirstSlice = 0;
    t->t.viewNumSlices = 1;
    t->t.compMap = words ? MAP_WORDS : MAP_BYTES;
    GX2InitTextureRegs(&t->t);
}

static bool texture_alloc(Texture* t, int w, int h, bool words) {
    if (!surface_alloc(t->t.surface, w, h, COLOR_FORMAT, GX2_SURFACE_USE_TEXTURE, GX2_TILE_MODE_LINEAR_ALIGNED)) return false;
    texture_regs(t, words);
    t->w = w;
    t->h = h;
    t->report = t->t.surface.imageSize;
    return true;
}

static uint32_t* row_of(const Texture* t, int y) { return (uint32_t*)t->t.surface.image + (size_t)y * t->t.surface.pitch; }

static void upload(Texture* t) { GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, t->t.surface.image, t->t.surface.imageSize); }

static void collect_garbage() {
    for (Texture* t : g_garbage) {
        if (g_tex == t) g_tex = nullptr;
        surface_free(t->t.surface);
        delete t;
    }
    g_garbage.clear();
}

static void wait_gpu() {
    if (!g_gpu_busy) return;
    GX2DrawDone();
    g_gpu_busy = false;
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
        case Format::RGBA8888: return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
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
    bool bytes = fmt == Format::RGBA8888;
    if (!g_ready || !texture_alloc(t, w, h, !bytes)) {
        plat::log("gfx: out of video memory for %dx%d", w, h);
        release(pixels);
        release(palette);
        delete t;
        return nullptr;
    }
    const uint8_t* src = (const uint8_t*)pixels;
    if (bytes) {
        for (int y = 0; y < h; y++) std::memcpy(row_of(t, y), src + (size_t)y * stride * 4, (size_t)w * 4);
    } else {
        int bpp = bpp_of(fmt);
        for (int y = 0; y < h; y++) {
            uint32_t* row = row_of(t, y);
            for (int x = 0; x < w; x++) row[x] = expand(fmt, src + ((size_t)y * stride + x) * bpp, palette);
        }
    }
    (void)swizzled;
    (void)rows;
    upload(t);
    release(pixels);
    release(palette);
    return t;
}

Texture* texture_wrap(int w, int h, int stride, uint32_t* pixels) {
    Texture* t = new Texture();
    if (!g_ready || !texture_alloc(t, w, h, true)) {
        plat::log("gfx: out of video memory for %dx%d", w, h);
        delete t;
        return nullptr;
    }
    t->cpu = pixels;
    t->cpu_stride = stride;
    texture_touch(t);
    return t;
}

void texture_touch(Texture* t) {
    if (!t || !t->cpu || !t->t.surface.image) return;
    if (t->cpu_stride == (int)t->t.surface.pitch) {
        std::memcpy(t->t.surface.image, t->cpu, (size_t)t->cpu_stride * t->h * 4);
    } else {
        for (int y = 0; y < t->h; y++) std::memcpy(row_of(t, y), t->cpu + (size_t)y * t->cpu_stride, (size_t)t->w * 4);
    }
    upload(t);
}

static void flush();
static void restore_context();

static const uint8_t* read_surface(int i) {
    SurfaceInfo& si = g_surface[i];
    if (!si.staging_ok) {
        if (!surface_alloc(si.staging, si.w, si.h, COLOR_FORMAT, GX2_SURFACE_USE_TEXTURE, GX2_TILE_MODE_LINEAR_ALIGNED)) return nullptr;
        si.staging_ok = true;
        si.staging_version = si.version - 1;
    }
    if (si.staging_version != si.version) {
        GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER, si.tex.t.surface.image, si.tex.t.surface.imageSize);
        GX2CopySurface(&si.tex.t.surface, 0, 0, &si.staging, 0, 0);
        GX2DrawDone();
        g_gpu_busy = false;
        DCInvalidateRange(si.staging.image, si.staging.imageSize);
        restore_context();
        si.staging_version = si.version;
    }
    return (const uint8_t*)si.staging.image;
}

static uint32_t staged_texel(int i, int x, int y) {
    const uint8_t* p = read_surface(i);
    if (!p) return 0;
    p += ((size_t)y * g_surface[i].staging.pitch + x) * 4;
    return rgba(p[0], p[1], p[2], p[3]);
}

bool texture_read(const Texture* t, int x, int y, uint32_t* out) {
    if (!t || x < 0 || y < 0 || x >= t->w || y >= t->h) return false;
    if (t->cpu) {
        *out = t->cpu[(size_t)y * t->cpu_stride + x];
        return true;
    }
    if (t->surface >= 0) {
        flush();
        *out = staged_texel(t->surface, x, y);
        return true;
    }
    uint32_t v = row_of(t, y)[x];
    if (t->words) {
        *out = v;
    } else {
        const uint8_t* p = (const uint8_t*)&v;
        *out = rgba(p[0], p[1], p[2], p[3]);
    }
    return true;
}

void texture_destroy(Texture* t) {
    if (!t) return;
    g_garbage.push_back(t);
}

size_t texture_bytes(const Texture* t) { return t ? t->report : 0; }
int texture_width(const Texture* t) { return t ? t->w : 0; }
int texture_height(const Texture* t) { return t ? t->h : 0; }

static bool setup_programs() {
    const EmbeddedShader* vs = embedded_shader("sprite_v");
    if (!vs || !(g_vs = WHBGfxLoadGFDVertexShader(0, vs->data))) return false;
    for (int p = 0; p < PROG_COUNT; p++) {
        const EmbeddedShader* ps = embedded_shader(PIXEL_NAMES[p]);
        if (!ps || !(g_ps[p] = WHBGfxLoadGFDPixelShader(0, ps->data))) return false;
    }
    const uint32_t vec2 = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_0, GX2_SQ_SEL_1);
    const uint32_t vec4 = GX2_SEL_MASK(GX2_SQ_SEL_X, GX2_SQ_SEL_Y, GX2_SQ_SEL_Z, GX2_SQ_SEL_W);
    GX2AttribStream attribs[4] = {
        {0, 0, offsetof(Vtx, x), GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0, vec2, GX2_ENDIAN_SWAP_DEFAULT},
        {1, 0, offsetof(Vtx, u), GX2_ATTRIB_FORMAT_FLOAT_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0, vec2, GX2_ENDIAN_SWAP_DEFAULT},
        {2, 0, offsetof(Vtx, color), GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, GX2_ATTRIB_INDEX_PER_VERTEX, 0, vec4, GX2_ENDIAN_SWAP_8_IN_32},
        {3, 0, offsetof(Vtx, e), GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0, vec4, GX2_ENDIAN_SWAP_DEFAULT},
    };
    uint32_t size = GX2CalcFetchShaderSizeEx(4, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    g_fetch_prog = (uint8_t*)memalign(GX2_SHADER_PROGRAM_ALIGNMENT, size);
    if (!g_fetch_prog) return false;
    GX2InitFetchShaderEx(&g_fetch, g_fetch_prog, 4, attribs, GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, g_fetch_prog, size);
    return true;
}

static bool setup_surface(Surface s, int w, int h) {
    SurfaceInfo& si = g_surface[(int)s];
    si.w = w;
    si.h = h;
    Texture& t = si.tex;
    if (!surface_alloc(t.t.surface, w, h, COLOR_FORMAT, (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER), GX2_TILE_MODE_DEFAULT))
        return false;
    texture_regs(&t, false);
    t.w = w;
    t.h = h;
    t.surface = (int)s;
    t.report = t.t.surface.imageSize;
    std::memset(&si.cb, 0, sizeof(si.cb));
    si.cb.surface = t.t.surface;
    si.cb.viewNumSlices = 1;
    GX2InitColorBufferRegs(&si.cb);
    std::memset(&si.db, 0, sizeof(si.db));
    if (!surface_alloc(si.db.surface, w, h, GX2_SURFACE_FORMAT_UNORM_R24_X8, GX2_SURFACE_USE_DEPTH_BUFFER, GX2_TILE_MODE_DEFAULT)) return false;
    si.db.viewNumSlices = 1;
    si.db.depthClear = 1.0f;
    si.db.stencilClear = 0;
    GX2InitDepthBufferRegs(&si.db);
    GX2ClearColor(&si.cb, 0.0f, 0.0f, 0.0f, 1.0f);
    GX2ClearDepthStencilEx(&si.db, 1.0f, 0, GX2_CLEAR_FLAGS_BOTH);
    si.dirty = true;
    si.version = 1;
    return true;
}

static void forget_state() {
    g_applied_stencil = ST_UNKNOWN;
    g_applied_blend[0] = g_applied_blend[1] = g_applied_blend[2] = -1;
    g_applied_alpha_test = -1;
    g_applied_mask = -1;
    g_applied_ps = nullptr;
    g_applied_vs = false;
}

static void scissor_now() {
    if (g_sx1 > g_sx0 && g_sy1 > g_sy0) GX2SetScissor((uint32_t)g_sx0, (uint32_t)g_sy0, (uint32_t)(g_sx1 - g_sx0), (uint32_t)(g_sy1 - g_sy0));
}

static void bind_surface(int i) {
    SurfaceInfo& si = g_surface[i];
    GX2SetColorBuffer(&si.cb, GX2_RENDER_TARGET_0);
    GX2SetDepthBuffer(&si.db);
    GX2SetViewport(0.0f, 0.0f, (float)si.w, (float)si.h, 0.0f, 1.0f);
}

static void base_state() {
    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 1, FALSE, TRUE);
    forget_state();
}

static void restore_context() {
    GX2SetContextState(g_ctx);
    base_state();
    bind_surface((int)g_target);
    scissor_now();
}

void init() {
    if (!WHBGfxInit()) {
        plat::log("gfx: WHBGfxInit failed");
        return;
    }
    g_ctx = (GX2ContextState*)memalign(GX2_CONTEXT_STATE_ALIGNMENT, sizeof(GX2ContextState));
    if (!g_ctx) {
        plat::log("gfx: out of memory for the context state");
        return;
    }
    GX2SetupContextStateEx(g_ctx, TRUE);
    GX2SetContextState(g_ctx);
    if (!setup_programs()) {
        plat::log("gfx: shader setup failed");
        return;
    }
    for (int s = 0; s < 2; s++) {
        for (int r = 0; r < 2; r++) {
            GX2InitSampler(&g_sampler[s][r], r ? GX2_TEX_CLAMP_MODE_WRAP : GX2_TEX_CLAMP_MODE_CLAMP, s ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT);
        }
    }
    GX2InitSampler(&g_lut_sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
    g_ring = (uint8_t*)memalign(GX2_VERTEX_BUFFER_ALIGNMENT, RING_BYTES);
    if (!g_ring || !texture_alloc(&g_lut, 256, LUT_ROWS, true)) {
        plat::log("gfx: out of memory for the vertex ring and palette atlas");
        return;
    }
    bool ok = setup_surface(Surface::Screen, SCREEN_W, SCREEN_H) && setup_surface(Surface::Grab, SCREEN_W, SCREEN_H) &&
              setup_surface(Surface::Half0, HALF_W, HALF_H) && setup_surface(Surface::Half1, HALF_W, HALF_H) &&
              setup_surface(Surface::Half2, HALF_W, HALF_H) && setup_surface(Surface::Wide, SCREEN_W, SCREEN_H);
    if (!ok) {
        plat::log("gfx: surface setup failed");
        return;
    }
    GX2DrawDone();
    g_target = Surface::Screen;
    g_sx0 = g_sy0 = 0;
    g_sx1 = SCREEN_W;
    g_sy1 = SCREEN_H;
    restore_context();
    g_ready = true;
}

static void apply_blend(Blend b, bool flat) {
    int op = GX2_BLEND_COMBINE_MODE_ADD, src = GX2_BLEND_MODE_ONE, dst = GX2_BLEND_MODE_ZERO;
    if (flat) {
        src = GX2_BLEND_MODE_SRC_ALPHA;
        if (b == Blend::Add) dst = GX2_BLEND_MODE_ONE;
        else if (b == Blend::Subtract) {
            op = GX2_BLEND_COMBINE_MODE_REV_SUB;
            dst = GX2_BLEND_MODE_ONE;
        } else {
            dst = GX2_BLEND_MODE_INV_SRC_ALPHA;
        }
    } else {
        switch (b) {
            case Blend::Normal: dst = GX2_BLEND_MODE_INV_SRC_ALPHA; break;
            case Blend::Add: dst = GX2_BLEND_MODE_ONE; break;
            case Blend::Multiply:
                src = GX2_BLEND_MODE_DST_COLOR;
                dst = GX2_BLEND_MODE_INV_SRC_ALPHA;
                break;
            case Blend::Screen: dst = GX2_BLEND_MODE_INV_SRC_COLOR; break;
            case Blend::Subtract:
                op = GX2_BLEND_COMBINE_MODE_REV_SUB;
                dst = GX2_BLEND_MODE_ONE;
                break;
            case Blend::Copy: break;
            case Blend::MulSrc:
                src = GX2_BLEND_MODE_ZERO;
                dst = GX2_BLEND_MODE_SRC_COLOR;
                break;
            case Blend::MulAdd:
                src = GX2_BLEND_MODE_DST_COLOR;
                dst = GX2_BLEND_MODE_ONE;
                break;
            case Blend::Max:
                op = GX2_BLEND_COMBINE_MODE_MAX;
                dst = GX2_BLEND_MODE_ONE;
                break;
            case Blend::Min:
                op = GX2_BLEND_COMBINE_MODE_MIN;
                dst = GX2_BLEND_MODE_ONE;
                break;
            case Blend::MulSub:
                op = GX2_BLEND_COMBINE_MODE_REV_SUB;
                src = GX2_BLEND_MODE_DST_COLOR;
                dst = GX2_BLEND_MODE_ONE;
                break;
        }
    }
    if (g_applied_blend[0] == src && g_applied_blend[1] == op && g_applied_blend[2] == dst) return;
    g_applied_blend[0] = src;
    g_applied_blend[1] = op;
    g_applied_blend[2] = dst;
    GX2SetBlendControl(GX2_RENDER_TARGET_0, (GX2BlendMode)src, (GX2BlendMode)dst, (GX2BlendCombineMode)op, TRUE, (GX2BlendMode)src, (GX2BlendMode)dst,
                       (GX2BlendCombineMode)op);
}

static void stencil_control(bool on, GX2CompareFunction func, GX2StencilFunction pass, GX2StencilFunction fail, uint8_t ref, uint8_t write) {
    GX2SetDepthStencilControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS, on, FALSE, func, pass, pass, fail, func, pass, pass, fail);
    GX2SetStencilMask(0xFF, write, ref, 0xFF, write, ref);
}

static void apply_stencil(int mode) {
    if (mode == g_applied_stencil) return;
    g_applied_stencil = mode;
    switch (mode) {
        case ST_OFF: stencil_control(false, GX2_COMPARE_FUNC_ALWAYS, GX2_STENCIL_FUNCTION_KEEP, GX2_STENCIL_FUNCTION_KEEP, 0, 0); break;
        case ST_WRITE: stencil_control(true, GX2_COMPARE_FUNC_ALWAYS, GX2_STENCIL_FUNCTION_REPLACE, GX2_STENCIL_FUNCTION_KEEP, 0xFF, 0xFF); break;
        case ST_TEST: stencil_control(true, GX2_COMPARE_FUNC_EQUAL, GX2_STENCIL_FUNCTION_KEEP, GX2_STENCIL_FUNCTION_KEEP, 0xFF, 0x00); break;
        case ST_CLEAR: stencil_control(true, GX2_COMPARE_FUNC_ALWAYS, GX2_STENCIL_FUNCTION_REPLACE, GX2_STENCIL_FUNCTION_REPLACE, 0x00, 0xFF); break;
    }
}

static void apply_alpha_test(bool on) {
    if ((int)on == g_applied_alpha_test) return;
    g_applied_alpha_test = on;
    GX2SetAlphaTest(on, GX2_COMPARE_FUNC_GEQUAL, 0.5f / 255.0f);
}

static void apply_mask(uint32_t mask) {
    if ((int)mask == g_applied_mask) return;
    g_applied_mask = (int)mask;
    GX2SetTargetChannelMasks((GX2ChannelMask)mask, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0,
                             (GX2ChannelMask)0, (GX2ChannelMask)0);
}

static void prepare_sample(Texture* t) {
    if (!t || t->surface < 0) return;
    SurfaceInfo& si = g_surface[t->surface];
    if (!si.dirty) return;
    GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE), si.tex.t.surface.image, si.tex.t.surface.imageSize);
    si.dirty = false;
}

static void emit(Program prog, bool alpha_test, Texture* tex, bool smooth, bool repeat, bool lut, Blend blend, uint32_t mask, int stencil, const Vtx* v,
                 int n, bool quads, int w, int h) {
    if (n <= 0) return;
    prepare_sample(tex);
    size_t bytes = (size_t)n * sizeof(Vtx);
    size_t at = (g_ring_used + 63) & ~(size_t)63;
    if (at + bytes > RING_BYTES) {
        wait_gpu();
        at = 0;
    }
    g_ring_used = at + bytes;
    g_ring_peak = std::max(g_ring_peak, g_ring_used);
    uint8_t* dst = g_ring + at;
    std::memcpy(dst, v, bytes);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, dst, bytes);
    if (!g_applied_vs) {
        GX2SetFetchShader(&g_fetch);
        GX2SetVertexShader(g_vs);
        g_applied_vs = true;
    }
    if (g_applied_ps != g_ps[prog]) {
        GX2SetPixelShader(g_ps[prog]);
        g_applied_ps = g_ps[prog];
    }
    const float xf[4] = {2.0f / w, -2.0f / h, -1.0f, 1.0f};
    GX2SetVertexUniformReg(0, 4, xf);
    if (prog == PROG_LUT) GX2SetPixelUniformReg(0, 4, g_lut_coord);
    if (prog == PROG_PRESENT) GX2SetPixelUniformReg(0, 8, g_present_size);
    apply_blend(blend, prog == PROG_FLAT);
    apply_stencil(stencil);
    apply_alpha_test(alpha_test);
    apply_mask(mask);
    if (tex) {
        GX2SetPixelTexture(&tex->t, 0);
        GX2SetPixelSampler(&g_sampler[smooth ? 1 : 0][repeat ? 1 : 0], 0);
    }
    if (lut) {
        GX2SetPixelTexture(&g_lut.t, 1);
        GX2SetPixelSampler(&g_lut_sampler, 1);
    }
    GX2SetAttribBuffer(0, bytes, sizeof(Vtx), dst);
    GX2DrawEx(quads ? GX2_PRIMITIVE_MODE_QUADS : GX2_PRIMITIVE_MODE_TRIANGLES, (uint32_t)n, 0, 1);
    g_batches++;
    g_gpu_busy = true;
}

static inline Vtx vtx(float x, float y, float u, float v, uint32_t c) {
    Vtx o;
    o.x = x;
    o.y = y;
    o.u = u;
    o.v = v;
    o.color = c;
    o.e[0] = o.e[1] = o.e[2] = o.e[3] = 0.0f;
    return o;
}

static void full_quad(Vtx q[4], float w, float h, uint32_t color) {
    q[0] = vtx(0, 0, 0, 0, color);
    q[1] = vtx(w, 0, 1, 0, color);
    q[2] = vtx(w, h, 1, 1, color);
    q[3] = vtx(0, h, 0, 1, color);
}

static int stencil_state() {
    switch (g_stencil) {
        case Stencil::Write: return ST_WRITE;
        case Stencil::Test: return ST_TEST;
        default: return ST_OFF;
    }
}

static uint32_t write_mask() {
    uint32_t m = 0;
    if (!(g_protect & 0x0000FFu)) m |= GX2_CHANNEL_MASK_R;
    if (!(g_protect & 0x00FF00u)) m |= GX2_CHANNEL_MASK_G;
    if (!(g_protect & 0xFF0000u)) m |= GX2_CHANNEL_MASK_B;
    return m;
}

static void mark_drawn(int i) {
    g_surface[i].dirty = true;
    g_surface[i].version++;
}

static void draw_now(Program prog, Texture* tex, bool lut, Blend blend, bool smooth, bool repeat, uint32_t mask, int stencil, const Vtx* v, int n, bool quads) {
    if (n <= 0) return;
    const SurfaceInfo& si = g_surface[(int)g_target];
    emit(prog, stencil == ST_WRITE, tex, smooth, repeat, lut, blend, mask, stencil, v, n, quads, si.w, si.h);
    mark_drawn((int)g_target);
}

static void target_size(int& w, int& h) {
    w = g_surface[(int)g_target].w;
    h = g_surface[(int)g_target].h;
}

static bool scissor_empty() { return g_sx1 <= g_sx0 || g_sy1 <= g_sy0; }

static void flush() {
    if (!g_count) return;
    int n = g_count;
    g_count = 0;
    if (!g_frame_open || scissor_empty()) return;
    Program p = g_tex ? (g_flat ? PROG_FLAT : PROG_TEXTURE) : PROG_COLOR;
    draw_now(p, g_tex, false, g_blend, g_smooth, g_wrap, write_mask(), stencil_state(), g_batch, n, g_quads);
}

static void prepare(Texture* tex, Blend blend, bool smooth, bool quads, int verts, bool flat) {
    if (g_count && (g_tex != tex || g_blend != blend || g_smooth != smooth || g_quads != quads || g_flat != flat || g_count + verts > MAX_VTX)) flush();
    g_tex = tex;
    g_blend = blend;
    g_smooth = smooth;
    g_quads = quads;
    g_flat = flat;
}

static void present_to(bool tv) {
    GX2ColorBuffer* cb = tv ? WHBGfxGetTVColourBuffer() : WHBGfxGetDRCColourBuffer();
    if (tv) WHBGfxBeginRenderTV();
    else WHBGfxBeginRenderDRC();
    WHBGfxClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    base_state();
    int dw = (int)cb->surface.width, dh = (int)cb->surface.height;
    float h = (float)dh;
    float w = h * (4.0f / 3.0f);
    if (w > dw) {
        w = (float)dw;
        h = w * 0.75f;
    }
    float x0 = std::floor((dw - w) * 0.5f);
    float y0 = std::floor((dh - h) * 0.5f);
    float scale = std::max(1.0f, h / SCREEN_H);
    g_present_size[4] = scale;
    g_present_size[5] = 0.5f - 0.5f / scale;
    GX2SetViewport(0.0f, 0.0f, (float)dw, (float)dh, 0.0f, 1.0f);
    GX2SetScissor(0, 0, (uint32_t)dw, (uint32_t)dh);
    Vtx q[4] = {vtx(x0, y0, 0, 0, 0xFFFFFFFFu), vtx(x0 + w, y0, 1, 0, 0xFFFFFFFFu), vtx(x0 + w, y0 + h, 1, 1, 0xFFFFFFFFu), vtx(x0, y0 + h, 0, 1, 0xFFFFFFFFu)};
    emit(PROG_PRESENT, false, &g_surface[(int)Surface::Screen].tex, true, false, false, Blend::Copy, MASK_RGBA, ST_OFF, q, 4, true, dw, dh);
    if (tv) WHBGfxFinishRenderTV();
    else WHBGfxFinishRenderDRC();
}

static void present_frame() {
    WHBGfxBeginRender();
    present_to(true);
    present_to(false);
    WHBGfxFinishRender();
    g_gpu_busy = false;
    restore_context();
}

void present() {
    if (!g_frame_pending || !g_ready) return;
    present_frame();
    g_frame_pending = false;
}

void shutdown() {
    if (!g_ready) return;
    present();
    wait_gpu();
    g_ready = false;
}

void reset_scissor();

void begin_frame(uint32_t clear_color) {
    if (!g_ready) return;
    present();
    wait_gpu();
    collect_garbage();
    g_ring_used = 0;
    g_lut_row = 0;
    g_count = 0;
    g_tex = nullptr;
    g_target = Surface::Screen;
    g_protect = 0;
    g_wrap = false;
    g_stencil = Stencil::Off;
    g_frame_open = true;
    g_sx0 = g_sy0 = 0;
    g_sx1 = SCREEN_W;
    g_sy1 = SCREEN_H;
    restore_context();
    Vtx q[4];
    full_quad(q, (float)SCREEN_W, (float)SCREEN_H, 0xFF000000u | (clear_color & 0xFFFFFFu));
    draw_now(PROG_COLOR, nullptr, false, Blend::Copy, false, false, MASK_RGBA, ST_CLEAR, q, 4, true);
}

void end_frame() {
    if (!g_frame_open) return;
    flush();
    if (g_target != Surface::Screen) set_target(Surface::Screen);
    if (g_stencil != Stencil::Off) set_stencil(Stencil::Off);
    if (g_protect) set_color_mask(0);
    GX2Flush();
    g_frame_open = false;
    g_frame_pending = true;
}

int list_peak_bytes() { return (int)g_ring_peak; }

bool reclaim_memory() {
    if (g_frame_open || !g_ready) return false;
    wait_gpu();
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

bool capture_row(int y, int x, int w, uint32_t* out) {
    if (!g_ready || y < 0 || y >= SCREEN_H || x < 0 || x + w > SCREEN_W) return false;
    flush();
    const uint8_t* px = read_surface((int)Surface::Screen);
    if (!px) return false;
    const uint8_t* p = px + ((size_t)y * g_surface[(int)Surface::Screen].staging.pitch + x) * 4;
    for (int i = 0; i < w; i++, p += 4) out[i] = rgba(p[0], p[1], p[2], p[3]);
    return true;
}

bool capture(uint32_t* out, int out_stride) {
    for (int y = 0; y < SCREEN_H; y++) {
        if (!capture_row(y, 0, SCREEN_W, out + (size_t)y * out_stride)) return false;
    }
    return true;
}

void set_scissor(int x, int y, int w, int h) {
    flush();
    int tw, th;
    target_size(tw, th);
    g_sx0 = std::max(0, x);
    g_sy0 = std::max(0, y);
    g_sx1 = std::min(tw, x + w);
    g_sy1 = std::min(th, y + h);
    if (g_ready) scissor_now();
}

void reset_scissor() {
    flush();
    int tw, th;
    target_size(tw, th);
    g_sx0 = 0;
    g_sy0 = 0;
    g_sx1 = tw;
    g_sy1 = th;
    if (g_ready) scissor_now();
}

static inline float nu(const Texture* t, float u) { return t ? u / (float)t->w : 0.0f; }
static inline float nv(const Texture* t, float v) { return t ? v / (float)t->h : 0.0f; }

static void push_rect(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color) {
    Vtx* v = &g_batch[g_count];
    v[0] = vtx(x0, y0, u0, v0, color);
    v[1] = vtx(x1, y0, u1, v0, color);
    v[2] = vtx(x1, y1, u1, v1, color);
    v[3] = vtx(x0, y1, u0, v1, color);
    g_count += 4;
}

void sprite(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    prepare(tex, blend, smooth, true, 4, false);
    push_rect(x0, y0, x1, y1, nu(tex, u0), nv(tex, v0), nu(tex, u1), nv(tex, v1), color);
}

static void quad_common(Texture* tex, const float uv[8], const float xy[8], uint32_t color, Blend blend, bool smooth, bool flat) {
    prepare(tex, blend, smooth, false, 6, flat);
    Vtx c[4];
    for (int k = 0; k < 4; k++) c[k] = vtx(xy[k * 2], xy[k * 2 + 1], nu(tex, uv[k * 2]), nv(tex, uv[k * 2 + 1]), color);
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    for (int i : order) g_batch[g_count++] = c[i];
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

static void copy_screen_to_grab() {
    int grab = (int)Surface::Grab;
    bind_surface(grab);
    GX2SetScissor(0, 0, SCREEN_W, SCREEN_H);
    Vtx q[4];
    full_quad(q, (float)SCREEN_W, (float)SCREEN_H, 0xFFFFFFFFu);
    emit(PROG_TEXTURE, false, &g_surface[(int)Surface::Screen].tex, false, false, false, Blend::Copy, MASK_RGBA, ST_OFF, q, 4, true, SCREEN_W, SCREEN_H);
    mark_drawn(grab);
    bind_surface((int)g_target);
    scissor_now();
}

void desaturate(float amount) {
    if (amount <= 0 || !g_frame_open || scissor_empty()) return;
    if (amount > 1) amount = 1;
    flush();
    copy_screen_to_grab();
    float level = (float)(int)(amount * 255.0f + 0.5f) / 255.0f;
    Vtx q[4];
    full_quad(q, (float)SCREEN_W, (float)SCREEN_H, 0xFFFFFFFFu);
    for (Vtx& v : q) v.e[0] = level;
    draw_now(PROG_GRAY, &g_surface[(int)Surface::Grab].tex, false, Blend::Copy, false, false, write_mask(), stencil_state(), q, 4, true);
}

Texture* grab_screen() {
    flush();
    if (g_frame_open) copy_screen_to_grab();
    return &g_surface[(int)Surface::Grab].tex;
}

void set_target(Surface s) {
    flush();
    g_target = s;
    g_sx0 = 0;
    g_sy0 = 0;
    g_sx1 = g_surface[(int)s].w;
    g_sy1 = g_surface[(int)s].h;
    if (!g_frame_open) return;
    bind_surface((int)s);
    scissor_now();
}

Surface current_target() { return g_target; }

Texture* surface_texture(Surface s) { return &g_surface[(int)s].tex; }
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
}

void stencil_clear(int x0, int y0, int x1, int y1) {
    if (x1 <= x0 || y1 <= y0 || !g_frame_open || scissor_empty()) return;
    flush();
    Vtx q[4] = {vtx((float)x0, (float)y0, 0, 0, 0xFFFFFFFFu), vtx((float)x1, (float)y0, 0, 0, 0xFFFFFFFFu), vtx((float)x1, (float)y1, 0, 0, 0xFFFFFFFFu),
                vtx((float)x0, (float)y1, 0, 0, 0xFFFFFFFFu)};
    draw_now(PROG_COLOR, nullptr, false, Blend::Copy, false, false, 0, ST_CLEAR, q, 4, true);
}

void draw_triangles(Texture* tex, const Vertex* v, int count, Blend blend, bool smooth) {
    count -= count % 3;
    for (int i = 0; i < count; i += 3) {
        prepare(tex, blend, smooth, false, 3, false);
        for (int k = 0; k < 3; k++) g_batch[g_count++] = vtx(v[i + k].x, v[i + k].y, nu(tex, v[i + k].u), nv(tex, v[i + k].v), v[i + k].color);
    }
}

void draw_grid(Texture* tex, const Vertex* v, int cols, int rows, Blend blend, bool smooth) {
    if (cols <= 0 || rows <= 0) return;
    int stride = cols + 1;
    auto at = [&](int c, int r) {
        const Vertex& s = v[(size_t)r * stride + c];
        return vtx(s.x, s.y, nu(tex, s.u), nv(tex, s.v), s.color);
    };
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            prepare(tex, blend, smooth, false, 6, false);
            Vtx p00 = at(c, r), p10 = at(c + 1, r), p01 = at(c, r + 1), p11 = at(c + 1, r + 1);
            g_batch[g_count++] = p00;
            g_batch[g_count++] = p01;
            g_batch[g_count++] = p10;
            g_batch[g_count++] = p10;
            g_batch[g_count++] = p01;
            g_batch[g_count++] = p11;
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
    if (scissor_empty()) return;
    if (g_lut_row >= LUT_ROWS) {
        wait_gpu();
        g_lut_row = 0;
    }
    int row = g_lut_row++;
    uint32_t* d = row_of(&g_lut, row);
    std::memcpy(d, lut, 256 * 4);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, d, 256 * 4);
    g_lut_coord[3] = ((float)row + 0.5f) / LUT_ROWS;
    Vtx q[4] = {vtx(x0, y0, nu(tex, u0), nv(tex, v0), 0xFFFFFFFFu), vtx(x1, y0, nu(tex, u1), nv(tex, v0), 0xFFFFFFFFu),
                vtx(x1, y1, nu(tex, u1), nv(tex, v1), 0xFFFFFFFFu), vtx(x0, y1, nu(tex, u0), nv(tex, v1), 0xFFFFFFFFu)};
    int ch = std::clamp(channel, 0, 3);
    for (Vtx& v : q) v.e[ch] = 1.0f;
    draw_now(PROG_LUT, tex, true, blend, false, g_wrap, write_mask(), stencil_state(), q, 4, true);
}

}
