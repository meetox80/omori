#include "../gfx.h"
#include "../common/border_tiles.h"

#include <malloc.h>
#include <xenos/edram.h>
#include <xenos/xe.h>
#include <xenos/xenos.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../common/embedded_shaders.h"
#include "../platform.h"

extern "C" {
void Xe_pSetState(struct XenosDevice* xe);
void Xe_pStuff(struct XenosDevice* xe);
void Xe_pSetIndexOffset(struct XenosDevice* xe, int offset);
void Xe_pDrawNonIndexed(struct XenosDevice* xe, int num_points, int primtype);
void Xe_pSyncToDevice(struct XenosDevice* xe, volatile void* data, int len);
void Xe_pSyncFromDevice(struct XenosDevice* xe, volatile void* data, int len);
void Xe_pInvalidateGpuCache(struct XenosDevice* xe, int base, int size);
}

namespace gfx {

constexpr int MAX_VTX = 4096;
constexpr size_t RING_BYTES = 8u << 20;
constexpr size_t RING_SLACK = 8192;
constexpr int LUT_ROWS = 1024;
constexpr int TEX_FORMAT = XE_FMT_8888 | XE_FMT_ARGB;
constexpr int FB_FORMAT = XE_FMT_8888 | XE_FMT_BGRA;
constexpr int EDRAM_TILES = 2048;
constexpr int TILE_W = 80;
constexpr int TILE_H = 16;
constexpr int KICK_DWORDS = 0x8000;
constexpr int SYNC_WPTR = 0x70000;
constexpr uint32_t MASK_RGBA = 15;
constexpr int SURFACES = 6;
constexpr uint64_t FLIP_GUARD_US = 4000;
constexpr u32 REG_COHER_DEST_BASE_1 = 0x2007;
constexpr u32 REG_SCREEN_SCISSOR_TL = 0x200e;
constexpr u32 REG_COLOR_MASK = 0x2104;

struct Texture {
    XenosSurface s;
    int w = 0;
    int h = 0;
    size_t report = 0;
    uint32_t* cpu = nullptr;
    int cpu_stride = 0;
    int surface = -1;
    uint32_t read = 0;
};

struct Vtx {
    float x, y, u, v;
    uint32_t color;
    float e[4];
};

enum Program { PROG_COLOR, PROG_TEXTURE, PROG_FLAT, PROG_LUT, PROG_GRAY, PROG_PRESENT, PROG_COUNT };
enum StencilState { ST_OFF, ST_WRITE, ST_TEST, ST_CLEAR, ST_UNKNOWN };

static const char* const PIXEL_NAMES[PROG_COUNT] = {"color_f", "texture_f", "flat_f", "lut_f", "gray_f", "present_f"};
static const char* const ALPHA_NAMES[PROG_COUNT] = {"color_at_f", "texture_at_f", "flat_at_f", "lut_at_f", nullptr, nullptr};

static const XenosVBFFormat VBF = {4,
                                   {{XE_USAGE_POSITION, 0, XE_TYPE_FLOAT2},
                                    {XE_USAGE_TEXCOORD, 0, XE_TYPE_FLOAT2},
                                    {XE_USAGE_COLOR, 0, XE_TYPE_UBYTE4},
                                    {XE_USAGE_TEXCOORD, 1, XE_TYPE_FLOAT4}}};

struct SurfaceInfo {
    int w = 0;
    int h = 0;
    int pitch = 0;
    int color_base = 0;
    int depth_base = 0;
    bool dirty = false;
    bool stale = true;
    Texture tex;
};

static XenosDevice g_device;
static XenosDevice* g_xe = nullptr;
static bool g_ready = false;
static XenosShader* g_vs = nullptr;
static XenosShader* g_ps[PROG_COUNT] = {};
static XenosShader* g_ps_at[PROG_COUNT] = {};
static XenosVertexBuffer* g_vb = nullptr;
static size_t g_ring_used = 0;
static size_t g_ring_peak = 0;
static XenosSurface g_lut;
static int g_lut_row = 0;
static float g_ps_consts[5][4];

static SurfaceInfo g_surface[SURFACES];
static Surface g_target = Surface::Screen;
static int g_edram_next = 0;
static XenosSurface g_fb[2];
static int g_back = 0;
static int g_display_w = 0, g_display_h = 0;
static float g_display_aspect = 4.0f / 3.0f;
static bool g_overscan = false;
static int g_band_h = 0, g_band_pitch = 0, g_band_depth = 0;
static uint64_t g_last_flip = 0;

static uint32_t g_serial = 0;
static uint32_t g_synced_serial = 0;
static uint32_t g_sync_id = 1;

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

static inline uint32_t to_argb(uint32_t c) { return ((c >> 24) << 24) | ((c & 255) << 16) | (((c >> 8) & 255) << 8) | ((c >> 16) & 255); }

static inline uint32_t from_argb(uint32_t v) { return rgba((int)((v >> 16) & 255), (int)((v >> 8) & 255), (int)(v & 255), (int)(v >> 24)); }

static inline size_t tiled_texel(int x, int y, int pitch) {
    return (size_t)((((y >> 5) * 32 * pitch + ((x >> 5) << 10) + (x & 3) + ((y & 1) << 2) + (((x & 31) >> 2) << 3) + (((y & 31) >> 1) << 6)) ^
                     ((y & 8) << 2)));
}

static inline void put(u32 v) { ((volatile u32*)g_xe->rb_secondary)[g_xe->rb_secondary_wptr++] = v; }

static void write_reg(u32 reg, u32 v) {
    put(reg);
    put(v);
}

static size_t surface_bytes(const XenosSurface& s) { return (size_t)s.wpitch * (size_t)s.hpitch; }

static bool surface_alloc(XenosSurface& s, int w, int h, int format, bool tiled) {
    std::memset(&s, 0, sizeof(s));
    s.width = w;
    s.height = h;
    s.wpitch = (w * 4 + 127) & ~127;
    s.hpitch = (h + 31) & ~31;
    s.tiled = tiled ? 1 : 0;
    s.format = format;
    s.bypp = 4;
    s.u_addressing = XE_TEXADDR_CLAMP;
    s.v_addressing = XE_TEXADDR_CLAMP;
    s.base = memalign(4096, surface_bytes(s));
    if (!s.base) return false;
    s.ptr = (u32)(uintptr_t)s.base & ~0x80000000u;
    return true;
}

static void surface_free(XenosSurface& s) {
    std::free(s.base);
    s.base = nullptr;
}

static void upload_range(const XenosSurface& s, size_t offset, size_t bytes) {
    Xe_pSyncToDevice(g_xe, (uint8_t*)s.base + offset, (int)bytes);
    size_t start = (s.ptr + offset) & ~(size_t)4095;
    size_t end = (s.ptr + offset + bytes + 4095) & ~(size_t)4095;
    Xe_pInvalidateGpuCache(g_xe, (int)start, (int)(end - start));
    g_serial++;
}

static void upload(const XenosSurface& s) { upload_range(s, 0, surface_bytes(s)); }

static void sync_gpu() {
    Xe_Sync(g_xe);
    g_ring_used = 0;
    g_synced_serial = g_serial;
    g_sync_id++;
}

static void submit(bool force) {
    if (!force && g_xe->rb_secondary_wptr - g_xe->last_wptr < KICK_DWORDS) return;
    if (g_xe->rb_secondary_wptr > SYNC_WPTR) {
        sync_gpu();
        return;
    }
    Xe_Execute(g_xe);
}

static void collect_garbage() {
    for (Texture* t : g_garbage) {
        if (g_tex == t) g_tex = nullptr;
        surface_free(t->s);
        delete t;
    }
    g_garbage.clear();
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

static uint32_t* row_of(const XenosSurface& s, int y) { return (uint32_t*)((uint8_t*)s.base + (size_t)y * s.wpitch); }

Texture* texture_create(int w, int h, int stride, int rows, Format fmt, bool swizzled, void* pixels, uint32_t* palette) {
    Texture* t = new Texture();
    if (!g_ready || !surface_alloc(t->s, w, h, TEX_FORMAT, false)) {
        plat::log("gfx: out of video memory for %dx%d", w, h);
        release(pixels);
        release(palette);
        delete t;
        return nullptr;
    }
    t->w = w;
    t->h = h;
    const uint8_t* src = (const uint8_t*)pixels;
    if (swizzled && fmt == Format::RGBA8888) {
        for (int y = 0; y < h; y++) std::memcpy(row_of(t->s, y), src + (size_t)y * stride * 4, (size_t)w * 4);
    } else {
        int bpp = bpp_of(fmt);
        for (int y = 0; y < h; y++) {
            uint32_t* row = row_of(t->s, y);
            for (int x = 0; x < w; x++) row[x] = to_argb(expand(fmt, src + ((size_t)y * stride + x) * bpp, palette));
        }
    }
    (void)rows;
    t->report = surface_bytes(t->s);
    upload_range(t->s, 0, (size_t)h * t->s.wpitch);
    release(pixels);
    release(palette);
    return t;
}

Texture* texture_wrap(int w, int h, int stride, uint32_t* pixels) {
    Texture* t = new Texture();
    if (!g_ready || !surface_alloc(t->s, w, h, TEX_FORMAT, false)) {
        plat::log("gfx: out of video memory for %dx%d", w, h);
        delete t;
        return nullptr;
    }
    t->w = w;
    t->h = h;
    t->report = surface_bytes(t->s);
    t->cpu = pixels;
    t->cpu_stride = stride;
    texture_touch(t);
    return t;
}

void texture_touch(Texture* t) {
    if (!t || !t->cpu || !t->s.base) return;
    for (int y = 0; y < t->h; y++) {
        const uint32_t* s = t->cpu + (size_t)y * t->cpu_stride;
        uint32_t* d = row_of(t->s, y);
        for (int x = 0; x < t->w; x++) d[x] = to_argb(s[x]);
    }
    upload_range(t->s, 0, (size_t)t->h * t->s.wpitch);
}

static void ensure_resolved(Texture* t);

static void prepare_read(Texture* t) {
    ensure_resolved(t);
    if (g_synced_serial != g_serial) sync_gpu();
    if (t->read != g_sync_id) {
        Xe_pSyncFromDevice(g_xe, t->s.base, (int)surface_bytes(t->s));
        t->read = g_sync_id;
    }
}

static void flush();

bool texture_read(const Texture* t, int x, int y, uint32_t* out) {
    if (!t || x < 0 || y < 0 || x >= t->w || y >= t->h) return false;
    if (t->cpu) {
        *out = t->cpu[(size_t)y * t->cpu_stride + x];
        return true;
    }
    if (t->surface >= 0) {
        flush();
        prepare_read(const_cast<Texture*>(t));
        *out = from_argb(((const uint32_t*)t->s.base)[tiled_texel(x, y, t->s.wpitch / 4)]);
        return true;
    }
    *out = from_argb(row_of(t->s, y)[x]);
    return true;
}

void texture_destroy(Texture* t) {
    if (!t) return;
    g_garbage.push_back(t);
}

size_t texture_bytes(const Texture* t) { return t ? t->report : 0; }
int texture_width(const Texture* t) { return t ? t->w : 0; }
int texture_height(const Texture* t) { return t ? t->h : 0; }

static XenosShader* load_shader(const char* name, bool vertex) {
    const EmbeddedShader* e = embedded_shader(name);
    if (!e) return nullptr;
    XenosShader* sh = Xe_LoadShaderFromMemory(g_xe, (void*)e->data);
    if (!sh) return nullptr;
    Xe_InstantiateShader(g_xe, sh, 0);
    if (vertex) Xe_ShaderApplyVFetchPatches(g_xe, sh, 0, &VBF);
    return sh;
}

static bool setup_programs() {
    g_vs = load_shader("sprite_v", true);
    if (!g_vs) return false;
    for (int p = 0; p < PROG_COUNT; p++) {
        g_ps[p] = load_shader(PIXEL_NAMES[p], false);
        if (!g_ps[p]) return false;
        if (ALPHA_NAMES[p]) {
            g_ps_at[p] = load_shader(ALPHA_NAMES[p], false);
            if (!g_ps_at[p]) return false;
        }
    }
    return true;
}

static bool setup_surface(Surface s, int w, int h) {
    SurfaceInfo& si = g_surface[(int)s];
    int tiles_per_line = ((w + TILE_W - 1) / TILE_W + 1) & ~1;
    int tile_rows = (h + TILE_H - 1) / TILE_H;
    si.w = w;
    si.h = h;
    si.pitch = tiles_per_line * TILE_W;
    si.color_base = g_edram_next;
    si.depth_base = g_edram_next + tiles_per_line * tile_rows;
    g_edram_next += 2 * tiles_per_line * tile_rows;
    if (g_edram_next > EDRAM_TILES) return false;
    if (!surface_alloc(si.tex.s, w, h, TEX_FORMAT, true)) return false;
    uint32_t* p = (uint32_t*)si.tex.s.base;
    size_t n = surface_bytes(si.tex.s) / 4;
    for (size_t i = 0; i < n; i++) p[i] = 0xFF000000u;
    upload(si.tex.s);
    si.tex.w = w;
    si.tex.h = h;
    si.tex.surface = (int)s;
    si.tex.report = surface_bytes(si.tex.s);
    si.stale = true;
    si.dirty = false;
    return true;
}

static void use_layout(int w, int h, int pitch, int color_base, int depth_base, XenosSurface* rt) {
    g_xe->rt = rt;
    g_xe->vp_xres = w;
    g_xe->vp_yres = h;
    g_xe->msaa_samples = 0;
    g_xe->edram_colorformat = 0;
    g_xe->edram_pitch = pitch;
    g_xe->edram_hizpitch = pitch;
    g_xe->edram_color0base = color_base;
    g_xe->edram_depthbase = depth_base;
    g_xe->dirty |= DIRTY_MISC;
}

static void use_surface(int i) {
    SurfaceInfo& si = g_surface[i];
    use_layout(si.w, si.h, si.pitch, si.color_base, si.depth_base, &si.tex.s);
}

static void after_resolve() {
    g_xe->alu_dirty = ~0u;
    g_xe->fetch_dirty = ~0u;
    g_xe->dirty = ~0;
    g_applied_stencil = ST_UNKNOWN;
    g_serial++;
}

static void resolve(int source, XenosSurface* into) {
    use_surface(source);
    Xe_ResolveInto(g_xe, into, XE_SOURCE_COLOR, 0);
    after_resolve();
    use_surface((int)g_target);
    Xe_SetScissor(g_xe, 1, g_sx0, g_sy0, g_sx1, g_sy1);
    submit(false);
}

static void ensure_resolved(Texture* t) {
    if (!t || t->surface < 0) return;
    SurfaceInfo& si = g_surface[t->surface];
    if (!si.dirty) return;
    resolve(t->surface, &si.tex.s);
    si.dirty = false;
}

static void choose_display() {
    XenosSurface* def = Xe_GetFramebufferSurface(g_xe);
    g_display_w = def->width;
    g_display_h = def->height;
    g_display_aspect = g_display_h <= 576 ? 4.0f / 3.0f : (float)g_display_w / (float)g_display_h;
    g_overscan = xenos_is_overscan() != 0;
    int tiles_per_line = ((g_display_w + TILE_W - 1) / TILE_W + 1) & ~1;
    int band = (EDRAM_TILES / (2 * tiles_per_line)) * TILE_H;
    band &= ~31;
    g_band_h = std::min(band, g_display_h);
    g_band_pitch = tiles_per_line * TILE_W;
    g_band_depth = tiles_per_line * ((g_band_h + TILE_H - 1) / TILE_H);
}

static void set_ps_const(int index, float x, float y, float z, float w) {
    g_ps_consts[index][0] = x;
    g_ps_consts[index][1] = y;
    g_ps_consts[index][2] = z;
    g_ps_consts[index][3] = w;
    Xe_SetPixelShaderConstantF(g_xe, index, g_ps_consts[index], 1);
}

void init() {
    xenos_init(VIDEO_MODE_AUTO);
    g_xe = &g_device;
    Xe_Init(g_xe);
    Xe_SetRenderTarget(g_xe, Xe_GetFramebufferSurface(g_xe));
    choose_display();
    if (!setup_programs()) {
        plat::log("gfx: shader setup failed");
        return;
    }
    g_vb = Xe_CreateVertexBuffer(g_xe, (int)(RING_BYTES + RING_SLACK));
    if (!surface_alloc(g_lut, 256, LUT_ROWS, TEX_FORMAT, false)) {
        plat::log("gfx: out of memory for the palette atlas");
        return;
    }
    for (int i = 0; i < 2; i++) {
        if (!surface_alloc(g_fb[i], g_display_w, g_display_h, FB_FORMAT, true)) {
            plat::log("gfx: out of memory for the display");
            return;
        }
        std::memset(g_fb[i].base, 0, surface_bytes(g_fb[i]));
        Xe_pSyncToDevice(g_xe, g_fb[i].base, (int)surface_bytes(g_fb[i]));
    }
    bool ok = setup_surface(Surface::Screen, SCREEN_W, SCREEN_H) && setup_surface(Surface::Grab, SCREEN_W, SCREEN_H) &&
              setup_surface(Surface::Half0, HALF_W, HALF_H) && setup_surface(Surface::Half1, HALF_W, HALF_H) &&
              setup_surface(Surface::Half2, HALF_W, HALF_H) && setup_surface(Surface::Wide, SCREEN_W, SCREEN_H);
    if (!ok) {
        plat::log("gfx: surface setup failed");
        return;
    }
    edram_init(g_xe);
    Xe_InvalidateState(g_xe);
    Xe_SetZEnable(g_xe, 0);
    Xe_SetZWrite(g_xe, 0);
    Xe_SetCullMode(g_xe, XE_CULL_NONE);
    Xe_SetStencilEnable(g_xe, 0);
    g_applied_stencil = ST_OFF;
    set_ps_const(0, 1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f, 1.0f);
    set_ps_const(1, 255.0f / 256.0f, 0.5f / 256.0f, 0.0f, 0.5f / LUT_ROWS);
    set_ps_const(2, 0.5f / 255.0f, 0.5f / 255.0f, 0.5f / 255.0f, 0.5f / 255.0f);
    set_ps_const(3, (float)SCREEN_W, (float)SCREEN_H, 1.0f / SCREEN_W, 1.0f / SCREEN_H);
    set_ps_const(4, 1.0f, 0.0f, 0.5f, 1.0f);
    put(0x00010000 | REG_SCREEN_SCISSOR_TL);
    put(0);
    put(8192u | (8192u << 16));
    use_surface((int)Surface::Screen);
    Xe_SetScissor(g_xe, 1, 0, 0, SCREEN_W, SCREEN_H);
    sync_gpu();
    while (!Xe_IsVBlank(g_xe)) {
    }
    Xe_SetFrameBufferSurface(g_xe, &g_fb[0]);
    g_last_flip = plat::time_us();
    g_back = 1;
    g_ready = true;
}

static void apply_blend(Blend b, bool flat) {
    int op = XE_BLENDOP_ADD, src = XE_BLEND_ONE, dst = XE_BLEND_ZERO;
    if (flat) {
        src = XE_BLEND_SRCALPHA;
        if (b == Blend::Add) dst = XE_BLEND_ONE;
        else if (b == Blend::Subtract) {
            op = XE_BLENDOP_REVSUBTRACT;
            dst = XE_BLEND_ONE;
        } else {
            dst = XE_BLEND_INVSRCALPHA;
        }
    } else {
        switch (b) {
            case Blend::Normal: dst = XE_BLEND_INVSRCALPHA; break;
            case Blend::Add: dst = XE_BLEND_ONE; break;
            case Blend::Multiply:
                src = XE_BLEND_DESTCOLOR;
                dst = XE_BLEND_INVSRCALPHA;
                break;
            case Blend::Screen: dst = XE_BLEND_INVSRCCOLOR; break;
            case Blend::Subtract:
                op = XE_BLENDOP_REVSUBTRACT;
                dst = XE_BLEND_ONE;
                break;
            case Blend::Copy: break;
            case Blend::MulSrc:
                src = XE_BLEND_ZERO;
                dst = XE_BLEND_SRCCOLOR;
                break;
            case Blend::MulAdd:
                src = XE_BLEND_DESTCOLOR;
                dst = XE_BLEND_ONE;
                break;
            case Blend::Max:
                op = XE_BLENDOP_MAX;
                dst = XE_BLEND_ONE;
                break;
            case Blend::Min:
                op = XE_BLENDOP_MIN;
                dst = XE_BLEND_ONE;
                break;
            case Blend::MulSub:
                op = XE_BLENDOP_REVSUBTRACT;
                src = XE_BLEND_DESTCOLOR;
                dst = XE_BLEND_ONE;
                break;
        }
    }
    if (g_applied_blend[0] == src && g_applied_blend[1] == op && g_applied_blend[2] == dst) return;
    g_applied_blend[0] = src;
    g_applied_blend[1] = op;
    g_applied_blend[2] = dst;
    Xe_SetBlendControl(g_xe, src, op, dst, src, op, dst);
}

static void apply_stencil(int mode) {
    if (mode == g_applied_stencil) return;
    g_applied_stencil = mode;
    switch (mode) {
        case ST_OFF: Xe_SetStencilEnable(g_xe, 0); break;
        case ST_WRITE:
            Xe_SetStencilEnable(g_xe, 1);
            Xe_SetStencilFunc(g_xe, 3, XE_CMP_ALWAYS);
            Xe_SetStencilOp(g_xe, 3, XE_STENCILOP_KEEP, XE_STENCILOP_KEEP, XE_STENCILOP_REPLACE);
            Xe_SetStencilRef(g_xe, 3, 0xFF);
            Xe_SetStencilMask(g_xe, 3, 0xFF);
            Xe_SetStencilWriteMask(g_xe, 3, 0xFF);
            break;
        case ST_TEST:
            Xe_SetStencilEnable(g_xe, 1);
            Xe_SetStencilFunc(g_xe, 3, XE_CMP_EQUAL);
            Xe_SetStencilOp(g_xe, 3, XE_STENCILOP_KEEP, XE_STENCILOP_KEEP, XE_STENCILOP_KEEP);
            Xe_SetStencilRef(g_xe, 3, 0xFF);
            Xe_SetStencilMask(g_xe, 3, 0xFF);
            Xe_SetStencilWriteMask(g_xe, 3, 0x00);
            break;
        case ST_CLEAR:
            Xe_SetStencilEnable(g_xe, 1);
            Xe_SetStencilFunc(g_xe, 3, XE_CMP_ALWAYS);
            Xe_SetStencilOp(g_xe, 3, XE_STENCILOP_REPLACE, XE_STENCILOP_REPLACE, XE_STENCILOP_REPLACE);
            Xe_SetStencilRef(g_xe, 3, 0x00);
            Xe_SetStencilMask(g_xe, 3, 0xFF);
            Xe_SetStencilWriteMask(g_xe, 3, 0xFF);
            break;
    }
}

static void set_fetch(int slot, const XenosSurface& s, bool smooth, bool repeat) {
    u32* d = g_xe->fetch_constants + slot * 6;
    u32 addr = repeat ? XE_TEXADDR_WRAP : XE_TEXADDR_CLAMP;
    u32 filter = smooth ? 1 : 0;
    d[0] = 2u | (addr << 10) | (addr << 13) | ((u32)(s.wpitch / 128) << 22) | ((u32)s.tiled << 31);
    d[1] = s.ptr | (u32)s.format;
    d[2] = ((u32)(s.height - 1) << 13) | (u32)(s.width - 1);
    d[3] = 0xc14u | (filter << 19) | (filter << 21) | (2u << 23);
    d[4] = 0;
    d[5] = 0xa00u;
    Xe_DirtyFetch(g_xe, slot * 3, 3);
}

static void emit(Program prog, bool alpha_test, Texture* tex, bool smooth, bool repeat, const XenosSurface* lut, Blend blend, u32 mask, int stencil,
                 const Vtx* v, int n, bool quads, int w, int h, int y0, int mark) {
    if (n <= 0) return;
    ensure_resolved(tex);
    g_batches++;
    size_t bytes = (size_t)n * sizeof(Vtx);
    size_t at = (g_ring_used + 31) & ~(size_t)31;
    if (at + bytes > RING_BYTES) {
        sync_gpu();
        at = 0;
    }
    g_ring_used = at + bytes;
    g_ring_peak = std::max(g_ring_peak, g_ring_used);
    void* dst = Xe_VB_Lock(g_xe, g_vb, (int)at, (int)bytes, XE_LOCK_WRITE);
    std::memcpy(dst, v, bytes);
    Xe_VB_Unlock(g_xe, g_vb);
    Xe_SetStreamSource(g_xe, 0, g_vb, (int)at, (int)sizeof(Vtx));
    Xe_SetShader(g_xe, SHADER_TYPE_VERTEX, g_vs, 0);
    Xe_SetShader(g_xe, SHADER_TYPE_PIXEL, alpha_test && g_ps_at[prog] ? g_ps_at[prog] : g_ps[prog], 0);
    const float xf[8] = {2.0f / w, -2.0f / h, 1.0f, 1.0f, -1.0f - 1.0f / w, 1.0f + (2.0f * y0 + 1.0f) / h, 0.0f, 0.0f};
    Xe_SetVertexShaderConstantF(g_xe, 0, xf, 2);
    apply_blend(blend, prog == PROG_FLAT);
    apply_stencil(stencil);
    if (tex) set_fetch(0, tex->s, smooth, repeat);
    if (lut) set_fetch(1, *lut, false, false);
    Xe_pStuff(g_xe);
    Xe_pSetState(g_xe);
    write_reg(REG_COHER_DEST_BASE_1, 0);
    write_reg(REG_COLOR_MASK, mask);
    Xe_pSetIndexOffset(g_xe, 0);
    Xe_pDrawNonIndexed(g_xe, n, quads ? XE_PRIMTYPE_QUADLIST : XE_PRIMTYPE_TRIANGLELIST);
    g_serial++;
    if (mark >= 0) g_surface[mark].dirty = true;
    submit(false);
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

static void restore_target() {
    SurfaceInfo& si = g_surface[(int)g_target];
    if (!si.stale) return;
    si.stale = false;
    si.dirty = false;
    Vtx q[4];
    full_quad(q, (float)si.w, (float)si.h, 0xFFFFFFFFu);
    emit(PROG_TEXTURE, false, &si.tex, false, false, nullptr, Blend::Copy, MASK_RGBA, ST_CLEAR, q, 4, true, si.w, si.h, 0, -1);
}

static int stencil_state() {
    switch (g_stencil) {
        case Stencil::Write: return ST_WRITE;
        case Stencil::Test: return ST_TEST;
        default: return ST_OFF;
    }
}

static u32 write_mask() {
    u32 m = 0;
    if (!(g_protect & 0x0000FFu)) m |= 1;
    if (!(g_protect & 0x00FF00u)) m |= 2;
    if (!(g_protect & 0xFF0000u)) m |= 4;
    return m;
}

static void draw_now(Program prog, Texture* tex, const XenosSurface* lut, Blend blend, bool smooth, bool repeat, u32 mask, int stencil, const Vtx* v, int n,
                     bool quads) {
    if (n <= 0) return;
    ensure_resolved(tex);
    restore_target();
    const SurfaceInfo& si = g_surface[(int)g_target];
    emit(prog, stencil == ST_WRITE, tex, smooth, repeat, lut, blend, mask, stencil, v, n, quads, si.w, si.h, 0, (int)g_target);
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
    draw_now(p, g_tex, nullptr, g_blend, g_smooth, g_wrap, write_mask(), stencil_state(), g_batch, n, g_quads);
}

static void prepare(Texture* tex, Blend blend, bool smooth, bool quads, int verts, bool flat) {
    if (g_count && (g_tex != tex || g_blend != blend || g_smooth != smooth || g_quads != quads || g_flat != flat || g_count + verts > MAX_VTX)) flush();
    g_tex = tex;
    g_blend = blend;
    g_smooth = smooth;
    g_quads = quads;
    g_flat = flat;
}

static void wait_flip() {
    if (Xe_IsVBlank(g_xe) && plat::time_us() - g_last_flip >= FLIP_GUARD_US) return;
    while (Xe_IsVBlank(g_xe)) {
    }
    while (!Xe_IsVBlank(g_xe)) {
    }
}

static std::vector<Texture*> g_border;
static BorderTiles g_border_tiles;
static std::vector<BorderRect> g_border_bars, g_border_frame;
static uint32_t g_border_line = 0;
static float g_border_scroll = 0;

struct PresentRect {
    float x0, y0, w, h;
};

static PresentRect present_rect() {
    float aw = (float)g_display_w, ah = (float)g_display_h, ax = 0.0f, ay = 0.0f;
    if (g_overscan) {
        ax = (float)(g_display_w / 28);
        ay = (float)(g_display_h / 28);
        aw -= 2.0f * ax;
        ah -= 2.0f * ay;
    }
    float pixel_aspect = g_display_aspect / ((float)g_display_w / (float)g_display_h);
    float h = ah;
    float w = h * (4.0f / 3.0f) / pixel_aspect;
    if (w > aw) {
        w = aw;
        h = w * pixel_aspect * 0.75f;
    }
    return {ax + (aw - w) * 0.5f, ay + (ah - h) * 0.5f, w, h};
}

void border_canvas(float& x0, float& y0, float& x1, float& y1) {
    PresentRect r = present_rect();
    float sx = r.w / SCREEN_W, sy = r.h / SCREEN_H;
    x0 = -r.x0 / sx;
    y0 = -r.y0 / sy;
    x1 = (g_display_w - r.x0) / sx;
    y1 = (g_display_h - r.y0) / sy;
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
    g_border_tiles = border_tiles(*art, bars, nbars, 4096, 1);
    for (int k = 0; k < g_border_tiles.chunks; k++) {
        uint32_t* px = border_chunk_rgba(*art, g_border_tiles, k);
        int w = border_chunk_width(g_border_tiles, k);
        Texture* t = px ? texture_wrap(w, g_border_tiles.rows, w, px) : nullptr;
        if (t) t->cpu = nullptr;
        release(px);
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

static void present_border(const PresentRect& r, int y, int bh) {
    float sx = r.w / SCREEN_W, sy = r.h / SCREEN_H;
    BorderSpan spans[BORDER_SPANS];
    for (const BorderRect& b : g_border_bars) {
        float y0 = r.y0 + b.y0 * sy, y1 = r.y0 + b.y1 * sy;
        if (y1 <= y || y0 >= y + bh) continue;
        float v0 = (float)(b.y0 - g_border_tiles.y0), v1 = v0 + (float)(b.y1 - b.y0);
        int n = border_spans(g_border_tiles, b, g_border_scroll, spans);
        for (int i = 0; i < n; i++) {
            const BorderSpan& p = spans[i];
            Texture* t = g_border[p.chunk];
            float tw = (float)t->w, th = (float)t->h;
            set_ps_const(3, tw, th, 1.0f / tw, 1.0f / th);
            float px0 = r.x0 + p.x0 * sx, px1 = r.x0 + p.x1 * sx;
            float u0 = p.u / tw, u1 = (p.u + p.x1 - p.x0) / tw, tv0 = v0 / th, tv1 = v1 / th;
            Vtx q[4] = {vtx(px0, y0, u0, tv0, 0xFFFFFFFFu), vtx(px1, y0, u1, tv0, 0xFFFFFFFFu), vtx(px1, y1, u1, tv1, 0xFFFFFFFFu), vtx(px0, y1, u0, tv1, 0xFFFFFFFFu)};
            emit(PROG_PRESENT, false, t, true, false, nullptr, Blend::Copy, MASK_RGBA, ST_OFF, q, 4, true, g_display_w, bh, y, -1);
        }
    }
    set_ps_const(3, (float)SCREEN_W, (float)SCREEN_H, 1.0f / SCREEN_W, 1.0f / SCREEN_H);
    for (const BorderRect& f : g_border_frame) {
        float fx0 = r.x0 + f.x0 * sx, fx1 = r.x0 + f.x1 * sx, fy0 = r.y0 + f.y0 * sy, fy1 = r.y0 + f.y1 * sy;
        if (fy1 <= y || fy0 >= y + bh) continue;
        Vtx q[4] = {vtx(fx0, fy0, 0, 0, g_border_line), vtx(fx1, fy0, 0, 0, g_border_line), vtx(fx1, fy1, 0, 0, g_border_line), vtx(fx0, fy1, 0, 0, g_border_line)};
        emit(PROG_COLOR, false, nullptr, false, false, nullptr, Blend::Copy, MASK_RGBA, ST_OFF, q, 4, true, g_display_w, bh, y, -1);
    }
}

static void present_frame() {
    for (int i = 0; i < SURFACES; i++) {
        if (!g_surface[i].dirty) continue;
        resolve(i, &g_surface[i].tex.s);
        g_surface[i].dirty = false;
    }
    PresentRect r = present_rect();
    float x0 = r.x0, y0 = r.y0, w = r.w, h = r.h;
    float scale = h / SCREEN_H;
    if (scale < 1.0f) scale = 1.0f;
    set_ps_const(4, scale, 0.5f - 0.5f / scale, 0.5f, 1.0f);
    Vtx q[4] = {vtx(x0, y0, 0, 0, 0xFFFFFFFFu), vtx(x0 + w, y0, 1, 0, 0xFFFFFFFFu), vtx(x0 + w, y0 + h, 1, 1, 0xFFFFFFFFu), vtx(x0, y0 + h, 0, 1, 0xFFFFFFFFu)};
    XenosSurface& fb = g_fb[g_back];
    Texture* screen = &g_surface[(int)Surface::Screen].tex;
    for (int y = 0; y < g_display_h; y += g_band_h) {
        int bh = std::min(g_band_h, g_display_h - y);
        use_layout(g_display_w, bh, g_band_pitch, 0, g_band_depth, &fb);
        Xe_SetScissor(g_xe, 1, 0, 0, g_display_w, bh);
        if (!g_border.empty()) {
            present_border(r, y, bh);
        } else {
            Vtx black[4] = {vtx(0, (float)y, 0, 0, 0xFF000000u), vtx((float)g_display_w, (float)y, 0, 0, 0xFF000000u),
                            vtx((float)g_display_w, (float)(y + bh), 0, 0, 0xFF000000u), vtx(0, (float)(y + bh), 0, 0, 0xFF000000u)};
            emit(PROG_COLOR, false, nullptr, false, false, nullptr, Blend::Copy, MASK_RGBA, ST_OFF, black, 4, true, g_display_w, bh, y, -1);
        }
        emit(PROG_PRESENT, false, screen, true, false, nullptr, Blend::Copy, MASK_RGBA, ST_OFF, q, 4, true, g_display_w, bh, y, -1);
        XenosSurface band = fb;
        band.height = bh;
        band.ptr = fb.ptr + (u32)(y * fb.wpitch);
        band.base = (uint8_t*)fb.base + (size_t)y * fb.wpitch;
        Xe_ResolveInto(g_xe, &band, XE_SOURCE_COLOR, 0);
        after_resolve();
    }
    sync_gpu();
    wait_flip();
    Xe_SetFrameBufferSurface(g_xe, &fb);
    g_last_flip = plat::time_us();
    g_back ^= 1;
    for (SurfaceInfo& si : g_surface) si.stale = true;
    use_surface((int)g_target);
}

void present() {
    if (!g_frame_pending || !g_ready) return;
    present_frame();
    g_frame_pending = false;
}

void shutdown() {
    if (!g_ready) return;
    present();
    sync_gpu();
}

void reset_scissor();

void begin_frame(uint32_t clear_color) {
    if (!g_ready) return;
    present();
    if (g_synced_serial != g_serial) sync_gpu();
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
    use_surface((int)Surface::Screen);
    reset_scissor();
    SurfaceInfo& si = g_surface[(int)Surface::Screen];
    si.stale = false;
    Vtx q[4];
    full_quad(q, (float)si.w, (float)si.h, 0xFF000000u | (clear_color & 0xFFFFFFu));
    emit(PROG_COLOR, false, nullptr, false, false, nullptr, Blend::Copy, MASK_RGBA, ST_CLEAR, q, 4, true, si.w, si.h, 0, (int)Surface::Screen);
}

void end_frame() {
    if (!g_frame_open) return;
    flush();
    if (g_target != Surface::Screen) set_target(Surface::Screen);
    if (g_stencil != Stencil::Off) set_stencil(Stencil::Off);
    if (g_protect) set_color_mask(0);
    submit(true);
    g_frame_open = false;
    g_frame_pending = true;
}

int list_peak_bytes() { return (int)g_ring_peak; }

bool reclaim_memory() {
    if (g_frame_open || !g_ready) return false;
    sync_gpu();
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
    Texture* t = &g_surface[(int)Surface::Screen].tex;
    prepare_read(t);
    const uint32_t* px = (const uint32_t*)t->s.base;
    int pitch = t->s.wpitch / 4;
    for (int i = 0; i < w; i++) out[i] = from_argb(px[tiled_texel(x + i, y, pitch)]);
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
    if (scissor_empty() || !g_xe) return;
    Xe_SetScissor(g_xe, 1, g_sx0, g_sy0, g_sx1, g_sy1);
}

void reset_scissor() {
    flush();
    int tw, th;
    target_size(tw, th);
    g_sx0 = 0;
    g_sy0 = 0;
    g_sx1 = tw;
    g_sy1 = th;
    if (g_xe) Xe_SetScissor(g_xe, 1, 0, 0, tw, th);
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
    SurfaceInfo& grab = g_surface[(int)Surface::Grab];
    resolve((int)Surface::Screen, &grab.tex.s);
    grab.dirty = false;
    grab.stale = true;
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
    draw_now(PROG_GRAY, &g_surface[(int)Surface::Grab].tex, nullptr, Blend::Copy, false, false, write_mask(), stencil_state(), q, 4, true);
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
    use_surface((int)s);
    Xe_SetScissor(g_xe, 1, 0, 0, g_sx1, g_sy1);
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
    draw_now(PROG_COLOR, nullptr, nullptr, Blend::Copy, false, false, 0, ST_CLEAR, q, 4, true);
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
        sync_gpu();
        g_lut_row = 0;
    }
    int row = g_lut_row++;
    uint32_t* d = row_of(g_lut, row);
    for (int i = 0; i < 256; i++) d[i] = to_argb(lut[i]);
    upload_range(g_lut, (size_t)row * g_lut.wpitch, 1024);
    set_ps_const(1, 255.0f / 256.0f, 0.5f / 256.0f, 0.0f, ((float)row + 0.5f) / LUT_ROWS);
    Vtx q[4] = {vtx(x0, y0, nu(tex, u0), nv(tex, v0), 0xFFFFFFFFu), vtx(x1, y0, nu(tex, u1), nv(tex, v0), 0xFFFFFFFFu),
                vtx(x1, y1, nu(tex, u1), nv(tex, v1), 0xFFFFFFFFu), vtx(x0, y1, nu(tex, u0), nv(tex, v1), 0xFFFFFFFFu)};
    int ch = std::clamp(channel, 0, 3);
    for (Vtx& v : q) v.e[ch] = 1.0f;
    draw_now(PROG_LUT, tex, &g_lut, blend, false, g_wrap, write_mask(), stencil_state(), q, 4, true);
}

}
