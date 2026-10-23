#include "../gfx.h"

#include <malloc.h>
#include <rsx/rsx.h>
#include <sys/thread.h>
#include <sysutil/video.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../common/embedded_shaders.h"
#include "../platform.h"

extern "C" s32 rsxContextCallback(gcmContextData* context, u32 count);

namespace gfx {

constexpr int MAX_VTX = 4096;
constexpr u32 HOST_BYTES = 16u << 20;
constexpr u32 COMMAND_BYTES = 0x100000;
constexpr size_t RING_BYTES = 8u << 20;
constexpr u8 LABEL = 255;
constexpr u32 GROUP_WORDS = 1024;
constexpr u16 BLEND_MIN = 0x8007;
constexpr u16 BLEND_MAX = 0x8008;

struct Texture {
    int w = 0;
    int h = 0;
    int pitch = 0;
    uint8_t* gpu = nullptr;
    u32 offset = 0;
    size_t bytes = 0;
    size_t report = 0;
    uint32_t* cpu = nullptr;
    int cpu_stride = 0;
    bool surface = false;
};

struct Vtx {
    float x, y, u, v;
    uint8_t r, g, b, a;
    float e[4];
};

enum Program { PROG_COLOR, PROG_TEXTURE, PROG_FLAT, PROG_LUT, PROG_GRAY, PROG_PRESENT, PROG_COUNT };

static const char* const FRAGMENT_NAMES[PROG_COUNT] = {"color_f", "texture_f", "flat_f", "lut_f", "gray_f", "present_f"};

struct Fragment {
    rsxFragmentProgram* program = nullptr;
    u32 offset = 0;
};

struct SurfaceInfo {
    int w = 0;
    int h = 0;
    u32 pitch = 0;
    u32 depth_offset = 0;
    u32 depth_pitch = 0;
    Texture tex;
};

static gcmContextData* g_ctx = nullptr;
static void* g_host = nullptr;
static rsxVertexProgram* g_vertex = nullptr;
static void* g_vertex_ucode = nullptr;
static rsxProgramConst* g_transform = nullptr;
static u8 g_attr_pos = 0, g_attr_uv = 8, g_attr_color = 3, g_attr_extra = 9;
static Fragment g_frag[PROG_COUNT];
static uint8_t* g_ring = nullptr;
static size_t g_ring_used = 0;
static size_t g_ring_peak = 0;
static u32 g_label = 1;

static SurfaceInfo g_surface[6];
static Surface g_target = Surface::Screen;
static int g_display_w = 0, g_display_h = 0;
static float g_display_aspect = 16.0f / 9.0f;
static u32 g_display_pitch = 0;
static u32 g_display_offset[2] = {0, 0};
static u32 g_display_depth_offset = 0;
static int g_display_index = 0;
static bool g_first_flip = true;

static Vtx g_batch[MAX_VTX];
static int g_count = 0;
static bool g_quads = true;
static Texture* g_tex = nullptr;
static Blend g_blend = Blend::Normal;
static bool g_smooth = false;
static bool g_flat = false;
static int g_batches = 0;

static bool g_frame_open = false;
static bool g_frame_pending = false;
static bool g_dirty_textures = true;
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

static void room() {
    if (g_ctx->current + GROUP_WORDS > g_ctx->end) rsxContextCallback(g_ctx, GROUP_WORDS);
}

static void wait_idle() {
    room();
    rsxSetWriteBackendLabel(g_ctx, LABEL, g_label);
    rsxFlushBuffer(g_ctx);
    while (*(vu32*)gcmGetLabelAddress(LABEL) != g_label) usleep(30);
    g_label++;
}

static void collect_garbage() {
    for (Texture* t : g_garbage) {
        if (g_tex == t) g_tex = nullptr;
        if (t->gpu) rsxFree(t->gpu);
        delete t;
    }
    g_garbage.clear();
}

static void* ring_alloc(size_t bytes, size_t align) {
    size_t at = (g_ring_used + align - 1) / align * align;
    if (at + bytes > RING_BYTES) {
        wait_idle();
        at = 0;
    }
    g_ring_used = at + bytes;
    g_ring_peak = std::max(g_ring_peak, g_ring_used);
    return g_ring + at;
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

static void flush();

static bool (*g_evict)() = nullptr;
static sys_ppu_thread_t g_main_thread = 0;
static bool g_evicting = false;

void set_evict_handler(bool (*fn)()) { g_evict = fn; }

static sys_ppu_thread_t self_id() {
    sys_ppu_thread_t id = 0;
    sysThreadGetId(&id);
    return id;
}

static void* vram_alloc(size_t bytes) {
    void* p = rsxMemalign(128, bytes);
    if (p || !g_evict || g_evicting || self_id() != g_main_thread) return p;
    g_evicting = true;
    flush();
    wait_idle();
    collect_garbage();
    p = rsxMemalign(128, bytes);
    while (!p && g_evict()) {
        wait_idle();
        collect_garbage();
        p = rsxMemalign(128, bytes);
    }
    g_evicting = false;
    if (p) plat::log("low memory: freed images for %u bytes of gpu memory", (unsigned)bytes);
    return p;
}

static bool gpu_alloc(Texture* t, int w, int h) {
    t->w = w;
    t->h = h;
    t->pitch = ((w * 4) + 63) & ~63;
    t->bytes = (size_t)t->pitch * h;
    t->gpu = (uint8_t*)vram_alloc(t->bytes);
    if (!t->gpu) return false;
    rsxAddressToOffset(t->gpu, &t->offset);
    return true;
}

Texture* texture_create(int w, int h, int stride, int rows, Format fmt, bool swizzled, void* pixels, uint32_t* palette) {
    Texture* t = new Texture();
    if (!gpu_alloc(t, w, h)) {
        plat::log("gfx: out of video memory for %dx%d", w, h);
        release(pixels);
        release(palette);
        delete t;
        return nullptr;
    }
    const uint8_t* src = (const uint8_t*)pixels;
    if (swizzled && fmt == Format::RGBA8888) {
        for (int y = 0; y < h; y++) std::memcpy(t->gpu + (size_t)y * t->pitch, src + (size_t)y * stride * 4, (size_t)w * 4);
    } else {
        int bpp = bpp_of(fmt);
        for (int y = 0; y < h; y++) {
            uint32_t* row = (uint32_t*)(t->gpu + (size_t)y * t->pitch);
            for (int x = 0; x < w; x++) row[x] = to_argb(expand(fmt, src + ((size_t)y * stride + x) * bpp, palette));
        }
    }
    (void)rows;
    t->report = t->bytes;
    release(pixels);
    release(palette);
    g_dirty_textures = true;
    return t;
}

Texture* texture_wrap(int w, int h, int stride, uint32_t* pixels) {
    Texture* t = new Texture();
    if (!gpu_alloc(t, w, h)) {
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
    if (!t || !t->cpu || !t->gpu) return;
    for (int y = 0; y < t->h; y++) {
        const uint32_t* s = t->cpu + (size_t)y * t->cpu_stride;
        uint32_t* d = (uint32_t*)(t->gpu + (size_t)y * t->pitch);
        for (int x = 0; x < t->w; x++) d[x] = to_argb(s[x]);
    }
    g_dirty_textures = true;
}

bool texture_read(const Texture* t, int x, int y, uint32_t* out) {
    if (!t || x < 0 || y < 0 || x >= t->w || y >= t->h) return false;
    if (t->cpu) {
        *out = t->cpu[(size_t)y * t->cpu_stride + x];
        return true;
    }
    if (t->surface) wait_idle();
    *out = from_argb(((const uint32_t*)(t->gpu + (size_t)y * t->pitch))[x]);
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
    if (!vs) return false;
    g_vertex = (rsxVertexProgram*)vs->data;
    u32 size = 0;
    rsxVertexProgramGetUCode(g_vertex, &g_vertex_ucode, &size);
    g_transform = rsxVertexProgramGetConst(g_vertex, "uTransform");
    rsxProgramAttrib* pos = rsxVertexProgramGetAttrib(g_vertex, "aPosition");
    rsxProgramAttrib* uv = rsxVertexProgramGetAttrib(g_vertex, "aTexcoord");
    rsxProgramAttrib* col = rsxVertexProgramGetAttrib(g_vertex, "aColor");
    rsxProgramAttrib* extra = rsxVertexProgramGetAttrib(g_vertex, "aExtra");
    if (!g_transform || !pos || !uv || !col || !extra) return false;
    g_attr_pos = (u8)pos->index;
    g_attr_uv = (u8)uv->index;
    g_attr_color = (u8)col->index;
    g_attr_extra = (u8)extra->index;
    for (int p = 0; p < PROG_COUNT; p++) {
        const EmbeddedShader* fs = embedded_shader(FRAGMENT_NAMES[p]);
        if (!fs) return false;
        g_frag[p].program = (rsxFragmentProgram*)fs->data;
        void* ucode = nullptr;
        u32 bytes = 0;
        rsxFragmentProgramGetUCode(g_frag[p].program, &ucode, &bytes);
        void* mem = rsxMemalign(64, bytes);
        if (!mem) return false;
        std::memcpy(mem, ucode, bytes);
        rsxAddressToOffset(mem, &g_frag[p].offset);
    }
    return true;
}

static bool setup_surface(Surface s, int w, int h) {
    SurfaceInfo& si = g_surface[(int)s];
    si.w = w;
    si.h = h;
    if (!gpu_alloc(&si.tex, w, h)) return false;
    si.pitch = (u32)si.tex.pitch;
    si.tex.surface = true;
    for (int y = 0; y < h; y++) {
        uint32_t* row = (uint32_t*)(si.tex.gpu + (size_t)y * si.pitch);
        for (int x = 0; x < w; x++) row[x] = 0xFF000000u;
    }
    si.depth_pitch = ((u32)w * 4 + 63) & ~63u;
    void* depth = rsxMemalign(64, (size_t)si.depth_pitch * h);
    if (!depth) return false;
    std::memset(depth, 0, (size_t)si.depth_pitch * h);
    rsxAddressToOffset(depth, &si.depth_offset);
    return true;
}

static void bind_surface(u32 color_offset, u32 color_pitch, u32 depth_offset, u32 depth_pitch, int w, int h, bool display) {
    room();
    gcmSurface sf;
    std::memset(&sf, 0, sizeof(sf));
    sf.colorFormat = display ? GCM_SURFACE_X8R8G8B8 : GCM_SURFACE_A8R8G8B8;
    sf.colorTarget = GCM_SURFACE_TARGET_0;
    sf.colorLocation[0] = GCM_LOCATION_RSX;
    sf.colorOffset[0] = color_offset;
    sf.colorPitch[0] = color_pitch;
    for (int i = 1; i < 4; i++) {
        sf.colorLocation[i] = GCM_LOCATION_RSX;
        sf.colorOffset[i] = 0;
        sf.colorPitch[i] = 64;
    }
    sf.depthFormat = GCM_SURFACE_ZETA_Z24S8;
    sf.depthLocation = GCM_LOCATION_RSX;
    sf.depthOffset = depth_offset;
    sf.depthPitch = depth_pitch;
    sf.type = GCM_SURFACE_TYPE_LINEAR;
    sf.antiAlias = GCM_SURFACE_CENTER_1;
    sf.width = (u16)w;
    sf.height = (u16)h;
    sf.x = 0;
    sf.y = 0;
    rsxSetSurface(g_ctx, &sf);
    f32 scale[4] = {w * 0.5f, h * -0.5f, 0.5f, 0.0f};
    f32 offset[4] = {w * 0.5f, h * 0.5f, 0.5f, 0.0f};
    rsxSetViewport(g_ctx, 0, 0, (u16)w, (u16)h, 0.0f, 1.0f, scale, offset);
    for (int i = 0; i < 8; i++) rsxSetViewportClip(g_ctx, (u8)i, (u16)w, (u16)h);
    rsxSetScissor(g_ctx, 0, 0, (u16)w, (u16)h);
    rsxInvalidateTextureCache(g_ctx, GCM_INVALIDATE_TEXTURE);
}

static void apply_stencil(Stencil mode) {
    room();
    switch (mode) {
        case Stencil::Off:
            rsxSetStencilTestEnable(g_ctx, GCM_FALSE);
            rsxSetAlphaTestEnable(g_ctx, GCM_FALSE);
            break;
        case Stencil::Write:
            rsxSetStencilTestEnable(g_ctx, GCM_TRUE);
            rsxSetStencilFunc(g_ctx, GCM_ALWAYS, 0xFF, 0xFF);
            rsxSetStencilOp(g_ctx, GCM_KEEP, GCM_KEEP, GCM_REPLACE);
            rsxSetStencilMask(g_ctx, 0xFF);
            rsxSetAlphaTestEnable(g_ctx, GCM_TRUE);
            rsxSetAlphaFunc(g_ctx, GCM_GREATER, 0);
            break;
        case Stencil::Test:
            rsxSetStencilTestEnable(g_ctx, GCM_TRUE);
            rsxSetStencilFunc(g_ctx, GCM_EQUAL, 0xFF, 0xFF);
            rsxSetStencilOp(g_ctx, GCM_KEEP, GCM_KEEP, GCM_KEEP);
            rsxSetStencilMask(g_ctx, 0x00);
            rsxSetAlphaTestEnable(g_ctx, GCM_FALSE);
            break;
    }
}

static void bind_target(Surface s) {
    SurfaceInfo& si = g_surface[(int)s];
    bind_surface(si.tex.offset, si.pitch, si.depth_offset, si.depth_pitch, si.w, si.h, false);
    apply_stencil(g_stencil);
}

static void choose_display() {
    static const u32 ids[] = {VIDEO_RESOLUTION_1080, VIDEO_RESOLUTION_720, VIDEO_RESOLUTION_576, VIDEO_RESOLUTION_480};
    videoResolution res;
    std::memset(&res, 0, sizeof(res));
    u32 id = VIDEO_RESOLUTION_480;
    for (u32 c : ids) {
        if (videoGetResolutionAvailability(VIDEO_PRIMARY, c, VIDEO_ASPECT_AUTO, 0) == 1 && videoGetResolution(c, &res) == 0) {
            id = c;
            break;
        }
    }
    videoGetResolution(id, &res);
    videoConfiguration config;
    std::memset(&config, 0, sizeof(config));
    config.resolution = (u8)id;
    config.format = VIDEO_BUFFER_FORMAT_XRGB;
    config.aspect = VIDEO_ASPECT_AUTO;
    config.pitch = (u32)res.width * 4;
    videoConfigure(VIDEO_PRIMARY, &config, nullptr, 0);
    videoState state;
    std::memset(&state, 0, sizeof(state));
    videoGetState(VIDEO_PRIMARY, 0, &state);
    g_display_aspect = state.displayMode.aspect == VIDEO_ASPECT_4_3 ? 4.0f / 3.0f : 16.0f / 9.0f;
    g_display_w = res.width;
    g_display_h = res.height;
}

void init() {
    g_main_thread = self_id();
    g_host = memalign(1024 * 1024, HOST_BYTES);
    if (rsxInit(&g_ctx, COMMAND_BYTES, HOST_BYTES, g_host) != 0 || !g_ctx) {
        plat::log("gfx: rsxInit failed");
        return;
    }
    choose_display();
    wait_idle();
    gcmSetFlipMode(GCM_FLIP_VSYNC);
    g_display_pitch = ((u32)g_display_w * 4 + 63) & ~63u;
    for (int i = 0; i < 2; i++) {
        void* mem = rsxMemalign(64, (size_t)g_display_pitch * g_display_h);
        std::memset(mem, 0, (size_t)g_display_pitch * g_display_h);
        rsxAddressToOffset(mem, &g_display_offset[i]);
        gcmSetDisplayBuffer((u8)i, g_display_offset[i], g_display_pitch, (u32)g_display_w, (u32)g_display_h);
    }
    void* depth = rsxMemalign(64, (size_t)g_display_pitch * g_display_h);
    rsxAddressToOffset(depth, &g_display_depth_offset);
    if (!setup_programs()) {
        plat::log("gfx: shader setup failed");
        return;
    }
    g_ring = (uint8_t*)rsxMemalign(128, RING_BYTES);
    setup_surface(Surface::Screen, SCREEN_W, SCREEN_H);
    setup_surface(Surface::Grab, SCREEN_W, SCREEN_H);
    setup_surface(Surface::Half0, HALF_W, HALF_H);
    setup_surface(Surface::Half1, HALF_W, HALF_H);
    setup_surface(Surface::Half2, HALF_W, HALF_H);
    setup_surface(Surface::Wide, SCREEN_W, SCREEN_H);
    room();
    rsxSetDepthTestEnable(g_ctx, GCM_FALSE);
    rsxSetDepthWriteEnable(g_ctx, GCM_FALSE);
    rsxSetCullFaceEnable(g_ctx, GCM_FALSE);
    rsxSetZMinMaxControl(g_ctx, GCM_FALSE, GCM_TRUE, GCM_FALSE);
    rsxSetUserClipPlaneControl(g_ctx, GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE,
                               GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE, GCM_USER_CLIP_PLANE_DISABLE);
    wait_idle();
}

static void set_blend(Blend b, bool flat) {
    u16 eq = GCM_FUNC_ADD, src = GCM_ONE, dst = GCM_ZERO;
    bool enable = true;
    if (flat) {
        src = GCM_SRC_ALPHA;
        if (b == Blend::Add) dst = GCM_ONE;
        else if (b == Blend::Subtract) {
            eq = GCM_FUNC_REVERSE_SUBTRACT;
            dst = GCM_ONE;
        } else {
            dst = GCM_ONE_MINUS_SRC_ALPHA;
        }
    } else {
        switch (b) {
            case Blend::Normal: dst = GCM_ONE_MINUS_SRC_ALPHA; break;
            case Blend::Add: dst = GCM_ONE; break;
            case Blend::Multiply:
                src = GCM_DST_COLOR;
                dst = GCM_ONE_MINUS_SRC_ALPHA;
                break;
            case Blend::Screen: dst = GCM_ONE_MINUS_SRC_COLOR; break;
            case Blend::Subtract:
                eq = GCM_FUNC_REVERSE_SUBTRACT;
                dst = GCM_ONE;
                break;
            case Blend::Copy: enable = false; break;
            case Blend::MulSrc:
                src = GCM_ZERO;
                dst = GCM_SRC_COLOR;
                break;
            case Blend::MulAdd:
                src = GCM_DST_COLOR;
                dst = GCM_ONE;
                break;
            case Blend::Max:
                eq = BLEND_MAX;
                dst = GCM_ONE;
                break;
            case Blend::Min:
                eq = BLEND_MIN;
                dst = GCM_ONE;
                break;
            case Blend::MulSub:
                eq = GCM_FUNC_REVERSE_SUBTRACT;
                src = GCM_DST_COLOR;
                dst = GCM_ONE;
                break;
        }
    }
    rsxSetBlendEnable(g_ctx, enable ? GCM_TRUE : GCM_FALSE);
    if (!enable) return;
    rsxSetBlendFunc(g_ctx, src, dst, src, dst);
    rsxSetBlendEquation(g_ctx, eq, eq);
}

static u32 write_mask() {
    u32 m = 0;
    if (!(g_protect & 0x0000FFu)) m |= GCM_COLOR_MASK_R;
    if (!(g_protect & 0x00FF00u)) m |= GCM_COLOR_MASK_G;
    if (!(g_protect & 0xFF0000u)) m |= GCM_COLOR_MASK_B;
    return m;
}

static void bind_texture(u8 unit, const Texture* t, bool smooth, bool repeat) {
    gcmTexture tex;
    std::memset(&tex, 0, sizeof(tex));
    tex.format = GCM_TEXTURE_FORMAT_A8R8G8B8 | GCM_TEXTURE_FORMAT_LIN;
    tex.mipmap = 1;
    tex.dimension = GCM_TEXTURE_DIMS_2D;
    tex.cubemap = GCM_FALSE;
    tex.remap = ((GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) | (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) |
                 (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) | (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) |
                 (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_B_SHIFT) | (GCM_TEXTURE_REMAP_COLOR_G << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) |
                 (GCM_TEXTURE_REMAP_COLOR_R << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) | (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT));
    tex.width = (u16)t->w;
    tex.height = (u16)t->h;
    tex.depth = 1;
    tex.location = GCM_LOCATION_RSX;
    tex.pitch = (u32)t->pitch;
    tex.offset = t->offset;
    rsxLoadTexture(g_ctx, unit, &tex);
    rsxTextureControl(g_ctx, unit, GCM_TRUE, 0 << 8, 12 << 8, GCM_TEXTURE_MAX_ANISO_1);
    u8 f = smooth ? GCM_TEXTURE_LINEAR : GCM_TEXTURE_NEAREST;
    rsxTextureFilter(g_ctx, unit, 0, f, f, GCM_TEXTURE_CONVOLUTION_QUINCUNX);
    bool pot = (t->w & (t->w - 1)) == 0 && (t->h & (t->h - 1)) == 0;
    u8 wrap = repeat && pot ? GCM_TEXTURE_REPEAT : GCM_TEXTURE_CLAMP_TO_EDGE;
    rsxTextureWrapMode(g_ctx, unit, wrap, wrap, wrap, 0, GCM_TEXTURE_ZFUNC_LESS, 0);
}

static void draw_now(Program prog, Texture* tex, const Texture* lut, Blend blend, bool smooth, bool repeat, u32 mask, const Vtx* v, int n, bool quads, int w,
                     int h) {
    if (n <= 0) return;
    room();
    g_batches++;
    if (g_dirty_textures) {
        rsxInvalidateTextureCache(g_ctx, GCM_INVALIDATE_TEXTURE);
        g_dirty_textures = false;
    }
    rsxLoadVertexProgram(g_ctx, g_vertex, g_vertex_ucode);
    const f32 xf[4] = {2.0f / w, -2.0f / h, -1.0f, 1.0f};
    rsxSetVertexProgramParameter(g_ctx, g_vertex, g_transform, xf);
    rsxLoadFragmentProgramLocation(g_ctx, g_frag[prog].program, g_frag[prog].offset, GCM_LOCATION_RSX);
    set_blend(blend, prog == PROG_FLAT);
    rsxSetColorMask(g_ctx, mask);
    rsxSetColorMaskMrt(g_ctx, 0);
    if (tex) bind_texture(0, tex, smooth, repeat);
    if (lut) bind_texture(1, lut, false, false);
    Vtx* mem = (Vtx*)ring_alloc((size_t)n * sizeof(Vtx), 128);
    std::memcpy(mem, v, (size_t)n * sizeof(Vtx));
    u32 off = 0;
    rsxAddressToOffset(mem, &off);
    rsxBindVertexArrayAttrib(g_ctx, g_attr_pos, 0, off + offsetof(Vtx, x), sizeof(Vtx), 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(g_ctx, g_attr_uv, 0, off + offsetof(Vtx, u), sizeof(Vtx), 2, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(g_ctx, g_attr_color, 0, off + offsetof(Vtx, r), sizeof(Vtx), 4, GCM_VERTEX_DATA_TYPE_U8, GCM_LOCATION_RSX);
    rsxBindVertexArrayAttrib(g_ctx, g_attr_extra, 0, off + offsetof(Vtx, e), sizeof(Vtx), 4, GCM_VERTEX_DATA_TYPE_F32, GCM_LOCATION_RSX);
    rsxInvalidateVertexCache(g_ctx);
    rsxDrawVertexArray(g_ctx, quads ? GCM_TYPE_QUADS : GCM_TYPE_TRIANGLES, 0, (u32)n);
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
    int w, h;
    target_size(w, h);
    Program p = g_tex ? (g_flat ? PROG_FLAT : PROG_TEXTURE) : PROG_COLOR;
    draw_now(p, g_tex, nullptr, g_blend, g_smooth, g_wrap, write_mask(), g_batch, n, g_quads, w, h);
}

static void prepare(Texture* tex, Blend blend, bool smooth, bool quads, int verts, bool flat) {
    if (g_count && (g_tex != tex || g_blend != blend || g_smooth != smooth || g_quads != quads || g_flat != flat || g_count + verts > MAX_VTX)) flush();
    g_tex = tex;
    g_blend = blend;
    g_smooth = smooth;
    g_quads = quads;
    g_flat = flat;
}

static inline Vtx vtx(float x, float y, float u, float v, uint32_t c) {
    Vtx o;
    o.x = x;
    o.y = y;
    o.u = u;
    o.v = v;
    o.r = (uint8_t)c;
    o.g = (uint8_t)(c >> 8);
    o.b = (uint8_t)(c >> 16);
    o.a = (uint8_t)(c >> 24);
    o.e[0] = o.e[1] = o.e[2] = o.e[3] = 0.0f;
    return o;
}

static void present_frame() {
    u32 color_off = g_display_offset[g_display_index];
    bind_surface(color_off, g_display_pitch, g_display_depth_offset, g_display_pitch, g_display_w, g_display_h, true);
    room();
    rsxSetStencilTestEnable(g_ctx, GCM_FALSE);
    rsxSetAlphaTestEnable(g_ctx, GCM_FALSE);
    rsxSetColorMask(g_ctx, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
    rsxSetClearColor(g_ctx, 0xFF000000u);
    rsxClearSurface(g_ctx, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);
    float h = (float)g_display_h;
    float w = h * (4.0f / 3.0f) * ((float)g_display_w / (float)g_display_h) / g_display_aspect;
    float x0 = (g_display_w - w) * 0.5f;
    float scale = h / SCREEN_H;
    Vtx q[4] = {vtx(x0, 0, 0, 0, 0xFFFFFFFFu), vtx(x0 + w, 0, 1, 0, 0xFFFFFFFFu), vtx(x0 + w, h, 1, 1, 0xFFFFFFFFu), vtx(x0, h, 0, 1, 0xFFFFFFFFu)};
    for (Vtx& v : q) {
        v.e[0] = (float)SCREEN_W;
        v.e[1] = (float)SCREEN_H;
        v.e[2] = scale < 1.0f ? 1.0f : scale;
    }
    draw_now(PROG_PRESENT, &g_surface[(int)Surface::Screen].tex, nullptr, Blend::Copy, true, false,
             GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B, q, 4, true, g_display_w, g_display_h);
    if (!g_first_flip) {
        while (gcmGetFlipStatus() != 0) usleep(200);
    }
    gcmResetFlipStatus();
    gcmSetFlip(g_ctx, (u8)g_display_index);
    rsxFlushBuffer(g_ctx);
    gcmSetWaitFlip(g_ctx);
    g_display_index ^= 1;
    g_first_flip = false;
}

void present() {
    if (!g_frame_pending || !g_ctx) return;
    present_frame();
    g_frame_pending = false;
}

void shutdown() {
    if (!g_ctx) return;
    present();
    room();
    gcmSetWaitFlip(g_ctx);
    rsxFinish(g_ctx, 1);
}

void reset_scissor();

void begin_frame(uint32_t clear_color) {
    if (!g_ctx) return;
    present();
    wait_idle();
    collect_garbage();
    g_ring_used = 0;
    g_count = 0;
    g_tex = nullptr;
    g_target = Surface::Screen;
    g_protect = 0;
    g_wrap = false;
    g_stencil = Stencil::Off;
    g_frame_open = true;
    bind_target(Surface::Screen);
    room();
    rsxSetColorMask(g_ctx, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B | GCM_COLOR_MASK_A);
    rsxSetClearColor(g_ctx, 0xFF000000u | (to_argb(clear_color) & 0xFFFFFFu));
    rsxSetClearDepthStencil(g_ctx, 0xFFFFFF00u);
    rsxClearSurface(g_ctx, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A | GCM_CLEAR_S | GCM_CLEAR_Z);
    reset_scissor();
}

void end_frame() {
    if (!g_frame_open) return;
    flush();
    if (g_target != Surface::Screen) set_target(Surface::Screen);
    if (g_stencil != Stencil::Off) set_stencil(Stencil::Off);
    if (g_protect) set_color_mask(0);
    rsxFlushBuffer(g_ctx);
    g_frame_open = false;
    g_frame_pending = true;
}

int list_peak_bytes() { return (int)g_ring_peak; }

bool reclaim_memory() {
    if (g_frame_open) return false;
    wait_idle();
    collect_garbage();
    return true;
}

double take_batch_count() {
    double n = g_batches;
    g_batches = 0;
    return n;
}

const uint32_t* framebuffer() { return nullptr; }

bool capture_row(int y, int x, int w, uint32_t* out) {
    if (y < 0 || y >= SCREEN_H || x < 0 || x + w > SCREEN_W) return false;
    flush();
    wait_idle();
    const Texture& t = g_surface[(int)Surface::Screen].tex;
    const uint32_t* row = (const uint32_t*)(t.gpu + (size_t)y * t.pitch);
    for (int i = 0; i < w; i++) out[i] = from_argb(row[x + i]);
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
    if (scissor_empty()) return;
    room();
    rsxSetScissor(g_ctx, (u16)g_sx0, (u16)g_sy0, (u16)(g_sx1 - g_sx0), (u16)(g_sy1 - g_sy0));
}

void reset_scissor() {
    flush();
    int tw, th;
    target_size(tw, th);
    g_sx0 = 0;
    g_sy0 = 0;
    g_sx1 = tw;
    g_sy1 = th;
    room();
    rsxSetScissor(g_ctx, 0, 0, (u16)tw, (u16)th);
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
    Surface cur = g_target;
    Texture& screen = g_surface[(int)Surface::Screen].tex;
    g_target = Surface::Grab;
    bind_target(Surface::Grab);
    room();
    rsxSetStencilTestEnable(g_ctx, GCM_FALSE);
    rsxSetAlphaTestEnable(g_ctx, GCM_FALSE);
    Vtx q[4] = {vtx(0, 0, 0, 0, 0xFFFFFFFFu), vtx(SCREEN_W, 0, 1, 0, 0xFFFFFFFFu), vtx(SCREEN_W, SCREEN_H, 1, 1, 0xFFFFFFFFu),
                vtx(0, SCREEN_H, 0, 1, 0xFFFFFFFFu)};
    draw_now(PROG_TEXTURE, &screen, nullptr, Blend::Copy, false, false, GCM_COLOR_MASK_R | GCM_COLOR_MASK_G | GCM_COLOR_MASK_B, q, 4, true, SCREEN_W, SCREEN_H);
    g_dirty_textures = true;
    g_target = cur;
    bind_target(cur);
    if (scissor_empty()) return;
    room();
    rsxSetScissor(g_ctx, (u16)g_sx0, (u16)g_sy0, (u16)(g_sx1 - g_sx0), (u16)(g_sy1 - g_sy0));
}

void desaturate(float amount) {
    if (amount <= 0 || !g_frame_open || scissor_empty()) return;
    if (amount > 1) amount = 1;
    flush();
    copy_screen_to_grab();
    float level = (float)(int)(amount * 255.0f + 0.5f) / 255.0f;
    Texture* g = &g_surface[(int)Surface::Grab].tex;
    Vtx q[4] = {vtx(0, 0, 0, 0, 0xFFFFFFFFu), vtx(SCREEN_W, 0, 1, 0, 0xFFFFFFFFu), vtx(SCREEN_W, SCREEN_H, 1, 1, 0xFFFFFFFFu),
                vtx(0, SCREEN_H, 0, 1, 0xFFFFFFFFu)};
    for (Vtx& v : q) v.e[0] = level;
    int w, h;
    target_size(w, h);
    draw_now(PROG_GRAY, g, nullptr, Blend::Copy, false, false, write_mask(), q, 4, true, w, h);
}

Texture* grab_screen() {
    flush();
    if (g_frame_open) copy_screen_to_grab();
    return &g_surface[(int)Surface::Grab].tex;
}

void set_target(Surface s) {
    flush();
    g_target = s;
    if (g_frame_open) bind_target(s);
    g_dirty_textures = true;
    g_sx0 = 0;
    g_sy0 = 0;
    g_sx1 = g_surface[(int)s].w;
    g_sy1 = g_surface[(int)s].h;
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
    if (g_frame_open) apply_stencil(mode);
}

void stencil_clear(int x0, int y0, int x1, int y1) {
    if (x1 <= x0 || y1 <= y0 || !g_frame_open || scissor_empty()) return;
    flush();
    room();
    rsxSetStencilTestEnable(g_ctx, GCM_TRUE);
    rsxSetStencilFunc(g_ctx, GCM_ALWAYS, 0, 0xFF);
    rsxSetStencilOp(g_ctx, GCM_REPLACE, GCM_REPLACE, GCM_REPLACE);
    rsxSetStencilMask(g_ctx, 0xFF);
    rsxSetAlphaTestEnable(g_ctx, GCM_FALSE);
    Vtx q[4] = {vtx((float)x0, (float)y0, 0, 0, 0xFFFFFFFFu), vtx((float)x1, (float)y0, 0, 0, 0xFFFFFFFFu), vtx((float)x1, (float)y1, 0, 0, 0xFFFFFFFFu),
                vtx((float)x0, (float)y1, 0, 0, 0xFFFFFFFFu)};
    int w, h;
    target_size(w, h);
    draw_now(PROG_COLOR, nullptr, nullptr, Blend::Copy, false, false, 0, q, 4, true, w, h);
    apply_stencil(g_stencil);
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
    Texture lt;
    lt.w = 256;
    lt.h = 1;
    lt.pitch = 1024;
    lt.gpu = (uint8_t*)ring_alloc(1024, 128);
    rsxAddressToOffset(lt.gpu, &lt.offset);
    uint32_t* d = (uint32_t*)lt.gpu;
    for (int i = 0; i < 256; i++) d[i] = to_argb(lut[i]);
    g_dirty_textures = true;
    Vtx q[4] = {vtx(x0, y0, nu(tex, u0), nv(tex, v0), 0xFFFFFFFFu), vtx(x1, y0, nu(tex, u1), nv(tex, v0), 0xFFFFFFFFu),
                vtx(x1, y1, nu(tex, u1), nv(tex, v1), 0xFFFFFFFFu), vtx(x0, y1, nu(tex, u0), nv(tex, v1), 0xFFFFFFFFu)};
    int ch = std::clamp(channel, 0, 3);
    for (Vtx& v : q) v.e[ch] = 1.0f;
    int w, h;
    target_size(w, h);
    draw_now(PROG_LUT, tex, &lt, blend, false, g_wrap, write_mask(), q, 4, true, w, h);
}

}
