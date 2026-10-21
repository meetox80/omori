#ifndef TB_WUHB_H
#define TB_WUHB_H

#include <stddef.h>
#include <stdint.h>

#include "image.h"
#include "util.h"

typedef struct {
    const uint8_t* rpx;
    size_t rpx_len;
    Buf icon;
    Buf tv;
    Buf drc;
} Wuhb;

int wuhb_tga_gz(const Image* img, int alpha, Buf* out);
int wuhb_write(const char* path, const Wuhb* w);
int wuhb_is_wuhb(const uint8_t magic[4]);
int wuhb_replace_rpx(const char* path, const uint8_t* rpx, size_t len);

#endif
