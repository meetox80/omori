#include "banner.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "image.h"
#include "snd0.h"
#include "util.h"

extern const unsigned char embedded_banner_template[];
extern const size_t embedded_banner_template_size;

#define SOUND_RATE 32728u
#define SOUND_FRAMES (SOUND_RATE * 3u)
#define FADE_FRAMES (SOUND_RATE / 2u)
#define SOUND_PEAK 0.7
#define SOUND_MAX_GAIN 4.0
#define LZ_WINDOW 4096u
#define LZ_MAX 0x10110u
#define LZ_HASH_BITS 15
#define LZ_CHAIN 128
#define SLOT_SIZE 20u
#define CGFX_LIMIT 0x80000u

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void le16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void le32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static uint32_t lz_hash(const uint8_t* p) { return ((uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2]) * 2654435761u >> (32 - LZ_HASH_BITS); }

int lz11_compress(const uint8_t* in, size_t len, Buf* out) {
    if (len >= (1u << 24)) {
        buf_u32(out, 0x11);
        buf_u32(out, (uint32_t)len);
    } else {
        buf_u32(out, 0x11u | (uint32_t)len << 8);
    }
    int32_t* head = xmalloc(sizeof(int32_t) << LZ_HASH_BITS);
    int32_t* prev = xmalloc(sizeof(int32_t) * (len ? len : 1));
    for (size_t i = 0; i < (size_t)1 << LZ_HASH_BITS; i++) head[i] = -1;
    size_t pos = 0;
    while (pos < len) {
        size_t flag_at = out->len;
        uint8_t flags = 0;
        buf_u8(out, 0);
        for (int bit = 0; bit < 8 && pos < len; bit++) {
            size_t best = 0, disp = 0;
            size_t limit = len - pos < LZ_MAX ? len - pos : LZ_MAX;
            if (limit >= 3) {
                int32_t cand = head[lz_hash(in + pos)];
                for (int chain = 0; cand >= 0 && pos - (size_t)cand <= LZ_WINDOW && chain < LZ_CHAIN; chain++) {
                    size_t n = 0;
                    while (n < limit && in[(size_t)cand + n] == in[pos + n]) n++;
                    if (n > best) {
                        best = n;
                        disp = pos - (size_t)cand;
                        if (n == limit) break;
                    }
                    cand = prev[cand];
                }
            }
            size_t step = 1;
            if (best >= 3) {
                size_t d = disp - 1;
                flags |= (uint8_t)(0x80 >> bit);
                if (best <= 0x10) {
                    buf_u8(out, (uint8_t)((best - 1) << 4 | d >> 8));
                } else if (best <= 0x110) {
                    size_t n = best - 0x11;
                    buf_u8(out, (uint8_t)(n >> 4));
                    buf_u8(out, (uint8_t)((n & 0xF) << 4 | d >> 8));
                } else {
                    size_t n = best - 0x111;
                    buf_u8(out, (uint8_t)(0x10 | n >> 12));
                    buf_u8(out, (uint8_t)(n >> 4));
                    buf_u8(out, (uint8_t)((n & 0xF) << 4 | d >> 8));
                }
                buf_u8(out, (uint8_t)d);
                step = best;
            } else {
                buf_u8(out, in[pos]);
            }
            for (size_t k = 0; k < step; k++, pos++) {
                if (pos + 3 > len) continue;
                uint32_t h = lz_hash(in + pos);
                prev[pos] = head[h];
                head[h] = (int32_t)pos;
            }
        }
        out->data[flag_at] = flags;
    }
    buf_zero(out, (4 - out->len % 4) % 4);
    free(head);
    free(prev);
    return 1;
}

static void put_texture(uint8_t* dst, const Image* img) {
    int w = img->w, h = img->h;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t* p = img->px + ((size_t)(h - 1 - y) * w + x) * 4;
            uint16_t v = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
            int m = 0;
            for (int i = 0; i < 3; i++) m |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1);
            le16(dst + (((size_t)(y >> 3) * (size_t)(w >> 3) + (size_t)(x >> 3)) * 64 + (size_t)m) * 2, v);
        }
    }
}

static int fill_slots(Game* g, Node* atlas, const uint8_t* table, uint32_t count, uint8_t* cgfx, size_t len) {
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* s = table + i * SLOT_SIZE;
        uint32_t at = rd32(s);
        int tw = rd16(s + 4), th = rd16(s + 6), x0 = rd16(s + 8), y0 = rd16(s + 10), x1 = rd16(s + 12), y1 = rd16(s + 14);
        if (tw % 8 || th % 8 || x1 - x0 > tw || y1 - y0 > th || x1 > 256 || y1 > 128 || at + (size_t)tw * th * 2 > len) return 0;
        Image* frame = assets_title_frame(g, atlas, s[16], s[17], 256, 128);
        if (!frame) return 0;
        Image* tex = img_new(tw, th);
        memset(tex->px, 255, (size_t)tw * th * 4);
        for (int y = y0; y < y1; y++) memcpy(tex->px + (size_t)(y - y0) * tw * 4, frame->px + ((size_t)y * 256 + x0) * 4, (size_t)(x1 - x0) * 4);
        put_texture(cgfx + at, tex);
        img_free(tex);
        img_free(frame);
    }
    return 1;
}

static void build_cwav(const float* pcm, size_t frames, Buf* out) {
    size_t channel = frames * 2;
    size_t data_size = 0x20 + channel * 2;
    uint8_t head[0xA0 + 0x20];
    memset(head, 0, sizeof(head));
    memcpy(head, "CWAV", 4);
    le16(head + 4, 0xFEFF);
    le16(head + 6, 0x40);
    le32(head + 8, 0x02010000);
    le32(head + 12, (uint32_t)(0xA0 + data_size));
    le16(head + 16, 2);
    le16(head + 20, 0x7000);
    le32(head + 24, 0x40);
    le32(head + 28, 0x60);
    le16(head + 32, 0x7001);
    le32(head + 36, 0xA0);
    le32(head + 40, (uint32_t)data_size);
    uint8_t* info = head + 0x40;
    memcpy(info, "INFO", 4);
    le32(info + 4, 0x60);
    info[8] = 1;
    le32(info + 0x0C, SOUND_RATE);
    le32(info + 0x14, (uint32_t)frames);
    le32(info + 0x1C, 2);
    for (uint32_t c = 0; c < 2; c++) {
        le16(info + 0x20 + c * 8, 0x7100);
        le32(info + 0x24 + c * 8, 0x14 + c * 20);
        uint8_t* ch = info + 0x30 + c * 20;
        le16(ch, 0x1F00);
        le32(ch + 4, (uint32_t)(0x18 + c * channel));
        le32(ch + 12, 0xFFFFFFFFu);
    }
    uint8_t* data = head + 0xA0;
    memcpy(data, "DATA", 4);
    le32(data + 4, (uint32_t)data_size);
    buf_put(out, head, sizeof(head));
    size_t at = out->len;
    buf_zero(out, channel * 2);
    double peak = 0;
    for (size_t i = 0; i < frames * 2; i++) peak = fmax(peak, fabs(pcm[i]));
    double gain = peak > 0 ? fmin(SOUND_PEAK / peak, SOUND_MAX_GAIN) : 1.0;
    for (size_t i = 0; i < frames; i++) {
        double level = gain;
        if (i + FADE_FRAMES >= frames) level *= 0.5 + 0.5 * cos(3.14159265358979323846 * (double)(i + FADE_FRAMES - frames + 1) / FADE_FRAMES);
        for (int c = 0; c < 2; c++) {
            double v = pcm[i * 2 + c] * level * 32767.0;
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
            le16(out->data + at + c * channel + i * 2, (uint16_t)(int16_t)lrint(v));
        }
    }
}

int banner_build(Game* g, Node* atlas, Buf* out) {
    const uint8_t* t = embedded_banner_template;
    size_t tlen = embedded_banner_template_size;
    uint32_t count = tlen >= 8 && memcmp(t, "OBNR", 4) == 0 ? rd32(t + 4) : 0;
    size_t skip = 8 + (size_t)count * SLOT_SIZE;
    if (!count || skip >= tlen || tlen - skip > CGFX_LIMIT) {
        log_msg("  warning: this builder has no HOME Menu banner model, so there is no omori.cia");
        return 0;
    }
    size_t len = tlen - skip;
    uint8_t* cgfx = xmalloc(len);
    memcpy(cgfx, t + skip, len);
    if (!fill_slots(g, atlas, t + 8, count, cgfx, len)) {
        log_msg("  warning: the title screen art is missing, so there is no omori.cia");
        free(cgfx);
        return 0;
    }
    Buf lz = {0};
    lz11_compress(cgfx, len, &lz);
    free(cgfx);
    Buf sound = {0};
    size_t frames = 0;
    float* pcm = title_music_pcm(g, SOUND_RATE, SOUND_FRAMES, &frames);
    frames &= ~(size_t)15;
    if (pcm && frames) build_cwav(pcm, frames, &sound);
    else log_msg("  no title music found, the HOME Menu banner is silent");
    free(pcm);
    uint8_t head[0x88];
    memset(head, 0, sizeof(head));
    memcpy(head, "CBMD", 4);
    le32(head + 8, sizeof(head));
    size_t sound_at = (sizeof(head) + lz.len + 0xF) & ~(size_t)0xF;
    if (sound.len) le32(head + 0x84, (uint32_t)sound_at);
    buf_put(out, head, sizeof(head));
    buf_put(out, lz.data, lz.len);
    if (sound.len) {
        buf_zero(out, sound_at - out->len);
        buf_put(out, sound.data, sound.len);
    }
    buf_free(&lz);
    buf_free(&sound);
    return 1;
}
