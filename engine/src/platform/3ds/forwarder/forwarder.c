#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define TARGET "/3ds/omori/omori.3dsx"
#define RESTORE "omori-forwarder:restore="

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
    u64 self = 0;
    Result r = APT_GetProgramID(&self);
    if (R_FAILED(r)) return r;
    Handle port;
    r = svcConnectToPort(&port, "hb:ldr");
    if (R_FAILED(r)) return r;
    r = hbldr_send(port, 2, 0, g_target, sizeof(g_target));
    if (R_SUCCEEDED(r)) {
        char* p = (char*)&g_args[1];
        g_args[0] = 2;
        p += sprintf(p, "sdmc:%s", TARGET) + 1;
        sprintf(p, RESTORE "%016llX", (unsigned long long)LUMA->selected);
        r = hbldr_send(port, 3, 1, g_args, sizeof(g_args));
    }
    svcCloseHandle(port);
    if (R_FAILED(r)) return r;
    LUMA->selected = self;
    aptSetChainloader(self, MEDIATYPE_SD);
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
