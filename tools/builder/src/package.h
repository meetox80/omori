#ifndef TB_PACKAGE_H
#define TB_PACKAGE_H

#include <stdint.h>
#include <stdio.h>

#include "game.h"
#include "node.h"
#include "pack.h"
#include "target.h"

typedef struct Package Package;

int package_has_engine(TargetId id);
Package* package_open(const char* out, const char* engine_path, Game* g, Node* atlas);
const char* package_data_path(const Package* p);
void package_add_entries(Package* p, Pack* pack);
int package_write_head(Package* p, FILE* f, uint64_t* pack_at);
int package_write_tail(Package* p, FILE* f, uint64_t pack_at, uint64_t pack_len);
int package_finish(Package* p);
const char* package_summary(const Package* p);
void package_free(Package* p);

const char* package_write_text(void);
const char* package_finish_text(void);
int package_update_engine(const char* file, const char* engine_path);
const char* package_out_prompt(void);
char* package_done_message(const char* out);

#endif
