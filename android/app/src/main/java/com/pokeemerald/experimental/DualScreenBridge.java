package com.pokeemerald.experimental;

/**
 * JNI boundary to the in-process game (libmain.so). The game code fills a
 * JSON snapshot of its state once per frame window; these calls read it.
 */
public final class DualScreenBridge {
    private DualScreenBridge() {}

    /** Latest game-state snapshot as JSON. Never null once the game has booted. */
    public static native String nativeGetSnapshotJson();

    /**
     * The battle menu cursor on its own, republished every frame so the
     * bottom screen's cursor ring can follow a d-pad press instead of
     * trailing the snapshot. Packed (menu &lt;&lt; 16) | (action &lt;&lt; 8)
     * | move, where menu is 1 action select / 2 move select; -1 when no
     * bottom-screen battle menu is open.
     */
    public static native int nativeGetBattleCursor();

    /**
     * One queued button press for an open battle takeover panel, or -1 when
     * the queue is empty. Drain in a loop. Values match DualScreenView's
     * NAV_* constants. These are sampled from the game's own input on the
     * frame thread, which is the path that carries the device's controller
     * however it reports its buttons.
     */
    public static native int nativeDrainNavKey();

    /**
     * Renders a 32x32 party icon frame (0 or 1) for a species into
     * ARGB_8888 pixels (row-major, length 32*32), decoded from the game's
     * own icon data. Returns null for invalid species.
     */
    public static native int[] nativeGetMonIcon(int species, int frame);

    /**
     * The game's region map location table as a JSON array of
     * {id, x, y, w, h, n} on the 28x15 region grid. Static data; fetch once.
     */
    public static native String nativeGetRegionMapJson();

    /**
     * The real Pokenav Hoenn map as 240x160 ARGB_8888 pixels, composed from
     * the game's own tileset. The location grid starts at tile (1, 2).
     */
    public static native int[] nativeGetRegionMapImage();

    /**
     * The game's normal Latin font: 256 ints of glyph advance widths,
     * followed by 256 glyphs * 256 (16x16) color indices
     * (0 transparent, 1 foreground, 2 shadow). Static data; fetch once.
     */
    public static native int[] nativeGetFontAtlas();

    /**
     * Enqueue synthetic GBA button states, one array entry per frame
     * (0 releases all). Masks: A=1 B=2 SELECT=4 START=8 RIGHT=16 LEFT=32
     * UP=64 DOWN=128 R=256 L=512.
     */
    public static native void nativeQueueKeys(int[] frameMasks);

    /** Debug: warp the player to map group/num at (x, y) on the next overworld frame. */
    public static native void nativeDebugWarp(int group, int num, int x, int y);

    /** Debug: voxel renderer switches (1 x-ray, 2 ghost, 4 tilt, 8 shadows, 16 overlay, 32 live atlas; set = off). */
    public static native void nativeSetVoxelDebugFlags(int flags);

    // Platform setting indices (see include/platform.h).
    public static final int SETTING_BACKGROUND_MODE = 6;
    public static final int SETTING_WIDESCREEN = 7;
    public static final int SETTING_TOUCH_CONTROLS = 8;
    public static final int SETTING_BATTLE_UI_TOP = 9;
    public static final int SETTING_FAST_FORWARD = 10;
    public static final int SETTING_VOXEL_RENDERER = 11;
    public static final int SETTING_FF_AUDIO = 12;
    public static final int SETTING_BATTLE_HINTS = 13;
    public static final int SETTING_VOLUME = 5;

    /**
     * Renders a bag item's 24x24 icon into ARGB_8888 pixels (row-major,
     * length 24*24), decoded from the game's own icon data. Returns null
     * for ITEM_NONE or out-of-range ids.
     */
    public static native int[] nativeGetItemIcon(int itemId);

    /**
     * The in-game description text for an item, decoded to a single line
     * (callers re-wrap to fit). Returns null for invalid ids.
     */
    public static native String nativeGetItemDescription(int itemId);

    /**
     * A party menu slot box as ARGB pixels, composed from the game's own
     * tileset/tilemaps. Kinds: 0 main (80x56), 1 main no-HP for eggs,
     * 2 wide (144x24), 3 wide no-HP, 4 wide empty. Pass fainted=1 for the
     * game's fainted recolor. Static data; fetch once per (kind, fainted).
     */
    public static native int[] nativeGetPartySlot(int kind, int fainted);

    /**
     * The party menu's full 240x160 background layer as ARGB_8888 pixels
     * (row-major), composed from the game's own tileset/tilemap/palette
     * (graphics/party_menu/bg.*). Static data; fetch once.
     */
    public static native int[] nativeGetPartyBgImage();

    /**
     * The party menu status tags: 8 x 32x8 ARGB pixels, tag-major, in
     * sheet order PSN, PAR, SLP, FRZ, BRN, PKRS, FNT, blank.
     */
    public static native int[] nativeGetStatusIcons();

    /** The party menu's held-item marks: 2 x 8x8 ARGB (item, mail). */
    public static native int[] nativeGetHoldIcons();

    /** All 8 badge sprites: 8 x 16x16 ARGB pixels, badge-major. */
    public static native int[] nativeGetBadges();

    /**
     * The summary screen's move-type icons: 18 types x 32x16 ARGB pixels,
     * type-major, decoded from the game's own sheet with the palette each
     * type uses in the summary screen. Static data; fetch once.
     */
    public static native int[] nativeGetTypeIcons();

    /** The player's 64x64 trainer front pic (gender 0 = Brendan, 1 = May). */
    public static native int[] nativeGetTrainerPic(int gender);

    /** Fills asset holes from files/baserom.gba; called by the ROM gate. */
    public static native void nativeFillAssets(String filesDir);

    public static native int nativeGetPlatformSetting(int setting);

    /** Persists to the port's config file. */
    public static native void nativeSetPlatformSetting(int setting, int value);

    /**
     * Arms the bottom-screen battle takeover right before key-walking the
     * action cursor to BAG (mode 1) or POKéMON (mode 2): the player battle
     * controller then waits for {@link #nativeBattleSubmit} instead of
     * opening the GBA bag/party menu, and the battle stays on the top
     * screen. Mode 0 disarms; an arm expires on its own after ~4 seconds.
     */
    public static native void nativeBattleArm(int mode);

    /**
     * Submits the pending battle choice. Bag wait: a = item id, b = target
     * party slot (-1 when the item needs no target). Party wait: a = party
     * slot. a = -1 cancels back to the action menu.
     */
    public static native void nativeBattleSubmit(int a, int b);

    /**
     * How an item can be used from the battle bag: 0 not usable, 1 ball,
     * 2 medicine (pick a target mon), 3 self/no target (X items),
     * 4 escape item (wild battles only), 5 PP restore (pick a target mon).
     * Static per item id; cacheable.
     */
    public static native int nativeGetItemBattleCategory(int itemId);
}
