#include "jpeg_soft.h"

#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_THREAD_LOCALS
#define STB_IMAGE_IMPLEMENTATION
#include "../../../third_party/stb_image.h"

bool jpeg_soft_decode(const uint8_t* jpeg, size_t len, uint32_t* dst, int stride, int rows) {
    int w = 0, h = 0, n = 0;
    stbi_uc* px = stbi_load_from_memory(jpeg, (int)len, &w, &h, &n, 3);
    if (!px) return false;
    int cw = w < stride ? w : stride;
    int ch = h < rows ? h : rows;
    for (int y = 0; y < ch; y++) {
        const stbi_uc* s = px + (size_t)y * w * 3;
        uint32_t* d = dst + (size_t)y * stride;
        for (int x = 0; x < cw; x++) d[x] = (uint32_t)s[x * 3] | ((uint32_t)s[x * 3 + 1] << 8) | ((uint32_t)s[x * 3 + 2] << 16) | 0xFF000000u;
    }
    stbi_image_free(px);
    return true;
}
