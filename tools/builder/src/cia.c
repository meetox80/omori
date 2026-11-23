#include "cia.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "ctr_certs.h"

#define MEDIA 0x200u
#define PAGE 0x1000u
#define CODE_BASE 0x100000u

typedef struct {
    uint32_t addr, size, mem;
    const uint8_t* data;
    int found;
} Segment;

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void le16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void le32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void le64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void be16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void be32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (24 - 8 * i));
}

static void be64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (56 - 8 * i));
}

static uint32_t pages(uint32_t size) { return (size + PAGE - 1) / PAGE; }
static size_t align(size_t v, size_t a) { return (v + a - 1) / a * a; }

static void put_text(uint8_t* dst, const char* s, size_t cap) {
    size_t n = strlen(s);
    memcpy(dst, s, n < cap ? n : cap);
}

static int read_segments(const uint8_t* elf, size_t len, Segment* text, Segment* ro, Segment* data) {
    memset(text, 0, sizeof(*text));
    memset(ro, 0, sizeof(*ro));
    memset(data, 0, sizeof(*data));
    if (len < 0x34 || memcmp(elf, "\x7f" "ELF", 4) != 0 || elf[4] != 1 || elf[5] != 1 || rd16(elf + 0x12) != 40) return 0;
    uint32_t phoff = rd32(elf + 0x1C);
    uint16_t phsize = rd16(elf + 0x2A), phnum = rd16(elf + 0x2C);
    if (phsize < 0x20 || phoff + (uint64_t)phsize * phnum > len) return 0;
    for (uint16_t i = 0; i < phnum; i++) {
        const uint8_t* ph = elf + phoff + (size_t)i * phsize;
        if (rd32(ph) != 1) continue;
        uint32_t off = rd32(ph + 4), vaddr = rd32(ph + 8), filesz = rd32(ph + 16), memsz = rd32(ph + 20), flags = rd32(ph + 24);
        if (off + (uint64_t)filesz > len || memsz < filesz) return 0;
        Segment* s = (flags & 1) ? text : (flags & 2) ? data : ro;
        if (s->found) return 0;
        *s = (Segment){vaddr, filesz, memsz, elf + off, 1};
    }
    if (!text->found || text->addr != CODE_BASE || text->size != text->mem) return 0;
    uint32_t ro_at = text->addr + pages(text->size) * PAGE;
    if (!ro->found) *ro = (Segment){ro_at, 0, 0, NULL, 1};
    if (ro->addr != ro_at || ro->size != ro->mem) return 0;
    uint32_t data_at = ro->addr + pages(ro->size) * PAGE;
    if (!data->found) *data = (Segment){data_at, 0, 0, NULL, 1};
    return data->addr == data_at;
}

static void put_codeset(uint8_t* p, const Segment* s) {
    le32(p, s->addr);
    le32(p + 4, pages(s->size));
    le32(p + 8, s->size);
}

static void build_aci(uint8_t* aci, uint64_t title_id) {
    le64(aci, title_id);
    le32(aci + 0x08, 2);
    aci[0x0E] = 0x04;
    aci[0x0F] = 0x30;
    le16(aci + 0x10, 0x009E);
    aci[0x48] = 0x80;
    aci[0x4F] = 0x01;
    static const char* const services[] = {"APT:U", "fs:USER", "gsp::Gpu", "hid:USER"};
    for (size_t i = 0; i < sizeof(services) / sizeof(services[0]); i++) put_text(aci + 0x50 + i * 8, services[i], 8);
    static const uint8_t svc_ranges[][2] = {{0x01, 0x3E}, {0x47, 0x56}};
    uint32_t masks[8] = {0};
    for (size_t r = 0; r < sizeof(svc_ranges) / sizeof(svc_ranges[0]); r++) {
        for (unsigned svc = svc_ranges[r][0]; svc <= svc_ranges[r][1]; svc++) masks[svc / 24] |= 1u << (svc % 24);
    }
    uint32_t caps[28];
    int n = 0;
    for (uint32_t i = 0; i < 8; i++) {
        if (masks[i]) caps[n++] = 0xF0000000u | i << 24 | masks[i];
    }
    caps[n++] = 0xFF00012Du;
    caps[n++] = 0xFE000200u;
    caps[n++] = 0xFC000221u;
    while (n < 28) caps[n++] = 0xFFFFFFFFu;
    for (int i = 0; i < 28; i++) le32(aci + 0x170 + i * 4, caps[i]);
    aci[0x1F1] = 0x03;
    aci[0x1FF] = 0x02;
}

static void exefs_add(Buf* exefs, int slot, const char* name, const uint8_t* data, size_t len) {
    size_t at = exefs->len - MEDIA;
    put_text(exefs->data + slot * 16, name, 8);
    le32(exefs->data + slot * 16 + 8, (uint32_t)at);
    le32(exefs->data + slot * 16 + 12, (uint32_t)len);
    sha256(data, len, exefs->data + 0xC0 + (9 - slot) * 32);
    buf_put(exefs, data, len);
    buf_zero(exefs, align(len, MEDIA) - len);
}

static int build_ncch(const CiaApp* app, Buf* out) {
    Segment text, ro, data;
    if (!read_segments(app->elf, app->elf_len, &text, &ro, &data)) {
        fprintf(stderr, "error: the 3DS forwarder is not a valid 3DS ELF\n");
        return 0;
    }
    Buf code = {0};
    buf_put(&code, text.data, text.size);
    buf_zero(&code, pages(text.size) * PAGE - text.size);
    if (ro.size) buf_put(&code, ro.data, ro.size);
    buf_zero(&code, pages(ro.size) * PAGE - ro.size);
    if (data.size) buf_put(&code, data.data, data.size);
    buf_zero(&code, pages(data.size) * PAGE - data.size);

    uint8_t ex[0x800];
    memset(ex, 0, sizeof(ex));
    put_text(ex, app->name, 8);
    ex[0x0D] = 0x02;
    put_codeset(ex + 0x10, &text);
    le32(ex + 0x1C, 0x10000);
    put_codeset(ex + 0x20, &ro);
    put_codeset(ex + 0x30, &data);
    le32(ex + 0x3C, data.mem - data.size);
    le64(ex + 0x1C8, app->title_id);
    build_aci(ex + 0x200, app->title_id);
    memcpy(ex + 0x600, ex + 0x200, 0x200);
    ex[0x60E] |= 0x01;
    ex[0x60F] = 0x18;

    Buf exefs = {0};
    buf_zero(&exefs, MEDIA);
    exefs_add(&exefs, 0, ".code", code.data, code.len);
    exefs_add(&exefs, 1, "banner", app->banner, app->banner_len);
    exefs_add(&exefs, 2, "icon", app->smdh, app->smdh_len);
    buf_free(&code);

    uint8_t hdr[0x200];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr + 0x100, "NCCH", 4);
    size_t total = 0xA00 + exefs.len;
    le32(hdr + 0x104, (uint32_t)(total / MEDIA));
    le64(hdr + 0x108, app->title_id);
    memcpy(hdr + 0x110, "00", 2);
    le16(hdr + 0x112, 2);
    le64(hdr + 0x118, app->title_id);
    put_text(hdr + 0x150, app->product_code, 16);
    sha256(ex, 0x400, hdr + 0x160);
    le32(hdr + 0x180, 0x400);
    hdr[0x18C] = 0x01;
    hdr[0x18D] = 0x02;
    hdr[0x18F] = 0x07;
    le32(hdr + 0x1A0, 0xA00 / MEDIA);
    le32(hdr + 0x1A4, (uint32_t)(exefs.len / MEDIA));
    le32(hdr + 0x1A8, 1);
    sha256(exefs.data, MEDIA, hdr + 0x1C0);

    buf_put(out, hdr, sizeof(hdr));
    buf_put(out, ex, sizeof(ex));
    buf_put(out, exefs.data, exefs.len);
    buf_free(&exefs);
    return 1;
}

static void put_signature_head(uint8_t* p) { be32(p, 0x00010004); }

static void build_ticket(const CiaApp* app, uint8_t* t) {
    memset(t, 0, 0x350);
    put_signature_head(t);
    put_text(t + 0x140, "Root-CA00000003-XS0000000c", 0x40);
    t[0x1BC] = 1;
    be64(t + 0x1D0, app->title_id & 0xFFFFFFFFu);
    be64(t + 0x1DC, app->title_id);
    static const uint32_t index_head[] = {0x00010014, 0xAC, 0x14, 0x00010014, 0, 0x28, 1, 0x84, 0x84, 0x00030000};
    for (int i = 0; i < 10; i++) be32(t + 0x2A4 + i * 4, index_head[i]);
    t[0x2D0] = 0x01;
}

static void build_tmd(const CiaApp* app, const uint8_t* content, size_t content_len, uint8_t* m) {
    memset(m, 0, 0xB34);
    put_signature_head(m);
    put_text(m + 0x140, "Root-CA00000003-CP0000000b", 0x40);
    m[0x180] = 1;
    be64(m + 0x18C, app->title_id);
    be32(m + 0x194, 0x40);
    be16(m + 0x1DE, 1);
    uint8_t* chunk = m + 0xB04;
    uint8_t digest[32];
    sha256(content, content_len, digest);
    be32(chunk, (uint32_t)digest[0] << 24 | (uint32_t)digest[1] << 16 | (uint32_t)digest[2] << 8 | digest[3]);
    be64(chunk + 8, content_len);
    memcpy(chunk + 16, digest, 32);
    be16(m + 0x204 + 2, 1);
    sha256(chunk, 0x30, m + 0x204 + 4);
    sha256(m + 0x204, 0x900, m + 0x1E4);
}

static void pad_to(Buf* b, size_t a) { buf_zero(b, align(b->len, a) - b->len); }

int cia_build(const CiaApp* app, Buf* out) {
    if (app->smdh_len != 0x36C0) {
        fprintf(stderr, "error: the 3DS icon is missing, so the HOME Menu CIA can't be made\n");
        return 0;
    }
    Buf ncch = {0};
    if (!build_ncch(app, &ncch)) return 0;
    uint8_t ticket[0x350], tmd[0xB34];
    build_ticket(app, ticket);
    build_tmd(app, ncch.data, ncch.len, tmd);
    uint8_t meta[0x3AC0];
    memset(meta, 0, sizeof(meta));
    le32(meta + 0x300, 2);
    memcpy(meta + 0x400, app->smdh, 0x36C0);

    uint8_t head[0x2020];
    memset(head, 0, sizeof(head));
    le32(head, sizeof(head));
    le32(head + 0x08, CTR_CERT_CHAIN_SIZE);
    le32(head + 0x0C, sizeof(ticket));
    le32(head + 0x10, sizeof(tmd));
    le32(head + 0x14, sizeof(meta));
    le64(head + 0x18, ncch.len);
    head[0x20] = 0x80;

    buf_put(out, head, sizeof(head));
    pad_to(out, 64);
    buf_put(out, ctr_cert_chain, CTR_CERT_CHAIN_SIZE);
    pad_to(out, 64);
    buf_put(out, ticket, sizeof(ticket));
    pad_to(out, 64);
    buf_put(out, tmd, sizeof(tmd));
    pad_to(out, 64);
    buf_put(out, ncch.data, ncch.len);
    pad_to(out, 64);
    buf_put(out, meta, sizeof(meta));
    buf_free(&ncch);
    return 1;
}
