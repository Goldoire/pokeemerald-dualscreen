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
u32 DualScreen_DebugQueueKeys(u16 keys);

// Implemented in battle_controller_player.c: whether the player-controlled
// battler is currently on the action menu / move menu, and which battler
// that is (-1 if the menu is not open; matters in double battles).
u32 DualScreen_PlayerAtActionSelect(void);
u32 DualScreen_PlayerAtMoveSelect(void);
s32 DualScreen_PlayerActionBattler(void);
s32 DualScreen_PlayerMoveBattler(void);

// ---------------------------------------------------------------------------
// Battle bag/party takeover (Gen 4 style): the bottom screen arms a takeover
// before key-walking the action cursor to BAG or POKeMON; the player battle
// controller then waits for a choice submitted over JNI instead of opening
// the GBA bag/party menu apps, and the battle never leaves the top screen.
// All backing state is runtime-only .bss in dualscreen_bridge.c.
// ---------------------------------------------------------------------------

// Result codes reported back to the bottom screen after a submission.
enum {
    DS_BMENU_RESULT_NONE = 0,
    DS_BMENU_RESULT_USED = 1,        // accepted; menu closed
    DS_BMENU_RESULT_NO_EFFECT = 2,   // "It won't have any effect."
    DS_BMENU_RESULT_CANT_USE = 3,    // blocked (box full, trainer battle, trapped)
    DS_BMENU_RESULT_BAD_TARGET = 4,  // invalid party choice
};

// Implemented in dualscreen_bridge.c: the pending-choice mailbox.
// Consumes the armed takeover intent (1 bag, 2 party); FALSE when not armed
// (or the arm expired), in which case the classic GBA menu must open.
u32 DualScreen_TakeBattleTakeover(u32 mode);
// The controller reports entering/leaving its bottom-screen wait state.
void DualScreen_SetBattleMenuOpen(u32 mode, u32 caseId, u32 battler);
void DualScreen_ClearBattleMenu(void);
// Snapshot access: current wait mode (0/1/2) plus details.
u32 DualScreen_BattleMenuInfo(u32 *caseId, u32 *battler, u32 *result, u32 *seq);
// Polls (and consumes) a choice submitted from the bottom screen.
u32 DualScreen_TakeBattleChoice(s32 *a, s32 *b);
// Reports a rejected submission; the wait state stays open.
void DualScreen_SetBattleMenuResult(u32 result);
// Whether the bottom screen has fetched a snapshot recently (it polls ~8/s
// while its Presentation is actually showing). Flows that install a
// bottom-screen wait with no arming tap (forced send-out) must require this,
// so a desktop build or a stolen bottom display fails safe to the GBA menu.
u32 DualScreen_BottomScreenLive(void);

// Implemented in item_use.c: applies one battle-bag item exactly as the GBA
// bag + party menu would (validate, run the item effect table, consume the
// item, refresh the healthbox), without opening either menu. Returns a
// DS_BMENU_RESULT_* code; on DS_BMENU_RESULT_USED the caller returns the
// item id to the battle engine, whose own scripts (ball throw, "used item"
// message, run-away) then run unchanged.
u32 DualScreen_UseBattleItem(u16 itemId, s32 targetPartyId);

// Implemented in party_menu.c: the battle-order bookkeeping that
// TrySwitchInPokemon performs for a switch choice, for a mon given as a
// field party index.
void DualScreen_BattleSwitchOrder(u8 monId);

#endif // GUARD_PLATFORM_DUALSCREEN_H
