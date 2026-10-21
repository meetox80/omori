#include "progress.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "target.h"
#include "util.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

typedef struct {
    const char* key;
    const char* text;
    double weight[TARGET_COUNT];
} Phase;

static const Phase PHASES[] = {
    {"database", "Reading the game's data", {11, 11, 11, 11, 11, 11, 11, 11}},
    {"maps", "Converting maps and events", {3, 3, 3, 3, 3, 3, 3, 3}},
    {"images", NULL, {13, 15, 12, 15, 15, 15, 12, 15}},
    {"audio", "Copying music and sounds", {1, 1, 1, 1, 1, 1, 14, 1}},
    {"movies", "Converting the movies", {63, 43, 60, 43, 43, 43, 60, 43}},
    {"fonts", "Rendering the fonts", {7, 26, 6, 26, 26, 26, 6, 26}},
    {"write", NULL, {1, 0.5, 1, 0.5, 0.5, 0.5, 1, 0.5}},
    {"finish", NULL, {0, 0.5, 0, 0.5, 0.5, 6, 0, 0.5}},
};

#define PHASE_COUNT ((int)(sizeof(PHASES) / sizeof(PHASES[0])))

static Mutex* g_lock;
static ProgressSink g_sink;
static void* g_ctx;
static int g_phase = -1;
static char g_phase_text[128];
static double g_sub;
static int g_done, g_total;
static double g_started;
static int g_last_permille = -1;
static char g_last_text[256];
static int g_terminal;
static size_t g_bar_len;

static double now_seconds(void) {
#ifdef _WIN32
    return (double)GetTickCount64() / 1000.0;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + ts.tv_nsec / 1e9;
#endif
}

static double weight_of(const Phase* p) { return p->weight[g_target->id]; }

static double overall(void) {
    double total = 0, before = 0, weight = 0;
    for (int i = 0; i < PHASE_COUNT; i++) {
        double w = weight_of(&PHASES[i]);
        if (i < g_phase) before += w;
        if (i == g_phase) weight = w;
        total += w;
    }
    if (g_phase < 0 || total <= 0) return 0;
    double f = (before + weight * g_sub) / total;
    return f > 1 ? 1 : f;
}

static void time_left(double fraction, char* out, size_t cap) {
    double elapsed = now_seconds() - g_started;
    out[0] = 0;
    if (fraction < 0.04 || elapsed < 8) return;
    double seconds = elapsed * (1 - fraction) / fraction;
    if (seconds < 50) {
        snprintf(out, cap, ", less than a minute left");
        return;
    }
    int minutes = (int)(seconds / 60 + 0.5);
    if (minutes == 1) snprintf(out, cap, ", about a minute left");
    else snprintf(out, cap, ", about %d minutes left", minutes);
}

static void clear_bar(void) {
    if (!g_bar_len) return;
    printf("\r%*s\r", (int)g_bar_len, "");
    g_bar_len = 0;
}

static void draw_bar(double fraction, const char* text) {
    char bar[31];
    int filled = (int)(fraction * 30 + 0.5);
    for (int i = 0; i < 30; i++) bar[i] = i < filled ? '#' : '-';
    bar[30] = 0;
    char line[400];
    int n = snprintf(line, sizeof(line), "[%s] %3d%%  %s", bar, (int)(fraction * 100), text);
    if (n < 0) return;
    printf("\r%s", line);
    if ((size_t)n < g_bar_len) printf("%*s", (int)(g_bar_len - (size_t)n), "");
    g_bar_len = (size_t)n;
    fflush(stdout);
}

static void terminal_sink(void* ctx, double fraction, const char* text) {
    (void)ctx;
    draw_bar(fraction, text);
}

static void terminal_log(const char* line) {
    if (!strncmp(line, "  progress ", 11) || !strncmp(line, "  frames ", 9)) return;
    mutex_lock(g_lock);
    clear_bar();
    printf("%s\n", line);
    if (g_last_text[0]) draw_bar(overall(), g_last_text);
    fflush(stdout);
    mutex_unlock(g_lock);
}

static void report(int force) {
    if (!g_sink || g_phase < 0) return;
    double f = overall();
    const char* name = g_phase_text;
    char left[64], text[256];
    time_left(f, left, sizeof(left));
    if (g_total > 0) snprintf(text, sizeof(text), "%s: %d of %d%s", name, g_done, g_total, left);
    else snprintf(text, sizeof(text), "%s%s", name, left);
    int permille = (int)(f * 1000);
    if (!force && permille == g_last_permille && !strcmp(text, g_last_text)) return;
    g_last_permille = permille;
    snprintf(g_last_text, sizeof(g_last_text), "%s", text);
    g_sink(g_ctx, f, text);
}

void progress_start(ProgressSink sink, void* ctx) {
    if (!g_lock) g_lock = mutex_new();
    mutex_lock(g_lock);
    g_sink = sink;
    g_ctx = ctx;
    g_phase = -1;
    g_sub = 0;
    g_done = g_total = 0;
    g_started = now_seconds();
    g_last_permille = -1;
    g_last_text[0] = 0;
    mutex_unlock(g_lock);
}

void progress_terminal(void) {
#ifdef _WIN32
    int tty = _isatty(_fileno(stdout));
#else
    int tty = isatty(1);
#endif
    if (!tty) return;
    progress_start(terminal_sink, NULL);
    g_terminal = 1;
    set_log_hook(terminal_log);
}

int progress_active(void) { return g_sink != NULL; }

void progress_phase(const char* key, const char* text) {
    if (!g_sink) return;
    mutex_lock(g_lock);
    for (int i = 0; i < PHASE_COUNT; i++) {
        if (!strcmp(PHASES[i].key, key)) {
            g_phase = i;
            g_sub = 0;
            g_done = g_total = 0;
            if (text) snprintf(g_phase_text, sizeof(g_phase_text), "%s", text);
            else if (PHASES[i].text) snprintf(g_phase_text, sizeof(g_phase_text), "%s", PHASES[i].text);
            else if (g_target->tex_scale < 1) snprintf(g_phase_text, sizeof(g_phase_text), "Scaling the artwork for the %s screen", g_target->console);
            else snprintf(g_phase_text, sizeof(g_phase_text), "Converting the artwork");
            break;
        }
    }
    report(1);
    mutex_unlock(g_lock);
}

void progress_items(int done, int total) {
    if (!g_sink || total <= 0) return;
    mutex_lock(g_lock);
    double sub = (double)done / total;
    if (sub > g_sub) g_sub = sub;
    if (done > g_done || total != g_total) {
        g_done = done;
        g_total = total;
    }
    report(0);
    mutex_unlock(g_lock);
}

void progress_fraction(double sub) {
    if (!g_sink) return;
    mutex_lock(g_lock);
    if (sub > g_sub) g_sub = sub > 1 ? 1 : sub;
    report(0);
    mutex_unlock(g_lock);
}

void progress_stop(void) {
    if (!g_sink) return;
    mutex_lock(g_lock);
    if (g_terminal) {
        clear_bar();
        fflush(stdout);
        set_log_hook(NULL);
        g_terminal = 0;
    }
    g_sink = NULL;
    g_last_text[0] = 0;
    mutex_unlock(g_lock);
}
