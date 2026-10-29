#include "atrac3.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CHANNELS 2
#define FRAME 1024
#define SPAN 256
#define BLOCK (SPAN * 2)
#define BANDS 4
#define BFUS 32
#define SELS 8
#define UNIT_BYTES 192
#define UNIT_BITS (UNIT_BYTES * 8)
#define DECODER_DELAY 69
#define GAIN_BANDS 3
#define GAIN_STEP 8
#define GAIN_WINDOWS (SPAN / GAIN_STEP)
#define MAX_SHIFT 11
#define ATTACK_RATIO 4.0f
#define ATTACK_FLOOR 1e-3f
#define MDCT_SCALE -128.0f
#define FULL_SCALE 9.5e8
#define MASK_OFFSET_DB 12.0
#define SPREAD_UP_DB 12.0
#define SPREAD_DOWN_DB 27.0
#define NO_BITS 0xFFFF

static const double PI = 3.14159265358979323846;

static const float QMF_HALF[24] = {-0.00001461907f, -0.00009205479f, -0.000056157569f, 0.00030117269f, 0.0002422519f, -0.00085293897f,
                                   -0.0005205574f,  0.0020340169f,   0.00078333891f,   -0.0042153862f, -0.00075614988f, 0.0078402944f,
                                   -0.000061169922f, -0.01344162f,   0.0024626821f,    0.021736089f,   -0.007801671f,   -0.034090221f,
                                   0.01880949f,      0.054326009f,   -0.043596379f,    -0.099384367f,  0.13207909f,     0.46424159f};

static const uint16_t BFU_EDGE[BFUS + 1] = {0,   8,   16,  24,  32,  40,  48,  56,  64,  80,  96,  112, 128, 144, 160, 176, 192,
                                            224, 256, 288, 320, 352, 384, 416, 448, 480, 512, 576, 640, 704, 768, 896, 1024};

static const uint8_t CLC_BITS[SELS] = {0, 4, 3, 3, 4, 4, 5, 6};
static const float MAX_QUANT[SELS] = {0.0f, 1.5f, 2.5f, 3.5f, 4.5f, 7.5f, 15.5f, 31.5f};
static const int LIMIT[SELS] = {0, 1, 2, 3, 4, 7, 15, 31};
static const uint8_t PAIR_LEN[9] = {1, 3, 3, 4, 4, 5, 5, 5, 5};
static const uint8_t PAIR_OF[9] = {8, 4, 7, 2, 0, 1, 6, 3, 5};
static const uint8_t CLC_PAIR[3] = {3, 0, 1};

typedef struct {
    uint8_t lo, hi, len;
} VlcGroup;

static const uint8_t ZERO_LEN[SELS] = {0, 0, 1, 1, 1, 2, 3, 3};
static const VlcGroup GROUPS[SELS][6] = {
    {{0, 0, 0}},
    {{0, 0, 0}},
    {{1, 2, 3}, {0, 0, 0}},
    {{1, 1, 3}, {2, 3, 4}, {0, 0, 0}},
    {{1, 1, 3}, {2, 2, 4}, {3, 4, 5}, {0, 0, 0}},
    {{1, 1, 3}, {2, 3, 4}, {7, 7, 4}, {4, 4, 5}, {5, 6, 6}, {0, 0, 0}},
    {{1, 3, 4}, {15, 15, 4}, {4, 6, 5}, {7, 10, 6}, {11, 14, 7}, {0, 0, 0}},
    {{31, 31, 4}, {1, 5, 5}, {6, 13, 6}, {14, 20, 7}, {21, 30, 8}, {0, 0, 0}},
};

typedef struct {
    uint16_t code[SELS][64];
    uint8_t len[SELS][64];
    float sf[64];
    float window[BLOCK];
    float qmf[48];
    float tw_re[64], tw_im[64];
    float pre_re[128], pre_im[128];
    float post_re[128], post_im[128];
    uint8_t rev[128];
    double ath[BFUS];
    double spread[BFUS][BFUS];
} Tables;

typedef struct {
    uint8_t points, level, loc;
} Gain;

typedef struct {
    float* band[BANDS];
    Gain* gain[BANDS];
} Channel;

typedef struct {
    uint8_t sf;
    float dist;
    uint16_t bits[2];
} Choice;

typedef struct {
    uint8_t sel[BFUS];
    int mode;
    double dist;
} Plan;

typedef struct {
    uint8_t* p;
    int at;
} Bits;

static void add_code(Tables* t, int sel, int index, int len, uint64_t* acc) {
    t->len[sel][index] = (uint8_t)len;
    t->code[sel][index] = (uint16_t)(*acc >> (32 - len));
    *acc += (uint64_t)1 << (32 - len);
}

static double bark(double f) { return 13.0 * atan(0.00076 * f) + 3.5 * atan((f / 7500.0) * (f / 7500.0)); }

static double ath_db(double f) {
    double k = f / 1000.0;
    if (k < 0.02) k = 0.02;
    double v = 3.64 * pow(k, -0.8) - 6.5 * exp(-0.6 * (k - 3.3) * (k - 3.3)) + 1e-3 * k * k * k * k;
    return v > 120.0 ? 120.0 : v;
}

static void tables_init(Tables* t, uint32_t rate) {
    for (int sel = 1; sel < SELS; sel++) {
        uint64_t acc = 0;
        if (sel == 1) {
            for (int s = 0; s < 9; s++) add_code(t, 1, s, PAIR_LEN[s], &acc);
            continue;
        }
        add_code(t, sel, 31, ZERO_LEN[sel], &acc);
        for (const VlcGroup* g = GROUPS[sel]; g->len; g++) {
            for (int m = g->lo; m <= g->hi; m++) {
                add_code(t, sel, 31 + m, g->len, &acc);
                add_code(t, sel, 31 - m, g->len, &acc);
            }
        }
    }
    for (int i = 0; i < 64; i++) t->sf[i] = (float)pow(2.0, (i - 15) / 3.0);
    for (int i = 0; i < SPAN; i++) {
        float w = (float)(1.0 - cos(PI * (i + 0.5) / SPAN));
        t->window[i] = w;
        t->window[BLOCK - 1 - i] = w;
    }
    for (int i = 0; i < 24; i++) t->qmf[i] = t->qmf[47 - i] = QMF_HALF[i] * 2.0f;
    for (int k = 0; k < 64; k++) {
        t->tw_re[k] = (float)cos(-2.0 * PI * k / 128);
        t->tw_im[k] = (float)sin(-2.0 * PI * k / 128);
    }
    for (int m = 0; m < 128; m++) {
        double a = -PI * (4 * m + 1) / (4.0 * SPAN), b = -PI * m / SPAN;
        t->pre_re[m] = (float)cos(a);
        t->pre_im[m] = (float)sin(a);
        t->post_re[m] = (float)cos(b);
        t->post_im[m] = (float)sin(b);
        int r = 0;
        for (int bit = 0; bit < 7; bit++) r |= ((m >> bit) & 1) << (6 - bit);
        t->rev[m] = (uint8_t)r;
    }
    double z[BFUS];
    for (int i = 0; i < BFUS; i++) {
        double f = (BFU_EDGE[i] + BFU_EDGE[i + 1]) * 0.5 * rate / (2.0 * FRAME);
        z[i] = bark(f);
        t->ath[i] = FULL_SCALE * pow(10.0, (ath_db(f) - 96.0) / 10.0);
    }
    for (int i = 0; i < BFUS; i++) {
        for (int j = 0; j < BFUS; j++) {
            double dz = z[i] - z[j];
            double db = dz >= 0 ? -SPREAD_UP_DB * dz : SPREAD_DOWN_DB * dz;
            t->spread[i][j] = pow(10.0, (db - MASK_OFFSET_DB) / 10.0);
        }
    }
}

static void qmf_split(const float* qmf, const float* in, size_t n, float* lo, float* hi) {
    for (size_t k = 0; k < n / 2; k++) {
        float a = 0, b = 0;
        size_t top = k < 23 ? k : 23;
        for (size_t q = 0; q <= top; q++) {
            const float* x = in + 2 * (k - q);
            a += qmf[2 * q + 1] * x[0];
            b += qmf[2 * q] * x[1];
        }
        lo[k] = 0.5f * (a + b);
        hi[k] = 0.5f * (b - a);
    }
}

static void fft128(const Tables* t, float* re, float* im) {
    for (int i = 0; i < 128; i++) {
        int j = t->rev[i];
        if (j > i) {
            float r = re[i], m = im[i];
            re[i] = re[j];
            im[i] = im[j];
            re[j] = r;
            im[j] = m;
        }
    }
    for (int size = 2; size <= 128; size <<= 1) {
        int half = size >> 1, step = 128 / size;
        for (int start = 0; start < 128; start += size) {
            for (int k = 0; k < half; k++) {
                float wr = t->tw_re[k * step], wi = t->tw_im[k * step];
                int a = start + k, b = a + half;
                float xr = re[b] * wr - im[b] * wi, xi = re[b] * wi + im[b] * wr;
                re[b] = re[a] - xr;
                im[b] = im[a] - xi;
                re[a] += xr;
                im[a] += xi;
            }
        }
    }
}

static void mdct(const Tables* t, const float* z, float* out) {
    float u[SPAN], re[128], im[128];
    for (int i = 0; i < 128; i++) {
        u[i] = -z[383 - i] - z[384 + i];
        u[128 + i] = z[i] - z[255 - i];
    }
    for (int m = 0; m < 128; m++) {
        float a = u[2 * m], b = u[SPAN - 1 - 2 * m];
        re[m] = a * t->pre_re[m] - b * t->pre_im[m];
        im[m] = a * t->pre_im[m] + b * t->pre_re[m];
    }
    fft128(t, re, im);
    for (int k = 0; k < 128; k++) {
        float sr = re[k] * t->post_re[k] - im[k] * t->post_im[k];
        float si = re[k] * t->post_im[k] + im[k] * t->post_re[k];
        out[2 * k] = sr * MDCT_SCALE;
        out[SPAN - 1 - 2 * k] = -si * MDCT_SCALE;
    }
}

static float gain_at(const Gain* g, int pos) {
    if (!g->points) return 1.0f;
    int start = g->loc * GAIN_STEP;
    if (pos >= start + GAIN_STEP) return 1.0f;
    float lev = exp2f((float)(4 - g->level));
    if (pos < start) return lev;
    return lev * exp2f((float)(g->level - 4) * (float)(pos - start) / GAIN_STEP);
}

static void plan_gain(const float* band, size_t len, Gain* g, int regions) {
    size_t windows = len / GAIN_STEP;
    float* peak = xmalloc(windows * sizeof(float));
    for (size_t w = 0; w < windows; w++) {
        float m = 0;
        for (int i = 0; i < GAIN_STEP; i++) m = fmaxf(m, fabsf(band[w * GAIN_STEP + (size_t)i]));
        peak[w] = m;
    }
    memset(g, 0, sizeof(Gain) * (size_t)regions);
    float prev = 0;
    for (int r = 1; r < regions; r++) {
        size_t first = (size_t)(r - 1) * GAIN_WINDOWS;
        float before = prev, best = 0;
        int best_j = 0;
        for (int j = 1; j <= GAIN_WINDOWS && first + (size_t)j + 1 < windows; j++) {
            before = fmaxf(before, peak[first + (size_t)j - 1]);
            float after = fmaxf(peak[first + (size_t)j], peak[first + (size_t)j + 1]);
            if (after < ATTACK_FLOOR) continue;
            float ratio = before > after / 4096.0f ? after / before : 4096.0f;
            if (ratio > best) {
                best = ratio;
                best_j = j;
            }
        }
        if (best >= ATTACK_RATIO) {
            int shift = (int)floorf(log2f(best));
            g[r].points = 1;
            g[r].level = (uint8_t)(4 + (shift > MAX_SHIFT ? MAX_SHIFT : shift));
            g[r].loc = (uint8_t)(best_j - 1);
        }
        prev = 0;
        const float* base = band + (size_t)(r - 1) * SPAN;
        for (int i = 0; i < SPAN; i++) prev = fmaxf(prev, fabsf(base[i]) / gain_at(&g[r], i));
    }
    free(peak);
}

static void spectrum(const Tables* t, const Channel* ch, size_t f, float* spec) {
    float z[BLOCK], x[SPAN];
    for (int b = 0; b < BANDS; b++) {
        const float* band = ch->band[b];
        const Gain* now = &ch->gain[b][f];
        const Gain* next = &ch->gain[b][f + 1];
        float scale = gain_at(next, 0);
        int shaped = now->points || next->points;
        for (int n = 0; n < BLOCK; n++) {
            float v = f > 0 || n >= SPAN ? band[f * SPAN + (size_t)n - SPAN] : 0.0f;
            if (shaped) v /= n < SPAN ? gain_at(now, n) * scale : gain_at(next, n - SPAN);
            z[n] = v * t->window[n];
        }
        mdct(t, z, x);
        float* dst = spec + b * SPAN;
        for (int i = 0; i < SPAN; i++) dst[i] = b & 1 ? x[SPAN - 1 - i] : x[i];
    }
}

static float quantize(const float* x, int n, float sf, int sel, int8_t* q) {
    float inv = MAX_QUANT[sel] / sf, step = sf / MAX_QUANT[sel];
    int lim = LIMIT[sel];
    float d = 0;
    for (int i = 0; i < n; i++) {
        int v = (int)lrintf(x[i] * inv);
        if (v > lim) v = lim;
        if (v < -lim) v = -lim;
        q[i] = (int8_t)v;
        float r = x[i] - (float)v * step;
        d += r * r;
    }
    return d;
}

static int vlc_bits(const Tables* t, const int8_t* q, int n, int sel) {
    int bits = 0;
    if (sel == 1) {
        for (int i = 0; i < n; i += 2) bits += PAIR_LEN[PAIR_OF[(q[i] + 1) * 3 + q[i + 1] + 1]];
    } else {
        for (int i = 0; i < n; i++) bits += t->len[sel][q[i] + 31];
    }
    return bits;
}

static void analyze(const Tables* t, const float* spec, Choice c[BFUS][SELS], double* weight) {
    int8_t q[128];
    double energy[BFUS];
    for (int i = 0; i < BFUS; i++) {
        const float* x = spec + BFU_EDGE[i];
        int n = BFU_EDGE[i + 1] - BFU_EDGE[i];
        float peak = 0, en = 0;
        for (int k = 0; k < n; k++) {
            peak = fmaxf(peak, fabsf(x[k]));
            en += x[k] * x[k];
        }
        energy[i] = en;
        c[i][0].sf = 0;
        c[i][0].dist = en;
        c[i][0].bits[0] = c[i][0].bits[1] = 0;
        int s0 = 0;
        while (s0 < 63 && t->sf[s0] < peak) s0++;
        for (int sel = 1; sel < SELS; sel++) {
            Choice* ch = &c[i][sel];
            if (peak <= 0) {
                ch->sf = 0;
                ch->dist = 0;
                ch->bits[0] = ch->bits[1] = NO_BITS;
                continue;
            }
            int best = s0;
            float best_d = quantize(x, n, t->sf[s0], sel, q);
            for (int s = s0 - 1; s >= 0 && s >= s0 - 2; s--) {
                float d = quantize(x, n, t->sf[s], sel, q);
                if (d < best_d) {
                    best_d = d;
                    best = s;
                }
            }
            quantize(x, n, t->sf[best], sel, q);
            ch->sf = (uint8_t)best;
            ch->dist = best_d;
            ch->bits[0] = (uint16_t)vlc_bits(t, q, n, sel);
            ch->bits[1] = (uint16_t)(sel == 1 ? n * 2 : n * CLC_BITS[sel]);
        }
    }
    for (int i = 0; i < BFUS; i++) {
        double mask = t->ath[i];
        for (int j = 0; j < BFUS; j++) mask += energy[j] * t->spread[i][j];
        weight[i] = 1.0 / mask;
    }
}

static int unit_cost(const Choice* c, int sel, int mode) { return sel ? c[sel].bits[mode] + 6 : 0; }

static int plan_bits(Choice c[BFUS][SELS], const uint8_t* sel, int mode) {
    int last = 0, bits = 0;
    for (int i = 0; i < BFUS; i++) {
        if (sel[i]) last = i;
        bits += unit_cost(c[i], sel[i], mode);
    }
    return bits + 3 * (last + 1);
}

static void pick(Choice c[BFUS][SELS], const double* weight, int mode, double lambda, uint8_t* sel) {
    for (int i = 0; i < BFUS; i++) {
        double best = c[i][0].dist * weight[i];
        sel[i] = 0;
        for (int s = 1; s < SELS; s++) {
            if (c[i][s].bits[mode] == NO_BITS) continue;
            double v = c[i][s].dist * weight[i] + lambda * unit_cost(c[i], s, mode);
            if (v < best) {
                best = v;
                sel[i] = (uint8_t)s;
            }
        }
    }
}

static void allocate(Choice c[BFUS][SELS], const double* weight, int mode, int budget, Plan* plan) {
    uint8_t sel[BFUS];
    double lo = 1e-12, hi = 1e12;
    pick(c, weight, mode, lo, sel);
    if (plan_bits(c, sel, mode) > budget) {
        for (int it = 0; it < 80; it++) {
            double mid = sqrt(lo * hi);
            pick(c, weight, mode, mid, sel);
            if (plan_bits(c, sel, mode) > budget) lo = mid;
            else hi = mid;
        }
        pick(c, weight, mode, hi, sel);
    }
    for (;;) {
        int room = budget - plan_bits(c, sel, mode);
        int bi = -1, bs = 0;
        double best = 0;
        for (int i = 0; i < BFUS; i++) {
            for (int s = 1; s < SELS; s++) {
                if (s == sel[i] || c[i][s].bits[mode] == NO_BITS) continue;
                double gain = (c[i][sel[i]].dist - c[i][s].dist) * weight[i];
                if (gain <= 0) continue;
                uint8_t keep = sel[i];
                sel[i] = (uint8_t)s;
                int need = plan_bits(c, sel, mode) - (budget - room);
                sel[i] = keep;
                if (need > room) continue;
                double score = need <= 0 ? 1e300 : gain / need;
                if (score > best) {
                    best = score;
                    bi = i;
                    bs = s;
                }
            }
        }
        if (bi < 0) break;
        sel[bi] = (uint8_t)bs;
    }
    double dist = 0;
    for (int i = 0; i < BFUS; i++) dist += c[i][sel[i]].dist * weight[i];
    memcpy(plan->sel, sel, sizeof(sel));
    plan->mode = mode;
    plan->dist = dist;
}

static void put(Bits* b, int n, uint32_t v) {
    for (int i = n - 1; i >= 0; i--) {
        if ((v >> i) & 1) b->p[b->at >> 3] |= (uint8_t)(0x80 >> (b->at & 7));
        b->at++;
    }
}

static void write_unit(const Tables* t, const float* spec, Choice c[BFUS][SELS], const Plan* plan, const Gain* const* gains, uint8_t* out) {
    Bits b = {out, 0};
    put(&b, 6, 0x28);
    put(&b, 2, BANDS - 1);
    for (int band = 0; band < BANDS; band++) {
        const Gain* g = gains[band];
        put(&b, 3, g->points);
        if (g->points) {
            put(&b, 4, g->level);
            put(&b, 5, g->loc);
        }
    }
    put(&b, 5, 0);
    int last = 0;
    for (int i = 0; i < BFUS; i++) {
        if (plan->sel[i]) last = i;
    }
    put(&b, 5, (uint32_t)last);
    put(&b, 1, (uint32_t)plan->mode);
    for (int i = 0; i <= last; i++) put(&b, 3, plan->sel[i]);
    for (int i = 0; i <= last; i++) {
        if (plan->sel[i]) put(&b, 6, c[i][plan->sel[i]].sf);
    }
    int8_t q[128];
    for (int i = 0; i <= last; i++) {
        int sel = plan->sel[i];
        if (!sel) continue;
        int n = BFU_EDGE[i + 1] - BFU_EDGE[i];
        quantize(spec + BFU_EDGE[i], n, t->sf[c[i][sel].sf], sel, q);
        if (plan->mode) {
            if (sel == 1) {
                for (int k = 0; k < n; k += 2) put(&b, 4, (uint32_t)(CLC_PAIR[q[k] + 1] << 2 | CLC_PAIR[q[k + 1] + 1]));
            } else {
                for (int k = 0; k < n; k++) put(&b, CLC_BITS[sel], (uint32_t)q[k] & ((1u << CLC_BITS[sel]) - 1));
            }
        } else if (sel == 1) {
            for (int k = 0; k < n; k += 2) {
                int s = PAIR_OF[(q[k] + 1) * 3 + q[k + 1] + 1];
                put(&b, t->len[1][s], t->code[1][s]);
            }
        } else {
            for (int k = 0; k < n; k++) put(&b, t->len[sel][q[k] + 31], t->code[sel][q[k] + 31]);
        }
    }
}

static void encode_unit(const Tables* t, const Channel* ch, size_t f, uint8_t* out) {
    float spec[FRAME];
    Choice c[BFUS][SELS];
    double weight[BFUS];
    spectrum(t, ch, f, spec);
    analyze(t, spec, c, weight);
    const Gain* gains[BANDS];
    int gain_bits = 0;
    for (int b = 0; b < BANDS; b++) {
        gains[b] = &ch->gain[b][f + 1];
        gain_bits += 3 + 9 * gains[b]->points;
    }
    int budget = UNIT_BITS - (6 + 2 + gain_bits + 5 + 5 + 1);
    Plan vlc, clc;
    allocate(c, weight, 0, budget, &vlc);
    allocate(c, weight, 1, budget, &clc);
    write_unit(t, spec, c, clc.dist < vlc.dist ? &clc : &vlc, gains, out);
}

static void put_tag(Buf* b, const char* tag, uint32_t len) {
    buf_put(b, tag, 4);
    buf_u32(b, len);
}

int atrac3_encode(const float* pcm, size_t frames, uint32_t rate, int32_t loop_start, int32_t loop_end, Buf* out) {
    if (!frames || !rate) return 0;
    size_t units = (frames + ATRAC3_DELAY + FRAME - 1) / FRAME;
    size_t len = units * FRAME;
    Tables* t = xcalloc(1, sizeof(Tables));
    tables_init(t, rate);
    Channel ch[CHANNELS];
    float* x = xmalloc(len * sizeof(float));
    float* lo = xmalloc(len / 2 * sizeof(float));
    float* hi = xmalloc(len / 2 * sizeof(float));
    for (int c = 0; c < CHANNELS; c++) {
        for (size_t i = 0; i < len; i++) x[i] = i < frames ? pcm[i * CHANNELS + (size_t)c] : 0.0f;
        for (int b = 0; b < BANDS; b++) {
            ch[c].band[b] = xmalloc(len / 4 * sizeof(float));
            ch[c].gain[b] = xcalloc(units + 1, sizeof(Gain));
        }
        qmf_split(t->qmf, x, len, lo, hi);
        qmf_split(t->qmf, lo, len / 2, ch[c].band[0], ch[c].band[1]);
        qmf_split(t->qmf, hi, len / 2, ch[c].band[3], ch[c].band[2]);
        for (int b = 0; b < GAIN_BANDS; b++) plan_gain(ch[c].band[b], len / 4, ch[c].gain[b], (int)units + 1);
    }
    free(x);
    free(lo);
    free(hi);
    uint32_t skip = ATRAC3_DELAY - DECODER_DELAY;
    uint32_t data = (uint32_t)(units * UNIT_BYTES * CHANNELS);
    int looped = loop_start >= 0 && loop_end > loop_start;
    out->len = 0;
    put_tag(out, "RIFF", 0);
    buf_put(out, "WAVE", 4);
    put_tag(out, "fmt ", 32);
    buf_u16(out, 0x0270);
    buf_u16(out, CHANNELS);
    buf_u32(out, rate);
    buf_u32(out, (uint32_t)((uint64_t)rate * UNIT_BYTES * CHANNELS / FRAME));
    buf_u16(out, UNIT_BYTES * CHANNELS);
    buf_u16(out, 0);
    buf_u16(out, 14);
    buf_u16(out, 1);
    buf_u32(out, FRAME * CHANNELS * 2);
    buf_u16(out, 0);
    buf_u16(out, 0);
    buf_u16(out, 1);
    buf_u16(out, 0);
    put_tag(out, "fact", 8);
    buf_u32(out, (uint32_t)frames);
    buf_u32(out, skip);
    if (looped) {
        put_tag(out, "smpl", 60);
        buf_u32(out, 0);
        buf_u32(out, 0);
        buf_u32(out, 1000000000u / rate);
        buf_u32(out, 60);
        buf_u32(out, 0);
        buf_u32(out, 0);
        buf_u32(out, 0);
        buf_u32(out, 1);
        buf_u32(out, 24);
        buf_u32(out, 0);
        buf_u32(out, 0);
        buf_u32(out, (uint32_t)loop_start + skip);
        buf_u32(out, (uint32_t)loop_end + skip);
        buf_u32(out, 0);
        buf_u32(out, 0);
    }
    put_tag(out, "data", data);
    size_t at = out->len;
    buf_zero(out, data);
    for (size_t f = 0; f < units; f++) {
        for (int c = 0; c < CHANNELS; c++) encode_unit(t, &ch[c], f, out->data + at + (f * CHANNELS + (size_t)c) * UNIT_BYTES);
    }
    uint32_t riff = (uint32_t)(out->len - 8);
    for (int i = 0; i < 4; i++) out->data[4 + i] = (uint8_t)(riff >> (8 * i));
    for (int c = 0; c < CHANNELS; c++) {
        for (int b = 0; b < BANDS; b++) {
            free(ch[c].band[b]);
            free(ch[c].gain[b]);
        }
    }
    free(t);
    return 1;
}
