#ifndef TB_VPK_H
#define TB_VPK_H

#include <stddef.h>
#include <stdint.h>

#include "util.h"

typedef struct {
    const uint8_t* eboot;
    size_t eboot_len;
    const uint8_t* sfo;
    size_t sfo_len;
    Buf icon0;
    Buf bg;
    Buf startup;
} Vpk;

int vpk_write(const char* path, const Vpk* v);
int vpk_replace_eboot(const char* path, const uint8_t* eboot, size_t len);
int vpk_is_vpk(const char* path);

#endif
