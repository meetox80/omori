#ifndef TB_MAPS_H
#define TB_MAPS_H

#include "game.h"
#include "util.h"

void maps_init(void);
void maps_cleanup(void);
int maps_build_one(Game* g, int id, Buf* bin, Buf* tex, char** err);

#endif
