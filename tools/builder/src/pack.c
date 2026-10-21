#include "pack.h"

#include <stdlib.h>
#include <string.h>

PackEntry* pack_add(Pack* p, const char* name) {
    if (p->count == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 256;
        p->items = xrealloc(p->items, sizeof(PackEntry) * (size_t)p->cap);
    }
    PackEntry* e = &p->items[p->count++];
    memset(e, 0, sizeof(*e));
    e->name = xstrdup(name);
    return e;
}

static int cmp_entry(const void* a, const void* b) { return strcmp((*(const PackEntry* const*)a)->name, (*(const PackEntry* const*)b)->name); }

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int sink_write(Pack* p, const void* data, size_t len) {
    if (len && fwrite(data, 1, len, p->sink) != len) {
        p->failed = 1;
        return 0;
    }
    p->pos += len;
    return 1;
}

static void sink_pad(Pack* p) {
    static const uint8_t zero[64];
    size_t pad = (size_t)((64 - p->pos % 64) % 64);
    sink_write(p, zero, pad);
}

int pack_open(Pack* p, FILE* sink) {
    p->sink = sink;
    p->base = 0;
    p->pos = 0;
    p->next = 0;
    p->failed = 0;
    p->lock = mutex_new();
    uint8_t h[16] = {0};
    memcpy(h, "TPAK", 4);
    put32(h + 4, 2);
    return sink_write(p, h, 16);
}

static void commit(Pack* p, PackEntry* e) {
    sink_pad(p);
    if (p->pos > 0xFFFFFFFFu || e->data.len > 0xFFFFFFFFu) p->failed = 1;
    e->offset = (uint32_t)p->pos;
    e->size = (uint32_t)e->data.len;
    sink_write(p, e->data.data, e->data.len);
    buf_free(&e->data);
}

static void advance(Pack* p) {
    while (p->next < p->count && p->items[p->next].ready) commit(p, &p->items[p->next++]);
}

void pack_ready(Pack* p, PackEntry* e) {
    if (!p->sink) {
        e->ready = 1;
        return;
    }
    mutex_lock(p->lock);
    e->ready = 1;
    advance(p);
    mutex_unlock(p->lock);
}

void pack_flush(Pack* p) {
    if (!p->sink) return;
    mutex_lock(p->lock);
    for (int i = p->next; i < p->count; i++) p->items[i].ready = 1;
    advance(p);
    mutex_unlock(p->lock);
}

int pack_finish(Pack* p, uint64_t* total) {
    pack_flush(p);
    PackEntry** order = xmalloc(sizeof(PackEntry*) * (size_t)(p->count ? p->count : 1));
    for (int i = 0; i < p->count; i++) order[i] = &p->items[i];
    qsort(order, (size_t)p->count, sizeof(PackEntry*), cmp_entry);
    size_t names = 0;
    for (int i = 0; i < p->count; i++) names += strlen(order[i]->name) + 1;
    sink_pad(p);
    uint64_t index_at = p->pos;
    Buf idx = {0};
    size_t name_off = 0;
    for (int i = 0; i < p->count; i++) {
        uint8_t e[12];
        put32(e, (uint32_t)name_off);
        put32(e + 4, order[i]->offset);
        put32(e + 8, order[i]->size);
        buf_put(&idx, e, 12);
        name_off += strlen(order[i]->name) + 1;
    }
    for (int i = 0; i < p->count; i++) buf_put(&idx, order[i]->name, strlen(order[i]->name) + 1);
    sink_write(p, idx.data, idx.len);
    buf_free(&idx);
    free(order);
    uint8_t t[16];
    memcpy(t, "TPKI", 4);
    put32(t + 4, (uint32_t)p->count);
    put32(t + 8, (uint32_t)names);
    put32(t + 12, (uint32_t)index_at);
    if (index_at > 0xFFFFFFFFu) p->failed = 1;
    sink_write(p, t, 16);
    if (total) *total = p->pos;
    return !p->failed;
}

void pack_free(Pack* p) {
    for (int i = 0; i < p->count; i++) {
        free(p->items[i].name);
        free(p->items[i].error);
        buf_free(&p->items[i].data);
    }
    free(p->items);
    if (p->lock) mutex_free(p->lock);
    memset(p, 0, sizeof(*p));
}

static uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

int pbp_read(const uint8_t* data, size_t len, Pbp* out) {
    memset(out, 0, sizeof(*out));
    if (len < 40 || memcmp(data, "\0PBP", 4) != 0) return 0;
    uint32_t off[9];
    for (int i = 0; i < 8; i++) off[i] = get32(data + 8 + i * 4);
    off[8] = (uint32_t)len;
    for (int i = 0; i < 8; i++) {
        if (off[i] > off[i + 1] || off[i + 1] > len) return 0;
        buf_put(&out->part[i], data + off[i], off[i + 1] - off[i]);
    }
    return 1;
}

int pbp_write_head(const Pbp* p, FILE* f) {
    Buf head = {0};
    uint32_t off = 40;
    buf_put(&head, "\0PBP", 4);
    buf_u32(&head, 0x00010000);
    for (int i = 0; i < 8; i++) {
        buf_u32(&head, off);
        if (i < 7) off += (uint32_t)p->part[i].len;
    }
    int ok = fwrite(head.data, 1, head.len, f) == head.len;
    buf_free(&head);
    for (int i = 0; i < 7 && ok; i++) {
        if (p->part[i].len && fwrite(p->part[i].data, 1, p->part[i].len, f) != p->part[i].len) ok = 0;
    }
    return ok;
}

void pbp_free(Pbp* p) {
    for (int i = 0; i < 8; i++) buf_free(&p->part[i]);
}

void sfo_build(const char* title, Buf* out) {
    SfoEntry e[] = {
        {"BOOTABLE", 1, 1, NULL, 0},      {"CATEGORY", 0, 0, "MG", 4},          {"DISC_ID", 0, 0, "OMORI0001", 16},
        {"DISC_VERSION", 0, 0, "1.00", 8}, {"MEMSIZE", 1, 1, NULL, 0},           {"PARENTAL_LEVEL", 1, 1, NULL, 0},
        {"PSP_SYSTEM_VER", 0, 0, "1.00", 8}, {"REGION", 1, 32768, NULL, 0},       {"TITLE", 0, 0, title, 128},
    };
    sfo_write(e, (int)(sizeof(e) / sizeof(e[0])), out);
}

void sfo_write(const SfoEntry* e, int n, Buf* out) {
    Buf keys = {0}, vals = {0}, index = {0};
    for (int i = 0; i < n; i++) {
        uint32_t key_off = (uint32_t)keys.len;
        buf_put(&keys, e[i].key, strlen(e[i].key) + 1);
        uint32_t data_off = (uint32_t)vals.len;
        uint16_t fmt;
        uint32_t length, maxlen;
        if (e[i].is_int) {
            buf_u32(&vals, e[i].ival);
            fmt = 0x0404;
            length = maxlen = 4;
        } else {
            length = (uint32_t)strlen(e[i].sval) + 1;
            maxlen = e[i].max ? e[i].max : (length + 3) / 4 * 4;
            buf_put(&vals, e[i].sval, length);
            buf_zero(&vals, maxlen - length);
            fmt = 0x0204;
        }
        buf_u16(&index, (uint16_t)key_off);
        buf_u16(&index, fmt);
        buf_u32(&index, length);
        buf_u32(&index, maxlen);
        buf_u32(&index, data_off);
    }
    while (keys.len % 4) buf_u8(&keys, 0);
    uint32_t key_start = 20 + (uint32_t)index.len;
    uint32_t data_start = key_start + (uint32_t)keys.len;
    buf_put(out, "\0PSF", 4);
    buf_u32(out, 0x00000101);
    buf_u32(out, key_start);
    buf_u32(out, data_start);
    buf_u32(out, (uint32_t)n);
    buf_put(out, index.data, index.len);
    buf_put(out, keys.data, keys.len);
    buf_put(out, vals.data, vals.len);
    buf_free(&keys);
    buf_free(&vals);
    buf_free(&index);
}

int x3dsx_read(const uint8_t* data, size_t len, Exe3dsx* out) {
    memset(out, 0, sizeof(*out));
    if (len < 0x20 || memcmp(data, "3DSX", 4) != 0) return 0;
    uint32_t header = (uint32_t)(data[4] | data[5] << 8);
    uint32_t reloc_hdr = (uint32_t)(data[6] | data[7] << 8);
    if (header < 0x20 || reloc_hdr < 8 || header + 3 * reloc_hdr > len) return 0;
    uint64_t code = get32(data + 0x10), rodata = get32(data + 0x14), dat = get32(data + 0x18), bss = get32(data + 0x1C);
    if (bss > dat) return 0;
    uint64_t end = header + 3 * (uint64_t)reloc_hdr + code + rodata + (dat - bss);
    for (int i = 0; i < 3; i++) {
        const uint8_t* r = data + header + (size_t)i * reloc_hdr;
        end += ((uint64_t)get32(r) + get32(r + 4)) * 4;
    }
    if (end > len) return 0;
    buf_put(&out->head, data, 0x20);
    buf_put(&out->body, data + header, (size_t)(end - header));
    return 1;
}

int x3dsx_write(const Exe3dsx* x, const Buf* smdh, FILE* f, uint64_t* pack_at) {
    uint32_t smdh_at = 0x2C + (uint32_t)x->body.len;
    uint32_t fs_at = (smdh_at + (uint32_t)smdh->len + 15) & ~15u;
    Buf head = {0};
    buf_put(&head, x->head.data, 0x20);
    head.data[4] = 0x2C;
    head.data[5] = 0;
    buf_u32(&head, smdh_at);
    buf_u32(&head, (uint32_t)smdh->len);
    buf_u32(&head, fs_at);
    int ok = fwrite(head.data, 1, head.len, f) == head.len;
    buf_free(&head);
    ok = ok && fwrite(x->body.data, 1, x->body.len, f) == x->body.len;
    ok = ok && (!smdh->len || fwrite(smdh->data, 1, smdh->len, f) == smdh->len);
    static const uint8_t zero[16] = {0};
    size_t pad = fs_at - (smdh_at + smdh->len);
    ok = ok && (!pad || fwrite(zero, 1, pad, f) == pad);
    if (pack_at) *pack_at = fs_at;
    return ok;
}

void x3dsx_free(Exe3dsx* x) {
    buf_free(&x->head);
    buf_free(&x->body);
}
