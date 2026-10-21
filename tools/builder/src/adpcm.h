#ifndef TB_ADPCM_H
#define TB_ADPCM_H

#include <stddef.h>
#include <stdint.h>

#include "buf.h"

#define ADPCM_BLOCK_FRAMES 30
#define ADPCM_BLOCK_BYTES 32
#define ADPCM_SEEK_BLOCKS 128
#define ADPCM_HEADER_BYTES 32

int adpcm_encode(const int16_t* pcm, uint32_t frames, int channels, uint32_t rate, int32_t loop_start, int32_t loop_length, Buf* out);
int adpcm_from_ogg(const uint8_t* ogg, size_t len, Buf* out, char* err, size_t errlen);
void adpcm_decode_block(const uint8_t* block, int32_t* h1, int32_t* h2, int16_t* out, int stride);

#endif
