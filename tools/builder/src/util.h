#ifndef TB_UTIL_H
#define TB_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "buf.h"

FILE* file_open(const char* path, const char* mode);
int file_seek(FILE* f, uint64_t at);
int file_length(FILE* f, uint64_t* len);
int file_replace(const char* from, const char* to);
int file_read(const char* path, Buf* out);
int file_write(const char* path, const void* data, size_t len);
int file_exists(const char* path);
int dir_exists(const char* path);
int make_dirs(const char* path);
char** dir_list(const char* path, int* count);
void str_list_free(char** list, int count);
char* path_join(const char* a, const char* b);

void log_msg(const char* fmt, ...);
void fatal(const char* fmt, ...);
void set_fatal_hook(void (*fn)(const char* msg));

typedef struct Mutex Mutex;
Mutex* mutex_new(void);
void mutex_lock(Mutex* m);
void mutex_unlock(Mutex* m);
void mutex_free(Mutex* m);

typedef void (*JobFn)(void* ctx, int index);
void run_jobs(JobFn fn, void* ctx, int count, int threads);
int cpu_count(void);
void sleep_ms(int ms);

#ifdef _WIN32
#include <wchar.h>
wchar_t* utf8_to_wide(const char* s);
char* wide_to_utf8(const wchar_t* w);
#endif

#endif
