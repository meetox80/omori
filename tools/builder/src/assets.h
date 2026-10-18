#ifndef TB_ASSETS_H
#define TB_ASSETS_H

#include "game.h"
#include "node.h"
#include "pack.h"

char* asset_key(const char* logical, const char* ext);
int assets_build_images(Game* g, Node* atlas, Pack* pack, int jobs);
int assets_build_audio(Game* g, Pack* pack);
void assets_build_fonts(Game* g, Node* db, Node* lang, Pack* pack, int jobs);
void assets_build_icons(Game* g, Node* atlas, Buf* icon0, Buf* pic1);

#endif
