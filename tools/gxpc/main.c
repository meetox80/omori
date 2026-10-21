#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/shacccg.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROOT "ux0:data/gxpc/"

static FILE* g_log;

static void say(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fflush(g_log);
}

static char* slurp(const char* path, unsigned* len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* d = malloc((size_t)n + 1);
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        d = NULL;
    }
    fclose(f);
    if (d) {
        d[n] = 0;
        *len = (unsigned)n;
    }
    return d;
}

static SceShaccCgSourceFile g_source;

static SceShaccCgSourceFile* open_file(const char* name, const SceShaccCgSourceLocation* from, const SceShaccCgCompileOptions* opts, const char** err) {
    (void)from;
    (void)opts;
    if (strcmp(name, g_source.fileName) == 0) return &g_source;
    *err = "include not supported";
    return NULL;
}

static void release_file(const SceShaccCgSourceFile* file, const SceShaccCgCompileOptions* opts) {
    (void)file;
    (void)opts;
}

static void* cg_alloc(unsigned int n) { return malloc(n); }

static int compile(const char* out, const char* src, const char* profile, int ndefs, const char** defs) {
    char path[256];
    snprintf(path, sizeof path, ROOT "in/%s", src);
    unsigned len = 0;
    char* text = slurp(path, &len);
    if (!text) {
        say("%s: cannot read %s\n", out, path);
        return 0;
    }
    g_source.fileName = src;
    g_source.text = text;
    g_source.size = len;
    SceShaccCgCompileOptions o;
    sceShaccCgInitializeCompileOptions(&o);
    o.mainSourceFile = src;
    o.targetProfile = strcmp(profile, "vp") == 0 ? SCE_SHACCCG_PROFILE_VP : SCE_SHACCCG_PROFILE_FP;
    o.entryFunctionName = "main";
    o.macroDefinitionCount = (SceUInt32)ndefs;
    o.macroDefinitions = defs;
    o.optimizationLevel = 3;
    o.useFastmath = 1;
    o.useFastprecision = 0;
    o.useFastint = 1;
    o.warningLevel = 3;
    SceShaccCgCallbackList cb;
    sceShaccCgInitializeCallbackList(&cb, SCE_SHACCCG_TRIVIAL);
    cb.openFile = open_file;
    cb.releaseFile = release_file;
    const SceShaccCgCompileOutput* r = sceShaccCgCompileProgram(&o, &cb, 0);
    int ok = 0;
    if (r) {
        for (int i = 0; i < r->diagnosticCount; i++) {
            const SceShaccCgDiagnosticMessage* d = &r->diagnostics[i];
            unsigned line = d->location ? d->location->lineNumber : 0;
            say("%s: %s:%u: %s\n", out, src, line, d->message);
        }
        if (r->programData && r->programSize) {
            snprintf(path, sizeof path, ROOT "out/%s.gxp", out);
            FILE* f = fopen(path, "wb");
            if (f) {
                ok = fwrite(r->programData, 1, r->programSize, f) == r->programSize;
                fclose(f);
            }
        }
        sceShaccCgDestroyCompileOutput(r);
    }
    say("%s: %s\n", out, ok ? "ok" : "failed");
    free(text);
    return ok;
}

int main(void) {
    g_log = fopen(ROOT "log.txt", "w");
    if (!g_log) sceKernelExitProcess(1);
    int status = 0;
    SceUID mod = sceKernelLoadStartModule("ur0:data/libshacccg.suprx", 0, NULL, 0, NULL, &status);
    if (mod < 0) {
        say("cannot load libshacccg.suprx: 0x%08X\n", (unsigned)mod);
        fclose(g_log);
        sceKernelExitProcess(1);
    }
    sceShaccCgSetDefaultAllocator(cg_alloc, free);
    say("compiler %s\n", sceShaccCgGetVersionString());
    unsigned len = 0;
    char* jobs = slurp(ROOT "jobs.txt", &len);
    int failed = 0, total = 0;
    if (!jobs) {
        say("cannot read jobs.txt\n");
        failed = 1;
    }
    for (char* line = jobs ? strtok(jobs, "\n") : NULL; line; line = strtok(NULL, "\n")) {
        char* words[32];
        int n = 0;
        for (char* p = line; *p && n < 32;) {
            while (*p == ' ' || *p == '\r') *p++ = 0;
            if (!*p) break;
            words[n++] = p;
            while (*p && *p != ' ' && *p != '\r') p++;
        }
        if (n < 3) continue;
        total++;
        if (!compile(words[0], words[1], words[2], n - 3, (const char**)&words[3])) failed++;
    }
    free(jobs);
    sceShaccCgReleaseCompiler();
    say("done %d/%d\n", total - failed, total);
    fclose(g_log);
    FILE* f = fopen(ROOT "done", "w");
    if (f) fclose(f);
    sceKernelExitProcess(failed ? 1 : 0);
    return 0;
}
