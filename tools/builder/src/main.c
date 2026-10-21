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
#include "package.h"
#include "progress.h"
#include "script.h"
#include "target.h"
#include "ui.h"
#include "util.h"
#include "yaml.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

static const char* SUPPORTED[] = {"1.0.8", NULL};
static char g_ui_title[64] = "OMORI builder";
#define UI_TITLE g_ui_title

static void use_target(const Target* t) {
    target_set(t);
    snprintf(g_ui_title, sizeof(g_ui_title), "OMORI for %s", t->console);
}

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

static void target_names(char* out, size_t cap) {
    out[0] = 0;
    for (int i = 0; i < TARGET_COUNT; i++) {
        size_t n = strlen(out);
        snprintf(out + n, cap - n, "%s%s%s", i ? (i + 1 == TARGET_COUNT ? " or " : ", ") : "", target_get((TargetId)i)->name, i ? "" : " (default)");
    }
}

static void usage(void) {
    char names[160];
    target_names(names, sizeof(names));
    printf("usage: omori-builder [game folder] [options]\n"
           "\n"
           "Builds OMORI for a console from your own copy of the game.\n"
           "Run it without a game folder to find your Steam copy automatically and pick the console and folders in windows.\n"
           "\n"
           "options:\n"
           "  -t, --target NAME      console to build for: %s\n"
           "  -o, --out PATH         output folder (default: out); the console's own folder layout is created inside it.\n"
           "                         A path ending in .PBP (psp), .3dsx (3ds), .pkg (ps3) or .iso (ps2) writes that single file instead\n"
           "  --engine FILE          use this engine file instead of the built-in one\n"
           "  --pak FILE             also write the data pack on its own (for the desktop test build)\n"
           "  --pak-only FILE        write only the data pack, without an engine\n"
           "  --key KEY              the game's 32-character decryption key, if it cannot be found automatically\n"
           "  --jobs N               worker threads (default: all cores)\n"
           "  --movie NAME           convert only the movie NAME (for example final_duet.webm) into the output folder and exit\n"
           "  --movies-from DIR      take movies already converted with --movie from DIR instead of converting them again\n"
           "  --allow-any-version    convert even if the game version is not supported\n"
           "  --update-engine FILE   replace only the engine inside an existing build (the console's executable or package) and exit\n",
           names);
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
    const Target* target;
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
    Node* atlas = load_atlas(g);
    Package* pkg = NULL;
    if (!o->pak_only) {
        pkg = package_open(out, o->engine, g, atlas);
        if (!pkg) {
            node_free(atlas);
            return 2;
        }
    }
    char* target = xstrdup(pkg ? package_data_path(pkg) : o->pak);
    char* dir = path_parent(target);
    make_dirs(dir);
    free(dir);
    FILE* f = file_open(target, "wb");
    if (!f) {
        fprintf(stderr, "error: cannot write %s\n", target);
        free(target);
        node_free(atlas);
        package_free(pkg);
        return 2;
    }
    uint64_t pack_at = 0;
    int ok = !pkg || package_write_head(pkg, f, &pack_at);
    Pack pack = {0};
    ok = ok && pack_open(&pack, f);
    if (pkg) package_add_entries(pkg, &pack);
    log_msg("database");
    progress_phase("database", NULL);
    Node* lang = NULL;
    Node* db = db_build(g, &pack, &lang);
    PackEntry* dbe = pack_add(&pack, "game.bin");
    vbin_dump(db, &dbe->data);
    pack_flush(&pack);
    log_msg("maps");
    progress_phase("maps", NULL);
    log_msg("  %d maps", build_maps(g, node_get(db, "mapInfos"), &pack, o->jobs));
    pack_flush(&pack);
    log_msg("images");
    progress_phase("images", NULL);
    log_msg("  %d images", assets_build_images(g, atlas, &pack, o->jobs));
    pack_flush(&pack);
    log_msg("audio");
    progress_phase("audio", g_target->id == TARGET_PS2 ? "Converting music and sounds" : NULL);
    log_msg("  %d files", assets_build_audio(g, &pack, o->jobs));
    pack_flush(&pack);
    log_msg("movies");
    progress_phase("movies", NULL);
    log_msg("  %d movies", assets_build_movies(g, &pack, o->jobs, o->movies_from));
    pack_flush(&pack);
    log_msg("fonts");
    progress_phase("fonts", NULL);
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
    log_msg("write");
    progress_phase("write", package_write_text());
    uint64_t pack_len = 0;
    ok = pack_finish(&pack, &pack_len) && ok;
    pack_free(&pack);
    ok = ok && (!pkg || package_write_tail(pkg, f, pack_at, pack_len));
    ok = fclose(f) == 0 && ok;
    if (ok && o->pak && pkg) {
        FILE* from = file_open(target, "rb");
        FILE* to = file_open(o->pak, "wb");
        int copied = from && to && file_copy_range(from, pack_at, pack_len, to);
        if (from) fclose(from);
        if (to && fclose(to) != 0) copied = 0;
        if (!copied) fprintf(stderr, "error: cannot write %s\n", o->pak);
    }
    if (ok && pkg) {
        log_msg("finish");
        progress_phase("finish", package_finish_text());
        ok = package_finish(pkg);
    }
    report();
    if (!ok) {
        fprintf(stderr, "error: cannot write %s\n", target);
        free(target);
        package_free(pkg);
        return 2;
    }
    log_msg("done (%.1f MB) -> %s", (pack_at + pack_len) / 1048576.0, pkg ? package_summary(pkg) : target);
    package_free(pkg);
    *target_out = target;
    return failed ? 1 : 0;
}

static const Target* choose_target(void) {
    const Target* avail[TARGET_COUNT];
    const char* names[TARGET_COUNT];
    int n = 0;
    for (int i = 0; i < TARGET_COUNT; i++) {
        if (!package_has_engine((TargetId)i)) continue;
        avail[n] = target_get((TargetId)i);
        names[n] = avail[n]->console;
        n++;
    }
    if (n == 0) {
        ui_message(UI_TITLE, "This builder was compiled without any console engine.", 1);
        return NULL;
    }
    if (n == 1) return avail[0];
    int pick = ui_choose(UI_TITLE, "Which console do you want to play OMORI on?", names, n);
    return pick >= 0 ? avail[pick] : NULL;
}

static void gui_progress(void* ctx, double fraction, const char* text) {
    (void)ctx;
    ui_progress_update(fraction, text);
}

static void show_progress(void) {
    if (ui_progress_open(UI_TITLE, "Getting ready...")) progress_start(gui_progress, NULL);
    else progress_terminal();
}

static void hide_progress(void) {
    progress_stop();
    ui_progress_close();
}

static int run_interactive(Options* o) {
    ui_use_native(ui_native_available());
    set_fatal_hook(fatal_dialog);
    if (!o->target) o->target = choose_target();
    if (!o->target) return 3;
    use_target(o->target);
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
    char* out = ui_pick_folder(UI_TITLE, package_out_prompt());
    if (!out) {
        game_close(&g);
        return 3;
    }
    char* target = NULL;
    show_progress();
    int rc = build(&g, out, o, &target);
    hide_progress();
    if (target) {
        char* m = package_done_message(out);
        ui_message(UI_TITLE, m, 0);
        free(m);
    } else {
        ui_message(UI_TITLE, "The build failed. Run the builder from a terminal to see the details.", 1);
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
    Options o = {NULL, NULL, NULL, NULL, cpu_count(), 0, 0, NULL, NULL};
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if ((!strcmp(a, "-o") || !strcmp(a, "--out")) && i + 1 < argc) out = argv[++i];
        else if ((!strcmp(a, "-t") || !strcmp(a, "--target")) && i + 1 < argc) {
            o.target = target_find(argv[++i]);
            if (!o.target) {
                char names[160];
                target_names(names, sizeof(names));
                fprintf(stderr, "error: unknown target %s (use %s)\n", argv[i], names);
                return 2;
            }
        }
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
    if (update) return package_update_engine(update, o.engine);
    if (!game_dir) return run_interactive(&o);
    use_target(o.target ? o.target : target_get(TARGET_PSP));
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
    progress_terminal();
    int rc = build(&g, out, &o, &target);
    progress_stop();
    free(target);
    game_close(&g);
    sl_free(&g_errors);
    sl_free(&g_script_errors);
    return rc;
}
