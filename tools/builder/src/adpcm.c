#include "adpcm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

static const int K0[5] = {0, 60, 115, 98, 122};
static const int K1[5] = {0, 0, -52, -55, -60};

static inline int32_t clamp16(int32_t v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

static inline int32_t predict(int f, int32_t h1, int32_t h2) { return (h1 * K0[f] + h2 * K1[f] + 32) >> 6; }

#define MAX_SHIFT 8

void adpcm_decode_block(const uint8_t* block, int32_t* h1, int32_t* h2, int16_t* out, int stride) {
    int f = block[0] >> 4;
    int shift = block[0] & 15;
    if (f > 4) f = 0;
    if (shift > MAX_SHIFT) shift = MAX_SHIFT;
    int32_t a = *h1, b = *h2;
    for (int i = 0; i < ADPCM_BLOCK_FRAMES; i++) {
        int32_t v = clamp16((int32_t)(int8_t)block[2 + i] * (1 << shift) + predict(f, a, b));
        out[i * stride] = (int16_t)v;
        b = a;
        a = v;
    }
    *h1 = a;
    *h2 = b;
}

static int64_t quantize(const int32_t* x, int f, int shift, int32_t h1, int32_t h2, uint8_t* codes, int32_t* end1, int32_t* end2) {
    int64_t err = 0;
    int32_t half = shift ? 1 << (shift - 1) : 0;
    for (int i = 0; i < ADPCM_BLOCK_FRAMES; i++) {
        int32_t p = predict(f, h1, h2);
        int32_t r = x[i] - p;
        int32_t q = r >= 0 ? (r + half) >> shift : -((-r + half) >> shift);
        if (q > 127) q = 127;
        if (q < -128) q = -128;
        int32_t v = clamp16(q * (1 << shift) + p);
        int64_t d = (int64_t)(v - x[i]);
        err += d * d;
        codes[i] = (uint8_t)(int8_t)q;
        h2 = h1;
        h1 = v;
    }
    *end1 = h1;
    *end2 = h2;
    return err;
}

static int shift_for(int32_t peak) {
    int shift = 0;
    while (shift < MAX_SHIFT && (127 << shift) < peak) shift++;
    return shift;
}

static void encode_block(const int32_t* x, int32_t* h1, int32_t* h2, uint8_t* out) {
    int64_t best = -1;
    uint8_t codes[ADPCM_BLOCK_FRAMES], best_codes[ADPCM_BLOCK_FRAMES] = {0};
    int best_f = 0, best_shift = 0;
    int32_t best_h1 = *h1, best_h2 = *h2;
    for (int f = 0; f < 5; f++) {
        int32_t a = *h1, b = *h2, peak = 0;
        for (int i = 0; i < ADPCM_BLOCK_FRAMES; i++) {
            int32_t r = x[i] - predict(f, a, b);
            int32_t mag = r < 0 ? -r : r;
            if (mag > peak) peak = mag;
            b = a;
            a = x[i];
        }
        int base = shift_for(peak);
        for (int s = base - 1; s <= base + 1; s++) {
            if (s < 0 || s > MAX_SHIFT) continue;
            int32_t e1, e2;
            int64_t e = quantize(x, f, s, *h1, *h2, codes, &e1, &e2);
            if (best < 0 || e < best) {
                best = e;
                best_f = f;
                best_shift = s;
                best_h1 = e1;
                best_h2 = e2;
                memcpy(best_codes, codes, sizeof(codes));
            }
        }
    }
    out[0] = (uint8_t)((best_f << 4) | best_shift);
    out[1] = 0;
    memcpy(out + 2, best_codes, sizeof(best_codes));
    *h1 = best_h1;
    *h2 = best_h2;
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

typedef struct {
    int channels;
    int32_t h1[2], h2[2];
    int16_t pend[ADPCM_BLOCK_FRAMES * 2];
    int npend;
    uint32_t blocks;
    uint64_t frames;
    Buf data;
    Buf table;
} Encoder;

static void enc_block(Encoder* e) {
    if (e->blocks % ADPCM_SEEK_BLOCKS == 0) {
        for (int c = 0; c < e->channels; c++) {
            buf_u16(&e->table, (uint16_t)(int16_t)e->h1[c]);
            buf_u16(&e->table, (uint16_t)(int16_t)e->h2[c]);
        }
    }
    int32_t x[ADPCM_BLOCK_FRAMES];
    uint8_t block[ADPCM_BLOCK_BYTES];
    for (int c = 0; c < e->channels; c++) {
        for (int i = 0; i < ADPCM_BLOCK_FRAMES; i++) x[i] = i < e->npend ? e->pend[i * e->channels + c] : 0;
        encode_block(x, &e->h1[c], &e->h2[c], block);
        buf_put(&e->data, block, sizeof(block));
    }
    e->blocks++;
    e->npend = 0;
}

static void enc_push(Encoder* e, const int16_t* pcm, size_t frames) {
    e->frames += frames;
    while (frames) {
        size_t n = (size_t)(ADPCM_BLOCK_FRAMES - e->npend);
        if (n > frames) n = frames;
        memcpy(e->pend + (size_t)e->npend * (size_t)e->channels, pcm, n * (size_t)e->channels * sizeof(int16_t));
        e->npend += (int)n;
        pcm += n * (size_t)e->channels;
        frames -= n;
        if (e->npend == ADPCM_BLOCK_FRAMES) enc_block(e);
    }
}

static int enc_finish(Encoder* e, uint32_t rate, int32_t loop_start, int32_t loop_length, Buf* out) {
    if (e->npend) enc_block(e);
    int ok = e->frames > 0 && e->frames <= 0xFFFFFFFFu;
    if (ok) {
        uint32_t data_at = (uint32_t)(ADPCM_HEADER_BYTES + e->table.len);
        uint8_t h[ADPCM_HEADER_BYTES];
        memset(h, 0, sizeof(h));
        memcpy(h, "OADP", 4);
        h[4] = 1;
        h[5] = (uint8_t)e->channels;
        h[6] = (uint8_t)(ADPCM_SEEK_BLOCKS & 255);
        h[7] = (uint8_t)(ADPCM_SEEK_BLOCKS >> 8);
        put32(h + 8, rate);
        put32(h + 12, (uint32_t)e->frames);
        put32(h + 16, (uint32_t)loop_start);
        put32(h + 20, (uint32_t)loop_length);
        put32(h + 24, (uint32_t)(e->table.len / ((size_t)e->channels * 4)));
        put32(h + 28, data_at);
        out->len = 0;
        buf_reserve(out, (size_t)data_at + e->data.len);
        buf_put(out, h, sizeof(h));
        buf_put(out, e->table.data, e->table.len);
        buf_put(out, e->data.data, e->data.len);
    }
    buf_free(&e->data);
    buf_free(&e->table);
    return ok;
}

int adpcm_encode(const int16_t* pcm, uint32_t frames, int channels, uint32_t rate, int32_t loop_start, int32_t loop_length, Buf* out) {
    if (channels < 1 || channels > 2 || !rate) return 0;
    Encoder e;
    memset(&e, 0, sizeof(e));
    e.channels = channels;
    enc_push(&e, pcm, frames);
    return enc_finish(&e, rate, loop_start, loop_length, out);
}

static int key_is(const char* c, const char* key) {
    for (; *key; c++, key++) {
        char a = *c >= 'a' && *c <= 'z' ? (char)(*c - 32) : *c;
        if (a != *key) return 0;
    }
    return *c == '=';
}

static int32_t comment_int(const stb_vorbis_comment* vc, const char* key) {
    for (int i = 0; i < vc->comment_list_length; i++) {
        const char* c = vc->comment_list[i];
        if (key_is(c, key)) return (int32_t)atol(c + strlen(key) + 1);
    }
    return -1;
}

static uint64_t last_granule(const uint8_t* d, size_t len) {
    for (size_t i = len >= 27 ? len - 27 : 0; i + 27 <= len; i--) {
        if (d[i] == 'O' && d[i + 1] == 'g' && d[i + 2] == 'g' && d[i + 3] == 'S' && d[i + 4] == 0) {
            uint64_t g = 0;
            for (int k = 7; k >= 0; k--) g = (g << 8) | d[i + 6 + k];
            if (g != ~(uint64_t)0) return g;
        }
        if (!i) break;
    }
    return 0;
}

int adpcm_from_ogg(const uint8_t* ogg, size_t len, Buf* out, char* err, size_t errlen) {
    int verr = 0;
    stb_vorbis* v = stb_vorbis_open_memory(ogg, (int)len, &verr, NULL);
    if (!v) {
        snprintf(err, errlen, "vorbis open failed (%d)", verr);
        return 0;
    }
    stb_vorbis_info info = stb_vorbis_get_info(v);
    int channels = info.channels;
    if (channels < 1 || channels > 2) {
        stb_vorbis_close(v);
        snprintf(err, errlen, "%d channels unsupported", channels);
        return 0;
    }
    stb_vorbis_comment vc = stb_vorbis_get_comment(v);
    int32_t loop_start = comment_int(&vc, "LOOPSTART");
    int32_t loop_length = comment_int(&vc, "LOOPLENGTH");
    if (loop_start < 0 || loop_length <= 0) {
        loop_start = -1;
        loop_length = 0;
    }
    uint64_t total = last_granule(ogg, len);
    if (!total) total = stb_vorbis_stream_length_in_samples(v);
    Encoder e;
    memset(&e, 0, sizeof(e));
    e.channels = channels;
    int16_t chunk[4096 * 2];
    for (;;) {
        int got = stb_vorbis_get_samples_short_interleaved(v, channels, chunk, 4096 * channels);
        if (got <= 0) break;
        size_t n = (size_t)got;
        if (total && e.frames + n > total) n = (size_t)(total - e.frames);
        enc_push(&e, chunk, n);
        if (total && e.frames >= total) break;
    }
    stb_vorbis_close(v);
    int ok = enc_finish(&e, info.sample_rate, loop_start, loop_length, out);
    if (!ok) snprintf(err, errlen, "no audio decoded");
    return ok;
}
