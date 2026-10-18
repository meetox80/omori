#include "game.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static int is_root(const char* p) {
    char* a = str_fmt("%s/js/rpg_managers.js", p);
    char* b = str_fmt("%s/data", p);
    char* c = str_fmt("%s/maps", p);
    int ok = file_exists(a) && dir_exists(b) && dir_exists(c);
    free(a);
    free(b);
    free(c);
    return ok;
}

static char* find_root(const char* start) {
    if (!dir_exists(start)) return NULL;
    if (is_root(start)) return xstrdup(start);
    char* www = path_join(start, "www");
    if (is_root(www)) return www;
    free(www);
    char** queue = xmalloc(sizeof(char*));
    int* depth = xmalloc(sizeof(int));
    int head = 0, tail = 1, cap = 1;
    queue[0] = xstrdup(start);
    depth[0] = 0;
    char* found = NULL;
    while (head < tail && !found) {
        char* d = queue[head];
        int dep = depth[head];
        head++;
        if (dep > 5) continue;
        int n;
        char** names = dir_list(d, &n);
        for (int i = 0; i < n && !found; i++) {
            char* c = path_join(d, names[i]);
            if (!dir_exists(c)) {
                free(c);
                continue;
            }
            if (is_root(c)) {
                found = c;
                break;
            }
            if (tail == cap) {
                cap *= 2;
                queue = xrealloc(queue, sizeof(char*) * (size_t)cap);
                depth = xrealloc(depth, sizeof(int) * (size_t)cap);
            }
            queue[tail] = c;
            depth[tail] = dep + 1;
            tail++;
        }
        str_list_free(names, n);
    }
    for (int i = 0; i < tail; i++) free(queue[i]);
    free(queue);
    free(depth);
    return found;
}

static char* lower_dup(const char* s) {
    char* r = xstrdup(s);
    for (char* c = r; *c; c++) *c = (char)tolower((unsigned char)*c);
    return r;
}

static void collect(Game* g, const char* rel, int* cap) {
    char* abs = rel[0] ? path_join(g->root, rel) : xstrdup(g->root);
    int cnt;
    char** names = dir_list(abs, &cnt);
    for (int i = 0; i < cnt; i++) {
        char* r = rel[0] ? str_fmt("%s/%s", rel, names[i]) : xstrdup(names[i]);
        char* a = path_join(abs, names[i]);
        if (dir_exists(a)) collect(g, r, cap);
        else if (file_exists(a)) {
            if (g->nfiles == *cap) {
                *cap = *cap ? *cap * 2 : 1024;
                g->files = xrealloc(g->files, sizeof(char*) * (size_t)*cap);
            }
            g->files[g->nfiles++] = r;
            r = NULL;
        }
        free(r);
        free(a);
    }
    str_list_free(names, cnt);
    free(abs);
}

static int cmp_str(const void* a, const void* b) { return strcmp(*(const char* const*)a, *(const char* const*)b); }



static char** g_sort_lower;

static int cmp_lower_idx(const void* a, const void* b) {
    int ia = *(const int*)a, ib = *(const int*)b;
    int c = strcmp(g_sort_lower[ia], g_sort_lower[ib]);
    return c ? c : ia - ib;
}

static void build_index(Game* g) {
    int cap = 0;
    collect(g, "", &cap);
    if (g->nfiles) qsort(g->files, (size_t)g->nfiles, sizeof(char*), cmp_str);
    g->lower = xmalloc(sizeof(char*) * (size_t)(g->nfiles ? g->nfiles : 1));
    for (int i = 0; i < g->nfiles; i++) g->lower[i] = lower_dup(g->files[i]);
    g->by_lower = xmalloc(sizeof(int) * (size_t)(g->nfiles ? g->nfiles : 1));
    for (int i = 0; i < g->nfiles; i++) g->by_lower[i] = i;
    g_sort_lower = g->lower;
    if (g->nfiles) qsort(g->by_lower, (size_t)g->nfiles, sizeof(int), cmp_lower_idx);
}

static const char* find_ci(Game* g, const char* rel) {
    char** hit = g->nfiles ? bsearch(&rel, g->files, (size_t)g->nfiles, sizeof(char*), cmp_str) : NULL;
    if (hit) return *hit;
    char* lo = lower_dup(rel);
    int a = 0, b = g->nfiles - 1;
    const char* found = NULL;
    while (a <= b) {
        int m = (a + b) / 2;
        int c = strcmp(g->lower[g->by_lower[m]], lo);
        if (c == 0) {
            found = g->files[g->by_lower[m]];
            b = m - 1;
        } else if (c < 0) {
            a = m + 1;
        } else {
            b = m - 1;
        }
    }
    free(lo);
    return found;
}

typedef struct {
    const char* plain;
    const char* stored;
    int aes;
} ExtMap;

static const ExtMap EXTS[] = {{".json", ".KEL", 1},  {".json", ".AUBREY", 1}, {".js", ".OMORI", 1},  {".yaml", ".HERO", 1},
                              {".yaml", ".PLUTO", 1}, {".png", ".rpgmvp", 0},  {".ogg", ".rpgmvo", 0}, {".m4a", ".rpgmvm", 0},
                              {NULL, NULL, 0}};

static int ends_with_ci(const char* s, const char* suf) {
    size_t a = strlen(s), b = strlen(suf);
    if (a < b) return 0;
    for (size_t i = 0; i < b; i++) {
        if (tolower((unsigned char)s[a - b + i]) != tolower((unsigned char)suf[i])) return 0;
    }
    return 1;
}

char* game_resolve(Game* g, const char* path) {
    const char* hit = find_ci(g, path);
    if (hit) return xstrdup(hit);
    const char* slash = strrchr(path, '/');
    const char* dot = strrchr(path, '.');
    if (!dot || (slash && dot < slash)) return NULL;
    size_t stem = (size_t)(dot - path);
    for (int i = 0; EXTS[i].plain; i++) {
        if (!ends_with_ci(path, EXTS[i].plain) || strlen(EXTS[i].plain) != strlen(dot)) continue;
        char* cand = str_fmt("%.*s%s", (int)stem, path, EXTS[i].stored);
        hit = find_ci(g, cand);
        free(cand);
        if (hit) return xstrdup(hit);
    }
    return NULL;
}

char* game_logical(const char* stored) {
    for (int i = 0; EXTS[i].plain; i++) {
        if (ends_with_ci(stored, EXTS[i].stored)) {
            size_t n = strlen(stored) - strlen(EXTS[i].stored);
            return str_fmt("%.*s%s", (int)n, stored, EXTS[i].plain);
        }
    }
    return xstrdup(stored);
}

char* game_abs(Game* g, const char* rel) { return path_join(g->root, rel); }

static int read_raw(Game* g, const char* stored, Buf* out) {
    char* p = game_abs(g, stored);
    int ok = file_read(p, out);
    free(p);
    return ok;
}

int game_read_stored(Game* g, const char* stored, Buf* out) {
    if (!read_raw(g, stored, out)) return 0;
    for (int i = 0; EXTS[i].plain; i++) {
        if (!ends_with_ci(stored, EXTS[i].stored)) continue;
        if (EXTS[i].aes) return aes_file_decrypt(&g->aes, out->data, &out->len);
        if (g->has_mv_key) mv_decrypt(out->data, &out->len, g->mv_key);
        return 1;
    }
    return 1;
}

int game_read(Game* g, const char* path, Buf* out) {
    char* s = game_resolve(g, path);
    if (!s) return 0;
    int ok = game_read_stored(g, s, out);
    free(s);
    return ok;
}

int game_exists(Game* g, const char* path) {
    char* s = game_resolve(g, path);
    free(s);
    return s != NULL;
}

Node* game_json_opt(Game* g, const char* path) {
    Buf b = {0};
    if (!game_read(g, path, &b)) return NULL;
    size_t skip = b.len >= 3 && b.data[0] == 0xEF && b.data[1] == 0xBB && b.data[2] == 0xBF ? 3 : 0;
    char err[128] = {0};
    Node* n = json_parse((const char*)b.data + skip, b.len - skip, err, sizeof(err));
    buf_free(&b);
    if (!n) fatal("bad json in %s: %s", path, err);
    return n;
}

Node* game_json(Game* g, const char* path) {
    Node* n = game_json_opt(g, path);
    if (!n) fatal("missing resource: %s", path);
    return n;
}

Node* game_plugins(Game* g) {
    Buf b = {0};
    if (!game_read(g, "js/plugins.js", &b)) fatal("missing js/plugins.js");
    char* s = (char*)b.data;
    size_t n = b.len;
    size_t start = 0, end = n;
    while (start < n && s[start] != '[') start++;
    while (end > start && s[end - 1] != ']') end--;
    if (start >= end) fatal("bad js/plugins.js");
    size_t k = end - 1;
    while (k > start && (s[k - 1] == ' ' || s[k - 1] == '\t' || s[k - 1] == '\r' || s[k - 1] == '\n')) k--;
    if (k > start && s[k - 1] == ',') s[k - 1] = ' ';
    char err[128] = {0};
    Node* r = json_parse(s + start, end - start, err, sizeof(err));
    buf_free(&b);
    if (!r) fatal("bad js/plugins.js: %s", err);
    return r;
}

char** game_stored_files(Game* g, const char* folder, int* count) {
    size_t fl = strlen(folder);
    char* lo = lower_dup(folder);
    char** out = NULL;
    int n = 0;
    for (int i = 0; i < g->nfiles; i++) {
        if (strncmp(g->lower[i], lo, fl) != 0 || g->lower[i][fl] != '/') continue;
        out = xrealloc(out, sizeof(char*) * (size_t)(n + 1));
        out[n++] = xstrdup(g->files[i]);
    }
    free(lo);
    *count = n;
    return out;
}

static int is_hex_key(const char* s) {
    for (int i = 0; i < 32; i++) {
        char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return 0;
    }
    return 1;
}

static int scan_key(const uint8_t* data, size_t len, char out[33]) {
    for (size_t i = 0; i + 34 <= len; i++) {
        if (data[i] != '-' || data[i + 1] != '-') continue;
        if (!is_hex_key((const char*)data + i + 2)) continue;
        if (i + 34 < len) {
            char c = (char)data[i + 34];
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) continue;
        }
        memcpy(out, data + i + 2, 32);
        out[32] = 0;
        return 1;
    }
    return 0;
}

static int key_works(Game* g, const char* key) {
    Aes256 a;
    aes256_init(&a, (const uint8_t*)key);
    const char* stored = find_ci(g, "data/System.KEL");
    if (!stored) return 0;
    Buf b = {0};
    if (!read_raw(g, stored, &b)) return 0;
    int ok = aes_file_decrypt(&a, b.data, &b.len) && b.len > 2 && b.data[0] == '{';
    buf_free(&b);
    if (ok) {
        memcpy(g->key, key, 33);
        g->aes = a;
    }
    return ok;
}

static int try_key_file(Game* g, const char* path) {
    Buf b = {0};
    if (!file_read(path, &b)) return 0;
    char k[33];
    int ok = scan_key(b.data, b.len, k) && key_works(g, k);
    buf_free(&b);
    return ok;
}

static int find_key(Game* g) {
    int n;
    char** names = dir_list(g->install, &n);
    int ok = 0;
    for (int i = 0; i < n && !ok; i++) {
        if (!ends_with_ci(names[i], ".bat") && !ends_with_ci(names[i], ".sh") && !ends_with_ci(names[i], ".cmd")) continue;
        char* p = path_join(g->install, names[i]);
        ok = try_key_file(g, p);
        free(p);
    }
    str_list_free(names, n);
    if (ok) return 1;
    const char* env = getenv("OMORI_KEY");
    if (env && strlen(env) == 32 && key_works(g, env)) return 1;
    const char* home = getenv("HOME");
    const char* rel[] = {".steam/steam/appcache/appinfo.vdf", ".local/share/Steam/appcache/appinfo.vdf",
                         ".var/app/com.valvesoftware.Steam/.local/share/Steam/appcache/appinfo.vdf",
                         "Library/Application Support/Steam/appcache/appinfo.vdf", NULL};
    for (int i = 0; home && rel[i] && !ok; i++) {
        char* p = path_join(home, rel[i]);
        ok = try_key_file(g, p);
        free(p);
    }
#ifdef _WIN32
    const char* win[] = {"C:\\Program Files (x86)\\Steam\\appcache\\appinfo.vdf", "C:\\Program Files\\Steam\\appcache\\appinfo.vdf", NULL};
    for (int i = 0; win[i] && !ok; i++) ok = try_key_file(g, win[i]);
#endif
    return ok;
}

static char* read_version(Game* g) {
    Buf b = {0};
    if (!game_read(g, "js/plugins/Omori Title Screen.js", &b)) return xstrdup("unknown");
    buf_u8(&b, 0);
    const char* s = (const char*)b.data;
    const char* m = strstr(s, "drawText(\"V");
    char* out = NULL;
    if (m) {
        m += 11;
        const char* e = strchr(m, '"');
        if (e && e > m && e - m < 16) out = xstrndup(m, (size_t)(e - m));
    }
    buf_free(&b);
    return out ? out : xstrdup("unknown");
}

static int hexv(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void load_mv_key(Game* g) {
    Node* sys = game_json_opt(g, "data/System.json");
    const char* k = sys ? node_strv(node_get(sys, "encryptionKey")) : NULL;
    if (k && strlen(k) == 32) {
        g->has_mv_key = 1;
        for (int i = 0; i < 16; i++) {
            int hi = hexv(k[i * 2]), lo = hexv(k[i * 2 + 1]);
            if (hi < 0 || lo < 0) g->has_mv_key = 0;
            g->mv_key[i] = (uint8_t)(hi * 16 + lo);
        }
    }
    node_free(sys);
}

static char* install_of(const char* root) {
    size_t n = strlen(root);
    while (n > 1 && (root[n - 1] == '/' || root[n - 1] == '\\')) n--;
    if (n >= 4 && ends_with_ci(root, "www") && (root[n - 4] == '/' || root[n - 4] == '\\')) return xstrndup(root, n - 4);
    return xstrndup(root, n);
}

int game_open(Game* g, const char* path, const char* key) {
    memset(g, 0, sizeof(*g));
    g->root = find_root(path);
    if (!g->root) return 0;
    g->install = install_of(g->root);
    build_index(g);
    int ok = key ? key_works(g, key) : find_key(g);
    if (!ok) {
        g->version = xstrdup("unknown");
        return -1;
    }
    load_mv_key(g);
    g->version = read_version(g);
    return 1;
}

void game_close(Game* g) {
    free(g->root);
    free(g->install);
    free(g->version);
    for (int i = 0; i < g->nfiles; i++) {
        free(g->files[i]);
        free(g->lower[i]);
    }
    free(g->files);
    free(g->lower);
    free(g->by_lower);
    memset(g, 0, sizeof(*g));
}
