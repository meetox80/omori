#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#include <wchar.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#ifndef __wasi__
#include <pthread.h>
#define TB_THREADS 1
#endif
#endif

#ifdef _WIN32
wchar_t* utf8_to_wide(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t* w = xmalloc(sizeof(wchar_t) * (size_t)(n > 0 ? n : 1));
    if (n <= 0) w[0] = 0;
    else MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

char* wide_to_utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char* s = xmalloc((size_t)(n > 0 ? n : 1));
    if (n <= 0) s[0] = 0;
    else WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}
#endif

int file_length(FILE* f, uint64_t* len) {
#ifdef _WIN32
    if (_fseeki64(f, 0, SEEK_END) != 0) return 0;
    __int64 n = _ftelli64(f);
#else
    if (fseeko(f, 0, SEEK_END) != 0) return 0;
    off_t n = ftello(f);
#endif
    if (n < 0) return 0;
    *len = (uint64_t)n;
    return 1;
}

int file_replace(const char* from, const char* to) {
#ifdef _WIN32
    wchar_t* wf = utf8_to_wide(from);
    wchar_t* wt = utf8_to_wide(to);
    int ok = MoveFileExW(wf, wt, MOVEFILE_REPLACE_EXISTING) != 0;
    free(wf);
    free(wt);
    return ok;
#else
    return rename(from, to) == 0;
#endif
}

int file_seek(FILE* f, uint64_t at) {
#ifdef _WIN32
    return _fseeki64(f, (__int64)at, SEEK_SET) == 0;
#else
    return fseeko(f, (off_t)at, SEEK_SET) == 0;
#endif
}

FILE* file_open(const char* path, const char* mode) {
#ifdef _WIN32
    wchar_t* wp = utf8_to_wide(path);
    wchar_t* wm = utf8_to_wide(mode);
    FILE* f = _wfopen(wp, wm);
    free(wp);
    free(wm);
    return f;
#else
    return fopen(path, mode);
#endif
}

static int path_stat(const char* path, int* is_dir) {
#ifdef _WIN32
    wchar_t* wp = utf8_to_wide(path);
    DWORD a = GetFileAttributesW(wp);
    free(wp);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;
    *is_dir = (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return 1;
#else
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    *is_dir = S_ISDIR(st.st_mode);
    return 1;
#endif
}



int file_read(const char* path, Buf* out) {
    FILE* f = file_open(path, "rb");
    if (!f) return 0;
    out->len = 0;
    uint8_t tmp[65536];
    size_t n;
    while ((n = fread(tmp, 1, sizeof(tmp), f)) > 0) buf_put(out, tmp, n);
    int ok = !ferror(f);
    fclose(f);
    return ok;
}

int file_write(const char* path, const void* data, size_t len) {
    char* tmp = str_fmt("%s.tmp", path);
    FILE* f = file_open(tmp, "wb");
    if (!f) {
        free(tmp);
        return 0;
    }
    int ok = fwrite(data, 1, len, f) == len;
    ok = (fclose(f) == 0) && ok;
#ifdef _WIN32
    wchar_t* wt = utf8_to_wide(tmp);
    wchar_t* wd = utf8_to_wide(path);
    if (ok) ok = MoveFileExW(wt, wd, MOVEFILE_REPLACE_EXISTING) != 0;
    if (!ok) DeleteFileW(wt);
    free(wt);
    free(wd);
#else
    if (ok) ok = rename(tmp, path) == 0;
    if (!ok) remove(tmp);
#endif
    free(tmp);
    return ok;
}

int file_exists(const char* path) {
    int d;
    return path_stat(path, &d) && !d;
}

int dir_exists(const char* path) {
    int d;
    return path_stat(path, &d) && d;
}

static int make_one(const char* p) {
#ifdef _WIN32
    wchar_t* w = utf8_to_wide(p);
    int r = _wmkdir(w);
    free(w);
    return r;
#else
    return mkdir(p, 0755);
#endif
}

int make_dirs(const char* path) {
    char* p = xstrdup(path);
    size_t n = strlen(p);
    for (size_t i = 1; i <= n; i++) {
        if (p[i] == '/' || p[i] == '\\' || p[i] == 0) {
            char c = p[i];
            p[i] = 0;
            if (!dir_exists(p) && make_one(p) != 0 && errno != EEXIST) {
                free(p);
                return 0;
            }
            p[i] = c;
        }
    }
    free(p);
    return 1;
}

static int cmp_str(const void* a, const void* b) { return strcmp(*(const char* const*)a, *(const char* const*)b); }

char** dir_list(const char* path, int* count) {
    char** out = NULL;
    int n = 0, cap = 0;
#ifdef _WIN32
    char* pat = str_fmt("%s\\*", path);
    wchar_t* wpat = utf8_to_wide(pat);
    free(pat);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(wpat, &fd);
    free(wpat);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 64;
                out = xrealloc(out, sizeof(char*) * (size_t)cap);
            }
            out[n++] = wide_to_utf8(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
#else
    DIR* d = opendir(path);
    if (d) {
        struct dirent* e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 64;
                out = xrealloc(out, sizeof(char*) * (size_t)cap);
            }
            out[n++] = xstrdup(e->d_name);
        }
        closedir(d);
    }
#endif
    if (n) qsort(out, (size_t)n, sizeof(char*), cmp_str);
    *count = n;
    return out;
}

void str_list_free(char** list, int count) {
    for (int i = 0; i < count; i++) free(list[i]);
    free(list);
}

char* path_join(const char* a, const char* b) {
    size_t n = strlen(a);
    if (n && (a[n - 1] == '/' || a[n - 1] == '\\')) return str_fmt("%s%s", a, b);
    return str_fmt("%s/%s", a, b);
}

void log_msg(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static void (*g_fatal_hook)(const char* msg);

void set_fatal_hook(void (*fn)(const char* msg)) { g_fatal_hook = fn; }

void fatal(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char* msg = malloc((size_t)n + 1);
    if (msg) {
        va_start(ap, fmt);
        vsnprintf(msg, (size_t)n + 1, fmt, ap);
        va_end(ap);
    }
    fprintf(stderr, "error: %s\n", msg ? msg : "out of memory");
    if (g_fatal_hook && msg) g_fatal_hook(msg);
    exit(2);
}

struct Mutex {
#ifdef _WIN32
    CRITICAL_SECTION cs;
#elif defined(TB_THREADS)
    pthread_mutex_t m;
#else
    int unused;
#endif
};

Mutex* mutex_new(void) {
    Mutex* m = xcalloc(1, sizeof(Mutex));
#ifdef _WIN32
    InitializeCriticalSection(&m->cs);
#elif defined(TB_THREADS)
    pthread_mutex_init(&m->m, NULL);
#endif
    return m;
}

void mutex_lock(Mutex* m) {
#ifdef _WIN32
    EnterCriticalSection(&m->cs);
#elif defined(TB_THREADS)
    pthread_mutex_lock(&m->m);
#else
    (void)m;
#endif
}

void mutex_unlock(Mutex* m) {
#ifdef _WIN32
    LeaveCriticalSection(&m->cs);
#elif defined(TB_THREADS)
    pthread_mutex_unlock(&m->m);
#else
    (void)m;
#endif
}

void mutex_free(Mutex* m) {
    if (!m) return;
#ifdef _WIN32
    DeleteCriticalSection(&m->cs);
#elif defined(TB_THREADS)
    pthread_mutex_destroy(&m->m);
#endif
    free(m);
}

typedef struct {
    JobFn fn;
    void* ctx;
    int count;
    int next;
#ifdef _WIN32
    CRITICAL_SECTION lock;
#elif defined(TB_THREADS)
    pthread_mutex_t lock;
#endif
} JobQueue;

static int job_take(JobQueue* q) {
#ifdef _WIN32
    EnterCriticalSection(&q->lock);
    int i = q->next < q->count ? q->next++ : -1;
    LeaveCriticalSection(&q->lock);
#else
#ifdef TB_THREADS
    pthread_mutex_lock(&q->lock);
#endif
    int i = q->next < q->count ? q->next++ : -1;
#ifdef TB_THREADS
    pthread_mutex_unlock(&q->lock);
#endif
#endif
    return i;
}

#ifdef _WIN32
static DWORD WINAPI job_thread(LPVOID arg) {
    JobQueue* q = arg;
    int i;
    while ((i = job_take(q)) >= 0) q->fn(q->ctx, i);
    return 0;
}
#elif defined(TB_THREADS)
static void* job_thread(void* arg) {
    JobQueue* q = arg;
    int i;
    while ((i = job_take(q)) >= 0) q->fn(q->ctx, i);
    return NULL;
}
#endif

void run_jobs(JobFn fn, void* ctx, int count, int threads) {
    if (threads < 1) threads = 1;
    if (threads > count) threads = count;
#if !defined(_WIN32) && !defined(TB_THREADS)
    threads = 1;
#endif
    if (threads <= 1) {
        for (int i = 0; i < count; i++) {
            fn(ctx, i);
            if (count >= 50 && ((i + 1) % 100 == 0 || i + 1 == count)) log_msg("  progress %d/%d", i + 1, count);
        }
        return;
    }
    JobQueue q;
    q.fn = fn;
    q.ctx = ctx;
    q.count = count;
    q.next = 0;
#ifdef _WIN32
    InitializeCriticalSection(&q.lock);
    HANDLE* hs = xmalloc(sizeof(HANDLE) * (size_t)threads);
    for (int i = 0; i < threads; i++) hs[i] = CreateThread(NULL, 0, job_thread, &q, 0, NULL);
    WaitForMultipleObjects((DWORD)threads, hs, TRUE, INFINITE);
    for (int i = 0; i < threads; i++) CloseHandle(hs[i]);
    free(hs);
    DeleteCriticalSection(&q.lock);
#elif defined(TB_THREADS)
    pthread_mutex_init(&q.lock, NULL);
    pthread_t* ts = xmalloc(sizeof(pthread_t) * (size_t)threads);
    for (int i = 0; i < threads; i++) pthread_create(&ts[i], NULL, job_thread, &q);
    for (int i = 0; i < threads; i++) pthread_join(ts[i], NULL);
    free(ts);
    pthread_mutex_destroy(&q.lock);
#endif
}

int cpu_count(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwNumberOfProcessors > 0 ? (int)si.dwNumberOfProcessors : 4;
#elif defined(__wasi__)
    return 1;
#else
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 4;
#endif
}

void sleep_ms(int ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}
