#include "wuhb.h"

#include <stdlib.h>
#include <string.h>

#include "../third_party/miniz.h"

#define ENTRY_EMPTY 0xFFFFFFFFu
#define HEADER_SIZE 0x50
#define PARTITION_AT 0x200

static const char META_INI[] = "[menu]\nlongname=OMORI\nshortname=OMORI\nauthor=kotwicha\n";

typedef struct {
    const char* name;
    const uint8_t* data;
    size_t len;
} WFile;

typedef struct {
    const char* name;
    const WFile* files;
    int count;
} WDir;

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void put_be64(uint8_t* p, uint64_t v) {
    put_be32(p, (uint32_t)(v >> 32));
    put_be32(p + 4, (uint32_t)v);
}

static void put_le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static uint64_t be64(const uint8_t* p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

static uint32_t align4(size_t v) { return (uint32_t)((v + 3) & ~(size_t)3); }

static uint32_t path_hash(uint32_t parent, const char* name) {
    uint32_t h = parent ^ 123456789u;
    for (const unsigned char* c = (const unsigned char*)name; *c; c++) {
        h = (h >> 5) | (h << 27);
        h ^= (*c >= 'a' && *c <= 'z') ? (uint32_t)(*c - 'a' + 'A') : *c;
    }
    return h;
}

static uint32_t table_count(uint32_t n) {
    if (n < 3) return 3;
    if (n < 19) return n | 1;
    uint32_t c = n;
    while (c % 2 == 0 || c % 3 == 0 || c % 5 == 0 || c % 7 == 0 || c % 11 == 0 || c % 13 == 0 || c % 17 == 0) c++;
    return c;
}

static void chain(uint8_t* table, uint32_t count, uint32_t hash, uint8_t* entry_hash, uint32_t offset) {
    uint8_t* slot = table + (size_t)(hash % count) * 4;
    memcpy(entry_hash, slot, 4);
    put_be32(slot, offset);
}

static void put_name(uint8_t* e, size_t at, const char* name) {
    size_t n = strlen(name);
    put_be32(e + at - 4, (uint32_t)n);
    memcpy(e + at, name, n);
}

static void build_archive(const WDir* dirs, int ndirs, Buf* out) {
    uint32_t dir_off[8];
    uint32_t dir_bytes = 0x18;
    for (int i = 0; i < ndirs; i++) {
        dir_off[i] = dir_bytes;
        dir_bytes += 0x18 + align4(strlen(dirs[i].name));
    }
    int nfiles = 0;
    for (int i = 0; i < ndirs; i++) nfiles += dirs[i].count;
    uint32_t* file_off = xcalloc((size_t)nfiles + 1, sizeof(uint32_t));
    uint64_t* data_off = xcalloc((size_t)nfiles + 1, sizeof(uint64_t));
    uint32_t file_bytes = 0;
    uint64_t part = 0;
    for (int i = 0, k = 0; i < ndirs; i++) {
        for (int j = 0; j < dirs[i].count; j++, k++) {
            part = (part + 15) & ~(uint64_t)15;
            data_off[k] = part;
            part += dirs[i].files[j].len;
            file_off[k] = file_bytes;
            file_bytes += 0x20 + align4(strlen(dirs[i].files[j].name));
        }
    }
    uint32_t dcount = table_count((uint32_t)ndirs + 1), fcount = table_count((uint32_t)nfiles);
    uint8_t* dht = xmalloc((size_t)dcount * 4);
    uint8_t* fht = xmalloc((size_t)fcount * 4);
    memset(dht, 0xFF, (size_t)dcount * 4);
    memset(fht, 0xFF, (size_t)fcount * 4);
    uint8_t* dt = xcalloc(dir_bytes, 1);
    uint8_t* ft = xcalloc(file_bytes ? file_bytes : 1, 1);
    put_be32(dt, 0);
    put_be32(dt + 4, ENTRY_EMPTY);
    put_be32(dt + 8, ndirs ? dir_off[0] : ENTRY_EMPTY);
    put_be32(dt + 12, ENTRY_EMPTY);
    chain(dht, dcount, path_hash(0, ""), dt + 16, 0);
    put_be32(dt + 20, 0);
    for (int i = 0, k = 0; i < ndirs; i++) {
        uint8_t* d = dt + dir_off[i];
        put_be32(d, 0);
        put_be32(d + 4, i + 1 < ndirs ? dir_off[i + 1] : ENTRY_EMPTY);
        put_be32(d + 8, ENTRY_EMPTY);
        put_be32(d + 12, dirs[i].count ? file_off[k] : ENTRY_EMPTY);
        chain(dht, dcount, path_hash(0, dirs[i].name), d + 16, dir_off[i]);
        put_name(d, 0x18, dirs[i].name);
        for (int j = 0; j < dirs[i].count; j++, k++) {
            uint8_t* f = ft + file_off[k];
            put_be32(f, dir_off[i]);
            put_be32(f + 4, j + 1 < dirs[i].count ? file_off[k + 1] : ENTRY_EMPTY);
            put_be64(f + 8, data_off[k]);
            put_be64(f + 16, dirs[i].files[j].len);
            chain(fht, fcount, path_hash(dir_off[i], dirs[i].files[j].name), f + 24, file_off[k]);
            put_name(f, 0x20, dirs[i].files[j].name);
        }
    }
    uint64_t dht_at = (part + PARTITION_AT + 3) & ~(uint64_t)3;
    uint64_t dt_at = dht_at + (uint64_t)dcount * 4;
    uint64_t fht_at = dt_at + dir_bytes;
    uint64_t ft_at = fht_at + (uint64_t)fcount * 4;
    uint8_t head[HEADER_SIZE];
    memset(head, 0, sizeof(head));
    memcpy(head, "WUHB", 4);
    put_be32(head + 4, HEADER_SIZE);
    put_be64(head + 0x08, dht_at);
    put_be64(head + 0x10, (uint64_t)dcount * 4);
    put_be64(head + 0x18, dt_at);
    put_be64(head + 0x20, dir_bytes);
    put_be64(head + 0x28, fht_at);
    put_be64(head + 0x30, (uint64_t)fcount * 4);
    put_be64(head + 0x38, ft_at);
    put_be64(head + 0x40, file_bytes);
    put_be64(head + 0x48, PARTITION_AT);
    buf_put(out, head, sizeof(head));
    buf_zero(out, PARTITION_AT - HEADER_SIZE);
    for (int i = 0, k = 0; i < ndirs; i++) {
        for (int j = 0; j < dirs[i].count; j++, k++) {
            size_t at = PARTITION_AT + (size_t)data_off[k];
            if (out->len < at) buf_zero(out, at - out->len);
            buf_put(out, dirs[i].files[j].data, dirs[i].files[j].len);
        }
    }
    if (out->len < dht_at) buf_zero(out, (size_t)dht_at - out->len);
    buf_put(out, dht, (size_t)dcount * 4);
    buf_put(out, dt, dir_bytes);
    buf_put(out, fht, (size_t)fcount * 4);
    buf_put(out, ft, file_bytes);
    free(dht);
    free(fht);
    free(dt);
    free(ft);
    free(file_off);
    free(data_off);
}

int wuhb_tga_gz(const Image* img, int alpha, Buf* out) {
    int bpp = alpha ? 4 : 3;
    size_t px = (size_t)img->w * img->h * bpp;
    size_t total = 18 + px + 26;
    uint8_t* t = xcalloc(total, 1);
    t[2] = 2;
    t[12] = (uint8_t)img->w;
    t[13] = (uint8_t)(img->w >> 8);
    t[14] = (uint8_t)img->h;
    t[15] = (uint8_t)(img->h >> 8);
    t[16] = (uint8_t)(bpp * 8);
    t[17] = (uint8_t)(alpha ? 8 : 0);
    for (int y = 0; y < img->h; y++) {
        const uint8_t* src = img->px + (size_t)(img->h - 1 - y) * img->w * 4;
        uint8_t* dst = t + 18 + (size_t)y * img->w * bpp;
        for (int x = 0; x < img->w; x++, src += 4, dst += bpp) {
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            if (alpha) dst[3] = src[3];
        }
    }
    memcpy(t + 18 + px + 8, "TRUEVISION-XFILE.", 18);
    size_t clen = 0;
    void* z = tdefl_compress_mem_to_heap(t, total, &clen, tdefl_create_comp_flags_from_zip_params(MZ_BEST_COMPRESSION, -MZ_DEFAULT_WINDOW_BITS, MZ_DEFAULT_STRATEGY));
    if (!z) {
        free(t);
        return 0;
    }
    static const uint8_t gz_head[10] = {0x1F, 0x8B, 8, 0, 0, 0, 0, 0, 2, 3};
    uint8_t tail[8];
    put_le32(tail, (uint32_t)mz_crc32(MZ_CRC32_INIT, t, total));
    put_le32(tail + 4, (uint32_t)total);
    buf_put(out, gz_head, sizeof(gz_head));
    buf_put(out, z, clen);
    buf_put(out, tail, sizeof(tail));
    mz_free(z);
    free(t);
    return 1;
}

int wuhb_write(const char* path, const Wuhb* w) {
    WFile code[1] = {{"root.rpx", w->rpx, w->rpx_len}};
    WFile meta[4];
    int n = 0;
    meta[n++] = (WFile){"meta.ini", (const uint8_t*)META_INI, sizeof(META_INI) - 1};
    if (w->icon.len) meta[n++] = (WFile){"iconTex.tga.gz", w->icon.data, w->icon.len};
    if (w->tv.len) meta[n++] = (WFile){"bootTvTex.tga.gz", w->tv.data, w->tv.len};
    if (w->drc.len) meta[n++] = (WFile){"bootDrcTex.tga.gz", w->drc.data, w->drc.len};
    WDir dirs[2] = {{"code", code, 1}, {"meta", meta, n}};
    Buf out = {0};
    build_archive(dirs, 2, &out);
    int ok = file_write(path, out.data, out.len);
    buf_free(&out);
    return ok;
}

int wuhb_is_wuhb(const uint8_t magic[4]) { return !memcmp(magic, "WUHB", 4); }

static void take_file(const Buf* b, const uint8_t* e, Buf* out) {
    uint64_t at = PARTITION_AT + be64(e + 8), len = be64(e + 16);
    if (at <= b->len && len <= b->len - at) buf_put(out, b->data + at, (size_t)len);
}

int wuhb_replace_rpx(const char* path, const uint8_t* rpx, size_t len) {
    Buf b = {0};
    if (!file_read(path, &b)) return 0;
    int ok = b.len >= HEADER_SIZE && wuhb_is_wuhb(b.data);
    Wuhb w;
    memset(&w, 0, sizeof(w));
    w.rpx = rpx;
    w.rpx_len = len;
    uint64_t ft_at = ok ? be64(b.data + 0x38) : 0, ft_len = ok ? be64(b.data + 0x40) : 0;
    ok = ok && ft_at <= b.len && ft_len <= b.len - ft_at;
    for (uint64_t at = 0; ok && at + 0x20 <= ft_len;) {
        const uint8_t* e = b.data + ft_at + at;
        uint32_t name_len = be32(e + 0x1C);
        if (at + 0x20 + name_len > ft_len) break;
        const char* name = (const char*)e + 0x20;
        if (name_len == 14 && !memcmp(name, "iconTex.tga.gz", 14)) take_file(&b, e, &w.icon);
        else if (name_len == 16 && !memcmp(name, "bootTvTex.tga.gz", 16)) take_file(&b, e, &w.tv);
        else if (name_len == 17 && !memcmp(name, "bootDrcTex.tga.gz", 17)) take_file(&b, e, &w.drc);
        at += 0x20 + align4(name_len);
    }
    buf_free(&b);
    if (ok) {
        char* tmp = str_fmt("%s.tmp", path);
        ok = wuhb_write(tmp, &w) && file_replace(tmp, path);
        if (!ok) remove(tmp);
        free(tmp);
    }
    buf_free(&w.icon);
    buf_free(&w.tv);
    buf_free(&w.drc);
    return ok;
}
