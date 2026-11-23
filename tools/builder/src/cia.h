#ifndef TB_CIA_H
#define TB_CIA_H

#include <stddef.h>
#include <stdint.h>

#include "buf.h"

typedef struct {
    const uint8_t* elf;
    size_t elf_len;
    const uint8_t* smdh;
    size_t smdh_len;
    const uint8_t* banner;
    size_t banner_len;
    uint64_t title_id;
    const char* name;
    const char* product_code;
} CiaApp;

int cia_build(const CiaApp* app, Buf* out);

#endif
