#ifndef TB_GAME_H
#define TB_GAME_H

#include "crypto.h"
#include "node.h"
#include "util.h"

typedef struct {
    char* root;
    char* install;
    char* version;
    char key[33];
    Aes256 aes;
    uint8_t mv_key[16];
    int has_mv_key;
    char** files;
    char** lower;
    int* by_lower;
    int nfiles;
} Game;

int game_open(Game* g, const char* path, const char* key);
void game_close(Game* g);
char* game_resolve(Game* g, const char* path);
int game_read_stored(Game* g, const char* stored, Buf* out);
int game_read(Game* g, const char* path, Buf* out);
int game_exists(Game* g, const char* path);
Node* game_json(Game* g, const char* path);
Node* game_json_opt(Game* g, const char* path);
Node* game_plugins(Game* g);
char** game_stored_files(Game* g, const char* folder, int* count);
char* game_logical(const char* stored);
char* game_abs(Game* g, const char* rel);

#endif
