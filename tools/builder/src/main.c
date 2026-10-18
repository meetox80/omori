#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets.h"
#include "db.h"
#include "detect.h"
#include "game.h"
#include "maps.h"
#include "movie.h"
#include "node.h"
#include "pack.h"
#include "script.h"
#include "ui.h"
#include "util.h"
#include "yaml.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

extern const unsigned char embedded_engine[];
extern const size_t embedded_engine_size;

static const char* SUPPORTED[] = {"1.0.8", NULL};
static const char* TITLE = "OMORI";
static const char* UI_TITLE = "OMORI for PSP";

typedef struct {
    char** items;
    int count;
    int cap;
} StrList;

static StrList g_errors;
static StrList g_script_errors;
static Mutex* g_errors_lock;

static void sl_add(StrList* l, char* s) {
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->items = xrealloc(l->items, sizeof(char*) * (size_t)l->cap);
    }
    l->items[l->count++] = s;
}

static void sl_free(StrList* l) {
    for (int i = 0; i < l->count; i++) free(l->items[i]);
    free(l->items);
    memset(l, 0, sizeof(*l));
}

static void on_problem(const char* msg) {
    mutex_lock(g_errors_lock);
    sl_add(&g_errors, xstrdup(msg));
    mutex_unlock(g_errors_lock);
}

static void on_script_error(const char* msg) {
    mutex_lock(g_errors_lock);
    sl_add(&g_script_errors, xstrdup(msg));
    mutex_unlock(g_errors_lock);
}

static void report(void) {
    if (g_script_errors.count) {
        log_msg("  %d scripts in the game data have errors; they are skipped, as in the original game", g_script_errors.count);
        if (getenv("OMORI_VERBOSE")) {
            for (int i = 0; i < g_script_errors.count; i++) log_msg("    %s", g_script_errors.items[i]);
        }
    }
    for (int i = 0; i < g_errors.count && i < 50; i++) log_msg("  problem: %s", g_errors.items[i]);
    if (g_errors.count > 50) log_msg("  ... %d more problems", g_errors.count - 50);
}

static Node* load_atlas(Game* g) {
    Buf b = {0};
    if (!game_read(g, "data/Atlas.yaml", &b)) fatal("missing data/Atlas.yaml");
    char err[200];
    Node* n = yaml_parse((const char*)b.data, b.len, err, sizeof(err));
    buf_free(&b);
    if (!n) fatal("bad data/Atlas.yaml: %s", err);
    return n;
}

typedef struct {
    Game* game;
    int* ids;
    PackEntry** bins;
    PackEntry** texs;
    Pack* pack;
} MapJobs;

static void map_job(void* ctx, int i) {
    MapJobs* mj = ctx;
    char* err = NULL;
    if (!maps_build_one(mj->game, mj->ids[i], &mj->bins[i]->data, &mj->texs[i]->data, &err)) {
        mj->bins[i]->error = str_fmt("map %d: missing data", mj->ids[i]);
    } else if (err) {
        on_problem(err);
    }
    free(err);
    pack_ready(mj->pack, mj->bins[i]);
    pack_ready(mj->pack, mj->texs[i]);
}

static int build_maps(Game* g, Node* infos, Pack* pack, int jobs) {
    int* ids = NULL;
    int n = 0;
    for (size_t i = 0; infos && i < infos->count; i++) {
        Node* info = infos->items[i];
        if (info->type != N_OBJ) continue;
        int id = (int)node_numv(node_get(info, "id"), 0);
        char* path = str_fmt("data/Map%03d.json", id);
        int exists = game_exists(g, path);
        free(path);
        if (!exists) continue;
        ids = xrealloc(ids, sizeof(int) * (size_t)(n + 1));
        ids[n++] = id;
    }
    MapJobs mj;
    mj.game = g;
    mj.ids = ids;
    mj.pack = pack;
    mj.bins = xmalloc(sizeof(PackEntry*) * (size_t)(n ? n : 1));
    mj.texs = xmalloc(sizeof(PackEntry*) * (size_t)(n ? n : 1));
    int first = pack->count;
    for (int i = 0; i < n; i++) {
        char* a = str_fmt("maps/map%03d.bin", ids[i]);
        char* b = str_fmt("maps/map%03d.tex", ids[i]);
        pack_add(pack, a);
        pack_add(pack, b);
        free(a);
        free(b);
    }
    for (int i = 0; i < n; i++) {
        mj.bins[i] = &pack->items[first + i * 2];
        mj.texs[i] = &pack->items[first + i * 2 + 1];
    }
    maps_init();
    run_jobs(map_job, &mj, n, jobs);
    maps_cleanup();
    free(mj.bins);
    free(mj.texs);
    free(ids);
    return n;
}

static int is_supported(const char* v) {
    for (int i = 0; SUPPORTED[i]; i++) {
        if (!strcmp(SUPPORTED[i], v)) return 1;
    }
    return 0;
}

static int ends_with_ci(const char* s, const char* suf) {
    size_t a = strlen(s), b = strlen(suf);
    if (a < b) return 0;
    for (size_t i = 0; i < b; i++) {
        char x = s[a - b + i], y = suf[i];
        if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
        if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
        if (x != y) return 0;
    }
    return 1;
}

static char* parent_dir(const char* path) {
    const char* a = strrchr(path, '/');
    const char* b = strrchr(path, '\\');
    const char* s = a > b ? a : b;
    return s ? xstrndup(path, (size_t)(s - path)) : xstrdup(".");
}

static void usage(void) {
    printf("usage: omori-builder [game folder] [options]\n"
           "\n"
           "Builds a single EBOOT.PBP for PSP from your own copy of OMORI.\n"
           "Run it without a game folder to find your Steam copy automatically or pick the folder in a window.\n"
           "\n"
           "options:\n"
           "  -o, --out PATH         output folder (EBOOT.PBP goes to PATH/PSP/GAME/OMORI) or a path ending in .PBP (default: out)\n"
           "  --engine FILE          use this engine EBOOT.PBP instead of the built-in one\n"
           "  --pak FILE             also write the data pack on its own (for the desktop test build)\n"
           "  --pak-only FILE        write only the data pack, without an engine or EBOOT.PBP\n"
           "  --key KEY              the game's 32-character decryption key, if it cannot be found automatically\n"
           "  --jobs N               worker threads (default: all cores)\n"
           "  --movie NAME           convert only the movie NAME (for example final_duet.webm) into the output folder and exit\n"
           "  --movies-from DIR      take movies already converted with --movie from DIR instead of converting them again\n"
           "  --allow-any-version    convert even if the game version is not supported\n"
           "  --update-engine FILE   replace only the engine inside an existing EBOOT.PBP and exit\n");
}

static int load_engine(const char* path, Pbp* out) {
    if (path) {
        Buf b = {0};
        if (!file_read(path, &b)) {
            fprintf(stderr, "error: cannot read %s\n", path);
            return 0;
        }
        int ok = pbp_read(b.data, b.len, out);
        buf_free(&b);
        if (!ok) fprintf(stderr, "error: %s is not an EBOOT.PBP\n", path);
        return ok;
    }
    if (!embedded_engine_size) {
        fprintf(stderr, "error: this builder was compiled without the PSP engine; pass --engine\n");
        return 0;
    }
    if (!pbp_read(embedded_engine, embedded_engine_size, out)) {
        fprintf(stderr, "error: built-in engine is damaged\n");
        return 0;
    }
    return 1;
}

static int copy_range(FILE* from, uint64_t at, uint64_t len, FILE* to) {
    if (!file_seek(from, at)) return 0;
    uint8_t* chunk = xmalloc(1 << 20);
    int ok = 1;
    while (len > 0 && ok) {
        size_t want = len > (1u << 20) ? (1u << 20) : (size_t)len;
        size_t got = fread(chunk, 1, want, from);
        if (got != want || fwrite(chunk, 1, got, to) != got) ok = 0;
        len -= got;
    }
    free(chunk);
    return ok;
}

static uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int update_engine(const char* target, const char* engine_path) {
    FILE* f = file_open(target, "rb");
    uint8_t h[40];
    uint64_t size = 0;
    if (!f || fread(h, 1, 40, f) != 40 || memcmp(h, "\0PBP", 4) != 0 || !file_length(f, &size)) {
        if (f) fclose(f);
        fprintf(stderr, "error: %s is not an EBOOT.PBP\n", target);
        return 2;
    }
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
    if (!ok || !load_engine(engine_path, &eng)) {
        fclose(f);
        pbp_free(&cur);
        if (!ok) fprintf(stderr, "error: %s is damaged\n", target);
        return 2;
    }
    cur.part[6] = eng.part[6];
    memset(&eng.part[6], 0, sizeof(Buf));
    char* tmp = str_fmt("%s.tmp", target);
    FILE* o = file_open(tmp, "wb");
    ok = o && pbp_write_head(&cur, o) && copy_range(f, off[7], size - off[7], o);
    fclose(f);
    if (o && fclose(o) != 0) ok = 0;
    if (ok) ok = file_replace(tmp, target);
    if (!ok) remove(tmp);
    free(tmp);
    pbp_free(&cur);
    pbp_free(&eng);
    if (!ok) {
        fprintf(stderr, "error: cannot write %s\n", target);
        return 2;
    }
    log_msg("engine updated in %s", target);
    return 0;
}

static void fatal_dialog(const char* msg) {
    if (ui_is_native()) ui_message(UI_TITLE, msg, 1);
}

static int check_copy(Game* g) {
    char reason[512];
    int cracked = crack_scan(g->install, reason, sizeof(reason));
    if (!cracked) return 1;
    log_msg("this copy looks cracked: %s", reason);
    if (getenv("OMORI_CRACK_OK")) return 1;
    int native = ui_is_native();
    if (!native && getenv("OMORI_GUI") && ui_native_available()) ui_use_native(1);
    else if (!native && !ui_stdin_is_terminal()) {
        if (!ui_native_available()) {
            fprintf(stderr, "error: this copy of the game looks cracked (%s). Buy the game on Steam: %s\n", reason, STEAM_STORE_URL);
            return 0;
        }
        ui_use_native(1);
    }
    char* text = str_fmt("This copy of OMORI looks cracked.\nFound: %s\n\n"
                         "You need to buy the game to use this port. OMOCAT made it, and buying it on Steam is what lets them keep "
                         "making games.",
                         reason);
    CrackChoice c = ui_crack_warning("Please buy the game", text);
    free(text);
    ui_use_native(native);
    if (c == CRACK_BUY) ui_open_url(STEAM_STORE_URL);
    if (c != CRACK_CONTINUE) {
        log_msg("stopped");
        return 0;
    }
    return 1;
}

typedef struct {
    const char* engine;
    const char* pak;
    const char* key;
    int jobs;
    int any_version;
    int pak_only;
    const char* movie;
    const char* movies_from;
} Options;

static int open_game(Game* g, const char* dir, const Options* o) {
    int r = game_open(g, dir, o->key);
    if (r == 0) {
        char* m = str_fmt("Could not find OMORI in %s (expected a folder containing www/js/rpg_managers.js).", dir);
        fprintf(stderr, "error: %s\n", m);
        if (ui_is_native()) ui_message(UI_TITLE, m, 1);
        free(m);
        return 0;
    }
    if (r < 0) {
        const char* m = "Could not find the game's decryption key. It is normally in Launch_OMORI.bat next to OMORI.exe, or in Steam's launch "
                        "options. Pass it with --key.";
        fprintf(stderr, "error: %s\n", m);
        if (ui_is_native()) ui_message(UI_TITLE, m, 1);
        game_close(g);
        return 0;
    }
    log_msg("game: %s", g->root);
    log_msg("game version %s", g->version);
    if (!is_supported(g->version) && !o->any_version) {
        char* m = str_fmt("This game version (%s) is not supported. The port needs version %s.", g->version, SUPPORTED[0]);
        fprintf(stderr, "error: %s\n", m);
        if (ui_is_native()) ui_message(UI_TITLE, m, 1);
        free(m);
        game_close(g);
        return 0;
    }
    return 1;
}

static char* choose_game(const Options* o) {
    char* found = steam_find_game();
    if (found) {
        char* q = str_fmt("Found OMORI in your Steam library:\n%s\n\nUse this copy?", found);
        int yes = ui_yes_no(UI_TITLE, q, "Use this copy", "Choose another folder");
        free(q);
        if (yes) return found;
        free(found);
    }
    const char* prompt = found ? "Select the OMORI folder (the one with OMORI.exe in it)"
                               : "Steam copy not found. Select the OMORI folder (the one with OMORI.exe in it)";
    for (;;) {
        char* dir = ui_pick_folder(UI_TITLE, prompt);
        if (!dir) return NULL;
        Game g;
        int r = game_open(&g, dir, o->key);
        game_close(&g);
        if (r != 0) return dir;
        ui_message(UI_TITLE, "That folder does not contain OMORI. Pick the folder that has OMORI.exe and the www folder in it.", 1);
        free(dir);
    }
}

static int build(Game* g, const char* out, const Options* o, char** target_out) {
    Pbp eng;
    memset(&eng, 0, sizeof(eng));
    if (!o->pak_only && !load_engine(o->engine, &eng)) return 2;
    Node* atlas = load_atlas(g);
    char* target = o->pak_only ? xstrdup(o->pak) : ends_with_ci(out, ".pbp") ? xstrdup(out) : str_fmt("%s/PSP/GAME/OMORI/EBOOT.PBP", out);
    char* dir = parent_dir(target);
    make_dirs(dir);
    free(dir);
    FILE* f = file_open(target, "wb");
    if (!f) {
        fprintf(stderr, "error: cannot write %s\n", target);
        free(target);
        node_free(atlas);
        pbp_free(&eng);
        return 2;
    }
    uint64_t pack_at = 0;
    int ok = 1;
    if (!o->pak_only) {
        Pbp head;
        memset(&head, 0, sizeof(head));
        sfo_build(TITLE, &head.part[0]);
        assets_build_icons(g, atlas, &head.part[1], &head.part[4]);
        head.part[6] = eng.part[6];
        memset(&eng.part[6], 0, sizeof(Buf));
        ok = pbp_write_head(&head, f);
        for (int i = 0; i < 7; i++) pack_at += head.part[i].len;
        pack_at += 40;
        pbp_free(&head);
    }
    pbp_free(&eng);
    Pack pack = {0};
    ok = ok && pack_open(&pack, f);
    log_msg("database");
    Node* lang = NULL;
    Node* db = db_build(g, &pack, &lang);
    PackEntry* dbe = pack_add(&pack, "game.bin");
    vbin_dump(db, &dbe->data);
    pack_flush(&pack);
    log_msg("maps");
    log_msg("  %d maps", build_maps(g, node_get(db, "mapInfos"), &pack, o->jobs));
    pack_flush(&pack);
    log_msg("images");
    log_msg("  %d images", assets_build_images(g, atlas, &pack, o->jobs));
    pack_flush(&pack);
    log_msg("audio");
    log_msg("  %d files", assets_build_audio(g, &pack));
    pack_flush(&pack);
    log_msg("movies");
    log_msg("  %d movies", assets_build_movies(g, &pack, o->jobs, o->movies_from));
    pack_flush(&pack);
    log_msg("fonts");
    assets_build_fonts(g, db, lang, &pack, o->jobs);
    node_free(lang);
    node_free(db);
    node_free(atlas);
    int failed = 0;
    for (int i = 0; i < pack.count; i++) {
        if (pack.items[i].error) {
            sl_add(&g_errors, xstrdup(pack.items[i].error));
            failed++;
        }
    }
    log_msg("eboot");
    uint64_t pack_len = 0;
    ok = pack_finish(&pack, &pack_len) && ok;
    pack_free(&pack);
    ok = fclose(f) == 0 && ok;
    if (ok && o->pak && !o->pak_only) {
        FILE* from = file_open(target, "rb");
        FILE* to = file_open(o->pak, "wb");
        int copied = from && to && copy_range(from, pack_at, pack_len, to);
        if (from) fclose(from);
        if (to && fclose(to) != 0) copied = 0;
        if (!copied) fprintf(stderr, "error: cannot write %s\n", o->pak);
    }
    report();
    if (!ok) {
        fprintf(stderr, "error: cannot write %s\n", target);
        free(target);
        return 2;
    }
    log_msg("done (%.1f MB) -> %s", (pack_at + pack_len) / 1048576.0, target);
    *target_out = target;
    return failed ? 1 : 0;
}

static int run_interactive(const Options* o) {
    ui_use_native(ui_native_available());
    set_fatal_hook(fatal_dialog);
    log_msg("%s builder", UI_TITLE);
    char* dir = choose_game(o);
    if (!dir) return 3;
    Game g;
    int ok = open_game(&g, dir, o);
    free(dir);
    if (!ok) return 2;
    if (!check_copy(&g)) {
        game_close(&g);
        return 3;
    }
    char* out = ui_pick_folder(UI_TITLE, "Choose where to save it: your memory stick, or any folder (a PSP folder will be created inside)");
    if (!out) {
        game_close(&g);
        return 3;
    }
    char* target = NULL;
    int rc = build(&g, out, o, &target);
    if (target) {
        char* m = str_fmt("Done!\n\nYour game is at:\n%s\n\nIf you picked your memory stick, it is ready to play. Otherwise copy the PSP folder from\n%s\nto the root of your memory stick.",
                          target, out);
        ui_message(UI_TITLE, m, 0);
        free(m);
    } else {
        ui_message(UI_TITLE, "Building the EBOOT failed. Run the builder from a terminal to see the details.", 1);
    }
    free(target);
    free(out);
    game_close(&g);
    return rc;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    int wargc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv) {
        argc = wargc;
        argv = xmalloc(sizeof(char*) * (size_t)(argc + 1));
        for (int i = 0; i < argc; i++) argv[i] = wide_to_utf8(wargv[i]);
        argv[argc] = NULL;
        LocalFree(wargv);
    }
#endif
    g_errors_lock = mutex_new();
    script_set_problem_handler(on_script_error);
    const char* game_dir = NULL;
    const char* out = "out";
    const char* update = NULL;
    Options o = {NULL, NULL, NULL, cpu_count(), 0, 0, NULL, NULL};
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if ((!strcmp(a, "-o") || !strcmp(a, "--out")) && i + 1 < argc) out = argv[++i];
        else if (!strcmp(a, "--engine") && i + 1 < argc) o.engine = argv[++i];
        else if (!strcmp(a, "--pak") && i + 1 < argc) o.pak = argv[++i];
        else if (!strcmp(a, "--key") && i + 1 < argc) o.key = argv[++i];
        else if (!strcmp(a, "--jobs") && i + 1 < argc) o.jobs = atoi(argv[++i]);
        else if (!strcmp(a, "--update-engine") && i + 1 < argc) update = argv[++i];
        else if (!strcmp(a, "--allow-any-version")) o.any_version = 1;
        else if (!strcmp(a, "--movie") && i + 1 < argc) o.movie = argv[++i];
        else if (!strcmp(a, "--movies-from") && i + 1 < argc) o.movies_from = argv[++i];
        else if (!strcmp(a, "--pak-only") && i + 1 < argc) {
            o.pak = argv[++i];
            o.pak_only = 1;
        }
        else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            usage();
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "error: unknown option %s\n", a);
            usage();
            return 2;
        } else if (!game_dir) game_dir = a;
        else {
            usage();
            return 2;
        }
    }
    if (update) return update_engine(update, o.engine);
    if (!game_dir) return run_interactive(&o);
    Game g;
    if (!open_game(&g, game_dir, &o)) return 2;
    if (!check_copy(&g)) {
        game_close(&g);
        return 3;
    }
    if (o.movie) {
        int rc = assets_convert_movie(&g, o.movie, out);
        game_close(&g);
        return rc;
    }
    char* target = NULL;
    int rc = build(&g, out, &o, &target);
    free(target);
    game_close(&g);
    sl_free(&g_errors);
    sl_free(&g_script_errors);
    return rc;
}
