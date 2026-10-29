#include "../gfx.h"
#include "ps2.h"

#include <dma.h>
#include <dma_tags.h>
#include <gif_tags.h>
#include <graph.h>
#include <gs_gp.h>
#include <gs_privileged.h>
#include <gs_psm.h>
#include <kernel.h>
#include <malloc.h>
#include <screenshot.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace ps2 {

static volatile uint32_t g_vcount = 0;
static int g_hz = 60;

uint32_t vsync_count() { return g_vcount; }
int refresh_hz() { return g_hz; }

}

namespace gfx {

constexpr int PAGES = 512;
constexpr int FULL_FBW = (SCREEN_W + 63) / 64;
constexpr int FULL_PAGES = FULL_FBW * ((SCREEN_H + 31) / 32);
constexpr int HALF_FBW = (HALF_W + 63) / 64;
constexpr int HALF_PAGES = HALF_FBW * ((HALF_H + 31) / 32);
constexpr int P_SCREEN0 = 0;
constexpr int P_SCREEN1 = P_SCREEN0 + FULL_PAGES;
constexpr int P_GRAB = P_SCREEN1 + FULL_PAGES;
constexpr int P_WIDE = P_GRAB + FULL_PAGES;
constexpr int P_TEMP = P_WIDE + FULL_PAGES;
constexpr int P_TEMP2 = P_TEMP + FULL_PAGES;
constexpr int P_HALF0 = P_TEMP2 + FULL_PAGES;
constexpr int P_ZBUF = P_HALF0 + 3 * HALF_PAGES;
constexpr int P_CLUT_RAMP = P_ZBUF + FULL_PAGES;
constexpr int P_CLUT_WORK = P_CLUT_RAMP + 1;
constexpr int P_CLUT_FLAT = P_CLUT_WORK + 1;
constexpr int P_TEX = P_CLUT_FLAT + 1;
constexpr int CHAIN_QW = 8192;
constexpr int OFS = 2048 * 16;
constexpr int BLOCKS_X = (SCREEN_W + 7) / 8;
constexpr int BAND_SPRITES = BLOCKS_X * 6;
constexpr size_t STAGE_BYTES = 384u << 10;
constexpr uint64_t Q_ONE = (uint64_t)0x3F800000u << 32;
constexpr uint32_t ALPHA_MASK = 0xFF000000u;
constexpr uint64_t CSR_FINISH = 2;
constexpr int TFX_MODULATE = 0;
constexpr int TFX_DECAL = 1;

static_assert(P_TEX + 160 <= PAGES, "VRAM layout leaves too little room for textures");

enum Origin : uint8_t { OWN, BORROWED, SURFACE };

struct Texture {
    int w = 0;
    int h = 0;
    int stride = 0;
    int rows = 0;
    int psm = GS_PSM_32;
    int tbw = 1;
    int tw = 0;
    int th = 0;
    int pages = 0;
    uint8_t* pixels = nullptr;
    uint32_t* clut = nullptr;
    size_t bytes = 0;
    int vram = -1;
    uint32_t used = 0;
    bool flat = false;
    bool opaque = false;
    Origin origin = OWN;
    int surface = -1;
    uint32_t* cpu = nullptr;
    int cpu_stride = 0;
};

enum Kind : uint8_t { K_NONE, K_TSPR, K_FSPR, K_TTRI, K_FTRI };

struct Regs {
    uint64_t frame, zbuf, test, alpha, scissor, xyoffset, tex0, tex1, clamp, prim;
};

struct Box {
    int x0, y0, x1, y1;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
};

struct PVert {
    float x, y, u, v;
    uint32_t color;
};

static uint64_t g_chain[2][CHAIN_QW * 2] __attribute__((aligned(64)));
static int g_cur = 0;
static uint64_t* g_q = nullptr;
static uint64_t* g_qbase = nullptr;
static uint64_t* g_qend = nullptr;
static uint64_t* g_cnt = nullptr;
static int g_peak_qw = 0;
static uint32_t g_gs_serial = 0;

static uint64_t* g_tag = nullptr;
static int g_tag_count = 0;
static int g_tag_nreg = 0;
static uint64_t g_tag_regs = 0;
static Kind g_kind = K_NONE;

static Regs g_want;
static Regs g_sent;
static bool g_sent_valid = false;
static bool g_force_tex0 = false;
static bool g_drawn_since_flush = true;

static Texture* g_owner[PAGES];
static uint32_t g_stamp = 1;
static std::vector<Texture*> g_garbage;
static std::vector<void*> g_scratch;

static uint8_t g_stage[STAGE_BYTES] __attribute__((aligned(64)));
static size_t g_stage_used = 0;

static Texture g_surf[6];
static Texture g_temp_tex;
static Texture g_temp2_tex;
static int g_screen_page[2] = {P_SCREEN0, P_SCREEN1};
static int g_back = 0;
static int g_shown = -1;
static int g_last = 0;

static Surface g_target = Surface::Screen;
static Box g_sc = {0, 0, SCREEN_W, SCREEN_H};
static uint32_t g_protect = 0;
static bool g_wrap = false;
static Stencil g_stencil = Stencil::Off;
static bool g_frame_open = false;
static int g_batches = 0;

static Blend g_layer_blend = Blend::Normal;
static bool g_layer_open = false;
static Box g_layer_box = {0, 0, 0, 0};

static uint64_t g_band[2][(1 + BAND_SPRITES * 2) * 2] __attribute__((aligned(64)));
static int g_band_sprites[2] = {0, 0};
static uint32_t g_ramp_clut[256] __attribute__((aligned(64)));
static uint32_t g_work_clut[256] __attribute__((aligned(64)));
static uint32_t g_flat_clut[256] __attribute__((aligned(64)));

static int g_vsync_sema = -1;
static volatile int g_vsync_waiting = 0;

void* alloc(size_t bytes) { return memalign(64, bytes ? (bytes + 63) & ~(size_t)63 : 64); }

void release(void* p) { std::free(p); }

size_t arena_free_bytes() { return 0; }

void* arena_try_alloc(size_t) { return nullptr; }

static uint32_t phys(const void* p) { return (uint32_t)(uintptr_t)p & 0x0FFFFFFFu; }

static int log2_ceil(int n) {
    int l = 0;
    while ((1 << l) < n) l++;
    return l;
}

static inline int clut_slot(int i) { return (i & 0xE7) | ((i & 0x08) << 1) | ((i & 0x10) >> 1); }

static inline uint32_t gs_from_premul(uint32_t c) {
    uint32_t a = c >> 24;
    if (!a) return 0;
    uint32_t r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
    if (a < 255) {
        r = std::min<uint32_t>((r * 255 + a / 2) / a, 255);
        g = std::min<uint32_t>((g * 255 + a / 2) / a, 255);
        b = std::min<uint32_t>((b * 255 + a / 2) / a, 255);
    }
    return r | (g << 8) | (b << 16) | (((a * 128 + 127) / 255) << 24);
}

static inline uint32_t premul_from_gs(uint32_t v) {
    uint32_t a = v >> 24;
    a = a >= 128 ? 255 : (a * 255 + 64) / 128;
    return rgba((int)((v & 255) * a / 255), (int)(((v >> 8) & 255) * a / 255), (int)(((v >> 16) & 255) * a / 255), (int)a);
}

static void chain_reset() {
    g_qbase = g_chain[g_cur];
    g_q = g_qbase;
    g_qend = g_qbase + CHAIN_QW * 2;
    g_cnt = nullptr;
}

static void cnt_open() {
    if (g_cnt) return;
    g_cnt = g_q;
    g_q += 2;
}

static void cnt_close() {
    if (!g_cnt) return;
    uint32_t qwc = (uint32_t)((g_q - g_cnt - 2) / 2);
    g_cnt[0] = DMATAG(qwc, 0, DMA_TAG_CNT, 0, 0, 0);
    g_cnt[1] = 0;
    g_cnt = nullptr;
}

static void close_prims() {
    if (!g_tag) return;
    int words = g_tag_count * g_tag_nreg;
    if (words & 1) *g_q++ = 0;
    g_tag[0] = GIF_SET_TAG(g_tag_count, 1, 0, 0, GIF_FLG_REGLIST, g_tag_nreg);
    g_tag[1] = g_tag_regs;
    g_tag = nullptr;
    g_tag_count = 0;
}

static void kick() {
    close_prims();
    cnt_close();
    if (g_q != g_qbase) g_gs_serial++;
    g_q[0] = DMATAG(0, 0, DMA_TAG_END, 0, 0, 0);
    g_q[1] = 0;
    g_q += 2;
    int qwc = (int)((g_q - g_qbase) / 2);
    g_peak_qw = std::max(g_peak_qw, qwc);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    dma_channel_send_chain(DMA_CHANNEL_GIF, g_qbase, qwc, 0, 0);
    g_cur ^= 1;
    chain_reset();
    g_kind = K_NONE;
}

static void drain() {
    kick();
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
}

static void need(int qw) {
    if (g_q + (qw + 4) * 2 > g_qend) kick();
}

static void ad_begin(int n) {
    close_prims();
    g_kind = K_NONE;
    need(n + 1);
    cnt_open();
    g_q[0] = GIF_SET_TAG(n, 1, 0, 0, GIF_FLG_PACKED, 1);
    g_q[1] = GIF_REG_AD;
    g_q += 2;
}

static void ad(int reg, uint64_t value) {
    g_q[0] = value;
    g_q[1] = (uint64_t)reg;
    g_q += 2;
}

static void ref(const void* data, int qwc) {
    cnt_close();
    g_q[0] = DMATAG(qwc, 0, DMA_TAG_REF, 0, phys(data), 0);
    g_q[1] = 0;
    g_q += 2;
}

static void image_ref(const void* data, int qwc) {
    const uint8_t* p = (const uint8_t*)data;
    while (qwc > 0) {
        int n = std::min(qwc, GIF_BLOCK_SIZE);
        need(4);
        cnt_open();
        g_q[0] = GIF_SET_TAG(n, 1, 0, 0, GIF_FLG_IMAGE, 0);
        g_q[1] = 0;
        g_q += 2;
        ref(p, n);
        p += (size_t)n * 16;
        qwc -= n;
    }
}

static void texflush() {
    ad_begin(1);
    ad(GS_REG_TEXFLUSH, 0);
    g_drawn_since_flush = false;
}

static uint64_t frame_reg(int page, int fbw, uint32_t mask) { return GS_SET_FRAME(page, fbw, GS_PSM_32, mask); }

static uint64_t scissor_reg(const Box& b) {
    if (b.empty()) return GS_SET_SCISSOR(1, 0, 1, 0);
    return GS_SET_SCISSOR(b.x0, b.x1 - 1, b.y0, b.y1 - 1);
}

static uint64_t zbuf_reg(bool write) { return GS_SET_ZBUF(P_ZBUF, GS_PSMZ_24 & 0xF, write ? 0 : 1); }

static uint64_t test_reg(bool alpha_test, int ztst) { return GS_SET_TEST(alpha_test ? 1 : 0, 6, 0, 0, 0, 0, 1, ztst); }

static void sync_regs() {
    uint64_t* want = &g_want.frame;
    uint64_t* sent = &g_sent.frame;
    static const int ids[10] = {GS_REG_FRAME_1, GS_REG_ZBUF_1, GS_REG_TEST_1, GS_REG_ALPHA_1, GS_REG_SCISSOR_1, GS_REG_XYOFFSET_1,
                                GS_REG_TEX0_1,  GS_REG_TEX1_1, GS_REG_CLAMP_1, GS_REG_PRIM};
    int n = 0;
    for (int i = 0; i < 10; i++) {
        if (!g_sent_valid || want[i] != sent[i] || (i == 6 && g_force_tex0)) n++;
    }
    if (!n) return;
    ad_begin(n);
    for (int i = 0; i < 10; i++) {
        if (g_sent_valid && want[i] == sent[i] && !(i == 6 && g_force_tex0)) continue;
        ad(ids[i], want[i]);
        sent[i] = want[i];
    }
    g_sent_valid = true;
    g_force_tex0 = false;
}

static void open_prims(Kind kind) {
    static const uint64_t T_SPR = (uint64_t)GIF_REG_RGBAQ | ((uint64_t)GIF_REG_UV << 4) | ((uint64_t)GIF_REG_XYZ2 << 8) | ((uint64_t)GIF_REG_UV << 12) |
                                  ((uint64_t)GIF_REG_XYZ2 << 16);
    static const uint64_t F_SPR = (uint64_t)GIF_REG_RGBAQ | ((uint64_t)GIF_REG_XYZ2 << 4) | ((uint64_t)GIF_REG_XYZ2 << 8);
    static const uint64_t T_TRI = (uint64_t)GIF_REG_RGBAQ | ((uint64_t)GIF_REG_UV << 4) | ((uint64_t)GIF_REG_XYZ2 << 8);
    static const uint64_t F_TRI = (uint64_t)GIF_REG_RGBAQ | ((uint64_t)GIF_REG_XYZ2 << 4);
    need(4);
    cnt_open();
    g_tag = g_q;
    g_q += 2;
    g_tag_count = 0;
    switch (kind) {
        case K_TSPR:
            g_tag_nreg = 5;
            g_tag_regs = T_SPR;
            break;
        case K_FSPR:
            g_tag_nreg = 3;
            g_tag_regs = F_SPR;
            break;
        case K_TTRI:
            g_tag_nreg = 3;
            g_tag_regs = T_TRI;
            break;
        default:
            g_tag_nreg = 2;
            g_tag_regs = F_TRI;
            break;
    }
    g_kind = kind;
}

static void begin_prims(Kind kind) {
    sync_regs();
    if (g_tag && g_kind == kind) return;
    close_prims();
    open_prims(kind);
    g_batches++;
}

static void prim_space(int words) {
    if (g_q + words + 16 <= g_qend && g_tag_count < 32000) return;
    Kind kind = g_kind;
    close_prims();
    kick();
    sync_regs();
    open_prims(kind);
}

static inline uint64_t uv(int u, int v) { return GS_SET_UV(u, v); }
static inline uint64_t xyz(int x, int y, int z) { return GS_SET_XYZ(x, y, z); }
static inline int fx(float x) { return (int)((x + 2047.5f) * 16.0f + 0.5f); }
static inline int fuv(float u) { return std::clamp((int)(u * 16.0f + 0.5f), 0, 16383); }

static uint64_t tex_rgbaq(uint32_t c) {
    uint32_t a = c >> 24;
    if (!a) return Q_ONE;
    uint32_t r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
    r = std::min<uint32_t>((r * 128 + a / 2) / a, 255);
    g = std::min<uint32_t>((g * 128 + a / 2) / a, 255);
    b = std::min<uint32_t>((b * 128 + a / 2) / a, 255);
    return GS_SET_RGBAQ(r, g, b, (a * 128 + 127) / 255, 0) | Q_ONE;
}

static uint64_t fill_rgbaq(uint32_t c) {
    uint32_t a = c >> 24;
    if (!a) return Q_ONE;
    uint32_t r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255;
    if (a < 255) {
        r = std::min<uint32_t>((r * 255 + a / 2) / a, 255);
        g = std::min<uint32_t>((g * 255 + a / 2) / a, 255);
        b = std::min<uint32_t>((b * 255 + a / 2) / a, 255);
    }
    return GS_SET_RGBAQ(r, g, b, (a * 128 + 127) / 255, 0) | Q_ONE;
}

static uint64_t flat_rgbaq(uint32_t rgb, int alpha) {
    uint32_t r = rgb & 255, g = (rgb >> 8) & 255, b = (rgb >> 16) & 255;
    return GS_SET_RGBAQ((r * 128 + 127) / 255, (g * 128 + 127) / 255, (b * 128 + 127) / 255, ((uint32_t)alpha * 128 + 127) / 255, 0) | Q_ONE;
}

static void ring_evict(Texture* t) {
    if (t->vram < 0 || t->origin == SURFACE) return;
    int n = t->pages + (t->clut ? 1 : 0);
    for (int i = t->vram; i < t->vram + n && i < PAGES; i++) {
        if (g_owner[i] == t) g_owner[i] = nullptr;
    }
    t->vram = -1;
    t->flat = false;
}

static int ring_alloc(int n, Texture* t) {
    int p = P_TEX;
    uint32_t best_age = UINT32_MAX;
    int best_taken = PAGES;
    for (int at = P_TEX; at + n <= PAGES; at++) {
        uint32_t age = 0;
        int taken = 0;
        for (int i = at; i < at + n; i++) {
            Texture* o = g_owner[i];
            if (!o) continue;
            taken++;
            age = std::max(age, o->used == g_stamp ? UINT32_MAX - 1 : o->used + 1);
        }
        if (age < best_age || (age == best_age && taken < best_taken)) {
            p = at;
            best_age = age;
            best_taken = taken;
            if (!age) break;
        }
    }
    for (int i = p; i < p + n; i++) {
        if (g_owner[i]) ring_evict(g_owner[i]);
    }
    for (int i = p; i < p + n; i++) g_owner[i] = t;
    return p;
}

static uint8_t* stage_take(size_t bytes) {
    bytes = (bytes + 63) & ~(size_t)63;
    if (bytes > STAGE_BYTES) {
        uint8_t* p = (uint8_t*)alloc(bytes);
        if (p) g_scratch.push_back(p);
        return p;
    }
    if (g_stage_used + bytes > STAGE_BYTES) {
        drain();
        g_stage_used = 0;
    }
    uint8_t* p = g_stage + g_stage_used;
    g_stage_used += bytes;
    return p;
}

static void blit_image(int dbp, int dbw, int psm, int w, int h) {
    ad_begin(4);
    ad(GS_REG_BITBLTBUF, GS_SET_BITBLTBUF(0, 0, 0, dbp, dbw, psm));
    ad(GS_REG_TRXPOS, GS_SET_TRXPOS(0, 0, 0, 0, 0));
    ad(GS_REG_TRXREG, GS_SET_TRXREG(w, h));
    ad(GS_REG_TRXDIR, GS_SET_TRXDIR(0));
}

static void upload_clut(int page, const uint32_t* clut) {
    blit_image(page * 32, 1, GS_PSM_32, 16, 16);
    image_ref(clut, 64);
    g_force_tex0 = true;
}

static void upload_wrapped(Texture* t) {
    int w4 = (t->w + 3) & ~3;
    bool direct = t->opaque && ((uintptr_t)t->cpu & 15) == 0 && (t->cpu_stride & 3) == 0 && t->cpu_stride >= w4;
    if (direct) {
        SyncDCache(t->cpu, t->cpu + (size_t)t->cpu_stride * (t->h - 1) + w4);
        blit_image(t->vram * 32, t->tbw, GS_PSM_32, w4, t->h);
        if (t->cpu_stride == w4) {
            image_ref(t->cpu, w4 * t->h / 4);
            return;
        }
        for (int y = 0; y < t->h; y++) {
            need(4);
            cnt_open();
            g_q[0] = GIF_SET_TAG(w4 / 4, 1, 0, 0, GIF_FLG_IMAGE, 0);
            g_q[1] = 0;
            g_q += 2;
            ref(t->cpu + (size_t)y * t->cpu_stride, w4 / 4);
        }
        return;
    }
    uint32_t* out = (uint32_t*)stage_take((size_t)w4 * t->h * 4);
    if (!out) return;
    for (int y = 0; y < t->h; y++) {
        const uint32_t* s = t->cpu + (size_t)y * t->cpu_stride;
        uint32_t* d = out + (size_t)y * w4;
        for (int x = 0; x < t->w; x++) d[x] = gs_from_premul(s[x]);
        for (int x = t->w; x < w4; x++) d[x] = 0;
    }
    SyncDCache(out, out + (size_t)w4 * t->h);
    blit_image(t->vram * 32, t->tbw, GS_PSM_32, w4, t->h);
    image_ref(out, w4 * t->h / 4);
}

static void upload(Texture* t) {
    t->vram = ring_alloc(t->pages + (t->clut ? 1 : 0), t);
    t->flat = false;
    if (t->cpu) {
        upload_wrapped(t);
    } else {
        int bpp = t->psm == GS_PSM_8 ? 1 : 4;
        blit_image(t->vram * 32, t->tbw, t->psm, t->stride, t->rows);
        image_ref(t->pixels, (int)((size_t)t->stride * t->rows * bpp / 16));
        if (t->clut) upload_clut(t->vram + t->pages, t->clut);
    }
    texflush();
    g_force_tex0 = true;
}

static void make_flat(Texture* t) {
    if (t->flat || t->psm == GS_PSM_8) return;
    Regs saved = g_want;
    g_want.frame = frame_reg(t->vram, t->tbw, ALPHA_MASK);
    g_want.scissor = GS_SET_SCISSOR(0, t->w - 1, 0, t->h - 1);
    g_want.xyoffset = GS_SET_XYOFFSET(OFS, OFS);
    g_want.test = test_reg(false, 1);
    g_want.zbuf = zbuf_reg(false);
    g_want.alpha = GS_SET_ALPHA(0, 2, 2, 2, 0x80);
    g_want.prim = GS_SET_PRIM(GS_PRIM_SPRITE, 0, 0, 0, 0, 0, 1, 0, 0);
    begin_prims(K_FSPR);
    prim_space(4);
    g_q[0] = GS_SET_RGBAQ(255, 255, 255, 0x80, 0) | Q_ONE;
    g_q[1] = xyz(OFS, OFS, 0);
    g_q[2] = xyz(OFS + t->w * 16, OFS + t->h * 16, 0);
    g_q += 3;
    g_tag_count++;
    g_want = saved;
    texflush();
    t->flat = true;
    g_force_tex0 = true;
}

static void finish_texture(Texture* t, int psm, int stride, int rows) {
    t->psm = psm;
    t->stride = stride;
    t->rows = rows;
    if (psm == GS_PSM_8) {
        t->tbw = ((stride + 127) / 128) * 2;
        t->pages = (t->tbw / 2) * ((rows + 63) / 64);
    } else {
        t->tbw = std::max(1, (stride + 63) / 64);
        t->pages = t->tbw * ((rows + 31) / 32);
    }
    t->tw = log2_ceil(t->w);
    t->th = log2_ceil(t->h);
}

static uint32_t expand(Format f, const uint8_t* p, const uint32_t* pal) {
    switch (f) {
        case Format::RGBA8888: return rgba(p[0], p[1], p[2], p[3]);
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
    const uint8_t* src = (const uint8_t*)pixels;
    if (swizzled && fmt == Format::RGBA8888 && ((uintptr_t)pixels & 15) == 0 && (stride & 3) == 0) {
        t->pixels = (uint8_t*)pixels;
        finish_texture(t, GS_PSM_32, stride, rows);
        release(palette);
    } else if (fmt == Format::T8 && palette) {
        int s16 = (w + 15) & ~15;
        if (s16 == stride && ((uintptr_t)pixels & 15) == 0) {
            t->pixels = (uint8_t*)pixels;
        } else {
            t->pixels = (uint8_t*)alloc((size_t)s16 * h);
            if (!t->pixels) {
                release(pixels);
                release(palette);
                delete t;
                return nullptr;
            }
            for (int y = 0; y < h; y++) {
                std::memcpy(t->pixels + (size_t)y * s16, src + (size_t)y * stride, (size_t)w);
                std::memset(t->pixels + (size_t)y * s16 + w, 0, (size_t)(s16 - w));
            }
            release(pixels);
        }
        t->clut = (uint32_t*)alloc(1024);
        if (!t->clut) {
            release(t->pixels);
            release(palette);
            delete t;
            return nullptr;
        }
        for (int i = 0; i < 256; i++) t->clut[clut_slot(i)] = gs_from_premul(palette[i]);
        release(palette);
        finish_texture(t, GS_PSM_8, s16, h);
        SyncDCache(t->clut, t->clut + 256);
    } else {
        int s4 = (w + 3) & ~3;
        uint32_t* out = (uint32_t*)alloc((size_t)s4 * h * 4);
        if (!out) {
            release(pixels);
            release(palette);
            delete t;
            return nullptr;
        }
        int bpp = fmt == Format::RGBA8888 ? 4 : fmt == Format::T8 ? 1 : 2;
        for (int y = 0; y < h; y++) {
            uint32_t* d = out + (size_t)y * s4;
            for (int x = 0; x < w; x++) {
                uint32_t c = expand(fmt, src + ((size_t)y * stride + x) * bpp, palette);
                d[x] = swizzled ? c : gs_from_premul(c);
            }
            for (int x = w; x < s4; x++) d[x] = 0;
        }
        release(pixels);
        release(palette);
        t->pixels = (uint8_t*)out;
        finish_texture(t, GS_PSM_32, s4, h);
    }
    t->bytes = (size_t)t->stride * t->rows * (t->psm == GS_PSM_8 ? 1 : 4) + (t->clut ? 1024 : 0);
    SyncDCache(t->pixels, t->pixels + (size_t)t->stride * t->rows * (t->psm == GS_PSM_8 ? 1 : 4));
    return t;
}

Texture* texture_wrap(int w, int h, int stride, uint32_t* pixels) {
    Texture* t = new Texture();
    t->w = w;
    t->h = h;
    t->origin = BORROWED;
    t->cpu = pixels;
    t->cpu_stride = stride;
    finish_texture(t, GS_PSM_32, (w + 3) & ~3, h);
    texture_touch(t);
    return t;
}

void texture_touch(Texture* t) {
    if (!t || !t->cpu) return;
    bool opaque = true;
    for (int y = 0; y < t->h && opaque; y++) {
        const uint32_t* row = t->cpu + (size_t)y * t->cpu_stride;
        for (int x = 0; x < t->w; x++) {
            if ((row[x] >> 24) != 255) {
                opaque = false;
                break;
            }
        }
    }
    t->opaque = opaque;
    ring_evict(t);
}

constexpr int READBACK_PIXELS = 16384;
static uint32_t g_readback[READBACK_PIXELS] __attribute__((aligned(64)));
static int g_readback_page = -1, g_readback_fbw = 0, g_readback_y = 0, g_readback_rows = 0;
static uint32_t g_readback_serial = 0;

static const uint32_t* readback_row(int page, int fbw, int y, int height) {
    if (g_q != g_qbase || g_tag || g_cnt) drain();
    bool hit = page == g_readback_page && fbw == g_readback_fbw && g_readback_serial == g_gs_serial && y >= g_readback_y &&
               y < g_readback_y + g_readback_rows;
    if (!hit) {
        int rows = std::max(1, std::min(READBACK_PIXELS / (fbw * 64), height - y));
        dma_channel_wait(DMA_CHANNEL_GIF, 0);
        ps2_screenshot(g_readback, (unsigned)(page * 32), 0, (unsigned)y, (unsigned)(fbw * 64), (unsigned)rows, GS_PSM_32);
        g_readback_page = page;
        g_readback_fbw = fbw;
        g_readback_y = y;
        g_readback_rows = rows;
        g_readback_serial = g_gs_serial;
    }
    return g_readback + (size_t)(y - g_readback_y) * fbw * 64;
}

bool texture_read(const Texture* t, int x, int y, uint32_t* out) {
    if (!t || x < 0 || y < 0 || x >= t->w || y >= t->h) return false;
    if (t->cpu) {
        *out = t->cpu[(size_t)y * t->cpu_stride + x];
        return true;
    }
    if (t->origin == SURFACE) {
        uint32_t v = readback_row(t->vram, t->tbw, y, t->h)[x];
        *out = rgba((int)(v & 255), (int)((v >> 8) & 255), (int)((v >> 16) & 255), 255);
        return true;
    }
    if (t->psm == GS_PSM_8) {
        *out = premul_from_gs(t->clut[clut_slot(t->pixels[(size_t)y * t->stride + x])]);
        return true;
    }
    *out = premul_from_gs(((const uint32_t*)t->pixels)[(size_t)y * t->stride + x]);
    return true;
}

void texture_read_rect(const Texture* t, int x, int y, int w, int h, uint32_t* out, size_t stride) {
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) texture_read(t, x + i, y + j, out + (size_t)j * stride + i);
    }
}

static void collect_garbage() {
    for (Texture* t : g_garbage) {
        ring_evict(t);
        if (t->origin == OWN) {
            release(t->pixels);
            release(t->clut);
        }
        delete t;
    }
    g_garbage.clear();
    for (void* p : g_scratch) release(p);
    g_scratch.clear();
    g_stage_used = 0;
}

void texture_destroy(Texture* t) {
    if (t) g_garbage.push_back(t);
}

size_t texture_bytes(const Texture* t) { return t ? t->bytes : 0; }
int texture_width(const Texture* t) { return t ? t->w : 0; }
int texture_height(const Texture* t) { return t ? t->h : 0; }

static void setup_surface(Texture& t, int page, int w, int h, int index) {
    t.w = w;
    t.h = h;
    t.origin = SURFACE;
    t.surface = index;
    t.opaque = true;
    finish_texture(&t, GS_PSM_32, (w + 63) & ~63, h);
    t.vram = page;
}

static Texture& surf(Surface s) { return g_surf[(int)s]; }

static void point_screen() {
    Texture& s = surf(Surface::Screen);
    s.vram = g_screen_page[g_back];
}

static int vsync_handler(int) {
    ps2::g_vcount = ps2::g_vcount + 1;
    if (g_vsync_waiting) {
        g_vsync_waiting = 0;
        iSignalSema(g_vsync_sema);
    }
    ExitHandler();
    return 0;
}

static void wait_vsync() {
    g_vsync_waiting = 1;
    WaitSema(g_vsync_sema);
}

static void build_band(int parity) {
    uint64_t* p = g_band[parity] + 2;
    int n = 0;
    auto put = [&](int x0, int y0, int u0, int v0, int w) {
        p[0] = uv(u0 * 16 + 8, v0 * 16 + 8);
        p[1] = xyz(OFS + x0 * 16, OFS + y0 * 16, 0);
        p[2] = uv((u0 + w) * 16 + 8, (v0 + 2) * 16 + 8);
        p[3] = xyz(OFS + (x0 + w) * 16, OFS + (y0 + 2) * 16, 0);
        p += 4;
        n++;
    };
    for (int bx = 0; bx < BLOCKS_X; bx++) {
        for (int k = 0; k < 4; k++) {
            int x0 = bx * 8, y0 = k * 2, u0 = bx * 16, v0 = k * 4 + parity * 2;
            if (((k & 1) ^ parity) != 0) {
                put(x0, y0, u0 + 4, v0, 4);
                put(x0 + 4, y0, u0, v0, 4);
            } else {
                put(x0, y0, u0, v0, 8);
            }
        }
    }
    g_band[parity][0] = GIF_SET_TAG(n, 1, 0, 0, GIF_FLG_REGLIST, 4);
    g_band[parity][1] = (uint64_t)GIF_REG_UV | ((uint64_t)GIF_REG_XYZ2 << 4) | ((uint64_t)GIF_REG_UV << 8) | ((uint64_t)GIF_REG_XYZ2 << 12);
    g_band_sprites[parity] = n;
    SyncDCache(g_band[parity], p);
}

static void base_regs() {
    g_want.zbuf = zbuf_reg(false);
    g_want.test = test_reg(false, 1);
    g_want.xyoffset = GS_SET_XYOFFSET(OFS, OFS);
    g_want.tex1 = GS_SET_TEX1(0, 0, 0, 0, 0, 0, 0);
    g_want.clamp = GS_SET_CLAMP(1, 1, 0, 0, 0, 0);
}

static void clear_rect(int page, int fbw, int w, int h, uint32_t color, uint32_t mask) {
    Regs saved = g_want;
    base_regs();
    g_want.frame = frame_reg(page, fbw, mask);
    g_want.scissor = GS_SET_SCISSOR(0, w - 1, 0, h - 1);
    g_want.prim = GS_SET_PRIM(GS_PRIM_SPRITE, 0, 0, 0, 0, 0, 1, 0, 0);
    begin_prims(K_FSPR);
    prim_space(4);
    g_q[0] = GS_SET_RGBAQ(color & 255, (color >> 8) & 255, (color >> 16) & 255, color >> 24, 0) | Q_ONE;
    g_q[1] = xyz(OFS, OFS, 0);
    g_q[2] = xyz(OFS + w * 16, OFS + h * 16, 0);
    g_q += 3;
    g_tag_count++;
    g_want = saved;
}

static void clear_z(int x0, int y0, int x1, int y1, int fbw, bool set) {
    Regs saved = g_want;
    base_regs();
    g_want.frame = frame_reg(surf(g_target).vram, fbw, 0xFFFFFFFFu);
    g_want.zbuf = zbuf_reg(true);
    g_want.prim = GS_SET_PRIM(GS_PRIM_SPRITE, 0, 0, 0, 0, 0, 1, 0, 0);
    Box b = {std::max(x0, g_sc.x0), std::max(y0, g_sc.y0), std::min(x1, g_sc.x1), std::min(y1, g_sc.y1)};
    g_want.scissor = scissor_reg(b);
    if (!b.empty()) {
        begin_prims(K_FSPR);
        prim_space(4);
        g_q[0] = Q_ONE;
        g_q[1] = xyz(OFS + b.x0 * 16, OFS + b.y0 * 16, set ? 0 : 1);
        g_q[2] = xyz(OFS + b.x1 * 16, OFS + b.y1 * 16, set ? 0 : 1);
        g_q += 3;
        g_tag_count++;
    }
    g_want = saved;
}

void init() {
    int region = graph_get_region();
    bool pal = region == GRAPH_MODE_PAL;
    ps2::g_hz = pal ? 50 : 60;
    graph_set_mode(GRAPH_MODE_INTERLACED, region, GRAPH_MODE_FIELD, GRAPH_DISABLE);
    int dx = pal ? 680 : 652;
    int top = pal ? 37 : 26;
    int lines = pal ? 512 : 448;
    int dy = (top - 1) * 2 + (lines - SCREEN_H * 2) / 2;
    *GS_REG_DISPLAY1 = GS_SET_DISPLAY(dx, dy, 2560 / SCREEN_W - 1, 1, 2559, SCREEN_H * 2 - 1);
    graph_set_bgcolor(0, 0, 0);
    graph_set_framebuffer(0, P_SCREEN0 * 2048, SCREEN_W, GS_PSM_32, 0, 0);
    graph_set_output(GRAPH_ENABLE, GRAPH_DISABLE, 1, 0, 1, 0xFF);
    dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);
    std::memset(g_owner, 0, sizeof(g_owner));
    setup_surface(g_surf[(int)Surface::Screen], P_SCREEN0, SCREEN_W, SCREEN_H, 0);
    setup_surface(g_surf[(int)Surface::Grab], P_GRAB, SCREEN_W, SCREEN_H, 1);
    setup_surface(g_surf[(int)Surface::Half0], P_HALF0, HALF_W, HALF_H, 2);
    setup_surface(g_surf[(int)Surface::Half1], P_HALF0 + HALF_PAGES, HALF_W, HALF_H, 3);
    setup_surface(g_surf[(int)Surface::Half2], P_HALF0 + 2 * HALF_PAGES, HALF_W, HALF_H, 4);
    setup_surface(g_surf[(int)Surface::Wide], P_WIDE, SCREEN_W, SCREEN_H, 5);
    setup_surface(g_temp_tex, P_TEMP, SCREEN_W, SCREEN_H, 6);
    setup_surface(g_temp2_tex, P_TEMP2, SCREEN_W, SCREEN_H, 7);
    build_band(0);
    build_band(1);
    for (int i = 0; i < 256; i++) g_ramp_clut[clut_slot(i)] = (uint32_t)((i * 128 + 127) / 255) << 24;
    SyncDCache(g_ramp_clut, g_ramp_clut + 256);
    ee_sema_t s{};
    s.init_count = 0;
    s.max_count = 1;
    g_vsync_sema = CreateSema(&s);
    DIntr();
    AddIntcHandler(INTC_VBLANK_S, vsync_handler, 0);
    EnableIntc(INTC_VBLANK_S);
    EIntr();
    g_cur = 0;
    chain_reset();
    ad_begin(9);
    ad(GS_REG_PRMODECONT, GS_SET_PRMODECONT(1));
    ad(GS_REG_COLCLAMP, GS_SET_COLCLAMP(1));
    ad(GS_REG_DTHE, GS_SET_DTHE(0));
    ad(GS_REG_PABE, GS_SET_PABE(0));
    ad(GS_REG_FBA_1, GS_SET_FBA(0));
    ad(GS_REG_TEXA, GS_SET_TEXA(0x80, 0, 0x80));
    ad(GS_REG_TEXCLUT, GS_SET_TEXCLUT(1, 0, 0));
    ad(GS_REG_ZBUF_1, zbuf_reg(false));
    ad(GS_REG_TEST_1, test_reg(false, 1));
    g_sent_valid = false;
    base_regs();
    g_want.alpha = GS_SET_ALPHA(0, 1, 0, 1, 0);
    g_want.tex0 = 0;
    g_want.prim = 0;
    g_want.frame = frame_reg(P_SCREEN0, FULL_FBW, 0);
    g_want.scissor = scissor_reg(Box{0, 0, SCREEN_W, SCREEN_H});
    for (int p : {P_SCREEN0, P_SCREEN1, P_GRAB, P_WIDE, P_TEMP, P_TEMP2}) clear_rect(p, FULL_FBW, SCREEN_W, SCREEN_H, 0x80000000u, 0);
    for (int i = 0; i < 3; i++) clear_rect(P_HALF0 + i * HALF_PAGES, HALF_FBW, HALF_W, HALF_H, 0x80000000u, 0);
    upload_clut(P_CLUT_RAMP, g_ramp_clut);
    drain();
    g_back = 1;
    g_shown = 0;
    point_screen();
}

void shutdown() {
    drain();
    DisableIntc(INTC_VBLANK_S);
    graph_shutdown();
}

static void apply_target_regs() {
    Texture& t = surf(g_target);
    g_want.frame = frame_reg(t.vram, t.tbw, g_protect | ALPHA_MASK);
    g_want.scissor = scissor_reg(g_sc);
    g_want.xyoffset = GS_SET_XYOFFSET(OFS, OFS);
    switch (g_stencil) {
        case Stencil::Off:
            g_want.zbuf = zbuf_reg(false);
            g_want.test = test_reg(false, 1);
            break;
        case Stencil::Write:
            g_want.zbuf = zbuf_reg(true);
            g_want.test = test_reg(true, 1);
            break;
        case Stencil::Test:
            g_want.zbuf = zbuf_reg(false);
            g_want.test = test_reg(false, 2);
            break;
    }
}

static uint64_t alpha_of(Blend b) {
    switch (b) {
        case Blend::Add: return GS_SET_ALPHA(0, 2, 0, 1, 0);
        case Blend::Subtract: return GS_SET_ALPHA(2, 0, 0, 1, 0);
        case Blend::Copy: return GS_SET_ALPHA(0, 2, 0, 2, 0);
        default: return GS_SET_ALPHA(0, 1, 0, 1, 0);
    }
}

static void bind_texture(Texture* tex, bool smooth, int tfx, bool flat) {
    if (tex->origin != SURFACE) {
        tex->used = g_stamp;
        if (tex->vram < 0) upload(tex);
        if (flat && !tex->flat) make_flat(tex);
        if (!flat && tex->flat) upload(tex);
    } else if (g_drawn_since_flush) {
        texflush();
    }
    int cbp = 0, cld = 0;
    if (tex->clut) {
        cld = 1;
        if (flat) {
            for (int i = 0; i < 256; i++) g_flat_clut[i] = (tex->clut[i] & ALPHA_MASK) | 0x00FFFFFFu;
            SyncDCache(g_flat_clut, g_flat_clut + 256);
            upload_clut(P_CLUT_FLAT, g_flat_clut);
            cbp = P_CLUT_FLAT * 32;
        } else {
            cbp = (tex->vram + tex->pages) * 32;
        }
    }
    int tcc = tex->opaque ? 0 : 1;
    g_want.tex0 = GS_SET_TEX0(tex->vram * 32, tex->tbw, tex->psm, tex->tw, tex->th, tcc, tfx, cbp, 0, 0, 0, cld);
    g_want.tex1 = GS_SET_TEX1(0, 0, smooth ? 1 : 0, smooth ? 1 : 0, 0, 0, 0);
    bool pot = tex->w == (1 << tex->tw) && tex->h == (1 << tex->th);
    if (g_wrap && pot) g_want.clamp = GS_SET_CLAMP(0, 0, 0, 0, 0, 0);
    else g_want.clamp = GS_SET_CLAMP(2, 2, 0, tex->w - 1, 0, tex->h - 1);
    if (cld) g_force_tex0 = true;
}

static bool clip_span(float& a0, float& a1, float& t0, float& t1, float lo, float hi) {
    if (a1 <= lo || a0 >= hi) return false;
    float span = a1 - a0;
    if (span <= 0) return false;
    float d = (t1 - t0) / span;
    if (a0 < lo) {
        t0 += (lo - a0) * d;
        a0 = lo;
    }
    if (a1 > hi) {
        t1 -= (a1 - hi) * d;
        a1 = hi;
    }
    return true;
}

static Box pixel_box(float x0, float y0, float x1, float y1) {
    Box b;
    b.x0 = std::max(g_sc.x0, (int)std::ceil(x0 - 0.5f));
    b.y0 = std::max(g_sc.y0, (int)std::ceil(y0 - 0.5f));
    b.x1 = std::min(g_sc.x1, (int)std::ceil(x1 - 0.5f));
    b.y1 = std::min(g_sc.y1, (int)std::ceil(y1 - 0.5f));
    return b;
}

static Box verts_box(const PVert* v, int n) {
    float x0 = v[0].x, y0 = v[0].y, x1 = v[0].x, y1 = v[0].y;
    for (int i = 1; i < n; i++) {
        x0 = std::min(x0, v[i].x);
        y0 = std::min(y0, v[i].y);
        x1 = std::max(x1, v[i].x);
        y1 = std::max(y1, v[i].y);
    }
    return pixel_box(x0, y0, x1, y1);
}

struct Geometry {
    bool sprites;
    Texture* tex;
    bool smooth;
    bool flat;
    uint32_t flat_rgb;
    int flat_alpha;
    const PVert* v;
    int n;
};

static void emit_geometry(const Geometry& g, int tfx) {
    if (g.tex && g.tex->origin == SURFACE && g.flat) {
        Kind kind = g.sprites ? K_FSPR : K_FTRI;
        g_want.prim = GS_SET_PRIM(g.sprites ? GS_PRIM_SPRITE : GS_PRIM_TRIANGLE, 0, 0, 0, 1, 0, 1, 0, 0);
        uint64_t c = fill_rgbaq((g.flat_rgb & 0xFFFFFFu) | 0xFF000000u);
        c = (c & ~((uint64_t)0xFF << 24)) | ((uint64_t)((g.flat_alpha * 128 + 127) / 255) << 24);
        begin_prims(kind);
        if (g.sprites) {
            for (int i = 0; i + 1 < g.n; i += 2) {
                prim_space(4);
                g_q[0] = c;
                g_q[1] = xyz(fx(g.v[i].x), fx(g.v[i].y), 0);
                g_q[2] = xyz(fx(g.v[i + 1].x), fx(g.v[i + 1].y), 0);
                g_q += 3;
                g_tag_count++;
            }
        } else {
            for (int i = 0; i < g.n; i++) {
                prim_space(3);
                g_q[0] = c;
                g_q[1] = xyz(fx(g.v[i].x), fx(g.v[i].y), 0);
                g_q += 2;
                g_tag_count++;
            }
        }
        return;
    }
    if (g.tex) bind_texture(g.tex, g.smooth, tfx, g.flat);
    bool tme = g.tex != nullptr;
    Kind kind = g.sprites ? (tme ? K_TSPR : K_FSPR) : (tme ? K_TTRI : K_FTRI);
    g_want.prim = GS_SET_PRIM(g.sprites ? GS_PRIM_SPRITE : GS_PRIM_TRIANGLE, g.sprites ? 0 : 1, tme ? 1 : 0, 0, 1, 0, 1, 0, 0);
    begin_prims(kind);
    auto color = [&](uint32_t c) -> uint64_t {
        if (g.flat) return flat_rgbaq(g.flat_rgb, g.flat_alpha);
        return tme ? tex_rgbaq(c) : fill_rgbaq(c);
    };
    if (g.sprites) {
        for (int i = 0; i + 1 < g.n; i += 2) {
            const PVert& a = g.v[i];
            const PVert& b = g.v[i + 1];
            if (tme) {
                prim_space(6);
                g_q[0] = color(a.color);
                g_q[1] = uv(fuv(a.u), fuv(a.v));
                g_q[2] = xyz(fx(a.x), fx(a.y), 0);
                g_q[3] = uv(fuv(b.u), fuv(b.v));
                g_q[4] = xyz(fx(b.x), fx(b.y), 0);
                g_q += 5;
            } else {
                prim_space(4);
                g_q[0] = color(a.color);
                g_q[1] = xyz(fx(a.x), fx(a.y), 0);
                g_q[2] = xyz(fx(b.x), fx(b.y), 0);
                g_q += 3;
            }
            g_tag_count++;
        }
    } else {
        for (int i = 0; i < g.n; i++) {
            const PVert& a = g.v[i];
            if (tme) {
                prim_space(4);
                g_q[0] = color(a.color);
                g_q[1] = uv(fuv(a.u), fuv(a.v));
                g_q[2] = xyz(fx(a.x), fx(a.y), 0);
                g_q += 3;
            } else {
                prim_space(3);
                g_q[0] = color(a.color);
                g_q[1] = xyz(fx(a.x), fx(a.y), 0);
                g_q += 2;
            }
            g_tag_count++;
        }
    }
}

static void draw_into_target(const Geometry& g, uint64_t alpha) {
    apply_target_regs();
    g_want.alpha = alpha;
    emit_geometry(g, TFX_MODULATE);
    g_drawn_since_flush = true;
}

static void stencil_pass(const Geometry& g) {
    apply_target_regs();
    Texture& t = surf(g_target);
    g_want.frame = frame_reg(t.vram, t.tbw, 0xFFFFFFFFu);
    g_want.alpha = GS_SET_ALPHA(0, 1, 0, 1, 0);
    emit_geometry(g, TFX_MODULATE);
}

static void blit_surface(Texture& src, int dst_page, int dst_fbw, const Box& b, uint64_t alpha, bool abe, uint32_t mask, int ztst, bool tcc = false,
                         bool zwrite = false) {
    if (b.empty()) return;
    g_want.frame = frame_reg(dst_page, dst_fbw, mask);
    g_want.scissor = scissor_reg(b);
    g_want.xyoffset = GS_SET_XYOFFSET(OFS, OFS);
    g_want.zbuf = zbuf_reg(zwrite);
    g_want.test = test_reg(zwrite, ztst);
    g_want.alpha = alpha;
    if (g_drawn_since_flush) texflush();
    g_want.tex0 = GS_SET_TEX0(src.vram * 32, src.tbw, GS_PSM_32, src.tw, src.th, tcc ? 1 : 0, TFX_DECAL, 0, 0, 0, 0, 0);
    g_want.tex1 = GS_SET_TEX1(0, 0, 0, 0, 0, 0, 0);
    g_want.clamp = GS_SET_CLAMP(1, 1, 0, 0, 0, 0);
    g_want.prim = GS_SET_PRIM(GS_PRIM_SPRITE, 0, 1, 0, abe ? 1 : 0, 0, 1, 0, 0);
    begin_prims(K_TSPR);
    prim_space(6);
    g_q[0] = GS_SET_RGBAQ(0x80, 0x80, 0x80, 0x80, 0) | Q_ONE;
    g_q[1] = uv(b.x0 * 16 + 8, b.y0 * 16 + 8);
    g_q[2] = xyz(OFS + b.x0 * 16, OFS + b.y0 * 16, 0);
    g_q[3] = uv(b.x1 * 16 + 8, b.y1 * 16 + 8);
    g_q[4] = xyz(OFS + b.x1 * 16, OFS + b.y1 * 16, 0);
    g_q += 5;
    g_tag_count++;
    g_drawn_since_flush = true;
}

static void fill_box(int page, int fbw, const Box& b, uint64_t rgbaq, uint64_t alpha, bool abe, uint32_t mask, int ztst) {
    if (b.empty()) return;
    g_want.frame = frame_reg(page, fbw, mask);
    g_want.scissor = scissor_reg(b);
    g_want.xyoffset = GS_SET_XYOFFSET(OFS, OFS);
    g_want.zbuf = zbuf_reg(false);
    g_want.test = test_reg(false, ztst);
    g_want.alpha = alpha;
    g_want.prim = GS_SET_PRIM(GS_PRIM_SPRITE, 0, 0, 0, abe ? 1 : 0, 0, 1, 0, 0);
    begin_prims(K_FSPR);
    prim_space(4);
    g_q[0] = rgbaq;
    g_q[1] = xyz(OFS + b.x0 * 16, OFS + b.y0 * 16, 0);
    g_q[2] = xyz(OFS + b.x1 * 16, OFS + b.y1 * 16, 0);
    g_q += 3;
    g_tag_count++;
    g_drawn_since_flush = true;
}

static void shuffle(const Texture& src, int channel, int clut_page, int dst_page, int dst_fbw, const Box& b, uint32_t mask, bool abe, uint64_t alpha) {
    if (b.empty()) return;
    int parity = channel & 1;
    bool high = (channel & 2) != 0;
    if (g_drawn_since_flush) texflush();
    g_want.frame = frame_reg(dst_page, dst_fbw, mask);
    g_want.scissor = scissor_reg(b);
    g_want.zbuf = zbuf_reg(false);
    g_want.test = test_reg(false, 1);
    g_want.alpha = alpha;
    g_want.tex0 = GS_SET_TEX0(src.vram * 32, src.tbw * 2, GS_PSM_8, 10, 10, 1, TFX_DECAL, clut_page * 32, 0, 0, 0, 1);
    g_want.tex1 = GS_SET_TEX1(0, 0, 0, 0, 0, 0, 0);
    g_want.prim = GS_SET_PRIM(GS_PRIM_SPRITE, 0, 1, 0, abe ? 1 : 0, 0, 1, 0, 0);
    g_force_tex0 = true;
    int wms = high ? 3 : 1;
    int minu = high ? 0x3F7 : 0;
    int maxu = high ? 0x008 : 0;
    for (int by = b.y0 / 8; by <= (b.y1 - 1) / 8; by++) {
        g_want.clamp = GS_SET_CLAMP(wms, 3, minu, maxu, 0x00F, by * 16);
        g_want.xyoffset = GS_SET_XYOFFSET(OFS, OFS - by * 8 * 16);
        sync_regs();
        close_prims();
        need(4);
        ref(g_band[parity], 1 + g_band_sprites[parity] * 2);
        g_batches++;
    }
    g_want.xyoffset = GS_SET_XYOFFSET(OFS, OFS);
    g_drawn_since_flush = true;
}

static bool is_special(Blend b) { return b == Blend::Multiply || b == Blend::Screen || b == Blend::MulSrc || b == Blend::MulAdd || b == Blend::MulSub || b == Blend::Max || b == Blend::Min; }

static uint32_t neutral_of(Blend b) {
    switch (b) {
        case Blend::MulSrc:
        case Blend::Multiply:
        case Blend::Min: return 0x80FFFFFFu;
        default: return 0x80000000u;
    }
}

static uint64_t resolve_alpha(Blend b) {
    switch (b) {
        case Blend::MulAdd: return GS_SET_ALPHA(1, 2, 1, 1, 0);
        case Blend::MulSub:
        case Blend::Screen: return GS_SET_ALPHA(2, 1, 1, 1, 0);
        default: return GS_SET_ALPHA(1, 2, 1, 2, 0);
    }
}

static int stencil_ztst() { return g_stencil == Stencil::Test ? 2 : 1; }

static void resolve_layer() {
    if (!g_layer_open) return;
    g_layer_open = false;
    Box b = g_layer_box;
    if (b.empty()) return;
    Texture& t = surf(g_target);
    Blend blend = g_layer_blend;
    uint32_t keep = g_protect | ALPHA_MASK;
    if (blend == Blend::Max) {
        blit_surface(t, P_TEMP, FULL_FBW, b, GS_SET_ALPHA(2, 0, 2, 1, 0x80), true, ALPHA_MASK, 1);
        blit_surface(g_temp_tex, t.vram, t.tbw, b, GS_SET_ALPHA(0, 2, 2, 1, 0x80), true, keep, stencil_ztst());
        return;
    }
    if (blend == Blend::Min) {
        blit_surface(t, P_TEMP2, FULL_FBW, b, GS_SET_ALPHA(0, 2, 2, 2, 0x80), false, ALPHA_MASK, 1);
        blit_surface(g_temp_tex, P_TEMP2, FULL_FBW, b, GS_SET_ALPHA(2, 0, 2, 1, 0x80), true, ALPHA_MASK, 1);
        blit_surface(g_temp2_tex, t.vram, t.tbw, b, GS_SET_ALPHA(2, 0, 2, 1, 0x80), true, keep, stencil_ztst());
        return;
    }
    for (int c = 0; c < 3; c++) {
        uint32_t bits = 0xFFu << (8 * c);
        if (g_protect & bits) continue;
        shuffle(g_temp_tex, c, P_CLUT_RAMP, t.vram, t.tbw, b, 0x00FFFFFFu, false, GS_SET_ALPHA(0, 1, 0, 1, 0));
        fill_box(t.vram, t.tbw, b, Q_ONE, resolve_alpha(blend), true, ~bits, stencil_ztst());
    }
}

static void begin_layer(Blend blend, const Box& b) {
    bool compatible = g_layer_open && g_layer_blend == blend;
    if (compatible) {
        Box o = {std::max(b.x0, g_layer_box.x0), std::max(b.y0, g_layer_box.y0), std::min(b.x1, g_layer_box.x1), std::min(b.y1, g_layer_box.y1)};
        if (!o.empty()) compatible = false;
    }
    if (!compatible) {
        resolve_layer();
        fill_box(P_TEMP, FULL_FBW, Box{0, 0, SCREEN_W, SCREEN_H}, GS_SET_RGBAQ(neutral_of(blend) & 255, (neutral_of(blend) >> 8) & 255,
                 (neutral_of(blend) >> 16) & 255, 0x80, 0) | Q_ONE, GS_SET_ALPHA(0, 2, 2, 2, 0x80), false, ALPHA_MASK, 1);
        g_layer_open = true;
        g_layer_blend = blend;
        g_layer_box = b;
        return;
    }
    g_layer_box = {std::min(b.x0, g_layer_box.x0), std::min(b.y0, g_layer_box.y0), std::max(b.x1, g_layer_box.x1), std::max(b.y1, g_layer_box.y1)};
}

static void draw_into_layer(const Geometry& g, Blend blend) {
    g_want.frame = frame_reg(P_TEMP, FULL_FBW, ALPHA_MASK);
    g_want.scissor = scissor_reg(g_sc);
    g_want.xyoffset = GS_SET_XYOFFSET(OFS, OFS);
    g_want.zbuf = zbuf_reg(false);
    g_want.test = test_reg(false, 1);
    g_want.alpha = blend == Blend::Multiply ? GS_SET_ALPHA(0, 1, 0, 1, 0) : GS_SET_ALPHA(0, 2, 0, 2, 0);
    emit_geometry(g, TFX_MODULATE);
    g_drawn_since_flush = true;
}

static bool constant_fill(const Geometry& g, Blend blend) {
    if (g.tex || !g.sprites || blend == Blend::Max || blend == Blend::Min) return false;
    uint32_t c = g.v[0].color;
    for (int i = 1; i < g.n; i++) {
        if (g.v[i].color != c) return false;
    }
    int a = (int)(c >> 24);
    int k[3];
    for (int i = 0; i < 3; i++) {
        int s = (int)((c >> (8 * i)) & 255);
        k[i] = blend == Blend::Multiply ? std::min(255, s + 255 - a) : s;
    }
    Texture& t = surf(g_target);
    uint64_t form = blend == Blend::MulAdd ? GS_SET_ALPHA(1, 2, 2, 1, 0) : (blend == Blend::MulSub || blend == Blend::Screen) ? GS_SET_ALPHA(2, 1, 2, 1, 0) : GS_SET_ALPHA(1, 2, 2, 2, 0);
    auto pass = [&](uint32_t bits, int factor) {
        uint64_t alpha = form | ((uint64_t)((factor * 128 + 127) / 255) << 32);
        for (int i = 0; i + 1 < g.n; i += 2) {
            Box r = pixel_box(std::min(g.v[i].x, g.v[i + 1].x), std::min(g.v[i].y, g.v[i + 1].y), std::max(g.v[i].x, g.v[i + 1].x),
                              std::max(g.v[i].y, g.v[i + 1].y));
            fill_box(t.vram, t.tbw, r, Q_ONE, alpha, true, ~bits | g_protect | ALPHA_MASK, stencil_ztst());
        }
    };
    if (k[0] == k[1] && k[1] == k[2]) {
        pass(0x00FFFFFFu, k[0]);
    } else {
        for (int i = 0; i < 3; i++) pass(0xFFu << (8 * i), k[i]);
    }
    return true;
}

static void submit(const Geometry& g, Blend blend) {
    if (g.n <= 0 || !g_frame_open) return;
    if (g_sc.empty()) return;
    if (g.flat) {
        if (blend != Blend::Add && blend != Blend::Subtract) blend = Blend::Normal;
    }
    if (!is_special(blend)) {
        resolve_layer();
        draw_into_target(g, alpha_of(blend));
        return;
    }
    Box b = verts_box(g.v, g.n);
    if (b.empty()) return;
    if (g_stencil == Stencil::Write) stencil_pass(g);
    if (blend == Blend::Screen) {
        resolve_layer();
        if (!constant_fill(g, Blend::Screen)) {
            begin_layer(Blend::MulSub, b);
            draw_into_layer(g, Blend::MulSub);
            resolve_layer();
        }
        draw_into_target(g, alpha_of(Blend::Add));
        return;
    }
    if (constant_fill(g, blend)) return;
    begin_layer(blend, b);
    draw_into_layer(g, blend);
}

void begin_frame(uint32_t clear_color) {
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    collect_garbage();
    point_screen();
    g_stamp++;
    g_frame_open = true;
    g_target = Surface::Screen;
    g_protect = 0;
    g_wrap = false;
    g_stencil = Stencil::Off;
    g_layer_open = false;
    g_sc = {0, 0, SCREEN_W, SCREEN_H};
    base_regs();
    clear_rect(surf(Surface::Screen).vram, FULL_FBW, SCREEN_W, SCREEN_H, (clear_color & 0xFFFFFFu) | 0x80000000u, 0);
    clear_z(0, 0, SCREEN_W, SCREEN_H, FULL_FBW, false);
    apply_target_regs();
}

void end_frame() {
    if (!g_frame_open) return;
    resolve_layer();
    g_frame_open = false;
    *GS_REG_CSR = CSR_FINISH;
    ad_begin(1);
    ad(GS_REG_FINISH, 0);
    drain();
    while (!(*GS_REG_CSR & CSR_FINISH)) {
    }
    *GS_REG_CSR = CSR_FINISH;
    wait_vsync();
    *GS_REG_DISPFB1 = GS_SET_DISPFB(g_screen_page[g_back], FULL_FBW, GS_PSM_32, 0, 0);
    g_last = g_back;
    g_shown = g_back;
    g_back ^= 1;
    collect_garbage();
}

void present() {}

void border_canvas(float& x0, float& y0, float& x1, float& y1) {
    x0 = 0;
    y0 = 0;
    x1 = (float)SCREEN_W;
    y1 = (float)SCREEN_H;
}

bool set_border(const BorderArt* art, const BorderRect*, int, const BorderRect*, int) { return !art; }

void set_border_scroll(float) {}

int list_peak_bytes() { return g_peak_qw * 16; }

bool reclaim_memory() {
    if (g_frame_open) return false;
    drain();
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
    if (y < 0 || y >= SCREEN_H || x < 0 || x + w > SCREEN_W) return false;
    int page = g_frame_open ? surf(Surface::Screen).vram : g_screen_page[g_last];
    if (g_frame_open) resolve_layer();
    const uint32_t* row = readback_row(page, FULL_FBW, y, SCREEN_H);
    for (int i = 0; i < w; i++) {
        uint32_t v = row[x + i];
        out[i] = rgba((int)(v & 255), (int)((v >> 8) & 255), (int)((v >> 16) & 255), 255);
    }
    return true;
}

bool capture(uint32_t* out, int out_stride) {
    for (int y = 0; y < SCREEN_H; y++) {
        if (!capture_row(y, 0, SCREEN_W, out + (size_t)y * out_stride)) return false;
    }
    return true;
}

void set_scissor(int x, int y, int w, int h) {
    resolve_layer();
    Texture& t = surf(g_target);
    g_sc = {std::max(0, x), std::max(0, y), std::min(t.w, x + w), std::min(t.h, y + h)};
}

void reset_scissor() {
    resolve_layer();
    Texture& t = surf(g_target);
    g_sc = {0, 0, t.w, t.h};
}

static inline PVert pv(float x, float y, float u, float v, uint32_t c) { return PVert{x, y, u, v, c}; }

void sprite(Texture* tex, float u0, float v0, float u1, float v1, float x0, float y0, float x1, float y1, uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    if ((color >> 24) == 0 && blend != Blend::Copy && blend != Blend::MulSrc && blend != Blend::Min) return;
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(u0, u1);
    }
    if (y1 < y0) {
        std::swap(y0, y1);
        std::swap(v0, v1);
    }
    if (!clip_span(x0, x1, u0, u1, -8.0f, SCREEN_W + 8.0f) || !clip_span(y0, y1, v0, v1, -8.0f, SCREEN_H + 8.0f)) return;
    PVert v[2] = {pv(x0, y0, u0, v0, color), pv(x1, y1, u1, v1, color)};
    submit(Geometry{true, tex, smooth, false, 0, 0, v, 2}, blend);
}

static bool quad_visible(const float xy[8]) {
    float minx = xy[0], maxx = xy[0], miny = xy[1], maxy = xy[1];
    for (int i = 1; i < 4; i++) {
        minx = std::min(minx, xy[i * 2]);
        maxx = std::max(maxx, xy[i * 2]);
        miny = std::min(miny, xy[i * 2 + 1]);
        maxy = std::max(maxy, xy[i * 2 + 1]);
    }
    if (maxx <= 0 || maxy <= 0 || minx >= SCREEN_W || miny >= SCREEN_H) return false;
    return minx >= -1500 && miny >= -1500 && maxx <= 2000 && maxy <= 2000;
}

static void quad_common(Texture* tex, const float uvs[8], const float xy[8], uint32_t color, Blend blend, bool smooth, bool flat, uint32_t rgb, int alpha) {
    if (!quad_visible(xy)) return;
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    PVert v[6];
    for (int i = 0; i < 6; i++) {
        int k = idx[i];
        v[i] = pv(xy[k * 2], xy[k * 2 + 1], uvs[k * 2], uvs[k * 2 + 1], color);
    }
    submit(Geometry{false, tex, smooth, flat, rgb, alpha, v, 6}, blend);
}

void draw_quad(Texture* tex, const float uvs[8], const float xy[8], uint32_t color, Blend blend, bool smooth) {
    if (!tex) return;
    if ((color >> 24) == 0 && blend != Blend::Copy && blend != Blend::MulSrc && blend != Blend::Min) return;
    quad_common(tex, uvs, xy, color, blend, smooth, false, 0, 0);
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
    if (!clip_span(x0, x1, u0, u1, -8.0f, SCREEN_W + 8.0f) || !clip_span(y0, y1, v0, v1, -8.0f, SCREEN_H + 8.0f)) return;
    PVert v[2] = {pv(x0, y0, u0, v0, 0), pv(x1, y1, u1, v1, 0)};
    submit(Geometry{true, tex, smooth, true, rgb, std::min(alpha, 255), v, 2}, blend);
}

void quad_flat(Texture* tex, const float uvs[8], const float xy[8], uint32_t rgb, int alpha, Blend blend, bool smooth) {
    if (!tex || alpha <= 0) return;
    quad_common(tex, uvs, xy, 0, blend, smooth, true, rgb, std::min(alpha, 255));
}

void fill(float x0, float y0, float x1, float y1, uint32_t color, Blend blend) {
    if ((color >> 24) == 0 && blend != Blend::Copy && blend != Blend::MulSrc && blend != Blend::Min) return;
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    x0 = std::max(x0, -8.0f);
    y0 = std::max(y0, -8.0f);
    x1 = std::min(x1, SCREEN_W + 8.0f);
    y1 = std::min(y1, SCREEN_H + 8.0f);
    if (x1 <= x0 || y1 <= y0) return;
    PVert v[2] = {pv(x0, y0, 0, 0, color), pv(x1, y1, 0, 0, color)};
    submit(Geometry{true, nullptr, false, false, 0, 0, v, 2}, blend);
}

void draw_triangles(Texture* tex, const Vertex* v, int count, Blend blend, bool smooth) {
    count -= count % 3;
    if (count <= 0) return;
    std::vector<PVert> out;
    out.reserve((size_t)count);
    for (int i = 0; i < count; i += 3) {
        float xy[6] = {v[i].x, v[i].y, v[i + 1].x, v[i + 1].y, v[i + 2].x, v[i + 2].y};
        float minx = std::min({xy[0], xy[2], xy[4]}), maxx = std::max({xy[0], xy[2], xy[4]});
        float miny = std::min({xy[1], xy[3], xy[5]}), maxy = std::max({xy[1], xy[3], xy[5]});
        if (maxx <= 0 || maxy <= 0 || minx >= SCREEN_W || miny >= SCREEN_H) continue;
        if (minx < -1500 || miny < -1500 || maxx > 2000 || maxy > 2000) continue;
        for (int k = 0; k < 3; k++) out.push_back(pv(v[i + k].x, v[i + k].y, v[i + k].u, v[i + k].v, v[i + k].color));
    }
    if (out.empty()) return;
    submit(Geometry{false, tex, smooth, false, 0, 0, out.data(), (int)out.size()}, blend);
}

void draw_grid(Texture* tex, const Vertex* v, int cols, int rows, Blend blend, bool smooth) {
    if (cols <= 0 || rows <= 0) return;
    int stride = cols + 1;
    std::vector<PVert> out;
    out.reserve((size_t)cols * rows * 6);
    auto at = [&](int c, int r) {
        const Vertex& s = v[(size_t)r * stride + c];
        return pv(s.x, s.y, s.u, s.v, s.color);
    };
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            PVert p00 = at(c, r), p10 = at(c + 1, r), p01 = at(c, r + 1), p11 = at(c + 1, r + 1);
            float minx = std::min({p00.x, p10.x, p01.x, p11.x}), maxx = std::max({p00.x, p10.x, p01.x, p11.x});
            float miny = std::min({p00.y, p10.y, p01.y, p11.y}), maxy = std::max({p00.y, p10.y, p01.y, p11.y});
            if (maxx <= 0 || maxy <= 0 || minx >= SCREEN_W || miny >= SCREEN_H) continue;
            if (minx < -1500 || miny < -1500 || maxx > 2000 || maxy > 2000) continue;
            out.push_back(p00);
            out.push_back(p01);
            out.push_back(p10);
            out.push_back(p10);
            out.push_back(p01);
            out.push_back(p11);
        }
    }
    if (out.empty()) return;
    submit(Geometry{false, tex, smooth, false, 0, 0, out.data(), (int)out.size()}, blend);
}

Texture* grab_screen() {
    resolve_layer();
    if (g_frame_open) {
        Texture& s = surf(Surface::Screen);
        blit_surface(s, P_GRAB, FULL_FBW, Box{0, 0, SCREEN_W, SCREEN_H}, GS_SET_ALPHA(0, 2, 2, 2, 0x80), false, ALPHA_MASK, 1);
    }
    return &surf(Surface::Grab);
}

void desaturate(float amount) {
    if (amount <= 0 || !g_frame_open) return;
    if (amount > 1) amount = 1;
    resolve_layer();
    int level = (int)(amount * 255.0f + 0.5f);
    int keep = 255 - level;
    Texture& s = surf(Surface::Screen);
    Box all = {0, 0, SCREEN_W, SCREEN_H};
    blit_surface(s, P_TEMP, FULL_FBW, all, GS_SET_ALPHA(0, 2, 2, 2, 0x80), false, 0, 1);
    fill_box(s.vram, FULL_FBW, all, Q_ONE, GS_SET_ALPHA(1, 2, 2, 2, (keep * 128 + 127) / 255), true, ALPHA_MASK, 1);
    for (int j = 0; j < 256; j++) {
        uint32_t v = (uint32_t)((j * level + 382) / 765);
        g_work_clut[clut_slot(j)] = v | (v << 8) | (v << 16) | 0x80000000u;
    }
    SyncDCache(g_work_clut, g_work_clut + 256);
    upload_clut(P_CLUT_WORK, g_work_clut);
    for (int c = 0; c < 3; c++) shuffle(g_temp_tex, c, P_CLUT_WORK, s.vram, FULL_FBW, all, ALPHA_MASK, true, GS_SET_ALPHA(0, 2, 2, 1, 0x80));
    apply_target_regs();
}

void set_target(Surface s) {
    resolve_layer();
    g_target = s;
    Texture& t = surf(s);
    g_sc = {0, 0, t.w, t.h};
    g_drawn_since_flush = true;
}

Surface current_target() { return g_target; }

Texture* surface_texture(Surface s) {
    resolve_layer();
    return &surf(s);
}

int surface_width(Surface s) { return surf(s).w; }
int surface_height(Surface s) { return surf(s).h; }

void set_color_mask(uint32_t protect_rgb) {
    resolve_layer();
    g_protect = protect_rgb & 0xFFFFFFu;
}

void set_wrap(bool repeat) { g_wrap = repeat; }

void set_stencil(Stencil mode) {
    resolve_layer();
    g_stencil = mode;
}

void stencil_clear(int x0, int y0, int x1, int y1) {
    if (x1 <= x0 || y1 <= y0 || !g_frame_open) return;
    resolve_layer();
    clear_z(x0, y0, x1, y1, surf(g_target).tbw, false);
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
    resolve_layer();
    Box b = pixel_box(x0, y0, x1, y1);
    if (b.empty()) return;
    int ch = std::clamp(channel, 0, 3);
    g_want.frame = frame_reg(P_TEMP2, FULL_FBW, 0);
    g_want.scissor = scissor_reg(b);
    g_want.xyoffset = GS_SET_XYOFFSET(OFS, OFS);
    g_want.zbuf = zbuf_reg(false);
    g_want.test = test_reg(false, 1);
    g_want.alpha = GS_SET_ALPHA(0, 2, 2, 2, 0x80);
    bind_texture(tex, false, TFX_DECAL, false);
    g_want.prim = GS_SET_PRIM(GS_PRIM_SPRITE, 0, 1, 0, 0, 0, 1, 0, 0);
    begin_prims(K_TSPR);
    prim_space(6);
    g_q[0] = GS_SET_RGBAQ(0x80, 0x80, 0x80, 0xFF, 0) | Q_ONE;
    g_q[1] = uv(fuv(u0), fuv(v0));
    g_q[2] = xyz(fx(x0), fx(y0), 0);
    g_q[3] = uv(fuv(u1), fuv(v1));
    g_q[4] = xyz(fx(x1), fx(y1), 0);
    g_q += 5;
    g_tag_count++;
    g_drawn_since_flush = true;
    bool special = is_special(blend);
    for (int i = 0; i < 256; i++) {
        uint32_t e = lut[i];
        if (!special) {
            e = gs_from_premul(e);
        } else if (blend == Blend::Multiply) {
            uint32_t a = e >> 24, out = 0x80000000u;
            for (int k = 0; k < 3; k++) out |= std::min<uint32_t>(((e >> (8 * k)) & 255) + 255 - a, 255) << (8 * k);
            e = out;
        } else {
            e = (e & 0xFFFFFFu) | 0x80000000u;
        }
        g_work_clut[clut_slot(i)] = e;
    }
    SyncDCache(g_work_clut, g_work_clut + 256);
    upload_clut(P_CLUT_WORK, g_work_clut);
    shuffle(g_temp2_tex, ch, P_CLUT_WORK, P_TEMP, FULL_FBW, b, 0, false, GS_SET_ALPHA(0, 2, 2, 2, 0x80));
    Texture& t = surf(g_target);
    uint32_t keep = g_protect | ALPHA_MASK;
    if (g_stencil == Stencil::Write) blit_surface(g_temp_tex, t.vram, t.tbw, b, GS_SET_ALPHA(0, 1, 0, 1, 0), true, 0xFFFFFFFFu, 1, true, true);
    if (!special) {
        blit_surface(g_temp_tex, t.vram, t.tbw, b, alpha_of(blend), true, keep, stencil_ztst(), true);
    } else {
        g_layer_open = true;
        g_layer_blend = blend == Blend::Screen ? Blend::MulSub : blend == Blend::Multiply ? Blend::MulSrc : blend;
        g_layer_box = b;
        resolve_layer();
        if (blend == Blend::Screen) blit_surface(g_temp_tex, t.vram, t.tbw, b, GS_SET_ALPHA(0, 2, 2, 1, 0x80), true, keep, stencil_ztst());
    }
    apply_target_regs();
}

}
