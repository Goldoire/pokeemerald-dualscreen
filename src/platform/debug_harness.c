// Headless test harness for the native Linux build.
//
// With EMERALD_KEYFEED=<fifo path> set, a reader thread takes text commands
// from that FIFO and drives the game without a keyboard or window manager:
//
//   key <mask> <frames>     hold the GBA button mask for that many frames
//                           (A=1 B=2 SELECT=4 START=8 RIGHT=16 LEFT=32
//                            UP=64 DOWN=128 R=256 L=512; 0 releases)
//   shot <path>             write the next voxel frame as a binary PPM
//   dumpjson <path>         write the world model, mesh counts and effects as JSON
//   warp <group> <num> <x> <y>   warp the player (overworld only)
//   quit                    exit the game
//
// Debug only; compiled into NATIVE_LINUX builds and inert without the env var.
#if defined(PLATFORM_SDL2) && (defined(NATIVE_LINUX) || defined(__ANDROID__))

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ANDROID__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#include "global.h"
#include "main.h"
#include "overworld.h"
#include "palette.h"
#include "field_screen_effect.h"
#include "field_player_avatar.h"
#include "platform/dualscreen.h"
#include "voxel/voxel_renderer.h"

extern bool isRunning;

static SDL_SpinLock sWarpLock;
static bool sWarpPending = false;
static int sWarpGroup, sWarpNum, sWarpX, sWarpY;
static int sWarpCooldown = 0;

// Any thread may ask for a warp; the game thread performs it.
void DebugHarness_RequestWarp(int group, int num, int x, int y)
{
    SDL_AtomicLock(&sWarpLock);
    sWarpGroup = group; sWarpNum = num; sWarpX = x; sWarpY = y;
    sWarpPending = true;
    SDL_AtomicUnlock(&sWarpLock);
}

#ifdef NATIVE_LINUX
static int ReaderThread(void *param)
{
    const char *path = (const char *)param;
    char line[512];
    for (;;)
    {
        FILE *f = fopen(path, "r");
        if (f == NULL)
        {
            SDL_Delay(200);
            continue;
        }
        while (fgets(line, sizeof(line), f))
        {
            unsigned mask, frames, i;
            int g, n, x, y;
            char shot[400];
            if (sscanf(line, "key %u %u", &mask, &frames) == 2)
            {
                for (i = 0; i < frames; i++)
                    while (!DualScreen_DebugQueueKeys((u16)mask))
                        SDL_Delay(2);
            }
            else if (sscanf(line, "shot %399s", shot) == 1)
            {
                VoxelRenderer_RequestScreenshot(shot);
            }
            else if (sscanf(line, "dumpjson %399s", shot) == 1)
            {
                VoxelRenderer_RequestDumpJson(shot);
            }
            else if (sscanf(line, "warp %d %d %d %d", &g, &n, &x, &y) == 4)
            {
                DebugHarness_RequestWarp(g, n, x, y);
            }
            else if (sscanf(line, "flags %u", &mask) == 1)
            {
                gVoxelDebugFlags = mask;
            }
            else if (strncmp(line, "quit", 4) == 0)
            {
                isRunning = false;
            }
        }
        fclose(f);
        SDL_Delay(20);
    }
    return 0;
}

void DebugHarness_Init(void)
{
    const char *path = getenv("EMERALD_KEYFEED");
    if (path == NULL || path[0] == 0)
        return;
    printf("[Harness] reading commands from %s\n", path);
    SDL_CreateThread(ReaderThread, "KeyFeed", (void *)path);
}
#endif // NATIVE_LINUX

// Runs on the game thread once per frame (from Platform_GetKeyInput).
void DebugHarness_Tick(void)
{
    extern void CB2_Overworld(void);
    bool go = false;
    int g = 0, n = 0, x = 0, y = 0;

    if (sWarpCooldown > 0)
        sWarpCooldown--;
    SDL_AtomicLock(&sWarpLock);
    if (sWarpPending && sWarpCooldown == 0 && gMain.callback2 == CB2_Overworld && !gPaletteFade.active)
    {
        go = true;
        g = sWarpGroup; n = sWarpNum; x = sWarpX; y = sWarpY;
        sWarpPending = false;
    }
    SDL_AtomicUnlock(&sWarpLock);
    if (!go)
        return;
    SDL_Log("[Harness] warp to %d:%d (%d,%d)", g, n, x, y);
    SetWarpDestination(g, n, -1, x, y);
    DoWarp();
    ResetInitialPlayerAvatarState();
    sWarpCooldown = 180;
}

#endif
