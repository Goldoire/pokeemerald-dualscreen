#ifdef PLATFORM_SDL2
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>
#endif

#ifdef __ANDROID__
#include <jni.h>
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#ifdef NATIVE_LINUX
#include <SDL2/SDL_image.h>
#endif

// The voxel renderer runs on desktop Linux (OpenGL 2.1) and Android
// (GLES 1.1 via the gles1_compat shim).
#if defined(NATIVE_LINUX) || defined(__ANDROID__)
#define VOXEL_CAPABLE 1
#include "voxel/voxel_renderer.h"
bool gVoxelModeEnabled = false;
#endif
#include "mods/mod_manager.h"

#include "global.h"
#include "platform.h"
#include "rtc.h"
#include "gba/defines.h"
#include "gba/m4a_internal.h"
#include "cgb_audio.h"
#include "gba/flash_internal.h"
#include "platform/dma.h"
#include "platform/framedraw.h"
#include "platform/dualscreen.h"

extern void (*const gIntrTable[])(void);

SDL_Thread *mainLoopThread;
SDL_Window *sdlWindow;
SDL_Renderer *sdlRenderer;
SDL_Texture *sdlTexture;
#if defined(NATIVE_LINUX) || defined(_WIN32)
#define MAX_BORDER_BACKGROUNDS 15
SDL_Texture *sdlBackgroundTextures[MAX_BORDER_BACKGROUNDS];
SDL_Texture *sdlBorderTexture;
#endif
static u8 sBorderBackgroundCount = 1;
SDL_AudioDeviceID sdlAudioDevice;
SDL_sem *vBlankSemaphore;
SDL_atomic_t isFrameAvailable;
bool speedUp = false;
unsigned int videoScale = 1;
bool isRunning = true;
bool paused = false;
double simTime = 0;
double lastGameTime = 0;
double curGameTime = 0;
double fixedTimestep = 1.0 / 60.0; // 16.666667ms
double timeScale = 1.0;
struct SiiRtcInfo internalClock;

static FILE *sSaveFile = NULL;
static char sSavePath[1024] = "pokeemerald.sav";
static char sConfigPath[1024] = "pokeemerald.cfg";
static u8 sBorderBackground;
static bool sHasBorderBackgroundConfig;
static u8 sBackgroundOrderVersion;
// Dual-screen defaults: black background, touch controls hidden, battle
// menus on the bottom screen.
static u8 sPlatformSettings[PLATFORM_SETTING_COUNT] = {0, 4, 0, 1, 1, 10, 1, 0, 0, 0, 0, 0};
#ifdef __ANDROID__
static SDL_GameController *androidController;
#endif

extern void AgbMain(void);
extern void DoSoftReset(void);

int DoMain(void *param);
void ProcessEvents(void);
void VDraw(SDL_Texture *texture);

static void ReadSaveFile(const char *path);
static void ReadConfigFile(void);
static void StoreConfigFile(void);
static void ApplyPlatformSettings(void);
static void StoreSaveFile(void);
static void CloseSaveFile(void);

static void UpdateInternalClock(void);

#ifdef __ANDROID__
static void HandleTouchEvent(const SDL_TouchFingerEvent *event);
static void DrawTouchControls(void);
static void MigrateFile(const char *from, const char *to);
static FILE *ReclaimSaveFile(const char *path);
#endif

int main(int argc, char **argv)
{
    // Open an output console on Windows
#ifdef _WIN32
    AllocConsole() ;
    AttachConsole( GetCurrentProcessId() ) ;
    freopen( "CON", "w", stdout ) ;
#endif

#ifdef NATIVE_LINUX
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--voxel") == 0 || strcmp(argv[i], "--renderer=voxel") == 0)
            gVoxelModeEnabled = true;
        if (strcmp(argv[i], "--mods") == 0)
            gModsEnabled = true;
        if (strcmp(argv[i], "--mod") == 0 && i + 1 < argc) {
            gModsEnabled = true;
            gActiveModSelector = argv[i+1];
            i++;
        }
    }
#endif

#ifdef __ANDROID__
    SDL_setenv("SDL_AUDIODRIVER", "openslES", 1);
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#endif
#ifdef __ANDROID__
    // Distributable builds: restore the zeroed asset ranges before anything
    // reads game data. ModManager_Init's RAM shadows copy from holed .rodata,
    // so this has to come first; the game normally reaches here already
    // filled by RomGateActivity, but Android can restart the game activity
    // straight into a fresh process without it.
    char *prefPath = SDL_GetPrefPath("pokeemerald", "pokeemerald");
    if (prefPath != NULL)
        DualScreen_FillAssets(prefPath);
#endif
    ModManager_Init();
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO
#ifdef __ANDROID__
                | SDL_INIT_GAMECONTROLLER
#endif
                ) < 0)
    {
        DBGPRINTF("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

#ifdef __ANDROID__
    for (int i = 0; i < SDL_NumJoysticks() && androidController == NULL; i++)
    {
        if (SDL_IsGameController(i))
            androidController = SDL_GameControllerOpen(i);
    }
#endif

#ifdef __ANDROID__
    // Only now that the fill above has run: these buffers live in .data, and
    // the zero padding past the string matches the ROM, so the fill holes
    // cover everything past the first 32 bytes of each path.
    if (prefPath != NULL)
    {
        // The save and the settings live in the app's external files dir
        // (/sdcard/Android/data/<pkg>/files), alongside the baserom.gba the
        // ROM gate reads: that directory is reachable over adb, USB and the
        // device's file manager, so a save can be copied in from an emulator
        // or out before an uninstall. The private pref path is the fallback
        // when external storage is not writable, and stays the home of
        // asset_manifest.bin, which only the fill above touches.
        const char *extPath = SDL_AndroidGetExternalStoragePath();
        if (extPath != NULL && (SDL_AndroidGetExternalStorageState() & SDL_ANDROID_EXTERNAL_STORAGE_WRITE))
        {
            char legacyPath[1024];

            SDL_snprintf(sSavePath, sizeof(sSavePath), "%s/pokeemerald.sav", extPath);
            SDL_snprintf(sConfigPath, sizeof(sConfigPath), "%s/pokeemerald.cfg", extPath);

            // Without this an existing install starts a fresh game on the
            // new path and leaves the old save stranded where only adb can
            // reach it.
            SDL_snprintf(legacyPath, sizeof(legacyPath), "%spokeemerald.sav", prefPath);
            MigrateFile(legacyPath, sSavePath);
            SDL_snprintf(legacyPath, sizeof(legacyPath), "%spokeemerald.cfg", prefPath);
            MigrateFile(legacyPath, sConfigPath);
        }
        else
        {
            SDL_snprintf(sSavePath, sizeof(sSavePath), "%spokeemerald.sav", prefPath);
            SDL_snprintf(sConfigPath, sizeof(sConfigPath), "%spokeemerald.cfg", prefPath);
        }
        SDL_free(prefPath);
    }
#endif
    ReadSaveFile(sSavePath);
    ReadConfigFile();
#ifdef __ANDROID__
    gVoxelModeEnabled = sPlatformSettings[PLATFORM_SETTING_VOXEL_RENDERER] != 0;
#endif

#ifdef __ANDROID__
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
#endif
#if defined(NATIVE_LINUX) || defined(_WIN32)
    Uint32 windowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
#ifdef NATIVE_LINUX
    if (gVoxelModeEnabled) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        windowFlags |= SDL_WINDOW_OPENGL;
    }
#endif
    sdlWindow = SDL_CreateWindow("Pokemon Emerald", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, windowFlags);
#else
    Uint32 androidWindowFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
    if (gVoxelModeEnabled)
    {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        androidWindowFlags |= SDL_WINDOW_OPENGL;
    }
    sdlWindow = SDL_CreateWindow("pokeemerald", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, DISPLAY_WIDTH * videoScale, DISPLAY_HEIGHT * videoScale, androidWindowFlags);
#endif
    if (sdlWindow == NULL)
    {
        DBGPRINTF("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

#ifdef VOXEL_CAPABLE
    SDL_GLContext glContext = NULL;
    if (gVoxelModeEnabled) {
        glContext = SDL_GL_CreateContext(sdlWindow);
        if (glContext == NULL) {
            printf("[Voxel] Failed to create OpenGL context: %s\n", SDL_GetError());
            gVoxelModeEnabled = false;
        } else if (!VoxelRenderer_Init()) {
            printf("[Voxel] Renderer init failed, falling back to classic\n");
            gVoxelModeEnabled = false;
        }
    }
#endif

#ifdef VOXEL_CAPABLE
    if (!gVoxelModeEnabled)
    {
#endif
#ifdef __ANDROID__
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_ACCELERATED);
#else
    sdlRenderer = SDL_CreateRenderer(sdlWindow, -1, SDL_RENDERER_PRESENTVSYNC);
#endif
    if (sdlRenderer == NULL)
    {
        DBGPRINTF("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
#ifdef VOXEL_CAPABLE
    } // end if (!gVoxelModeEnabled)
#endif

    if (sdlRenderer != NULL) {
        SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
        SDL_RenderClear(sdlRenderer);
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");


    for (int i = 1; i < 15; i++)
    {
        char filename[16];
#ifdef _WIN32
        snprintf(filename, sizeof(filename), "BG%d.bmp", i);
#else
        snprintf(filename, sizeof(filename), "BG%d.png", i);
#endif
        SDL_RWops *backgroundFile = SDL_RWFromFile(filename, "rb");
        if (backgroundFile == NULL)
            break;
        SDL_RWclose(backgroundFile);
        sBorderBackgroundCount++;
    }
    if (sBackgroundOrderVersion < 2)
    {
        if (sHasBorderBackgroundConfig)
        {
            if (sBorderBackground == 1)
                sBorderBackground = sBorderBackgroundCount;
            else if (sBorderBackground >= 2)
                sBorderBackground--;
        }
        sBackgroundOrderVersion = 2;
        StoreConfigFile();
    }
#ifdef NATIVE_LINUX
    if (sdlRenderer != NULL)
    {
        SDL_RenderSetLogicalSize(sdlRenderer, 0, 0);
        if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0)
        {
            SDL_Log("SDL_image could not initialize: %s", IMG_GetError());
        }
        else
        {
            for (int i = 0; i < sBorderBackgroundCount; i++)
            {
                char filename[16];
                snprintf(filename, sizeof(filename), i == 0 ? "BG.png" : "BG%d.png", i);
                sdlBackgroundTextures[i] = IMG_LoadTexture(sdlRenderer, filename);
            }
            sdlBorderTexture = IMG_LoadTexture(sdlRenderer, "Border.png");
            if (sdlBackgroundTextures[0] == NULL)
                SDL_Log("Background image could not be loaded: %s", IMG_GetError());
            if (sdlBorderTexture == NULL)
                SDL_Log("Border image could not be loaded: %s", IMG_GetError());
        }
    }
#elif defined(_WIN32)
    if (sdlRenderer != NULL)
    {
        SDL_RenderSetLogicalSize(sdlRenderer, 0, 0);
        SDL_Surface *borderSurface = SDL_LoadBMP("Border.bmp");
        for (int i = 0; i < sBorderBackgroundCount; i++)
        {
            char filename[16];
            snprintf(filename, sizeof(filename), i == 0 ? "BG.bmp" : "BG%d.bmp", i);
            SDL_Surface *backgroundSurface = SDL_LoadBMP(filename);
            if (backgroundSurface == NULL)
                continue;
            sdlBackgroundTextures[i] = SDL_CreateTextureFromSurface(sdlRenderer, backgroundSurface);
            SDL_FreeSurface(backgroundSurface);
        }
        if (sdlBackgroundTextures[0] == NULL)
            SDL_Log("Background image could not be loaded: %s", SDL_GetError());
        if (borderSurface == NULL)
        {
            SDL_Log("Border image could not be loaded: %s", SDL_GetError());
        }
        else
        {
            sdlBorderTexture = SDL_CreateTextureFromSurface(sdlRenderer, borderSurface);
            SDL_FreeSurface(borderSurface);
        }
    }
#else
    if (sdlRenderer != NULL)
    {
        SDL_RenderSetLogicalSize(sdlRenderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        SDL_RenderSetIntegerScale(sdlRenderer, SDL_TRUE);
    }
#endif
    ApplyPlatformSettings();

    if (sdlRenderer != NULL) {
        sdlTexture = SDL_CreateTexture(sdlRenderer,
                                       SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       DISPLAY_WIDTH, DISPLAY_HEIGHT);
        if (sdlTexture == NULL)
        {
            DBGPRINTF("Texture could not be created! SDL_Error: %s\n", SDL_GetError());
            return 1;
        }
        SDL_SetTextureBlendMode(sdlTexture, SDL_BLENDMODE_NONE);
    }


    simTime = curGameTime = lastGameTime = SDL_GetPerformanceCounter();

    isFrameAvailable.value = 0;
    vBlankSemaphore = SDL_CreateSemaphore(0);

    SDL_AudioSpec want;

    SDL_memset(&want, 0, sizeof(want)); /* or SDL_zero(want) */
    want.freq = 42060;
    want.format = AUDIO_F32;
    want.channels = 2;
    want.samples = 1024;
    cgb_audio_init(want.freq);


    sdlAudioDevice = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (sdlAudioDevice == 0)
        SDL_Log("Failed to open audio: %s", SDL_GetError());
    else
    {
        if (want.format != AUDIO_F32) /* we let this one thing change. */
            SDL_Log("We didn't get Float32 audio format.");
        SDL_PauseAudioDevice(sdlAudioDevice, 0);
    }
#ifndef __ANDROID__
    if (sdlTexture != NULL)
        VDraw(sdlTexture);
#ifdef NATIVE_LINUX
    else if (gVoxelModeEnabled)
        REG_VCOUNT = 161;
#endif
#endif
    mainLoopThread = SDL_CreateThread(DoMain, "AgbMain", NULL);

    double accumulator = 0.0;
#ifdef __ANDROID__
    bool needsPresent = false;
#endif

    memset(&internalClock, 0, sizeof(internalClock));
    internalClock.status = SIIRTCINFO_24HOUR;
    UpdateInternalClock();

    while (isRunning)
    {
        ProcessEvents();

        if (!paused)
        {
            // Fast-forward setting: 0 = 1x .. 3 = 4x game speed.
            timeScale = 1.0 + sPlatformSettings[PLATFORM_SETTING_FAST_FORWARD];
            double dt = fixedTimestep / timeScale; // TODO: Fix speedup

            curGameTime = SDL_GetPerformanceCounter();
            double deltaTime = (double)((curGameTime - lastGameTime) / (double)SDL_GetPerformanceFrequency());
            if (deltaTime > (dt * 5))
                deltaTime = dt;
            lastGameTime = curGameTime;

            accumulator += deltaTime;

            while (accumulator >= dt)
            {
                if (SDL_AtomicGet(&isFrameAvailable))
                {
#ifdef VOXEL_CAPABLE
                    if (gVoxelModeEnabled) {
                        VoxelRenderer_RenderFrame();
                    } else
#endif
                    {
                        VDraw(sdlTexture);
                        if (sdlRenderer) SDL_RenderClear(sdlRenderer);
#if defined(NATIVE_LINUX) || defined(_WIN32)
                        u8 backgroundOption = Platform_GetBorderBackground();
                        if (backgroundOption < sBorderBackgroundCount
                         && sdlBackgroundTextures[backgroundOption] != NULL)
                            SDL_RenderCopy(sdlRenderer, sdlBackgroundTextures[backgroundOption], NULL, NULL);
                        int outputWidth;
                        int outputHeight;
                        SDL_GetRendererOutputSize(sdlRenderer, &outputWidth, &outputHeight);
                        int gameHeight;
                        int gameWidth;
                        if (sPlatformSettings[PLATFORM_SETTING_INTEGER_SCALE])
                        {
                            int scale = outputWidth / DISPLAY_WIDTH;
                            if (outputHeight / DISPLAY_HEIGHT < scale)
                                scale = outputHeight / DISPLAY_HEIGHT;
                            if (scale < 1)
                                scale = 1;
                            gameWidth = DISPLAY_WIDTH * scale;
                            gameHeight = DISPLAY_HEIGHT * scale;
                        }
                        else
                        {
                            gameHeight = outputHeight * 8 / 9;
                            gameWidth = gameHeight * 3 / 2;
                        }
                        SDL_Rect gameViewport = {(outputWidth - gameWidth) / 2,
                                                 (outputHeight - gameHeight) / 2,
                                                 gameWidth, gameHeight};
                        SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &gameViewport);
                        if (sPlatformSettings[PLATFORM_SETTING_BORDER] && sdlBorderTexture != NULL)
                        {
                            SDL_Rect borderSource = {141, 18, 1000, 683};
                            int innerWidth = gameViewport.w - 2;
                            int innerHeight = gameViewport.h - 2;
                            SDL_Rect borderViewport = {
                                gameViewport.x + 1 - innerWidth * 19 / 961,
                                gameViewport.y + 1 - innerHeight * 20 / 643,
                                innerWidth * 1000 / 961,
                                innerHeight * 683 / 643
                            };
                            SDL_RenderCopy(sdlRenderer, sdlBorderTexture, &borderSource, &borderViewport);
                        }
#else
                        {
                            // Widescreen stretches to the full surface; otherwise
                            // the 240x160 logical size letterboxes to 3:2.
                            static u8 sAppliedWidescreen = 0xFF;
                            u8 widescreen = sPlatformSettings[PLATFORM_SETTING_WIDESCREEN];
                            if (sdlRenderer != NULL && widescreen != sAppliedWidescreen)
                            {
                                SDL_RenderSetLogicalSize(sdlRenderer,
                                        widescreen ? 0 : DISPLAY_WIDTH,
                                        widescreen ? 0 : DISPLAY_HEIGHT);
                                sAppliedWidescreen = widescreen;
                            }
                        }
                        if (sdlRenderer) SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
#endif
                    }
#ifdef __ANDROID__
                    // Presenting here would block on vsync once per game
                    // frame, capping the catch-up loop at the display's
                    // refresh rate and cancelling out fast-forward. Present
                    // once per outer iteration instead, after the catch-up.
                    needsPresent = true;
#endif
                    SDL_AtomicSet(&isFrameAvailable, 0);

                    REG_DISPSTAT |= INTR_FLAG_VBLANK;

                    RunDMAs(DMA_HBLANK);

#ifdef __ANDROID__
                    if (REG_IE & INTR_FLAG_VBLANK)
#else
                    if (REG_DISPSTAT & DISPSTAT_VBLANK_INTR)
#endif
                    {
                        static int sVBlankPrints = 0;
                        if (sVBlankPrints < 300) {
                            if (sVBlankPrints % 30 == 0) printf("[DoMain] Executing VBlankIntr, REG_DISPSTAT=%04x\n", REG_DISPSTAT);
                            sVBlankPrints++;
                        }
                        gIntrTable[4]();
                    }
                    else
                    {
                        static int sNoVBlankPrints = 0;
                        if (sNoVBlankPrints < 300) {
                            if (sNoVBlankPrints % 30 == 0) printf("[DoMain] SKIPPING VBlankIntr, REG_DISPSTAT=%04x, sRegIE=%04x\n", REG_DISPSTAT, REG_IE);
                            sNoVBlankPrints++;
                        }
                    }
                    REG_DISPSTAT &= ~INTR_FLAG_VBLANK;

                    DualScreen_FrameHook();

                    SDL_SemPost(vBlankSemaphore);

                    accumulator -= dt;
                }
            }
        }

#ifdef __ANDROID__
        if (needsPresent)
        {
            if (sdlRenderer) SDL_RenderPresent(sdlRenderer);
            needsPresent = false;
        }
#else
        #ifdef NATIVE_LINUX
        if (!gVoxelModeEnabled)
        #endif
        if (sdlRenderer) SDL_RenderPresent(sdlRenderer);
#endif
    }

    //StoreSaveFile();
    CloseSaveFile();

#if defined(NATIVE_LINUX) || defined(_WIN32)
    for (int i = 0; i < sBorderBackgroundCount; i++)
        SDL_DestroyTexture(sdlBackgroundTextures[i]);
    SDL_DestroyTexture(sdlBorderTexture);
#endif

#ifdef NATIVE_LINUX
    IMG_Quit();
#endif
#ifdef VOXEL_CAPABLE
    if (gVoxelModeEnabled) {
        VoxelRenderer_Shutdown();
    }
#endif
    SDL_DestroyWindow(sdlWindow);
    ModManager_Shutdown();
    SDL_Quit();
    return 0;
}

#ifdef __ANDROID__
// Copy `from` to `to`, but only when `to` does not exist yet: the destination
// is the live file once the game has run there, and a second migration must
// never overwrite newer progress with the stale copy left behind.
static void MigrateFile(const char *from, const char *to)
{
    FILE *src, *dst;
    char buffer[4096];
    size_t got;

    dst = fopen(to, "rb");
    if (dst != NULL)
    {
        fclose(dst);
        return;
    }

    src = fopen(from, "rb");
    if (src == NULL)
        return;

    dst = fopen(to, "wb");
    if (dst == NULL)
    {
        fclose(src);
        return;
    }

    while ((got = fread(buffer, 1, sizeof(buffer), src)) > 0)
    {
        if (fwrite(buffer, 1, got, dst) != got)
        {
            SDL_Log("Failed to migrate %s to %s", from, to);
            fclose(src);
            fclose(dst);
            remove(to);
            return;
        }
    }

    fclose(src);
    fclose(dst);
    SDL_Log("Migrated %s to %s", from, to);
}
#endif

#ifdef __ANDROID__
// A save copied into the external files dir belongs to whatever wrote it —
// `adb push` leaves it owned by shell, mode 644 — so the game can read it but
// cannot reopen it "r+b", and the "w+b" fallback cannot truncate it either.
// The directory is ours, so rewrite the contents into a file the app owns.
// The copy goes to a sibling and is renamed over the original rather than
// unlinking first: if any step fails, the incoming save is still on disk.
static FILE *ReclaimSaveFile(const char *path)
{
    char tempPath[1024];
    FILE *incoming, *owned;
    char buffer[4096];
    size_t got;

    // Missing rather than unwritable: let the caller create it as usual.
    incoming = fopen(path, "rb");
    if (incoming == NULL)
        return NULL;

    if (SDL_snprintf(tempPath, sizeof(tempPath), "%s.reclaim", path) >= (int)sizeof(tempPath))
    {
        fclose(incoming);
        return NULL;
    }

    owned = fopen(tempPath, "wb");
    if (owned == NULL)
    {
        fclose(incoming);
        return NULL;
    }

    while ((got = fread(buffer, 1, sizeof(buffer), incoming)) > 0)
    {
        if (fwrite(buffer, 1, got, owned) != got)
        {
            fclose(incoming);
            fclose(owned);
            remove(tempPath);
            return NULL;
        }
    }

    fclose(incoming);
    if (fclose(owned) != 0 || rename(tempPath, path) != 0)
    {
        remove(tempPath);
        return NULL;
    }

    SDL_Log("Took ownership of imported save file: %s", path);
    return fopen(path, "r+b");
}
#endif

static void ReadSaveFile(const char *path)
{
    // Check whether the saveFile exists, and create it if not
    sSaveFile = fopen(path, "r+b");
#ifdef __ANDROID__
    if (sSaveFile == NULL)
    {
        sSaveFile = ReclaimSaveFile(path);
    }
#endif
    if (sSaveFile == NULL)
    {
        sSaveFile = fopen(path, "w+b");
    }

    if (sSaveFile == NULL)
    {
        memset(FLASH_BASE, 0xFF, sizeof(FLASH_BASE));
        SDL_Log("Unable to open save file: %s", path);
        return;
    }

    fseek(sSaveFile, 0, SEEK_END);
    int fileSize = ftell(sSaveFile);
    fseek(sSaveFile, 0, SEEK_SET);

    // Only read as many bytes as fit inside the buffer
    // or as many bytes as are in the file
    int bytesToRead = (fileSize < sizeof(FLASH_BASE)) ? fileSize : sizeof(FLASH_BASE);

    int bytesRead = fread(FLASH_BASE, 1, bytesToRead, sSaveFile);

    // Fill the buffer if the savefile was just created or smaller than the buffer itself
    for (int i = bytesRead; i < sizeof(FLASH_BASE); i++)
    {
        FLASH_BASE[i] = 0xFF;
    }
}

static void ReadConfigFile(void)
{
    FILE *configFile = fopen(sConfigPath, "r");
    char line[64];
    unsigned int value;

    if (configFile == NULL)
        return;
    while (fgets(line, sizeof(line), configFile) != NULL)
    {
        if (sscanf(line, "borderBackground=%u", &value) == 1 && value < 16)
        {
            sBorderBackground = value;
            sHasBorderBackgroundConfig = true;
        }
        else if (sscanf(line, "backgroundOrder=%u", &value) == 1)
            sBackgroundOrderVersion = value;
        else if (sscanf(line, "fullscreen=%u", &value) == 1)
            sPlatformSettings[PLATFORM_SETTING_FULLSCREEN] = value != 0;
        else if (sscanf(line, "windowScale=%u", &value) == 1 && value >= 2 && value <= 5)
            sPlatformSettings[PLATFORM_SETTING_WINDOW_SCALE] = value;
        else if (sscanf(line, "integerScale=%u", &value) == 1)
            sPlatformSettings[PLATFORM_SETTING_INTEGER_SCALE] = value != 0;
        else if (sscanf(line, "vsync=%u", &value) == 1)
            sPlatformSettings[PLATFORM_SETTING_VSYNC] = value != 0;
        else if (sscanf(line, "border=%u", &value) == 1)
            sPlatformSettings[PLATFORM_SETTING_BORDER] = value != 0;
        else if (sscanf(line, "volume=%u", &value) == 1 && value <= 10)
            sPlatformSettings[PLATFORM_SETTING_VOLUME] = value;
        else if (sscanf(line, "backgroundMode=%u", &value) == 1 && value <= 2)
            sPlatformSettings[PLATFORM_SETTING_BACKGROUND_MODE] = value;
        else if (sscanf(line, "widescreen=%u", &value) == 1)
            sPlatformSettings[PLATFORM_SETTING_WIDESCREEN] = value != 0;
        else if (sscanf(line, "touchControls=%u", &value) == 1)
            sPlatformSettings[PLATFORM_SETTING_TOUCH_CONTROLS] = value != 0;
        else if (sscanf(line, "battleUiTop=%u", &value) == 1)
            sPlatformSettings[PLATFORM_SETTING_BATTLE_UI_TOP] = value != 0;
        else if (sscanf(line, "fastForward=%u", &value) == 1 && value <= 3)
            sPlatformSettings[PLATFORM_SETTING_FAST_FORWARD] = value;
        else if (sscanf(line, "voxelRenderer=%u", &value) == 1)
            sPlatformSettings[PLATFORM_SETTING_VOXEL_RENDERER] = value != 0;
    }
    fclose(configFile);
}

static void StoreConfigFile(void)
{
    FILE *configFile = fopen(sConfigPath, "w");

    if (configFile == NULL)
        return;
    fprintf(configFile, "borderBackground=%u\n", sBorderBackground);
    fprintf(configFile, "backgroundOrder=2\n");
    fprintf(configFile, "fullscreen=%u\n", sPlatformSettings[PLATFORM_SETTING_FULLSCREEN]);
    fprintf(configFile, "windowScale=%u\n", sPlatformSettings[PLATFORM_SETTING_WINDOW_SCALE]);
    fprintf(configFile, "integerScale=%u\n", sPlatformSettings[PLATFORM_SETTING_INTEGER_SCALE]);
    fprintf(configFile, "vsync=%u\n", sPlatformSettings[PLATFORM_SETTING_VSYNC]);
    fprintf(configFile, "border=%u\n", sPlatformSettings[PLATFORM_SETTING_BORDER]);
    fprintf(configFile, "volume=%u\n", sPlatformSettings[PLATFORM_SETTING_VOLUME]);
    fprintf(configFile, "backgroundMode=%u\n", sPlatformSettings[PLATFORM_SETTING_BACKGROUND_MODE]);
    fprintf(configFile, "widescreen=%u\n", sPlatformSettings[PLATFORM_SETTING_WIDESCREEN]);
    fprintf(configFile, "touchControls=%u\n", sPlatformSettings[PLATFORM_SETTING_TOUCH_CONTROLS]);
    fprintf(configFile, "battleUiTop=%u\n", sPlatformSettings[PLATFORM_SETTING_BATTLE_UI_TOP]);
    fprintf(configFile, "fastForward=%u\n", sPlatformSettings[PLATFORM_SETTING_FAST_FORWARD]);
    fprintf(configFile, "voxelRenderer=%u\n", sPlatformSettings[PLATFORM_SETTING_VOXEL_RENDERER]);
    fclose(configFile);
}

static void ApplyPlatformSettings(void)
{
    if (sdlRenderer == NULL)
        return; // In voxel mode, no SDL renderer to configure
    SDL_RenderSetVSync(sdlRenderer, sPlatformSettings[PLATFORM_SETTING_VSYNC]);
#if defined(NATIVE_LINUX) || defined(_WIN32)
    SDL_SetWindowFullscreen(sdlWindow, sPlatformSettings[PLATFORM_SETTING_FULLSCREEN]
                                      ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (!sPlatformSettings[PLATFORM_SETTING_FULLSCREEN])
    {
        int scale = sPlatformSettings[PLATFORM_SETTING_WINDOW_SCALE];
        SDL_SetWindowSize(sdlWindow, 320 * scale, 180 * scale);
        SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
#endif
}

static void StoreSaveFile()
{
    if (sSaveFile != NULL)
    {
        fseek(sSaveFile, 0, SEEK_SET);
        fwrite(FLASH_BASE, 1, sizeof(FLASH_BASE), sSaveFile);
    }
}

void Platform_StoreSaveFile(void)
{
    StoreSaveFile();
}

void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size)
{
    DBGPRINTF("ReadFlash(sectorNum=0x%04X,offset=0x%08X,size=0x%02X)\n",sectorNum,offset,size);
    FILE * savefile = fopen(sSavePath, "r+b");
    if (savefile == NULL)
    {
        puts("Error opening save file.");
        return;
    }
    if (fseek(savefile, (sectorNum << gFlash->sector.shift) + offset, SEEK_SET))
    {
        fclose(savefile);
        return;
    }
    if (fread(dest, 1, size, savefile) != size)
    {
        fclose(savefile);
        return;
    }
    fclose(savefile);
}

void Platform_QueueAudio(float *audioBuffer, s32 samplesPerFrame)
{
    if (sdlAudioDevice != 0)
    {
        int floatCount = samplesPerFrame / sizeof(float);
        float adjustedAudio[floatCount];
        float volume = sPlatformSettings[PLATFORM_SETTING_VOLUME] / 10.0f;
        for (int i = 0; i < floatCount; i++)
            adjustedAudio[i] = audioBuffer[i] * volume;
        if (SDL_QueueAudio(sdlAudioDevice, adjustedAudio, samplesPerFrame) < 0)
            SDL_Log("Failed to queue audio: %s", SDL_GetError());
    }
}

u8 Platform_GetBorderBackgroundCount(void)
{
    return sBorderBackgroundCount + 1;
}

u8 Platform_GetBorderBackground(void)
{
    if (sHasBorderBackgroundConfig)
        return sBorderBackground;
    if (gSaveBlock2Ptr != NULL)
    {
        u8 legacySelection = gSaveBlock2Ptr->optionsBorderBackground;
        if (legacySelection == 1)
            return sBorderBackgroundCount;
        if (legacySelection >= 2)
            return legacySelection - 1;
    }
    return 0;
}

void Platform_SetBorderBackground(u8 selection)
{
    sBorderBackground = selection;
    sHasBorderBackgroundConfig = true;
    StoreConfigFile();
}

u8 Platform_GetSetting(enum PlatformSetting setting)
{
    return sPlatformSettings[setting];
}

void Platform_SetSetting(enum PlatformSetting setting, u8 value)
{
    sPlatformSettings[setting] = value;
    if (setting == PLATFORM_SETTING_VSYNC && sdlRenderer != NULL)
        SDL_RenderSetVSync(sdlRenderer, value);
#if defined(NATIVE_LINUX) || defined(_WIN32)
    else if (setting == PLATFORM_SETTING_FULLSCREEN)
    {
        SDL_SetWindowFullscreen(sdlWindow, value ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        if (!value)
        {
            int scale = sPlatformSettings[PLATFORM_SETTING_WINDOW_SCALE];
            SDL_SetWindowSize(sdlWindow, 320 * scale, 180 * scale);
            SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    }
    else if (setting == PLATFORM_SETTING_WINDOW_SCALE && !sPlatformSettings[PLATFORM_SETTING_FULLSCREEN])
    {
        SDL_SetWindowSize(sdlWindow, 320 * value, 180 * value);
        SDL_SetWindowPosition(sdlWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
#endif
    StoreConfigFile();
}

#ifdef __ANDROID__
JNIEXPORT jint JNICALL Java_com_pokeemerald_experimental_GbaControlsView_getBorderBackground(JNIEnv *env, jclass clazz)
{
    return Platform_GetBorderBackground();
}

JNIEXPORT jint JNICALL Java_com_pokeemerald_experimental_GbaControlsView_getPlatformSetting(JNIEnv *env, jclass clazz, jint setting)
{
    if (setting < 0 || setting >= PLATFORM_SETTING_COUNT)
        return 0;
    return Platform_GetSetting(setting);
}

JNIEXPORT jint JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeGetPlatformSetting(JNIEnv *env, jclass clazz, jint setting)
{
    if (setting < 0 || setting >= PLATFORM_SETTING_COUNT)
        return 0;
    return Platform_GetSetting(setting);
}

JNIEXPORT void JNICALL Java_com_pokeemerald_experimental_DualScreenBridge_nativeSetPlatformSetting(JNIEnv *env, jclass clazz, jint setting, jint value)
{
    if (setting < 0 || setting >= PLATFORM_SETTING_COUNT)
        return;
    Platform_SetSetting(setting, (u8)value);
}
#endif


static void CloseSaveFile()
{
    if (sSaveFile != NULL)
    {
        fclose(sSaveFile);
    }
}

// Key mappings
#define KEY_A_BUTTON      SDLK_z
#define KEY_B_BUTTON      SDLK_x
#define KEY_START_BUTTON  SDLK_RETURN
#define KEY_SELECT_BUTTON SDLK_BACKSLASH
#define KEY_L_BUTTON      SDLK_a
#define KEY_R_BUTTON      SDLK_s
#define KEY_DPAD_UP       SDLK_UP
#define KEY_DPAD_DOWN     SDLK_DOWN
#define KEY_DPAD_LEFT     SDLK_LEFT
#define KEY_DPAD_RIGHT    SDLK_RIGHT

#define HANDLE_KEYUP(key) \
case KEY_##key:  keyboardKeys &= ~key; break;

#define HANDLE_KEYDOWN(key) \
case KEY_##key:  keyboardKeys |= key; break;

static u16 keyboardKeys;

#ifdef __ANDROID__
#define MAX_TOUCH_FINGERS 10

struct TouchFinger
{
    SDL_FingerID id;
    float x;
    float y;
    bool active;
};

static struct TouchFinger touchFingers[MAX_TOUCH_FINGERS];
static u16 touchKeys;
static u16 controllerKeys;
static u16 controllerAxisKeys;
static Sint16 controllerAxisX;
static Sint16 controllerAxisY;

static bool IsInsideRect(int x, int y, SDL_Rect rect)
{
    SDL_Point point = {x, y};
    return SDL_PointInRect(&point, &rect);
}

static int MinInt(int a, int b)
{
    return a < b ? a : b;
}

static int GetControlSideWidth(int windowWidth, int windowHeight)
{
    int sideWidth = (windowWidth - windowHeight * 3 / 2) / 2;
    int minimumWidth = windowWidth * 14 / 100;
    return sideWidth > minimumWidth ? sideWidth : minimumWidth;
}

static void UpdateTouchKeys(void)
{
    int windowWidth;
    int windowHeight;
    SDL_GetWindowSize(sdlWindow, &windowWidth, &windowHeight);
    int sideWidth = GetControlSideWidth(windowWidth, windowHeight);
    int buttonSize = MinInt(sideWidth * 2 / 5, windowHeight / 6);
    int dpadUnit = MinInt(sideWidth / 3, windowHeight / 8);
    int dpadX = sideWidth * 2 / 3;
    int dpadY = windowHeight * 7 / 10;
    SDL_Rect dpadUp = {dpadX - dpadUnit / 2, dpadY - dpadUnit * 3 / 2,
                       dpadUnit, dpadUnit};
    SDL_Rect dpadDown = {dpadX - dpadUnit / 2, dpadY + dpadUnit / 2,
                         dpadUnit, dpadUnit};
    SDL_Rect dpadLeft = {dpadX - dpadUnit * 3 / 2, dpadY - dpadUnit / 2,
                         dpadUnit, dpadUnit};
    SDL_Rect dpadRight = {dpadX + dpadUnit / 2, dpadY - dpadUnit / 2,
                          dpadUnit, dpadUnit};
    SDL_Rect aButton = {windowWidth - sideWidth / 4 - buttonSize,
                        windowHeight * 58 / 100, buttonSize, buttonSize};
    SDL_Rect bButton = {windowWidth - sideWidth + sideWidth / 4,
                        windowHeight * 76 / 100, buttonSize, buttonSize};
    SDL_Rect selectButton = {sideWidth / 4, windowHeight / 4,
                             sideWidth / 2, windowHeight / 10};
    SDL_Rect startButton = {windowWidth - sideWidth * 3 / 4, windowHeight / 4,
                            sideWidth / 2, windowHeight / 10};
    SDL_Rect lButton = {sideWidth / 4, windowHeight / 20,
                        sideWidth / 2, windowHeight / 10};
    SDL_Rect rButton = {windowWidth - sideWidth * 3 / 4, windowHeight / 20,
                        sideWidth / 2, windowHeight / 10};

    touchKeys = 0;

    for (int i = 0; i < MAX_TOUCH_FINGERS; i++)
    {
        if (!touchFingers[i].active)
            continue;

        int x = touchFingers[i].x * windowWidth;
        int y = touchFingers[i].y * windowHeight;

        if (IsInsideRect(x, y, dpadUp)) touchKeys |= DPAD_UP;
        if (IsInsideRect(x, y, dpadDown)) touchKeys |= DPAD_DOWN;
        if (IsInsideRect(x, y, dpadLeft)) touchKeys |= DPAD_LEFT;
        if (IsInsideRect(x, y, dpadRight)) touchKeys |= DPAD_RIGHT;

        if (IsInsideRect(x, y, aButton)) touchKeys |= A_BUTTON;
        if (IsInsideRect(x, y, bButton)) touchKeys |= B_BUTTON;
        if (IsInsideRect(x, y, startButton)) touchKeys |= START_BUTTON;
        if (IsInsideRect(x, y, selectButton)) touchKeys |= SELECT_BUTTON;
        if (IsInsideRect(x, y, lButton)) touchKeys |= L_BUTTON;
        if (IsInsideRect(x, y, rButton)) touchKeys |= R_BUTTON;
    }
}

static void HandleTouchEvent(const SDL_TouchFingerEvent *event)
{
    int slot = -1;
    for (int i = 0; i < MAX_TOUCH_FINGERS; i++)
    {
        if (touchFingers[i].active && touchFingers[i].id == event->fingerId)
        {
            slot = i;
            break;
        }
        if (slot < 0 && !touchFingers[i].active)
            slot = i;
    }

    if (slot < 0)
        return;

    if (event->type == SDL_FINGERUP)
    {
        touchFingers[slot].active = false;
    }
    else
    {
        touchFingers[slot].id = event->fingerId;
        touchFingers[slot].x = event->x;
        touchFingers[slot].y = event->y;
        touchFingers[slot].active = true;
    }

    UpdateTouchKeys();
}

static const Uint8 *GetGlyph(char character)
{
    static const Uint8 glyphA[7] = {14, 17, 17, 31, 17, 17, 17};
    static const Uint8 glyphB[7] = {30, 17, 17, 30, 17, 17, 30};
    static const Uint8 glyphC[7] = {15, 16, 16, 16, 16, 16, 15};
    static const Uint8 glyphE[7] = {31, 16, 16, 30, 16, 16, 31};
    static const Uint8 glyphL[7] = {16, 16, 16, 16, 16, 16, 31};
    static const Uint8 glyphR[7] = {30, 17, 17, 30, 20, 18, 17};
    static const Uint8 glyphS[7] = {15, 16, 16, 14, 1, 1, 30};
    static const Uint8 glyphT[7] = {31, 4, 4, 4, 4, 4, 4};

    switch (character)
    {
    case 'A': return glyphA;
    case 'B': return glyphB;
    case 'C': return glyphC;
    case 'E': return glyphE;
    case 'L': return glyphL;
    case 'R': return glyphR;
    case 'S': return glyphS;
    case 'T': return glyphT;
    default:  return NULL;
    }
}

static void DrawControlLabel(SDL_Rect rect, const char *label)
{
    int length = SDL_strlen(label);
    int scale = MinInt(rect.h / 9, rect.w / (length * 6));
    if (scale < 1)
        scale = 1;
    int startX = rect.x + (rect.w - (length * 6 - 1) * scale) / 2;
    int startY = rect.y + (rect.h - 7 * scale) / 2;

    SDL_SetRenderDrawColor(sdlRenderer, 255, 255, 255, 230);
    for (int character = 0; character < length; character++)
    {
        const Uint8 *glyph = GetGlyph(label[character]);
        if (glyph == NULL)
            continue;
        for (int row = 0; row < 7; row++)
        {
            for (int column = 0; column < 5; column++)
            {
                if (glyph[row] & (1 << (4 - column)))
                {
                    SDL_Rect pixel = {startX + (character * 6 + column) * scale,
                                      startY + row * scale, scale, scale};
                    SDL_RenderFillRect(sdlRenderer, &pixel);
                }
            }
        }
    }
}

static void DrawControlRect(SDL_Rect rect, bool pressed, const char *label)
{
    SDL_SetRenderDrawColor(sdlRenderer, 255, 255, 255, pressed ? 150 : 65);
    SDL_RenderFillRect(sdlRenderer, &rect);
    SDL_SetRenderDrawColor(sdlRenderer, 255, 255, 255, pressed ? 230 : 130);
    SDL_RenderDrawRect(sdlRenderer, &rect);
    if (label != NULL)
        DrawControlLabel(rect, label);
}

static void DrawTouchControls(void)
{
    int windowWidth;
    int windowHeight;
    SDL_GetWindowSize(sdlWindow, &windowWidth, &windowHeight);
    int sideWidth = GetControlSideWidth(windowWidth, windowHeight);
    int buttonSize = MinInt(sideWidth * 2 / 5, windowHeight / 6);
    int dpadUnit = MinInt(sideWidth / 3, windowHeight / 8);
    int dpadX = sideWidth * 2 / 3;
    int dpadY = windowHeight * 7 / 10;

    SDL_RenderSetLogicalSize(sdlRenderer, 0, 0);
    SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_BLEND);

    DrawControlRect((SDL_Rect){dpadX - dpadUnit / 2, dpadY - dpadUnit * 3 / 2,
                               dpadUnit, dpadUnit}, touchKeys & DPAD_UP, NULL);
    DrawControlRect((SDL_Rect){dpadX - dpadUnit / 2, dpadY + dpadUnit / 2,
                               dpadUnit, dpadUnit}, touchKeys & DPAD_DOWN, NULL);
    DrawControlRect((SDL_Rect){dpadX - dpadUnit * 3 / 2, dpadY - dpadUnit / 2,
                               dpadUnit, dpadUnit}, touchKeys & DPAD_LEFT, NULL);
    DrawControlRect((SDL_Rect){dpadX + dpadUnit / 2, dpadY - dpadUnit / 2,
                               dpadUnit, dpadUnit}, touchKeys & DPAD_RIGHT, NULL);
    DrawControlRect((SDL_Rect){windowWidth - sideWidth / 4 - buttonSize,
                               windowHeight * 58 / 100, buttonSize, buttonSize}, touchKeys & A_BUTTON, "A");
    DrawControlRect((SDL_Rect){windowWidth - sideWidth + sideWidth / 4,
                               windowHeight * 76 / 100, buttonSize, buttonSize}, touchKeys & B_BUTTON, "B");
    DrawControlRect((SDL_Rect){windowWidth - sideWidth * 3 / 4, windowHeight / 4,
                               sideWidth / 2, windowHeight / 10}, touchKeys & START_BUTTON, "START");
    DrawControlRect((SDL_Rect){sideWidth / 4, windowHeight / 4,
                               sideWidth / 2, windowHeight / 10}, touchKeys & SELECT_BUTTON, "SELECT");
    DrawControlRect((SDL_Rect){sideWidth / 4, windowHeight / 20,
                               sideWidth / 2, windowHeight / 10}, touchKeys & L_BUTTON, "L");
    DrawControlRect((SDL_Rect){windowWidth - sideWidth * 3 / 4, windowHeight / 20,
                               sideWidth / 2, windowHeight / 10}, touchKeys & R_BUTTON, "R");

    SDL_SetRenderDrawColor(sdlRenderer, 0, 0, 0, 255);
    SDL_SetRenderDrawBlendMode(sdlRenderer, SDL_BLENDMODE_NONE);
    SDL_RenderSetLogicalSize(sdlRenderer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    SDL_RenderSetIntegerScale(sdlRenderer, SDL_TRUE);
}

static u16 ControllerButtonMask(Uint8 button)
{
    switch (button)
    {
    case SDL_CONTROLLER_BUTTON_A:             return A_BUTTON;
    case SDL_CONTROLLER_BUTTON_B:             return B_BUTTON;
    case SDL_CONTROLLER_BUTTON_BACK:          return SELECT_BUTTON;
    case SDL_CONTROLLER_BUTTON_START:         return START_BUTTON;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  return L_BUTTON;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return R_BUTTON;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:       return DPAD_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:     return DPAD_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:     return DPAD_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:    return DPAD_RIGHT;
    default:                                  return 0;
    }
}
#endif

void ProcessEvents(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            isRunning = false;
            break;
#ifdef __ANDROID__
        case SDL_CONTROLLERDEVICEADDED:
            if (androidController == NULL && SDL_IsGameController(event.cdevice.which))
                androidController = SDL_GameControllerOpen(event.cdevice.which);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (androidController != NULL
             && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(androidController)) == event.cdevice.which)
            {
                SDL_GameControllerClose(androidController);
                androidController = NULL;
                controllerKeys = 0;
                controllerAxisKeys = 0;
                controllerAxisX = 0;
                controllerAxisY = 0;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
            controllerKeys |= ControllerButtonMask(event.cbutton.button);
            break;
        case SDL_CONTROLLERBUTTONUP:
            controllerKeys &= ~ControllerButtonMask(event.cbutton.button);
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX)
                controllerAxisX = event.caxis.value;
            else if (event.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY)
                controllerAxisY = event.caxis.value;

            controllerAxisKeys = 0;
            if (controllerAxisX < -16000) controllerAxisKeys |= DPAD_LEFT;
            if (controllerAxisX >  16000) controllerAxisKeys |= DPAD_RIGHT;
            if (controllerAxisY < -16000) controllerAxisKeys |= DPAD_UP;
            if (controllerAxisY >  16000) controllerAxisKeys |= DPAD_DOWN;
            break;
#endif
        case SDL_KEYUP:
            switch (event.key.keysym.sym)
            {
            HANDLE_KEYUP(A_BUTTON)
            HANDLE_KEYUP(B_BUTTON)
            HANDLE_KEYUP(START_BUTTON)
            HANDLE_KEYUP(SELECT_BUTTON)
            HANDLE_KEYUP(L_BUTTON)
            HANDLE_KEYUP(R_BUTTON)
            HANDLE_KEYUP(DPAD_UP)
            HANDLE_KEYUP(DPAD_DOWN)
            HANDLE_KEYUP(DPAD_LEFT)
            HANDLE_KEYUP(DPAD_RIGHT)
            case SDLK_SPACE:
                if (speedUp)
                {
                    speedUp = false;
                    timeScale = 1.0;
                    SDL_ClearQueuedAudio(sdlAudioDevice);
                    SDL_PauseAudioDevice(sdlAudioDevice, 0);
                }
                break;
            }
            break;
        case SDL_MOUSEMOTION:
#ifdef NATIVE_LINUX
            if (gVoxelModeEnabled) {
                extern bool gVoxelFirstPersonMode;
                if (gVoxelFirstPersonMode) {
                    extern void VoxelCamera_FPSMouseMotion(void *cam, int dx, int dy);
                    // We don't have direct access to sCamera here. Wait, VoxelCamera is in voxel_renderer.
                    // We should add a function to voxel_renderer.c to handle it.
                    extern void VoxelRenderer_HandleMouseMotion(int dx, int dy);
                    VoxelRenderer_HandleMouseMotion(event.motion.xrel, event.motion.yrel);
                }
            }
#endif
            break;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym)
            {
            HANDLE_KEYDOWN(A_BUTTON)
            HANDLE_KEYDOWN(B_BUTTON)
            HANDLE_KEYDOWN(START_BUTTON)
            HANDLE_KEYDOWN(SELECT_BUTTON)
            HANDLE_KEYDOWN(L_BUTTON)
            HANDLE_KEYDOWN(R_BUTTON)
            HANDLE_KEYDOWN(DPAD_UP)
            HANDLE_KEYDOWN(DPAD_DOWN)
            HANDLE_KEYDOWN(DPAD_LEFT)
            HANDLE_KEYDOWN(DPAD_RIGHT)
#ifdef NATIVE_LINUX
            case SDLK_F7:
                if (gVoxelModeEnabled) {
                    extern bool gVoxelFirstPersonMode;
                    gVoxelFirstPersonMode = !gVoxelFirstPersonMode;
                    SDL_SetRelativeMouseMode(gVoxelFirstPersonMode ? SDL_TRUE : SDL_FALSE);
                }
                break;
#endif
            case SDLK_r:
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                {
                    DoSoftReset();
                }
                break;
            case SDLK_p:
                if (event.key.keysym.mod & (KMOD_LCTRL | KMOD_RCTRL))
                {
                    paused = !paused;
                }
                break;
            case SDLK_SPACE:
                if (!speedUp)
                {
                    speedUp = true;
                    timeScale = 5.0;
                    SDL_PauseAudioDevice(sdlAudioDevice, 1);
                }
                break;
            }
            break;
        }
    }
}

#ifdef _WIN32
#define STICK_THRESHOLD 0.5f
u16 GetXInputKeys()
{
    XINPUT_STATE state;
    ZeroMemory(&state, sizeof(XINPUT_STATE));

    DWORD dwResult = XInputGetState(0, &state);
    u16 xinputKeys = 0;

    if (dwResult == ERROR_SUCCESS)
    {
        /* A */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) >> 12;
        /* B */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_X) >> 13;
        /* Start */  xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) >> 1;
        /* Select */ xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) >> 3;
        /* L */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) << 1;
        /* R */      xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) >> 1;
        /* Up */     xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) << 6;
        /* Down */   xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) << 6;
        /* Left */   xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) << 3;
        /* Right */  xinputKeys |= (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) << 1;


        /* Control Stick */
        float xAxis = (float)state.Gamepad.sThumbLX / (float)SHRT_MAX;
        float yAxis = (float)state.Gamepad.sThumbLY / (float)SHRT_MAX;

        if (xAxis < -STICK_THRESHOLD) xinputKeys |= DPAD_LEFT;
        if (xAxis >  STICK_THRESHOLD) xinputKeys |= DPAD_RIGHT;
        if (yAxis < -STICK_THRESHOLD) xinputKeys |= DPAD_DOWN;
        if (yAxis >  STICK_THRESHOLD) xinputKeys |= DPAD_UP;


        /* Speedup */
        // Note: 'speedup' variable is only (un)set on keyboard input
        double oldTimeScale = timeScale;
        timeScale = (state.Gamepad.bRightTrigger > 0x80 || speedUp) ? 5.0 : 1.0;

        if (oldTimeScale != timeScale)
        {
            if (timeScale > 1.0)
            {
                SDL_PauseAudioDevice(sdlAudioDevice, 1);
            }
            else
            {
                SDL_ClearQueuedAudio(sdlAudioDevice);
                SDL_PauseAudioDevice(sdlAudioDevice, 0);
            }
        }
    }

    return xinputKeys;
}
#endif // _WIN32

u16 Platform_GetKeyInput(void)
{
#ifdef _WIN32
    u16 gamepadKeys = GetXInputKeys();
    return gamepadKeys | keyboardKeys | DualScreen_ConsumeVirtualKeys();
#elif defined(__ANDROID__)
    return keyboardKeys | controllerKeys | controllerAxisKeys | DualScreen_ConsumeVirtualKeys();
#endif

    return keyboardKeys | DualScreen_ConsumeVirtualKeys();
}

void VDraw(SDL_Texture *texture)
{
    static uint16_t gbaImage[DISPLAY_WIDTH * DISPLAY_HEIGHT];
    static uint32_t image[DISPLAY_WIDTH * DISPLAY_HEIGHT];

    static int sClassicFrames = 0;
    sClassicFrames++;
    memset(gbaImage, 0, sizeof(gbaImage));
    DrawFrame(gbaImage);
    if (sClassicFrames == 900) {
        printf("[Classic2D] frame 900, p[100]=%04x\n", gbaImage[100]);
    }
    for (int i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++)
    {
        uint16_t color = gbaImage[i];
        uint32_t r = (color & 0x1F) * 255 / 31;
        uint32_t g = ((color >> 5) & 0x1F) * 255 / 31;
        uint32_t b = ((color >> 10) & 0x1F) * 255 / 31;
        image[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
    SDL_UpdateTexture(texture, NULL, image, DISPLAY_WIDTH * sizeof(Uint32));
    REG_VCOUNT = 161; // prep for being in VBlank period
}

int DoMain(void *data)
{
    AgbMain();
    return 0;
}

void VBlankIntrWait(void)
{
    SDL_AtomicSet(&isFrameAvailable, 1);
    SDL_SemWait(vBlankSemaphore);
}

u8 BinToBcd(u8 bin)
{
    int placeCounter = 1;
    u8 out = 0;
    do
    {
        out |= (bin % 10) * placeCounter;
        placeCounter *= 16;
    }
    while ((bin /= 10) > 0);

    return out;
}

void Platform_GetStatus(struct SiiRtcInfo *rtc)
{
    rtc->status = internalClock.status;
}

void Platform_SetStatus(struct SiiRtcInfo *rtc)
{
    internalClock.status = rtc->status;
}

static void UpdateInternalClock(void)
{
    time_t rawTime = time(NULL);
    struct tm *time = localtime(&rawTime);

    internalClock.year = BinToBcd(time->tm_year - 100);
    internalClock.month = BinToBcd(time->tm_mon + 1);
    internalClock.day = BinToBcd(time->tm_mday);
    internalClock.dayOfWeek = BinToBcd(time->tm_wday);
    internalClock.hour = BinToBcd(time->tm_hour);
    internalClock.minute = BinToBcd(time->tm_min);
    internalClock.second = BinToBcd(time->tm_sec);
}

void Platform_GetDateTime(struct SiiRtcInfo *rtc)
{
    UpdateInternalClock();

    rtc->year = internalClock.year;
    rtc->month = internalClock.month;
    rtc->day = internalClock.day;
    rtc->dayOfWeek = internalClock.dayOfWeek;
    rtc->hour = internalClock.hour;
    rtc->minute = internalClock.minute;
    rtc->second = internalClock.second;
    DBGPRINTF("GetDateTime: %d-%02d-%02d %02d:%02d:%02d\n", ConvertBcdToBinary(rtc->year),
                                                         ConvertBcdToBinary(rtc->month),
                                                         ConvertBcdToBinary(rtc->day),
                                                         ConvertBcdToBinary(rtc->hour),
                                                         ConvertBcdToBinary(rtc->minute),
                                                         ConvertBcdToBinary(rtc->second));
}

void Platform_SetDateTime(struct SiiRtcInfo *rtc)
{
    internalClock.month = rtc->month;
    internalClock.day = rtc->day;
    internalClock.dayOfWeek = rtc->dayOfWeek;
    internalClock.hour = rtc->hour;
    internalClock.minute = rtc->minute;
    internalClock.second = rtc->second;
}

void Platform_GetTime(struct SiiRtcInfo *rtc)
{
    UpdateInternalClock();

    rtc->hour = internalClock.hour;
    rtc->minute = internalClock.minute;
    rtc->second = internalClock.second;
    DBGPRINTF("GetTime: %02d:%02d:%02d\n", ConvertBcdToBinary(rtc->hour),
                                        ConvertBcdToBinary(rtc->minute),
                                        ConvertBcdToBinary(rtc->second));
}

void Platform_SetTime(struct SiiRtcInfo *rtc)
{
    internalClock.hour = rtc->hour;
    internalClock.minute = rtc->minute;
    internalClock.second = rtc->second;
}

void Platform_SetAlarm(u8 *alarmData)
{
    // TODO
}

void SoftReset(u32 resetFlags)
{
    puts("Soft Reset called. Exiting.");
    exit(0);
}

#endif
