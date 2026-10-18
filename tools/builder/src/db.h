#ifndef TB_DB_H
#define TB_DB_H

#include "game.h"
#include "node.h"
#include "pack.h"

Node* db_build(Game* g, Pack* pack, Node** lang_out);

#endif
