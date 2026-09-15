#ifndef GUARD_PLATFORM_H
#define GUARD_PLATFORM_H

#include "global.h"
#include "siirtc.h"

// Widescreen rendering geometry.
//
// The GBA viewport is DISPLAY_WIDTH (240) wide and every coordinate the game
// computes stays in that space. Widescreen works by having the software PPU
// render extra columns either side of it and revealing what the game already
// scrolls past, rather than by rescaling anything -- so pixels stay square.
//
// Buffers are sized for the widest mode; gRenderWidth/gRenderMargin pick the
// active geometry at runtime so the setting can be toggled without a rebuild.
// Buffer index 0 holds game-space column -gRenderMargin.
// 24 fills a 16:9 panel: 240 + 48 = 288, which is 1.8:1 and lands within a
// few pixels of 1920x1080. Overworld BG maps are 512px wide so those margin
// columns contain real map tiles rather than wrapped copies of the 240px view.
#define MAX_RENDER_MARGIN 24
#define WIDESCREEN_MARGIN 24
#define MAX_RENDER_WIDTH  (DISPLAY_WIDTH + 2 * MAX_RENDER_MARGIN)

// Layers narrower than the widened frame skip the margin columns entirely, so
// only maps wide enough to hold real content (the overworld's) fill them.
extern int gRenderWidth;
extern int gRenderMargin;

void Platform_StoreSaveFile(void);
void Platform_ReadFlash(u16 sectorNum, u32 offset, u8 *dest, u32 size);
void Platform_QueueAudio(float *audioBuffer, s32 samplesPerFrame);
// TRUE on the game frames that the sound engine has to sit out so that music
// keeps playing at its normal tempo while the game is fast-forwarded.
bool32 Platform_SkipAudioFrame(void);
u16 Platform_GetKeyInput(void);
u8 Platform_GetBorderBackgroundCount(void);
u8 Platform_GetBorderBackground(void);
void Platform_SetBorderBackground(u8 selection);

enum PlatformSetting
{
    PLATFORM_SETTING_FULLSCREEN,
    PLATFORM_SETTING_WINDOW_SCALE,
    PLATFORM_SETTING_INTEGER_SCALE,
    PLATFORM_SETTING_VSYNC,
    PLATFORM_SETTING_BORDER,
    PLATFORM_SETTING_VOLUME,
    PLATFORM_SETTING_BACKGROUND_MODE, // 0 artwork, 1 black, 2 white
    PLATFORM_SETTING_WIDESCREEN,      // 0 aspect-correct, 1 stretch to fill
    PLATFORM_SETTING_TOUCH_CONTROLS,  // 0 hidden, 1 shown (Android)
    PLATFORM_SETTING_BATTLE_UI_TOP,   // 0 battle menus on bottom screen, 1 classic top
    PLATFORM_SETTING_FAST_FORWARD,    // 0 off, 1..3 = 2x..4x game speed
    PLATFORM_SETTING_VOXEL_RENDERER,  // 0 classic 2D, 1 voxel 3D overworld (applies on restart)
    PLATFORM_SETTING_FF_AUDIO,        // 0 music keeps its normal tempo while fast-forwarding, 1 music speeds up too
    PLATFORM_SETTING_BATTLE_HINTS,    // 0 off, 1 effectiveness carets on the move grid + foe weakness strip
    PLATFORM_SETTING_COUNT,
};

u8 Platform_GetSetting(enum PlatformSetting setting);
void Platform_SetSetting(enum PlatformSetting setting, u8 value);
void Platform_GetStatus(struct SiiRtcInfo *rtc);
void Platform_SetStatus(struct SiiRtcInfo *rtc);
static void UpdateInternalClock(void);
void Platform_GetDateTime(struct SiiRtcInfo *rtc);
void Platform_SetDateTime(struct SiiRtcInfo *rtc);
void Platform_GetTime(struct SiiRtcInfo *rtc);
void Platform_SetTime(struct SiiRtcInfo *rtc);
void Platform_SetAlarm(u8 *alarmData);

#endif
