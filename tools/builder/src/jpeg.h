#ifndef TB_JPEG_H
#define TB_JPEG_H

#include <stdint.h>

#include "buf.h"

int jpeg_encode_rgb(const uint8_t* rgb, int w, int h, int quality, Buf* out);

#endif
