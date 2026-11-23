#ifndef TB_BANNER_H
#define TB_BANNER_H

#include "buf.h"
#include "game.h"
#include "node.h"

int banner_build(Game* g, Node* atlas, Buf* out);
int lz11_compress(const uint8_t* in, size_t len, Buf* out);

#endif
