#ifndef TB_PACK_H
#define TB_PACK_H

#include <stdio.h>

#include "util.h"

typedef struct {
    char* name;
    Buf data;
    char* error;
    uint32_t offset;
    uint32_t size;
    int ready;
} PackEntry;

typedef struct {
    PackEntry* items;
    int count;
    int cap;
    FILE* sink;
    uint64_t base;
    uint64_t pos;
    int next;
    int failed;
    Mutex* lock;
} Pack;

PackEntry* pack_add(Pack* p, const char* name);
int pack_open(Pack* p, FILE* sink);
void pack_ready(Pack* p, PackEntry* e);
void pack_flush(Pack* p);
int pack_finish(Pack* p, uint64_t* total);
void pack_free(Pack* p);

typedef struct {
    Buf part[8];
} Pbp;

int pbp_read(const uint8_t* data, size_t len, Pbp* out);
int pbp_write_head(const Pbp* p, FILE* f);
void pbp_free(Pbp* p);
void sfo_build(const char* title, Buf* out);

#endif
