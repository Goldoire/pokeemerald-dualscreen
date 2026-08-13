#ifndef GUARD_PLATFORM_DUALSCREEN_H
#define GUARD_PLATFORM_DUALSCREEN_H

// Second-screen state bridge. Called from the SDL frame loop each vblank,
// after the game's vblank interrupt handler has run and before the game
// thread is released. Snapshots live game state into a JSON buffer that the
// Android bottom-screen UI (or a desktop debug consumer) reads.
void DualScreen_FrameHook(void);

// Returns the most recent snapshot as a NUL-terminated JSON string.
// Thread-safe; may be called from any thread.
const char *DualScreen_GetSnapshotJson(void);

#ifdef __ANDROID__
// Restores zeroed asset ranges from the user's extracted ROM data before
// the game starts (no-op in development builds). See make_asset_holes.py.
void DualScreen_FillAssets(const char *prefPath);
#endif

// True when the bottom screen owns the battle menus: the top screen then
// suppresses the action/move menu text and cursor, and input can arrive
// through the virtual key queue.
u32 DualScreen_BattleUiActive(void);

// One frame's worth of synthetic GBA button state, consumed by
// Platform_GetKeyInput. Returns 0 when the queue is empty.
u16 DualScreen_ConsumeVirtualKeys(void);

// Implemented in battle_controller_player.c: whether the player-controlled
// battler is currently on the action menu / move menu, and which battler
// that is (-1 if the menu is not open; matters in double battles).
u32 DualScreen_PlayerAtActionSelect(void);
u32 DualScreen_PlayerAtMoveSelect(void);
s32 DualScreen_PlayerActionBattler(void);
s32 DualScreen_PlayerMoveBattler(void);

#endif // GUARD_PLATFORM_DUALSCREEN_H
