#include "bitmap.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>

#include "../platform/gfx.h"
#include "../platform/platform.h"
#include "text.h"

namespace g2d {

static int phys(double v) { return (int)std::nearbyint(v * S); }

Canvas::Canvas(int w, int h) : lw(std::max(1, w)), lh(std::max(1, h)) {
    pw = std::max(1, (int)std::nearbyint(lw * S));
    ph = std::max(1, (int)std::nearbyint(lh * S));
    stride = (pw + 15) & ~15;
    size_t bytes = (size_t)stride * ph * 4;
    px = profile::CANVAS_HEAP ? (uint32_t*)std::malloc(bytes) : nullptr;
    if (!px) px = (uint32_t*)images::alloc_pixels(bytes);
    if (!px) {
        plat::log("canvas %dx%d skipped: out of memory", pw, ph);
        pw = ph = 1;
        stride = 16;
        bytes = (size_t)stride * 4;
        px = (uint32_t*)images::alloc_pixels(bytes);
        if (!px) throw std::bad_alloc();
    }
    std::memset(px, 0, bytes);
    img.ow = lw;
    img.oh = lh;
    img.w = pw;
    img.h = ph;
    img.fx = (float)pw / lw;
    img.fy = (float)ph / lh;
    img.bytes = bytes;
    for (int cy = 0; cy < ph; cy += 512) {
        for (int cx = 0; cx < pw; cx += 512) {
            ImageChunk c{};
            c.x = cx;
            c.y = cy;
            c.w = std::min(512, pw - cx);
            c.h = std::min(512, ph - cy);
            c.tex = px ? gfx::texture_wrap(c.w, c.h, stride, px + (size_t)cy * stride + cx) : nullptr;
            img.chunks.push_back(c);
        }
    }
}

Canvas::~Canvas() {
    for (auto& c : img.chunks) gfx::texture_destroy(c.tex);
    img.chunks.clear();
    gfx::release(px);
}

void Canvas::touch() {
    if (!dirty) return;
    dirty = false;
    for (auto& c : img.chunks) gfx::texture_touch(c.tex);
}

BitmapPtr Bitmap::create(int w, int h) {
    BitmapPtr b(new Bitmap());
    b->lw_ = std::max(0, w);
    b->lh_ = std::max(0, h);
    b->canvas_.reset(new Canvas(w, h));
    return b;
}

BitmapPtr Bitmap::load(const std::string& key) {
    BitmapPtr b(new Bitmap());
    b->key_ = images::lower(key);
    Image* img = images::get_path(b->key_);
    if (img) {
        b->lw_ = img->ow;
        b->lh_ = img->oh;
    } else if (!images::waiting_for_memory(b->key_) || !images::logical_size_path(b->key_, b->lw_, b->lh_)) {
        b->missing_ = true;
    }
    return b;
}

BitmapPtr Bitmap::empty() { return create(1, 1); }

void Bitmap::warm() {
    if (profile::BACKGROUND_DECODE && !canvas_ && !key_.empty() && !missing_) images::warm_path(key_);
}

int Bitmap::width() const { return lw_; }
int Bitmap::height() const { return lh_; }

Image* Bitmap::image() {
    if (canvas_) return &canvas_->img;
    if (key_.empty() || missing_) return nullptr;
    return images::get_path_cached(key_, image_handle_);
}

float Bitmap::fx() {
    Image* i = image();
    return i ? i->fx : S;
}

float Bitmap::fy() {
    Image* i = image();
    return i ? i->fy : S;
}

void Bitmap::touch() {
    if (canvas_) canvas_->touch();
}

void Bitmap::ensure_canvas() {
    if (canvas_) return;
    Image* img = image();
    canvas_.reset(new Canvas(lw_ > 0 ? lw_ : 1, lh_ > 0 ? lh_ : 1));
    if (img) images::read_rect(img, 0, 0, std::min(img->w, canvas_->pw), std::min(img->h, canvas_->ph), canvas_->px, (size_t)canvas_->stride);
    key_.clear();
    canvas_->dirty = true;
}

void Bitmap::resize(int w, int h) {
    lw_ = std::max(0, w);
    lh_ = std::max(0, h);
    key_.clear();
    missing_ = false;
    canvas_.reset(new Canvas(lw_, lh_));
    version++;
}

static inline uint32_t div255(uint32_t v) { return (v + 1 + (v >> 8)) >> 8; }

static inline uint32_t div255x2(uint32_t v) { return ((v + 0x00010001u + ((v >> 8) & 0x00FF00FFu)) >> 8) & 0x00FF00FFu; }
static inline uint32_t clamp255x2(uint32_t v) { return (v | (((v >> 8) & 0x00010001u) * 255u)) & 0x00FF00FFu; }

static inline uint32_t over(uint32_t d, uint32_t s) {
    uint32_t sa = s >> 24;
    if (sa == 255 || d == 0) return s;
    if (sa == 0 && (s & 0xFFFFFF) == 0) return d;
    uint32_t inv = 255 - sa;
    uint32_t rb = clamp255x2((s & 0x00FF00FFu) + div255x2((d & 0x00FF00FFu) * inv + 0x007F007Fu));
    uint32_t ga = clamp255x2(((s >> 8) & 0x00FF00FFu) + div255x2(((d >> 8) & 0x00FF00FFu) * inv + 0x007F007Fu));
    return rb | (ga << 8);
}

static inline uint32_t scale_px(uint32_t p, int k) {
    if (k >= 255) return p;
    uint32_t r = div255((p & 255) * k + 127), g = div255(((p >> 8) & 255) * k + 127), b = div255(((p >> 16) & 255) * k + 127),
             a = div255((p >> 24) * k + 127);
    return r | (g << 8) | (b << 16) | (a << 24);
}

void Bitmap::fill_physical(int x0, int y0, int x1, int y1, uint32_t premul, bool copy) {
    ensure_canvas();
    Canvas& c = *canvas_;
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(c.pw, x1);
    y1 = std::min(c.ph, y1);
    if (x0 >= x1 || y0 >= y1) return;
    for (int y = y0; y < y1; y++) {
        uint32_t* row = c.row(y);
        if (copy) {
            for (int x = x0; x < x1; x++) row[x] = premul;
        } else {
            for (int x = x0; x < x1; x++) row[x] = over(row[x], premul);
        }
    }
    c.dirty = true;
    version++;
}

void Bitmap::clear() {
    ensure_canvas();
    Canvas& c = *canvas_;
    std::memset(c.px, 0, (size_t)c.stride * c.ph * 4);
    c.dirty = true;
    version++;
}

void Bitmap::clear_rect(double x, double y, double w, double h) { fill_physical(phys(x), phys(y), phys(x + w), phys(y + h), 0, true); }

void Bitmap::fill_all(const std::string& color) { fill_rect(0, 0, lw_, lh_, color); }

void Bitmap::fill_rect(double x, double y, double w, double h, const std::string& color) {
    Color c = parse_color(color);
    fill_physical(phys(x), phys(y), phys(x + w), phys(y + h), premul_rgba(c, paint_opacity / 255.0), false);
}

void Bitmap::stroke_rect(double x, double y, double w, double h, const std::string& color) {
    Color c = parse_color(color);
    uint32_t p = premul_rgba(c, paint_opacity / 255.0);
    int x0 = phys(x), y0 = phys(y), x1 = phys(x + w), y1 = phys(y + h);
    fill_physical(x0, y0, x1, y0 + 1, p, false);
    fill_physical(x0, y1 - 1, x1, y1, p, false);
    fill_physical(x0, y0 + 1, x0 + 1, y1 - 1, p, false);
    fill_physical(x1 - 1, y0 + 1, x1, y1 - 1, p, false);
}

void Bitmap::gradient_fill_rect(double x, double y, double w, double h, const std::string& c1, const std::string& c2, bool vertical) {
    ensure_canvas();
    Color a = parse_color(c1), b = parse_color(c2);
    uint32_t pa = premul_rgba(a, paint_opacity / 255.0), pb = premul_rgba(b, paint_opacity / 255.0);
    int x0 = phys(x), y0 = phys(y), x1 = phys(x + w), y1 = phys(y + h);
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    Canvas& c = *canvas_;
    int span = vertical ? (y1 - y0) : (x1 - x0);
    if (span <= 0) return;
    std::vector<uint32_t> ramp((size_t)span);
    for (int pos = 0; pos < span; pos++) {
        double t = span > 1 ? (pos + 0.5) / span : 0;
        uint32_t out = 0;
        for (int sh = 0; sh < 32; sh += 8) {
            double va = (pa >> sh) & 255, vb = (pb >> sh) & 255;
            out |= (uint32_t)std::lround(va + (vb - va) * t) << sh;
        }
        ramp[(size_t)pos] = out;
    }
    const int xs = std::max(0, x0), xe = std::min(c.pw, x1), ye = std::min(c.ph, y1);
    for (int yy = std::max(0, y0); yy < ye; yy++) {
        uint32_t* row = c.row(yy);
        if (vertical) {
            const uint32_t v = ramp[(size_t)(yy - y0)];
            for (int xx = xs; xx < xe; xx++) row[xx] = over(row[xx], v);
        } else {
            const uint32_t* r = ramp.data();
            for (int xx = xs; xx < xe; xx++) row[xx] = over(row[xx], r[xx - x0]);
        }
    }
    c.dirty = true;
    version++;
}

void Bitmap::draw_circle(double x, double y, double r, const std::string& color) {
    ensure_canvas();
    Color col = parse_color(color);
    double cx = x * S, cy = y * S, rr = r * S;
    Canvas& c = *canvas_;
    int x0 = std::max(0, (int)std::floor(cx - rr - 1)), x1 = std::min(c.pw, (int)std::ceil(cx + rr + 1));
    int y0 = std::max(0, (int)std::floor(cy - rr - 1)), y1 = std::min(c.ph, (int)std::ceil(cy + rr + 1));
    for (int yy = y0; yy < y1; yy++) {
        uint32_t* row = c.row(yy);
        for (int xx = x0; xx < x1; xx++) {
            double d = std::hypot(xx + 0.5 - cx, yy + 0.5 - cy);
            double cov = std::clamp(rr - d + 0.5, 0.0, 1.0);
            if (cov <= 0) continue;
            row[xx] = over(row[xx], premul_rgba(col, cov * paint_opacity / 255.0));
        }
    }
    c.dirty = true;
    version++;
}

uint32_t Bitmap::pixel_premul(int px, int py) {
    if (canvas_) {
        if (px < 0 || py < 0 || px >= canvas_->pw || py >= canvas_->ph) return 0;
        return canvas_->row(py)[px];
    }
    uint32_t v = 0;
    Image* img = image();
    if (img) images::read_pixel(img, px, py, &v);
    return v;
}

struct PixelSource {
    const uint32_t* px = nullptr;
    int stride = 0;
    int x0 = 0, y0 = 0, w = 0, h = 0;
    std::vector<uint32_t> buf;
    uint32_t at(int x, int y) const {
        x -= x0;
        y -= y0;
        if (x < 0 || y < 0 || x >= w || y >= h) return 0;
        return px[(size_t)y * stride + x];
    }
};

struct DecodedImage {
    const Image* img = nullptr;
    uint64_t generation = 0;
    uint64_t last = 0;
    std::vector<uint32_t> px;
};

static std::vector<DecodedImage> g_decoded;
static bool g_decoding = false;
static uint64_t g_decode_clock = 0;
constexpr size_t DECODED_MAX_IMAGE = 256 * 1024;
constexpr size_t DECODED_BUDGET = 1024 * 1024;

static const DecodedImage* decoded_image_locked(Image* img) {
    if ((size_t)img->w * img->h * 4 > DECODED_MAX_IMAGE) return nullptr;
    uint64_t gen = images::generation();
    g_decode_clock++;
    for (auto it = g_decoded.begin(); it != g_decoded.end();) {
        if (it->generation != gen && it->img == img) {
            it = g_decoded.erase(it);
            continue;
        }
        if (it->img == img) {
            it->last = g_decode_clock;
            it->generation = gen;
            return &*it;
        }
        ++it;
    }
    size_t need = (size_t)img->w * img->h * 4, total = need;
    for (auto& d : g_decoded) total += d.px.size() * 4;
    while (total > DECODED_BUDGET && !g_decoded.empty()) {
        auto oldest = g_decoded.begin();
        for (auto it = g_decoded.begin(); it != g_decoded.end(); ++it) {
            if (it->last < oldest->last) oldest = it;
        }
        total -= oldest->px.size() * 4;
        g_decoded.erase(oldest);
    }
    DecodedImage d;
    d.img = img;
    d.generation = gen;
    d.last = g_decode_clock;
    d.px.assign((size_t)img->w * img->h, 0);
    images::read_rect(img, 0, 0, img->w, img->h, d.px.data(), (size_t)img->w);
    g_decoded.push_back(std::move(d));
    return &g_decoded.back();
}

static const DecodedImage* decoded_image(Image* img) {
    g_decoding = true;
    const DecodedImage* d = nullptr;
    try {
        d = decoded_image_locked(img);
    } catch (...) {
        g_decoding = false;
        throw;
    }
    g_decoding = false;
    return d;
}

void release_decoded_sources() {
    if (g_decoding) return;
    std::vector<DecodedImage>().swap(g_decoded);
}

static void make_source(PixelSource& out, Canvas* canvas, Image* img, int rx0, int ry0, int rx1, int ry1) {
    if (canvas) {
        out.px = canvas->px;
        out.stride = canvas->stride;
        out.w = canvas->pw;
        out.h = canvas->ph;
        return;
    }
    if (const DecodedImage* d = decoded_image(img)) {
        out.px = d->px.data();
        out.stride = img->w;
        out.w = img->w;
        out.h = img->h;
        return;
    }
    rx0 = std::max(0, rx0);
    ry0 = std::max(0, ry0);
    rx1 = std::min(img->w, rx1);
    ry1 = std::min(img->h, ry1);
    if (rx1 <= rx0 || ry1 <= ry0) return;
    out.x0 = rx0;
    out.y0 = ry0;
    out.w = rx1 - rx0;
    out.h = ry1 - ry0;
    out.stride = out.w;
    out.buf.assign((size_t)out.w * out.h, 0);
    images::read_rect(img, rx0, ry0, out.w, out.h, out.buf.data(), (size_t)out.w);
    out.px = out.buf.data();
}

static inline uint32_t lerp_px(uint32_t c00, uint32_t c10, uint32_t c01, uint32_t c11, int ax, int ay) {
    uint32_t out = 0;
    int iax = 256 - ax, iay = 256 - ay;
    for (int s = 0; s < 32; s += 8) {
        int a = (c00 >> s) & 255, b = (c10 >> s) & 255, c = (c01 >> s) & 255, d = (c11 >> s) & 255;
        int top = a * iax + b * ax, bot = c * iax + d * ax;
        int v = (top * iay + bot * ay + 32768) >> 16;
        out |= (uint32_t)std::clamp(v, 0, 255) << s;
    }
    return out;
}

void Bitmap::blt(Bitmap* src, double sx, double sy, double sw, double sh, double dx, double dy, double dw, double dh) {
    if (!src) return;
    if (dw <= 0) dw = sw;
    if (dh <= 0) dh = sh;
    if (!(sx >= 0 && sy >= 0 && sw > 0 && sh > 0 && dw > 0 && dh > 0 && sx + sw <= src->width() && sy + sh <= src->height())) return;
    Image* simg = src->image();
    if (!simg) return;
    ensure_canvas();
    if (src == this) {
        BitmapPtr copy = Bitmap::create(lw_, lh_);
        copy->blt(this, 0, 0, lw_, lh_, 0, 0);
        blt(copy.get(), sx, sy, sw, sh, dx, dy, dw, dh);
        return;
    }
    Canvas& c = *canvas_;
    float sfx = simg->fx, sfy = simg->fy;
    double psx0 = sx * sfx, psy0 = sy * sfy, psx1 = (sx + sw) * sfx, psy1 = (sy + sh) * sfy;
    int dx0 = phys(dx), dy0 = phys(dy), dx1 = phys(dx + dw), dy1 = phys(dy + dh);
    if (dx1 <= dx0 || dy1 <= dy0) return;
    int k = std::clamp(paint_opacity, 0, 255);
    int cx0 = std::max(0, dx0), cx1 = std::min(c.pw, dx1), cy0 = std::max(0, dy0), cy1 = std::min(c.ph, dy1);
    if (cx1 <= cx0 || cy1 <= cy0) return;
    PixelSource source;
    bool unscaled = std::fabs((psx1 - psx0) - (dx1 - dx0)) < 0.75 && std::fabs((psy1 - psy0) - (dy1 - dy0)) < 0.75;
    if (unscaled) {
        int ox = (int)std::nearbyint(psx0) - dx0, oy = (int)std::nearbyint(psy0) - dy0;
        int xa = std::max(cx0, -ox), xb = std::min(cx1, simg->w - ox);
        int ya = std::max(cy0, -oy), yb = std::min(cy1, simg->h - oy);
        if (xb <= xa || yb <= ya) return;
        make_source(source, src->canvas_.get(), simg, xa + ox, ya + oy, xb + ox, yb + oy);
        for (int y = ya; y < yb; y++) {
            uint32_t* row = c.row(y);
            int syy = y + oy;
            if (k >= 255) {
                for (int x = xa; x < xb; x++) row[x] = over(row[x], source.at(x + ox, syy));
            } else {
                for (int x = xa; x < xb; x++) row[x] = over(row[x], scale_px(source.at(x + ox, syy), k));
            }
        }
    } else {
        double ku = (psx1 - psx0) / (dx1 - dx0), kv = (psy1 - psy0) / (dy1 - dy0);
        int w = simg->w, h = simg->h;
        size_t nx = (size_t)(cx1 - cx0);
        std::vector<int> col0(nx), col1(nx), colw(nx);
        for (int x = cx0; x < cx1; x++) {
            float fu = (float)(psx0 + (x - dx0 + 0.5) * ku) - 0.5f;
            int x0 = (int)std::floor(fu);
            int ax = (int)((fu - (float)x0) * 256.0f + 0.5f);
            if (ax >= 256) {
                ax = 0;
                x0++;
            }
            size_t i = (size_t)(x - cx0);
            col0[i] = std::clamp(x0, 0, w - 1);
            col1[i] = std::clamp(x0 + 1, 0, w - 1);
            colw[i] = ax;
        }
        int ymin = h, ymax = -1;
        std::vector<int> row0((size_t)(cy1 - cy0)), row1((size_t)(cy1 - cy0)), roww((size_t)(cy1 - cy0));
        for (int y = cy0; y < cy1; y++) {
            float fv = (float)(psy0 + (y - dy0 + 0.5) * kv) - 0.5f;
            int y0 = (int)std::floor(fv);
            int ay = (int)((fv - (float)y0) * 256.0f + 0.5f);
            if (ay >= 256) {
                ay = 0;
                y0++;
            }
            size_t i = (size_t)(y - cy0);
            row0[i] = std::clamp(y0, 0, h - 1);
            row1[i] = std::clamp(y0 + 1, 0, h - 1);
            roww[i] = ay;
            ymin = std::min(ymin, row0[i]);
            ymax = std::max(ymax, row1[i]);
        }
        int xmin = *std::min_element(col0.begin(), col0.end()), xmax = *std::max_element(col1.begin(), col1.end());
        make_source(source, src->canvas_.get(), simg, xmin, ymin, xmax + 1, ymax + 1);
        for (int y = cy0; y < cy1; y++) {
            size_t ry = (size_t)(y - cy0);
            uint32_t* row = c.row(y);
            int y0 = row0[ry], y1 = row1[ry], ay = roww[ry];
            for (int x = cx0; x < cx1; x++) {
                size_t i = (size_t)(x - cx0);
                uint32_t p = lerp_px(source.at(col0[i], y0), source.at(col1[i], y0), source.at(col0[i], y1), source.at(col1[i], y1), colw[i], ay);
                row[x] = over(row[x], scale_px(p, k));
            }
        }
    }
    c.dirty = true;
    version++;
}

void Bitmap::draw_text(const std::string& text, double x, double y, double max_width, double line_height, const std::string& align) {
    if (text_blocked || text.empty()) return;
    ensure_canvas();
    Canvas& c = *canvas_;
    bool outlined = outline_width > 0;
    FontFace* f = font_for(font_face, font_size, outlined);
    if (!f) return;
    double scale = (double)font_size / f->size;
    std::vector<uint32_t> cps = utf8_codepoints(text);
    double width = 0;
    for (uint32_t cp : cps) width += f->advance(cp) * scale;
    if (max_width <= 0) max_width = 4294967295.0;
    double squeeze = width > max_width ? max_width / width : 1.0;
    double drawn = width * squeeze;
    double tx = x;
    if (align == "center") tx = x + max_width / 2 - drawn / 2;
    else if (align == "right") tx = x + max_width - drawn;
    double baseline = y + line_height - (line_height - font_size * 0.7) / 2;
    Color tc = parse_color(text_color);
    Color oc = parse_color(outline_color);
    double po = std::clamp(paint_opacity, 0, 255) / 255.0;
    double fa = tc.a * po;
    double oa = outlined ? oc.a : 0;
    const float fa_f = (float)fa, oa_f = (float)oa;
    const float tcr = (float)tc.r, tcg = (float)tc.g, tcb = (float)tc.b;
    const float ocr = (float)oc.r, ocg = (float)oc.g, ocb = (float)oc.b;
    auto round_pos = [](float v) { return (uint32_t)(int)(v + 0.5f); };
    const uint16_t* atlas = f->pixels();
    if (!atlas) return;
    struct TextInk {
        uint32_t key;
        uint32_t stroke;
        uint32_t fill;
        uint32_t solo;
    };
    struct InkSet {
        float from[9];
        uint32_t count;
        uint64_t used;
        std::vector<TextInk> slots;
    };
    constexpr int INK_BITS = 10;
    constexpr uint32_t INK_SLOTS = 1u << INK_BITS, INK_EMPTY = 0xFFFFFFFFu;
    static InkSet ink_sets[4];
    static uint64_t ink_clock = 0;
    const bool outline_font = f->outline;
    const float ink_now[9] = {fa_f, oa_f, tcr, tcg, tcb, ocr, ocg, ocb, outline_font ? 1.0f : 0.0f};
    InkSet* set = nullptr;
    for (InkSet& e : ink_sets) {
        if (!e.slots.empty() && std::equal(ink_now, ink_now + 9, e.from)) set = &e;
    }
    if (!set) {
        set = &ink_sets[0];
        for (InkSet& e : ink_sets) {
            if (e.used < set->used) set = &e;
        }
        std::copy(ink_now, ink_now + 9, set->from);
        set->slots.assign(INK_SLOTS, TextInk{INK_EMPTY, 0, 0, 0});
        set->count = 0;
    }
    set->used = ++ink_clock;
    TextInk* inks = set->slots.data();
    const int atlas_stride = f->atlas_w;
    const int cpw = c.pw, cph = c.ph;
    struct ColumnSample {
        int s0, s1;
        float t;
    };
    static std::vector<ColumnSample> columns;
    double pen = tx;
    double top_phys = (baseline - f->ascent * scale) * S;
    for (uint32_t cp : cps) {
        const GlyphInfo* g = f->glyph(cp);
        if (!g) continue;
        double gx = pen * S;
        double base = std::floor(gx);
        int ph = (int)std::lround((gx - base) * 3.0);
        if (ph >= 3) {
            ph = 0;
            base += 1;
        }
        const GlyphCell& q = g->phase[ph];
        if (q.w > 0 && q.h > 0) {
            const int qw = q.w, qh = q.h, qx = q.x, qy = q.y;
            int py0 = (int)std::nearbyint(top_phys) + (int)q.oy;
            double px0 = base + q.ox;
            int dw = squeeze < 1.0 ? std::max(1, (int)std::lround(qw * squeeze * scale)) : (int)std::lround(qw * scale);
            int dh = (int)std::lround(qh * scale);
            if (scale == 1.0) dh = qh;
            if (squeeze >= 1.0 && scale == 1.0) dw = qw;
            const int ipx = (int)px0;
            const int x_lo = std::max(0, -ipx), x_hi = std::min(dw, cpw - ipx);
            const bool direct = dw == qw;
            if (!direct && x_lo < x_hi) {
                columns.resize((size_t)dw);
                for (int xx = x_lo; xx < x_hi; xx++) {
                    float su = (xx + 0.5f) * qw / dw - 0.5f;
                    float fl = std::floor(su);
                    int s0 = std::clamp((int)fl, 0, qw - 1), s1 = std::clamp(s0 + 1, 0, qw - 1);
                    columns[(size_t)xx] = {s0, s1, su - fl};
                }
            }
            for (int yy = 0; yy < dh; yy++) {
                int ty = py0 + yy;
                if (ty < 0 || ty >= cph) continue;
                int sy = dh == qh ? yy : std::min(qh - 1, (int)((yy + 0.5) * qh / dh));
                uint32_t* row = c.row(ty);
                const uint16_t* srow = atlas + (size_t)(qy + sy) * atlas_stride + qx;
                for (int xx = x_lo; xx < x_hi; xx++) {
                    uint32_t sp;
                    if (direct) {
                        sp = srow[xx];
                    } else {
                        const ColumnSample& col = columns[(size_t)xx];
                        uint32_t a = srow[col.s0], b = srow[col.s1];
                        if (!(a | b)) continue;
                        float t = col.t;
                        sp = 0;
                        for (int s = 0; s < 16; s += 8) {
                            float va = (float)((a >> s) & 255), vb = (float)((b >> s) & 255);
                            sp |= (uint32_t)std::clamp((int)round_pos(va + (vb - va) * t), 0, 255) << s;
                        }
                    }
                    int alpha = (int)(sp >> 8);
                    if (!alpha) continue;
                    uint32_t slot = (sp * 0x9E3779B1u) >> (32 - INK_BITS);
                    while (inks[slot].key != sp && inks[slot].key != INK_EMPTY) slot = (slot + 1) & (INK_SLOTS - 1);
                    if (inks[slot].key != sp) {
                        if (++set->count > INK_SLOTS * 3 / 4) {
                            std::fill(inks, inks + INK_SLOTS, TextInk{INK_EMPTY, 0, 0, 0});
                            set->count = 1;
                            slot = (sp * 0x9E3779B1u) >> (32 - INK_BITS);
                        }
                        TextInk& ink = inks[slot];
                        ink.key = sp;
                        int lum = (int)(sp & 255);
                        float fill, stroke;
                        if (outline_font) {
                            float total = alpha / 255.0f;
                            fill = std::min(1.0f, (float)lum / (float)alpha);
                            stroke = fill < 1.0f ? std::clamp((total - fill) / (1.0f - fill), 0.0f, 1.0f) : 1.0f;
                        } else {
                            fill = alpha / 255.0f;
                            stroke = 0;
                        }
                        ink.stroke = 0;
                        if (oa_f > 0 && stroke > 0) {
                            float a1 = oa_f * stroke;
                            ink.stroke = round_pos(ocr * a1) | (round_pos(ocg * a1) << 8) | (round_pos(ocb * a1) << 16) | (round_pos(255 * a1) << 24);
                        }
                        ink.fill = 0;
                        if (fill > 0) {
                            float a2 = fa_f * fill;
                            ink.fill = round_pos(tcr * a2) | (round_pos(tcg * a2) << 8) | (round_pos(tcb * a2) << 16) | (round_pos(255 * a2) << 24);
                        }
                        ink.solo = over(ink.stroke, ink.fill);
                    }
                    const TextInk& ink = inks[slot];
                    uint32_t& dst = row[ipx + xx];
                    uint32_t d = dst;
                    if (!d) {
                        dst = ink.solo;
                        continue;
                    }
                    if (ink.stroke) d = over(d, ink.stroke);
                    if (ink.fill) d = over(d, ink.fill);
                    dst = d;
                }
            }
        }
        pen += g->adv * scale * squeeze;
    }
    c.dirty = true;
    version++;
}

void Bitmap::fill_rows(const std::function<void(int y, uint32_t* row, int w)>& fill) {
    ensure_canvas();
    Canvas& c = *canvas_;
    for (int y = 0; y < c.ph; y++) fill(y, c.row(y), c.pw);
    c.dirty = true;
    version++;
}

void Bitmap::write_pixels(const uint32_t* src, int pw, int ph) {
    ensure_canvas();
    Canvas& c = *canvas_;
    for (int y = 0; y < std::min(ph, c.ph); y++) std::memcpy(c.row(y), src + (size_t)y * pw, sizeof(uint32_t) * (size_t)std::min(pw, c.pw));
    c.dirty = true;
    version++;
}

int Bitmap::measure_text_width(const std::string& text) { return (int)std::lround(measure_text(font_face, font_size, text)); }

std::string Bitmap::get_pixel(int x, int y) {
    Image* img = image();
    if (!img) return "#000000";
    uint32_t p = pixel_premul(phys(x), phys(y));
    int a = (int)(p >> 24);
    if (!a) return "#000000";
    int r = std::min(255, (int)(p & 255) * 255 / a), g = std::min(255, (int)((p >> 8) & 255) * 255 / a), b = std::min(255, (int)((p >> 16) & 255) * 255 / a);
    return css_hex(r, g, b);
}

int Bitmap::get_alpha_pixel(int x, int y) { return (int)(pixel_premul(phys(x), phys(y)) >> 24); }

void Bitmap::adjust_tone(int r, int g, int b) {
    if (!r && !g && !b) return;
    ensure_canvas();
    Canvas& c = *canvas_;
    for (int y = 0; y < c.ph; y++) {
        uint32_t* row = c.row(y);
        for (int x = 0; x < c.pw; x++) {
            uint32_t p = row[x];
            int a = (int)(p >> 24);
            if (!a) continue;
            int pr = std::clamp((int)(p & 255) + r * a / 255, 0, a), pg = std::clamp((int)((p >> 8) & 255) + g * a / 255, 0, a),
                pb = std::clamp((int)((p >> 16) & 255) + b * a / 255, 0, a);
            row[x] = (uint32_t)pr | ((uint32_t)pg << 8) | ((uint32_t)pb << 16) | ((uint32_t)a << 24);
        }
    }
    c.dirty = true;
    version++;
}

void Bitmap::rotate_hue(int offset) {
    offset = ((offset % 360) + 360) % 360;
    if (!offset) return;
    ensure_canvas();
    Canvas& c = *canvas_;
    for (int y = 0; y < c.ph; y++) {
        uint32_t* row = c.row(y);
        for (int x = 0; x < c.pw; x++) {
            uint32_t p = row[x];
            int a = (int)(p >> 24);
            if (!a) continue;
            double r = (p & 255), g = ((p >> 8) & 255), b = ((p >> 16) & 255);
            double cmin = std::min(r, std::min(g, b)), cmax = std::max(r, std::max(g, b));
            double h = 0, l = (cmin + cmax) / 2, delta = cmax - cmin, s = 0;
            if (delta > 0) {
                if (r == cmax) h = 60 * std::fmod((g - b) / delta + 6, 6);
                else if (g == cmax) h = 60 * ((b - r) / delta + 2);
                else h = 60 * ((r - g) / delta + 4);
                s = delta / (255 - std::fabs(2 * l - 255));
            }
            h = std::fmod(h + offset, 360.0);
            double cc = (255 - std::fabs(2 * l - 255)) * s;
            double xx = cc * (1 - std::fabs(std::fmod(h / 60, 2) - 1));
            double m = l - cc / 2;
            double rr, gg, bb;
            if (h < 60) { rr = cc; gg = xx; bb = 0; }
            else if (h < 120) { rr = xx; gg = cc; bb = 0; }
            else if (h < 180) { rr = 0; gg = cc; bb = xx; }
            else if (h < 240) { rr = 0; gg = xx; bb = cc; }
            else if (h < 300) { rr = xx; gg = 0; bb = cc; }
            else { rr = cc; gg = 0; bb = xx; }
            int nr = std::clamp((int)std::lround(rr + m), 0, a), ng = std::clamp((int)std::lround(gg + m), 0, a),
                nb = std::clamp((int)std::lround(bb + m), 0, a);
            row[x] = (uint32_t)nr | ((uint32_t)ng << 8) | ((uint32_t)nb << 16) | ((uint32_t)a << 24);
        }
    }
    c.dirty = true;
    version++;
}

void Bitmap::blur() {
    ensure_canvas();
    Canvas& c = *canvas_;
    int w = c.pw, h = c.ph;
    if (w <= 0 || h <= 0) return;
    uint32_t* rb = (uint32_t*)images::alloc_pixels((size_t)w * h * 4);
    uint32_t* g = (uint32_t*)images::alloc_pixels((size_t)w * h * 4);
    if (!rb || !g) {
        gfx::release(rb);
        gfx::release(g);
        return;
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int y = 0; y < h; y++) {
            const uint32_t* row = c.row(y);
            uint32_t* orb = &rb[(size_t)y * w];
            uint32_t* og = &g[(size_t)y * w];
            for (int x = 0; x < w; x++) {
                uint32_t a = row[x > 0 ? x - 1 : 0], b = row[x], d = row[x + 1 < w ? x + 1 : w - 1];
                orb[x] = (a & 0xFF00FFu) + (b & 0xFF00FFu) + (d & 0xFF00FFu);
                og[x] = ((a >> 8) & 0xFFu) + ((b >> 8) & 0xFFu) + ((d >> 8) & 0xFFu);
            }
        }
        for (int y = 0; y < h; y++) {
            const uint32_t* rb0 = &rb[(size_t)(y > 0 ? y - 1 : 0) * w];
            const uint32_t* rb1 = &rb[(size_t)y * w];
            const uint32_t* rb2 = &rb[(size_t)(y + 1 < h ? y + 1 : h - 1) * w];
            const uint32_t* g0 = &g[(size_t)(y > 0 ? y - 1 : 0) * w];
            const uint32_t* g1 = &g[(size_t)y * w];
            const uint32_t* g2 = &g[(size_t)(y + 1 < h ? y + 1 : h - 1) * w];
            uint32_t* row = c.row(y);
            for (int x = 0; x < w; x++) {
                uint32_t srb = rb0[x] + rb1[x] + rb2[x];
                uint32_t sr = srb & 0xFFFFu, sb = srb >> 16, sg = g0[x] + g1[x] + g2[x];
                row[x] = ((sr * 7282) >> 16) | (((sg * 7282) >> 16) << 8) | (((sb * 7282) >> 16) << 16) | (255u << 24);
            }
        }
    }
    gfx::release(rb);
    gfx::release(g);
    c.dirty = true;
    version++;
}

}
