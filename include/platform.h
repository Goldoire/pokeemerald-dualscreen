#ifndef GUARD_PLATFORM_H
#define GUARD_PLATFORM_H

#include "global.h"
#include "siirtc.h"

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
    PLATFORM_SETTING_VOXEL_RENDERER,  // 0 classic 2D, 1 voxel 3D (applies on restart)
    PLATFORM_SETTING_FF_AUDIO,        // 0 music keeps its normal tempo while fast-forwarding, 1 music speeds up too
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
