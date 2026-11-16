#ifndef TB_FONT_H
#define TB_FONT_H

#include <stddef.h>
#include <stdint.h>

#include "util.h"

typedef struct FontSet FontSet;

FontSet* fontset_new(const uint8_t* primary, size_t len);
void fontset_add(FontSet* fs, const uint8_t* data, size_t len);
void fontset_add_cjk(FontSet* fs);
void fontset_resolve(FontSet* fs, const uint32_t* cps, const uint32_t* freq, int count);
void fontset_free(FontSet* fs);
int font_build(const FontSet* fs, int logical_size, int outline, Buf* tex, Buf* fnt);

#endif
