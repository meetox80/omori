#ifndef TB_ASSETS_H
#define TB_ASSETS_H

#include "game.h"
#include "node.h"
#include "pack.h"

char* asset_key(const char* logical, const char* ext);
int assets_build_images(Game* g, Node* atlas, Pack* pack, int jobs);
int assets_build_audio(Game* g, Pack* pack, int jobs);
void assets_build_fonts(Game* g, Node* db, Node* lang, Pack* pack, int jobs);
void assets_build_icons(Game* g, Node* atlas, Buf* icon0, Buf* pic1);
int assets_build_vita_art(Game* g, Node* atlas, Buf* icon0, Buf* bg, Buf* startup);
int assets_build_smdh(Game* g, Node* atlas, Buf* out);
int assets_build_hbc_icon(Game* g, Node* atlas, Buf* png);
int assets_build_wiiu_art(Game* g, Node* atlas, Buf* icon, Buf* tv, Buf* drc);
int assets_build_ps3_art(Game* g, Node* atlas, Buf* icon0, Buf* pic1);
int assets_build_ps2_icon(Game* g, Node* atlas, Buf* sys, Buf* icn);

#endif
