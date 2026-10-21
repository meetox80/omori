#include "ps3pkg.h"

#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "progress.h"

#define HEAD_SIZE 0x140
#define TRAILER 0x60
#define ITEMS 6
#define DESC_SIZE 0x20
#define CHUNK (8u << 20)
#define TYPE_NPDRMSELF 0x1
#define TYPE_RAW 0x3
#define TYPE_DIRECTORY 0x4
#define OVERWRITE 0x80000000u

static const char* const NAMES[ITEMS] = {"ICON0.PNG", "PARAM.SFO", "PIC1.PNG", "USRDIR", "USRDIR/EBOOT.BIN", "USRDIR/omori.pak"};
enum { ITEM_ICON0, ITEM_SFO, ITEM_PIC1, ITEM_USRDIR, ITEM_EBOOT, ITEM_PACK };

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static uint64_t be64(const uint8_t* p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }

static void put_be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

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

static uint64_t align16(uint64_t v) { return (v + 15) & ~(uint64_t)15; }

static void xor_keystream(const uint8_t key[16], uint64_t counter, uint8_t* data, size_t len) {
    uint8_t ctx[64], hash[20];
    memcpy(ctx, key, 8);
    memcpy(ctx + 8, key, 8);
    memcpy(ctx + 16, key + 8, 8);
    memcpy(ctx + 24, key + 8, 8);
    memset(ctx + 32, 0, 32);
    for (size_t at = 0; at < len; at += 16, counter++) {
        put_be64(ctx + 0x38, counter);
        sha1_block64(ctx, hash);
        size_t n = len - at < 16 ? len - at : 16;
        for (size_t i = 0; i < n; i++) data[at + i] ^= hash[i];
    }
}

static uint64_t eboot_size(size_t len) { return len < 0x30 ? len : ((len - 0x30 + 63) & ~(uint64_t)63) + 0x30; }

static void put_eboot(Buf* out, const Buf* eboot, const char* content_id) {
    static const uint8_t SELF_MAGIC[9] = {'S', 'C', 'E', 0, 0, 0, 0, 2, 0x80};
    const uint8_t* d = eboot->data;
    size_t len = eboot->len;
    size_t meta_at = 0;
    int npdrm = 0;
    if (len >= 0x68 && !memcmp(d, SELF_MAGIC, 9)) {
        uint64_t info = be64(d + 40);
        uint64_t doff = be64(d + 88);
        uint32_t app_type = info + 0x18 <= len ? be32(d + info + 12) : 0;
        int found = 0;
        while (doff + 0x10 <= len) {
            uint32_t type = be32(d + doff), size = be32(d + doff + 4);
            uint64_t next = be64(d + doff + 8);
            if (type == 3) {
                found = 1;
                break;
            }
            doff += size;
            if (next != 1) break;
        }
        doff += 0x10;
        if (app_type == 8 && found && doff + 0x80 <= len) {
            npdrm = 1;
            meta_at = (size_t)doff;
        }
    }
    if (npdrm) {
        uint8_t sum[20], meta[0x80];
        sha1(d, len, sum);
        memset(meta, 0, sizeof(meta));
        put_be32(meta, 0x4E504400u);
        put_be32(meta + 4, 1);
        put_be32(meta + 8, 3);
        put_be32(meta + 12, 1);
        size_t cid = strlen(content_id);
        memcpy(meta + 16, content_id, cid > 0x30 ? 0x30 : cid);
        for (int j = 0; j < 16; j++) {
            meta[0x40 + j] = sum[j];
            meta[0x50 + j] = (uint8_t)~sum[j];
            meta[0x60 + j] = (uint8_t)((j == 15 ? 1 : 0) ^ meta[0x50 + j] ^ 0xAA);
        }
        buf_put(out, d, meta_at);
        buf_put(out, meta, sizeof(meta));
        buf_put(out, d + meta_at + 0x80, len - meta_at - 0x80);
    } else {
        buf_put(out, d, len);
    }
    buf_zero(out, (size_t)(align16(eboot_size(len)) - len));
}

static void build_plain(Ps3Pkg* p, uint64_t pack_len) {
    const Buf* files[ITEMS] = {&p->icon0, &p->sfo, &p->pic1, NULL, &p->eboot, NULL};
    uint64_t name_at[ITEMS], file_at[ITEMS], size[ITEMS];
    uint64_t cur = (uint64_t)DESC_SIZE * ITEMS;
    for (int i = 0; i < ITEMS; i++) {
        name_at[i] = cur;
        cur += align16(strlen(NAMES[i]));
    }
    for (int i = 0; i < ITEMS; i++) {
        file_at[i] = cur;
        if (i == ITEM_USRDIR) size[i] = 0;
        else if (i == ITEM_PACK) size[i] = pack_len;
        else if (i == ITEM_EBOOT) size[i] = eboot_size(p->eboot.len);
        else size[i] = files[i]->len;
        if (i != ITEM_USRDIR) cur += align16(size[i]);
    }
    p->plain.len = 0;
    for (int i = 0; i < ITEMS; i++) {
        uint8_t desc[DESC_SIZE] = {0};
        uint32_t type = i == ITEM_USRDIR ? TYPE_DIRECTORY : i == ITEM_EBOOT ? TYPE_NPDRMSELF : TYPE_RAW;
        put_be32(desc, (uint32_t)name_at[i]);
        put_be32(desc + 4, (uint32_t)strlen(NAMES[i]));
        put_be64(desc + 8, file_at[i]);
        put_be64(desc + 16, size[i]);
        put_be32(desc + 24, OVERWRITE | type);
        buf_put(&p->plain, desc, DESC_SIZE);
    }
    for (int i = 0; i < ITEMS; i++) {
        size_t n = strlen(NAMES[i]);
        buf_put(&p->plain, NAMES[i], n);
        buf_zero(&p->plain, (size_t)(align16(n) - n));
    }
    for (int i = 0; i < ITEMS; i++) {
        if (i == ITEM_USRDIR || i == ITEM_PACK) continue;
        if (i == ITEM_EBOOT) {
            put_eboot(&p->plain, &p->eboot, p->content_id);
            continue;
        }
        buf_put(&p->plain, files[i]->data, files[i]->len);
        buf_zero(&p->plain, (size_t)(align16(files[i]->len) - files[i]->len));
    }
}

static size_t desc_area(void) {
    size_t n = (size_t)DESC_SIZE * ITEMS;
    for (int i = 0; i < ITEMS; i++) n += (size_t)align16(strlen(NAMES[i]));
    return n;
}

int ps3pkg_write_head(Ps3Pkg* p, FILE* f, uint64_t* pack_at) {
    build_plain(p, 0);
    uint8_t zero[HEAD_SIZE] = {0};
    if (fwrite(zero, 1, HEAD_SIZE, f) != HEAD_SIZE || fwrite(p->plain.data, 1, p->plain.len, f) != p->plain.len) return 0;
    p->pack_at = HEAD_SIZE + p->plain.len;
    *pack_at = p->pack_at;
    return 1;
}

int ps3pkg_write_tail(FILE* f, uint64_t pack_len) {
    uint8_t zero[16 + TRAILER] = {0};
    size_t n = (size_t)(align16(pack_len) - pack_len) + TRAILER;
    return fwrite(zero, 1, n, f) == n;
}

static int g_seal_step;

static void seal_report(double f) {
    progress_fraction(f);
    int step = (int)(f * 20);
    if (step > g_seal_step) {
        g_seal_step = step;
        log_msg("  progress %d/100", step * 5);
    }
}

typedef struct {
    const char* path;
    uint64_t at;
    uint64_t len;
    uint64_t counter;
    const uint8_t* key;
    int chunks;
    int done;
    int failed;
    double lo, hi;
    Mutex* lock;
} CryptJob;

static void crypt_job(void* ctx, int index) {
    CryptJob* j = ctx;
    uint64_t off = (uint64_t)index * CHUNK;
    size_t n = j->len - off < CHUNK ? (size_t)(j->len - off) : CHUNK;
    uint8_t* buf = malloc(n);
    FILE* f = buf ? file_open(j->path, "r+b") : NULL;
    int ok = f && file_seek(f, j->at + off) && fread(buf, 1, n, f) == n;
    if (ok) {
        xor_keystream(j->key, j->counter + off / 16, buf, n);
        ok = file_seek(f, j->at + off) && fwrite(buf, 1, n, f) == n;
    }
    if (f && fclose(f) != 0) ok = 0;
    free(buf);
    mutex_lock(j->lock);
    if (!ok) j->failed = 1;
    j->done++;
    seal_report(j->lo + (j->hi - j->lo) * j->done / j->chunks);
    mutex_unlock(j->lock);
}

static int crypt_range(const char* path, uint64_t at, uint64_t len, const uint8_t key[16], uint64_t counter, int threads, double lo, double hi) {
    if (!len) return 1;
    CryptJob j = {path, at, len, counter, key, (int)((len + CHUNK - 1) / CHUNK), 0, 0, lo, hi, mutex_new()};
    run_jobs_quiet(crypt_job, &j, j.chunks, threads);
    mutex_free(j.lock);
    return !j.failed;
}

static int hash_range(FILE* f, uint64_t at, uint64_t len, Sha1* s, double lo, double hi) {
    if (!len) return 1;
    if (!file_seek(f, at)) return 0;
    uint8_t* buf = xmalloc(CHUNK);
    uint64_t left = len;
    int ok = 1;
    while (left && ok) {
        size_t n = left < CHUNK ? (size_t)left : CHUNK;
        if (fread(buf, 1, n, f) != n) ok = 0;
        else sha1_update(s, buf, n);
        left -= n;
        seal_report(lo + (hi - lo) * (double)(len - left) / (double)len);
    }
    free(buf);
    return ok;
}

static int finish_range(Ps3Pkg* p, const char* path, uint64_t pack_len, int threads, double lo, double hi) {
    uint64_t data_size = p->plain.len + align16(pack_len);
    if (data_size > 0xFFFFFFFFu) {
        fprintf(stderr, "error: the PS3 package would be larger than 4 GB\n");
        return 0;
    }
    build_plain(p, pack_len);
    FILE* f = file_open(path, "r+b");
    if (!f) return 0;
    uint8_t hdr[0x80] = {0};
    put_be32(hdr, 0x7F504B47u);
    put_be32(hdr + 4, 1);
    put_be32(hdr + 8, 0xC0);
    put_be32(hdr + 12, 5);
    put_be32(hdr + 16, 0x80);
    put_be32(hdr + 20, ITEMS);
    put_be64(hdr + 24, data_size + HEAD_SIZE + TRAILER);
    put_be64(hdr + 32, HEAD_SIZE);
    put_be64(hdr + 40, data_size);
    size_t cid = strlen(p->content_id);
    memcpy(hdr + 0x30, p->content_id, cid > 0x30 ? 0x30 : cid);
    Sha1 qa;
    sha1_init(&qa);
    sha1_update(&qa, p->icon0.data, p->icon0.len);
    sha1_update(&qa, p->sfo.data, p->sfo.len);
    sha1_update(&qa, p->pic1.data, p->pic1.len);
    sha1_update(&qa, p->eboot.data, p->eboot.len);
    double mid = lo + (hi - lo) * 0.3;
    int ok = hash_range(f, p->pack_at, pack_len, &qa, lo, mid);
    uint8_t zero[0x50] = {0};
    sha1_update(&qa, hdr, 0x30);
    sha1_update(&qa, zero, 0x50);
    sha1_update(&qa, p->plain.data, desc_area());
    uint8_t digest[20];
    sha1_final(&qa, digest);
    memcpy(hdr + 0x60, digest, 16);
    memset(hdr + 0x70, 0, 16);
    xor_keystream(hdr + 0x60, 0xFFFFFFFFFFFFFFFFull, hdr + 0x70, 16);
    uint8_t meta[0x40] = {0};
    put_be32(meta, 1);
    put_be32(meta + 4, 4);
    put_be32(meta + 8, 3);
    put_be32(meta + 12, 2);
    put_be32(meta + 16, 4);
    put_be32(meta + 20, 5);
    put_be32(meta + 24, 3);
    put_be32(meta + 28, 4);
    put_be32(meta + 32, 0x0E);
    put_be32(meta + 36, 4);
    put_be32(meta + 40, 8);
    put_be32(meta + 48, (uint32_t)data_size);
    put_be32(meta + 52, 5);
    put_be32(meta + 56, 4);
    put_be16(meta + 60, 0x1061);
    uint8_t head[HEAD_SIZE], hdr_sum[20], meta_sum[20];
    sha1(hdr, sizeof(hdr), hdr_sum);
    sha1(meta, sizeof(meta), meta_sum);
    uint8_t pad1[0x30] = {0}, pad2[0x30];
    xor_keystream(meta_sum + 3, 0, pad1, sizeof(pad1));
    memcpy(pad2, pad1, sizeof(pad2));
    xor_keystream(hdr_sum + 3, 0, pad2, sizeof(pad2));
    memcpy(head, hdr, 0x80);
    memcpy(head + 0x80, hdr_sum + 3, 16);
    memcpy(head + 0x90, pad2, 0x30);
    memcpy(head + 0xC0, meta, 0x40);
    memcpy(head + 0x100, meta_sum + 3, 16);
    memcpy(head + 0x110, pad1, 0x30);
    uint8_t* sealed = xmalloc(p->plain.len);
    memcpy(sealed, p->plain.data, p->plain.len);
    xor_keystream(digest, 0, sealed, p->plain.len);
    ok = ok && file_seek(f, 0) && fwrite(head, 1, HEAD_SIZE, f) == HEAD_SIZE && fwrite(sealed, 1, p->plain.len, f) == p->plain.len;
    free(sealed);
    if (fclose(f) != 0) ok = 0;
    return ok && crypt_range(path, p->pack_at, align16(pack_len), digest, p->plain.len / 16, threads, mid, hi);
}

int ps3pkg_finish(Ps3Pkg* p, const char* path, uint64_t pack_len, int threads) {
    g_seal_step = 0;
    return finish_range(p, path, pack_len, threads, 0, 1);
}

int ps3pkg_is_pkg(const uint8_t magic[4]) { return magic[0] == 0x7F && magic[1] == 'P' && magic[2] == 'K' && magic[3] == 'G'; }

static int read_sealed(FILE* f, const uint8_t key[16], uint64_t rel, uint64_t len, Buf* out) {
    out->len = 0;
    buf_zero(out, (size_t)len);
    if (len && (!file_seek(f, HEAD_SIZE + rel) || fread(out->data, 1, (size_t)len, f) != len)) return 0;
    xor_keystream(key, rel / 16, out->data, (size_t)len);
    return 1;
}

int ps3pkg_replace_eboot(const char* path, const uint8_t* eboot, size_t len, int threads) {
    FILE* f = file_open(path, "rb");
    uint8_t hdr[0x80];
    if (!f || fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) || !ps3pkg_is_pkg(hdr) || be32(hdr + 4) != 1 || be64(hdr + 32) != HEAD_SIZE) {
        if (f) fclose(f);
        return 0;
    }
    uint32_t count = be32(hdr + 20);
    uint8_t key[16];
    memcpy(key, hdr + 0x60, 16);
    char cid[0x31] = {0};
    memcpy(cid, hdr + 0x30, 0x30);
    Ps3Pkg p;
    memset(&p, 0, sizeof(p));
    p.content_id = cid;
    Buf descs = {0}, name = {0};
    uint64_t pack_rel = 0, pack_len = 0;
    int have = 0;
    int ok = count > 0 && count < 64 && read_sealed(f, key, 0, (uint64_t)count * DESC_SIZE, &descs);
    for (uint32_t i = 0; ok && i < count; i++) {
        const uint8_t* d = descs.data + (size_t)i * DESC_SIZE;
        uint32_t name_at = be32(d), name_len = be32(d + 4);
        uint64_t at = be64(d + 8), size = be64(d + 16);
        if (name_len > 255 || (name_at & 15) || (at & 15) || !read_sealed(f, key, name_at, name_len, &name)) {
            ok = 0;
            break;
        }
        buf_u8(&name, 0);
        const char* n = (const char*)name.data;
        Buf* slot = !strcmp(n, NAMES[ITEM_ICON0]) ? &p.icon0 : !strcmp(n, NAMES[ITEM_SFO]) ? &p.sfo : !strcmp(n, NAMES[ITEM_PIC1]) ? &p.pic1 : NULL;
        if (slot) {
            ok = read_sealed(f, key, at, size, slot);
            have++;
        } else if (!strcmp(n, NAMES[ITEM_PACK])) {
            pack_rel = at;
            pack_len = size;
            have++;
        }
    }
    buf_free(&descs);
    buf_free(&name);
    ok = ok && have == 4;
    char* tmp = str_fmt("%s.tmp", path);
    FILE* o = ok ? file_open(tmp, "wb") : NULL;
    if (ok) {
        buf_put(&p.eboot, eboot, len);
        uint64_t pack_at = 0;
        uint8_t trailer[TRAILER] = {0};
        ok = o && ps3pkg_write_head(&p, o, &pack_at) && file_copy_range(f, HEAD_SIZE + pack_rel, align16(pack_len), o) &&
             fwrite(trailer, 1, TRAILER, o) == TRAILER;
    }
    fclose(f);
    if (o && fclose(o) != 0) ok = 0;
    g_seal_step = 0;
    ok = ok && crypt_range(tmp, p.pack_at, align16(pack_len), key, pack_rel / 16, threads, 0, 0.3);
    ok = ok && finish_range(&p, tmp, pack_len, threads, 0.3, 1);
    if (ok) ok = file_replace(tmp, path);
    if (!ok) remove(tmp);
    free(tmp);
    ps3pkg_free(&p);
    return ok;
}

void ps3pkg_free(Ps3Pkg* p) {
    buf_free(&p->icon0);
    buf_free(&p->sfo);
    buf_free(&p->pic1);
    buf_free(&p->eboot);
    buf_free(&p->plain);
}
