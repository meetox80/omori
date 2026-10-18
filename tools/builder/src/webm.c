#include "webm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"

#define ID_EBML 0x1A45DFA3u
#define ID_DOCTYPE 0x4282u
#define ID_SEGMENT 0x18538067u
#define ID_SEEK_HEAD 0x114D9B74u
#define ID_INFO 0x1549A966u
#define ID_TIMECODE_SCALE 0x2AD7B1u
#define ID_DURATION 0x4489u
#define ID_TRACKS 0x1654AE6Bu
#define ID_TRACK_ENTRY 0xAEu
#define ID_TRACK_NUMBER 0xD7u
#define ID_TRACK_TYPE 0x83u
#define ID_CODEC_ID 0x86u
#define ID_CODEC_PRIVATE 0x63A2u
#define ID_DEFAULT_DURATION 0x23E383u
#define ID_CODEC_DELAY 0x56AAu
#define ID_SEEK_PREROLL 0x56BBu
#define ID_VIDEO 0xE0u
#define ID_PIXEL_WIDTH 0xB0u
#define ID_PIXEL_HEIGHT 0xBAu
#define ID_AUDIO 0xE1u
#define ID_SAMPLING_FREQ 0xB5u
#define ID_CHANNELS 0x9Fu
#define ID_CONTENT_ENCODINGS 0x6D80u
#define ID_CLUSTER 0x1F43B675u
#define ID_CUES 0x1C53BB6Bu
#define ID_TAGS 0x1254C367u
#define ID_CHAPTERS 0x1043A770u
#define ID_ATTACHMENTS 0x1941A469u
#define ID_TIMECODE 0xE7u
#define ID_SIMPLE_BLOCK 0xA3u
#define ID_BLOCK_GROUP 0xA0u
#define ID_BLOCK 0xA1u
#define ID_BLOCK_DURATION 0x9Bu
#define ID_REFERENCE_BLOCK 0xFBu
#define ID_DISCARD_PADDING 0x75A2u

#define UNKNOWN_SIZE UINT64_MAX

typedef struct {
    Webm* w;
    char* err;
    size_t errlen;
    int failed;
} Ctx;

static void fail(Ctx* c, const char* msg) {
    if (c->failed) return;
    c->failed = 1;
    if (c->err && c->errlen) snprintf(c->err, c->errlen, "%s", msg);
}

static int read_id(const uint8_t** pp, const uint8_t* end, uint32_t* id) {
    const uint8_t* p = *pp;
    if (p >= end || !*p) return 0;
    int len = 1;
    uint8_t mask = 0x80;
    while (!(*p & mask)) {
        mask >>= 1;
        len++;
    }
    if (len > 4 || p + len > end) return 0;
    uint32_t v = 0;
    for (int i = 0; i < len; i++) v = (v << 8) | p[i];
    *id = v;
    *pp = p + len;
    return 1;
}

static int read_vint(const uint8_t** pp, const uint8_t* end, uint64_t* out, int* out_len) {
    const uint8_t* p = *pp;
    if (p >= end || !*p) return 0;
    int len = 1;
    uint8_t mask = 0x80;
    while (!(*p & mask)) {
        mask >>= 1;
        len++;
    }
    if (p + len > end) return 0;
    uint64_t v = *p & (uint8_t)(mask - 1);
    int all_ones = v == (uint64_t)(mask - 1);
    for (int i = 1; i < len; i++) {
        v = (v << 8) | p[i];
        if (p[i] != 0xFF) all_ones = 0;
    }
    *out = all_ones ? UNKNOWN_SIZE : v;
    if (out_len) *out_len = len;
    *pp = p + len;
    return 1;
}

static int read_header(const uint8_t** pp, const uint8_t* end, uint32_t* id, uint64_t* size) {
    if (!read_id(pp, end, id)) return 0;
    return read_vint(pp, end, size, NULL);
}

static uint64_t read_uint(const uint8_t* p, uint64_t n) {
    uint64_t v = 0;
    for (uint64_t i = 0; i < n && i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static double read_float(const uint8_t* p, uint64_t n) {
    if (n == 4) {
        uint32_t u = (uint32_t)read_uint(p, 4);
        float f;
        memcpy(&f, &u, 4);
        return f;
    }
    if (n == 8) {
        uint64_t u = read_uint(p, 8);
        double d;
        memcpy(&d, &u, 8);
        return d;
    }
    return 0;
}

static int child_end(Ctx* c, const uint8_t* p, uint64_t size, const uint8_t* end, const uint8_t** out) {
    if (size == UNKNOWN_SIZE) {
        fail(c, "element of unknown size where a sized one is required");
        return 0;
    }
    if (size > (uint64_t)(end - p)) {
        fail(c, "element extends past its parent");
        return 0;
    }
    *out = p + size;
    return 1;
}

static int64_t read_sint(const uint8_t* p, uint64_t n) {
    if (!n || n > 8) return 0;
    uint64_t v = read_uint(p, n);
    if (n < 8 && (p[0] & 0x80)) v |= ~(uint64_t)0 << (n * 8);
    return (int64_t)v;
}

static void add_packet(Webm* w, int track, int64_t ticks, int64_t dur_ticks, int key, const uint8_t* data, size_t len) {
    if (w->npackets == w->cap) {
        w->cap = w->cap ? w->cap * 2 : 1024;
        w->packets = xrealloc(w->packets, sizeof(WebmPacket) * (size_t)w->cap);
    }
    WebmPacket* k = &w->packets[w->npackets++];
    k->track = track;
    k->ts_ns = ticks;
    k->duration_ns = dur_ticks;
    k->discard_ns = 0;
    k->key = key;
    k->data = data;
    k->len = len;
}

static void parse_info(Ctx* c, const uint8_t* p, const uint8_t* end) {
    while (p < end && !c->failed) {
        uint32_t id;
        uint64_t size;
        const uint8_t* e;
        if (!read_header(&p, end, &id, &size) || !child_end(c, p, size, end, &e)) {
            fail(c, "bad Info element");
            return;
        }
        if (id == ID_TIMECODE_SCALE) c->w->timecode_scale = read_uint(p, size);
        else if (id == ID_DURATION) c->w->duration_ns = read_float(p, size);
        p = e;
    }
}

static void parse_track_sub(Ctx* c, WebmTrack* t, const uint8_t* p, const uint8_t* end, int audio) {
    while (p < end && !c->failed) {
        uint32_t id;
        uint64_t size;
        const uint8_t* e;
        if (!read_header(&p, end, &id, &size) || !child_end(c, p, size, end, &e)) {
            fail(c, "bad track settings");
            return;
        }
        if (!audio && id == ID_PIXEL_WIDTH) t->width = (int)read_uint(p, size);
        else if (!audio && id == ID_PIXEL_HEIGHT) t->height = (int)read_uint(p, size);
        else if (audio && id == ID_SAMPLING_FREQ) t->rate = read_float(p, size);
        else if (audio && id == ID_CHANNELS) t->channels = (int)read_uint(p, size);
        p = e;
    }
}

static void parse_track(Ctx* c, const uint8_t* p, const uint8_t* end) {
    WebmTrack t;
    memset(&t, 0, sizeof(t));
    t.channels = 1;
    t.rate = 8000;
    while (p < end && !c->failed) {
        uint32_t id;
        uint64_t size;
        const uint8_t* e;
        if (!read_header(&p, end, &id, &size) || !child_end(c, p, size, end, &e)) {
            fail(c, "bad TrackEntry");
            return;
        }
        switch (id) {
        case ID_TRACK_NUMBER: t.number = (int)read_uint(p, size); break;
        case ID_TRACK_TYPE: t.type = (int)read_uint(p, size); break;
        case ID_CODEC_ID: {
            size_t n = size < sizeof(t.codec) - 1 ? (size_t)size : sizeof(t.codec) - 1;
            memcpy(t.codec, p, n);
            t.codec[n] = 0;
            break;
        }
        case ID_CODEC_PRIVATE:
            t.priv = p;
            t.priv_len = (size_t)size;
            break;
        case ID_DEFAULT_DURATION: t.default_duration_ns = read_uint(p, size); break;
        case ID_CODEC_DELAY: t.codec_delay_ns = read_uint(p, size); break;
        case ID_SEEK_PREROLL: t.seek_preroll_ns = read_uint(p, size); break;
        case ID_VIDEO: parse_track_sub(c, &t, p, e, 0); break;
        case ID_AUDIO: parse_track_sub(c, &t, p, e, 1); break;
        case ID_CONTENT_ENCODINGS: t.encoded = 1; break;
        default: break;
        }
        p = e;
    }
    if (c->failed) return;
    Webm* w = c->w;
    w->tracks = xrealloc(w->tracks, sizeof(WebmTrack) * (size_t)(w->ntracks + 1));
    w->tracks[w->ntracks++] = t;
}

static void parse_tracks(Ctx* c, const uint8_t* p, const uint8_t* end) {
    while (p < end && !c->failed) {
        uint32_t id;
        uint64_t size;
        const uint8_t* e;
        if (!read_header(&p, end, &id, &size) || !child_end(c, p, size, end, &e)) {
            fail(c, "bad Tracks element");
            return;
        }
        if (id == ID_TRACK_ENTRY) parse_track(c, p, e);
        p = e;
    }
}

static int64_t signed_vint(uint64_t v, int len) { return (int64_t)v - (((int64_t)1 << (7 * len - 1)) - 1); }

static void parse_block(Ctx* c, const uint8_t* p, const uint8_t* end, int64_t cluster_tc, int simple, int has_ref, int64_t block_dur,
                        int64_t discard) {
    uint64_t track;
    if (!read_vint(&p, end, &track, NULL) || track == UNKNOWN_SIZE || end - p < 3) {
        fail(c, "bad block header");
        return;
    }
    int16_t rel = (int16_t)((p[0] << 8) | p[1]);
    uint8_t flags = p[2];
    p += 3;
    int key = simple ? (flags & 0x80) != 0 : !has_ref;
    int lacing = (flags >> 1) & 3;
    int64_t ticks = cluster_tc + rel;
    if (!lacing) {
        add_packet(c->w, (int)track, ticks, block_dur, key, p, (size_t)(end - p));
        c->w->packets[c->w->npackets - 1].discard_ns = discard;
        return;
    }
    if (p >= end) {
        fail(c, "bad laced block");
        return;
    }
    int count = *p++ + 1;
    size_t* sizes = xcalloc((size_t)count, sizeof(size_t));
    size_t total = 0;
    if (lacing == 1) {
        for (int i = 0; i < count - 1; i++) {
            size_t s = 0;
            for (;;) {
                if (p >= end) {
                    free(sizes);
                    fail(c, "bad xiph lacing");
                    return;
                }
                uint8_t b = *p++;
                s += b;
                if (b != 255) break;
            }
            sizes[i] = s;
            total += s;
        }
    } else if (lacing == 3) {
        uint64_t first;
        if (!read_vint(&p, end, &first, NULL) || first == UNKNOWN_SIZE) {
            free(sizes);
            fail(c, "bad ebml lacing");
            return;
        }
        sizes[0] = (size_t)first;
        total = sizes[0];
        int64_t prev = (int64_t)first;
        for (int i = 1; i < count - 1; i++) {
            uint64_t raw;
            int len;
            if (!read_vint(&p, end, &raw, &len) || raw == UNKNOWN_SIZE) {
                free(sizes);
                fail(c, "bad ebml lacing");
                return;
            }
            int64_t s = prev + signed_vint(raw, len);
            if (s < 0) {
                free(sizes);
                fail(c, "bad ebml lacing");
                return;
            }
            sizes[i] = (size_t)s;
            total += sizes[i];
            prev = s;
        }
    } else {
        size_t rest = (size_t)(end - p);
        if (rest % (size_t)count) {
            free(sizes);
            fail(c, "bad fixed lacing");
            return;
        }
        for (int i = 0; i < count - 1; i++) {
            sizes[i] = rest / (size_t)count;
            total += sizes[i];
        }
    }
    if (total > (size_t)(end - p)) {
        free(sizes);
        fail(c, "laced frames exceed block");
        return;
    }
    sizes[count - 1] = (size_t)(end - p) - total;
    for (int i = 0; i < count; i++) {
        add_packet(c->w, (int)track, ticks, i == count - 1 ? block_dur : -1, key, p, sizes[i]);
        if (i == count - 1) c->w->packets[c->w->npackets - 1].discard_ns = discard;
        p += sizes[i];
    }
    free(sizes);
}

static void parse_block_group(Ctx* c, const uint8_t* p, const uint8_t* end, int64_t cluster_tc) {
    const uint8_t* block = NULL;
    const uint8_t* block_end = NULL;
    int has_ref = 0;
    int64_t dur = -1;
    int64_t discard = 0;
    while (p < end && !c->failed) {
        uint32_t id;
        uint64_t size;
        const uint8_t* e;
        if (!read_header(&p, end, &id, &size) || !child_end(c, p, size, end, &e)) {
            fail(c, "bad BlockGroup");
            return;
        }
        if (id == ID_BLOCK) {
            block = p;
            block_end = e;
        } else if (id == ID_REFERENCE_BLOCK) {
            has_ref = 1;
        } else if (id == ID_BLOCK_DURATION) {
            dur = (int64_t)read_uint(p, size);
        } else if (id == ID_DISCARD_PADDING) {
            discard = read_sint(p, size);
        }
        p = e;
    }
    if (block && !c->failed) parse_block(c, block, block_end, cluster_tc, 0, has_ref, dur, discard);
}

static int is_top_level(uint32_t id) {
    return id == ID_CLUSTER || id == ID_CUES || id == ID_TAGS || id == ID_INFO || id == ID_TRACKS || id == ID_SEEK_HEAD || id == ID_CHAPTERS ||
           id == ID_ATTACHMENTS;
}

static const uint8_t* parse_cluster(Ctx* c, const uint8_t* p, const uint8_t* end) {
    int64_t tc = 0;
    while (p < end && !c->failed) {
        const uint8_t* start = p;
        uint32_t id;
        uint64_t size;
        if (!read_header(&p, end, &id, &size)) {
            fail(c, "bad Cluster");
            return end;
        }
        if (is_top_level(id)) return start;
        const uint8_t* e;
        if (!child_end(c, p, size, end, &e)) return end;
        if (id == ID_TIMECODE) tc = (int64_t)read_uint(p, size);
        else if (id == ID_SIMPLE_BLOCK) parse_block(c, p, e, tc, 1, 0, -1, 0);
        else if (id == ID_BLOCK_GROUP) parse_block_group(c, p, e, tc);
        p = e;
    }
    return end;
}

static void parse_segment(Ctx* c, const uint8_t* p, const uint8_t* end) {
    while (p < end && !c->failed) {
        uint32_t id;
        uint64_t size;
        if (!read_header(&p, end, &id, &size)) {
            if (c->w->npackets) return;
            fail(c, "bad Segment child");
            return;
        }
        if (id == ID_CLUSTER) {
            const uint8_t* ce = size == UNKNOWN_SIZE || size > (uint64_t)(end - p) ? end : p + size;
            const uint8_t* stop = parse_cluster(c, p, ce);
            p = size == UNKNOWN_SIZE ? stop : ce;
            continue;
        }
        const uint8_t* e;
        if (size > (uint64_t)(end - p) && size != UNKNOWN_SIZE && c->w->npackets) return;
        if (!child_end(c, p, size, end, &e)) return;
        if (id == ID_INFO) parse_info(c, p, e);
        else if (id == ID_TRACKS) parse_tracks(c, p, e);
        p = e;
    }
}

int webm_parse(const uint8_t* data, size_t len, Webm* out, char* err, size_t errlen) {
    memset(out, 0, sizeof(*out));
    out->timecode_scale = 1000000;
    Ctx c = {out, err, errlen, 0};
    const uint8_t* p = data;
    const uint8_t* end = data + len;
    int saw_ebml = 0, saw_segment = 0;
    while (p < end && !c.failed) {
        uint32_t id;
        uint64_t size;
        if (!read_header(&p, end, &id, &size)) break;
        if (id == ID_EBML) {
            const uint8_t* e;
            if (!child_end(&c, p, size, end, &e)) break;
            const uint8_t* q = p;
            int doc_ok = 0;
            while (q < e) {
                uint32_t cid;
                uint64_t cs;
                if (!read_header(&q, e, &cid, &cs) || cs > (uint64_t)(e - q)) break;
                if (cid == ID_DOCTYPE && ((cs == 4 && !memcmp(q, "webm", 4)) || (cs == 8 && !memcmp(q, "matroska", 8)))) doc_ok = 1;
                q += cs;
            }
            if (!doc_ok) fail(&c, "not a WebM/Matroska file");
            saw_ebml = 1;
            p = e;
        } else if (id == ID_SEGMENT) {
            const uint8_t* e = size == UNKNOWN_SIZE || size > (uint64_t)(end - p) ? end : p + size;
            parse_segment(&c, p, e);
            saw_segment = 1;
            p = e;
        } else {
            if (size == UNKNOWN_SIZE || size > (uint64_t)(end - p)) break;
            p += size;
        }
    }
    if (!c.failed && (!saw_ebml || !saw_segment)) fail(&c, "missing EBML header or Segment");
    if (c.failed) {
        webm_free(out);
        return 0;
    }
    int64_t scale = (int64_t)out->timecode_scale;
    for (int i = 0; i < out->npackets; i++) {
        WebmPacket* k = &out->packets[i];
        k->ts_ns *= scale;
        if (k->duration_ns >= 0) k->duration_ns *= scale;
    }
    out->duration_ns *= (double)scale;
    return 1;
}

void webm_free(Webm* w) {
    free(w->tracks);
    free(w->packets);
    memset(w, 0, sizeof(*w));
}

const WebmTrack* webm_find_track(const Webm* w, int type) {
    for (int i = 0; i < w->ntracks; i++) {
        if (w->tracks[i].type == type) return &w->tracks[i];
    }
    return NULL;
}
