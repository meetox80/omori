#include "ogg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t g_crc_table[256];
static int g_crc_ready = 0;

static void crc_init(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t r = i << 24;
        for (int k = 0; k < 8; k++) r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : r << 1;
        g_crc_table[i] = r;
    }
    g_crc_ready = 1;
}

uint32_t ogg_crc(const uint8_t* data, size_t len) {
    if (!g_crc_ready) crc_init();
    uint32_t crc = 0;
    for (size_t i = 0; i < len; i++) crc = (crc << 8) ^ g_crc_table[((crc >> 24) & 0xFF) ^ data[i]];
    return crc;
}

void ogg_writer_init(OggWriter* w, Buf* out, uint32_t serial) {
    memset(w, 0, sizeof(*w));
    w->out = out;
    w->serial = serial;
    w->first = 1;
    if (!g_crc_ready) crc_init();
}

static void put_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void emit_page(OggWriter* w, int eos) {
    uint8_t h[27];
    memcpy(h, "OggS", 4);
    h[4] = 0;
    h[5] = (uint8_t)((w->continued ? 1 : 0) | (w->first ? 2 : 0) | (eos ? 4 : 0));
    int64_t g = w->completed ? w->granule : -1;
    put_le32(h + 6, (uint32_t)((uint64_t)g & 0xFFFFFFFFu));
    put_le32(h + 10, (uint32_t)((uint64_t)g >> 32));
    put_le32(h + 14, w->serial);
    put_le32(h + 18, w->seq++);
    put_le32(h + 22, 0);
    h[26] = (uint8_t)w->nlace;
    size_t start = w->out->len;
    buf_put(w->out, h, 27);
    buf_put(w->out, w->lace, (size_t)w->nlace);
    buf_put(w->out, w->body.data, w->body.len);
    uint32_t crc = ogg_crc(w->out->data + start, w->out->len - start);
    put_le32(w->out->data + start + 22, crc);
    w->nlace = 0;
    w->body.len = 0;
    w->completed = 0;
    w->first = 0;
    w->continued = 0;
}

void ogg_writer_packet(OggWriter* w, const uint8_t* data, size_t len, int64_t granule, int eos, int flush) {
    size_t pos = 0;
    for (;;) {
        if (w->nlace == 255) {
            int mid = pos > 0;
            emit_page(w, 0);
            w->continued = mid;
        }
        size_t seg = len - pos > 255 ? 255 : len - pos;
        w->lace[w->nlace++] = (uint8_t)seg;
        buf_put(&w->body, data + pos, seg);
        pos += seg;
        if (seg < 255) break;
    }
    w->granule = granule;
    w->completed = 1;
    if (flush || eos || w->body.len >= 4096) emit_page(w, eos);
}

void ogg_writer_free(OggWriter* w) { buf_free(&w->body); }

int vorbis_split_private(const uint8_t* priv, size_t len, const uint8_t* hdr[3], size_t hlen[3]) {
    if (len < 3 || priv[0] != 2) return 0;
    size_t p = 1;
    size_t sizes[2];
    for (int i = 0; i < 2; i++) {
        size_t s = 0;
        for (;;) {
            if (p >= len) return 0;
            uint8_t b = priv[p++];
            s += b;
            if (b != 255) break;
        }
        sizes[i] = s;
    }
    if (sizes[0] + sizes[1] > len - p) return 0;
    hdr[0] = priv + p;
    hlen[0] = sizes[0];
    hdr[1] = hdr[0] + sizes[0];
    hlen[1] = sizes[1];
    hdr[2] = hdr[1] + sizes[1];
    hlen[2] = len - p - sizes[0] - sizes[1];
    if (hlen[0] < 7 || hdr[0][0] != 1 || memcmp(hdr[0] + 1, "vorbis", 6)) return 0;
    if (hlen[1] < 7 || hdr[1][0] != 3 || memcmp(hdr[1] + 1, "vorbis", 6)) return 0;
    if (hlen[2] < 7 || hdr[2][0] != 5 || memcmp(hdr[2] + 1, "vorbis", 6)) return 0;
    return 1;
}

typedef struct {
    const uint8_t* d;
    int64_t pos;
} BackBits;

static int back_bit(BackBits* b) {
    if (b->pos < 0) return 0;
    int v = (b->d[b->pos >> 3] >> (b->pos & 7)) & 1;
    b->pos--;
    return v;
}

static uint32_t back_get(BackBits* b, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 1) | (uint32_t)back_bit(b);
    return v;
}

int vorbis_parse_info(const uint8_t* id, size_t idlen, const uint8_t* setup, size_t setuplen, VorbisInfo* vi) {
    memset(vi, 0, sizeof(*vi));
    if (idlen < 30 || id[0] != 1 || memcmp(id + 1, "vorbis", 6)) return 0;
    vi->channels = id[11];
    vi->rate = (int)((uint32_t)id[12] | ((uint32_t)id[13] << 8) | ((uint32_t)id[14] << 16) | ((uint32_t)id[15] << 24));
    vi->bs0 = 1 << (id[28] & 15);
    vi->bs1 = 1 << (id[28] >> 4);
    if (vi->channels < 1 || vi->rate < 1 || vi->bs0 < 64 || vi->bs1 < vi->bs0 || vi->bs1 > 8192) return 0;
    if (setuplen < 7 || setup[0] != 5 || memcmp(setup + 1, "vorbis", 6)) return 0;
    int64_t total = (int64_t)setuplen * 8;
    BackBits b = {setup, total - 1};
    int64_t framing = -1;
    while (b.pos + 1 > 97) {
        if (back_bit(&b)) {
            framing = b.pos;
            break;
        }
    }
    if (framing < 0) return 0;
    int mode_count = 0, last = 0;
    while (b.pos + 1 >= 97) {
        if (back_get(&b, 8) > 63 || back_get(&b, 16) || back_get(&b, 16)) break;
        back_bit(&b);
        mode_count++;
        if (mode_count > 64) break;
        BackBits peek = b;
        if ((int)back_get(&peek, 6) + 1 == mode_count) last = mode_count;
    }
    if (!last || last > 63) return 0;
    vi->mode_count = last;
    b.pos = framing;
    for (int i = last - 1; i >= 0; i--) {
        back_get(&b, 40);
        vi->mode_block[i] = (uint8_t)back_bit(&b);
    }
    return 1;
}

int vorbis_packet_blocksize(const VorbisInfo* vi, const uint8_t* pkt, size_t len) {
    if (!len || (pkt[0] & 1)) return 0;
    int mode = 0;
    if (vi->mode_count > 1) {
        int bits = 0;
        for (int v = vi->mode_count - 1; v; v >>= 1) bits++;
        int mask = ((1 << bits) - 1) << 1;
        mode = (pkt[0] & mask) >> 1;
        if (mode >= vi->mode_count) return 0;
    }
    return vi->mode_block[mode] ? vi->bs1 : vi->bs0;
}

int ogg_vorbis_remux(const uint8_t* priv, size_t priv_len, const uint8_t* const* pkts, const size_t* lens, int n, uint32_t serial, Buf* out,
                     int64_t* samples_out, char* err, size_t errlen) {
    const uint8_t* hdr[3];
    size_t hlen[3];
    if (!vorbis_split_private(priv, priv_len, hdr, hlen)) {
        snprintf(err, errlen, "bad Vorbis CodecPrivate");
        return 0;
    }
    VorbisInfo vi;
    if (!vorbis_parse_info(hdr[0], hlen[0], hdr[2], hlen[2], &vi)) {
        snprintf(err, errlen, "unreadable Vorbis setup header");
        return 0;
    }
    OggWriter w;
    ogg_writer_init(&w, out, serial);
    ogg_writer_packet(&w, hdr[0], hlen[0], 0, 0, 1);
    ogg_writer_packet(&w, hdr[1], hlen[1], 0, 0, 0);
    ogg_writer_packet(&w, hdr[2], hlen[2], 0, n == 0, 1);
    int64_t granule = 0;
    int prev = 0;
    for (int i = 0; i < n; i++) {
        int bs = vorbis_packet_blocksize(&vi, pkts[i], lens[i]);
        if (!bs) {
            ogg_writer_free(&w);
            snprintf(err, errlen, "bad Vorbis audio packet %d", i);
            return 0;
        }
        if (prev) granule += prev / 4 + bs / 4;
        prev = bs;
        ogg_writer_packet(&w, pkts[i], lens[i], granule, i == n - 1, 0);
    }
    ogg_writer_free(&w);
    if (samples_out) *samples_out = granule;
    return 1;
}
