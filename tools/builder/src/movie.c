#include "movie.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "image.h"
#include "jpeg.h"
#include "ogg.h"
#include "webm.h"

#include "opus.h"
#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"

#define MOVIE_MAX_W 480
#define MOVIE_MAX_H 272
#define DUP_TOLERANCE 2
#define OPUS_RATE 48000

typedef struct {
    int out_w, out_h;
    int coded_w, coded_h;
    int quality;
    Image* full;
    uint8_t* rgb;
    uint8_t* last;
    int have_last;
    uint8_t* coded;
    Buf jpegs;
    uint32_t* pts;
    uint32_t* off;
    uint32_t* size;
    int n, cap;
    int unique;
    uint32_t max_size;
    uint32_t last_off, last_size;
    int map_w, map_h, map_xs, map_ys;
    int *cx0, *cx1, *cxw;
    int *cy0, *cy1, *cyw;
} Enc;

typedef struct {
    int yc, yo, rv, gu, gv, bu;
} Coef;

static void coef_for(const vpx_image_t* img, Coef* c) {
    double kr = 0.299, kb = 0.114;
    if (img->cs == VPX_CS_BT_709) {
        kr = 0.2126;
        kb = 0.0722;
    } else if (img->cs == VPX_CS_BT_2020) {
        kr = 0.2627;
        kb = 0.0593;
    } else if (img->cs == VPX_CS_SMPTE_240) {
        kr = 0.212;
        kb = 0.087;
    }
    double kg = 1.0 - kr - kb;
    int full = img->range == VPX_CR_FULL_RANGE;
    double ys = full ? 1.0 : 255.0 / 219.0;
    double cs = full ? 1.0 : 255.0 / 224.0;
    c->yo = full ? 0 : 16;
    c->yc = (int)lround(ys * 65536.0);
    c->rv = (int)lround(cs * 2.0 * (1.0 - kr) * 65536.0);
    c->bu = (int)lround(cs * 2.0 * (1.0 - kb) * 65536.0);
    c->gu = (int)lround(cs * 2.0 * (1.0 - kb) * kb / kg * 65536.0);
    c->gv = (int)lround(cs * 2.0 * (1.0 - kr) * kr / kg * 65536.0);
}

static void build_axis(int n, int shift, int** i0p, int** i1p, int** wp) {
    int cn = (n + (1 << shift) - 1) >> shift;
    int* i0 = xrealloc(*i0p, sizeof(int) * (size_t)n);
    int* i1 = xrealloc(*i1p, sizeof(int) * (size_t)n);
    int* w = xrealloc(*wp, sizeof(int) * (size_t)n);
    for (int x = 0; x < n; x++) {
        double f = (x + 0.5) / (double)(1 << shift) - 0.5;
        int a = (int)floor(f);
        int wt = (int)lround((f - a) * 256.0);
        int b = a + 1;
        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if (a > cn - 1) a = cn - 1;
        if (b > cn - 1) b = cn - 1;
        i0[x] = a;
        i1[x] = b;
        w[x] = wt;
    }
    *i0p = i0;
    *i1p = i1;
    *wp = w;
}

static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void yuv_to_rgba(Enc* e, const vpx_image_t* img) {
    int w = (int)img->d_w, h = (int)img->d_h;
    int xs = (int)img->x_chroma_shift, ys = (int)img->y_chroma_shift;
    if (!e->full || e->full->w != w || e->full->h != h) {
        img_free(e->full);
        e->full = img_new(w, h);
    }
    if (e->map_w != w || e->map_h != h || e->map_xs != xs || e->map_ys != ys) {
        build_axis(w, xs, &e->cx0, &e->cx1, &e->cxw);
        build_axis(h, ys, &e->cy0, &e->cy1, &e->cyw);
        e->map_w = w;
        e->map_h = h;
        e->map_xs = xs;
        e->map_ys = ys;
    }
    Coef c;
    coef_for(img, &c);
    for (int y = 0; y < h; y++) {
        const uint8_t* yr = img->planes[0] + (size_t)y * img->stride[0];
        const uint8_t* u0 = img->planes[1] + (size_t)e->cy0[y] * img->stride[1];
        const uint8_t* u1 = img->planes[1] + (size_t)e->cy1[y] * img->stride[1];
        const uint8_t* v0 = img->planes[2] + (size_t)e->cy0[y] * img->stride[2];
        const uint8_t* v1 = img->planes[2] + (size_t)e->cy1[y] * img->stride[2];
        int wy = e->cyw[y];
        uint8_t* out = e->full->px + (size_t)y * w * 4;
        for (int x = 0; x < w; x++) {
            int a = e->cx0[x], b = e->cx1[x], wx = e->cxw[x];
            int ut = u0[a] * (256 - wx) + u0[b] * wx;
            int ub = u1[a] * (256 - wx) + u1[b] * wx;
            int vt = v0[a] * (256 - wx) + v0[b] * wx;
            int vb = v1[a] * (256 - wx) + v1[b] * wx;
            int u = ((ut * (256 - wy) + ub * wy + 32768) >> 16) - 128;
            int v = ((vt * (256 - wy) + vb * wy + 32768) >> 16) - 128;
            int yy = c.yc * (yr[x] - c.yo) + 32768;
            out[x * 4 + 0] = clamp8((yy + c.rv * v) >> 16);
            out[x * 4 + 1] = clamp8((yy - c.gu * u - c.gv * v) >> 16);
            out[x * 4 + 2] = clamp8((yy + c.bu * u) >> 16);
            out[x * 4 + 3] = 255;
        }
    }
}

static void enc_init(Enc* e, int src_w, int src_h, int quality) {
    memset(e, 0, sizeof(*e));
    double s = TEX_SCALE;
    if (src_w * s > MOVIE_MAX_W) s = (double)MOVIE_MAX_W / src_w;
    if (src_h * s > MOVIE_MAX_H) s = (double)MOVIE_MAX_H / src_h;
    e->out_w = (int)lround(src_w * s);
    e->out_h = (int)lround(src_h * s);
    if (e->out_w < 1) e->out_w = 1;
    if (e->out_h < 1) e->out_h = 1;
    e->coded_w = (e->out_w + 15) & ~15;
    e->coded_h = (e->out_h + 15) & ~15;
    if (e->coded_h > MOVIE_MAX_H) e->coded_h = e->out_h;
    e->quality = quality;
    e->rgb = xmalloc((size_t)e->out_w * e->out_h * 3);
    e->last = xmalloc((size_t)e->out_w * e->out_h * 3);
    e->coded = xmalloc((size_t)e->coded_w * e->coded_h * 3);
}

static void enc_free(Enc* e) {
    img_free(e->full);
    free(e->rgb);
    free(e->last);
    free(e->coded);
    buf_free(&e->jpegs);
    free(e->pts);
    free(e->off);
    free(e->size);
    free(e->cx0);
    free(e->cx1);
    free(e->cxw);
    free(e->cy0);
    free(e->cy1);
    free(e->cyw);
    memset(e, 0, sizeof(*e));
}

static void enc_push(Enc* e, uint32_t pts, uint32_t off, uint32_t size) {
    if (e->n == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 1024;
        e->pts = xrealloc(e->pts, sizeof(uint32_t) * (size_t)e->cap);
        e->off = xrealloc(e->off, sizeof(uint32_t) * (size_t)e->cap);
        e->size = xrealloc(e->size, sizeof(uint32_t) * (size_t)e->cap);
    }
    e->pts[e->n] = pts;
    e->off[e->n] = off;
    e->size[e->n] = size;
    e->n++;
}

static int same_as_last(const Enc* e) {
    if (!e->have_last) return 0;
    size_t n = (size_t)e->out_w * e->out_h * 3;
    for (size_t i = 0; i < n; i++) {
        int d = (int)e->rgb[i] - (int)e->last[i];
        if (d > DUP_TOLERANCE || d < -DUP_TOLERANCE) return 0;
    }
    return 1;
}

static int enc_frame(Enc* e, const vpx_image_t* img, uint32_t pts) {
    yuv_to_rgba(e, img);
    Image* small = img_resize(e->full, e->out_w, e->out_h, FILTER_LANCZOS);
    for (size_t i = 0, n = (size_t)e->out_w * e->out_h; i < n; i++) memcpy(e->rgb + i * 3, small->px + i * 4, 3);
    img_free(small);
    if (same_as_last(e)) {
        enc_push(e, pts, e->last_off, e->last_size);
        return 1;
    }
    for (int y = 0; y < e->coded_h; y++) {
        int sy = y < e->out_h ? y : e->out_h - 1;
        uint8_t* d = e->coded + (size_t)y * e->coded_w * 3;
        const uint8_t* s = e->rgb + (size_t)sy * e->out_w * 3;
        memcpy(d, s, (size_t)e->out_w * 3);
        for (int x = e->out_w; x < e->coded_w; x++) memcpy(d + x * 3, s + (e->out_w - 1) * 3, 3);
    }
    size_t before = e->jpegs.len;
    if (!jpeg_encode_rgb(e->coded, e->coded_w, e->coded_h, e->quality, &e->jpegs)) return 0;
    uint32_t size = (uint32_t)(e->jpegs.len - before);
    e->last_off = (uint32_t)before;
    e->last_size = size;
    if (size > e->max_size) e->max_size = size;
    memcpy(e->last, e->rgb, (size_t)e->out_w * e->out_h * 3);
    e->have_last = 1;
    e->unique++;
    enc_push(e, pts, e->last_off, size);
    return 1;
}

static void put16(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void enc_finish(Enc* e, int src_w, int src_h, double duration, int has_audio, Buf* out) {
    uint32_t data_start = MJV_HEADER_SIZE + (uint32_t)e->n * MJV_ENTRY_SIZE;
    size_t base = out->len;
    buf_zero(out, data_start);
    uint8_t* h = out->data + base;
    memcpy(h, "OMJV", 4);
    put16(h + 4, 1);
    put16(h + 6, MJV_HEADER_SIZE);
    put16(h + 8, (uint32_t)src_w);
    put16(h + 10, (uint32_t)src_h);
    put16(h + 12, (uint32_t)e->out_w);
    put16(h + 14, (uint32_t)e->out_h);
    put16(h + 16, (uint32_t)e->coded_w);
    put16(h + 18, (uint32_t)e->coded_h);
    put32(h + 20, (uint32_t)e->n);
    put32(h + 24, (uint32_t)llround(duration * 1e6));
    put32(h + 28, e->max_size);
    put32(h + 32, MJV_HEADER_SIZE);
    put32(h + 36, has_audio ? 1u : 0u);
    for (int i = 0; i < e->n; i++) {
        uint8_t* t = h + MJV_HEADER_SIZE + (size_t)i * MJV_ENTRY_SIZE;
        put32(t, e->pts[i]);
        put32(t + 4, data_start + e->off[i]);
        put32(t + 8, e->size[i]);
    }
    buf_put(out, e->jpegs.data, e->jpegs.len);
}

static void wav_header(Buf* out, int channels, int rate, uint32_t frames) {
    uint32_t data = frames * (uint32_t)channels * 2;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + data);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);
    put16(h + 22, (uint32_t)channels);
    put32(h + 24, (uint32_t)rate);
    put32(h + 28, (uint32_t)rate * (uint32_t)channels * 2);
    put16(h + 32, (uint32_t)channels * 2);
    put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put32(h + 40, data);
    buf_put(out, h, 44);
}

static int opus_to_wav(const WebmTrack* t, const WebmPacket* const* pk, int n, Buf* out, double* seconds, char* err, size_t errlen) {
    if (t->priv_len < 19 || memcmp(t->priv, "OpusHead", 8)) {
        snprintf(err, errlen, "bad OpusHead");
        return 0;
    }
    int channels = t->priv[9];
    int pre_skip = t->priv[10] | (t->priv[11] << 8);
    int16_t gain_q8 = (int16_t)(t->priv[16] | (t->priv[17] << 8));
    int family = t->priv[18];
    if (family != 0 || channels < 1 || channels > 2) {
        snprintf(err, errlen, "unsupported Opus channel mapping %d (%d channels)", family, channels);
        return 0;
    }
    int oe = 0;
    OpusDecoder* dec = opus_decoder_create(OPUS_RATE, channels, &oe);
    if (!dec || oe != OPUS_OK) {
        snprintf(err, errlen, "opus decoder init failed");
        return 0;
    }
    float gain = gain_q8 ? (float)pow(10.0, gain_q8 / (20.0 * 256.0)) : 1.0f;
    float* pcm = NULL;
    size_t frames = 0, cap = 0;
    float tmp[5760 * 2];
    int64_t discard = 0;
    for (int i = 0; i < n; i++) {
        int got = opus_decode_float(dec, pk[i]->data, (opus_int32)pk[i]->len, tmp, 5760, 0);
        if (got < 0) {
            opus_decoder_destroy(dec);
            free(pcm);
            snprintf(err, errlen, "opus packet %d: %s", i, opus_strerror(got));
            return 0;
        }
        if (frames + (size_t)got > cap) {
            cap = (frames + (size_t)got) * 2;
            pcm = xrealloc(pcm, sizeof(float) * cap * (size_t)channels);
        }
        memcpy(pcm + frames * (size_t)channels, tmp, sizeof(float) * (size_t)got * (size_t)channels);
        frames += (size_t)got;
        if (i == n - 1 && pk[i]->discard_ns > 0) discard = (int64_t)llround(pk[i]->discard_ns * (double)OPUS_RATE / 1e9);
    }
    opus_decoder_destroy(dec);
    int64_t start = pre_skip;
    int64_t end = (int64_t)frames - discard;
    if (end < start) end = start;
    uint32_t count = (uint32_t)(end - start);
    wav_header(out, channels, OPUS_RATE, count);
    size_t at = out->len;
    buf_zero(out, (size_t)count * (size_t)channels * 2);
    uint8_t* d = out->data + at;
    for (size_t i = 0; i < (size_t)count * (size_t)channels; i++) {
        float s = pcm[(size_t)start * (size_t)channels + i] * gain * 32767.0f;
        long v = lrintf(s);
        if (v > 32767) v = 32767;
        if (v < -32768) v = -32768;
        d[i * 2] = (uint8_t)(v & 0xFF);
        d[i * 2 + 1] = (uint8_t)((v >> 8) & 0xFF);
    }
    free(pcm);
    *seconds = (double)count / OPUS_RATE;
    return 1;
}

static const WebmPacket** track_packets(const Webm* w, int number, int* count) {
    const WebmPacket** list = xmalloc(sizeof(WebmPacket*) * (size_t)(w->npackets ? w->npackets : 1));
    int n = 0;
    for (int i = 0; i < w->npackets; i++) {
        if (w->packets[i].track == number) list[n++] = &w->packets[i];
    }
    *count = n;
    return list;
}

static int convert_audio(const Webm* w, MovieOut* out, char* err, size_t errlen) {
    const WebmTrack* at = webm_find_track(w, WEBM_AUDIO);
    if (!at) return 1;
    if (at->encoded) {
        snprintf(err, errlen, "audio track uses content encoding");
        return 0;
    }
    int n;
    const WebmPacket** pk = track_packets(w, at->number, &n);
    int ok = 0;
    if (!strcmp(at->codec, "A_VORBIS")) {
        const uint8_t** data = xmalloc(sizeof(uint8_t*) * (size_t)(n ? n : 1));
        size_t* lens = xmalloc(sizeof(size_t) * (size_t)(n ? n : 1));
        for (int i = 0; i < n; i++) {
            data[i] = pk[i]->data;
            lens[i] = pk[i]->len;
        }
        int64_t samples = 0;
        ok = ogg_vorbis_remux(at->priv, at->priv_len, data, lens, n, 0x4F4D4A56u, &out->audio, &samples, err, errlen);
        if (ok) {
            out->audio_ext = "ogg";
            out->audio_seconds = at->rate > 0 ? (double)samples / at->rate : 0;
        }
        free(data);
        free(lens);
    } else if (!strcmp(at->codec, "A_OPUS")) {
        ok = opus_to_wav(at, pk, n, &out->audio, &out->audio_seconds, err, errlen);
        if (ok) out->audio_ext = "wav";
    } else {
        snprintf(err, errlen, "unsupported audio codec %s", at->codec);
    }
    free(pk);
    return ok;
}

static int handle_image(Enc* e, int* started, const vpx_image_t* img, int64_t ts_ns, int quality, int* src_w, int* src_h, char* err,
                        size_t errlen) {
    if (img->fmt & VPX_IMG_FMT_HIGHBITDEPTH) {
        snprintf(err, errlen, "high bit depth video is not supported");
        return 0;
    }
    if (img->x_chroma_shift > 1 || img->y_chroma_shift > 1) {
        snprintf(err, errlen, "unsupported chroma layout");
        return 0;
    }
    if (img->cs == VPX_CS_SRGB) {
        snprintf(err, errlen, "RGB coded video is not supported");
        return 0;
    }
    if (!*started) {
        *src_w = (int)img->d_w;
        *src_h = (int)img->d_h;
        enc_init(e, *src_w, *src_h, quality);
        *started = 1;
    }
    int64_t us = ts_ns > 0 ? ts_ns / 1000 : 0;
    if (us > 0xFFFFFFFFll) us = 0xFFFFFFFFll;
    if (!enc_frame(e, img, (uint32_t)us)) {
        snprintf(err, errlen, "jpeg encoding failed");
        return 0;
    }
    return 1;
}

int movie_convert(const uint8_t* data, size_t len, int quality, MovieOut* out, char* err, size_t errlen) {
    return movie_convert_progress(data, len, quality, out, err, errlen, NULL, NULL);
}

int movie_convert_progress(const uint8_t* data, size_t len, int quality, MovieOut* out, char* err, size_t errlen, MovieProgress progress,
                           void* progress_ctx) {
    memset(out, 0, sizeof(*out));
    Webm w;
    if (!webm_parse(data, len, &w, err, errlen)) return 0;
    const WebmTrack* vt = webm_find_track(&w, WEBM_VIDEO);
    if (!vt) {
        snprintf(err, errlen, "no video track");
        webm_free(&w);
        return 0;
    }
    vpx_codec_iface_t* iface = NULL;
    if (!strcmp(vt->codec, "V_VP8")) iface = vpx_codec_vp8_dx();
    else if (!strcmp(vt->codec, "V_VP9")) iface = vpx_codec_vp9_dx();
    if (!iface || vt->encoded) {
        snprintf(err, errlen, "unsupported video codec %s", vt->codec);
        webm_free(&w);
        return 0;
    }
    vpx_codec_ctx_t ctx;
    vpx_codec_dec_cfg_t cfg;
    cfg.threads = 1;
    cfg.w = (unsigned)vt->width;
    cfg.h = (unsigned)vt->height;
    if (vpx_codec_dec_init(&ctx, iface, &cfg, 0) != VPX_CODEC_OK) {
        snprintf(err, errlen, "video decoder init failed");
        webm_free(&w);
        return 0;
    }
    int n;
    const WebmPacket** pk = track_packets(&w, vt->number, &n);
    Enc e;
    memset(&e, 0, sizeof(e));
    int started = 0, ok = 1, src_w = 0, src_h = 0;
    int64_t last_ts = 0, first_ts = -1;
    for (int i = 0; i < n && ok; i++) {
        if (vpx_codec_decode(&ctx, pk[i]->data, (unsigned int)pk[i]->len, NULL, 0) != VPX_CODEC_OK) {
            const char* detail = vpx_codec_error_detail(&ctx);
            snprintf(err, errlen, "video frame %d: %s%s%s", i, vpx_codec_error(&ctx), detail ? ": " : "", detail ? detail : "");
            ok = 0;
            break;
        }
        vpx_codec_iter_t iter = NULL;
        const vpx_image_t* img;
        while (ok && (img = vpx_codec_get_frame(&ctx, &iter)) != NULL) {
            ok = handle_image(&e, &started, img, pk[i]->ts_ns, quality, &src_w, &src_h, err, errlen);
            if (first_ts < 0) first_ts = pk[i]->ts_ns;
            last_ts = pk[i]->ts_ns;
        }
        if (progress) progress(progress_ctx, i + 1, n);
    }
    int64_t step = vt->default_duration_ns ? (int64_t)vt->default_duration_ns
                   : e.n > 1                ? (last_ts - first_ts) / (e.n - 1)
                                            : 33333333;
    if (ok && vpx_codec_decode(&ctx, NULL, 0, NULL, 0) == VPX_CODEC_OK) {
        vpx_codec_iter_t iter = NULL;
        const vpx_image_t* img;
        while (ok && (img = vpx_codec_get_frame(&ctx, &iter)) != NULL) {
            last_ts += step;
            ok = handle_image(&e, &started, img, last_ts, quality, &src_w, &src_h, err, errlen);
        }
    }
    vpx_codec_destroy(&ctx);
    free(pk);
    if (ok && !e.n) {
        snprintf(err, errlen, "no video frames");
        ok = 0;
    }
    if (ok) ok = convert_audio(&w, out, err, errlen);
    if (ok) {
        double video_end = (last_ts + step) / 1e9;
        double duration = w.duration_ns > 0 ? w.duration_ns / 1e9 : video_end;
        if (w.duration_ns <= 0 && out->audio_seconds > duration) duration = out->audio_seconds;
        enc_finish(&e, src_w, src_h, duration, out->audio.len > 0, &out->mjv);
        out->frames = e.n;
        out->unique = e.unique;
        out->duration = duration;
    }
    enc_free(&e);
    webm_free(&w);
    if (!ok) movie_out_free(out);
    return ok;
}

void movie_out_free(MovieOut* out) {
    buf_free(&out->mjv);
    buf_free(&out->audio);
    memset(out, 0, sizeof(*out));
}

static uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static char* cache_path(const char* dir, const char* logical, const char* ext) {
    char* key = asset_key(logical, ext);
    char* path = path_join(dir, key);
    free(key);
    return path;
}

static int load_cached(const char* dir, const char* logical, MovieOut* out) {
    memset(out, 0, sizeof(*out));
    char* path = cache_path(dir, logical, "mjv");
    int ok = file_read(path, &out->mjv);
    free(path);
    if (!ok) return 0;
    const uint8_t* h = out->mjv.data;
    uint32_t n = out->mjv.len >= MJV_HEADER_SIZE ? get32(h + 20) : 0;
    if (out->mjv.len < MJV_HEADER_SIZE || memcmp(h, "OMJV", 4) || out->mjv.len < MJV_HEADER_SIZE + (size_t)n * MJV_ENTRY_SIZE) {
        movie_out_free(out);
        return 0;
    }
    out->frames = (int)n;
    out->duration = get32(h + 24) / 1e6;
    uint32_t last = 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t off = get32(h + MJV_HEADER_SIZE + i * MJV_ENTRY_SIZE + 4);
        if (i == 0 || off != last) out->unique++;
        last = off;
    }
    if (!get32(h + 36)) return 1;
    static const char* const EXTS[] = {"ogg", "wav"};
    for (int i = 0; i < 2; i++) {
        char* audio = cache_path(dir, logical, EXTS[i]);
        int found = file_read(audio, &out->audio);
        free(audio);
        if (found) {
            out->audio_ext = EXTS[i];
            return 1;
        }
    }
    movie_out_free(out);
    return 0;
}

static int write_cached(const char* dir, const char* logical, const char* ext, const Buf* data) {
    char* path = cache_path(dir, logical, ext);
    char* slash = strrchr(path, '/');
    if (slash) {
        *slash = 0;
        make_dirs(path);
        *slash = '/';
    }
    int ok = file_write(path, data->data, data->len);
    free(path);
    return ok;
}

typedef struct {
    Game* game;
    char* stored;
    char* logical;
    const char* cache;
    MovieOut out;
    char* error;
} MovieJob;

typedef struct {
    MovieJob* list;
    int count;
    int done;
    Mutex* lock;
} MovieBatch;

static int convert_stored(Game* g, const char* stored, const char* logical, MovieOut* out, char** error, MovieProgress progress, void* ctx) {
    Buf b = {0};
    int ok = 0;
    if (!game_read_stored(g, stored, &b)) {
        *error = str_fmt("%s: unreadable", stored);
    } else {
        char err[256] = {0};
        ok = movie_convert_progress(b.data, b.len, MOVIE_QUALITY, out, err, sizeof(err), progress, ctx);
        if (!ok) *error = str_fmt("%s: %s", logical, err);
    }
    buf_free(&b);
    return ok;
}

static void movie_job(void* ctx, int i) {
    MovieBatch* batch = ctx;
    MovieJob* j = &batch->list[i];
    if (!j->cache || !load_cached(j->cache, j->logical, &j->out)) convert_stored(j->game, j->stored, j->logical, &j->out, &j->error, NULL, NULL);
    mutex_lock(batch->lock);
    batch->done++;
    log_msg("  progress %d/%d", batch->done, batch->count);
    mutex_unlock(batch->lock);
}

static int is_webm(const char* s) {
    size_t n = strlen(s);
    if (n < 5) return 0;
    const char* e = s + n - 5;
    return e[0] == '.' && tolower((unsigned char)e[1]) == 'w' && tolower((unsigned char)e[2]) == 'e' && tolower((unsigned char)e[3]) == 'b' &&
           tolower((unsigned char)e[4]) == 'm';
}

static int same_name(const char* path, const char* name) {
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    while (*base && *name) {
        if (tolower((unsigned char)*base) != tolower((unsigned char)*name)) return 0;
        base++;
        name++;
    }
    return !*base && !*name;
}

static void log_movie(const char* logical, const MovieOut* out, double mb) {
    log_msg("  %s: %d frames (%d stored), %.2f s, %.1f MB", logical, out->frames, out->unique, out->duration, mb);
}

typedef struct {
    int percent;
} FrameLog;

static void log_frames(void* ctx, int done, int total) {
    FrameLog* f = ctx;
    int percent = total ? (int)((int64_t)done * 100 / total) : 100;
    if (percent == f->percent) return;
    f->percent = percent;
    log_msg("  frames %d/%d", done, total);
}

int assets_convert_movie(Game* g, const char* name, const char* out_dir) {
    int nfiles;
    char** files = game_stored_files(g, "movies", &nfiles);
    int rc = 2;
    for (int i = 0; i < nfiles && rc == 2; i++) {
        char* logical = game_logical(files[i]);
        if (is_webm(logical) && same_name(logical, name)) {
            MovieOut out;
            char* error = NULL;
            FrameLog progress = {-1};
            if (!convert_stored(g, files[i], logical, &out, &error, log_frames, &progress)) {
                fprintf(stderr, "error: %s\n", error);
                free(error);
                rc = 1;
            } else {
                int ok = write_cached(out_dir, logical, "mjv", &out.mjv);
                if (ok && out.audio.len) ok = write_cached(out_dir, logical, out.audio_ext, &out.audio);
                if (ok) log_movie(logical, &out, (out.mjv.len + out.audio.len) / 1048576.0);
                else fprintf(stderr, "error: cannot write %s\n", out_dir);
                rc = ok ? 0 : 1;
                movie_out_free(&out);
            }
        }
        free(logical);
    }
    if (rc == 2) fprintf(stderr, "error: no movie named %s\n", name);
    str_list_free(files, nfiles);
    return rc;
}

int assets_build_movies(Game* g, Pack* pack, int jobs, const char* cache) {
    int nfiles;
    char** files = game_stored_files(g, "movies", &nfiles);
    MovieJob* list = xcalloc((size_t)(nfiles ? nfiles : 1), sizeof(MovieJob));
    int n = 0;
    for (int i = 0; i < nfiles; i++) {
        char* logical = game_logical(files[i]);
        if (!is_webm(logical)) {
            free(logical);
            continue;
        }
        list[n].game = g;
        list[n].stored = xstrdup(files[i]);
        list[n].logical = logical;
        list[n].cache = cache;
        n++;
    }
    str_list_free(files, nfiles);
    MovieBatch batch = {list, n, 0, mutex_new()};
    run_jobs(movie_job, &batch, n, jobs);
    mutex_free(batch.lock);
    double total = 0;
    for (int i = 0; i < n; i++) {
        MovieJob* j = &list[i];
        char* key = asset_key(j->logical, "mjv");
        PackEntry* e = pack_add(pack, key);
        if (j->error) {
            e->error = j->error;
            j->error = NULL;
            pack_ready(pack, e);
        } else {
            e->data = j->out.mjv;
            memset(&j->out.mjv, 0, sizeof(Buf));
            double mb = e->data.len / 1048576.0;
            pack_ready(pack, e);
            if (j->out.audio.len) {
                char* akey = asset_key(j->logical, j->out.audio_ext);
                PackEntry* a = pack_add(pack, akey);
                a->data = j->out.audio;
                memset(&j->out.audio, 0, sizeof(Buf));
                pack_ready(pack, a);
                mb += a->data.len / 1048576.0;
                free(akey);
            }
            total += mb;
            log_movie(j->logical, &j->out, mb);
        }
        movie_out_free(&j->out);
        free(key);
        free(j->stored);
        free(j->logical);
        free(j->error);
    }
    free(list);
    if (n) log_msg("  movies total %.1f MB", total);
    return n;
}
