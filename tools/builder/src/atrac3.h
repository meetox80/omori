#ifndef TB_ATRAC3_H
#define TB_ATRAC3_H

#include <stddef.h>
#include <stdint.h>

#include "buf.h"

#define ATRAC3_DELAY 1162

int atrac3_encode(const float* pcm, size_t frames, uint32_t rate, int32_t loop_start, int32_t loop_end, Buf* out);

#endif
