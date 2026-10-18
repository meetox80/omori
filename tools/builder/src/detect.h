#ifndef TB_DETECT_H
#define TB_DETECT_H

#include <stddef.h>

#define STEAM_APP_ID "1150690"
#define STEAM_STORE_URL "https://store.steampowered.com/app/1150690/OMORI/"

char* steam_find_game(void);
char* install_dir_of(const char* game_root);
int crack_scan(const char* install_dir, char* reason, size_t reason_len);

#endif
