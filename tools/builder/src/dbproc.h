#ifndef TB_DBPROC_H
#define TB_DBPROC_H

#include "game.h"
#include "node.h"

#ifdef __cplusplus
extern "C" {
#endif

int dbproc_run(Game* g, Node* root, Node* plugin_list, char** err);

#ifdef __cplusplus
}
#endif

#endif
