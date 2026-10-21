#include "target.h"

#include <string.h>

static const Target TARGETS[TARGET_COUNT] = {
    {TARGET_PSP, "psp", "PSP", 0.5625, 512, TEX_PSP, 480, 272},
    {TARGET_VITA, "vita", "PS Vita", 1.0, 4096, TEX_LINEAR, 640, 480},
    {TARGET_3DS, "3ds", "3DS", 0.5, 512, TEX_PICA, 400, 240},
    {TARGET_WII, "wii", "Wii", 1.0, 1024, TEX_GX, 640, 480},
    {TARGET_WIIU, "wiiu", "Wii U", 1.0, 4096, TEX_LINEAR, 640, 480},
    {TARGET_PS3, "ps3", "PS3", 1.0, 4096, TEX_ARGB, 640, 480},
    {TARGET_PS2, "ps2", "PS2", 0.5, 512, TEX_GS, 320, 240},
    {TARGET_XENON, "xbox360", "Xbox 360", 1.0, 4096, TEX_ARGB, 640, 480},
};

const Target* g_target = &TARGETS[TARGET_PSP];

const Target* target_get(TargetId id) { return id >= 0 && id < TARGET_COUNT ? &TARGETS[id] : NULL; }

const Target* target_find(const char* name) {
    for (int i = 0; i < TARGET_COUNT; i++) {
        if (!strcmp(TARGETS[i].name, name)) return &TARGETS[i];
    }
    return NULL;
}

void target_set(const Target* t) { g_target = t; }
