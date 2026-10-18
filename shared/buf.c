#include "buf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void* xmalloc(size_t n) {
    void* p = malloc(n ? n : 1);
    if (!p) abort();
    return p;
}

void* xcalloc(size_t n, size_t size) {
    void* p = calloc(n ? n : 1, size ? size : 1);
    if (!p) abort();
    return p;
}

void* xrealloc(void* p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) abort();
    return p;
}

char* xstrdup(const char* s) { return xstrndup(s, strlen(s)); }

char* xstrndup(const char* s, size_t n) {
    char* d = (char*)xmalloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

char* str_fmt(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char* s = (char*)xmalloc((size_t)n + 1);
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

void buf_reserve(Buf* b, size_t extra) {
    if (b->len + extra <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra) cap *= 2;
    b->data = (uint8_t*)xrealloc(b->data, cap);
    b->cap = cap;
}

void buf_put(Buf* b, const void* p, size_t n) {
    if (!n) return;
    buf_reserve(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

void buf_u8(Buf* b, uint8_t v) { buf_put(b, &v, 1); }

void buf_u16(Buf* b, uint16_t v) {
    uint8_t t[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    buf_put(b, t, 2);
}

void buf_u32(Buf* b, uint32_t v) {
    uint8_t t[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    buf_put(b, t, 4);
}

void buf_zero(Buf* b, size_t n) {
    buf_reserve(b, n);
    memset(b->data + b->len, 0, n);
    b->len += n;
}

void buf_free(Buf* b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}
