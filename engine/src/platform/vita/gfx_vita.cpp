#include "../gfx.h"
#include "../common/border_tiles.h"

#include <malloc.h>
#include <psp2/display.h>
#include <psp2/gxm.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../platform.h"
#include "../common/embedded_shaders.h"

namespace gfx {

constexpr int DISPLAY_W = 960;
constexpr int DISPLAY_H = 544;
constexpr int DISPLAY_STRIDE = 960;
constexpr int MAX_VTX = 4096;
constexpr size_t RING_BYTES = 8u << 20;
constexpr size_t EXTRA_BLOCK = 32u << 20;
constexpr uint32_t POOL_UNIT = 256;

struct Texture {
    int w = 0;
    int h = 0;
    int stride = 0;
    int gpu_stride = 0;
    int rows = 0;
    Format fmt = Format::RGBA8888;
    void* gpu = nullptr;
    void* pal = nullptr;
    uint32_t* cpu = nullptr;
    size_t gpu_bytes = 0;
    size_t bytes = 0;
    bool surface = false;
    SceGxmTexture gxm;
};

struct Vtx {
    float x, y, u, v;
    uint32_t color;
};

struct Pool {
    uint8_t* base = nullptr;
    uint32_t size = 0;
    uint32_t free_bytes = 0;
    std::map<uint32_t, uint32_t> free_blocks;
    std::unordered_map<uint32_t, uint32_t> used_blocks;
};

static std::vector<Pool*> g_pools;
static SceUID g_pool_lock = -1;

static uint8_t* map_block(SceKernelMemBlockType type, size_t size, size_t unit) {
    size = (size + unit - 1) / unit * unit;
    SceUID id = sceKernelAllocMemBlock("gpu", type, size, nullptr);
    if (id < 0) return nullptr;
    void* base = nullptr;
    sceKernelGetMemBlockBase(id, &base);
    if (sceGxmMapMemory(base, size, SCE_GXM_MEMORY_ATTRIB_RW) < 0) {
        sceKernelFreeMemBlock(id);
        return nullptr;
    }
    return (uint8_t*)base;
}

static Pool* pool_add(SceKernelMemBlockType type, size_t size, size_t unit) {
    uint8_t* base = map_block(type, size, unit);
    if (!base) return nullptr;
    Pool* p = new Pool();
    p->base = base;
    p->size = (uint32_t)((size + unit - 1) / unit * unit);
    p->free_bytes = p->size;
    p->free_blocks[0] = p->size;
    g_pools.push_back(p);
    return p;
}

static void* pool_take(Pool* p, uint32_t need) {
    auto best = p->free_blocks.end();
    for (auto it = p->free_blocks.begin(); it != p->free_blocks.end(); ++it) {
        if (it->second >= need && (best == p->free_blocks.end() || it->second < best->second)) {
            best = it;
            if (it->second == need) break;
        }
    }
    if (best == p->free_blocks.end()) return nullptr;
    uint32_t at = best->first, len = best->second;
    p->free_blocks.erase(best);
    if (len > need) p->free_blocks[at + need] = len - need;
    p->used_blocks[at] = need;
    p->free_bytes -= need;
    return p->base + at;
}

static void pool_give(Pool* p, uint32_t at) {
    auto u = p->used_blocks.find(at);
    if (u == p->used_blocks.end()) return;
    uint32_t len = u->second;
    p->used_blocks.erase(u);
    p->free_bytes += len;
    auto next = p->free_blocks.lower_bound(at);
    if (next != p->free_blocks.end() && next->first == at + len) {
        len += next->second;
        next = p->free_blocks.erase(next);
    }
    if (next != p->free_blocks.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == at) {
            prev->second += len;
            return;
        }
    }
    p->free_blocks[at] = len;
}

static void* gpu_take(size_t bytes) {
    uint32_t need = (uint32_t)((bytes + POOL_UNIT - 1) / POOL_UNIT * POOL_UNIT);
    if (need == 0) need = POOL_UNIT;
    sceKernelWaitSema(g_pool_lock, 1, nullptr);
    void* out = nullptr;
    for (Pool* p : g_pools) {
        if ((out = pool_take(p, need))) break;
    }
    if (!out) {
        Pool* p = pool_add(SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, std::max<size_t>(EXTRA_BLOCK, need), 4096);
        if (p) out = pool_take(p, need);
    }
    sceKernelSignalSema(g_pool_lock, 1);
    return out;
}

static void gpu_free(void* ptr) {
    if (!ptr) return;
    sceKernelWaitSema(g_pool_lock, 1, nullptr);
    for (Pool* p : g_pools) {
        if ((uint8_t*)ptr >= p->base && (uint8_t*)ptr < p->base + p->size) {
            pool_give(p, (uint32_t)((uint8_t*)ptr - p->base));
            break;
        }
    }
    sceKernelSignalSema(g_pool_lock, 1);
}

void* alloc(size_t bytes) { return memalign(64, bytes ? bytes : 16); }

void release(void* p) { std::free(p); }

size_t arena_free_bytes() {
    sceKernelWaitSema(g_pool_lock, 1, nullptr);
    size_t n = 0;
    for (Pool* p : g_pools) n += p->free_bytes;
    sceKernelSignalSema(g_pool_lock, 1);
    return n;
}

void* arena_try_alloc(size_t) { return nullptr; }

static void* uncached_block(size_t size) {
    size = (size + 4095) & ~(size_t)4095;
    SceUID id = sceKernelAllocMemBlock("gxm", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE, size, nullptr);
    if (id < 0) return nullptr;
    void* base = nullptr;
    sceKernelGetMemBlockBase(id, &base);
    return base;
}

static void* mapped_block(size_t size, SceGxmMemoryAttribFlags attr) {
    void* base = uncached_block(size);
    if (base && sceGxmMapMemory(base, (size + 4095) & ~(size_t)4095, attr) < 0) return nullptr;
    return base;
}

static void* usse_block(size_t size, bool vertex, unsigned int* offset) {
    void* base = uncached_block(size);
    if (!base) return nullptr;
    size = (size + 4095) & ~(size_t)4095;
    int r = vertex ? sceGxmMapVertexUsseMemory(base, size, offset) : sceGxmMapFragmentUsseMemory(base, size, offset);
    return r < 0 ? nullptr : base;
}

enum Program { PROG_COLOR, PROG_TEXTURE, PROG_FLAT, PROG_LUT, PROG_GRAY, PROG_PRESENT, PROG_COUNT };

static const char* const FRAGMENT_NAMES[PROG_COUNT][2] = {{"color_f", "color_at_f"},     {"texture_f", "texture_at_f"}, {"flat_f", "flat_at_f"},
                                                          {"lut_f", "lut_at_f"},         {"gray_f", "gray_f"},          {"present_f", "present_f"}};

struct FragmentShader {
    SceGxmShaderPatcherId id;
    const SceGxmProgram* program = nullptr;
    const SceGxmProgramParameter* texture = nullptr;
    const SceGxmProgramParameter* lut = nullptr;
    const SceGxmProgramParameter* channel = nullptr;
    const SceGxmProgramParameter* level = nullptr;
    const SceGxmProgramParameter* size = nullptr;
    const SceGxmProgramParameter* scale = nullptr;
};

static SceGxmContext* g_ctx = nullptr;
static SceGxmShaderPatcher* g_patcher = nullptr;
static SceGxmShaderPatcherId g_vertex_id;
static const SceGxmProgram* g_vertex_program = nullptr;
static const SceGxmProgramParameter* g_transform = nullptr;
static SceGxmVertexProgram* g_vertex = nullptr;
static FragmentShader g_frag[PROG_COUNT][2];
static std::unordered_map<uint32_t, SceGxmFragmentProgram*> g_frag_cache;
static uint16_t* g_quad_indices = nullptr;
static uint16_t* g_seq_indices = nullptr;
static uint8_t* g_ring = nullptr;
static size_t g_ring_used = 0;
static size_t g_ring_peak = 0;

struct SurfaceInfo {
    int w = 0;
    int h = 0;
    int stride = 0;
    SceGxmRenderTarget* rt = nullptr;
    SceGxmColorSurface color;
    SceGxmDepthStencilSurface depth;
};

struct DisplayBuffer {
    void* mem = nullptr;
    SceGxmColorSurface color;
    SceGxmSyncObject* sync = nullptr;
};

struct DisplayData {
    void* address;
};

static SurfaceInfo g_surface[6];
static Texture g_surface_tex[6];
static SceGxmRenderTarget* g_display_rt = nullptr;
static DisplayBuffer g_display[2];
static int g_front = 0;
static int g_back = 1;

static Vtx g_batch[MAX_VTX];
static int g_count = 0;
static bool g_quads = true;
static Texture* g_tex = nullptr;
static Blend g_blend = Blend::Normal;
static bool g_smooth = false;
static bool g_flat = false;
static int g_batches = 0;

static Surface g_target = Surface::Screen;
static bool g_in_scene = false;
static bool g_frame_pending = false;
static bool g_frame_open = false;
static uint32_t g_protect = 0;
static bool g_wrap = false;
static Stencil g_stencil = Stencil::Off;
static int g_sx0 = 0, g_sy0 = 0, g_sx1 = SCREEN_W, g_sy1 = SCREEN_H;
static std::vector<Texture*> g_garbage;

static void display_callback(const void* data) {
    const DisplayData* d = (const DisplayData*)data;
    SceDisplayFrameBuf fb{};
    fb.size = sizeof(fb);
    fb.base = d->address;
    fb.pitch = DISPLAY_STRIDE;
    fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    fb.width = DISPLAY_W;
    fb.height = DISPLAY_H;
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_SETBUF_NEXTFRAME);
    sceDisplayWaitVblankStart();
}

static void* patcher_host_alloc(void*, unsigned int size) { return std::malloc(size); }
static void patcher_host_free(void*, void* mem) { std::free(mem); }

static bool register_program(const char* name, SceGxmShaderPatcherId* id, const SceGxmProgram** program) {
    const EmbeddedShader* s = embedded_shader(name);
    if (!s || sceGxmProgramCheck((const SceGxmProgram*)s->data) < 0) {
        plat::log("gfx: shader %s missing or invalid", name);
        return false;
    }
    if (sceGxmShaderPatcherRegisterProgram(g_patcher, (const SceGxmProgram*)s->data, id) < 0) return false;
    *program = (const SceGxmProgram*)s->data;
    return true;
}

static bool setup_programs() {
    if (!register_program("sprite_v", &g_vertex_id, &g_vertex_program)) return false;
    const SceGxmProgramParameter* pos = sceGxmProgramFindParameterByName(g_vertex_program, "aPosition");
    const SceGxmProgramParameter* uv = sceGxmProgramFindParameterByName(g_vertex_program, "aTexcoord");
    const SceGxmProgramParameter* col = sceGxmProgramFindParameterByName(g_vertex_program, "aColor");
    g_transform = sceGxmProgramFindParameterByName(g_vertex_program, "uTransform");
    if (!pos || !uv || !col || !g_transform) return false;
    SceGxmVertexAttribute attrs[3];
    attrs[0].streamIndex = 0;
    attrs[0].offset = 0;
    attrs[0].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attrs[0].componentCount = 2;
    attrs[0].regIndex = (uint16_t)sceGxmProgramParameterGetResourceIndex(pos);
    attrs[1].streamIndex = 0;
    attrs[1].offset = 8;
    attrs[1].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    attrs[1].componentCount = 2;
    attrs[1].regIndex = (uint16_t)sceGxmProgramParameterGetResourceIndex(uv);
    attrs[2].streamIndex = 0;
    attrs[2].offset = 16;
    attrs[2].format = SCE_GXM_ATTRIBUTE_FORMAT_U8N;
    attrs[2].componentCount = 4;
    attrs[2].regIndex = (uint16_t)sceGxmProgramParameterGetResourceIndex(col);
    SceGxmVertexStream stream;
    stream.stride = sizeof(Vtx);
    stream.indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
    if (sceGxmShaderPatcherCreateVertexProgram(g_patcher, g_vertex_id, attrs, 3, &stream, 1, &g_vertex) < 0) return false;
    for (int p = 0; p < PROG_COUNT; p++) {
        for (int at = 0; at < 2; at++) {
            FragmentShader& f = g_frag[p][at];
            if (at && std::strcmp(FRAGMENT_NAMES[p][0], FRAGMENT_NAMES[p][1]) == 0) {
                f = g_frag[p][0];
                continue;
            }
            if (!register_program(FRAGMENT_NAMES[p][at], &f.id, &f.program)) return false;
            f.texture = sceGxmProgramFindParameterByName(f.program, "uTexture");
            f.lut = sceGxmProgramFindParameterByName(f.program, "uLut");
            f.channel = sceGxmProgramFindParameterByName(f.program, "uChannel");
            f.level = sceGxmProgramFindParameterByName(f.program, "uLevel");
            f.size = sceGxmProgramFindParameterByName(f.program, "uSize");
            f.scale = sceGxmProgramFindParameterByName(f.program, "uScale");
        }
    }
    return true;
}

static SceGxmBlendInfo blend_info(Blend b, bool flat, uint8_t mask) {
    SceGxmBlendInfo i{};
    i.colorMask = mask;
    i.colorFunc = SCE_GXM_BLEND_FUNC_ADD;
    i.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    i.colorSrc = SCE_GXM_BLEND_FACTOR_ONE;
    i.colorDst = SCE_GXM_BLEND_FACTOR_ZERO;
    if (flat) {
        i.colorSrc = SCE_GXM_BLEND_FACTOR_SRC_ALPHA;
        if (b == Blend::Add) {
            i.colorDst = SCE_GXM_BLEND_FACTOR_ONE;
        } else if (b == Blend::Subtract) {
            i.colorFunc = SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT;
            i.colorDst = SCE_GXM_BLEND_FACTOR_ONE;
        } else {
            i.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        }
    } else {
        switch (b) {
            case Blend::Normal: i.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
            case Blend::Add: i.colorDst = SCE_GXM_BLEND_FACTOR_ONE; break;
            case Blend::Multiply:
                i.colorSrc = SCE_GXM_BLEND_FACTOR_DST_COLOR;
                i.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                break;
            case Blend::Screen: i.colorDst = SCE_GXM_BLEND_FACTOR_ONE_MINUS_SRC_COLOR; break;
            case Blend::Subtract:
                i.colorFunc = SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT;
                i.colorDst = SCE_GXM_BLEND_FACTOR_ONE;
                break;
            case Blend::Copy: break;
            case Blend::MulSrc:
                i.colorSrc = SCE_GXM_BLEND_FACTOR_ZERO;
                i.colorDst = SCE_GXM_BLEND_FACTOR_SRC_COLOR;
                break;
            case Blend::MulAdd:
                i.colorSrc = SCE_GXM_BLEND_FACTOR_DST_COLOR;
                i.colorDst = SCE_GXM_BLEND_FACTOR_ONE;
                break;
            case Blend::Max: i.colorFunc = SCE_GXM_BLEND_FUNC_MAX; break;
            case Blend::Min: i.colorFunc = SCE_GXM_BLEND_FUNC_MIN; break;
            case Blend::MulSub:
                i.colorFunc = SCE_GXM_BLEND_FUNC_REVERSE_SUBTRACT;
                i.colorSrc = SCE_GXM_BLEND_FACTOR_DST_COLOR;
                i.colorDst = SCE_GXM_BLEND_FACTOR_ONE;
                break;
        }
    }
    i.alphaSrc = i.colorSrc;
    i.alphaDst = i.colorDst;
    if (i.colorFunc == SCE_GXM_BLEND_FUNC_MAX || i.colorFunc == SCE_GXM_BLEND_FUNC_MIN) {
        i.colorSrc = i.colorDst = i.alphaSrc = i.alphaDst = SCE_GXM_BLEND_FACTOR_ONE;
        i.alphaFunc = SCE_GXM_BLEND_FUNC_ADD;
    }
    return i;
}

static uint8_t write_mask() {
    uint8_t m = 0;
    if (!(g_protect & 0x0000FFu)) m |= SCE_GXM_COLOR_MASK_R;
    if (!(g_protect & 0x00FF00u)) m |= SCE_GXM_COLOR_MASK_G;
    if (!(g_protect & 0xFF0000u)) m |= SCE_GXM_COLOR_MASK_B;
    return m;
}

static SceGxmFragmentProgram* fragment(Program p, bool alpha_test, Blend b, bool flat, uint8_t mask) {
    uint32_t key = (uint32_t)p | ((uint32_t)alpha_test << 4) | ((uint32_t)b << 5) | ((uint32_t)flat << 10) | ((uint32_t)mask << 11);
    auto it = g_frag_cache.find(key);
    if (it != g_frag_cache.end()) return it->second;
    SceGxmBlendInfo info = blend_info(b, flat, mask);
    SceGxmFragmentProgram* fp = nullptr;
    if (sceGxmShaderPatcherCreateFragmentProgram(g_patcher, g_frag[p][alpha_test ? 1 : 0].id, SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4, SCE_GXM_MULTISAMPLE_NONE,
                                                 &info, g_vertex_program, &fp) < 0) {
        plat::log("gfx: fragment program %u failed", (unsigned)key);
        fp = nullptr;
    }
    g_frag_cache[key] = fp;
    return fp;
}

static void* ring_alloc(size_t bytes, size_t align);

static void apply_stencil(Stencil mode) {
    switch (mode) {
        case Stencil::Off:
            sceGxmSetFrontStencilFunc(g_ctx, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0xFF, 0x00);
            break;
        case Stencil::Write:
            sceGxmSetFrontStencilFunc(g_ctx, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_REPLACE, 0xFF,
                                      0xFF);
            sceGxmSetFrontStencilRef(g_ctx, 0xFF);
            break;
        case Stencil::Test:
            sceGxmSetFrontStencilFunc(g_ctx, SCE_GXM_STENCIL_FUNC_EQUAL, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, 0xFF, 0x00);
            sceGxmSetFrontStencilRef(g_ctx, 0xFF);
            break;
    }
}

static void set_viewport(int w, int h) {
    sceGxmSetViewport(g_ctx, w * 0.5f, w * 0.5f, h * 0.5f, -h * 0.5f, 0.5f, 0.5f);
}

static void scene_state(int w, int h) {
    set_viewport(w, h);
    sceGxmSetCullMode(g_ctx, SCE_GXM_CULL_NONE);
    sceGxmSetTwoSidedEnable(g_ctx, SCE_GXM_TWO_SIDED_DISABLED);
    sceGxmSetFrontDepthFunc(g_ctx, SCE_GXM_DEPTH_FUNC_ALWAYS);
    sceGxmSetFrontDepthWriteEnable(g_ctx, SCE_GXM_DEPTH_WRITE_DISABLED);
    sceGxmSetVertexProgram(g_ctx, g_vertex);
}

static void begin_scene(Surface s) {
    SurfaceInfo& si = g_surface[(int)s];
    sceGxmBeginScene(g_ctx, 0, si.rt, nullptr, nullptr, nullptr, &si.color, &si.depth);
    g_in_scene = true;
    scene_state(si.w, si.h);
    apply_stencil(g_stencil);
}

static void flush();

static void end_scene() {
    if (!g_in_scene) return;
    flush();
    sceGxmEndScene(g_ctx, nullptr, nullptr);
    g_in_scene = false;
}

static void collect_garbage() {
    for (Texture* t : g_garbage) {
        if (g_tex == t) g_tex = nullptr;
        gpu_free(t->gpu);
        gpu_free(t->pal);
        delete t;
    }
    g_garbage.clear();
}

static void wait_gpu() {
    sceGxmFinish(g_ctx);
    collect_garbage();
}

static bool (*g_evict)() = nullptr;
static SceUID g_main_thread = -1;
static bool g_evicting = false;

void set_evict_handler(bool (*fn)()) { g_evict = fn; }

static void* gpu_alloc(size_t bytes) {
    void* p = gpu_take(bytes);
    if (p || !g_evict || g_evicting || sceKernelGetThreadId() != g_main_thread) return p;
    g_evicting = true;
    bool was = g_in_scene;
    end_scene();
    wait_gpu();
    p = gpu_take(bytes);
    while (!p && g_evict()) {
        wait_gpu();
        p = gpu_take(bytes);
    }
    if (was) begin_scene(g_target);
    g_evicting = false;
    if (p) plat::log("low memory: freed images for %u bytes of gpu memory", (unsigned)bytes);
    return p;
}

static void* ring_alloc(size_t bytes, size_t align) {
    size_t at = (g_ring_used + align - 1) / align * align;
    if (at + bytes > RING_BYTES) {
        bool was = g_in_scene;
        Surface s = g_target;
        if (was) {
            sceGxmEndScene(g_ctx, nullptr, nullptr);
            g_in_scene = false;
        }
        sceGxmFinish(g_ctx);
        g_ring_used = 0;
        at = 0;
        if (was) begin_scene(s);
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

static SceGxmTextureFormat gxm_format(Format f) {
    switch (f) {
        case Format::RGBA8888: return SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR;
        case Format::RGBA4444: return SCE_GXM_TEXTURE_FORMAT_U4U4U4U4_ABGR;
        case Format::RGB565: return SCE_GXM_TEXTURE_FORMAT_U5U6U5_BGR;
        case Format::RGBA5551: return SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR;
        case Format::T8: return SCE_GXM_TEXTURE_FORMAT_P8_ABGR;
    }
    return SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR;
}

static bool pow2(int n) { return n >= 8 && (n & (n - 1)) == 0; }

static void init_gxm_texture(Texture* t) {
    SceGxmTextureFormat f = gxm_format(t->fmt);
    if (pow2(t->w) && pow2(t->h) && t->gpu_stride == t->w) sceGxmTextureInitLinear(&t->gxm, t->gpu, f, (unsigned)t->w, (unsigned)t->h, 0);
    else sceGxmTextureInitLinearStrided(&t->gxm, t->gpu, f, (unsigned)t->w, (unsigned)t->h, (unsigned)(t->gpu_stride * bpp_of(t->fmt)));
    if (t->pal) sceGxmTextureSetPalette(&t->gxm, t->pal);
}

static void unswizzle(const uint8_t* src, uint8_t* dst, int bw, int rows) {
    int blocks = bw / 16;
    for (int y = 0; y < rows; y++) {
        for (int bx = 0; bx < blocks; bx++) std::memcpy(dst + (size_t)y * bw + (size_t)bx * 16, src + ((size_t)(y / 8) * blocks + bx) * 128 + (y % 8) * 16, 16);
    }
}

Texture* texture_create(int w, int h, int stride, int rows, Format fmt, bool swizzled, void* pixels, uint32_t* palette) {
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    t->stride = stride;
    t->gpu_stride = stride;
    t->rows = rows;
    t->fmt = fmt;
    int bw = stride * bpp_of(fmt);
    t->gpu_bytes = (size_t)bw * rows;
    t->gpu = gpu_alloc(t->gpu_bytes);
    if (palette) t->pal = gpu_alloc(1024);
    if (!t->gpu || (palette && !t->pal)) {
        plat::log("gfx: out of texture memory for %dx%d", w, h);
        gpu_free(t->gpu);
        gpu_free(t->pal);
        release(pixels);
        release(palette);
        delete t;
        return nullptr;
    }
    if (swizzled) unswizzle((const uint8_t*)pixels, (uint8_t*)t->gpu, bw, rows);
    else std::memcpy(t->gpu, pixels, t->gpu_bytes);
    if (palette) std::memcpy(t->pal, palette, 1024);
    t->bytes = t->gpu_bytes + (palette ? 1024 : 0);
    release(pixels);
    release(palette);
    init_gxm_texture(t);
    return t;
}

Texture* texture_wrap(int w, int h, int stride, uint32_t* pixels) {
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    t->stride = stride;
    t->gpu_stride = (w + 3) & ~3;
    t->rows = h;
    t->fmt = Format::RGBA8888;
    t->cpu = pixels;
    t->gpu_bytes = (size_t)t->gpu_stride * h * 4;
    t->gpu = gpu_alloc(t->gpu_bytes);
    if (!t->gpu) {
        plat::log("gfx: out of texture memory for %dx%d", w, h);
        delete t;
        return nullptr;
    }
    texture_touch(t);
    init_gxm_texture(t);
    return t;
}

void texture_touch(Texture* t) {
    if (!t || !t->cpu || !t->gpu) return;
    uint32_t* dst = (uint32_t*)t->gpu;
    if (t->gpu_stride == t->stride) {
        std::memcpy(dst, t->cpu, (size_t)t->stride * t->h * 4);
        return;
    }
    for (int y = 0; y < t->h; y++) std::memcpy(dst + (size_t)y * t->gpu_stride, t->cpu + (size_t)y * t->stride, (size_t)t->w * 4);
}

bool texture_read(const Texture* t, int x, int y, uint32_t* out) {
    if (!t || x < 0 || y < 0 || x >= t->w || y >= t->h) return false;
    if (t->cpu) {
        *out = t->cpu[(size_t)y * t->stride + x];
        return true;
    }
    if (t->surface && g_frame_pending && !g_in_scene) wait_gpu();
    const uint8_t* p = (const uint8_t*)t->gpu;
    switch (t->fmt) {
        case Format::RGBA8888: {
            const uint8_t* q = p + ((size_t)y * t->gpu_stride + x) * 4;
            *out = (uint32_t)q[0] | ((uint32_t)q[1] << 8) | ((uint32_t)q[2] << 16) | ((uint32_t)q[3] << 24);
            return true;
        }
        case Format::T8:
            if (!t->pal) return false;
            *out = ((const uint32_t*)t->pal)[p[(size_t)y * t->gpu_stride + x]];
            return true;
        default: return false;
    }
}

void texture_read_rect(const Texture* t, int x, int y, int w, int h, uint32_t* out, size_t stride) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) texture_read(t, x + i, y + j, out + (size_t)j * stride + i);
    }
}

void texture_destroy(Texture* t) {
    if (!t) return;
    g_garbage.push_back(t);
}

size_t texture_bytes(const Texture* t) { return t ? t->bytes : 0; }
int texture_width(const Texture* t) { return t ? t->w : 0; }
int texture_height(const Texture* t) { return t ? t->h : 0; }

static SceGxmRenderTarget* render_target(int w, int h) {
    SceGxmRenderTargetParams p{};
    p.flags = 0;
    p.width = (uint16_t)w;
    p.height = (uint16_t)h;
    p.scenesPerFrame = 8;
    p.multisampleMode = SCE_GXM_MULTISAMPLE_NONE;
    p.multisampleLocations = 0;
    p.driverMemBlock = -1;
    SceGxmRenderTarget* rt = nullptr;
    if (sceGxmCreateRenderTarget(&p, &rt) < 0) return nullptr;
    return rt;
}

static bool setup_surface(Surface s, int w, int h, SceGxmRenderTarget* rt) {
    SurfaceInfo& si = g_surface[(int)s];
    si.w = w;
    si.h = h;
    si.stride = (w + 63) & ~63;
    si.rt = rt;
    size_t color_bytes = (size_t)si.stride * h * 4;
    void* color = gpu_alloc(color_bytes);
    int ds_stride = (w + 31) & ~31;
    size_t ds_bytes = (size_t)ds_stride * ((h + 31) & ~31) * 4;
    void* ds = gpu_alloc(ds_bytes);
    if (!color || !ds) return false;
    uint32_t* px = (uint32_t*)color;
    for (size_t i = 0; i < color_bytes / 4; i++) px[i] = 0xFF000000u;
    std::memset(ds, 0, ds_bytes);
    sceGxmColorSurfaceInit(&si.color, SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                           SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, (unsigned)w, (unsigned)h, (unsigned)si.stride, color);
    sceGxmDepthStencilSurfaceInit(&si.depth, SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24, SCE_GXM_DEPTH_STENCIL_SURFACE_TILED, (unsigned)ds_stride, ds, nullptr);
    sceGxmDepthStencilSurfaceSetForceLoadMode(&si.depth, SCE_GXM_DEPTH_STENCIL_FORCE_LOAD_ENABLED);
    sceGxmDepthStencilSurfaceSetForceStoreMode(&si.depth, SCE_GXM_DEPTH_STENCIL_FORCE_STORE_ENABLED);
    Texture& t = g_surface_tex[(int)s];
    t.w = w;
    t.h = h;
    t.stride = si.stride;
    t.gpu_stride = si.stride;
    t.rows = h;
    t.fmt = Format::RGBA8888;
    t.gpu = color;
    t.surface = true;
    sceGxmTextureInitLinearStrided(&t.gxm, color, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, (unsigned)w, (unsigned)h, (unsigned)(si.stride * 4));
    return true;
}

void init() {
    g_main_thread = sceKernelGetThreadId();
    g_pool_lock = sceKernelCreateSema("gpupool", 0, 1, 1, nullptr);
    for (size_t mb = 112; mb >= 32; mb -= 16) {
        if (pool_add(SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, mb << 20, 256 * 1024)) break;
    }
    SceGxmInitializeParams ip{};
    ip.flags = 0;
    ip.displayQueueMaxPendingCount = 2;
    ip.displayQueueCallback = display_callback;
    ip.displayQueueCallbackDataSize = sizeof(DisplayData);
    ip.parameterBufferSize = SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE;
    if (sceGxmInitialize(&ip) < 0) {
        plat::log("gfx: sceGxmInitialize failed");
        return;
    }
    SceGxmContextParams cp{};
    cp.hostMem = std::malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
    cp.hostMemSize = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    cp.vdmRingBufferMem = mapped_block(SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE, SCE_GXM_MEMORY_ATTRIB_READ);
    cp.vdmRingBufferMemSize = SCE_GXM_DEFAULT_VDM_RING_BUFFER_SIZE;
    cp.vertexRingBufferMem = mapped_block(SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE, SCE_GXM_MEMORY_ATTRIB_READ);
    cp.vertexRingBufferMemSize = SCE_GXM_DEFAULT_VERTEX_RING_BUFFER_SIZE;
    cp.fragmentRingBufferMem = mapped_block(SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE, SCE_GXM_MEMORY_ATTRIB_READ);
    cp.fragmentRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_RING_BUFFER_SIZE;
    unsigned int usse_offset = 0;
    cp.fragmentUsseRingBufferMem = usse_block(SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE, false, &usse_offset);
    cp.fragmentUsseRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    cp.fragmentUsseRingBufferOffset = usse_offset;
    if (sceGxmCreateContext(&cp, &g_ctx) < 0) {
        plat::log("gfx: sceGxmCreateContext failed");
        return;
    }
    const size_t patcher_bytes = 64 * 1024, usse_bytes = 64 * 1024;
    SceGxmShaderPatcherParams pp{};
    pp.hostAllocCallback = patcher_host_alloc;
    pp.hostFreeCallback = patcher_host_free;
    pp.bufferMem = mapped_block(patcher_bytes, SCE_GXM_MEMORY_ATTRIB_READ);
    pp.bufferMemSize = patcher_bytes;
    pp.vertexUsseMem = usse_block(usse_bytes, true, &pp.vertexUsseOffset);
    pp.vertexUsseMemSize = usse_bytes;
    pp.fragmentUsseMem = usse_block(usse_bytes, false, &pp.fragmentUsseOffset);
    pp.fragmentUsseMemSize = usse_bytes;
    if (sceGxmShaderPatcherCreate(&pp, &g_patcher) < 0 || !setup_programs()) {
        plat::log("gfx: shader setup failed");
        return;
    }
    g_ring = (uint8_t*)mapped_block(RING_BYTES, SCE_GXM_MEMORY_ATTRIB_READ);
    g_quad_indices = (uint16_t*)mapped_block(MAX_VTX / 4 * 6 * 2, SCE_GXM_MEMORY_ATTRIB_READ);
    g_seq_indices = (uint16_t*)mapped_block(MAX_VTX * 2, SCE_GXM_MEMORY_ATTRIB_READ);
    for (int q = 0; q < MAX_VTX / 4; q++) {
        uint16_t b = (uint16_t)(q * 4);
        uint16_t* d = g_quad_indices + q * 6;
        d[0] = b;
        d[1] = (uint16_t)(b + 1);
        d[2] = (uint16_t)(b + 2);
        d[3] = (uint16_t)(b + 1);
        d[4] = (uint16_t)(b + 3);
        d[5] = (uint16_t)(b + 2);
    }
    for (int i = 0; i < MAX_VTX; i++) g_seq_indices[i] = (uint16_t)i;
    SceGxmRenderTarget* full = render_target(SCREEN_W, SCREEN_H);
    SceGxmRenderTarget* half = render_target(HALF_W, HALF_H);
    setup_surface(Surface::Screen, SCREEN_W, SCREEN_H, full);
    setup_surface(Surface::Grab, SCREEN_W, SCREEN_H, full);
    setup_surface(Surface::Half0, HALF_W, HALF_H, half);
    setup_surface(Surface::Half1, HALF_W, HALF_H, half);
    setup_surface(Surface::Half2, HALF_W, HALF_H, half);
    setup_surface(Surface::Wide, SCREEN_W, SCREEN_H, full);
    g_display_rt = render_target(DISPLAY_W, DISPLAY_H);
    for (auto& d : g_display) {
        size_t bytes = (size_t)DISPLAY_STRIDE * DISPLAY_H * 4;
        d.mem = gpu_alloc(bytes);
        if (d.mem) {
            uint32_t* px = (uint32_t*)d.mem;
            for (size_t i = 0; i < bytes / 4; i++) px[i] = 0xFF000000u;
        }
        sceGxmColorSurfaceInit(&d.color, SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
                               SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, DISPLAY_W, DISPLAY_H, DISPLAY_STRIDE, d.mem);
        sceGxmSyncObjectCreate(&d.sync);
    }
    DisplayData first{g_display[g_front].mem};
    display_callback(&first);
}

static void draw_now(Program prog, Texture* tex, Texture* tex2, Blend blend, bool smooth, bool repeat, uint8_t mask, bool alpha_test, const Vtx* v, int n,
                     bool quads, const float* uniforms) {
    if (n <= 0) return;
    SceGxmFragmentProgram* fp = fragment(prog, alpha_test, blend, prog == PROG_FLAT, mask);
    if (!fp) return;
    g_batches++;
    sceGxmSetVertexProgram(g_ctx, g_vertex);
    sceGxmSetFragmentProgram(g_ctx, fp);
    SurfaceInfo* si = g_in_scene ? &g_surface[(int)g_target] : nullptr;
    float w = si ? (float)si->w : (float)DISPLAY_W, h = si ? (float)si->h : (float)DISPLAY_H;
    void* vub = nullptr;
    sceGxmReserveVertexDefaultUniformBuffer(g_ctx, &vub);
    const float xf[4] = {2.0f / w, -2.0f / h, -1.0f, 1.0f};
    sceGxmSetUniformDataF(vub, g_transform, 0, 4, xf);
    const FragmentShader& fs = g_frag[prog][alpha_test ? 1 : 0];
    if (fs.channel || fs.level || fs.size || fs.scale) {
        void* fub = nullptr;
        sceGxmReserveFragmentDefaultUniformBuffer(g_ctx, &fub);
        if (fs.channel) sceGxmSetUniformDataF(fub, fs.channel, 0, 4, uniforms);
        if (fs.level) sceGxmSetUniformDataF(fub, fs.level, 0, 1, uniforms);
        if (fs.size) sceGxmSetUniformDataF(fub, fs.size, 0, 4, uniforms);
        if (fs.scale) sceGxmSetUniformDataF(fub, fs.scale, 0, 1, uniforms + 4);
    }
    if (tex && fs.texture) {
        SceGxmTexture t = tex->gxm;
        SceGxmTextureFilter f = smooth ? SCE_GXM_TEXTURE_FILTER_LINEAR : SCE_GXM_TEXTURE_FILTER_POINT;
        sceGxmTextureSetMinFilter(&t, f);
        sceGxmTextureSetMagFilter(&t, f);
        SceGxmTextureAddrMode a = repeat && sceGxmTextureGetType(&t) == SCE_GXM_TEXTURE_LINEAR ? SCE_GXM_TEXTURE_ADDR_REPEAT : SCE_GXM_TEXTURE_ADDR_CLAMP;
        sceGxmTextureSetUAddrMode(&t, a);
        sceGxmTextureSetVAddrMode(&t, a);
        sceGxmSetFragmentTexture(g_ctx, sceGxmProgramParameterGetResourceIndex(fs.texture), &t);
    }
    if (tex2 && fs.lut) {
        SceGxmTexture t = tex2->gxm;
        sceGxmTextureSetMinFilter(&t, SCE_GXM_TEXTURE_FILTER_POINT);
        sceGxmTextureSetMagFilter(&t, SCE_GXM_TEXTURE_FILTER_POINT);
        sceGxmTextureSetUAddrMode(&t, SCE_GXM_TEXTURE_ADDR_CLAMP);
        sceGxmTextureSetVAddrMode(&t, SCE_GXM_TEXTURE_ADDR_CLAMP);
        sceGxmSetFragmentTexture(g_ctx, sceGxmProgramParameterGetResourceIndex(fs.lut), &t);
    }
    void* mem = ring_alloc((size_t)n * sizeof(Vtx), 16);
    std::memcpy(mem, v, (size_t)n * sizeof(Vtx));
    sceGxmSetVertexStream(g_ctx, 0, mem);
    if (quads) sceGxmDraw(g_ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, g_quad_indices, (unsigned)(n / 4 * 6));
    else sceGxmDraw(g_ctx, SCE_GXM_PRIMITIVE_TRIANGLES, SCE_GXM_INDEX_FORMAT_U16, g_seq_indices, (unsigned)n);
}

static void flush() {
    if (!g_count) return;
    int n = g_count;
    g_count = 0;
    if (!g_in_scene) return;
    Program p = g_tex ? (g_flat ? PROG_FLAT : PROG_TEXTURE) : PROG_COLOR;
    draw_now(p, g_tex, nullptr, g_blend, g_smooth, g_wrap, write_mask(), g_stencil == Stencil::Write, g_batch, n, g_quads, nullptr);
}

static void prepare(Texture* tex, Blend blend, bool smooth, bool quads, int verts, bool flat) {
    if (g_count && (g_tex != tex || g_blend != blend || g_smooth != smooth || g_quads != quads || g_flat != flat || g_count + verts > MAX_VTX)) flush();
    g_tex = tex;
    g_blend = blend;
    g_smooth = smooth;
    g_quads = quads;
    g_flat = flat;
}

static bool scissor_full() {
    const SurfaceInfo& si = g_surface[(int)g_target];
    return g_sx0 <= 0 && g_sy0 <= 0 && g_sx1 >= si.w && g_sy1 >= si.h;
}

static bool scissor_empty() { return g_sx1 <= g_sx0 || g_sy1 <= g_sy0; }

static bool clip_rect(float& x0, float& y0, float& x1, float& y1, float& u0, float& v0, float& u1, float& v1) {
    if (scissor_empty()) return false;
    if (scissor_full()) return x1 > x0 && y1 > y0;
    float cx0 = std::max(x0, (float)g_sx0), cy0 = std::max(y0, (float)g_sy0);
    float cx1 = std::min(x1, (float)g_sx1), cy1 = std::min(y1, (float)g_sy1);
    if (cx1 <= cx0 || cy1 <= cy0) return false;
    float du = (u1 - u0) / (x1 - x0), dv = (v1 - v0) / (y1 - y0);
    float nu0 = u0 + (cx0 - x0) * du, nu1 = u0 + (cx1 - x0) * du;
    float nv0 = v0 + (cy0 - y0) * dv, nv1 = v0 + (cy1 - y0) * dv;
    x0 = cx0;
    y0 = cy0;
    x1 = cx1;
    y1 = cy1;
    u0 = nu0;
    u1 = nu1;
    v0 = nv0;
    v1 = nv1;
    return true;
}

static void push_rect(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t color) {
    Vtx* v = &g_batch[g_count];
    v[0] = {x0, y0, u0, v0, color};
    v[1] = {x1, y0, u1, v0, color};
    v[2] = {x0, y1, u0, v1, color};
    v[3] = {x1, y1, u1, v1, color};
    g_count += 4;
}

static Vtx lerp_vtx(const Vtx& a, const Vtx& b, float t) {
    Vtx o;
    o.x = a.x + (b.x - a.x) * t;
    o.y = a.y + (b.y - a.y) * t;
    o.u = a.u + (b.u - a.u) * t;
    o.v = a.v + (b.v - a.v) * t;
    uint32_t c = 0;
    for (int s = 0; s < 32; s += 8) {
        float ca = (float)((a.color >> s) & 255), cb = (float)((b.color >> s) & 255);
        int k = (int)std::lround(ca + (cb - ca) * t);
        c |= (uint32_t)std::clamp(k, 0, 255) << s;
    }
    o.color = c;
    return o;
}

static int clip_edge(const Vtx* in, int n, Vtx* out, int axis, float limit, bool keep_greater) {
    int m = 0;
    for (int i = 0; i < n; i++) {
        const Vtx& a = in[i];
        const Vtx& b = in[(i + 1) % n];
        float da = (axis == 0 ? a.x : a.y) - limit, db = (axis == 0 ? b.x : b.y) - limit;
        if (!keep_greater) {
            da = -da;
            db = -db;
        }
        bool ia = da >= 0, ib = db >= 0;
        if (ia) out[m++] = a;
        if (ia != ib) out[m++] = lerp_vtx(a, b, da / (da - db));
    }
    return m;
}

static void push_triangle(const Vtx& a, const Vtx& b, const Vtx& c) {
    if (scissor_full()) {
        if (g_count + 3 > MAX_VTX) flush();
        g_batch[g_count++] = a;
        g_batch[g_count++] = b;
        g_batch[g_count++] = c;
        return;
    }
    if (scissor_empty()) return;
    Vtx p0[9] = {a, b, c}, p1[9];
    int n = clip_edge(p0, 3, p1, 0, (float)g_sx0, true);
    n = clip_edge(p1, n, p0, 0, (float)g_sx1, false);
    n = clip_edge(p0, n, p1, 1, (float)g_sy0, true);
    n = clip_edge(p1, n, p0, 1, (float)g_sy1, false);
    for (int i = 1; i + 1 < n; i++) {
        if (g_count + 3 > MAX_VTX) flush();
        g_batch[g_count++] = p0[0];
        g_batch[g_count++] = p0[i];
        g_batch[g_count++] = p0[i + 1];
    }
}

static inline float nu(const Texture* t, float u) { return t ? u / (float)t->w : 0.0f; }
static inline float nv(const Texture* t, float v) { return t ? v / (float)t->h : 0.0f; }

static std::vector<Texture*> g_border;
static BorderTiles g_border_tiles;
static std::vector<BorderRect> g_border_bars, g_border_frame;
static uint32_t g_border_line = 0;
static float g_border_scroll = 0;

void border_canvas(float& x0, float& y0, float& x1, float& y1) {
    float scale = (float)DISPLAY_H / SCREEN_H;
    float left = (DISPLAY_W - SCREEN_W * scale) * 0.5f;
    x0 = -left / scale;
    y0 = 0;
    x1 = (DISPLAY_W - left) / scale;
    y1 = (float)SCREEN_H;
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

static void present_border(float x0, float scale, uint8_t mask) {
    BorderSpan spans[BORDER_SPANS];
    for (const BorderRect& b : g_border_bars) {
        float v0 = (float)(b.y0 - g_border_tiles.y0), v1 = v0 + (float)(b.y1 - b.y0), y0 = b.y0 * scale, y1 = b.y1 * scale;
        int n = border_spans(g_border_tiles, b, g_border_scroll, spans);
        for (int i = 0; i < n; i++) {
            const BorderSpan& p = spans[i];
            Texture* t = g_border[p.chunk];
            const float bu[5] = {(float)t->w, (float)t->h, 1.0f / t->w, 1.0f / t->h, scale};
            float px0 = x0 + p.x0 * scale, px1 = x0 + p.x1 * scale;
            float u0 = bu[2] * p.u, u1 = bu[2] * (p.u + p.x1 - p.x0), tv0 = bu[3] * v0, tv1 = bu[3] * v1;
            Vtx q[4] = {{px0, y0, u0, tv0, 0xFFFFFFFFu}, {px1, y0, u1, tv0, 0xFFFFFFFFu}, {px0, y1, u0, tv1, 0xFFFFFFFFu}, {px1, y1, u1, tv1, 0xFFFFFFFFu}};
            draw_now(PROG_PRESENT, t, nullptr, Blend::Copy, true, false, mask, false, q, 4, true, bu);
        }
    }
    for (const BorderRect& f : g_border_frame) {
        float fx0 = x0 + f.x0 * scale, fx1 = x0 + f.x1 * scale, fy0 = f.y0 * scale, fy1 = f.y1 * scale;
        Vtx q[4] = {{fx0, fy0, 0, 0, g_border_line}, {fx1, fy0, 0, 0, g_border_line}, {fx0, fy1, 0, 0, g_border_line}, {fx1, fy1, 0, 0, g_border_line}};
        draw_now(PROG_COLOR, nullptr, nullptr, Blend::Copy, false, false, mask, false, q, 4, true, nullptr);
    }
}

static void present_frame() {
    DisplayBuffer& d = g_display[g_back];
    sceGxmBeginScene(g_ctx, 0, g_display_rt, nullptr, nullptr, d.sync, &d.color, nullptr);
    scene_state(DISPLAY_W, DISPLAY_H);
    apply_stencil(Stencil::Off);
    const uint8_t rgb = SCE_GXM_COLOR_MASK_R | SCE_GXM_COLOR_MASK_G | SCE_GXM_COLOR_MASK_B;
    float scale = (float)DISPLAY_H / SCREEN_H;
    float w = SCREEN_W * scale, x0 = (DISPLAY_W - w) * 0.5f;
    if (!g_border.empty()) {
        present_border(x0, scale, rgb | SCE_GXM_COLOR_MASK_A);
    } else {
        Vtx bg[4] = {{0, 0, 0, 0, 0xFF000000u}, {DISPLAY_W, 0, 0, 0, 0xFF000000u}, {0, DISPLAY_H, 0, 0, 0xFF000000u}, {DISPLAY_W, DISPLAY_H, 0, 0, 0xFF000000u}};
        draw_now(PROG_COLOR, nullptr, nullptr, Blend::Copy, false, false, rgb | SCE_GXM_COLOR_MASK_A, false, bg, 4, true, nullptr);
    }
    Vtx q[4] = {{x0, 0, 0, 0, 0xFFFFFFFFu}, {x0 + w, 0, 1, 0, 0xFFFFFFFFu}, {x0, DISPLAY_H, 0, 1, 0xFFFFFFFFu}, {x0 + w, DISPLAY_H, 1, 1, 0xFFFFFFFFu}};
    const float u[5] = {(float)SCREEN_W, (float)SCREEN_H, 1.0f / SCREEN_W, 1.0f / SCREEN_H, scale};
    draw_now(PROG_PRESENT, &g_surface_tex[(int)Surface::Screen], nullptr, Blend::Copy, true, false, rgb, false, q, 4, true, u);
    sceGxmEndScene(g_ctx, nullptr, nullptr);
    sceGxmPadHeartbeat(&d.color, d.sync);
    DisplayData data{d.mem};
    sceGxmDisplayQueueAddEntry(g_display[g_front].sync, d.sync, &data);
    g_front = g_back;
    g_back ^= 1;
}

void present() {
    if (!g_frame_pending || !g_ctx) return;
    present_frame();
    g_frame_pending = false;
}

void shutdown() {
    if (!g_ctx) return;
    present();
    sceGxmDisplayQueueFinish();
    sceGxmFinish(g_ctx);
}

void begin_frame(uint32_t clear_color) {
    if (!g_ctx) return;
    present();
    wait_gpu();
    g_ring_used = 0;
    g_count = 0;
    g_tex = nullptr;
    g_target = Surface::Screen;
    g_protect = 0;
    g_wrap = false;
    g_stencil = Stencil::Off;
    begin_scene(Surface::Screen);
    g_frame_open = true;
    reset_scissor();
    fill(0, 0, SCREEN_W, SCREEN_H, clear_color | 0xFF000000u, Blend::Copy);
}

void end_frame() {
    if (!g_frame_open) return;
    flush();
    if (g_target != Surface::Screen) set_target(Surface::Screen);
    if (g_stencil != Stencil::Off) set_stencil(Stencil::Off);
    if (g_protect) set_color_mask(0);
    end_scene();
    g_frame_open = false;
    g_frame_pending = true;
}

int list_peak_bytes() { return (int)g_ring_peak; }

bool reclaim_memory() {
    if (g_in_scene) return false;
    wait_gpu();
    return true;
}

double take_batch_count() {
    double n = g_batches;
    g_batches = 0;
    return n;
}

const uint32_t* framebuffer() { return nullptr; }

static const uint32_t* screen_pixels() {
    if (g_in_scene) return nullptr;
    sceGxmFinish(g_ctx);
    return (const uint32_t*)g_surface_tex[(int)Surface::Screen].gpu;
}

bool capture_row(int y, int x, int w, uint32_t* out) {
    if (y < 0 || y >= SCREEN_H || x < 0 || x + w > SCREEN_W) return false;
    const uint32_t* px = screen_pixels();
    if (!px) return false;
    std::memcpy(out, px + (size_t)y * g_surface[(int)Surface::Screen].stride + x, (size_t)w * 4);
    return true;
}

bool capture(uint32_t* out, int out_stride) {
    const uint32_t* px = screen_pixels();
    if (!px) return false;
    int stride = g_surface[(int)Surface::Screen].stride;
    for (int y = 0; y < SCREEN_H; y++) std::memcpy(out + (size_t)y * out_stride, px + (size_t)y * stride, SCREEN_W * 4);
    return true;
}

void set_scissor(int x, int y, int w, int h) {
    flush();
    const SurfaceInfo& si = g_surface[(int)g_target];
    g_sx0 = std::max(0, x);
    g_sy0 = std::max(0, y);
    g_sx1 = std::min(si.w, x + w);
    g_sy1 = std::min(si.h, y + h);
}

void reset_scissor() {
    flush();
    g_sx0 = 0;
    g_sy0 = 0;
    g_sx1 = g_surface[(int)g_target].w;
    g_sy1 = g_surface[(int)g_target].h;
}

void sprite(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    if (!clip_rect(x0, y0, x1, y1, u0, v0, u1, v1)) return;
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
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    if (!clip_rect(x0, y0, x1, y1, u0, v0, u1, v1)) return;
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
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    if (!clip_rect(x0, y0, x1, y1, u0, v0, u1, v1)) return;
    prepare(nullptr, blend, false, true, 4, false);
    push_rect(x0, y0, x1, y1, 0, 0, 0, 0, color);
}

static void copy_screen_to_grab() {
    Surface cur = g_target;
    end_scene();
    g_target = Surface::Grab;
    begin_scene(Surface::Grab);
    apply_stencil(Stencil::Off);
    Vtx q[4] = {{0, 0, 0, 0, 0xFFFFFFFFu}, {SCREEN_W, 0, 1, 0, 0xFFFFFFFFu}, {0, SCREEN_H, 0, 1, 0xFFFFFFFFu}, {SCREEN_W, SCREEN_H, 1, 1, 0xFFFFFFFFu}};
    draw_now(PROG_TEXTURE, &g_surface_tex[(int)Surface::Screen], nullptr, Blend::Copy, false, false,
             SCE_GXM_COLOR_MASK_R | SCE_GXM_COLOR_MASK_G | SCE_GXM_COLOR_MASK_B, false, q, 4, true, nullptr);
    sceGxmEndScene(g_ctx, nullptr, nullptr);
    g_in_scene = false;
    g_target = cur;
    begin_scene(cur);
}

void desaturate(float amount) {
    if (amount <= 0 || !g_in_scene) return;
    if (amount > 1) amount = 1;
    flush();
    int level = (int)(amount * 255.0f + 0.5f);
    copy_screen_to_grab();
    float x0 = 0, y0 = 0, x1 = SCREEN_W, y1 = SCREEN_H, u0 = 0, v0 = 0, u1 = SCREEN_W, v1 = SCREEN_H;
    if (!clip_rect(x0, y0, x1, y1, u0, v0, u1, v1)) return;
    Texture* g = &g_surface_tex[(int)Surface::Grab];
    Vtx q[4] = {{x0, y0, nu(g, u0), nv(g, v0), 0xFFFFFFFFu}, {x1, y0, nu(g, u1), nv(g, v0), 0xFFFFFFFFu},
                {x0, y1, nu(g, u0), nv(g, v1), 0xFFFFFFFFu}, {x1, y1, nu(g, u1), nv(g, v1), 0xFFFFFFFFu}};
    const float u[1] = {level / 255.0f};
    draw_now(PROG_GRAY, g, nullptr, Blend::Copy, false, false, write_mask(), false, q, 4, true, u);
}

Texture* grab_screen() {
    flush();
    if (g_in_scene) copy_screen_to_grab();
    return &g_surface_tex[(int)Surface::Grab];
}

void set_target(Surface s) {
    flush();
    if (g_in_scene) end_scene();
    g_target = s;
    begin_scene(s);
    g_sx0 = 0;
    g_sy0 = 0;
    g_sx1 = g_surface[(int)s].w;
    g_sy1 = g_surface[(int)s].h;
}

Surface current_target() { return g_target; }

Texture* surface_texture(Surface s) { return &g_surface_tex[(int)s]; }
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
    if (g_in_scene) apply_stencil(mode);
}

void stencil_clear(int x0, int y0, int x1, int y1) {
    if (x1 <= x0 || y1 <= y0 || !g_in_scene) return;
    flush();
    float fx0 = (float)x0, fy0 = (float)y0, fx1 = (float)x1, fy1 = (float)y1, u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    if (!clip_rect(fx0, fy0, fx1, fy1, u0, v0, u1, v1)) return;
    sceGxmSetFrontStencilFunc(g_ctx, SCE_GXM_STENCIL_FUNC_ALWAYS, SCE_GXM_STENCIL_OP_REPLACE, SCE_GXM_STENCIL_OP_REPLACE, SCE_GXM_STENCIL_OP_REPLACE, 0xFF,
                              0xFF);
    sceGxmSetFrontStencilRef(g_ctx, 0);
    Vtx q[4] = {{fx0, fy0, 0, 0, 0xFFFFFFFFu}, {fx1, fy0, 0, 0, 0xFFFFFFFFu}, {fx0, fy1, 0, 0, 0xFFFFFFFFu}, {fx1, fy1, 0, 0, 0xFFFFFFFFu}};
    draw_now(PROG_COLOR, nullptr, nullptr, Blend::Copy, false, false, SCE_GXM_COLOR_MASK_NONE, false, q, 4, true, nullptr);
    apply_stencil(g_stencil);
}

void draw_triangles(Texture* tex, const Vertex* v, int count, Blend blend, bool smooth) {
    count -= count % 3;
    for (int i = 0; i < count; i += 3) {
        prepare(tex, blend, smooth, false, 3, false);
        Vtx a = {v[i].x, v[i].y, nu(tex, v[i].u), nv(tex, v[i].v), v[i].color};
        Vtx b = {v[i + 1].x, v[i + 1].y, nu(tex, v[i + 1].u), nv(tex, v[i + 1].v), v[i + 1].color};
        Vtx c = {v[i + 2].x, v[i + 2].y, nu(tex, v[i + 2].u), nv(tex, v[i + 2].v), v[i + 2].color};
        push_triangle(a, b, c);
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
    if (!tex || !lut || tex->fmt != Format::RGBA8888 || !g_in_scene) return;
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    flush();
    if (!clip_rect(x0, y0, x1, y1, u0, v0, u1, v1)) return;
    Texture lt;
    lt.w = 256;
    lt.h = 1;
    lt.stride = 256;
    lt.rows = 1;
    lt.gpu = ring_alloc(1024, 64);
    std::memcpy(lt.gpu, lut, 1024);
    sceGxmTextureInitLinearStrided(&lt.gxm, lt.gpu, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR, 256, 1, 1024);
    float ch[4] = {0, 0, 0, 0};
    ch[std::clamp(channel, 0, 3)] = 1.0f;
    Vtx q[4] = {{x0, y0, nu(tex, u0), nv(tex, v0), 0xFFFFFFFFu}, {x1, y0, nu(tex, u1), nv(tex, v0), 0xFFFFFFFFu},
                {x0, y1, nu(tex, u0), nv(tex, v1), 0xFFFFFFFFu}, {x1, y1, nu(tex, u1), nv(tex, v1), 0xFFFFFFFFu}};
    draw_now(PROG_LUT, tex, &lt, blend, false, g_wrap, write_mask(), g_stencil == Stencil::Write, q, 4, true, ch);
}

}
