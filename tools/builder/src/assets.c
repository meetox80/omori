#include "assets.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adpcm.h"
#include "font.h"
#include "image.h"
#include "ps2disc.h"

static char* lower_dup(const char* s) {
    char* r = xstrdup(s);
    for (char* c = r; *c; c++) *c = (char)tolower((unsigned char)*c);
    return r;
}

static int ends_with_ci(const char* s, const char* suf) {
    size_t a = strlen(s), b = strlen(suf);
    if (a < b) return 0;
    for (size_t i = 0; i < b; i++) {
        if (tolower((unsigned char)s[a - b + i]) != tolower((unsigned char)suf[i])) return 0;
    }
    return 1;
}

char* asset_key(const char* logical, const char* ext) {
    const char* dot = strrchr(logical, '.');
    const char* slash = strrchr(logical, '/');
    size_t n = (dot && (!slash || dot > slash)) ? (size_t)(dot - logical) : strlen(logical);
    char* stem = xstrndup(logical, n);
    char* low = lower_dup(stem);
    free(stem);
    char* r = str_fmt("%s.%s", low, ext);
    free(low);
    return r;
}

typedef struct {
    Game* game;
    char* stored;
    char* logical;
    Node* atlas_entry;
    PackEntry* entry;
    Pack* pack;
    int sheet;
    int order;
    char* key;
} ImageJob;

typedef struct {
    char* name;
    Image* img;
    Mutex* lock;
    int loaded;
    int users;
} AtlasSheet;

static AtlasSheet* g_sheets;
static int g_nsheets;

static Image* sheet_image(Game* g, const char* name) {
    for (int i = 0; i < g_nsheets; i++) {
        if (strcmp(g_sheets[i].name, name)) continue;
        AtlasSheet* s = &g_sheets[i];
        mutex_lock(s->lock);
        if (!s->loaded) {
            char* rel = str_fmt("img/atlases/%s.png", name);
            Buf b = {0};
            if (game_read(g, rel, &b)) s->img = img_load_png(b.data, b.len);
            buf_free(&b);
            free(rel);
            s->loaded = 1;
        }
        Image* r = s->img;
        mutex_unlock(s->lock);
        return r;
    }
    return NULL;
}

static int base_has_dollar(const char* logical) {
    const char* slash = strrchr(logical, '/');
    const char* base = slash ? slash + 1 : logical;
    return base[0] == '$' || (base[0] && base[1] == '$');
}

static int emf_frames(const char* logical) {
    const char* p = strchr(logical, '%');
    while (p) {
        if (p[1] == '(' || p[1] == '[') {
            const char* q = p + 2;
            while (*q == ' ') q++;
            if (isdigit((unsigned char)*q)) return atoi(q);
        }
        p = strchr(p + 1, '%');
    }
    return 3;
}

static void cell_grid(const char* logical, int w, int h, int* cx, int* cy) {
    *cx = *cy = 0;
    if (!strncmp(logical, "img/characters/", 15)) {
        int frames = emf_frames(logical);
        int cols = base_has_dollar(logical) ? frames : frames * 4;
        int rows = base_has_dollar(logical) ? 4 : 8;
        if (cols > 0 && w % cols == 0 && h % rows == 0) {
            *cx = cols;
            *cy = rows;
        }
    } else if (!strncmp(logical, "img/faces/", 10)) {
        if (w % 106 == 0 && h % 106 == 0) {
            *cx = w / 106;
            *cy = h / 106;
        }
    } else if (!strncmp(logical, "img/system/", 11)) {
        const char* base = logical + 11;
        if (!strncmp(base, "IconSet", 7) && w % 32 == 0 && h % 32 == 0) {
            *cx = w / 32;
            *cy = h / 32;
        } else if (!strncmp(base, "Balloon", 7) && w % 48 == 0 && h % 48 == 0) {
            *cx = w / 48;
            *cy = h / 48;
        } else if ((!strncmp(base, "Window", 6) || !strncmp(base, "window", 6)) && w == 192 && h == 192) {
            *cx = 8;
            *cy = 8;
        }
    }
}

static void image_job_body(ImageJob* job) {
    Image* img = NULL;
    if (job->atlas_entry) {
        Node* e = job->atlas_entry;
        const char* name = node_strv(node_get(e, "atlasName"));
        Image* sheet = name ? sheet_image(job->game, name) : NULL;
        if (!sheet) {
            job->entry->error = str_fmt("%s: atlas %s missing", job->logical, name ? name : "?");
            return;
        }
        Node* r = node_get(e, "rect");
        Node* sr = node_get(e, "sourceRect");
        int rw = (int)node_numv(node_get(r, "width"), 0), rh = (int)node_numv(node_get(r, "height"), 0);
        int rx = (int)node_numv(node_get(r, "x"), 0), ry = (int)node_numv(node_get(r, "y"), 0);
        int sx = (int)node_numv(node_get(sr, "x"), 0), sy = (int)node_numv(node_get(sr, "y"), 0);
        int sw = (int)node_numv(node_get(sr, "width"), 0), sh = (int)node_numv(node_get(sr, "height"), 0);
        if (rw <= 0 || rh <= 0) {
            job->entry->error = str_fmt("%s: empty atlas rect", job->logical);
            return;
        }
        img = img_new(rw, rh);
        for (int y = 0; y < sh; y++) {
            int ty = ry + y, fy = sy + y;
            if (ty < 0 || ty >= rh || fy < 0 || fy >= sheet->h) continue;
            for (int x = 0; x < sw; x++) {
                int tx = rx + x, fx = sx + x;
                if (tx < 0 || tx >= rw || fx < 0 || fx >= sheet->w) continue;
                memcpy(img->px + ((size_t)ty * rw + tx) * 4, sheet->px + ((size_t)fy * sheet->w + fx) * 4, 4);
            }
        }
    } else {
        Buf b = {0};
        if (!game_read_stored(job->game, job->stored, &b)) {
            job->entry->error = str_fmt("%s: unreadable", job->stored);
            return;
        }
        img = img_load_png(b.data, b.len);
        buf_free(&b);
        if (!img) {
            job->entry->error = str_fmt("%s: not a png", job->stored);
            return;
        }
    }
    int cx, cy;
    cell_grid(job->logical, img->w, img->h, &cx, &cy);
    if (!tex_from_image(img, cx, cy, &job->entry->data)) job->entry->error = str_fmt("%s: could not convert", job->logical);
    img_free(img);
}

static void image_job(void* ctx, int index) {
    ImageJob* job = &((ImageJob*)ctx)[index];
    image_job_body(job);
    pack_ready(job->pack, job->entry);
    if (job->sheet >= 0) {
        AtlasSheet* s = &g_sheets[job->sheet];
        mutex_lock(s->lock);
        if (--s->users == 0 && s->img) {
            img_free(s->img);
            s->img = NULL;
        }
        mutex_unlock(s->lock);
    }
}

static int cmp_image_job(const void* a, const void* b) {
    const ImageJob* x = a;
    const ImageJob* y = b;
    int sx = x->sheet < 0 ? 1 << 30 : x->sheet, sy = y->sheet < 0 ? 1 << 30 : y->sheet;
    if (sx != sy) return sx < sy ? -1 : 1;
    return x->order - y->order;
}

static int skip_image(const char* logical) {
    if (!ends_with_ci(logical, ".png")) return 1;
    if (!strncmp(logical, "img/tilesets/", 13)) return 1;
    if (!strncmp(logical, "img/atlases/", 12) && strchr(logical + 12, '/')) return 1;
    return 0;
}

int assets_build_images(Game* g, Node* atlas, Pack* pack, int jobs) {
    Node* source = atlas ? node_get(atlas, "source") : NULL;
    ImageJob* list = NULL;
    int n = 0;
    char** keys = NULL;
    for (size_t i = 0; source && i < source->count; i++) {
        const char* logical = source->keys[i];
        Node* e = source->items[i];
        const char* an = node_strv(node_get(e, "atlasName"));
        if (!an) continue;
        int known = 0;
        for (int k = 0; k < g_nsheets; k++) known |= !strcmp(g_sheets[k].name, an);
        if (!known) {
            g_sheets = xrealloc(g_sheets, sizeof(AtlasSheet) * (size_t)(g_nsheets + 1));
            g_sheets[g_nsheets].name = xstrdup(an);
            g_sheets[g_nsheets].img = NULL;
            g_sheets[g_nsheets].loaded = 0;
            g_sheets[g_nsheets].users = 0;
            g_sheets[g_nsheets].lock = mutex_new();
            g_nsheets++;
        }
        char* key = asset_key(logical, "tex");
        int dup = 0;
        for (int k = 0; k < n && !dup; k++) dup = !strcmp(keys[k], key);
        if (dup) {
            free(key);
            continue;
        }
        list = xrealloc(list, sizeof(ImageJob) * (size_t)(n + 1));
        keys = xrealloc(keys, sizeof(char*) * (size_t)(n + 1));
        memset(&list[n], 0, sizeof(ImageJob));
        list[n].game = g;
        list[n].logical = xstrdup(logical);
        list[n].atlas_entry = e;
        keys[n] = key;
        n++;
    }
    int nfiles;
    char** files = game_stored_files(g, "img", &nfiles);
    for (int i = 0; i < nfiles; i++) {
        char* logical = game_logical(files[i]);
        if (skip_image(logical)) {
            free(logical);
            continue;
        }
        char* key = asset_key(logical, "tex");
        int dup = 0;
        for (int k = 0; k < n && !dup; k++) dup = !strcmp(keys[k], key);
        if (dup) {
            free(key);
            free(logical);
            continue;
        }
        list = xrealloc(list, sizeof(ImageJob) * (size_t)(n + 1));
        keys = xrealloc(keys, sizeof(char*) * (size_t)(n + 1));
        memset(&list[n], 0, sizeof(ImageJob));
        list[n].game = g;
        list[n].stored = xstrdup(files[i]);
        list[n].logical = logical;
        keys[n] = key;
        n++;
    }
    str_list_free(files, nfiles);
    for (int i = 0; i < n; i++) {
        list[i].order = i;
        list[i].key = keys[i];
        list[i].sheet = -1;
        const char* an = list[i].atlas_entry ? node_strv(node_get(list[i].atlas_entry, "atlasName")) : NULL;
        for (int k = 0; an && k < g_nsheets; k++) {
            if (!strcmp(g_sheets[k].name, an)) {
                list[i].sheet = k;
                g_sheets[k].users++;
                break;
            }
        }
    }
    qsort(list, (size_t)n, sizeof(ImageJob), cmp_image_job);
    int first = pack->count;
    for (int i = 0; i < n; i++) pack_add(pack, list[i].key);
    for (int i = 0; i < n; i++) {
        list[i].entry = &pack->items[first + i];
        list[i].pack = pack;
    }
    run_jobs(image_job, list, n, jobs);
    for (int i = 0; i < n; i++) {
        free(list[i].stored);
        free(list[i].logical);
        free(list[i].key);
    }
    free(list);
    free(keys);
    for (int i = 0; i < g_nsheets; i++) {
        img_free(g_sheets[i].img);
        free(g_sheets[i].name);
        mutex_free(g_sheets[i].lock);
    }
    free(g_sheets);
    g_sheets = NULL;
    g_nsheets = 0;
    return n;
}

static int audio_known(const uint8_t* d, size_t n) {
    if (n >= 4 && !memcmp(d, "OggS", 4)) return 1;
    if (n >= 3 && !memcmp(d, "ID3", 3)) return 1;
    if (n >= 12 && !memcmp(d, "RIFF", 4) && !memcmp(d + 8, "WAVE", 4)) return 1;
    if (n >= 2 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0) return 1;
    return 0;
}

typedef struct {
    Game* game;
    char* stored;
    int index;
    PackEntry* entry;
    Pack* pack;
} AudioJob;

static void audio_job(void* ctx, int index) {
    AudioJob* job = &((AudioJob*)ctx)[index];
    PackEntry* e = job->entry;
    if (!game_read_stored(job->game, job->stored, &e->data)) {
        e->error = str_fmt("%s: unreadable", job->stored);
    } else if (!audio_known(e->data.data, e->data.len)) {
        e->error = str_fmt("%s: unknown audio format", job->stored);
    } else if (g_target->id == TARGET_PS2 && e->data.len >= 4 && !memcmp(e->data.data, "OggS", 4)) {
        Buf out = {0};
        char err[128];
        if (adpcm_from_ogg(e->data.data, e->data.len, &out, err, sizeof(err))) {
            buf_free(&e->data);
            e->data = out;
        } else {
            buf_free(&out);
            e->error = str_fmt("%s: %s", job->stored, err);
        }
    }
    pack_ready(job->pack, e);
}

int assets_build_audio(Game* g, Pack* pack, int jobs) {
    int nfiles;
    char** files = game_stored_files(g, "audio", &nfiles);
    AudioJob* list = xcalloc((size_t)(nfiles ? nfiles : 1), sizeof(AudioJob));
    int n = 0;
    for (int i = 0; i < nfiles; i++) {
        char* logical = game_logical(files[i]);
        if (ends_with_ci(logical, ".ogg")) {
            char* key = asset_key(logical, "ogg");
            list[n].game = g;
            list[n].stored = files[i];
            list[n].index = pack->count;
            list[n].pack = pack;
            pack_add(pack, key);
            n++;
            free(key);
        }
        free(logical);
    }
    for (int i = 0; i < n; i++) list[i].entry = &pack->items[list[i].index];
    run_jobs(audio_job, list, n, jobs);
    free(list);
    str_list_free(files, nfiles);
    return n;
}

typedef struct {
    uint8_t* seen;
} CharSet;

static void cs_add_utf8(CharSet* cs, const char* p0, size_t len) {
    const unsigned char* p = (const unsigned char*)p0;
    const unsigned char* end = p + len;
    while (p < end) {
        uint32_t cp;
        int n;
        if (*p < 0x80) {
            cp = *p;
            n = 1;
        } else if ((*p >> 5) == 6) {
            cp = *p & 0x1F;
            n = 2;
        } else if ((*p >> 4) == 14) {
            cp = *p & 0x0F;
            n = 3;
        } else if ((*p >> 3) == 30) {
            cp = *p & 0x07;
            n = 4;
        } else {
            p++;
            continue;
        }
        if (p + n > end) break;
        int ok = 1;
        for (int k = 1; k < n; k++) {
            if ((p[k] & 0xC0) != 0x80) {
                ok = 0;
                break;
            }
            cp = (cp << 6) | (p[k] & 0x3F);
        }
        if (!ok) {
            p++;
            continue;
        }
        p += n;
        if (cp < 0x110000) cs->seen[cp] = 1;
    }
}

static void cs_add_node(CharSet* cs, const Node* v) {
    if (!v) return;
    if (v->type == N_STR) cs_add_utf8(cs, v->str, v->len);
    else if (v->type == N_ARR || v->type == N_OBJ) {
        for (size_t i = 0; i < v->count; i++) cs_add_node(cs, v->items[i]);
        if (v->type == N_OBJ) {
            for (size_t i = 0; i < v->count; i++) cs_add_utf8(cs, v->keys[i], strlen(v->keys[i]));
        }
    }
}

static const int FONT_SIZES[] = {12, 14, 16, 18, 20, 22, 23, 24, 25, 26, 28, 30, 32, 34, 36, 38, 40, 48, 72};

typedef struct {
    const uint8_t* ttf;
    size_t ttf_len;
    uint32_t* cps;
    int ncps;
    int nsizes;
    PackEntry** tex;
    PackEntry** fnt;
    int* ok;
    Pack* pack;
} FontJobs;

static void font_job(void* ctx, int i) {
    FontJobs* fj = ctx;
    int size = FONT_SIZES[i % fj->nsizes];
    int outline = i >= fj->nsizes;
    fj->ok[i] = font_build(fj->ttf, fj->ttf_len, size, outline, fj->cps, fj->ncps, &fj->tex[i]->data, &fj->fnt[i]->data);
    pack_ready(fj->pack, fj->tex[i]);
    pack_ready(fj->pack, fj->fnt[i]);
}

static void build_family(Game* g, const char* file, const char* family, uint32_t* cps, int n, Pack* pack, int jobs) {
    Buf ttf = {0};
    char* rel = str_fmt("fonts/%s", file);
    if (!game_read(g, rel, &ttf)) fatal("missing %s", rel);
    free(rel);
    int nsizes = (int)(sizeof(FONT_SIZES) / sizeof(FONT_SIZES[0]));
    int total = nsizes * 2;
    FontJobs fj;
    fj.ttf = ttf.data;
    fj.ttf_len = ttf.len;
    fj.cps = cps;
    fj.ncps = n;
    fj.nsizes = nsizes;
    fj.tex = xmalloc(sizeof(PackEntry*) * (size_t)total);
    fj.fnt = xmalloc(sizeof(PackEntry*) * (size_t)total);
    fj.ok = xcalloc((size_t)total, sizeof(int));
    fj.pack = pack;
    int first = pack->count;
    for (int i = 0; i < total; i++) {
        const char* suffix = i >= nsizes ? "_o" : "";
        char* a = str_fmt("fonts/%s_%d%s.tex", family, FONT_SIZES[i % nsizes], suffix);
        char* b = str_fmt("fonts/%s_%d%s.fnt", family, FONT_SIZES[i % nsizes], suffix);
        pack_add(pack, a);
        pack_add(pack, b);
        free(a);
        free(b);
    }
    for (int i = 0; i < total; i++) {
        fj.tex[i] = &pack->items[first + i * 2];
        fj.fnt[i] = &pack->items[first + i * 2 + 1];
    }
    run_jobs(font_job, &fj, total, jobs);
    for (int i = 0; i < total; i++) {
        if (!fj.ok[i]) fj.tex[i]->error = str_fmt("font %s %d: could not build", family, FONT_SIZES[i % nsizes]);
    }
    free(fj.tex);
    free(fj.fnt);
    free(fj.ok);
    buf_free(&ttf);
}

void assets_build_fonts(Game* g, Node* db, Node* lang, Pack* pack, int jobs) {
    CharSet cs;
    cs.seen = xcalloc(0x110000, 1);
    for (int c = 0x20; c < 0x7F; c++) cs.seen[c] = 1;
    cs_add_node(&cs, lang);
    const char* keys[] = {"actors", "classes", "skills", "items", "weapons", "armors", "enemies", "states", "system", "mapInfos", "quests", NULL};
    for (int i = 0; keys[i]; i++) cs_add_node(&cs, node_get(db, keys[i]));
    cs_add_utf8(&cs, "\xE2\x80\xA6\xE2\x80\x94\xE2\x80\x99\xE2\x80\x98\xE2\x80\x9C\xE2\x80\x9D\xC3\xA9\xC3\x97\xE2\x86\x91\xE2\x86\x93\xE2\x86\x90\xE2\x86\x92\xE2\x87\xAA\xE2\x8C\xAB", 40);
    uint32_t* cps = NULL;
    int n = 0;
    for (uint32_t c = 0x20; c < 0x110000; c++) {
        if (!cs.seen[c] || c == 0x7F || (c >= 0x80 && c < 0xA0)) continue;
        cps = xrealloc(cps, sizeof(uint32_t) * (size_t)(n + 1));
        cps[n++] = c;
    }
    free(cs.seen);
    build_family(g, "OMORI_GAME2.ttf", "game", cps, n, pack, jobs);
    build_family(g, "OMORI_GAME.ttf", "omori", cps, n, pack, jobs);
    free(cps);
}

static Image* fit_rgb(const Image* src, int w, int h, uint32_t bg) {
    Image* canvas = img_new(w, h);
    for (int i = 0; i < w * h; i++) {
        canvas->px[i * 4 + 0] = (uint8_t)(bg & 255);
        canvas->px[i * 4 + 1] = (uint8_t)((bg >> 8) & 255);
        canvas->px[i * 4 + 2] = (uint8_t)((bg >> 16) & 255);
        canvas->px[i * 4 + 3] = 255;
    }
    double s = fmin((double)w / src->w, (double)h / src->h);
    int dw = (int)lround(src->w * s), dh = (int)lround(src->h * s);
    Image* pm = img_clone(src);
    img_premultiply(pm);
    Image* rs = img_resize(pm, dw > 0 ? dw : 1, dh > 0 ? dh : 1, FILTER_LANCZOS);
    img_free(pm);
    for (int y = 0; y < rs->h; y++) {
        for (int x = 0; x < rs->w; x++) {
            int tx = (w - dw) / 2 + x, ty = (h - dh) / 2 + y;
            if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
            uint8_t* d = canvas->px + ((size_t)ty * w + tx) * 4;
            const uint8_t* sp = rs->px + ((size_t)y * rs->w + x) * 4;
            int a = sp[3];
            for (int k = 0; k < 3; k++) d[k] = (uint8_t)(sp[k] + d[k] * (255 - a) / 255);
        }
    }
    img_free(rs);
    return canvas;
}

static Image* atlas_virtual(Game* g, Node* atlas, const char* key) {
    Node* e = atlas ? node_get(node_get(atlas, "source"), key) : NULL;
    if (!e) return NULL;
    const char* name = node_strv(node_get(e, "atlasName"));
    char* rel = str_fmt("img/atlases/%s.png", name ? name : "");
    Buf b = {0};
    Image* sheet = NULL;
    if (game_read(g, rel, &b)) sheet = img_load_png(b.data, b.len);
    buf_free(&b);
    free(rel);
    if (!sheet) return NULL;
    Node* sr = node_get(e, "sourceRect");
    int sx = (int)node_numv(node_get(sr, "x"), 0), sy = (int)node_numv(node_get(sr, "y"), 0);
    int sw = (int)node_numv(node_get(sr, "width"), 0), sh = (int)node_numv(node_get(sr, "height"), 0);
    Image* out = img_crop(sheet, sx, sy, sx + sw, sy + sh);
    img_free(sheet);
    return out;
}

static Image* atlas_placed(Game* g, Node* atlas, const char* key, int* ox, int* oy) {
    Node* e = atlas ? node_get(node_get(atlas, "source"), key) : NULL;
    Node* r = e ? node_get(e, "rect") : NULL;
    *ox = (int)node_numv(node_get(r, "x"), 0);
    *oy = (int)node_numv(node_get(r, "y"), 0);
    return atlas_virtual(g, atlas, key);
}

static int place(Image* canvas, Game* g, Node* atlas, const char* key, int x, int y, int crop_w, int stretch_h) {
    int ox, oy;
    Image* pic = atlas_placed(g, atlas, key, &ox, &oy);
    if (!pic) return 0;
    if (crop_w > 0 && crop_w < pic->w) {
        Image* c = img_crop(pic, 0, 0, crop_w, pic->h);
        img_free(pic);
        pic = c;
    }
    if (stretch_h > 0) {
        Image* rs = img_resize(pic, pic->w, stretch_h, FILTER_LANCZOS);
        img_free(pic);
        pic = rs;
    }
    img_alpha_composite(canvas, pic, x + ox, y + oy);
    img_free(pic);
    return 1;
}

static Image* white_canvas(int w, int h) {
    Image* c = img_new(w, h);
    memset(c->px, 255, (size_t)w * h * 4);
    return c;
}

typedef struct {
    Image* scene;
    Image* mark;
    Image* bust;
} TitleArt;

static void title_art_free(TitleArt* t) {
    img_free(t->scene);
    img_free(t->mark);
    img_free(t->bust);
    memset(t, 0, sizeof(*t));
}

static int title_art(Game* g, Node* atlas, TitleArt* t) {
    const int tx = 167, ty = -30;
    memset(t, 0, sizeof(*t));
    Image* logo = white_canvas(640, 480);
    t->scene = white_canvas(640, 480);
    int ok = 1;
    for (int pass = 0; pass < 2 && ok; pass++) {
        Image* c = pass ? logo : t->scene;
        if (!pass) ok = place(c, g, atlas, "img/pictures/OMO_WS.png", 167, 129, 306, 0);
        ok = ok && place(c, g, atlas, "img/pictures/OMO_STRING_WS.png", tx + 120, ty - 282, 0, 300);
        ok = ok && place(c, g, atlas, "img/pictures/OMO_BULB_WS.png", tx + 120, ty - 2, 0, 0);
        ok = ok && place(c, g, atlas, "img/pictures/OMO_BULB_WS_LINES.png", tx + 120, ty, 68, 0);
        ok = ok && place(c, g, atlas, "img/pictures/OMO_TITLE_WS.png", tx, ty, 0, 0);
    }
    if (ok) {
        int fox, foy;
        Image* face = atlas_placed(g, atlas, "img/pictures/OMO_WS.png", &fox, &foy);
        Image* frame = face ? img_crop(face, 0, 0, 306, face->h) : NULL;
        t->mark = img_crop(logo, tx - 10, 0, tx + 287, 128);
        t->bust = white_canvas(340, 340);
        if (frame) img_alpha_composite(t->bust, frame, (340 - frame->w) / 2, 22);
        ok = frame != NULL;
        img_free(face);
        img_free(frame);
    }
    img_free(logo);
    if (!ok) {
        log_msg("  error: the title screen art (OMO_WS, OMO_TITLE_WS, OMO_BULB_WS) is missing from the game files");
        title_art_free(t);
    }
    return ok;
}

void assets_build_icons(Game* g, Node* atlas, Buf* icon0, Buf* pic1) {
    TitleArt t;
    if (!title_art(g, atlas, &t)) return;
    Image* icon = fit_rgb(t.mark, 144, 80, 0xFFFFFF);
    Image* bg = fit_rgb(t.scene, 480, 272, 0xFFFFFF);
    png_encode(icon, 0, icon0);
    png_encode(bg, 0, pic1);
    img_free(icon);
    img_free(bg);
    title_art_free(&t);
}

int assets_build_vita_art(Game* g, Node* atlas, Buf* icon0, Buf* bg, Buf* startup) {
    TitleArt t;
    if (!title_art(g, atlas, &t)) return 0;
    Image* icon = fit_rgb(t.bust, 128, 128, 0xFFFFFF);
    Image* back = fit_rgb(t.scene, 840, 500, 0xFFFFFF);
    Image* gate = fit_rgb(t.mark, 280, 158, 0xFFFFFF);
    int ok = png_encode_indexed(icon, icon0) && png_encode_indexed(back, bg) && png_encode_indexed(gate, startup);
    img_free(icon);
    img_free(back);
    img_free(gate);
    title_art_free(&t);
    return ok;
}

static Image* upscale_nearest(const Image* src, int k) {
    Image* out = img_new(src->w * k, src->h * k);
    for (int y = 0; y < out->h; y++) {
        for (int x = 0; x < out->w; x++) memcpy(out->px + ((size_t)y * out->w + x) * 4, src->px + ((size_t)(y / k) * src->w + x / k) * 4, 4);
    }
    return out;
}

int assets_build_ps3_art(Game* g, Node* atlas, Buf* icon0, Buf* pic1) {
    TitleArt t;
    if (!title_art(g, atlas, &t)) return 0;
    Image* icon = fit_rgb(t.mark, 320, 176, 0xFFFFFF);
    Image* big = upscale_nearest(t.scene, 3);
    Image* back = fit_rgb(big, 1920, 1080, 0xFFFFFF);
    int ok = png_encode(icon, 0, icon0) && png_encode(back, 0, pic1);
    img_free(icon);
    img_free(big);
    img_free(back);
    title_art_free(&t);
    return ok;
}

int assets_build_ps2_icon(Game* g, Node* atlas, Buf* sys, Buf* icn) {
    TitleArt t;
    if (!title_art(g, atlas, &t)) return 0;
    ps2_icon_sys("OMORI", sys);
    ps2_icon_icn(t.bust, icn);
    title_art_free(&t);
    return 1;
}

static void put_utf16(uint8_t* dst, size_t cap, const char* text) {
    size_t n = 0;
    for (const char* c = text; *c && (n + 1) * 2 <= cap; c++, n++) {
        dst[n * 2] = (uint8_t)*c;
        dst[n * 2 + 1] = 0;
    }
}

static void put_icon(uint8_t* dst, const Image* img) {
    int w = img->w;
    for (int y = 0; y < img->h; y++) {
        for (int x = 0; x < w; x++) {
            const uint8_t* p = img->px + ((size_t)y * w + x) * 4;
            uint16_t v = (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
            int m = 0;
            for (int i = 0; i < 3; i++) m |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1);
            size_t at = (((size_t)(y >> 3) * (size_t)(w >> 3) + (size_t)(x >> 3)) * 64 + (size_t)m) * 2;
            dst[at] = (uint8_t)v;
            dst[at + 1] = (uint8_t)(v >> 8);
        }
    }
}

int assets_build_smdh(Game* g, Node* atlas, Buf* out) {
    Image* src = NULL;
    Buf png = {0};
    if (game_read(g, "icon/icon.png", &png)) src = img_load_png(png.data, png.len);
    buf_free(&png);
    if (!src) src = atlas_virtual(g, atlas, "img/pictures/OMO_TITLE_WS.png");
    if (!src) {
        log_msg("  warning: the game icon is missing, the HOME menu will show no icon");
        return 0;
    }
    Image* big = fit_rgb(src, 48, 48, 0xFFFFFF);
    Image* small = fit_rgb(src, 24, 24, 0xFFFFFF);
    img_free(src);
    uint8_t* d = xcalloc(0x36C0, 1);
    memcpy(d, "SMDH", 4);
    for (int i = 0; i < 16; i++) {
        uint8_t* t = d + 8 + (size_t)i * 0x200;
        put_utf16(t, 0x80, "OMORI");
        put_utf16(t + 0x80, 0x100, "OMORI (unofficial 3DS port)");
        put_utf16(t + 0x180, 0x80, "kotwicha");
    }
    static const uint8_t ratings[16] = {0xA0, 0xA0, 0x00, 0xA0, 0xA0, 0x00, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0x00, 0x00, 0x00, 0x00, 0x00};
    memcpy(d + 0x2008, ratings, 16);
    memset(d + 0x2018, 0xFF, 4);
    d[0x2028] = 0x41;
    d[0x2029] = 0x01;
    put_icon(d + 0x2040, small);
    put_icon(d + 0x24C0, big);
    img_free(big);
    img_free(small);
    buf_put(out, d, 0x36C0);
    free(d);
    return 1;
}

int assets_build_hbc_icon(Game* g, Node* atlas, Buf* png) {
    TitleArt t;
    if (!title_art(g, atlas, &t)) return 0;
    Image* icon = fit_rgb(t.mark, 128, 48, 0xFFFFFF);
    int ok = png_encode(icon, 0, png);
    img_free(icon);
    title_art_free(&t);
    return ok;
}
