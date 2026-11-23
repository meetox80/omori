#include <3ds.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define TARGET "/3ds/omori/omori.3dsx"
#define HOMEBREW_LAUNCHER 0x000400000D921E00ULL

typedef struct {
    u64 current;
    u64 selected;
    bool enabled;
} LumaConfig;

#define LUMA ((volatile LumaConfig*)(OS_SHAREDCFG_VADDR + 0x800))

static char g_target[sizeof(TARGET)] __attribute__((aligned(4))) = TARGET;
static u32 g_args[0x100];

static Result hbldr_send(Handle port, u32 command, u32 buffer, const void* data, size_t size) {
    u32* cmd = getThreadCommandBuffer();
    cmd[0] = IPC_MakeHeader(command, 0, 2);
    cmd[1] = IPC_Desc_StaticBuffer(size, buffer);
    cmd[2] = (u32)data;
    Result r = svcSendSyncRequest(port);
    return R_SUCCEEDED(r) ? (Result)cmd[1] : r;
}

static bool title_installed(u64 tid) {
    if ((tid >> 32) != 0x00040000) return true;
    bool found = false;
    DIR* top = opendir("sdmc:/Nintendo 3DS");
    if (!top) return false;
    struct dirent* a;
    while (!found && (a = readdir(top))) {
        if (strlen(a->d_name) != 32) continue;
        char dir[288];
        snprintf(dir, sizeof(dir), "sdmc:/Nintendo 3DS/%s", a->d_name);
        DIR* mid = opendir(dir);
        if (!mid) continue;
        struct dirent* b;
        while (!found && (b = readdir(mid))) {
            if (strlen(b->d_name) != 32) continue;
            char path[640];
            struct stat st;
            snprintf(path, sizeof(path), "%s/%s/title/%08lx/%08lx/content", dir, b->d_name, (unsigned long)(tid >> 32), (unsigned long)(tid & 0xFFFFFFFF));
            found = stat(path, &st) == 0;
        }
        closedir(mid);
    }
    closedir(top);
    return found;
}

static Result forward(const char** problem) {
    struct stat st;
    if (stat("sdmc:" TARGET, &st) != 0) {
        *problem = "OMORI isn't on the SD card.\n\nPut omori.3dsx in sdmc:/3ds/omori/,\nthe folder the builder made.";
        return -1;
    }
    s64 luma = 0;
    if (R_FAILED(svcGetSystemInfo(&luma, 0x10000, 0)) || !LUMA->enabled) {
        *problem = "This needs Luma3DS to start OMORI.\n\nYou can still start omori.3dsx from\nthe Homebrew Launcher.";
        return -1;
    }
    u64 hb = LUMA->selected, self = 0;
    APT_GetProgramID(&self);
    if (!hb || hb == self) hb = HOMEBREW_LAUNCHER;
    FS_MediaType media = (hb >> 32) == 0x00040000 ? MEDIATYPE_SD : MEDIATYPE_NAND;
    if (!title_installed(hb)) {
        *problem = "This needs the Homebrew Launcher\nloader CIA installed.\n\nYou can still start omori.3dsx from\nthe Homebrew Launcher.";
        return -1;
    }
    Handle port;
    Result r = svcConnectToPort(&port, "hb:ldr");
    if (R_FAILED(r)) return r;
    r = hbldr_send(port, 2, 0, g_target, sizeof(g_target));
    if (R_SUCCEEDED(r)) {
        g_args[0] = 1;
        sprintf((char*)&g_args[1], "sdmc:%s", TARGET);
        r = hbldr_send(port, 3, 1, g_args, sizeof(g_args));
    }
    svcCloseHandle(port);
    if (R_FAILED(r)) return r;
    aptSetChainloader(hb, media);
    return 0;
}

int main(void) {
    const char* problem = NULL;
    Result r = forward(&problem);
    if (R_SUCCEEDED(r)) return 0;
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);
    if (problem) printf("\n%s\n", problem);
    else printf("\nOMORI couldn't start (error %08lX).\n\nYou can still start omori.3dsx from\nthe Homebrew Launcher.\n", (unsigned long)r);
    printf("\nPress START to go back.\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        gspWaitForVBlank();
    }
    gfxExit();
    return 0;
}
