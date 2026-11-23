#ifndef TB_SND0_H
#define TB_SND0_H

#include "game.h"

int snd0_build(Game* g, Buf* out);
float* title_music_pcm(Game* g, uint32_t rate, size_t limit, size_t* count);

#endif
