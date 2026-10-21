#include "ps2disc.h"

#include <stdlib.h>
#include <string.h>

#define SECTOR 2048
#define LBA_PVD 16
#define LBA_ROOT 20
#define LBA_CNF 21
#define LBA_ELF 22
#define ELF_SECTORS 8192
#define LBA_PAK (LBA_ELF + ELF_SECTORS)

static const char CNF[] = "BOOT2 = cdrom0:\\" PS2_BOOT_NAME ";1\r\nVER = 1.08\r\nVMODE = NTSC\r\n";
static const uint8_t STAMP[7] = {126, 1, 1, 0, 0, 0, 0};
static const char LONG_STAMP[] = "2026010100000000";

static void le16(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void be16(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void le32(uint8_t* p, uint32_t v) {
    le16(p, v & 0xFFFF);
    le16(p + 2, v >> 16);
}

static void be32(uint8_t* p, uint32_t v) {
    be16(p, v >> 16);
    be16(p + 2, v & 0xFFFF);
}

static void both16(uint8_t* p, uint32_t v) {
    le16(p, v);
    be16(p + 2, v);
}

static void both32(uint8_t* p, uint32_t v) {
    le32(p, v);
    be32(p + 4, v);
}

static uint32_t rd_le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void pad_str(uint8_t* p, size_t n, const char* s) {
    memset(p, ' ', n);
    size_t l = strlen(s);
    memcpy(p, s, l < n ? l : n);
}

static uint32_t sectors(uint64_t len) { return (uint32_t)((len + SECTOR - 1) / SECTOR); }

static size_t dir_record(uint8_t* p, uint32_t lba, uint32_t size, int dir, const char* name, size_t name_len) {
    size_t len = 33 + name_len + ((name_len & 1) ? 0 : 1);
    memset(p, 0, len);
    p[0] = (uint8_t)len;
    both32(p + 2, lba);
    both32(p + 10, size);
    memcpy(p + 18, STAMP, 7);
    p[25] = dir ? 2 : 0;
    both16(p + 28, 1);
    p[32] = (uint8_t)name_len;
    memcpy(p + 33, name, name_len);
    return len;
}

static int pak_parts(uint64_t pak_len) {
    int n = 1;
    while (pak_len > (uint64_t)n * PS2_PART_BYTES) n++;
    return n;
}

static size_t part_name(int part, char* name) {
    if (!part) {
        memcpy(name, PS2_PAK_NAME ";1", sizeof(PS2_PAK_NAME ";1"));
    } else {
        snprintf(name, 16, "%.*sPK%d;1", (int)(sizeof(PS2_PAK_NAME) - 4), PS2_PAK_NAME, part);
    }
    return strlen(name);
}

static void build_root(uint8_t* dir, uint32_t elf_len, uint64_t pak_len) {
    memset(dir, 0, SECTOR);
    size_t pos = 0;
    pos += dir_record(dir + pos, LBA_ROOT, SECTOR, 1, "\0", 1);
    pos += dir_record(dir + pos, LBA_ROOT, SECTOR, 1, "\1", 1);
    int parts = pak_parts(pak_len);
    for (int i = 0; i < parts; i++) {
        uint64_t at = (uint64_t)i * PS2_PART_BYTES;
        uint64_t len = pak_len - at < PS2_PART_BYTES ? pak_len - at : PS2_PART_BYTES;
        char name[16];
        size_t name_len = part_name(i, name);
        pos += dir_record(dir + pos, LBA_PAK + (uint32_t)(at / SECTOR), (uint32_t)len, 0, name, name_len);
    }
    pos += dir_record(dir + pos, LBA_ELF, elf_len, 0, PS2_BOOT_NAME ";1", strlen(PS2_BOOT_NAME ";1"));
    dir_record(dir + pos, LBA_CNF, (uint32_t)(sizeof(CNF) - 1), 0, "SYSTEM.CNF;1", 12);
}

static uint8_t* find_record(uint8_t* dir, const char* name) {
    size_t name_len = strlen(name);
    for (size_t pos = 0; pos + 33 <= SECTOR && dir[pos];) {
        uint8_t* r = dir + pos;
        if (pos + r[0] > SECTOR || r[0] < 33 + r[32]) return NULL;
        if (r[32] == name_len && memcmp(r + 33, name, name_len) == 0) return r;
        pos += r[0];
    }
    return NULL;
}

static void build_pvd(uint8_t* pvd, uint32_t total) {
    memset(pvd, 0, SECTOR);
    pvd[0] = 1;
    memcpy(pvd + 1, "CD001", 5);
    pvd[6] = 1;
    pad_str(pvd + 8, 32, "PLAYSTATION");
    pad_str(pvd + 40, 32, "OMORI");
    both32(pvd + 80, total);
    both16(pvd + 120, 1);
    both16(pvd + 124, 1);
    both16(pvd + 128, SECTOR);
    both32(pvd + 132, 10);
    le32(pvd + 140, 18);
    be32(pvd + 148, 19);
    dir_record(pvd + 156, LBA_ROOT, SECTOR, 1, "\0", 1);
    pad_str(pvd + 190, 128, "");
    pad_str(pvd + 318, 128, "");
    pad_str(pvd + 446, 128, "");
    pad_str(pvd + 574, 128, "PLAYSTATION");
    pad_str(pvd + 702, 37, "");
    pad_str(pvd + 739, 37, "");
    pad_str(pvd + 776, 37, "");
    memcpy(pvd + 813, LONG_STAMP, 16);
    memcpy(pvd + 830, LONG_STAMP, 16);
    memset(pvd + 847, '0', 16);
    memset(pvd + 864, '0', 16);
    pvd[881] = 1;
}

int ps2_elf_check(const uint8_t* data, size_t len) {
    if (len < 52 || memcmp(data, "\x7f" "ELF", 4) != 0) return 0;
    return data[4] == 1 && data[5] == 1 && data[18] == 8 && data[19] == 0 && len <= (size_t)ELF_SECTORS * SECTOR;
}

int ps2_iso_write_head(FILE* f, const uint8_t* elf, size_t elf_len, uint64_t* pak_at) {
    if (!ps2_elf_check(elf, elf_len)) return 0;
    uint8_t* head = xcalloc((size_t)LBA_ELF, SECTOR);
    build_pvd(head + LBA_PVD * SECTOR, LBA_PAK);
    uint8_t* term = head + 17 * SECTOR;
    term[0] = 255;
    memcpy(term + 1, "CD001", 5);
    term[6] = 1;
    uint8_t* lpt = head + 18 * SECTOR;
    lpt[0] = 1;
    le32(lpt + 2, LBA_ROOT);
    le16(lpt + 6, 1);
    uint8_t* mpt = head + 19 * SECTOR;
    mpt[0] = 1;
    be32(mpt + 2, LBA_ROOT);
    be16(mpt + 6, 1);
    build_root(head + LBA_ROOT * SECTOR, (uint32_t)elf_len, 0);
    memcpy(head + LBA_CNF * SECTOR, CNF, sizeof(CNF) - 1);
    int ok = fwrite(head, SECTOR, LBA_ELF, f) == LBA_ELF && fwrite(elf, 1, elf_len, f) == elf_len;
    free(head);
    uint8_t* zero = xcalloc(1, 1 << 16);
    uint64_t left = (uint64_t)ELF_SECTORS * SECTOR - elf_len;
    while (ok && left) {
        size_t n = left < (1u << 16) ? (size_t)left : (1u << 16);
        ok = fwrite(zero, 1, n, f) == n;
        left -= n;
    }
    free(zero);
    *pak_at = (uint64_t)LBA_PAK * SECTOR;
    return ok;
}

int ps2_iso_write_tail(FILE* f, uint32_t elf_len, uint64_t pak_len) {
    if (pak_len > (uint64_t)(0xFFFFFFFFu - LBA_PAK) * SECTOR) {
        fprintf(stderr, "error: the PS2 data is larger than 4 GB, which a disc image can't hold in one file\n");
        return 0;
    }
    uint8_t zero[SECTOR] = {0};
    size_t pad = (size_t)((uint64_t)sectors(pak_len) * SECTOR - pak_len);
    if (fwrite(zero, 1, pad, f) != pad) return 0;
    uint32_t total = LBA_PAK + sectors(pak_len);
    uint8_t field[8];
    both32(field, total);
    if (!file_seek(f, (uint64_t)LBA_PVD * SECTOR + 80) || fwrite(field, 1, 8, f) != 8) return 0;
    uint8_t root[SECTOR];
    build_root(root, elf_len, pak_len);
    return file_seek(f, (uint64_t)LBA_ROOT * SECTOR) && fwrite(root, 1, SECTOR, f) == SECTOR;
}

int ps2_iso_is_iso(FILE* f) {
    uint8_t pvd[8];
    uint8_t root[SECTOR];
    if (!file_seek(f, (uint64_t)LBA_PVD * SECTOR) || fread(pvd, 1, 8, f) != 8 || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) return 0;
    if (!file_seek(f, (uint64_t)LBA_ROOT * SECTOR) || fread(root, 1, SECTOR, f) != SECTOR) return 0;
    const uint8_t* pak = find_record(root, PS2_PAK_NAME ";1");
    const uint8_t* elf = find_record(root, PS2_BOOT_NAME ";1");
    return pak && elf && rd_le32(pak + 2) == LBA_PAK && rd_le32(elf + 2) == LBA_ELF;
}

int ps2_iso_replace_elf(const char* path, const uint8_t* elf, size_t elf_len) {
    if (!ps2_elf_check(elf, elf_len)) return 0;
    FILE* f = file_open(path, "r+b");
    if (!f) return 0;
    uint8_t root[SECTOR];
    int ok = ps2_iso_is_iso(f) && file_seek(f, (uint64_t)LBA_ROOT * SECTOR) && fread(root, 1, SECTOR, f) == SECTOR;
    uint8_t* r = ok ? find_record(root, PS2_BOOT_NAME ";1") : NULL;
    if (!r) ok = 0;
    if (r) {
        uint32_t old_len = rd_le32(r + 10);
        ok = file_seek(f, (uint64_t)LBA_ELF * SECTOR) && fwrite(elf, 1, elf_len, f) == elf_len;
        uint8_t zero[SECTOR] = {0};
        uint64_t left = old_len > elf_len ? old_len - elf_len : 0;
        while (ok && left) {
            size_t n = left < SECTOR ? (size_t)left : SECTOR;
            ok = fwrite(zero, 1, n, f) == n;
            left -= n;
        }
        both32(r + 10, (uint32_t)elf_len);
        ok = ok && file_seek(f, (uint64_t)LBA_ROOT * SECTOR) && fwrite(root, 1, SECTOR, f) == SECTOR;
    }
    if (fclose(f) != 0) ok = 0;
    return ok;
}

static void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t* p, uint32_t v) {
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

static void putf(uint8_t* p, float f) {
    uint32_t v;
    memcpy(&v, &f, 4);
    put32(p, v);
}

static uint16_t sjis(char c) {
    if (c >= 'A' && c <= 'Z') return (uint16_t)(0x8260 + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (uint16_t)(0x8281 + (c - 'a'));
    if (c >= '0' && c <= '9') return (uint16_t)(0x824F + (c - '0'));
    if (c == '-') return 0x817C;
    if (c == ':') return 0x8146;
    if (c == '.') return 0x8144;
    if (c == '\'') return 0x8166;
    return 0x8140;
}

void ps2_icon_sys(const char* title, Buf* out) {
    uint8_t s[964];
    memset(s, 0, sizeof(s));
    memcpy(s, "PS2D", 4);
    put32(s + 12, 0x60);
    static const uint32_t bg[4][3] = {{232, 232, 232}, {232, 232, 232}, {96, 96, 96}, {96, 96, 96}};
    for (int i = 0; i < 4; i++) {
        for (int c = 0; c < 3; c++) put32(s + 16 + i * 16 + c * 4, bg[i][c]);
    }
    static const float dir[3][3] = {{0.5f, 0.5f, 0.5f}, {0.0f, -0.4f, -0.1f}, {-0.5f, -0.5f, 0.5f}};
    static const float col[3][3] = {{0.5f, 0.5f, 0.5f}, {0.3f, 0.3f, 0.3f}, {0.2f, 0.2f, 0.2f}};
    for (int i = 0; i < 3; i++) {
        for (int c = 0; c < 3; c++) {
            putf(s + 80 + i * 16 + c * 4, dir[i][c]);
            putf(s + 128 + i * 16 + c * 4, col[i][c]);
        }
    }
    for (int c = 0; c < 3; c++) putf(s + 176 + c * 4, 0.75f);
    size_t pos = 0;
    for (size_t i = 0; title[i] && pos + 2 <= 66; i++) {
        uint16_t v = sjis(title[i]);
        s[192 + pos] = (uint8_t)(v >> 8);
        s[192 + pos + 1] = (uint8_t)v;
        pos += 2;
    }
    put16(s + 6, (uint16_t)pos);
    for (int k = 0; k < 3; k++) memcpy(s + 260 + k * 64, "icon.icn", 8);
    buf_put(out, s, sizeof(s));
}

static void vertex(Buf* out, int x, int y, int u, int v) {
    uint8_t p[24];
    put16(p, (uint16_t)(int16_t)x);
    put16(p + 2, (uint16_t)(int16_t)y);
    put16(p + 4, 0);
    put16(p + 6, 0x400);
    put16(p + 8, 0);
    put16(p + 10, 0);
    put16(p + 12, (uint16_t)(int16_t)-4096);
    put16(p + 14, 0x400);
    put16(p + 16, (uint16_t)(int16_t)u);
    put16(p + 18, (uint16_t)(int16_t)v);
    p[20] = 0x80;
    p[21] = 0x80;
    p[22] = 0x80;
    p[23] = 0x80;
    buf_put(out, p, sizeof(p));
}

void ps2_icon_icn(const Image* art, Buf* out) {
    uint8_t h[20];
    put32(h, 0x010000);
    put32(h + 4, 1);
    put32(h + 8, 0x07);
    putf(h + 12, 1.0f);
    put32(h + 16, 6);
    buf_put(out, h, sizeof(h));
    const int x0 = -6554, x1 = 6554, y0 = -16794, y1 = -3686;
    vertex(out, x0, y0, 0, 0);
    vertex(out, x1, y0, 4096, 0);
    vertex(out, x1, y1, 4096, 4096);
    vertex(out, x0, y0, 0, 0);
    vertex(out, x1, y1, 4096, 4096);
    vertex(out, x0, y1, 0, 4096);
    uint8_t anim[36];
    memset(anim, 0, sizeof(anim));
    put32(anim, 1);
    put32(anim + 4, 1);
    put32(anim + 16, 1);
    put32(anim + 24, 1);
    putf(anim + 32, 1.0f);
    buf_put(out, anim, sizeof(anim));
    Image* tex = img_resize(art, 128, 128, FILTER_LANCZOS);
    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {
            const uint8_t* p = tex->px + ((size_t)y * 128 + x) * 4;
            int a = p[3];
            int r = (p[0] * a + 255 * (255 - a)) / 255, g = (p[1] * a + 255 * (255 - a)) / 255, b = (p[2] * a + 255 * (255 - a)) / 255;
            uint16_t v = (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | 0x8000);
            uint8_t q[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
            buf_put(out, q, 2);
        }
    }
    img_free(tex);
}
