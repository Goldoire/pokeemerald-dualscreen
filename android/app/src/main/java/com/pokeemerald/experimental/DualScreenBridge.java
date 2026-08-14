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

    // Platform setting indices (see include/platform.h).
    public static final int SETTING_BACKGROUND_MODE = 6;
    public static final int SETTING_WIDESCREEN = 7;
    public static final int SETTING_TOUCH_CONTROLS = 8;
    public static final int SETTING_BATTLE_UI_TOP = 9;
    public static final int SETTING_FAST_FORWARD = 10;
    public static final int SETTING_VOXEL_RENDERER = 11;
    public static final int SETTING_FF_AUDIO = 12;
    public static final int SETTING_VOLUME = 5;

    /** All 8 badge sprites: 8 x 16x16 ARGB pixels, badge-major. */
    public static native int[] nativeGetBadges();

    /** The player's 64x64 trainer front pic (gender 0 = Brendan, 1 = May). */
    public static native int[] nativeGetTrainerPic(int gender);

    /** Fills asset holes from files/baserom.gba; called by the ROM gate. */
    public static native void nativeFillAssets(String filesDir);

    public static native int nativeGetPlatformSetting(int setting);

    /** Persists to the port's config file. */
    public static native void nativeSetPlatformSetting(int setting, int value);
}
