#include "jpeg.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t ZIGZAG[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
                                   41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
                                   30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static const uint8_t STD_LUMA[64] = {16, 11, 10, 16, 24,  40,  51,  61,  12, 12, 14, 19, 26,  58,  60,  55,  14, 13, 16, 24,  40,  57,
                                     69, 56, 14, 17, 22,  29,  51,  87,  80, 62, 18, 22, 37,  56,  68,  109, 103, 77, 24, 35, 55,  64,
                                     81, 104, 113, 92, 49, 64, 78,  87,  103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99};

static const uint8_t STD_CHROMA[64] = {17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99,
                                       99, 99, 47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
                                       99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99};

static const float AAN[8] = {1.0f, 1.387039845f, 1.306562965f, 1.175875602f, 1.0f, 0.785694958f, 0.541196100f, 0.275899379f};

typedef struct {
    uint8_t bits[17];
    uint8_t vals[256];
    int nvals;
    uint16_t code[256];
    uint8_t len[256];
} Huff;

typedef struct {
    Buf* out;
    uint32_t acc;
    int nbits;
} BitWriter;

static void fdct8(float* d, int step) {
    float t0 = d[0] + d[7 * step], t7 = d[0] - d[7 * step];
    float t1 = d[step] + d[6 * step], t6 = d[step] - d[6 * step];
    float t2 = d[2 * step] + d[5 * step], t5 = d[2 * step] - d[5 * step];
    float t3 = d[3 * step] + d[4 * step], t4 = d[3 * step] - d[4 * step];
    float t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
    d[0] = t10 + t11;
    d[4 * step] = t10 - t11;
    float z1 = (t12 + t13) * 0.707106781f;
    d[2 * step] = t13 + z1;
    d[6 * step] = t13 - z1;
    t10 = t4 + t5;
    t11 = t5 + t6;
    t12 = t6 + t7;
    float z5 = (t10 - t12) * 0.382683433f;
    float z2 = 0.541196100f * t10 + z5;
    float z4 = 1.306562965f * t12 + z5;
    float z3 = t11 * 0.707106781f;
    float z11 = t7 + z3, z13 = t7 - z3;
    d[5 * step] = z13 + z2;
    d[3 * step] = z13 - z2;
    d[step] = z11 + z4;
    d[7 * step] = z11 - z4;
}

static void scale_table(const uint8_t* base, int quality, uint8_t* table, float* div) {
    if (quality < 1) quality = 1;
    if (quality > 100) quality = 100;
    int scale = quality < 50 ? 5000 / quality : 200 - quality * 2;
    for (int i = 0; i < 64; i++) {
        int v = (base[i] * scale + 50) / 100;
        if (v < 1) v = 1;
        if (v > 255) v = 255;
        table[i] = (uint8_t)v;
    }
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) div[r * 8 + c] = 1.0f / (table[r * 8 + c] * AAN[r] * AAN[c] * 8.0f);
    }
}

static void quantize(float* blk, const float* div, int16_t* out) {
    for (int r = 0; r < 8; r++) fdct8(blk + r * 8, 1);
    for (int c = 0; c < 8; c++) fdct8(blk + c, 8);
    for (int k = 0; k < 64; k++) {
        int n = ZIGZAG[k];
        out[k] = (int16_t)lrintf(blk[n] * div[n]);
    }
}

static int bit_size(int v) {
    if (v < 0) v = -v;
    int n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

static void count_block(const int16_t* b, int prev_dc, uint32_t* dc_freq, uint32_t* ac_freq) {
    dc_freq[bit_size(b[0] - prev_dc)]++;
    int run = 0;
    for (int k = 1; k < 64; k++) {
        if (!b[k]) {
            run++;
            continue;
        }
        while (run > 15) {
            ac_freq[0xF0]++;
            run -= 16;
        }
        ac_freq[(run << 4) | bit_size(b[k])]++;
        run = 0;
    }
    if (run) ac_freq[0x00]++;
}

static void build_huff(const uint32_t* freq_in, Huff* h) {
    long freq[257];
    int codesize[257], others[257];
    for (int i = 0; i < 256; i++) freq[i] = (long)freq_in[i];
    freq[256] = 1;
    for (int i = 0; i < 257; i++) {
        codesize[i] = 0;
        others[i] = -1;
    }
    for (;;) {
        int c1 = -1, c2 = -1;
        long v1 = 0x7FFFFFFFL, v2 = 0x7FFFFFFFL;
        for (int i = 0; i <= 256; i++) {
            if (freq[i] && freq[i] <= v1) {
                v1 = freq[i];
                c1 = i;
            }
        }
        for (int i = 0; i <= 256; i++) {
            if (freq[i] && freq[i] <= v2 && i != c1) {
                v2 = freq[i];
                c2 = i;
            }
        }
        if (c2 < 0) break;
        freq[c1] += freq[c2];
        freq[c2] = 0;
        codesize[c1]++;
        while (others[c1] >= 0) {
            c1 = others[c1];
            codesize[c1]++;
        }
        others[c1] = c2;
        codesize[c2]++;
        while (others[c2] >= 0) {
            c2 = others[c2];
            codesize[c2]++;
        }
    }
    int bits[33];
    memset(bits, 0, sizeof(bits));
    for (int i = 0; i <= 256; i++) {
        if (codesize[i]) bits[codesize[i] > 32 ? 32 : codesize[i]]++;
    }
    for (int i = 32; i > 16; i--) {
        while (bits[i] > 0) {
            int j = i - 2;
            while (bits[j] == 0) j--;
            bits[i] -= 2;
            bits[i - 1]++;
            bits[j + 1] += 2;
            bits[j]--;
        }
    }
    int top = 16;
    while (bits[top] == 0) top--;
    bits[top]--;
    memset(h, 0, sizeof(*h));
    for (int i = 1; i <= 16; i++) h->bits[i] = (uint8_t)bits[i];
    int p = 0;
    for (int len = 1; len <= 32; len++) {
        for (int s = 0; s < 256; s++) {
            if (codesize[s] == len) h->vals[p++] = (uint8_t)s;
        }
    }
    h->nvals = p;
    int code = 0, k = 0;
    for (int len = 1; len <= 16; len++) {
        for (int i = 0; i < h->bits[len]; i++) {
            h->code[h->vals[k]] = (uint16_t)code;
            h->len[h->vals[k]] = (uint8_t)len;
            code++;
            k++;
        }
        code <<= 1;
    }
}

static void put_bits(BitWriter* w, uint32_t v, int n) {
    w->acc = (w->acc << n) | (v & ((1u << n) - 1));
    w->nbits += n;
    while (w->nbits >= 8) {
        uint8_t byte = (uint8_t)(w->acc >> (w->nbits - 8));
        buf_u8(w->out, byte);
        if (byte == 0xFF) buf_u8(w->out, 0);
        w->nbits -= 8;
    }
    w->acc &= (1u << w->nbits) - 1;
}

static void put_value(BitWriter* w, int v, int size) {
    if (!size) return;
    put_bits(w, (uint32_t)(v < 0 ? v + (1 << size) - 1 : v), size);
}

static void write_block(BitWriter* w, const int16_t* b, int prev_dc, const Huff* dc, const Huff* ac) {
    int diff = b[0] - prev_dc;
    int s = bit_size(diff);
    put_bits(w, dc->code[s], dc->len[s]);
    put_value(w, diff, s);
    int run = 0;
    for (int k = 1; k < 64; k++) {
        if (!b[k]) {
            run++;
            continue;
        }
        while (run > 15) {
            put_bits(w, ac->code[0xF0], ac->len[0xF0]);
            run -= 16;
        }
        s = bit_size(b[k]);
        int sym = (run << 4) | s;
        put_bits(w, ac->code[sym], ac->len[sym]);
        put_value(w, b[k], s);
        run = 0;
    }
    if (run) put_bits(w, ac->code[0x00], ac->len[0x00]);
}

static void marker(Buf* out, uint8_t m, int len) {
    buf_u8(out, 0xFF);
    buf_u8(out, m);
    buf_u8(out, (uint8_t)(len >> 8));
    buf_u8(out, (uint8_t)len);
}

static void write_dht(Buf* out, int cls, int id, const Huff* h) {
    marker(out, 0xC4, 2 + 1 + 16 + h->nvals);
    buf_u8(out, (uint8_t)((cls << 4) | id));
    buf_put(out, h->bits + 1, 16);
    buf_put(out, h->vals, (size_t)h->nvals);
}

int jpeg_encode_rgb(const uint8_t* rgb, int w, int h, int quality, Buf* out) {
    if (w < 1 || h < 1 || w > 65535 || h > 65535) return 0;
    uint8_t qt[2][64];
    float div[2][64];
    scale_table(STD_LUMA, quality, qt[0], div[0]);
    scale_table(STD_CHROMA, quality, qt[1], div[1]);
    int mx = (w + 15) / 16, my = (h + 15) / 16;
    size_t nblocks = (size_t)mx * my * 6;
    int16_t* blocks = malloc(nblocks * 64 * sizeof(int16_t));
    if (!blocks) return 0;
    float ys[256], cb[256], cr[256];
    size_t bi = 0;
    for (int by = 0; by < my; by++) {
        for (int bx = 0; bx < mx; bx++) {
            for (int y = 0; y < 16; y++) {
                int sy = by * 16 + y;
                if (sy >= h) sy = h - 1;
                for (int x = 0; x < 16; x++) {
                    int sx = bx * 16 + x;
                    if (sx >= w) sx = w - 1;
                    const uint8_t* p = rgb + ((size_t)sy * w + sx) * 3;
                    float r = p[0], g = p[1], b = p[2];
                    ys[y * 16 + x] = 0.299f * r + 0.587f * g + 0.114f * b - 128.0f;
                    cb[y * 16 + x] = -0.168735892f * r - 0.331264108f * g + 0.5f * b;
                    cr[y * 16 + x] = 0.5f * r - 0.418687589f * g - 0.081312411f * b;
                }
            }
            for (int k = 0; k < 4; k++) {
                float blk[64];
                int ox = (k & 1) * 8, oy = (k >> 1) * 8;
                for (int y = 0; y < 8; y++) {
                    for (int x = 0; x < 8; x++) blk[y * 8 + x] = ys[(oy + y) * 16 + ox + x];
                }
                quantize(blk, div[0], blocks + (bi++) * 64);
            }
            float bcb[64], bcr[64];
            for (int y = 0; y < 8; y++) {
                for (int x = 0; x < 8; x++) {
                    int i = y * 2 * 16 + x * 2;
                    bcb[y * 8 + x] = (cb[i] + cb[i + 1] + cb[i + 16] + cb[i + 17]) * 0.25f;
                    bcr[y * 8 + x] = (cr[i] + cr[i + 1] + cr[i + 16] + cr[i + 17]) * 0.25f;
                }
            }
            quantize(bcb, div[1], blocks + (bi++) * 64);
            quantize(bcr, div[1], blocks + (bi++) * 64);
        }
    }
    uint32_t dcf[2][256], acf[2][256];
    memset(dcf, 0, sizeof(dcf));
    memset(acf, 0, sizeof(acf));
    int prev[3] = {0, 0, 0};
    for (size_t i = 0; i < nblocks; i++) {
        int k = (int)(i % 6);
        int comp = k < 4 ? 0 : k - 3;
        int t = comp ? 1 : 0;
        const int16_t* b = blocks + i * 64;
        count_block(b, prev[comp], dcf[t], acf[t]);
        prev[comp] = b[0];
    }
    Huff hdc[2], hac[2];
    for (int t = 0; t < 2; t++) {
        build_huff(dcf[t], &hdc[t]);
        build_huff(acf[t], &hac[t]);
    }
    buf_put(out, "\xFF\xD8", 2);
    marker(out, 0xE0, 16);
    buf_put(out, "JFIF\0\x01\x01\0\0\x01\0\x01\0\0", 14);
    marker(out, 0xDB, 2 + 65 * 2);
    for (int t = 0; t < 2; t++) {
        buf_u8(out, (uint8_t)t);
        for (int k = 0; k < 64; k++) buf_u8(out, qt[t][ZIGZAG[k]]);
    }
    marker(out, 0xC0, 17);
    buf_u8(out, 8);
    buf_u8(out, (uint8_t)(h >> 8));
    buf_u8(out, (uint8_t)h);
    buf_u8(out, (uint8_t)(w >> 8));
    buf_u8(out, (uint8_t)w);
    buf_u8(out, 3);
    buf_put(out, "\x01\x22\x00\x02\x11\x01\x03\x11\x01", 9);
    write_dht(out, 0, 0, &hdc[0]);
    write_dht(out, 1, 0, &hac[0]);
    write_dht(out, 0, 1, &hdc[1]);
    write_dht(out, 1, 1, &hac[1]);
    marker(out, 0xDA, 12);
    buf_u8(out, 3);
    buf_put(out, "\x01\x00\x02\x11\x03\x11\x00\x3F\x00", 9);
    BitWriter bw = {out, 0, 0};
    prev[0] = prev[1] = prev[2] = 0;
    for (size_t i = 0; i < nblocks; i++) {
        int k = (int)(i % 6);
        int comp = k < 4 ? 0 : k - 3;
        int t = comp ? 1 : 0;
        const int16_t* b = blocks + i * 64;
        write_block(&bw, b, prev[comp], &hdc[t], &hac[t]);
        prev[comp] = b[0];
    }
    if (bw.nbits) put_bits(&bw, 0x7F, 8 - bw.nbits);
    buf_put(out, "\xFF\xD9", 2);
    free(blocks);
    return 1;
}
