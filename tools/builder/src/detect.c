#include "detect.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "game.h"
#include "util.h"

#ifdef _WIN32
#include <windows.h>
#endif

static int ieq(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static int icontains(const char* hay, size_t n, const char* needle) {
    size_t m = strlen(needle);
    if (m == 0 || n < m) return 0;
    for (size_t i = 0; i + m <= n; i++) {
        size_t k = 0;
        while (k < m && tolower((unsigned char)hay[i + k]) == tolower((unsigned char)needle[k])) k++;
        if (k == m) return 1;
    }
    return 0;
}

static int ends_with_i(const char* s, const char* suf) {
    size_t a = strlen(s), b = strlen(suf);
    return a >= b && ieq(s + a - b, suf);
}

typedef struct {
    char** items;
    int count;
} Paths;

static void paths_add(Paths* p, char* s) {
    for (int i = 0; i < p->count; i++) {
        if (!strcmp(p->items[i], s)) {
            free(s);
            return;
        }
    }
    p->items = xrealloc(p->items, sizeof(char*) * (size_t)(p->count + 1));
    p->items[p->count++] = s;
}

static void paths_free(Paths* p) {
    for (int i = 0; i < p->count; i++) free(p->items[i]);
    free(p->items);
}

static char* vdf_next_string(const char** cur) {
    const char* p = *cur;
    while (*p && *p != '"') p++;
    if (!*p) return NULL;
    p++;
    Buf b = {0};
    while (*p && *p != '"') {
        if (*p == '\\' && p[1]) {
            p++;
            buf_u8(&b, (uint8_t)*p++);
            continue;
        }
        buf_u8(&b, (uint8_t)*p++);
    }
    if (*p == '"') p++;
    *cur = p;
    buf_u8(&b, 0);
    return (char*)b.data;
}

static char* vdf_value(const char* text, const char* key) {
    const char* p = text;
    char* s;
    while ((s = vdf_next_string(&p)) != NULL) {
        int hit = ieq(s, key);
        free(s);
        if (hit) {
            const char* q = p;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '"') return vdf_next_string(&p);
        }
    }
    return NULL;
}

static void library_paths(const char* steam_root, Paths* libs) {
    paths_add(libs, xstrdup(steam_root));
    const char* files[] = {"steamapps/libraryfolders.vdf", "config/libraryfolders.vdf", NULL};
    for (int f = 0; files[f]; f++) {
        char* path = path_join(steam_root, files[f]);
        Buf b = {0};
        int ok = file_read(path, &b);
        free(path);
        if (!ok) continue;
        buf_u8(&b, 0);
        const char* p = (const char*)b.data;
        char* s;
        while ((s = vdf_next_string(&p)) != NULL) {
            int hit = ieq(s, "path");
            free(s);
            if (!hit) continue;
            const char* q = p;
            while (*q == ' ' || *q == '\t') q++;
            if (*q != '"') continue;
            char* v = vdf_next_string(&p);
            if (v) paths_add(libs, v);
        }
        buf_free(&b);
    }
}

static int looks_like_game(const char* dir) {
    Game g;
    if (!dir_exists(dir)) return 0;
    int r = game_open(&g, dir, NULL);
    game_close(&g);
    return r != 0;
}

static char* find_in_library(const char* lib) {
    char* apps = path_join(lib, "steamapps");
    int n;
    char** names = dir_list(apps, &n);
    char* found = NULL;
    for (int i = 0; i < n && !found; i++) {
        if (strncmp(names[i], "appmanifest_", 12) != 0 || !ends_with_i(names[i], ".acf")) continue;
        char* path = path_join(apps, names[i]);
        Buf b = {0};
        int ok = file_read(path, &b);
        free(path);
        if (!ok) continue;
        buf_u8(&b, 0);
        char* appid = vdf_value((const char*)b.data, "appid");
        char* name = vdf_value((const char*)b.data, "name");
        int match = (appid && !strcmp(appid, STEAM_APP_ID)) || (name && ieq(name, "OMORI"));
        if (match) {
            char* inst = vdf_value((const char*)b.data, "installdir");
            if (inst) {
                char* common = path_join(apps, "common");
                char* dir = path_join(common, inst);
                if (looks_like_game(dir)) found = dir;
                else free(dir);
                free(common);
                free(inst);
            }
        }
        free(appid);
        free(name);
        buf_free(&b);
    }
    str_list_free(names, n);
    if (!found) {
        char* dir = str_fmt("%s/common/OMORI", apps);
        if (looks_like_game(dir)) found = dir;
        else free(dir);
    }
    free(apps);
    return found;
}

#ifdef _WIN32
static char* reg_string(HKEY root, const wchar_t* key, const wchar_t* value) {
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, NULL, buf, &size) != ERROR_SUCCESS) return NULL;
    return wide_to_utf8(buf);
}
#endif

static void steam_roots(Paths* roots) {
    const char* env = getenv("OMORI_STEAM_ROOT");
    if (env && *env) {
        paths_add(roots, xstrdup(env));
        return;
    }
#ifdef _WIN32
    char* s = reg_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (s) paths_add(roots, s);
    s = reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (s) paths_add(roots, s);
    s = reg_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");
    if (s) paths_add(roots, s);
    paths_add(roots, xstrdup("C:\\Program Files (x86)\\Steam"));
    paths_add(roots, xstrdup("C:\\Program Files\\Steam"));
#else
    const char* home = getenv("HOME");
    if (!home) return;
#ifdef __APPLE__
    const char* rel[] = {"Library/Application Support/Steam", NULL};
#else
    const char* rel[] = {".steam/steam", ".steam/root", ".local/share/Steam", ".var/app/com.valvesoftware.Steam/.local/share/Steam",
                         "snap/steam/common/.local/share/Steam", NULL};
#endif
    for (int i = 0; rel[i]; i++) paths_add(roots, path_join(home, rel[i]));
#endif
}

char* steam_find_game(void) {
    Paths roots = {0}, libs = {0};
    steam_roots(&roots);
    for (int i = 0; i < roots.count; i++) {
        if (dir_exists(roots.items[i])) library_paths(roots.items[i], &libs);
    }
    char* found = NULL;
    for (int i = 0; i < libs.count && !found; i++) found = find_in_library(libs.items[i]);
    paths_free(&roots);
    paths_free(&libs);
    return found;
}

char* install_dir_of(const char* game_root) {
    size_t n = strlen(game_root);
    while (n > 1 && (game_root[n - 1] == '/' || game_root[n - 1] == '\\')) n--;
    if (n >= 4 && ieq(game_root + n - 3, "www") && (game_root[n - 4] == '/' || game_root[n - 4] == '\\')) return xstrndup(game_root, n - 4);
    return xstrndup(game_root, n);
}

static const char* BAD_DIRS[] = {"steam_settings", "smartsteamemu", "goldberg", "goldberg_emulator", "gse_settings", "coldclientloader", NULL};
static const char* BAD_FILES[] = {"steamclient.dll", "steamclient64.dll", "steamclient_loader.exe", "steamclient_loader_x64.exe",
                                  "steamclient_loader_x32.exe", "coldclientloader.ini", "steam_emu.ini", "steam_api.ini", "steam_api64.ini",
                                  "codex.ini", "codex64.dll", "cpy.ini", "ali213.ini", "3dmgame.ini", "3dmgame.dll", "onlinefix.ini",
                                  "onlinefix.dll", "onlinefix64.dll", "smartsteamemu.ini", "smartsteamemu.dll", "smartsteamemu64.dll",
                                  "cream_api.ini", "steam_interfaces.txt", "local_save.txt", "steam_api.cdx", "steam_api64.cdx",
                                  "steam_api_o.dll", "steam_api64_o.dll", "rld.ini", "valve.ini", "hlm.ini", "tenoke.ini", "empress.ini",
                                  "gbe_crash.txt", "steamconfig.ini", "steam_api.dll.valve", "steam_api64.dll.valve", "steam_api.dll.bak",
                                  "steam_api64.dll.bak", NULL};
static const char* INI_MARKERS[] = {"[steamclient]", "steamclientdll", "[main::", "[user::", "[app::", "goldberg", "gbe_", "steamemu",
                                    "steam_emu", "smartsteam", "codex", "onlinefix", "ali213", "3dmgame", "creamapi", "cream_api",
                                    "account_name", "accountname", "playername", "savetype", "[settings]\r\nappid", "[settings]\nappid", NULL};
static const char* DLL_MARKERS[] = {"gbe_", "goldberg", "smartsteamemu", "onlinefix", "creamapi", "codex", "ali213", NULL};
static const char* GROUP_MARKERS[] = {"steamrip", "fitgirl", "dodi", NULL};

static int has_marker(const char* data, size_t n, const char** markers, const char** which) {
    for (int i = 0; markers[i]; i++) {
        if (icontains(data, n, markers[i])) {
            *which = markers[i];
            return 1;
        }
    }
    return 0;
}

static int check_file_content(const char* path, const char** markers, size_t max, const char** which) {
    Buf b = {0};
    if (!file_read(path, &b)) return 0;
    int hit = has_marker((const char*)b.data, b.len < max ? b.len : max, markers, which);
    buf_free(&b);
    return hit;
}

static int scan(const char* base, const char* rel, int depth, char* reason, size_t rlen) {
    char* dir = rel[0] ? path_join(base, rel) : xstrdup(base);
    int n;
    char** names = dir_list(dir, &n);
    int hit = 0;
    for (int i = 0; i < n && !hit; i++) {
        const char* nm = names[i];
        char* full = path_join(dir, nm);
        char* shown = rel[0] ? str_fmt("%s/%s", rel, nm) : xstrdup(nm);
        const char* which = NULL;
        if (dir_exists(full)) {
            for (int k = 0; BAD_DIRS[k] && !hit; k++) {
                if (ieq(nm, BAD_DIRS[k])) {
                    snprintf(reason, rlen, "%s (crack settings folder)", shown);
                    hit = 1;
                }
            }
            int skip = ieq(nm, "data") || ieq(nm, "img") || ieq(nm, "audio") || ieq(nm, "locales") || ieq(nm, "swiftshader") ||
                       ieq(nm, "maps") || ieq(nm, "languages") || ieq(nm, "movies") || ieq(nm, "plugins");
            if (!hit && depth < 4 && !skip) hit = scan(base, shown, depth + 1, reason, rlen);
        } else {
            for (int k = 0; BAD_FILES[k] && !hit; k++) {
                if (ieq(nm, BAD_FILES[k])) {
                    snprintf(reason, rlen, "%s (crack file)", shown);
                    hit = 1;
                }
            }
            if (!hit && ends_with_i(nm, ".ini") && check_file_content(full, INI_MARKERS, 1 << 20, &which)) {
                snprintf(reason, rlen, "%s (contains \"%s\")", shown, which);
                hit = 1;
            }
            if (!hit && (ieq(nm, "steam_api.dll") || ieq(nm, "steam_api64.dll")) && check_file_content(full, DLL_MARKERS, 64u << 20, &which)) {
                snprintf(reason, rlen, "%s (replaced by a steam emulator)", shown);
                hit = 1;
            }
            if (!hit && ends_with_i(nm, ".nfo")) {
                snprintf(reason, rlen, "%s (release info file)", shown);
                hit = 1;
            }
            if (!hit && depth == 0 && (ends_with_i(nm, ".url") || ends_with_i(nm, ".txt")) && has_marker(nm, strlen(nm), GROUP_MARKERS, &which)) {
                snprintf(reason, rlen, "%s (release group file)", shown);
                hit = 1;
            }
        }
        free(full);
        free(shown);
    }
    str_list_free(names, n);
    free(dir);
    return hit;
}

static int scan_parent(const char* install_dir, char* reason, size_t rlen) {
    size_t n = strlen(install_dir);
    while (n > 0 && install_dir[n - 1] != '/' && install_dir[n - 1] != '\\') n--;
    if (n <= 1) return 0;
    char* parent = xstrndup(install_dir, n - 1);
    const char* pn = parent;
    for (const char* c = parent; *c; c++) {
        if (*c == '/' || *c == '\\') pn = c + 1;
    }
    const char* which = NULL;
    int hit = 0;
    if (has_marker(pn, strlen(pn), GROUP_MARKERS, &which)) {
        snprintf(reason, rlen, "%s (release group folder)", pn);
        hit = 1;
    }
    int cnt;
    char** names = dir_list(parent, &cnt);
    for (int i = 0; i < cnt && !hit; i++) {
        const char* nm = names[i];
        if ((ends_with_i(nm, ".url") || ends_with_i(nm, ".txt") || ends_with_i(nm, ".nfo")) &&
            (ends_with_i(nm, ".nfo") || has_marker(nm, strlen(nm), GROUP_MARKERS, &which) || icontains(nm, strlen(nm), "read_me_instructions"))) {
            snprintf(reason, rlen, "%s next to the game folder (release group file)", nm);
            hit = 1;
        }
    }
    str_list_free(names, cnt);
    free(parent);
    return hit;
}

int crack_scan(const char* install_dir, char* reason, size_t reason_len) {
    reason[0] = 0;
    if (scan(install_dir, "", 0, reason, reason_len)) return 1;
    return scan_parent(install_dir, reason, reason_len);
}
