#include "snd0.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "atrac3.h"

#define STB_VORBIS_HEADER_ONLY
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

#define RATE 48000
#define LENGTH 1786636
#define LOOP_START 32078
#define LOOP_END 1546525
#define FADE_START 1416720
#define FADE_END 1824960
#define FADE_CURVE 1.39
#define LEVEL 1.2178
#define CUTOFF 0.485
#define ZEROS 32
#define KAISER_BETA 9.0

static const double PI = 3.14159265358979323846;

static double bessel_i0(double x) {
    double sum = 1, term = 1;
    for (int k = 1; k < 64 && term > sum * 1e-15; k++) {
        double h = x / (2.0 * k);
        term *= h * h;
        sum += term;
    }
    return sum;
}

static uint32_t gcd(uint32_t a, uint32_t b) {
    while (b) {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return a;
}

static float* decode(const Buf* ogg, uint32_t* rate, size_t* frames) {
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_memory(ogg->data, (int)ogg->len, &err, NULL);
    if (!v) return NULL;
    stb_vorbis_info info = stb_vorbis_get_info(v);
    int ch = info.channels;
    if (ch < 1 || !info.sample_rate) {
        stb_vorbis_close(v);
        return NULL;
    }
    size_t want = (size_t)((uint64_t)LENGTH * info.sample_rate / RATE) + 1024;
    float* pcm = xmalloc(want * 2 * sizeof(float));
    float* chunk = xmalloc(4096 * (size_t)ch * sizeof(float));
    size_t have = 0;
    while (have < want) {
        int got = stb_vorbis_get_samples_float_interleaved(v, ch, chunk, 4096 * ch);
        if (got <= 0) break;
        for (int i = 0; i < got && have < want; i++, have++) {
            pcm[have * 2] = chunk[i * ch];
            pcm[have * 2 + 1] = chunk[i * ch + (ch > 1)];
        }
    }
    free(chunk);
    stb_vorbis_close(v);
    *rate = info.sample_rate;
    *frames = have;
    return pcm;
}

static float* resample(const float* in, size_t frames, uint32_t from, size_t count) {
    uint32_t g = gcd(RATE, from);
    uint32_t up = RATE / g, down = from / g;
    double fc = CUTOFF * (from > RATE ? (double)RATE / from : 1.0);
    int half = (int)ceil(ZEROS / (2.0 * fc));
    int taps = half * 2;
    float* kernel = xmalloc((size_t)up * (size_t)taps * sizeof(float));
    double norm = bessel_i0(KAISER_BETA);
    for (uint32_t p = 0; p < up; p++) {
        float* k = kernel + (size_t)p * (size_t)taps;
        double sum = 0;
        for (int j = 0; j < taps; j++) {
            double t = (double)p / up + half - 1 - j;
            double u = t / half;
            double w = fabs(u) < 1 ? bessel_i0(KAISER_BETA * sqrt(1 - u * u)) / norm : 0;
            double a = 2 * fc * t;
            double s = fabs(a) < 1e-12 ? 1 : sin(PI * a) / (PI * a);
            k[j] = (float)(2 * fc * s * w);
            sum += k[j];
        }
        for (int j = 0; j < taps; j++) k[j] = (float)(k[j] / sum);
    }
    float* out = xmalloc(count * 2 * sizeof(float));
    for (size_t m = 0; m < count; m++) {
        uint64_t pos = (uint64_t)m * down;
        int64_t base = (int64_t)(pos / up) - half + 1;
        const float* k = kernel + (size_t)(pos % up) * (size_t)taps;
        float l = 0, r = 0;
        for (int j = 0; j < taps; j++) {
            int64_t at = base + j;
            if (at < 0 || at >= (int64_t)frames) continue;
            l += in[at * 2] * k[j];
            r += in[at * 2 + 1] * k[j];
        }
        out[m * 2] = l;
        out[m * 2 + 1] = r;
    }
    free(kernel);
    return out;
}

static void shape(float* pcm, size_t count) {
    for (size_t i = 0; i < count; i++) {
        double level = LEVEL;
        if (i >= FADE_START) level *= pow(1.0 - (double)(i - FADE_START) / (FADE_END - FADE_START), FADE_CURVE);
        pcm[i * 2] = (float)(pcm[i * 2] * level);
        pcm[i * 2 + 1] = (float)(pcm[i * 2 + 1] * level);
    }
}

int snd0_build(Game* g, Buf* out) {
    Node* sys = game_json_opt(g, "data/System.json");
    const char* name = sys ? node_strv(node_get(node_get(sys, "titleBgm"), "name")) : NULL;
    char* path = name && *name ? str_fmt("audio/bgm/%s.ogg", name) : NULL;
    node_free(sys);
    Buf ogg = {0};
    int ok = path && game_read(g, path, &ogg);
    free(path);
    uint32_t from = 0;
    size_t frames = 0;
    float* pcm = NULL;
    if (ok) {
        pcm = decode(&ogg, &from, &frames);
        ok = pcm && frames > 0;
    }
    buf_free(&ogg);
    if (ok) {
        size_t count = (size_t)((uint64_t)frames * RATE / from);
        if (count > LENGTH) count = LENGTH;
        float* wave = resample(pcm, frames, from, count);
        shape(wave, count);
        int looped = count > LOOP_END;
        ok = atrac3_encode(wave, count, RATE, looped ? LOOP_START : -1, looped ? LOOP_END : -1, out);
        free(wave);
    }
    free(pcm);
    return ok;
}
