#ifndef OMORI_BUF_H
#define OMORI_BUF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t* data;
    size_t len;
    size_t cap;
} Buf;

void* xmalloc(size_t n);
void* xcalloc(size_t n, size_t size);
void* xrealloc(void* p, size_t n);
char* xstrdup(const char* s);
char* xstrndup(const char* s, size_t n);
char* str_fmt(const char* fmt, ...);

void buf_reserve(Buf* b, size_t extra);
void buf_put(Buf* b, const void* p, size_t n);
void buf_u8(Buf* b, uint8_t v);
void buf_u16(Buf* b, uint16_t v);
void buf_u32(Buf* b, uint32_t v);
void buf_zero(Buf* b, size_t n);
void buf_free(Buf* b);

#ifdef __cplusplus
}
#endif

#endif
