#include "package.h"

#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "pack.h"
#include "ps2disc.h"
#include "ps3pkg.h"
#include "util.h"
#include "vpk.h"
#include "wuhb.h"

typedef struct {
    const unsigned char* data;
    size_t size;
} Blob;

#ifdef OMORI_EXTERNAL_ENGINES
static Blob embedded(TargetId id) {
    (void)id;
    return (Blob){NULL, 0};
}

static Blob embedded_sfo(void) { return (Blob){NULL, 0}; }
#else
extern const unsigned char embedded_psp_eboot[];
extern const size_t embedded_psp_eboot_size;
extern const unsigned char embedded_vita_eboot[];
extern const size_t embedded_vita_eboot_size;
extern const unsigned char embedded_vita_sfo[];
extern const size_t embedded_vita_sfo_size;
extern const unsigned char embedded_3ds_engine[];
extern const size_t embedded_3ds_engine_size;
extern const unsigned char embedded_wii_dol[];
extern const size_t embedded_wii_dol_size;
extern const unsigned char embedded_wiiu_rpx[];
extern const size_t embedded_wiiu_rpx_size;
extern const unsigned char embedded_ps3_eboot[];
extern const size_t embedded_ps3_eboot_size;
extern const unsigned char embedded_ps2_elf[];
extern const size_t embedded_ps2_elf_size;
extern const unsigned char embedded_xenon_elf[];
extern const size_t embedded_xenon_elf_size;

static Blob embedded(TargetId id) {
    switch (id) {
        case TARGET_PSP: return (Blob){embedded_psp_eboot, embedded_psp_eboot_size};
        case TARGET_VITA: return (Blob){embedded_vita_eboot, embedded_vita_eboot_size};
        case TARGET_3DS: return (Blob){embedded_3ds_engine, embedded_3ds_engine_size};
        case TARGET_WII: return (Blob){embedded_wii_dol, embedded_wii_dol_size};
        case TARGET_WIIU: return (Blob){embedded_wiiu_rpx, embedded_wiiu_rpx_size};
        case TARGET_PS3: return (Blob){embedded_ps3_eboot, embedded_ps3_eboot_size};
        case TARGET_PS2: return (Blob){embedded_ps2_elf, embedded_ps2_elf_size};
        case TARGET_XENON: return (Blob){embedded_xenon_elf, embedded_xenon_elf_size};
        default: return (Blob){NULL, 0};
    }
}

static Blob embedded_sfo(void) { return (Blob){embedded_vita_sfo, embedded_vita_sfo_size}; }
#endif

static const char PS3_CONTENT_ID[] = "UP0001-OMRI00001_00-0000000000000000";
static const char PS3_LICENSE[] = "An unofficial fan port of OMORI with an engine written from scratch, built from your own copy of the game. OMORI is (c) OMOCAT, "
                                  "LLC. This project is not affiliated with or endorsed by OMOCAT or Sony Interactive Entertainment.";

static const char WII_META[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                               "<app version=\"1\">\n"
                               "  <name>OMORI</name>\n"
                               "  <coder>kotwicha</coder>\n"
                               "  <version>1.0.8</version>\n"
                               "  <short_description>Unofficial port, built from your own copy</short_description>\n"
                               "  <long_description>An unofficial fan port of OMORI with an engine written from scratch. OMORI is (c) OMOCAT, LLC. "
                               "This project is not affiliated with or endorsed by OMOCAT.</long_description>\n"
                               "</app>\n";

struct Package {
    TargetId id;
    char* out;
    char* data_path;
    char* summary;
    Pbp pbp;
    Buf icon0;
    Buf pic1;
    Exe3dsx x3ds;
    Buf smdh;
    Buf eboot;
    Buf sfo;
    Vpk vpk;
    Buf dol;
    Buf icon;
    Ps3Pkg ps3;
    Buf elf;
    Buf icon_sys;
    Buf icon_icn;
    Buf rpx;
    Wuhb wuhb;
    uint64_t pack_len;
};

int package_has_engine(TargetId id) {
    if (id == TARGET_VITA && !embedded_sfo().size) return 0;
    return embedded(id).size > 0;
}

static int read_engine(TargetId id, const char* path, Buf* out) {
    if (path) {
        if (file_read(path, out)) return 1;
        fprintf(stderr, "error: cannot read %s\n", path);
        return 0;
    }
    Blob b = embedded(id);
    if (!b.size) {
        fprintf(stderr, "error: this builder was compiled without the %s engine; pass --engine\n", target_get(id)->console);
        return 0;
    }
    buf_put(out, b.data, b.size);
    return 1;
}

static int load_psp(const char* path, Pbp* out) {
    Buf b = {0};
    if (!read_engine(TARGET_PSP, path, &b)) return 0;
    int ok = pbp_read(b.data, b.len, out);
    buf_free(&b);
    if (!ok) fprintf(stderr, "error: the PSP engine is not a valid EBOOT.PBP\n");
    return ok;
}

static int load_3ds(const char* path, Exe3dsx* out) {
    Buf b = {0};
    if (!read_engine(TARGET_3DS, path, &b)) return 0;
    int ok = x3dsx_read(b.data, b.len, out);
    buf_free(&b);
    if (!ok) fprintf(stderr, "error: the 3DS engine is not a valid .3dsx\n");
    return ok;
}

static int load_vita(const char* path, Buf* eboot, Buf* sfo) {
    if (!read_engine(TARGET_VITA, path, eboot)) return 0;
    if (path) {
        char* dir = path_parent(path);
        char* side = path_join(dir, "param.sfo");
        int own = file_read(side, sfo);
        free(side);
        free(dir);
        if (own) return 1;
    }
    Blob b = embedded_sfo();
    if (!b.size) {
        fprintf(stderr, "error: this builder was compiled without the Vita param.sfo; put param.sfo next to the --engine file\n");
        buf_free(eboot);
        return 0;
    }
    buf_put(sfo, b.data, b.size);
    return 1;
}

static void ps3_sfo(Buf* out) {
    const SfoEntry e[] = {
        {"APP_VER", 0, 0, "01.00", 8},
        {"ATTRIBUTE", 1, 0, NULL, 0},
        {"BOOTABLE", 1, 1, NULL, 0},
        {"CATEGORY", 0, 0, "HG", 4},
        {"LICENSE", 0, 0, PS3_LICENSE, 512},
        {"PARENTAL_LEVEL", 1, 0, NULL, 0},
        {"PS3_SYSTEM_VER", 0, 0, "03.5500", 8},
        {"RESOLUTION", 1, 63, NULL, 0},
        {"SOUND_FORMAT", 1, 1, NULL, 0},
        {"TITLE", 0, 0, "OMORI", 128},
        {"TITLE_ID", 0, 0, "OMRI00001", 16},
        {"VERSION", 0, 0, "01.00", 8},
    };
    sfo_write(e, (int)(sizeof(e) / sizeof(e[0])), out);
}

static int wiiu_rpx_check(const uint8_t* d, size_t len) {
    return len >= 52 && !memcmp(d, "\x7f" "ELF", 4) && d[4] == 1 && d[5] == 2 && d[7] == 0xCA && d[8] == 0xFE && d[18] == 0 && d[19] == 20;
}

static int xenon_elf_check(const uint8_t* d, size_t len) {
    return len >= 52 && !memcmp(d, "\x7f" "ELF", 4) && d[4] == 1 && d[5] == 2 && d[18] == 0 && d[19] == 20;
}

#define XENON_PART_BYTES 0x7FFF0000u

static int xenon_split(const char* path, uint64_t len) {
    int parts = (int)((len + XENON_PART_BYTES - 1) / XENON_PART_BYTES);
    int ok = 1;
    if (parts > 1) {
        FILE* f = file_open(path, "rb+");
        ok = f != NULL;
        for (int i = 1; ok && i < parts; i++) {
            uint64_t at = (uint64_t)i * XENON_PART_BYTES;
            uint64_t n = len - at < XENON_PART_BYTES ? len - at : XENON_PART_BYTES;
            char* name = str_fmt("%s.%d", path, i);
            FILE* to = file_open(name, "wb");
            ok = to && file_copy_range(f, at, n, to);
            if (to && fclose(to) != 0) ok = 0;
            free(name);
        }
        ok = ok && file_truncate(f, XENON_PART_BYTES);
        if (f && fclose(f) != 0) ok = 0;
    }
    for (int i = parts > 1 ? parts : 1;; i++) {
        char* name = str_fmt("%s.%d", path, i);
        int stale = file_exists(name);
        if (stale && !file_remove(name)) {
            fprintf(stderr, "error: cannot remove the old %s\n", name);
            ok = 0;
            stale = 0;
        }
        free(name);
        if (!stale) break;
    }
    return ok;
}

static char* data_path(TargetId id, const char* out) {
    switch (id) {
        case TARGET_PSP: return str_ends_with_ci(out, ".pbp") ? xstrdup(out) : str_fmt("%s/PSP/GAME/OMORI/EBOOT.PBP", out);
        case TARGET_3DS: return str_ends_with_ci(out, ".3dsx") ? xstrdup(out) : str_fmt("%s/3ds/omori/omori.3dsx", out);
        case TARGET_VITA: return str_fmt("%s/data/OMORI/omori.pak", out);
        case TARGET_WII: return str_fmt("%s/apps/omori/omori.pak", out);
        case TARGET_WIIU: return str_fmt("%s/wiiu/apps/omori/omori.pak", out);
        case TARGET_PS3: return str_ends_with_ci(out, ".pkg") ? xstrdup(out) : str_fmt("%s/OMORI.pkg", out);
        case TARGET_PS2: return str_ends_with_ci(out, ".iso") ? xstrdup(out) : str_fmt("%s/OMORI.iso", out);
        case TARGET_XENON: return str_fmt("%s/omori/omori.pak", out);
        default: return NULL;
    }
}

Package* package_open(const char* out, const char* engine_path, Game* g, Node* atlas) {
    Package* p = xcalloc(1, sizeof(Package));
    p->id = g_target->id;
    p->out = xstrdup(out);
    p->data_path = data_path(p->id, out);
    int ok = p->data_path != NULL;
    switch (p->id) {
        case TARGET_PSP:
            ok = ok && load_psp(engine_path, &p->pbp);
            if (ok) assets_build_icons(g, atlas, &p->icon0, &p->pic1);
            break;
        case TARGET_3DS:
            ok = ok && load_3ds(engine_path, &p->x3ds);
            if (ok) assets_build_smdh(g, atlas, &p->smdh);
            break;
        case TARGET_VITA:
            ok = ok && load_vita(engine_path, &p->eboot, &p->sfo);
            ok = ok && assets_build_vita_art(g, atlas, &p->vpk.icon0, &p->vpk.bg, &p->vpk.startup);
            break;
        case TARGET_WII:
            ok = ok && read_engine(TARGET_WII, engine_path, &p->dol);
            ok = ok && assets_build_hbc_icon(g, atlas, &p->icon);
            break;
        case TARGET_WIIU:
            ok = ok && read_engine(TARGET_WIIU, engine_path, &p->rpx);
            if (ok && !wiiu_rpx_check(p->rpx.data, p->rpx.len)) {
                fprintf(stderr, "error: the Wii U engine is not a valid .rpx\n");
                ok = 0;
            }
            ok = ok && assets_build_wiiu_art(g, atlas, &p->wuhb.icon, &p->wuhb.tv, &p->wuhb.drc);
            break;
        case TARGET_PS3:
            p->ps3.content_id = PS3_CONTENT_ID;
            ok = ok && read_engine(TARGET_PS3, engine_path, &p->ps3.eboot);
            ok = ok && assets_build_ps3_art(g, atlas, &p->ps3.icon0, &p->ps3.pic1);
            if (ok) ps3_sfo(&p->ps3.sfo);
            break;
        case TARGET_PS2:
            ok = ok && read_engine(TARGET_PS2, engine_path, &p->elf);
            if (ok && !ps2_elf_check(p->elf.data, p->elf.len)) {
                fprintf(stderr, "error: the PS2 engine is not a valid PS2 ELF\n");
                ok = 0;
            }
            ok = ok && assets_build_ps2_icon(g, atlas, &p->icon_sys, &p->icon_icn);
            break;
        case TARGET_XENON:
            ok = ok && read_engine(TARGET_XENON, engine_path, &p->elf);
            if (ok && !xenon_elf_check(p->elf.data, p->elf.len)) {
                fprintf(stderr, "error: the Xbox 360 engine is not a valid XeLL ELF\n");
                ok = 0;
            }
            break;
        default: ok = 0; break;
    }
    if (!ok) {
        package_free(p);
        return NULL;
    }
    return p;
}

const char* package_data_path(const Package* p) { return p->data_path; }

void package_add_entries(Package* p, Pack* pack) {
    if (p->id != TARGET_PS2) return;
    PackEntry* sys = pack_add(pack, "ps2/icon.sys");
    buf_put(&sys->data, p->icon_sys.data, p->icon_sys.len);
    PackEntry* icn = pack_add(pack, "ps2/icon.icn");
    buf_put(&icn->data, p->icon_icn.data, p->icon_icn.len);
}

int package_write_head(Package* p, FILE* f, uint64_t* pack_at) {
    *pack_at = 0;
    if (p->id == TARGET_PSP) {
        Pbp head;
        memset(&head, 0, sizeof(head));
        sfo_build("OMORI", &head.part[0]);
        head.part[1] = p->icon0;
        head.part[4] = p->pic1;
        head.part[6] = p->pbp.part[6];
        int ok = pbp_write_head(&head, f);
        for (int i = 0; i < 7; i++) *pack_at += head.part[i].len;
        *pack_at += 40;
        buf_free(&head.part[0]);
        return ok;
    }
    if (p->id == TARGET_3DS) return x3dsx_write(&p->x3ds, &p->smdh, f, pack_at);
    if (p->id == TARGET_PS3) return ps3pkg_write_head(&p->ps3, f, pack_at);
    if (p->id == TARGET_PS2) return ps2_iso_write_head(f, p->elf.data, p->elf.len, pack_at);
    return 1;
}

int package_write_tail(Package* p, FILE* f, uint64_t pack_at, uint64_t pack_len) {
    (void)pack_at;
    p->pack_len = pack_len;
    if (p->id == TARGET_PS3) return ps3pkg_write_tail(f, pack_len);
    if (p->id == TARGET_PS2) return ps2_iso_write_tail(f, (uint32_t)p->elf.len, pack_len);
    return 1;
}

int package_finish(Package* p) {
    if (p->id == TARGET_VITA) {
        p->vpk.eboot = p->eboot.data;
        p->vpk.eboot_len = p->eboot.len;
        p->vpk.sfo = p->sfo.data;
        p->vpk.sfo_len = p->sfo.len;
        char* vpk = str_fmt("%s/OMORI.vpk", p->out);
        int ok = vpk_write(vpk, &p->vpk);
        if (!ok) fprintf(stderr, "error: cannot write %s\n", vpk);
        p->summary = str_fmt("%s and %s", vpk, p->data_path);
        free(vpk);
        return ok;
    }
    if (p->id == TARGET_WII) {
        char* dir = str_fmt("%s/apps/omori", p->out);
        char* dol = path_join(dir, "boot.dol");
        char* meta = path_join(dir, "meta.xml");
        char* icon = path_join(dir, "icon.png");
        int ok = file_write(dol, p->dol.data, p->dol.len) && file_write(meta, WII_META, sizeof(WII_META) - 1) && file_write(icon, p->icon.data, p->icon.len);
        if (!ok) fprintf(stderr, "error: cannot write the files in %s\n", dir);
        p->summary = xstrdup(dir);
        free(dir);
        free(dol);
        free(meta);
        free(icon);
        return ok;
    }
    if (p->id == TARGET_WIIU) {
        char* dir = str_fmt("%s/wiiu/apps/omori", p->out);
        char* wuhb = path_join(dir, "omori.wuhb");
        p->wuhb.rpx = p->rpx.data;
        p->wuhb.rpx_len = p->rpx.len;
        int ok = wuhb_write(wuhb, &p->wuhb);
        if (!ok) fprintf(stderr, "error: cannot write %s\n", wuhb);
        p->summary = xstrdup(dir);
        free(dir);
        free(wuhb);
        return ok;
    }
    if (p->id == TARGET_XENON) {
        char* elf = path_join(p->out, "xenon.elf");
        int ok = xenon_split(p->data_path, p->pack_len);
        if (!ok) fprintf(stderr, "error: cannot split %s into parts\n", p->data_path);
        ok = ok && file_write(elf, p->elf.data, p->elf.len);
        if (!ok) fprintf(stderr, "error: cannot write %s\n", elf);
        p->summary = str_fmt("%s and %s", elf, p->data_path);
        free(elf);
        return ok;
    }
    if (p->id == TARGET_PS3) {
        int ok = ps3pkg_finish(&p->ps3, p->data_path, p->pack_len, cpu_count());
        if (!ok) fprintf(stderr, "error: cannot finish %s\n", p->data_path);
        p->summary = xstrdup(p->data_path);
        return ok;
    }
    p->summary = xstrdup(p->data_path);
    return 1;
}

const char* package_summary(const Package* p) { return p->summary ? p->summary : p->data_path; }

void package_free(Package* p) {
    if (!p) return;
    free(p->out);
    free(p->data_path);
    free(p->summary);
    pbp_free(&p->pbp);
    buf_free(&p->icon0);
    buf_free(&p->pic1);
    x3dsx_free(&p->x3ds);
    buf_free(&p->smdh);
    buf_free(&p->eboot);
    buf_free(&p->sfo);
    buf_free(&p->vpk.icon0);
    buf_free(&p->vpk.bg);
    buf_free(&p->vpk.startup);
    buf_free(&p->dol);
    buf_free(&p->icon);
    ps3pkg_free(&p->ps3);
    buf_free(&p->elf);
    buf_free(&p->icon_sys);
    buf_free(&p->icon_icn);
    buf_free(&p->rpx);
    buf_free(&p->wuhb.icon);
    buf_free(&p->wuhb.tv);
    buf_free(&p->wuhb.drc);
    free(p);
}

const char* package_write_text(void) {
    switch (g_target->id) {
        case TARGET_PSP: return "Writing EBOOT.PBP";
        case TARGET_3DS: return "Writing omori.3dsx";
        case TARGET_PS3: return "Writing OMORI.pkg";
        case TARGET_PS2: return "Writing OMORI.iso";
        default: return "Writing the game data";
    }
}

const char* package_finish_text(void) {
    switch (g_target->id) {
        case TARGET_VITA: return "Writing OMORI.vpk";
        case TARGET_WII: return "Writing boot.dol";
        case TARGET_WIIU: return "Writing omori.wuhb";
        case TARGET_PS3: return "Sealing OMORI.pkg";
        case TARGET_XENON: return "Writing xenon.elf";
        default: return "Finishing up";
    }
}

static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int replace_head(const char* target, FILE* f, uint64_t data_at, uint64_t size, int (*write_head)(void*, FILE*), void* ctx) {
    char* tmp = str_fmt("%s.tmp", target);
    FILE* o = file_open(tmp, "wb");
    int ok = o && write_head(ctx, o) && file_copy_range(f, data_at, size - data_at, o);
    fclose(f);
    if (o && fclose(o) != 0) ok = 0;
    if (ok) ok = file_replace(tmp, target);
    if (!ok) remove(tmp);
    free(tmp);
    return ok;
}

static int write_pbp(void* ctx, FILE* o) { return pbp_write_head((const Pbp*)ctx, o); }

static int update_psp(const char* target, FILE* f, const char* engine_path) {
    uint8_t h[40];
    uint64_t size = 0;
    if (!file_seek(f, 0) || fread(h, 1, 40, f) != 40 || !file_length(f, &size)) return 0;
    uint64_t off[9];
    for (int i = 0; i < 8; i++) off[i] = le32(h + 8 + i * 4);
    off[8] = size;
    Pbp cur;
    memset(&cur, 0, sizeof(cur));
    int ok = 1;
    for (int i = 0; i < 8 && ok; i++) {
        if (off[i] > off[i + 1]) ok = 0;
    }
    for (int i = 0; i < 6 && ok; i++) {
        size_t len = (size_t)(off[i + 1] - off[i]);
        buf_zero(&cur.part[i], len);
        if (len && (!file_seek(f, off[i]) || fread(cur.part[i].data, 1, len, f) != len)) ok = 0;
    }
    Pbp eng;
    memset(&eng, 0, sizeof(eng));
    ok = ok && load_psp(engine_path, &eng);
    if (ok) {
        buf_free(&cur.part[6]);
        cur.part[6] = eng.part[6];
        memset(&eng.part[6], 0, sizeof(Buf));
        ok = replace_head(target, f, off[7], size, write_pbp, &cur);
        f = NULL;
    }
    if (f) fclose(f);
    pbp_free(&cur);
    pbp_free(&eng);
    return ok;
}

typedef struct {
    Exe3dsx* eng;
    Buf* smdh;
} X3dsHead;

static int write_3dsx(void* ctx, FILE* o) {
    X3dsHead* h = ctx;
    return x3dsx_write(h->eng, h->smdh, o, NULL);
}

static int update_3ds(const char* target, FILE* f, const char* engine_path) {
    uint8_t h[0x2C];
    uint64_t size = 0;
    if (!file_seek(f, 0) || fread(h, 1, sizeof(h), f) != sizeof(h) || (h[4] | h[5] << 8) < 0x2C || !file_length(f, &size)) {
        fclose(f);
        return 0;
    }
    uint32_t smdh_at = le32(h + 0x20), smdh_len = le32(h + 0x24), pack_at = le32(h + 0x28);
    Buf smdh = {0};
    int ok = pack_at > 0 && pack_at <= size && smdh_at + (uint64_t)smdh_len <= size;
    if (ok && smdh_len) {
        buf_zero(&smdh, smdh_len);
        ok = file_seek(f, smdh_at) && fread(smdh.data, 1, smdh_len, f) == smdh_len;
    }
    Exe3dsx eng;
    memset(&eng, 0, sizeof(eng));
    ok = ok && load_3ds(engine_path, &eng);
    if (ok) {
        X3dsHead head = {&eng, &smdh};
        ok = replace_head(target, f, pack_at, size, write_3dsx, &head);
    } else {
        fclose(f);
    }
    buf_free(&smdh);
    x3dsx_free(&eng);
    return ok;
}

static int update_vita(const char* target, const char* engine_path) {
    Buf eboot = {0}, sfo = {0};
    int ok = load_vita(engine_path, &eboot, &sfo) && vpk_replace_eboot(target, eboot.data, eboot.len);
    buf_free(&eboot);
    buf_free(&sfo);
    return ok;
}

int package_update_engine(const char* file, const char* engine_path) {
    FILE* f = file_open(file, "rb");
    uint8_t magic[4];
    if (!f || fread(magic, 1, 4, f) != 4) {
        if (f) fclose(f);
        fprintf(stderr, "error: cannot read %s\n", file);
        return 2;
    }
    int ok;
    if (!memcmp(magic, "\0PBP", 4)) {
        target_set(target_get(TARGET_PSP));
        ok = update_psp(file, f, engine_path);
    } else if (!memcmp(magic, "3DSX", 4)) {
        target_set(target_get(TARGET_3DS));
        ok = update_3ds(file, f, engine_path);
    } else if (magic[0] == 'P' && magic[1] == 'K' && magic[2] == 3 && magic[3] == 4) {
        fclose(f);
        target_set(target_get(TARGET_VITA));
        ok = update_vita(file, engine_path);
    } else if (ps3pkg_is_pkg(magic)) {
        fclose(f);
        target_set(target_get(TARGET_PS3));
        Buf eboot = {0};
        ok = read_engine(TARGET_PS3, engine_path, &eboot) && ps3pkg_replace_eboot(file, eboot.data, eboot.len, cpu_count());
        buf_free(&eboot);
    } else if (str_ends_with_ci(file, ".iso") && ps2_iso_is_iso(f)) {
        fclose(f);
        target_set(target_get(TARGET_PS2));
        Buf elf = {0};
        ok = read_engine(TARGET_PS2, engine_path, &elf) && ps2_iso_replace_elf(file, elf.data, elf.len);
        buf_free(&elf);
    } else if (wuhb_is_wuhb(magic)) {
        fclose(f);
        target_set(target_get(TARGET_WIIU));
        Buf rpx = {0};
        ok = read_engine(TARGET_WIIU, engine_path, &rpx) && wiiu_rpx_check(rpx.data, rpx.len) && wuhb_replace_rpx(file, rpx.data, rpx.len);
        buf_free(&rpx);
    } else if (str_ends_with_ci(file, ".elf") && !memcmp(magic, "\x7f" "ELF", 4)) {
        fclose(f);
        target_set(target_get(TARGET_XENON));
        Buf elf = {0};
        ok = read_engine(TARGET_XENON, engine_path, &elf) && xenon_elf_check(elf.data, elf.len) && file_write(file, elf.data, elf.len);
        buf_free(&elf);
    } else if (str_ends_with_ci(file, ".dol")) {
        fclose(f);
        target_set(target_get(TARGET_WII));
        Buf dol = {0};
        ok = read_engine(TARGET_WII, engine_path, &dol) && file_write(file, dol.data, dol.len);
        buf_free(&dol);
    } else {
        fclose(f);
        fprintf(stderr, "error: %s is not an OMORI build this builder can update\n", file);
        return 2;
    }
    if (!ok) {
        fprintf(stderr, "error: cannot update the engine in %s\n", file);
        return 2;
    }
    log_msg("engine updated in %s", file);
    return 0;
}

const char* package_out_prompt(void) {
    switch (g_target->id) {
        case TARGET_VITA: return "Choose a folder to save it in (OMORI.vpk and a data folder will be created inside)";
        case TARGET_3DS: return "Choose where to save it: your SD card, or any folder (a 3ds folder will be created inside)";
        case TARGET_WII: return "Choose where to save it: your SD card or USB drive, or any folder (an apps folder will be created inside)";
        case TARGET_WIIU: return "Choose where to save it: your SD card, or any folder (a wiiu folder will be created inside)";
        case TARGET_PS3: return "Choose where to save it: a USB drive formatted as FAT32, or any folder (OMORI.pkg will be created inside)";
        case TARGET_PS2: return "Choose where to save it: the DVD folder of your Open PS2 Loader drive, or any folder (OMORI.iso will be created inside)";
        case TARGET_XENON: return "Choose where to save it: a USB drive formatted as FAT32, or any folder (xenon.elf and an omori folder will be created inside)";
        default: return "Choose where to save it: your memory stick, or any folder (a PSP folder will be created inside)";
    }
}

char* package_done_message(const char* out) {
    switch (g_target->id) {
        case TARGET_VITA:
            return str_fmt("Done!\n\nIn %s you will find OMORI.vpk and a data folder.\n\n1. Copy the data folder to ux0: on your Vita, so the game data ends up at "
                           "ux0:data/OMORI/omori.pak.\n2. Copy OMORI.vpk to the Vita and install it with VitaShell.",
                           out);
        case TARGET_WII:
            return str_fmt("Done!\n\nIf you picked your SD card or USB drive, OMORI is ready to play from the Homebrew Channel. Otherwise copy the apps "
                           "folder from\n%s\nto the root of your SD card or USB drive.",
                           out);
        case TARGET_WIIU:
            return str_fmt("Done!\n\nIf you picked your SD card, OMORI is ready to play from the Wii U Menu with Aroma. Otherwise copy the wiiu folder "
                           "from\n%s\nto the root of your SD card.",
                           out);
        case TARGET_PS3:
            return str_fmt("Done!\n\nIn %s you will find OMORI.pkg.\n\n1. Copy OMORI.pkg to the root of a FAT32 USB drive, if it is not on one "
                           "already.\n2. Plug the drive into your PS3 and install it from Package Manager, Install Package Files, on the "
                           "XMB. You need a PS3 running CFW or PS3HEN.\n3. OMORI then shows up under Game. You can delete OMORI.pkg "
                           "afterwards.",
                           out);
        case TARGET_PS2:
            return str_fmt("Done!\n\nIn %s you will find OMORI.iso.\n\nPut it in the DVD folder of the USB drive, hard drive or network share you use with "
                           "Open PS2 Loader and start OMORI from its game list. Saves go to the memory card in slot 1. In PCSX2, open OMORI.iso with "
                           "Start File.",
                           out);
        case TARGET_XENON:
            return str_fmt("Done!\n\nIn %s you will find xenon.elf and an omori folder.\n\n1. Copy both to the root of a FAT32 USB drive, if they are not "
                           "on one already.\n2. Plug the drive into an Xbox 360 with RGH or JTAG and boot XeLL. It loads xenon.elf from the drive and "
                           "OMORI starts. Saves go to the omori folder on the drive.",
                           out);
        case TARGET_3DS:
            return str_fmt("Done!\n\nIf you picked your SD card, OMORI is ready to play from the Homebrew Launcher. Otherwise copy the 3ds folder from\n%s\nto "
                           "the root of your SD card.",
                           out);
        default:
            return str_fmt("Done!\n\nIf you picked your memory stick, it is ready to play. Otherwise copy the PSP folder from\n%s\nto the root of your memory "
                           "stick.",
                           out);
    }
}
