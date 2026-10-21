#ifndef TB_TARGET_H
#define TB_TARGET_H

typedef enum { TARGET_PSP, TARGET_VITA, TARGET_3DS, TARGET_WII, TARGET_WIIU, TARGET_PS3, TARGET_PS2, TARGET_XENON, TARGET_COUNT } TargetId;

typedef enum { TEX_LINEAR, TEX_PSP, TEX_PICA, TEX_GX, TEX_ARGB, TEX_GS } TexLayout;

typedef struct {
    TargetId id;
    const char* name;
    const char* console;
    double tex_scale;
    int max_tex;
    TexLayout layout;
    int movie_max_w;
    int movie_max_h;
} Target;

extern const Target* g_target;

const Target* target_get(TargetId id);
const Target* target_find(const char* name);
void target_set(const Target* t);

#endif
