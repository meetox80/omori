#ifndef TB_PS3PKG_H
#define TB_PS3PKG_H

#include <stdint.h>
#include <stdio.h>

#include "util.h"

typedef struct {
    const char* content_id;
    Buf icon0;
    Buf sfo;
    Buf pic1;
    Buf eboot;
    Buf plain;
    uint64_t pack_at;
} Ps3Pkg;

int ps3pkg_write_head(Ps3Pkg* p, FILE* f, uint64_t* pack_at);
int ps3pkg_write_tail(FILE* f, uint64_t pack_len);
int ps3pkg_finish(Ps3Pkg* p, const char* path, uint64_t pack_len, int threads);
int ps3pkg_replace_eboot(const char* path, const uint8_t* eboot, size_t len, int threads);
int ps3pkg_is_pkg(const uint8_t magic[4]);
void ps3pkg_free(Ps3Pkg* p);

#endif
