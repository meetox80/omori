#include "maps.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "image.h"
#include "script.h"

typedef struct {
    char* key;
    Node* json;
    Image* img;
    Image** scaled;
    int count;
    int columns;
    int tw, th;
    Mutex* lock;
} Tileset;

typedef struct {
    Tileset** items;
    int count;
    Mutex* lock;
} TilesetCache;

static TilesetCache g_cache;

static const char* basename_of(const char* path) {
    const char* s = path;
    for (const char* c = path; *c; c++) {
        if (*c == '/' || *c == '\\') s = c + 1;
    }
    return s;
}

static Tileset* tileset_new(Game* g, const char* key, Node* json) {
    Tileset* t = xcalloc(1, sizeof(Tileset));
    t->key = xstrdup(key);
    t->json = json;
    t->lock = mutex_new();
    t->count = (int)node_numv(node_get(json, "tilecount"), 0);
    t->columns = (int)node_numv(node_get(json, "columns"), 1);
    if (t->columns < 1) t->columns = 1;
    t->tw = (int)node_numv(node_get(json, "tilewidth"), 32);
    t->th = (int)node_numv(node_get(json, "tileheight"), 32);
    if (t->count > 0) t->scaled = xcalloc((size_t)t->count, sizeof(Image*));
    const char* image = node_strv(node_get(json, "image"));
    if (image && *image) {
        char* rel = str_fmt("img/tilesets/%s", basename_of(image));
        Buf b = {0};
        if (game_read(g, rel, &b)) t->img = img_load_png(b.data, b.len);
        buf_free(&b);
        free(rel);
    }
    return t;
}

static Tileset* tileset_external(Game* g, const char* source) {
    const char* name = basename_of(source);
    mutex_lock(g_cache.lock);
    for (int i = 0; i < g_cache.count; i++) {
        if (!strcmp(g_cache.items[i]->key, name)) {
            Tileset* t = g_cache.items[i];
            mutex_unlock(g_cache.lock);
            return t;
        }
    }
    char* rel = str_fmt("maps/%s", name);
    Node* json = game_json_opt(g, rel);
    free(rel);
    Tileset* t = json ? tileset_new(g, name, json) : NULL;
    if (t) {
        g_cache.items = xrealloc(g_cache.items, sizeof(Tileset*) * (size_t)(g_cache.count + 1));
        g_cache.items[g_cache.count++] = t;
    }
    mutex_unlock(g_cache.lock);
    return t;
}

static void tileset_free(Tileset* t) {
    if (!t) return;
    for (int i = 0; i < t->count; i++) img_free(t->scaled ? t->scaled[i] : NULL);
    free(t->scaled);
    img_free(t->img);
    node_free(t->json);
    free(t->key);
    mutex_free(t->lock);
    free(t);
}

void maps_init(void) {
    if (!g_cache.lock) g_cache.lock = mutex_new();
}

void maps_cleanup(void) {
    for (int i = 0; i < g_cache.count; i++) tileset_free(g_cache.items[i]);
    free(g_cache.items);
    g_cache.items = NULL;
    g_cache.count = 0;
}

static Image* tile_image(Tileset* t, int local) {
    if (local < 0 || local >= t->count) return NULL;
    mutex_lock(t->lock);
    Image* r = t->scaled[local];
    if (!r) {
        int x0 = (local % t->columns) * t->tw, y0 = (local / t->columns) * t->th;
        Image* cell = img_new(t->tw, t->th);
        if (t->img) {
            for (int y = 0; y < t->th; y++) {
                int sy = y0 + y;
                if (sy < 0 || sy >= t->img->h) continue;
                for (int x = 0; x < t->tw; x++) {
                    int sx = x0 + x;
                    if (sx < 0 || sx >= t->img->w) continue;
                    memcpy(cell->px + ((size_t)y * t->tw + x) * 4, t->img->px + ((size_t)sy * t->img->w + sx) * 4, 4);
                }
            }
        }
        img_premultiply(cell);
        int ow, oh;
        tex_scaled_size(t->tw, t->th, &ow, &oh);
        r = (ow == t->tw && oh == t->th) ? img_clone(cell) : img_resize(cell, ow, oh, FILTER_LANCZOS);
        img_free(cell);
        t->scaled[local] = r;
    }
    mutex_unlock(t->lock);
    return r;
}

typedef struct {
    Tileset* ts;
    int firstgid;
    int count;
    int owned;
} MapTs;

static Node* props_of(const Node* n) {
    Node* p = node_get(n, "properties");
    return p && p->type == N_OBJ ? p : NULL;
}

static const char* prop_str(const Node* n, const char* key) {
    Node* p = props_of(n);
    Node* v = p ? node_get(p, key) : NULL;
    if (!v) return NULL;
    if (v->type == N_STR) return v->str;
    return NULL;
}

static int prop_truthy(const Node* n, const char* key) {
    Node* p = props_of(n);
    Node* v = p ? node_get(p, key) : NULL;
    if (!v) return 0;
    switch (v->type) {
    case N_STR: return v->len > 0;
    case N_NUM: return v->num != 0 && v->num == v->num;
    case N_TRUE: return 1;
    case N_NULL:
    case N_FALSE: return 0;
    default: return 1;
    }
}

static int js_parse_int(const Node* v, int* ok) {
    *ok = 0;
    if (!v) return 0;
    if (v->type == N_NUM) {
        if (v->num != v->num) return 0;
        *ok = 1;
        return (int)v->num;
    }
    if (v->type == N_TRUE || v->type == N_FALSE) return 0;
    if (v->type != N_STR) return 0;
    const char* s = v->str;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    int neg = 0;
    if (*s == '-' || *s == '+') {
        neg = *s == '-';
        s++;
    }
    if (*s < '0' || *s > '9') return 0;
    long r = 0;
    while (*s >= '0' && *s <= '9') r = r * 10 + (*s++ - '0');
    *ok = 1;
    return (int)(neg ? -r : r);
}

static int prop_int(const Node* n, const char* key, int def) {
    Node* p = props_of(n);
    int ok;
    int v = js_parse_int(p ? node_get(p, key) : NULL, &ok);
    return ok ? v : def;
}

static int find_ts(const MapTs* ts, int nts, int gid) {
    for (int i = 0; i < nts; i++) {
        if (gid >= ts[i].firstgid && gid < ts[i].firstgid + ts[i].count) return i;
    }
    return -1;
}

static Node* tile_props(const MapTs* ts, int nts, int gid) {
    int i = find_ts(ts, nts, gid);
    if (!gid || i < 0 || !ts[i].ts) return NULL;
    Node* tp = node_get(ts[i].ts->json, "tileproperties");
    if (!tp || tp->type != N_OBJ) return NULL;
    char key[32];
    snprintf(key, sizeof(key), "%d", gid - ts[i].firstgid);
    Node* r = node_get(tp, key);
    return r && r->type == N_OBJ ? r : NULL;
}

static const char* tp_str(const Node* tp, const char* key) {
    Node* v = tp ? node_get(tp, key) : NULL;
    return v && v->type == N_STR ? v->str : NULL;
}

static Node* tile_anim(const Tileset* t, int local) {
    Node* tiles = t ? node_get(t->json, "tiles") : NULL;
    if (!tiles || tiles->type != N_OBJ) return NULL;
    char key[32];
    snprintf(key, sizeof(key), "%d", local);
    Node* tile = node_get(tiles, key);
    Node* anim = tile ? node_get(tile, "animation") : NULL;
    return anim && anim->type == N_ARR && anim->count ? anim : NULL;
}

typedef struct {
    int ts;
    int local;
    int w, h;
    int ow, oh;
    int page, x, y;
} Rect;

typedef struct {
    Rect* items;
    int count;
} RectList;

static int rect_find(RectList* rl, int ts, int local) {
    for (int i = 0; i < rl->count; i++) {
        if (rl->items[i].ts == ts && rl->items[i].local == local) return i;
    }
    return -1;
}

static int rect_add(RectList* rl, const MapTs* mts, int ts, int local) {
    int f = rect_find(rl, ts, local);
    if (f >= 0) return f;
    rl->items = xrealloc(rl->items, sizeof(Rect) * (size_t)(rl->count + 1));
    Rect* r = &rl->items[rl->count];
    memset(r, 0, sizeof(*r));
    r->ts = ts;
    r->local = local;
    r->ow = mts[ts].ts->tw;
    r->oh = mts[ts].ts->th;
    tex_scaled_size(r->ow, r->oh, &r->w, &r->h);
    return rl->count++;
}

static int cmp_rect_h(const void* a, const void* b) {
    const Rect* const* x = a;
    const Rect* const* y = b;
    if ((*x)->h != (*y)->h) return (*y)->h - (*x)->h;
    if ((*x)->w != (*y)->w) return (*y)->w - (*x)->w;
    if ((*x)->ts != (*y)->ts) return (*x)->ts - (*y)->ts;
    return (*x)->local - (*y)->local;
}

static int pack_rects(RectList* rl) {
    Rect** order = xmalloc(sizeof(Rect*) * (size_t)(rl->count ? rl->count : 1));
    for (int i = 0; i < rl->count; i++) order[i] = &rl->items[i];
    qsort(order, (size_t)rl->count, sizeof(Rect*), cmp_rect_h);
    int page = 0, x = 0, y = 0, row_h = 0;
    for (int i = 0; i < rl->count; i++) {
        Rect* r = order[i];
        int gutter = (r->w == 18 && r->h == 18) ? 0 : 1;
        int need_w = r->w + gutter * 2, need_h = r->h + gutter * 2;
        if (need_w > ATLAS_PAGE) need_w = ATLAS_PAGE;
        if (need_h > ATLAS_PAGE) need_h = ATLAS_PAGE;
        if (x + need_w > ATLAS_PAGE) {
            x = 0;
            y += row_h;
            row_h = 0;
        }
        if (y + need_h > ATLAS_PAGE) {
            page++;
            x = 0;
            y = 0;
            row_h = 0;
        }
        r->page = page;
        r->x = x + gutter;
        r->y = y + gutter;
        x += need_w;
        if (need_h > row_h) row_h = need_h;
    }
    free(order);
    return rl->count ? page + 1 : 0;
}

static void blit_tile(Image* atlas, const Image* tile, int dx, int dy, int gutter) {
    for (int y = -gutter; y < tile->h + gutter; y++) {
        int sy = y < 0 ? 0 : y >= tile->h ? tile->h - 1 : y;
        int ty = dy + y;
        if (ty < 0 || ty >= atlas->h) continue;
        for (int x = -gutter; x < tile->w + gutter; x++) {
            int sx = x < 0 ? 0 : x >= tile->w ? tile->w - 1 : x;
            int tx = dx + x;
            if (tx < 0 || tx >= atlas->w) continue;
            memcpy(atlas->px + ((size_t)ty * atlas->w + tx) * 4, tile->px + ((size_t)sy * tile->w + sx) * 4, 4);
        }
    }
}

static void put16(Buf* b, int v) { buf_u16(b, (uint16_t)(int16_t)v); }

static int is_render_layer(const Node* l) {
    const char* type = node_strv(node_get(l, "type"));
    if (!type || strcmp(type, "tilelayer")) return 0;
    if (prop_truthy(l, "collision") || prop_truthy(l, "toLevel") || prop_truthy(l, "regionId")) return 0;
    if (prop_truthy(l, "reflectionSurface") || prop_truthy(l, "reflectionCast")) return 0;
    return 1;
}

static int layer_visible(const Node* l) {
    Node* v = node_get(l, "visible");
    if (!v) return 0;
    if (v->type == N_TRUE) return 1;
    if (v->type == N_NUM) return v->num != 0;
    return 0;
}

typedef struct {
    Buf coll, arrows, region, change;
    int used;
} Level;

static Level* level_get(Level* levels, int* nlev, int lv, int size) {
    if (lv < 0 || lv > 15) return NULL;
    while (*nlev <= lv) {
        memset(&levels[*nlev], 0, sizeof(Level));
        (*nlev)++;
    }
    Level* L = &levels[lv];
    if (!L->used) {
        L->used = 1;
        buf_zero(&L->coll, (size_t)size);
        buf_zero(&L->arrows, (size_t)size);
        memset(L->arrows.data, 15, (size_t)size);
        buf_zero(&L->region, (size_t)size);
        buf_zero(&L->change, (size_t)size);
        memset(L->change.data, 0xFF, (size_t)size);
    }
    return L;
}

static int arrow_bit(const char* s) {
    int bit = 0;
    char* copy = xstrdup(s);
    char* save = copy;
    for (;;) {
        char* amp = strchr(save, '&');
        if (amp) *amp = 0;
        char* a = save;
        while (*a == ' ' || *a == '\t') a++;
        char* e = a + strlen(a);
        while (e > a && (e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        for (char* c = a; *c; c++) {
            if (*c >= 'A' && *c <= 'Z') *c = (char)(*c + 32);
        }
        if (!strcmp(a, "left")) bit ^= 1;
        if (!strcmp(a, "up")) bit ^= 2;
        if (!strcmp(a, "right")) bit ^= 4;
        if (!strcmp(a, "down")) bit ^= 8;
        if (!amp) break;
        save = amp + 1;
    }
    free(copy);
    return bit;
}

static int cell_gid(const Node* data, size_t i) {
    const Node* v = node_at(data, i);
    return v && v->type == N_NUM ? (int)v->num : 0;
}

static Node* build_tiled(Game* g, Node* tm, int map_id, Buf* tex, char** err) {
    int W = (int)node_numv(node_get(tm, "width"), 0), H = (int)node_numv(node_get(tm, "height"), 0);
    int size = W * H;
    Node* tsl = node_get(tm, "tilesets");
    int nts = tsl ? (int)tsl->count : 0;
    MapTs* mts = xcalloc((size_t)(nts ? nts : 1), sizeof(MapTs));
    for (int i = 0; i < nts; i++) {
        Node* t = tsl->items[i];
        mts[i].firstgid = (int)node_numv(node_get(t, "firstgid"), 1);
        const char* source = node_strv(node_get(t, "source"));
        if (source) {
            mts[i].ts = tileset_external(g, source);
            if (!mts[i].ts && !*err) *err = str_fmt("map %d: missing tileset %s", map_id, source);
        } else {
            char* key = str_fmt("embedded:%d:%d", map_id, i);
            mts[i].ts = tileset_new(g, key, node_clone(t));
            mts[i].owned = 1;
            free(key);
        }
        mts[i].count = mts[i].ts ? mts[i].ts->count : 0;
        if (mts[i].ts && !mts[i].ts->img && !*err) *err = str_fmt("map %d: missing tileset image for %s", map_id, mts[i].ts->key);
    }
    Node* layers = node_get(tm, "layers");
    int nl = layers ? (int)layers->count : 0;
    RectList rl = {0};
    int* table_rect = NULL;
    int* table_anim = NULL;
    int* table_gid = NULL;
    int ntable = 0;
    Node* anims_out = node_arr();
    int* anim_ts = NULL;
    int* anim_local = NULL;
    int nanims = 0;
    for (int ti = 0; ti < nts; ti++) {
        Tileset* t = mts[ti].ts;
        Node* tiles = t ? node_get(t->json, "tiles") : NULL;
        if (!tiles || tiles->type != N_OBJ) continue;
        for (size_t k = 0; k < tiles->count; k++) {
            Node* anim = node_get(tiles->items[k], "animation");
            if (!anim || anim->type != N_ARR || !anim->count) continue;
            anim_ts = xrealloc(anim_ts, sizeof(int) * (size_t)(nanims + 1));
            anim_local = xrealloc(anim_local, sizeof(int) * (size_t)(nanims + 1));
            anim_ts[nanims] = ti;
            anim_local[nanims] = atoi(tiles->keys[k]);
            Node* entry = node_arr();
            node_push(entry, node_num(ti));
            node_push(entry, node_num(atoi(tiles->keys[k])));
            Node* durs = node_arr();
            for (size_t f = 0; f < anim->count; f++) node_push(durs, node_num(node_numv(node_get(anim->items[f], "duration"), 0) / 1000.0 * 60.0));
            node_push(entry, durs);
            node_push(entry, node_arr());
            node_push(anims_out, entry);
            nanims++;
        }
    }
    int* gid_to_table = NULL;
    int max_gid = 0;
    for (int li = 0; li < nl; li++) {
        Node* l = layers->items[li];
        Node* data = node_get(l, "data");
        const char* type = node_strv(node_get(l, "type"));
        if (type && !strcmp(type, "tilelayer") && data) {
            for (size_t i = 0; i < data->count; i++) {
                int gid = cell_gid(data, i);
                if (gid > max_gid) max_gid = gid;
            }
        }
        Node* objs = node_get(l, "objects");
        for (size_t i = 0; objs && i < objs->count; i++) {
            int gid = (int)node_numv(node_get(objs->items[i], "gid"), 0);
            if (gid > max_gid) max_gid = gid;
        }
    }
    gid_to_table = xmalloc(sizeof(int) * (size_t)(max_gid + 1));
    for (int i = 0; i <= max_gid; i++) gid_to_table[i] = -1;
#define USE_GID(gidv)                                                                                                                  \
    do {                                                                                                                               \
        int _g = (gidv);                                                                                                               \
        if (_g > 0 && _g <= max_gid && gid_to_table[_g] < 0) {                                                                         \
            int _ti = find_ts(mts, nts, _g);                                                                                           \
            if (_ti >= 0 && mts[_ti].ts) {                                                                                             \
                int _local = _g - mts[_ti].firstgid;                                                                                   \
                table_rect = xrealloc(table_rect, sizeof(int) * (size_t)(ntable + 1));                                                 \
                table_anim = xrealloc(table_anim, sizeof(int) * (size_t)(ntable + 1));                                                 \
                table_gid = xrealloc(table_gid, sizeof(int) * (size_t)(ntable + 1));                                                   \
                table_rect[ntable] = rect_add(&rl, mts, _ti, _local);                                                                  \
                table_anim[ntable] = -1;                                                                                               \
                table_gid[ntable] = _g;                                                                                                \
                for (int _a = 0; _a < nanims; _a++) {                                                                                  \
                    if (anim_ts[_a] == _ti && anim_local[_a] == _local) table_anim[ntable] = _a;                                       \
                }                                                                                                                      \
                gid_to_table[_g] = ntable++;                                                                                           \
            }                                                                                                                          \
        }                                                                                                                              \
    } while (0)
    Node* layers_out = node_arr();
    Node* objects_out = node_arr();
    for (int li = 0; li < nl; li++) {
        Node* l = layers->items[li];
        if (is_render_layer(l) && layer_visible(l)) {
            Node* data = node_get(l, "data");
            for (size_t i = 0; data && i < data->count; i++) USE_GID(cell_gid(data, i));
        }
        const char* type = node_strv(node_get(l, "type"));
        if (type && !strcmp(type, "objectgroup")) {
            Node* objs = node_get(l, "objects");
            for (size_t i = 0; objs && i < objs->count; i++) {
                Node* o = objs->items[i];
                int gid = (int)node_numv(node_get(o, "gid"), 0);
                if (!gid) continue;
                Node* vis = node_get(o, "visible");
                if (!vis || vis->type == N_FALSE || (vis->type == N_NUM && vis->num == 0)) continue;
                USE_GID(gid);
                if (gid > max_gid || gid_to_table[gid] < 0) continue;
                Node* oe = node_arr();
                node_push(oe, node_num(li));
                node_push(oe, node_num(prop_int(l, "zIndex", 0)));
                node_push(oe, node_num(prop_int(l, "priority", 0)));
                node_push(oe, node_num(gid_to_table[gid]));
                node_push(oe, node_num(node_numv(node_get(o, "x"), 0)));
                node_push(oe, node_num(node_numv(node_get(o, "y"), 0) - node_numv(node_get(o, "height"), 0)));
                node_push(oe, node_num(node_numv(node_get(l, "offsetx"), 0)));
                node_push(oe, node_num(node_numv(node_get(l, "offsety"), 0)));
                node_push(objects_out, oe);
            }
        }
    }
    for (int t = 0; t < ntable; t++) {
        int a = table_anim[t];
        if (a < 0) continue;
        Node* entry = anims_out->items[a];
        Node* rects = entry->items[3];
        if (rects->count) continue;
        Tileset* ts = mts[anim_ts[a]].ts;
        Node* anim = tile_anim(ts, anim_local[a]);
        for (size_t f = 0; anim && f < anim->count; f++) {
            int local = (int)node_numv(node_get(anim->items[f], "tileid"), 0);
            node_push(rects, node_num(local >= 0 && local < ts->count ? rect_add(&rl, mts, anim_ts[a], local) : -1));
        }
    }
    int pages = pack_rects(&rl);
    Image* atlas = img_new(ATLAS_PAGE, ATLAS_PAGE * (pages ? pages : 1));
    for (int i = 0; i < rl.count; i++) {
        Rect* r = &rl.items[i];
        Image* tile = tile_image(mts[r->ts].ts, r->local);
        if (!tile) continue;
        int gutter = (r->w == 18 && r->h == 18) ? 0 : 1;
        blit_tile(atlas, tile, r->x, r->page * ATLAS_PAGE + r->y, gutter);
    }
    tex_encode(atlas, atlas->w, atlas->h, tex);
    img_free(atlas);
    Buf rects = {0};
    for (int i = 0; i < rl.count; i++) {
        Rect* r = &rl.items[i];
        put16(&rects, r->page);
        put16(&rects, r->x);
        put16(&rects, r->y);
        put16(&rects, r->w);
        put16(&rects, r->h);
        put16(&rects, r->ow);
        put16(&rects, r->oh);
    }
    Buf table = {0};
    for (int i = 0; i < ntable; i++) {
        put16(&table, table_rect[i]);
        put16(&table, table_anim[i]);
    }
    for (int li = 0; li < nl; li++) {
        Node* l = layers->items[li];
        if (!is_render_layer(l) || !layer_visible(l)) continue;
        Node* data = node_get(l, "data");
        Buf cells = {0};
        int any = 0;
        for (int i = 0; i < size; i++) {
            int gid = data ? cell_gid(data, (size_t)i) : 0;
            int v = (gid > 0 && gid <= max_gid && gid_to_table[gid] >= 0) ? gid_to_table[gid] + 1 : 0;
            if (v) any = 1;
            buf_u16(&cells, (uint16_t)v);
        }
        Node* props = props_of(l);
        int hide = -1;
        if (props && node_get(props, "hideOnLevel")) hide = prop_int(l, "hideOnLevel", -1000);
        if (any || hide != -1) {
            Node* le = node_arr();
            node_push(le, node_num(li));
            node_push(le, node_num(prop_int(l, "zIndex", 0)));
            node_push(le, node_num(prop_int(l, "priority", 0)));
            node_push(le, node_num(hide));
            node_push(le, node_num(node_numv(node_get(l, "offsetx"), 0)));
            node_push(le, node_num(node_numv(node_get(l, "offsety"), 0)));
            node_push(le, node_bytes(cells.data, cells.len));
            node_push(layers_out, le);
        }
        buf_free(&cells);
    }
    Level levels[16];
    int nlev = 0;
    level_get(levels, &nlev, 0, size);
    for (int li = 0; li < nl; li++) {
        Node* l = layers->items[li];
        const char* col = prop_str(l, "collision");
        if (!col) continue;
        if (strcmp(col, "full") && strcmp(col, "up-left") && strcmp(col, "up-right") && strcmp(col, "down-left") && strcmp(col, "down-right") &&
            strcmp(col, "tile-base"))
            continue;
        Level* L = level_get(levels, &nlev, prop_int(l, "level", 0), size);
        Node* data = node_get(l, "data");
        if (!L || !data) continue;
        for (int x = 0; x < size; x++) {
            int gid = cell_gid(data, (size_t)x);
            if (!gid) continue;
            int id = -1;
            if (!strcmp(col, "full") || !strcmp(col, "up-left")) id = x;
            else if (!strcmp(col, "up-right")) id = x + 1;
            else if (!strcmp(col, "down-left")) id = x + W;
            else if (!strcmp(col, "down-right")) id = x + W + 1;
            else {
                const char* c = tp_str(tile_props(mts, nts, gid), "collision");
                if (c && !strcmp(c, "full")) id = x;
            }
            if (id >= 0 && id < size) L->coll.data[id] = 1;
        }
    }
    for (int li = 0; li < nl; li++) {
        Node* l = layers->items[li];
        const char* col = prop_str(l, "collision");
        if (!col || (strcmp(col, "arrow") && strcmp(col, "tile-base"))) continue;
        Level* L = level_get(levels, &nlev, prop_int(l, "level", 0), size);
        Node* data = node_get(l, "data");
        if (!L || !data) continue;
        for (int x = 0; x < size; x++) {
            int gid = cell_gid(data, (size_t)x);
            if (!strcmp(col, "tile-base")) {
                const char* ai = tp_str(tile_props(mts, nts, gid), "arrowImpassable");
                if (ai && *ai) L->arrows.data[x] ^= (uint8_t)arrow_bit(ai);
            } else if (gid) {
                const char* ai = prop_str(l, "arrowImpassable");
                if (!ai || !*ai) continue;
                L->arrows.data[x] ^= (uint8_t)arrow_bit(ai);
            }
        }
    }
    for (int li = 0; li < nl; li++) {
        Node* l = layers->items[li];
        if (!prop_truthy(l, "regionId")) continue;
        Level* L = level_get(levels, &nlev, prop_int(l, "level", 0), size);
        Node* data = node_get(l, "data");
        if (!L || !data) continue;
        const char* rs = prop_str(l, "regionId");
        int tile_base = rs && !strcmp(rs, "tile-base");
        int rid = prop_int(l, "regionId", 0);
        for (int x = 0; x < size; x++) {
            int gid = cell_gid(data, (size_t)x);
            if (tile_base) {
                Node* tp = tile_props(mts, nts, gid);
                Node* v = tp ? node_get(tp, "regionId") : NULL;
                int ok;
                int r = js_parse_int(v, &ok);
                if (v && (v->type != N_STR || v->len)) L->region.data[x] = (uint8_t)(ok ? r : 0);
            } else if (gid) {
                L->region.data[x] = (uint8_t)rid;
            }
        }
    }
    for (int li = 0; li < nl; li++) {
        Node* l = layers->items[li];
        if (!prop_truthy(l, "toLevel")) continue;
        Level* L = level_get(levels, &nlev, prop_int(l, "level", 0), size);
        Node* data = node_get(l, "data");
        if (!L || !data) continue;
        int to = prop_int(l, "toLevel", 0);
        for (int x = 0; x < size; x++) {
            if (cell_gid(data, (size_t)x)) L->change.data[x] = (uint8_t)(int8_t)to;
        }
    }
    Node* levels_out = node_arr();
    for (int i = 0; i < nlev; i++) {
        Node* le = node_arr();
        if (levels[i].used) {
            node_push(le, node_bytes(levels[i].coll.data, levels[i].coll.len));
            node_push(le, node_bytes(levels[i].arrows.data, levels[i].arrows.len));
            node_push(le, node_bytes(levels[i].region.data, levels[i].region.len));
            node_push(le, node_bytes(levels[i].change.data, levels[i].change.len));
        }
        node_push(levels_out, le);
        buf_free(&levels[i].coll);
        buf_free(&levels[i].arrows);
        buf_free(&levels[i].region);
        buf_free(&levels[i].change);
    }
    Node* refl = node_arr();
    Node* tevents = node_arr();
    for (int li = 0; li < nl; li++) {
        Node* l = layers->items[li];
        const char* type = node_strv(node_get(l, "type"));
        if (!type || strcmp(type, "objectgroup")) continue;
        Node* props = props_of(l);
        Node* objs = node_get(l, "objects");
        if (props && node_get(props, "reflectionCast")) {
            for (size_t i = 0; objs && i < objs->count; i++) {
                Node* o = objs->items[i];
                Node* r = node_arr();
                node_push(r, node_num(node_numv(node_get(o, "x"), 0)));
                node_push(r, node_num(node_numv(node_get(o, "y"), 0)));
                node_push(r, node_num(node_numv(node_get(o, "width"), 0)));
                node_push(r, node_num(node_numv(node_get(o, "height"), 0)));
                node_push(r, node_num(node_numv(node_get(props, "reflectionCast"), 0)));
                Node* op = node_get(props, "reflectionOpacity");
                node_push(r, node_num(op && node_numv(op, 0) ? node_numv(op, 255) : 255));
                node_push(r, node_num(node_numv(node_get(props, "reflectionOffset"), 0)));
                node_push(refl, r);
            }
        }
        for (size_t i = 0; objs && i < objs->count; i++) {
            Node* o = objs->items[i];
            int ok;
            Node* op = props_of(o);
            int ev = js_parse_int(op ? node_get(op, "eventId") : NULL, &ok);
            if (!ok || !ev) continue;
            Node* te = node_arr();
            node_push(te, node_num(ev));
            node_push(te, node_num(floor(node_numv(node_get(o, "x"), 0) / 32.0)));
            node_push(te, node_num(floor(node_numv(node_get(o, "y"), 0) / 32.0)));
            node_push(tevents, te);
        }
    }
    Node* out = node_obj();
    node_set(out, "w", node_num(W));
    node_set(out, "h", node_num(H));
    node_set(out, "tw", node_num(node_numv(node_get(tm, "tilewidth"), 32)));
    node_set(out, "th", node_num(node_numv(node_get(tm, "tileheight"), 32)));
    Node* mprops = props_of(tm);
    node_set(out, "properties", mprops ? node_clone(mprops) : node_obj());
    node_set(out, "rects", node_bytes(rects.data, rects.len));
    node_set(out, "tiles", node_bytes(table.data, table.len));
    node_set(out, "anims", anims_out);
    node_set(out, "layers", layers_out);
    node_set(out, "objects", objects_out);
    node_set(out, "levels", levels_out);
    node_set(out, "reflections", refl);
    node_set(out, "tevents", tevents);
    buf_free(&rects);
    buf_free(&table);
    free(rl.items);
    free(table_rect);
    free(table_anim);
    free(table_gid);
    free(anim_ts);
    free(anim_local);
    free(gid_to_table);
    for (int i = 0; i < nts; i++) {
        if (mts[i].owned) tileset_free(mts[i].ts);
    }
    free(mts);
#undef USE_GID
    return out;
}

static void compile_events(Node* m, int id) {
    Node* events = node_get(m, "events");
    for (size_t e = 0; events && e < events->count; e++) {
        Node* ev = events->items[e];
        if (ev->type != N_OBJ) continue;
        int evid = (int)node_numv(node_get(ev, "id"), 0);
        Node* pages = node_get(ev, "pages");
        for (size_t pi = 0; pages && pi < pages->count; pi++) {
            Node* page = pages->items[pi];
            char* where = str_fmt("map%d/ev%d/p%d", id, evid, (int)pi);
            script_compile_list(node_get(page, "list"), where);
            Node* route = node_get(page, "moveRoute");
            script_compile_route_list(route ? node_get(route, "list") : NULL, where, -1);
            free(where);
        }
    }
}

int maps_build_one(Game* g, int id, Buf* bin, Buf* tex, char** err) {
    *err = NULL;
    char* path = str_fmt("data/Map%03d.json", id);
    Node* m = game_json_opt(g, path);
    free(path);
    if (!m) return 0;
    node_remove(m, "data");
    compile_events(m, id);
    char* tpath = str_fmt("maps/map%d.json", id);
    Node* tm = game_json_opt(g, tpath);
    free(tpath);
    Node* root = node_obj();
    node_set(root, "id", node_num(id));
    if (tm) {
        Node* tiled = build_tiled(g, tm, id, tex, err);
        node_set(root, "tiled", tiled);
        node_free(tm);
        Node* w = node_get(tiled, "w");
        Node* h = node_get(tiled, "h");
        node_set(m, "width", node_num(node_numv(w, 0)));
        node_set(m, "height", node_num(node_numv(h, 0)));
    } else {
        node_set(root, "tiled", node_null());
    }
    node_set(root, "data", m);
    vbin_dump(root, bin);
    node_free(root);
    return 1;
}
