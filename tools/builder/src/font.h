#ifndef TB_FONT_H
#define TB_FONT_H

#include <stddef.h>
#include <stdint.h>

#include "util.h"

int font_build(const uint8_t* ttf, size_t ttf_len, int logical_size, int outline, const uint32_t* codepoints, int count, Buf* tex, Buf* fnt);

#endif
