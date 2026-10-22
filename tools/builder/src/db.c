#include "db.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dbproc.h"
#include "image.h"
#include "js.h"
#include "progress.h"
#include "rx.h"
#include "script.h"
#include "yaml.h"

static Node* g_notes;

static Node* load_yaml(Game* g, const char* rel) {
    Buf b = {0};
    if (!game_read(g, rel, &b)) return NULL;
    char err[200];
    Node* n = yaml_parse((const char*)b.data, b.len, err, sizeof(err));
    buf_free(&b);
    if (!n) fatal("bad yaml in %s: %s", rel, err);
    return n;
}

static char* trim_dup(const char* s, size_t n) {
    while (n && isspace((unsigned char)*s)) {
        s++;
        n--;
    }
    while (n && isspace((unsigned char)s[n - 1])) n--;
    return xstrndup(s, n);
}

static void js_string_of(const Node* v, Buf* out) {
    if (!v || v->type == N_NULL) {
        buf_put(out, "null", 4);
    } else if (v->type == N_STR) {
        buf_put(out, v->str, v->len);
    } else if (v->type == N_NUM) {
        char tmp[64];
        if (v->num == (double)(long long)v->num) snprintf(tmp, sizeof(tmp), "%lld", (long long)v->num);
        else snprintf(tmp, sizeof(tmp), "%.17g", v->num);
        buf_put(out, tmp, strlen(tmp));
    } else if (v->type == N_TRUE) {
        buf_put(out, "true", 4);
    } else if (v->type == N_FALSE) {
        buf_put(out, "false", 5);
    } else {
        buf_put(out, "[object Object]", 15);
    }
}

static void expand_load_notes(Node* obj) {
    Node* note = node_get(obj, "note");
    if (!note || note->type != N_STR || !note->len) return;
    Rx* rx = rx_compile("<LoadNotes:(.+)>", "i");
    Buf extra = {0};
    size_t from = 0;
    RxMatch m;
    int any = 0;
    while (rx_search(rx, note->str, note->len, from, &m)) {
        any = 1;
        char* args = rx_group(&m, note->str, 1);
        char* comma = strchr(args, ',');
        char* a = trim_dup(args, comma ? (size_t)(comma - args) : strlen(args));
        char* b = comma ? trim_dup(comma + 1, strlen(comma + 1)) : xstrdup("");
        if (comma && strchr(b, ',')) *strchr(b, ',') = 0;
        Node* cat = g_notes ? node_get(node_get(g_notes, "NOTES"), a) : NULL;
        Node* v = cat ? node_get(cat, b) : NULL;
        if (!v) log_msg("  warning: LoadNotes %s,%s not found", a, b);
        js_string_of(v, &extra);
        buf_u8(&extra, '\n');
        free(a);
        free(b);
        free(args);
        from = m.end[0] > m.start[0] ? m.end[0] : m.end[0] + 1;
    }
    rx_free(rx);
    if (any) {
        Buf nb = {0};
        buf_put(&nb, note->str, note->len);
        buf_put(&nb, extra.data, extra.len);
        node_set(obj, "note", node_strn((const char*)nb.data, nb.len));
        buf_free(&nb);
    }
    buf_free(&extra);
}

static void extract_meta(Node* obj) {
    Node* meta = node_obj();
    Node* note = node_get(obj, "note");
    if (note && note->type == N_STR) {
        Rx* rx = rx_compile("<([^<>:]+)(:?)([^>]*)>", "");
        RxMatch m;
        size_t from = 0;
        while (rx_search(rx, note->str, note->len, from, &m)) {
            char* key = rx_group(&m, note->str, 1);
            char* colon = rx_group(&m, note->str, 2);
            if (*colon) {
                char* val = rx_group(&m, note->str, 3);
                node_set(meta, key, node_str(val));
                free(val);
            } else {
                node_set(meta, key, node_bool(1));
            }
            free(key);
            free(colon);
            from = m.end[0];
        }
        rx_free(rx);
    }
    node_set(obj, "meta", meta);
}

static const char* NON_JS_BLOCKS[] = {"setup action", "whole action", "target action", "follow action", "finish action", "ai priority",
                                      "sideview battler motion", "enemy drops", "battlecommandslist", "passive condition cases",
                                      "custom passive condition cases", NULL};

static int is_js_block(const char* lower) {
    for (int i = 0; NON_JS_BLOCKS[i]; i++) {
        if (!strcmp(lower, NON_JS_BLOCKS[i])) return 0;
    }
    size_t n = strlen(lower);
    if (!strncmp(lower, "custom ", 7)) return 1;
    if (n > 5 && !strcmp(lower + n - 5, " eval")) return 1;
    if (!strcmp(lower, "damage formula")) return 1;
    return 0;
}

static void compile_note_blocks(Node* obj, const char* where) {
    Node* note = node_get(obj, "note");
    if (!note || note->type != N_STR || !note->len) return;
    Node* js = NULL;
    const char* s = note->str;
    size_t n = note->len;
    for (size_t i = 0; i < n; i++) {
        if (s[i] != '<' || (i + 1 < n && s[i + 1] == '/')) continue;
        size_t e = i + 1;
        while (e < n && s[e] != '>' && s[e] != '<' && s[e] != '\n' && s[e] != ':') e++;
        if (e >= n || s[e] != '>') continue;
        char* tag = xstrndup(s + i + 1, e - i - 1);
        char* lower = xstrdup(tag);
        for (char* c = lower; *c; c++) *c = (char)tolower((unsigned char)*c);
        char* trimmed = trim_dup(lower, strlen(lower));
        if (!is_js_block(trimmed)) {
            free(tag);
            free(lower);
            free(trimmed);
            continue;
        }
        size_t body_start = e + 1;
        size_t k = body_start;
        size_t close = (size_t)-1, close_end = 0;
        while (k + 2 < n) {
            if (s[k] == '<' && s[k + 1] == '/') {
                size_t ce = k + 2;
                while (ce < n && s[ce] != '>') ce++;
                if (ce < n) {
                    char* ct = xstrndup(s + k + 2, ce - k - 2);
                    for (char* c = ct; *c; c++) *c = (char)tolower((unsigned char)*c);
                    char* ctt = trim_dup(ct, strlen(ct));
                    int match = !strcmp(ctt, trimmed);
                    free(ct);
                    free(ctt);
                    if (match) {
                        close = k;
                        close_end = ce + 1;
                        break;
                    }
                }
            }
            k++;
        }
        if (close == (size_t)-1) {
            free(tag);
            free(lower);
            free(trimmed);
            continue;
        }
        char* body = xstrndup(s + body_start, close - body_start);
        char* w = str_fmt("%s <%s>", where, trimmed);
        Node* ast = script_compile(body, w, 0, 0);
        free(w);
        if (ast) {
            if (!js) js = node_obj();
            if (node_get(js, trimmed)) node_free(ast);
            else node_set(js, trimmed, ast);
        }
        free(body);
        free(tag);
        free(lower);
        free(trimmed);
        i = close_end - 1;
    }
    if (js) node_set(obj, "_js", js);
}

static void prepare_objects(Node* arr) {
    for (size_t i = 0; arr && i < arr->count; i++) {
        Node* o = arr->items[i];
        if (o->type != N_OBJ) continue;
        expand_load_notes(o);
        extract_meta(o);
    }
}

static double js_to_number(const char* s, size_t n) {
    while (n && isspace((unsigned char)*s)) s++, n--;
    while (n && isspace((unsigned char)s[n - 1])) n--;
    if (!n) return 0;
    char* t = xstrndup(s, n);
    char* end;
    double v = strtod(t, &end);
    int ok = *end == 0;
    free(t);
    return ok ? v : NAN;
}

static void battle_commands_lists(Node* arr) {
    Rx* start = rx_compile("<BattleCommandsList>", "i");
    for (size_t i = 0; arr && i < arr->count; i++) {
        Node* o = arr->items[i];
        Node* note = o->type == N_OBJ ? node_get(o, "note") : NULL;
        if (!note || note->type != N_STR || !note->len) continue;
        Node* list = node_arr();
        int add = 0;
        const char* s = note->str;
        size_t pos = 0;
        while (pos < note->len) {
            size_t e = pos;
            while (e < note->len && s[e] != '\r' && s[e] != '\n') e++;
            size_t len = e - pos;
            RxMatch m;
            char* line = xstrndup(s + pos, len);
            if (rx_search(start, line, len, 0, &m)) add = 1;
            else if (strstr(line, "</BattleCommandsList>")) add = 0;
            else if (add) {
                char* colon = strchr(line, ':');
                size_t tl = colon ? (size_t)(colon - line) : len;
                char* type = xstrndup(line, tl);
                for (char* c = type; *c; c++) *c = (char)tolower((unsigned char)*c);
                double id = NAN;
                if (colon) {
                    char* next = strchr(colon + 1, ':');
                    id = js_to_number(colon + 1, next ? (size_t)(next - colon - 1) : strlen(colon + 1));
                }
                Node* cmd = node_obj();
                node_set(cmd, "type", node_str(type));
                node_set(cmd, "id", node_num(id));
                node_push(list, cmd);
                free(type);
            }
            free(line);
            pos = e;
            while (pos < note->len && (s[pos] == '\r' || s[pos] == '\n')) pos++;
        }
        node_set(node_get(o, "meta"), "BattleCommandsList", list);
    }
    rx_free(start);
}

static int key_is_eval(const char* key) {
    size_t n = strlen(key);
    if (n >= 4 && !strcmp(key + n - 4, "Eval")) return 1;
    return !strcmp(key, "critRate") || !strcmp(key, "critMult") || !strcmp(key, "flatCrit") || !strcmp(key, "passiveCondition");
}

static void compile_evals(Node* n, const char* path, int eval_parent, Node* out, const char* where) {
    if (!n) return;
    if (n->type == N_STR) {
        if (!eval_parent || !n->len) return;
        Node* ast = script_compile(n->str, where, 0, 0);
        if (ast) node_set(out, path, ast);
        return;
    }
    if (n->type == N_ARR) {
        for (size_t i = 0; i < n->count; i++) {
            char* p = str_fmt("%s%s%d", path, *path ? "." : "", (int)i);
            compile_evals(n->items[i], p, eval_parent, out, where);
            free(p);
        }
        return;
    }
    if (n->type == N_OBJ) {
        for (size_t i = 0; i < n->count; i++) {
            if (n->keys[i][0] == '_') continue;
            char* p = str_fmt("%s%s%s", path, *path ? "." : "", n->keys[i]);
            int ev = eval_parent || key_is_eval(n->keys[i]);
            if (ev || n->items[i]->type == N_OBJ || n->items[i]->type == N_ARR) compile_evals(n->items[i], p, ev, out, where);
            free(p);
        }
    }
}

static void process_objects(Node* arr, const char* kind) {
    for (size_t i = 0; arr && i < arr->count; i++) {
        Node* o = arr->items[i];
        if (o->type != N_OBJ) continue;
        char* where = str_fmt("%s%d", kind, (int)node_numv(node_get(o, "id"), (double)i));
        compile_note_blocks(o, where);
        Node* dmg = node_get(o, "damage");
        const char* formula = dmg ? node_strv(node_get(dmg, "formula")) : NULL;
        if (formula && *formula) {
            Node* ast = script_compile(formula, where, 0, 0);
            if (ast) node_set(dmg, "_ast", ast);
        }
        Node* ev = node_obj();
        compile_evals(o, "", 0, ev, where);
        if (ev->count) node_set(o, "_ev", ev);
        else node_free(ev);
        free(where);
    }
}

static const char* NOTE_TAGS[][3] = {{"skills", "<ChainSkill:(.+),(.+)>", "i"},
                                     {"enemies", "<Position Offset [XY]:[ ]*-?\\d+>", "i"},
                                     {"items", "<Is Toy>", ""},
                                     {"actors", NULL, NULL},
                                     {"classes", NULL, NULL},
                                     {"weapons", NULL, NULL},
                                     {"armors", NULL, NULL},
                                     {"states", NULL, NULL},
                                     {NULL, NULL, NULL}};

static void trim_notes(Node* root) {
    for (int t = 0; NOTE_TAGS[t][0]; t++) {
        Node* arr = node_get(root, NOTE_TAGS[t][0]);
        Rx* rx = NOTE_TAGS[t][1] ? rx_compile(NOTE_TAGS[t][1], NOTE_TAGS[t][2]) : NULL;
        for (size_t i = 0; arr && i < arr->count; i++) {
            Node* note = node_get(arr->items[i], "note");
            if (!note || note->type != N_STR || !note->len) continue;
            Buf kept = {0};
            RxMatch m;
            size_t from = 0;
            while (rx && rx_search(rx, note->str, note->len, from, &m)) {
                if (kept.len) buf_u8(&kept, '\n');
                buf_put(&kept, note->str + m.start[0], m.end[0] - m.start[0]);
                from = m.end[0] > m.start[0] ? m.end[0] : m.end[0] + 1;
            }
            node_set(arr->items[i], "note", node_strn(kept.len ? (const char*)kept.data : "", kept.len));
            buf_free(&kept);
        }
        if (rx) rx_free(rx);
    }
}

static Node* text_colors(Game* g) {
    Node* arr = node_arr();
    Buf b = {0};
    if (!game_read(g, "img/system/Window.png", &b)) return arr;
    Image* img = img_load_png(b.data, b.len);
    buf_free(&b);
    if (!img) return arr;
    for (int n = 0; n < 32; n++) {
        int x = 96 + (n % 8) * 12 + 6, y = 144 + (n / 8) * 12 + 6;
        Node* c = node_arr();
        for (int k = 0; k < 3; k++) node_push(c, node_num(x < img->w && y < img->h ? img->px[(y * img->w + x) * 4 + k] : 0));
        node_push(arr, c);
    }
    img_free(img);
    return arr;
}

static const char* DB_FILES[][2] = {{"actors", "data/Actors.json"},       {"classes", "data/Classes.json"},   {"skills", "data/Skills.json"},
                                    {"items", "data/Items.json"},         {"weapons", "data/Weapons.json"},   {"armors", "data/Armors.json"},
                                    {"enemies", "data/Enemies.json"},     {"troops", "data/Troops.json"},     {"states", "data/States.json"},
                                    {"animations", "data/Animations.json"}, {"commonEvents", "data/CommonEvents.json"},
                                    {"mapInfos", "data/MapInfos.json"},   {"tilesets", "data/Tilesets.json"}, {NULL, NULL}};

Node* db_build(Game* g, Pack* pack, Node** lang_out) {
    g_notes = load_yaml(g, "data/Notes.yaml");
    Node* root = node_obj();
    node_set(root, "version", node_str(g->version));
    Node* system = game_json(g, "data/System.json");
    expand_load_notes(system);
    Node* sw = node_get(system, "switches");
    Node* va = node_get(system, "variables");
    node_set(system, "switchCount", node_num(sw && sw->type == N_ARR ? (double)sw->count : 0));
    node_set(system, "variableCount", node_num(va && va->type == N_ARR ? (double)va->count : 0));
    node_remove(system, "switches");
    node_remove(system, "variables");
    node_set(root, "system", system);
    for (int i = 0; DB_FILES[i][0]; i++) {
        Node* arr = game_json(g, DB_FILES[i][1]);
        const char* key = DB_FILES[i][0];
        if (strcmp(key, "mapInfos") && strcmp(key, "animations")) prepare_objects(arr);
        if (!strcmp(key, "actors")) battle_commands_lists(arr);
        node_set(root, key, arr);
    }
    Node* plugins = node_obj();
    Node* order = node_arr();
    Node* pl = game_plugins(g);
    Node* pl_raw = node_clone(pl);
    for (size_t i = 0; i < pl->count; i++) {
        Node* p = pl->items[i];
        const char* name = node_strv(node_get(p, "name"));
        Node* st = node_get(p, "status");
        if (!name || !st || st->type != N_TRUE) continue;
        if (node_get(plugins, name)) continue;
        Node* params = node_take(p, "parameters");
        node_set(plugins, name, params ? params : node_obj());
        node_push(order, node_str(name));
    }
    node_free(pl);
    node_set(root, "plugins", plugins);
    node_set(root, "pluginOrder", order);
    progress_fraction(0.05);
    char* dberr = NULL;
    if (!dbproc_run(g, root, pl_raw, &dberr)) fatal("%s", dberr ? dberr : "data step failed");
    node_free(pl_raw);
    progress_fraction(0.9);
    Node* yan = node_get(root, "yanfly");
    Node* yparam = yan ? node_get(yan, "Param") : NULL;
    if (yparam && yparam->type == N_OBJ) {
        Node* yev = node_obj();
        for (size_t i = 0; i < yparam->count; i++) {
            Node* v = yparam->items[i];
            if (v->type != N_STR || !v->len) continue;
            char e[256];
            Node* ast = js_compile(v->str, v->len, e, sizeof(e));
            if (ast) node_set(yev, yparam->keys[i], ast);
        }
        node_set(yan, "_ev", yev);
    }
    for (int i = 0; DB_FILES[i][0]; i++) {
        const char* key = DB_FILES[i][0];
        Node* arr = node_get(root, key);
        if (strcmp(key, "mapInfos") && strcmp(key, "animations")) process_objects(arr, key);
        if (!strcmp(key, "commonEvents")) {
            for (size_t k = 0; k < arr->count; k++) {
                Node* ce = arr->items[k];
                if (ce->type != N_OBJ) continue;
                char* where = str_fmt("ce%d", (int)node_numv(node_get(ce, "id"), 0));
                script_compile_list(node_get(ce, "list"), where);
                free(where);
            }
        } else if (!strcmp(key, "troops")) {
            for (size_t k = 0; k < arr->count; k++) {
                Node* tr = arr->items[k];
                if (tr->type != N_OBJ) continue;
                Node* pages = node_get(tr, "pages");
                for (size_t p = 0; pages && p < pages->count; p++) {
                    char* where = str_fmt("troop%d/p%d", (int)node_numv(node_get(tr, "id"), 0), (int)p);
                    script_compile_list(node_get(pages->items[p], "list"), where);
                    free(where);
                }
            }
        }
    }
    trim_notes(root);
    Node* quests = load_yaml(g, "data/Quests.yaml");
    node_set(root, "quests", quests ? quests : node_null());
    node_set(root, "textColors", text_colors(g));
    Node* lang_index = node_obj();
    Node* langs = node_obj();
    int nfiles;
    char** files = game_stored_files(g, "languages", &nfiles);
    for (int i = 0; i < nfiles; i++) {
        char* logical = game_logical(files[i]);
        size_t ll = strlen(logical);
        if (ll < 5 || strcmp(logical + ll - 5, ".yaml")) {
            free(logical);
            continue;
        }
        const char* rest = logical + strlen("languages/");
        const char* slash = strchr(rest, '/');
        if (!slash) {
            free(logical);
            continue;
        }
        char* lang = xstrndup(rest, (size_t)(slash - rest));
        char* file = xstrndup(slash + 1, strlen(slash + 1) - 5);
        Buf b = {0};
        if (!game_read_stored(g, files[i], &b)) fatal("unreadable %s", files[i]);
        char err[200];
        Node* data = yaml_parse((const char*)b.data, b.len, err, sizeof(err));
        buf_free(&b);
        if (!data) fatal("bad yaml in %s: %s", files[i], err);
        Node* li = node_get(lang_index, lang);
        if (!li) {
            li = node_arr();
            node_set(lang_index, lang, li);
            node_set(langs, lang, node_obj());
        }
        node_push(li, node_str(file));
        char* entry = str_fmt("lang/%s/%s.bin", lang, file);
        PackEntry* e = pack_add(pack, entry);
        vbin_dump(data, &e->data);
        node_set(node_get(langs, lang), file, data);
        free(entry);
        free(lang);
        free(file);
        free(logical);
    }
    str_list_free(files, nfiles);
    node_set(root, "languages", lang_index);
    node_free(g_notes);
    g_notes = NULL;
    *lang_out = langs;
    return root;
}
