#ifndef TB_MOVIE_H
#define TB_MOVIE_H

#include <stddef.h>
#include <stdint.h>

#include "game.h"
#include "pack.h"

#define MJV_HEADER_SIZE 40
#define MJV_ENTRY_SIZE 12
#define MOVIE_QUALITY 75

typedef struct {
    Buf mjv;
    Buf audio;
    const char* audio_ext;
    int frames;
    int unique;
    double duration;
    double audio_seconds;
} MovieOut;

typedef void (*MovieProgress)(void* ctx, int done, int total);

int movie_convert(const uint8_t* webm, size_t len, int quality, MovieOut* out, char* err, size_t errlen);
int movie_convert_progress(const uint8_t* webm, size_t len, int quality, MovieOut* out, char* err, size_t errlen, MovieProgress progress,
                           void* progress_ctx);
void movie_out_free(MovieOut* out);
int assets_build_movies(Game* g, Pack* pack, int jobs, const char* cache);
int assets_convert_movie(Game* g, const char* name, const char* out_dir);

#endif
