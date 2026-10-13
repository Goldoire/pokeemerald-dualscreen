package com.pokeemerald.experimental;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;

/**
 * Bottom-screen UI, styled after the game's own Pokenav: dotted mint
 * background, cream menu bars, and the game's real font and graphics
 * (all decoded at runtime from game data over the bridge).
 */
public final class DualScreenView extends View {
    public static final int TAB_PARTY = 0;
    public static final int TAB_MAP = 1;
    public static final int TAB_BAG = 2;
    public static final int TAB_CARD = 3;
    public static final int TAB_SETTINGS = 4;
    private static final String[] TAB_NAMES = {"PARTY", "MAP", "BAG", "CARD", null}; // null = cog icon

    private static final String[] TYPE_NAMES = {
        "NORMAL", "FIGHT", "FLYING", "POISON", "GROUND", "ROCK", "BUG", "GHOST",
        "STEEL", "???", "FIRE", "WATER", "GRASS", "ELECTR", "PSYCHC", "ICE",
        "DRAGON", "DARK"
    };
    private static final int[] TYPE_COLORS = {
        0xFFA8A878, 0xFFC03028, 0xFFA890F0, 0xFFA040A0, 0xFFE0C068, 0xFFB8A038,
        0xFFA8B820, 0xFF705898, 0xFFB8B8D0, 0xFF68A090, 0xFFF08030, 0xFF6890F0,
        0xFF78C850, 0xFFF8D030, 0xFFF85888, 0xFF98D8D8, 0xFF7038F8, 0xFF705848
    };

    // Pokenav palette.
    private static final int BG_MINT = 0xFFD8F8E8;
    private static final int BG_DOT = 0xFFB8E4CE;
    private static final int HEADER_GREEN = 0xFF50C484;
    private static final int HEADER_GREEN_DARK = 0xFF2E9A62;
    // Repaint pace while a battle menu is open, so the cursor ring tracks the
    // d-pad rather than the snapshot pump.
    private static final int CURSOR_REPAINT_MS = 33;
    // How long a closed menu is held on screen before the idle cards take
    // over, to ride out the gap between one battle menu and the next.
    private static final int MENU_HOLD_MS = 180;
    private int lastLiveMenu;
    private long lastLiveMenuMs;
    private int lastPressCount = -1;   // native real-button-press counter
    private boolean showCursor;        // last battle input was buttons, not touch
    private static final int BAR_CREAM = 0xFFF8F0B0;
    private static final int BAR_CREAM_DARK = 0xFFE8CE7A;
    private static final int BAR_BORDER = 0xFFA88848;
    private static final int PANEL_WHITE = 0xFFFFFFFF;
    private static final int PANEL_BORDER = 0xFF58585A;
    private static final int TEXT_DARK = 0xFF484850;
    private static final int TEXT_SHADOW = 0xFFD0D0C8;
    private static final int TEXT_WHITE = 0xFFFFFFFF;
    private static final int TEXT_GREEN_SHADOW = 0xFF2E9A62;
    private static final int SEA_BLUE = 0xFF9CC7E8;
    private static final int HP_GREEN = 0xFF58D080;
    private static final int HP_YELLOW = 0xFFF8B050;
    private static final int HP_RED = 0xFFF05868;

    // Party menu palette: bank 3 of the game's party bg palette, plus the
    // gender and HP bar recolors the game loads into it (data/party_menu.h).
    private static final int PARTY_TEXT = 0xFFF8F8F8;
    private static final int PARTY_TEXT_SHADOW = 0xFF707070;
    private static final int PARTY_MALE = 0xFF40C8F8;
    private static final int PARTY_MALE_SHADOW = 0xFF006090;
    private static final int PARTY_FEMALE = 0xFFF89890;
    private static final int PARTY_FEMALE_SHADOW = 0xFF984038;
    private static final int[] PARTY_HP_TOP = {0xFF70F8A8, 0xFFF8E038, 0xFFF87030};
    private static final int[] PARTY_HP_BODY = {0xFF58D080, 0xFFC8A808, 0xFFC03800};
    // The party slots' blue box gradient, reused for the summary-style detail view.
    private static final int SUMMARY_BLUE = 0xFF3890D8;
    private static final int SUMMARY_BLUE_LIGHT = 0xFF80C0D8;
    private static final int SUMMARY_BLUE_DARK = 0xFF2878B0;

    // Content backdrops sampled from the GBA screens' own bg art: the bag
    // screen's slate wallpaper (graphics/bag/menu.png), plus the party
    // menu's gray-green tones (graphics/party_menu/bg.png) as a fallback
    // when the real decoded bg layer is unavailable. The mint Pokenav
    // chrome only frames the content (tab bar).
    private static final int PARTY_BG_BASE = 0xFF7B9C73;
    private static final int PARTY_BG_WEAVE = 0xFF4A4A62;
    private static final int BAG_BG_BASE = 0xFF626273;
    private static final int BAG_BG_WEAVE = 0xFF293941;
    private static final int LIST_PANEL = 0xFFF8F8F0; // bag's light list window

    private static final class MapEntry {
        int id, x, y, w, h;
        String name = "";
    }

    // GBA button masks for the virtual key queue.
    private static final int KEY_A = 1;
    private static final int KEY_B = 2;
    private static final int KEY_RIGHT = 16;
    private static final int KEY_LEFT = 32;
    private static final int KEY_UP = 64;
    private static final int KEY_DOWN = 128;

    private final Paint paint = new Paint();
    private final Paint pixelPaint = new Paint();
    private final SparseArray<Bitmap> iconCache = new SparseArray<>();
    private final SparseArray<Bitmap> itemIconCache = new SparseArray<>();
    private final SparseArray<String> itemDescCache = new SparseArray<>();
    private final SparseArray<Bitmap> partySlotCache = new SparseArray<>();
    private Bitmap[] statusIcons;
    private Bitmap[] holdIcons;
    private Bitmap[] typeIcons;
    private DualScreenState state = new DualScreenState();
    private int tab = TAB_PARTY;
    private int bagPocket;
    private float bagScroll;
    private float bagTouchDownY;
    private float bagScrollStart;
    private boolean bagDragging;
    private final int[] bagSelected = new int[5]; // remembered per pocket
    // List geometry from the last draw, for touch hit-testing.
    private float bagListTop;
    private float bagListBottom;
    private float bagRowH = 1;
    private int detailMon = -1; // party index shown in the detail view, -1 = grid
    private final RectF[] partyCards = {new RectF(), new RectF(), new RectF(),
                                        new RectF(), new RectF(), new RectF()};
    private java.util.List<MapEntry> mapEntries;
    private Bitmap regionMap;
    private Bitmap partyBg;
    private final RectF[] battleButtons = {new RectF(), new RectF(), new RectF(), new RectF()};
    private final RectF battleCancel = new RectF();
    private int battleButtonsMenu; // which menu the drawn buttons belong to
    private long lastKeyQueueMs;
    private int battlePressedCmd = -1; // command button in its pressed-in state
    private final Runnable battlePressReset = new Runnable() {
        @Override
        public void run() {
            battlePressedCmd = -1;
            invalidate();
        }
    };

    // Battle bag/party takeover (Gen 4-style bottom-screen menus).
    private int battlePanel; // 0 none, 1 bag, 2 switch party, 3 item-target party
    private int pendingItemId;
    private long battleArmMs;    // when we last armed the takeover
    private long battleCloseMs;  // when we last cancelled/submitted a close
    private int lastSubSeq;
    private String battleNotice = "";
    private long battleNoticeMs;
    private int battleBagPocket; // index into battleVisiblePockets()
    private float battleBagScroll;
    private float battleBagTouchDownY;
    private float battleBagScrollStart;
    private boolean battleBagDragging;
    private int battleBagSelected = -1;
    private float battleBagListTop;
    private float battleBagListBottom;
    private float battleBagRowH = 1;
    private final RectF battleUseButton = new RectF();
    private final RectF[] battlePartyCards = {new RectF(), new RectF(), new RectF(),
                                              new RectF(), new RectF(), new RectF()};
    private int battlePartyFocus = -1; // staircase slot under the button cursor
    private int lastBattlePanel;       // to reset that focus when the panel changes
    private final SparseArray<Integer> battleCategoryCache = new SparseArray<>();

    // Directions and actions the physical buttons can send down here.
    public static final int NAV_UP = 0;
    public static final int NAV_DOWN = 1;
    public static final int NAV_LEFT = 2;
    public static final int NAV_RIGHT = 3;
    public static final int NAV_CONFIRM = 4;
    public static final int NAV_CANCEL = 5;

    public DualScreenView(Context context) {
        super(context);
        pixelPaint.setFilterBitmap(false);
    }

    private Runnable settingsListener;

    public void setSettingsListener(Runnable listener) {
        settingsListener = listener;
    }

    public void setState(DualScreenState next) {
        state = next;
        syncBattlePanel();
        invalidate();
    }

    /**
     * Whether the physical buttons belong to this screen right now. Only the
     * battle takeover panels claim them: these are our own menus, and while
     * one is open the engine is parked in a wait handler that reads no input,
     * so nothing is competing for the buttons. Every other screen leaves them
     * to the game.
     */
    public boolean isCapturingKeys() {
        return state.inBattle && battlePanel != 0;
    }

    /**
     * One button press routed from the activity. Movement updates the panel's
     * own cursor; confirm and cancel are lowered to a tap at the focused
     * rect, so the button and touch paths run exactly the same code.
     */
    public void navigate(int action) {
        if (!isCapturingKeys()) {
            return;
        }
        showCursor = true;
        switch (action) {
        case NAV_UP: case NAV_DOWN: case NAV_LEFT: case NAV_RIGHT:
            if (battlePanel == 1) {
                navBattleBag(action);
            } else {
                navBattleParty(action);
            }
            break;
        case NAV_CONFIRM:
            if (battlePanel == 1) {
                if (!battleUseButton.isEmpty()) {
                    handleBattleBagTouch(battleUseButton.centerX(), battleUseButton.centerY());
                }
            } else if (battlePartyFocus >= 0 && battlePartyFocus < state.party.size()) {
                RectF card = battlePartyCards[battlePartyFocus];
                handleBattlePartyTouch(card.centerX(), card.centerY());
            }
            break;
        case NAV_CANCEL:
            if (battlePanel == 1) {
                cancelBattlePanel();
            } else if (!battleCancel.isEmpty()) {
                // The party panel's own back handling: panel 3 returns to the
                // bag, panel 2 cancels. A forced send-out leaves this rect
                // empty, and then there is deliberately no way out.
                handleBattlePartyTouch(battleCancel.centerX(), battleCancel.centerY());
            }
            break;
        default:
            return;
        }
        invalidate();
    }

    /** Bag panel: up/down walk the item list, left/right change pocket. */
    private void navBattleBag(int action) {
        if (action == NAV_LEFT || action == NAV_RIGHT) {
            int count = battleVisiblePockets().length;
            if (count > 0) {
                battleBagPocket = (battleBagPocket + (action == NAV_RIGHT ? 1 : count - 1)) % count;
                battleBagScroll = 0;
                battleBagSelected = -1;
            }
            return;
        }
        java.util.List<DualScreenState.BagItem> items = battleBagItems();
        if (items.isEmpty()) {
            return;
        }
        int next = battleBagSelected < 0 ? 0
                : battleBagSelected + (action == NAV_DOWN ? 1 : -1);
        battleBagSelected = Math.max(0, Math.min(items.size() - 1, next));
        // Keep the selection on screen; the list is a uniform-row scroller.
        float rowTop = battleBagSelected * battleBagRowH;
        float viewH = battleBagListBottom - battleBagListTop;
        if (rowTop < battleBagScroll) {
            battleBagScroll = rowTop;
        } else if (rowTop + battleBagRowH > battleBagScroll + viewH) {
            battleBagScroll = rowTop + battleBagRowH - viewH;
        }
        battleBagScroll = Math.max(0, Math.min(battleBagMaxScroll(), battleBagScroll));
    }

    /**
     * Party panel: a spatial walk over the staircase rects the draw pass
     * already produced, so the lead slot's odd geometry needs no special
     * case and the nav cannot drift out of sync with the layout. Slots the
     * panel would refuse anyway are skipped rather than focused.
     */
    private void navBattleParty(int action) {
        int count = Math.min(battlePartyCards.length, state.party.size());
        if (count == 0) {
            return;
        }
        if (battlePartyFocus < 0 || battlePartyFocus >= count) {
            battlePartyFocus = firstEnabledPartySlot(count);
            return;
        }
        RectF from = battlePartyCards[battlePartyFocus];
        int best = -1;
        float bestScore = Float.MAX_VALUE;
        for (int i = 0; i < count; i++) {
            if (i == battlePartyFocus || !battlePartySlotEnabled(i)) {
                continue;
            }
            RectF to = battlePartyCards[i];
            float dx = to.centerX() - from.centerX();
            float dy = to.centerY() - from.centerY();
            boolean ahead = action == NAV_UP ? dy < -1 : action == NAV_DOWN ? dy > 1
                    : action == NAV_LEFT ? dx < -1 : dx > 1;
            if (!ahead) {
                continue;
            }
            boolean vertical = action == NAV_UP || action == NAV_DOWN;
            // Distance along the direction asked for, with drift across it
            // weighted heavier so a near-straight neighbour always wins.
            float score = Math.abs(vertical ? dy : dx)
                    + Math.abs(vertical ? dx : dy) * 2f;
            if (score < bestScore) {
                bestScore = score;
                best = i;
            }
        }
        if (best >= 0) {
            battlePartyFocus = best;
        }
    }

    private int firstEnabledPartySlot(int count) {
        for (int i = 0; i < count; i++) {
            if (battlePartySlotEnabled(i)) {
                return i;
            }
        }
        return -1;
    }

    /** Reconciles the local battle bag/party panel with the engine's wait state. */
    private void syncBattlePanel() {
        if (!state.inBattle) {
            battlePanel = 0;
            return;
        }
        // The sequence bumps on every wait the engine opens as well as on
        // every result, so a bump carrying no result is a brand new menu -
        // which is what tells a fresh open apart from the wait we just closed
        // and can still see in a snapshot that has not caught up yet.
        boolean freshWait = false;
        if (state.battleSubSeq != lastSubSeq) {
            lastSubSeq = state.battleSubSeq;
            freshWait = state.battleSubResult == 0;
            switch (state.battleSubResult) {
            case 2:  battleNotice = "It won't have any effect."; break;
            case 3:  battleNotice = "It can't be used now."; break;
            case 4:  battleNotice = "Can't choose that POKéMON."; break;
            default: battleNotice = ""; break;
            }
            if (!battleNotice.isEmpty()) {
                battleNoticeMs = System.currentTimeMillis();
            }
        }
        long now = System.currentTimeMillis();
        if (battlePanel == 0 && state.battleSub != 0
                && (freshWait || now - battleCloseMs > 1500 || battleSendOutWait())) {
            // The engine is waiting on us but no panel is up: a menu chosen
            // with the physical buttons (no tap sets the panel up front), a
            // reopen after a race, or a forced send-out (a fainted mon needs
            // a replacement), which arrives with no arming tap and opens the
            // party staircase proactively - immediately, so the second
            // replacement in a doubles double-faint isn't delayed by the
            // battleCloseMs stamp of the first. freshWait is what lets a
            // button-chosen menu reopen right after a cancel instead of
            // waiting out the stale-snapshot guard.
            battlePanel = state.battleSub == 1 ? 1 : 2;
        } else if (battleSendOutWait() && (battlePanel == 1 || battlePanel == 3)) {
            // A stale bag panel must never sit on a send-out wait: with
            // cancel disabled there would be no way out of it.
            battlePanel = 2;
        } else if (battlePanel != 0 && state.battleSub == 0
                && (now - battleArmMs > 2500 || now - battleCloseMs < 1500)) {
            // The choice landed, was cancelled, or the takeover never engaged.
            // (battleCloseMs is stamped on every submission, so an accepted
            // choice closes the panel as soon as the wait state clears.)
            battlePanel = 0;
        }
        if (battlePanel != lastBattlePanel) {
            lastBattlePanel = battlePanel;
            battlePartyFocus = -1; // a fresh panel starts on its first legal slot
        }
    }

    private GbaFont font() {
        return GbaFont.get();
    }

    private Bitmap monIcon(int species) {
        // Alternate the icon's two animation frames, like the party menu.
        int frame = (int) ((System.currentTimeMillis() / 300) % 2);
        int key = species * 2 + frame;
        Bitmap cached = iconCache.get(key);
        if (cached != null) {
            return cached;
        }
        int[] pixels = DualScreenBridge.nativeGetMonIcon(species, frame);
        if (pixels == null || pixels.length != 32 * 32) {
            return null;
        }
        Bitmap bitmap = Bitmap.createBitmap(pixels, 32, 32, Bitmap.Config.ARGB_8888);
        iconCache.put(key, bitmap);
        return bitmap;
    }

    private Bitmap itemIcon(int itemId) {
        Bitmap cached = itemIconCache.get(itemId);
        if (cached != null) {
            return cached;
        }
        int[] pixels = DualScreenBridge.nativeGetItemIcon(itemId);
        if (pixels == null || pixels.length != 24 * 24) {
            return null;
        }
        Bitmap bitmap = Bitmap.createBitmap(pixels, 24, 24, Bitmap.Config.ARGB_8888);
        itemIconCache.put(itemId, bitmap);
        return bitmap;
    }

    private String itemDescription(int itemId) {
        String cached = itemDescCache.get(itemId);
        if (cached != null) {
            return cached;
        }
        String desc = DualScreenBridge.nativeGetItemDescription(itemId);
        if (desc == null) {
            desc = "";
        }
        itemDescCache.put(itemId, desc);
        return desc;
    }

    /**
     * A party slot box bitmap; kinds as in nativeGetPartySlot. pal selects
     * the palette: 0 normal, 1 fainted/dimmed, 2 the no-mon (empty) remap.
     */
    private Bitmap partySlot(int kind, int pal) {
        int key = kind * 4 + pal;
        Bitmap cached = partySlotCache.get(key);
        if (cached != null) {
            return cached;
        }
        int w = kind <= 1 ? 80 : 144;
        int h = kind <= 1 ? 56 : 24;
        int[] pixels = DualScreenBridge.nativeGetPartySlot(kind, pal);
        if (pixels == null || pixels.length != w * h) {
            return null;
        }
        Bitmap bitmap = Bitmap.createBitmap(pixels, w, h, Bitmap.Config.ARGB_8888);
        partySlotCache.put(key, bitmap);
        return bitmap;
    }

    /**
     * The box behind a party slot: the game's own tile art, with a flat
     * rounded card as fallback when the tile art is unavailable. pal 1
     * covers both fainted and untappable slots, pal 2 the empty ones.
     */
    private void drawSlotChrome(Canvas canvas, RectF slot, int kind, int pal) {
        Bitmap slotGfx = partySlot(kind, pal);
        if (slotGfx != null) {
            // Empty bays fade back so occupied cards carry the screen.
            if (pal == 2) pixelPaint.setAlpha(110);
            canvas.drawBitmap(slotGfx, null, slot, pixelPaint);
            pixelPaint.setAlpha(255);
            return;
        }
        paint.setColor(kind == 4 || pal == 2 ? 0x50FFFFFF : pal == 1 ? 0xFF8894A0 : SUMMARY_BLUE);
        canvas.drawRoundRect(slot, 10, 10, paint);
    }

    /** One of the game's 32x8 status tags (PSN, PAR, SLP, FRZ, BRN, PKRS, FNT). */
    private Bitmap statusIcon(int index) {
        if (statusIcons == null) {
            int[] pixels = DualScreenBridge.nativeGetStatusIcons();
            if (pixels == null || pixels.length != 8 * 32 * 8) {
                return null;
            }
            statusIcons = new Bitmap[8];
            for (int i = 0; i < 8; i++) {
                int[] one = new int[32 * 8];
                System.arraycopy(pixels, i * 32 * 8, one, 0, 32 * 8);
                statusIcons[i] = Bitmap.createBitmap(one, 32, 8, Bitmap.Config.ARGB_8888);
            }
        }
        return index >= 0 && index < 8 ? statusIcons[index] : null;
    }

    /** The game's 8x8 held-item mark (0) or mail mark (1). */
    private Bitmap holdIcon(int index) {
        if (holdIcons == null) {
            int[] pixels = DualScreenBridge.nativeGetHoldIcons();
            if (pixels == null || pixels.length != 2 * 8 * 8) {
                return null;
            }
            holdIcons = new Bitmap[2];
            for (int i = 0; i < 2; i++) {
                int[] one = new int[64];
                System.arraycopy(pixels, i * 64, one, 0, 64);
                holdIcons[i] = Bitmap.createBitmap(one, 8, 8, Bitmap.Config.ARGB_8888);
            }
        }
        return index >= 0 && index < 2 ? holdIcons[index] : null;
    }

    /** One of the game's 32x16 move-type icons, or null if unavailable. */
    private Bitmap typeIconBitmap(int type) {
        if (typeIcons == null) {
            int[] pixels = DualScreenBridge.nativeGetTypeIcons();
            if (pixels == null || pixels.length != 18 * 32 * 16) {
                return null;
            }
            typeIcons = new Bitmap[18];
            for (int i = 0; i < 18; i++) {
                int[] one = new int[32 * 16];
                System.arraycopy(pixels, i * 32 * 16, one, 0, 32 * 16);
                typeIcons[i] = Bitmap.createBitmap(one, 32, 16, Bitmap.Config.ARGB_8888);
            }
        }
        return type >= 0 && type < 18 ? typeIcons[type] : null;
    }

    /**
     * Draws the game's own type icon (2:1 aspect, so width is 2x height),
     * falling back to the old text chip if the graphics are unavailable.
     */
    private void drawTypeIcon(Canvas canvas, int type, float left, float top, float height) {
        Bitmap icon = typeIconBitmap(type);
        if (icon == null) {
            drawTypeBadge(canvas, type, left, top, height);
            return;
        }
        canvas.drawBitmap(icon, null,
                new RectF(left, top, left + height * 2, top + height), pixelPaint);
    }

    // A 7x4-unit pixel caret, row bitmaps top to bottom (up orientation).
    private static final int[][] CHEVRON_ROWS = {
            {3}, {2, 3, 4}, {1, 2, 4, 5}, {0, 1, 5, 6}};
    private static final int[][] CROSS_ROWS = {
            {0, 4}, {1, 3}, {2}, {1, 3}, {0, 4}};

    private void drawPixelRows(Canvas canvas, int[][] rows, boolean flip,
            float left, float top, float u, int color) {
        // GBA-style drop shadow first, then the glyph, in unit squares.
        for (int pass = 0; pass < 2; pass++) {
            float off = pass == 0 ? u : 0;
            paint.setColor(pass == 0 ? 0x55101820 : color);
            for (int ry = 0; ry < rows.length; ry++) {
                int[] cols = rows[flip ? rows.length - 1 - ry : ry];
                for (int col : cols) {
                    canvas.drawRect(left + col * u + off, top + ry * u + off,
                            left + (col + 1) * u + off, top + (ry + 1) * u + off, paint);
                }
            }
        }
    }

    /**
     * Effectiveness marker on a move card, as GBA-style pixel carets:
     * one up = effective (2x), two up = super (4x), one down = not very
     * (1/2x), two down = quarter (1/4x), pixel cross = no effect; nothing
     * for neutral or no data. eff carries the multiplier tier (see
     * DualScreenState.Move.eff).
     */
    private void drawEffMarker(Canvas canvas, int eff, float cx, float cy, float r) {
        if (eff < 0 || eff == 3 || eff > 5) {
            return;
        }
        if (DualScreenBridge.nativeGetPlatformSetting(DualScreenBridge.SETTING_BATTLE_HINTS) == 0) {
            return; // hints are opt-in (SET tab)
        }
        float u = Math.max(1f, Math.round(r / 2f));
        if (eff == 0) {
            drawPixelRows(canvas, CROSS_ROWS, false, cx - 2.5f * u, cy - 2.5f * u, u, 0xFF888E96);
            return;
        }
        boolean up = eff >= 4;
        int count = (eff == 5 || eff == 1) ? 2 : 1;
        int color = up ? 0xFFE86828 : 0xFF5888D8;
        float totalH = count * 4 * u + (count - 1) * u;
        float top = cy - totalH / 2f;
        for (int c = 0; c < count; c++) {
            drawPixelRows(canvas, CHEVRON_ROWS, !up, cx - 3.5f * u, top + c * 5 * u, u, color);
        }
    }

    private static int blendColor(int a, int b, float t) {
        int ar = (a >> 16) & 255, ag = (a >> 8) & 255, ab = a & 255;
        int br = (b >> 16) & 255, bg = (b >> 8) & 255, bb = b & 255;
        return 0xFF000000 | ((int) (ar + (br - ar) * t) << 16)
                | ((int) (ag + (bg - ag) * t) << 8) | (int) (ab + (bb - ab) * t);
    }

    /** Index into the status tag sheet, or -1 for none (mirrors GetMonAilment). */
    private int statusIconIndex(long status, int hp) {
        if (hp == 0) return 6;              // FNT
        if ((status & 0x7) != 0) return 2;  // SLP
        if ((status & 0x88) != 0) return 0; // PSN, incl. toxic
        if ((status & 0x10) != 0) return 4; // BRN
        if ((status & 0x20) != 0) return 3; // FRZ
        if ((status & 0x40) != 0) return 1; // PAR
        return -1;
    }

    private float tabBarHeight() {
        return getHeight() * 0.105f;
    }

    private RectF tabRect(int index) {
        float w = getWidth() / (float) TAB_NAMES.length;
        float top = getHeight() - tabBarHeight();
        return new RectF(index * w, top, (index + 1) * w, getHeight());
    }

    private static final String[] POCKET_NAMES = {"ITEMS", "POKé BALLS", "TMs-HMs", "BERRIES", "KEY ITEMS"};
    // Per-pocket accent colors, like the GBA bag's pocket theming.
    private static final int[] POCKET_ACCENTS = {
        0xFFE06858, 0xFFE8A030, 0xFF58A868, 0xFF5880C8, 0xFFA068C8
    };
    private static final int[] POCKET_ACCENTS_DARK = {
        0xFFA84840, 0xFFA87828, 0xFF3C7A48, 0xFF405E98, 0xFF744C94
    };

    private RectF pocketRect(int index) {
        float w = getWidth() / (float) POCKET_NAMES.length;
        float h = (getHeight() - tabBarHeight()) * 0.105f;
        return new RectF(index * w, 0, (index + 1) * w, h);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            showCursor = false; // a finger takes the screen back from the cursor
        }
        // Battle bag panel: drag to scroll, tap to select/use.
        if (state.inBattle && battlePanel == 1) {
            switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                battleBagTouchDownY = event.getY();
                battleBagScrollStart = battleBagScroll;
                battleBagDragging = false;
                break;
            case MotionEvent.ACTION_MOVE:
                if (battleBagDragging || Math.abs(event.getY() - battleBagTouchDownY) > 24) {
                    battleBagDragging = true;
                    battleBagScroll = Math.max(0, Math.min(battleBagMaxScroll(),
                            battleBagScrollStart + (battleBagTouchDownY - event.getY())));
                    invalidate();
                }
                return true;
            case MotionEvent.ACTION_UP:
                if (!battleBagDragging) {
                    handleBattleBagTouch(event.getX(), event.getY());
                }
                return true;
            }
            return true;
        }
        // Battle party panel (switch target or item target): plain taps.
        if (state.inBattle && (battlePanel == 2 || battlePanel == 3)) {
            if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
                handleBattlePartyTouch(event.getX(), event.getY());
            }
            return true;
        }
        // Settings list: drag to scroll, tap to toggle.
        if (tab == TAB_SETTINGS && !state.inBattle) {
            switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                settingsTouchDownY = event.getY();
                settingsScrollStart = settingsScroll;
                settingsDragging = false;
                break;
            case MotionEvent.ACTION_MOVE:
                if (!secretSettings && (settingsDragging || Math.abs(event.getY() - settingsTouchDownY) > 24)) {
                    settingsDragging = true;
                    settingsScroll = Math.max(0, Math.min(settingsMaxScroll(),
                            settingsScrollStart + (settingsTouchDownY - event.getY())));
                    invalidate();
                }
                return true;
            case MotionEvent.ACTION_UP:
                if (!settingsDragging) {
                    for (int i = 0; i < TAB_NAMES.length; i++) {
                        if (tabRect(i).contains(event.getX(), event.getY())) {
                            tab = i;
                            detailMon = -1;
                            invalidate();
                            return true;
                        }
                    }
                    handleSettingsTouch(event.getX(), event.getY());
                }
                return true;
            }
            return true;
        }
        // Bag list: drag to scroll, tap to select an item.
        if (tab == TAB_BAG && !state.inBattle && state.inGame) {
            switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                bagTouchDownY = event.getY();
                bagScrollStart = bagScroll;
                bagDragging = false;
                break;
            case MotionEvent.ACTION_MOVE:
                if (bagDragging || Math.abs(event.getY() - bagTouchDownY) > 24) {
                    bagDragging = true;
                    bagScroll = Math.max(0, Math.min(bagMaxScroll(),
                            bagScrollStart + (bagTouchDownY - event.getY())));
                    invalidate();
                }
                return true;
            case MotionEvent.ACTION_UP:
                if (!bagDragging) {
                    for (int i = 0; i < TAB_NAMES.length; i++) {
                        if (tabRect(i).contains(event.getX(), event.getY())) {
                            tab = i;
                            detailMon = -1;
                            invalidate();
                            return true;
                        }
                    }
                    handleBagTouch(event.getX(), event.getY());
                }
                return true;
            }
            return true;
        }
        if (event.getActionMasked() == MotionEvent.ACTION_DOWN) {
            if (state.inBattle) {
                handleBattleTouch(event.getX(), event.getY());
                return true;
            }
            for (int i = 0; i < TAB_NAMES.length; i++) {
                if (tabRect(i).contains(event.getX(), event.getY())) {
                    tab = i;
                    detailMon = -1;
                    invalidate();
                    return true;
                }
            }
            if (tab == TAB_PARTY) {
                if (detailMon >= 0) {
                    detailMon = -1; // any tap closes the detail view
                    invalidate();
                    return true;
                }
                for (int i = 0; i < partyCards.length; i++) {
                    if (partyCards[i].contains(event.getX(), event.getY())
                            && i < state.party.size()) {
                        detailMon = i;
                        invalidate();
                        return true;
                    }
                }
            }
            if (tab == TAB_SETTINGS) {
                handleSettingsTouch(event.getX(), event.getY());
            }
        }
        return true;
    }

    // How the command cursor steps between buttons, by [button][direction]
    // with -1 for "no neighbour that way". This mirrors
    // DualScreen_ActionCursorStep in the engine, which walks the same cursor
    // by the DP layout drawn here rather than by its own 2x2 box. A tap has
    // to steer along the same graph or it would aim at the old one.
    private static final int[][] CMD_STEPS = {
        // up, down, left, right
        {-1,  1, -1, -1}, // FIGHT
        { 0, -1, -1,  3}, // BAG, row left
        { 0, -1,  3, -1}, // POKéMON, row right
        { 0, -1,  1,  2}, // RUN, row centre
    };
    // Bottom row left to right, as the DS games order it: BAG, RUN, POKéMON.
    private static final int[] CMD_ROW = {1, 3, 2};
    private static final int[] DIR_KEYS = {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT};

    /** Shortest run of directions from one command button to another. */
    private static java.util.List<Integer> commandPath(int from, int to) {
        int[] prev = {-1, -1, -1, -1};
        int[] via = {-1, -1, -1, -1};
        boolean[] seen = new boolean[4];
        java.util.ArrayDeque<Integer> queue = new java.util.ArrayDeque<>();
        java.util.LinkedList<Integer> dirs = new java.util.LinkedList<>();

        if (from < 0 || from > 3 || to < 0 || to > 3) {
            return dirs;
        }
        seen[from] = true;
        queue.add(from);
        while (!queue.isEmpty()) {
            int cur = queue.poll();
            if (cur == to) {
                break;
            }
            for (int d = 0; d < 4; d++) {
                int next = CMD_STEPS[cur][d];
                if (next < 0 || seen[next]) {
                    continue;
                }
                seen[next] = true;
                prev[next] = cur;
                via[next] = d;
                queue.add(next);
            }
        }
        for (int cur = to; cur != from && prev[cur] >= 0; cur = prev[cur]) {
            dirs.addFirst(via[cur]);
        }
        return dirs;
    }

    /**
     * Walks the engine's cursor from `from` to `to`, then confirms. The move
     * grid is a real 2x2 and the layout matches it, so it steps by axis; the
     * command screen follows CMD_STEPS instead.
     */
    private void queueGridSelection(int menu, int from, int to, boolean confirm) {
        java.util.List<Integer> seq = new java.util.ArrayList<>();
        if (menu == 1) {
            for (int dir : commandPath(from, to)) {
                seq.add(DIR_KEYS[dir]);
                seq.add(0);
                seq.add(0);
            }
        } else {
            int dx = (to & 1) - (from & 1);
            int dy = ((to >> 1) & 1) - ((from >> 1) & 1);
            if (dx > 0) { seq.add(KEY_RIGHT); seq.add(0); seq.add(0); }
            if (dx < 0) { seq.add(KEY_LEFT); seq.add(0); seq.add(0); }
            if (dy > 0) { seq.add(KEY_DOWN); seq.add(0); seq.add(0); }
            if (dy < 0) { seq.add(KEY_UP); seq.add(0); seq.add(0); }
        }
        if (confirm) { seq.add(KEY_A); seq.add(0); }
        int[] masks = new int[seq.size()];
        for (int i = 0; i < masks.length; i++) masks[i] = seq.get(i);
        DualScreenBridge.nativeQueueKeys(masks);
    }

    private void handleBattleTouch(float x, float y) {
        long now = System.currentTimeMillis();
        if (now - lastKeyQueueMs < 350) {
            return; // let the previous selection land first
        }
        int menu = liveMenu();
        if (menu != battleButtonsMenu || menu == 0) {
            return;
        }
        if (battleCancel.contains(x, y) && menu == 2) {
            lastKeyQueueMs = now;
            DualScreenBridge.nativeQueueKeys(new int[] {KEY_B, 0});
            return;
        }
        for (int i = 0; i < 4; i++) {
            if (battleButtons[i].contains(x, y) && !battleButtons[i].isEmpty()) {
                int cursor = menu == 1 ? liveCursor(1, state.actionCursor)
                        : liveCursor(2, state.moveCursor);
                if (menu == 1 && i == 3 && state.battleKind == 1) {
                    return; // RUN is disabled in trainer battles
                }
                lastKeyQueueMs = now;
                if (menu == 1) {
                    // Pressed-in visual on the command buttons.
                    battlePressedCmd = i;
                    removeCallbacks(battlePressReset);
                    postDelayed(battlePressReset, 220);
                    invalidate();
                }
                if (menu == 1 && i == 1 && state.battleCanUseItems) {
                    // BAG: arm the takeover, then walk the cursor as usual.
                    // The battle bag opens down here instead of the GBA bag.
                    battlePanel = 1;
                    battleBagPocket = 0;
                    battleBagSelected = -1;
                    battleBagScroll = 0;
                    battleArmMs = now;
                    DualScreenBridge.nativeBattleArm(1);
                } else if (menu == 1 && i == 2) {
                    // POKéMON: same, with the party staircase down here.
                    battlePanel = 2;
                    battleArmMs = now;
                    DualScreenBridge.nativeBattleArm(2);
                }
                queueGridSelection(battleButtonsMenu, cursor, i, true);
                return;
            }
        }
    }

    // ------------------------------------------------------------------
    // Battle bag/party takeover panels
    // ------------------------------------------------------------------

    /** Game bag pockets usable in battle, in tab order (balls only when legal). */
    private int[] battleVisiblePockets() {
        return state.battleCanCatch ? new int[] {0, 1, 3} : new int[] {0, 3};
    }

    private int itemBattleCategory(int itemId) {
        Integer cached = battleCategoryCache.get(itemId);
        if (cached == null) {
            cached = DualScreenBridge.nativeGetItemBattleCategory(itemId);
            battleCategoryCache.put(itemId, cached);
        }
        return cached;
    }

    /** The current battle pocket's items, filtered down to battle-usable ones. */
    private java.util.List<DualScreenState.BagItem> battleBagItems() {
        java.util.List<DualScreenState.BagItem> out = new java.util.ArrayList<>();
        int[] pockets = battleVisiblePockets();
        int pocket = pockets[Math.min(battleBagPocket, pockets.length - 1)];
        if (pocket < state.bag.size()) {
            for (DualScreenState.BagItem item : state.bag.get(pocket)) {
                int category = itemBattleCategory(item.id);
                if (category <= 0) continue;
                if (category == 4 && state.battleKind == 1) continue; // escape items: wild only
                out.add(item);
            }
        }
        return out;
    }

    private float battleBagMaxScroll() {
        return Math.max(0, battleBagItems().size() * battleBagRowH
                - (battleBagListBottom - battleBagListTop));
    }

    private RectF battlePocketRect(int index, int count) {
        float w = getWidth() / (float) count;
        float h = getHeight() * 0.095f;
        return new RectF(index * w, 0, (index + 1) * w, h);
    }

    /**
     * Whether the engine is waiting on a forced send-out choice (a fainted
     * mon needs its replacement, PARTY_ACTION_SEND_OUT). The choice is
     * mandatory: the panel opens with no arming tap, shows no CANCEL, and
     * only slots the engine would accept are tappable.
     */
    private boolean battleSendOutWait() {
        return state.battleSub == 2 && state.battleSubCase == 1;
    }

    /** Closes the open panel, cancelling the engine-side wait if it engaged. */
    private void cancelBattlePanel() {
        if (battleSendOutWait()) {
            return; // forced send-out: the choice is mandatory, no cancel
        }
        if (state.battleSub != 0) {
            DualScreenBridge.nativeBattleSubmit(-1, -1);
        } else {
            DualScreenBridge.nativeBattleArm(0);
        }
        battleCloseMs = System.currentTimeMillis();
        battlePanel = 0;
        invalidate();
    }

    private void handleBattleBagTouch(float x, float y) {
        int[] pockets = battleVisiblePockets();
        for (int i = 0; i < pockets.length; i++) {
            if (battlePocketRect(i, pockets.length).contains(x, y)) {
                if (battleBagPocket != i) {
                    battleBagPocket = i;
                    battleBagScroll = 0;
                    battleBagSelected = -1;
                }
                invalidate();
                return;
            }
        }
        if (battleCancel.contains(x, y)) {
            cancelBattlePanel();
            return;
        }
        java.util.List<DualScreenState.BagItem> items = battleBagItems();
        if (!battleUseButton.isEmpty() && battleUseButton.contains(x, y)
                && battleBagSelected >= 0 && battleBagSelected < items.size()) {
            if (state.battleSub != 1) {
                return; // the engine hasn't reached its wait state yet
            }
            DualScreenState.BagItem sel = items.get(battleBagSelected);
            int category = itemBattleCategory(sel.id);
            if (category == 2 || category == 5) {
                pendingItemId = sel.id; // needs a target mon: second step
                battlePanel = 3;
            } else {
                battleCloseMs = System.currentTimeMillis();
                DualScreenBridge.nativeBattleSubmit(sel.id, -1);
            }
            invalidate();
            return;
        }
        if (y >= battleBagListTop && y < battleBagListBottom && battleBagRowH > 0) {
            int index = (int) ((y - battleBagListTop + battleBagScroll) / battleBagRowH);
            if (index >= 0 && index < items.size()) {
                battleBagSelected = index;
                invalidate();
            }
        }
    }

    /** Whether a staircase slot is tappable in the current panel. */
    private boolean battlePartySlotEnabled(int i) {
        DualScreenState.Mon mon = state.party.get(i);
        if (battlePanel == 3) {
            // Item target: anything but an egg; the engine's own "won't have
            // any effect" rejection covers the rest (e.g. Revive on healthy).
            return !mon.isEgg;
        }
        if (state.battleSubCase != 0 && !battleSendOutWait()) {
            return false; // engine says no switch can happen (trapped etc.)
        }
        // Send-out uses the same eligibility as a switch: alive, not an egg,
        // not on the field (a fainted battler's own slot fails the hp check),
        // and not already queued as the other slot's replacement (prevSel,
        // the doubles double-faint case).
        return !mon.isEgg && mon.hp > 0
                && i != state.battleActive[0] && i != state.battleActive[1]
                && i != state.battlePrevSel;
    }

    private void handleBattlePartyTouch(float x, float y) {
        if (battleCancel.contains(x, y)) {
            if (battlePanel == 3) {
                battlePanel = 1; // back to the bag; the wait state stays open
                invalidate();
            } else {
                cancelBattlePanel();
            }
            return;
        }
        for (int i = 0; i < battlePartyCards.length && i < state.party.size(); i++) {
            if (battlePartyCards[i].contains(x, y)) {
                if (!battlePartySlotEnabled(i)) {
                    return;
                }
                if (battlePanel == 3) {
                    if (state.battleSub != 1) return;
                    battleCloseMs = System.currentTimeMillis();
                    DualScreenBridge.nativeBattleSubmit(pendingItemId, i);
                    battlePanel = 1; // closes via sync once the engine accepts
                } else {
                    if (state.battleSub != 2) return;
                    battleCloseMs = System.currentTimeMillis();
                    DualScreenBridge.nativeBattleSubmit(i, -1);
                }
                invalidate();
                return;
            }
        }
    }

    // ------------------------------------------------------------------
    // Shared chrome
    // ------------------------------------------------------------------

    private void drawBackground(Canvas canvas) {
        canvas.drawColor(BG_MINT);
        paint.setColor(BG_DOT);
        float step = getWidth() / 40f;
        float radius = step * 0.10f;
        for (float y = step / 2; y < getHeight(); y += step) {
            for (float x = step / 2; x < getWidth(); x += step) {
                canvas.drawCircle(x, y, radius, paint);
            }
        }
    }

    /**
     * Covers the content area with a GBA-screen-flavored backdrop (base tone
     * plus a faint diagonal weave, both sampled from the game's bg art), so
     * the mint Pokenav chrome stays an outer frame.
     */
    private void drawContentBackdrop(Canvas canvas, float bottom, int base, int weave) {
        paint.setColor(base);
        canvas.drawRect(0, 0, getWidth(), bottom, paint);
        paint.setColor((weave & 0x00FFFFFF) | 0x30000000);
        float step = getWidth() / 26f;
        paint.setStrokeWidth(step * 0.16f);
        for (float x = -bottom; x < getWidth(); x += step) {
            canvas.drawLine(x, 0, x + bottom, bottom, paint);
        }
    }

    /** The party menu's real 240x160 background layer, decoded once. */
    private Bitmap partyBgBitmap() {
        if (partyBg == null) {
            int[] pixels = DualScreenBridge.nativeGetPartyBgImage();
            if (pixels != null && pixels.length == 240 * 160) {
                partyBg = Bitmap.createBitmap(pixels, 240, 160, Bitmap.Config.ARGB_8888);
            }
        }
        return partyBg;
    }

    /**
     * The party screens' backdrop: the game's own party menu bg layer
     * (tiles + tilemap + palette, composed over the bridge), scaled to the
     * content area. Falls back to the sampled base tone if unavailable.
     */
    private void drawPartyBackdrop(Canvas canvas, float bottom) {
        Bitmap bg = partyBgBitmap();
        if (bg != null) {
            canvas.drawBitmap(bg, null, new RectF(0, 0, getWidth(), bottom), pixelPaint);
        } else {
            drawContentBackdrop(canvas, bottom, PARTY_BG_BASE, PARTY_BG_WEAVE);
        }
    }

    private void drawBar(Canvas canvas, RectF r, boolean selected, String label, float textScale) {
        paint.setColor(BAR_BORDER);
        canvas.drawRoundRect(r, 6, 6, paint);
        RectF inner = new RectF(r);
        inner.inset(3, 3);
        paint.setColor(selected ? BAR_CREAM : BAR_CREAM_DARK);
        canvas.drawRoundRect(inner, 4, 4, paint);
        if (selected) {
            paint.setColor(0xFFF8B850);
            RectF edge = new RectF(inner.left, inner.bottom - 6, inner.right, inner.bottom);
            canvas.drawRoundRect(edge, 3, 3, paint);
        }
        GbaFont f = font();
        if (f != null && label != null) {
            float w = f.measure(label, textScale);
            f.draw(canvas, label, r.centerX() - w / 2,
                    r.centerY() - GbaFont.LINE_HEIGHT * textScale / 2, textScale, TEXT_DARK, TEXT_SHADOW);
        }
    }

    /**
     * Ring around the battler's current selection, mirroring the engine's own
     * action/move cursor so the physical buttons show where they are: with
     * the battle menus down here the top screen draws no cursor at all, so
     * without this a D-pad press moves something invisible. Sits in the gap
     * outside the cell rather than over it, so nothing on the card is
     * covered, in the selection gold the move bars already use.
     */
    /**
     * Cursor index to highlight for the menu being drawn, preferring the
     * per-frame native publish over the snapshot: the snapshot is rebuilt
     * every 8 frames and polled at 120ms, so on its own the ring would trail
     * a d-pad press by up to a quarter second. Falls back to the snapshot
     * when the fast read reports no menu, or a different one than we are
     * drawing.
     */
    /**
     * The menu the engine has open right now. Backing out of one leaves a
     * frame or two where neither is open before the next one appears, and at
     * the snapshot's cadence that blip was long enough to flash the idle
     * cards up. Prefer the per-frame publish, and hold the last menu briefly
     * across a gap so the transition doesn't drop to idle and back.
     */
    private int liveMenu() {
        long now = System.currentTimeMillis();
        int packed = DualScreenBridge.nativeGetBattleCursor();
        if (packed >= 0) {
            // The DS games show the cursor once you touch the d-pad and drop
            // it the moment a finger takes over, rather than carrying both
            // affordances at once. The native side counts real presses only.
            int presses = (packed >> 24) & 0x7F;
            if (lastPressCount >= 0 && presses != lastPressCount) {
                showCursor = true;
            }
            lastPressCount = presses;
        }
        int menu = packed >= 0 ? (packed >> 16) & 0xFF : state.battleMenu;
        if (menu != 0) {
            lastLiveMenu = menu;
            lastLiveMenuMs = now;
            return menu;
        }
        return now - lastLiveMenuMs < MENU_HOLD_MS ? lastLiveMenu : 0;
    }

    private int liveCursor(int menu, int fallback) {
        int packed = DualScreenBridge.nativeGetBattleCursor();
        if (packed < 0 || ((packed >> 16) & 0xFF) != menu) {
            return fallback;
        }
        return menu == 1 ? (packed >> 8) & 0xFF : packed & 0xFF;
    }

    private void drawCursorRing(Canvas canvas, RectF cell, float radius) {
        drawCursorRing(canvas, cell, radius, true);
    }

    /**
     * outside=false draws the ring just inside the cell instead, for the
     * party staircase: those slots sit flush against each other the way the
     * game's own party menu does, so a ring drawn outside would run over the
     * neighbouring slot.
     */
    private void drawCursorRing(Canvas canvas, RectF cell, float radius, boolean outside) {
        if (!showCursor) {
            return;
        }
        // Inside rings sit on the party staircase, whose slots are short: the
        // thickness that suits the big battle buttons all but disappears on
        // them, so they get their own, heavier weight. The real party cursor
        // is a thick border, not a hairline.
        float grow = outside
                ? Math.max(4f, Math.min(10f, Math.min(cell.width(), cell.height()) * 0.022f))
                : Math.max(6f, Math.min(16f, Math.min(cell.width(), cell.height()) * 0.075f));
        RectF ring = new RectF(cell);
        ring.inset(outside ? -grow : grow, outside ? -grow : grow);
        float r = outside ? radius + grow : Math.max(0, radius - grow);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(grow * 1.6f);
        paint.setColor(0xFF702008); // dark backing, so the border reads on any fill
        canvas.drawRoundRect(ring, r, r, paint);
        paint.setStrokeWidth(grow * 0.9f);
        // A selection is marked with an orange-red border throughout the
        // series - the GBA party list slot and the DS bag grid both do it.
        paint.setColor(0xFFF05030);
        canvas.drawRoundRect(ring, r, r, paint);
        paint.setStyle(Paint.Style.FILL);
    }

    private void drawHeader(Canvas canvas, String title, float scale) {
        RectF bar = new RectF(0, 0, getWidth(), GbaFont.LINE_HEIGHT * scale * 1.6f);
        paint.setColor(HEADER_GREEN);
        canvas.drawRect(bar, paint);
        paint.setColor(HEADER_GREEN_DARK);
        canvas.drawRect(new RectF(bar.left, bar.bottom - 4, bar.right, bar.bottom), paint);
        GbaFont f = font();
        if (f != null) {
            f.draw(canvas, title, scale * 8,
                    bar.centerY() - GbaFont.LINE_HEIGHT * scale / 2, scale, TEXT_WHITE, TEXT_GREEN_SHADOW);
        }
    }

    private void drawTabBar(Canvas canvas) {
        float scale = tabBarHeight() / (GbaFont.LINE_HEIGHT * 2.4f);
        paint.setColor(HEADER_GREEN);
        canvas.drawRect(new RectF(0, getHeight() - tabBarHeight(), getWidth(), getHeight()), paint);
        for (int i = 0; i < TAB_NAMES.length; i++) {
            RectF r = tabRect(i);
            r.inset(6, 7);
            drawBar(canvas, r, i == tab, TAB_NAMES[i], scale);
            if (TAB_NAMES[i] == null) {
                drawCog(canvas, r.centerX(), r.centerY(), r.height() * 0.28f, TEXT_DARK);
            }
        }
    }

    private void drawCog(Canvas canvas, float cx, float cy, float radius, int color) {
        paint.setColor(color);
        for (int t = 0; t < 8; t++) {
            double angle = Math.PI * 2 * t / 8;
            int save = canvas.save();
            canvas.rotate((float) Math.toDegrees(angle), cx, cy);
            canvas.drawRect(cx - radius * 0.18f, cy - radius * 1.35f,
                    cx + radius * 0.18f, cy - radius * 0.5f, paint);
            canvas.restoreToCount(save);
        }
        canvas.drawCircle(cx, cy, radius, paint);
        paint.setColor(BAR_CREAM);
        canvas.drawCircle(cx, cy, radius * 0.45f, paint);
    }

    private void drawCenteredMessage(Canvas canvas, String message) {
        GbaFont f = font();
        if (f == null) {
            return;
        }
        float scale = getWidth() / 420f;
        float w = f.measure(message, scale);
        f.draw(canvas, message, (getWidth() - w) / 2,
                (getHeight() - tabBarHeight()) / 2 - GbaFont.LINE_HEIGHT * scale / 2,
                scale, TEXT_DARK, TEXT_SHADOW);
    }

    /** 0 green, 1 yellow, 2 red, with the game's HP bar thresholds. */
    private int hpBarLevel(int hp, int maxHp) {
        if (maxHp <= 0) return 0;
        float ratio = hp / (float) maxHp;
        if (ratio > 0.5f) return 0;
        if (ratio > 0.2f) return 1;
        return 2;
    }

    private int hpColor(int hp, int maxHp) {
        switch (hpBarLevel(hp, maxHp)) {
        case 0:  return HP_GREEN;
        case 1:  return HP_YELLOW;
        default: return HP_RED;
        }
    }

    private void drawHpBar(Canvas canvas, float left, float top, float width, float height, int hp, int maxHp) {
        paint.setColor(PANEL_BORDER);
        canvas.drawRoundRect(new RectF(left - 2, top - 2, left + width + 2, top + height + 2), height / 2, height / 2, paint);
        paint.setColor(PANEL_WHITE);
        canvas.drawRoundRect(new RectF(left, top, left + width, top + height), height / 2, height / 2, paint);
        if (maxHp > 0 && hp > 0) {
            float fill = Math.max(height, width * hp / (float) maxHp);
            paint.setColor(hpColor(hp, maxHp));
            canvas.drawRoundRect(new RectF(left, top, left + fill, top + height), height / 2, height / 2, paint);
        }
    }

    private void drawTypeBadge(Canvas canvas, int type, float left, float top, float height) {
        if (type < 0 || type >= TYPE_NAMES.length) return;
        GbaFont f = font();
        float scale = height / (GbaFont.LINE_HEIGHT * 1.3f);
        float width = height * 3.1f;
        RectF r = new RectF(left, top, left + width, top + height);
        paint.setColor(0x40000000);
        canvas.drawRoundRect(new RectF(r.left, r.top + 2, r.right, r.bottom + 2), 5, 5, paint);
        paint.setColor(TYPE_COLORS[Math.min(type, TYPE_COLORS.length - 1)]);
        canvas.drawRoundRect(r, 5, 5, paint);
        if (f != null) {
            String name = TYPE_NAMES[type];
            float w = f.measure(name, scale);
            f.draw(canvas, name, r.centerX() - w / 2,
                    r.centerY() - GbaFont.LINE_HEIGHT * scale / 2, scale, TEXT_WHITE, 0xFF585858);
        }
    }

    private String statusLabel(long status, int hp) {
        if (hp == 0) return "FNT";
        if ((status & 0x7) != 0) return "SLP";
        if ((status & 0x8) != 0 || (status & 0x80) != 0) return "PSN";
        if ((status & 0x10) != 0) return "BRN";
        if ((status & 0x20) != 0) return "FRZ";
        if ((status & 0x40) != 0) return "PAR";
        return null;
    }

    // ------------------------------------------------------------------
    // Tabs
    // ------------------------------------------------------------------

    @Override
    public void draw(Canvas canvas) {
        super.draw(canvas);
        drawControllerFocus(canvas);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        drawBackground(canvas);
        if (!state.inGame) {
            // Settings work regardless of game state; other tabs need data.
            if (tab == TAB_SETTINGS) {
                drawSettings(canvas);
            } else {
                drawCenteredMessage(canvas, "Waiting for the adventure to start~");
            }
            drawTabBar(canvas);
            return;
        }
        if (state.inBattle) {
            // Battle takes over the whole bottom screen, Gen 4 style.
            drawBattle(canvas);
            return;
        }
        switch (tab) {
        case TAB_PARTY:    drawParty(canvas); break;
        case TAB_MAP:      drawMap(canvas); break;
        case TAB_BAG:      drawBag(canvas); break;
        case TAB_CARD:     drawTrainerCard(canvas); break;
        case TAB_SETTINGS: drawSettings(canvas); break;
        }
        drawTabBar(canvas);
    }

    /**
     * The Emerald party menu: lead mon in the big slot at upper left, the
     * rest stacked in wide slots on the right. All geometry comes from the
     * game's own tables (sPartyMenuSpriteCoords / sPartyBoxInfoRects /
     * window templates in src/data/party_menu.h), in GBA pixels on the
     * 240x160 screen, integer-scaled and centered like the map tab.
     */
    private void drawParty(Canvas canvas) {
        if (detailMon >= 0 && detailMon < state.party.size()) {
            drawPartyDetail(canvas, state.party.get(detailMon));
            return;
        }
        GbaFont f = font();
        float contentHeight = getHeight() - tabBarHeight();
        drawPartyBackdrop(canvas, contentHeight);
        // Six rich main-slot cards in a 2x3 grid: the GBA staircase wastes
        // most of this panel, so every mon gets the lead slot's treatment,
        // scaled to fill the content area. The slot art is flat fills and
        // borders, so it takes a horizontal stretch (sx > sy) invisibly;
        // sprites and text keep square pixels at sy.
        float pad = 16;
        int sy = (int) ((contentHeight - 4 * pad) / (3 * 56f));
        if (sy < 1) sy = 1;
        int sx = (int) ((getWidth() - 3 * pad) / (2 * 80f));
        if (sx < sy) sx = sy;
        float cw = 80 * sx, ch = 56 * sy;
        float gx = (getWidth() - 2 * cw) / 3f;
        float gy = (contentHeight - 3 * ch) / 4f;
        float nameScale = sy * 0.8f;
        float subScale = sy * 0.65f;

        for (int i = 0; i < 6; i++) {
            boolean present = i < state.party.size();
            DualScreenState.Mon mon = present ? state.party.get(i) : null;
            boolean fainted = present && !mon.isEgg && mon.hp == 0;
            int kind = present && !mon.isEgg ? 0 : 1;
            int pal = !present ? 2 : fainted ? 1 : 0;
            float wx = gx + (i % 2) * (cw + gx);
            float wy = gy + (i / 2) * (ch + gy);
            RectF slot = new RectF(wx, wy, wx + cw, wy + ch);
            partyCards[i].set(slot);
            drawSlotChrome(canvas, slot, kind, pal);
            if (!present || f == null) continue;

            // Content at the lead slot's anchors, kept inside the card
            // (the staircase let the icon hang over the slot edge; here it
            // sits flush). X anchors scale by sx; sprites stay square.
            Bitmap icon = mon.isEgg ? null : monIcon(mon.species);
            if (icon != null) {
                canvas.drawBitmap(icon, null,
                        new RectF(wx + 2 * sy, wy + 2 * sy, wx + 34 * sy, wy + 34 * sy), pixelPaint);
            }
            if (!mon.isEgg && mon.item > 0) {
                // Mail items get the game's mail mark (ITEM_ORANGE_MAIL..ITEM_RETRO_MAIL).
                Bitmap mark = holdIcon(mon.item >= 121 && mon.item <= 132 ? 1 : 0);
                if (mark != null) {
                    canvas.drawBitmap(mark, null,
                            new RectF(wx + 8 * sy, wy + 22 * sy, wx + 16 * sy, wy + 30 * sy), pixelPaint);
                }
            }

            String title = mon.isEgg ? "EGG" : mon.nick;
            f.draw(canvas, title, wx + 24 * sx, wy + 11 * sy, nameScale, PARTY_TEXT, PARTY_TEXT_SHADOW);
            if (mon.isEgg) continue;

            // Level and gender give way to the status tag, like in the game.
            float subY = wy + 20 * sy;
            int statusIdx = statusIconIndex(mon.status, mon.hp);
            if (statusIdx < 0) {
                f.draw(canvas, "Lv" + mon.level, wx + 32 * sx, subY,
                        subScale, PARTY_TEXT, PARTY_TEXT_SHADOW);
                if (mon.gender == 0) {
                    f.draw(canvas, "♂", wx + 64 * sx, subY,
                            subScale, PARTY_MALE, PARTY_MALE_SHADOW);
                } else if (mon.gender == 1) {
                    f.draw(canvas, "♀", wx + 64 * sx, subY,
                            subScale, PARTY_FEMALE, PARTY_FEMALE_SHADOW);
                }
            } else {
                Bitmap tag = statusIcon(statusIdx);
                if (tag != null) {
                    canvas.drawBitmap(tag, null,
                            new RectF(wx + 26 * sx, wy + 24 * sy, wx + 26 * sx + 32 * sy, wy + 32 * sy), pixelPaint);
                }
            }

            // HP bar fill over the groove baked into the slot graphic; the
            // groove stretches with the card, so the fill scales by sx.
            float barX = wx + 24 * sx;
            float barY = wy + 35 * sy;
            float fillPx = mon.maxHp > 0 ? 48f * mon.hp / mon.maxHp : 0;
            if (mon.hp > 0 && fillPx < 1) fillPx = 1;
            int level = hpBarLevel(mon.hp, mon.maxHp);
            paint.setColor(PARTY_HP_TOP[level]);
            canvas.drawRect(barX, barY, barX + fillPx * sx, barY + sy, paint);
            paint.setColor(PARTY_HP_BODY[level]);
            canvas.drawRect(barX, barY + sy, barX + fillPx * sx, barY + 3 * sy, paint);

            // Right-aligned to the groove's end so it stays inside the card.
            String hpText = mon.hp + "/" + mon.maxHp;
            float hpRight = wx + 72 * sx;
            f.draw(canvas, hpText, hpRight - f.measure(hpText, subScale), wy + 40 * sy,
                    subScale, PARTY_TEXT, PARTY_TEXT_SHADOW);
        }
    }

    // Stat rows in the summary screen's Skills-page order, mapping into
    // the snapshot's [atk, def, speed, spatk, spdef] array.
    private static final String[] STAT_NAMES = {"ATTACK", "DEFENSE", "SP. ATK", "SP. DEF", "SPEED"};
    private static final int[] STAT_ORDER = {0, 1, 3, 4, 2};

    /** A white panel with a blue title strip, like the summary screen pages. */
    private void drawSummaryPanel(Canvas canvas, RectF box, String label, float stripH, float scale) {
        RectF inner = new RectF(box);
        paint.setColor(SUMMARY_BLUE_DARK);
        canvas.drawRoundRect(box, 12, 12, paint);
        inner.inset(3, 3);
        paint.setColor(PANEL_WHITE);
        canvas.drawRoundRect(inner, 9, 9, paint);
        paint.setColor(SUMMARY_BLUE);
        canvas.drawRoundRect(new RectF(inner.left, inner.top, inner.right, inner.top + stripH), 9, 9, paint);
        canvas.drawRect(new RectF(inner.left, inner.top + stripH / 2, inner.right, inner.top + stripH), paint);
        GbaFont f = font();
        if (f != null) {
            f.draw(canvas, label, inner.left + 14,
                    inner.top + (stripH - GbaFont.LINE_HEIGHT * scale) / 2,
                    scale, PARTY_TEXT, SUMMARY_BLUE_DARK);
        }
    }

    /** A flat two-tone GBA-style bar: 1/3 highlight on top, body below. */
    private void drawGbaBar(Canvas canvas, float left, float top, float width, float height,
                            float fillRatio, int topColor, int bodyColor) {
        paint.setColor(0xFF505050);
        canvas.drawRect(left - 2, top - 2, left + width + 2, top + height + 2, paint);
        paint.setColor(PANEL_WHITE);
        canvas.drawRect(left, top, left + width, top + height, paint);
        float fill = width * Math.max(0f, Math.min(1f, fillRatio));
        if (fill > 0) {
            paint.setColor(topColor);
            canvas.drawRect(left, top, left + fill, top + height / 3, paint);
            paint.setColor(bodyColor);
            canvas.drawRect(left, top + height / 3, left + fill, top + height, paint);
        }
    }

    /** Detail view in the summary screen's language: identity header, Skills page, Moves page. */
    private void drawPartyDetail(Canvas canvas, DualScreenState.Mon mon) {
        GbaFont f = font();
        if (f == null) {
            return;
        }
        float contentHeight = getHeight() - tabBarHeight();
        drawPartyBackdrop(canvas, contentHeight);
        float pad = getWidth() * 0.025f;
        float scale = getWidth() / 440f;

        // Identity header in the party slots' blue box language.
        RectF header = new RectF(pad, pad, getWidth() - pad, contentHeight * 0.28f);
        paint.setColor(SUMMARY_BLUE_DARK);
        canvas.drawRoundRect(header, 12, 12, paint);
        RectF inner = new RectF(header);
        inner.inset(4, 4);
        paint.setColor(SUMMARY_BLUE);
        canvas.drawRoundRect(inner, 9, 9, paint);
        paint.setColor(SUMMARY_BLUE_LIGHT);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(3);
        canvas.drawRoundRect(new RectF(inner.left + 3, inner.top + 3, inner.right - 3, inner.bottom - 3), 7, 7, paint);
        paint.setStyle(Paint.Style.FILL);

        float inset = header.height() * 0.14f;
        float iconSize = header.height() * 0.68f;
        RectF iconRect = new RectF(header.left + inset, header.centerY() - iconSize / 2,
                header.left + inset + iconSize, header.centerY() + iconSize / 2);
        paint.setColor(SUMMARY_BLUE_LIGHT);
        canvas.drawCircle(iconRect.centerX(), iconRect.centerY(), iconSize * 0.62f, paint);
        Bitmap icon = mon.isEgg ? null : monIcon(mon.species);
        if (icon != null) {
            canvas.drawBitmap(icon, null, iconRect, pixelPaint);
        }

        float textLeft = iconRect.right + inset;
        String title = mon.isEgg ? "EGG" : mon.nick;
        f.draw(canvas, title, textLeft, header.top + inset, scale * 1.15f, PARTY_TEXT, SUMMARY_BLUE_DARK);
        if (mon.isEgg) {
            f.draw(canvas, "What will hatch from this?", textLeft,
                    header.top + inset + GbaFont.LINE_HEIGHT * scale * 1.4f,
                    scale * 0.9f, PARTY_TEXT, SUMMARY_BLUE_DARK);
            return;
        }
        float nameW = f.measure(title, scale * 1.15f);
        if (mon.gender == 0) {
            f.draw(canvas, "♂", textLeft + nameW + 12, header.top + inset,
                    scale * 1.15f, PARTY_MALE, PARTY_MALE_SHADOW);
        } else if (mon.gender == 1) {
            f.draw(canvas, "♀", textLeft + nameW + 12, header.top + inset,
                    scale * 1.15f, PARTY_FEMALE, PARTY_FEMALE_SHADOW);
        }
        f.draw(canvas, "Lv" + mon.level + "  " + mon.name, textLeft,
                header.top + inset + GbaFont.LINE_HEIGHT * scale * 1.35f,
                scale * 0.9f, PARTY_TEXT, SUMMARY_BLUE_DARK);
        f.draw(canvas, mon.nature + "  " + mon.ability, textLeft,
                header.top + inset + GbaFont.LINE_HEIGHT * scale * 2.4f,
                scale * 0.8f, PARTY_TEXT, SUMMARY_BLUE_DARK);
        float badgeH = header.height() * 0.18f;
        drawTypeIcon(canvas, mon.types[0], header.right - inset - badgeH * 2,
                header.top + inset, badgeH);
        if (mon.types[1] != mon.types[0]) {
            drawTypeIcon(canvas, mon.types[1], header.right - inset - badgeH * 4 - 8,
                    header.top + inset, badgeH);
        }

        // Skills page on the left, Moves page on the right, item row below.
        float columnsTop = header.bottom + pad;
        float itemH = contentHeight * 0.105f;
        float colW = (getWidth() - pad * 3) / 2;
        float stripH = GbaFont.LINE_HEIGHT * scale + 12;
        RectF skillsBox = new RectF(pad, columnsTop, pad + colW,
                contentHeight - pad * 1.5f - itemH);
        RectF movesBox = new RectF(pad * 2 + colW, columnsTop, getWidth() - pad, skillsBox.bottom);
        drawSummaryPanel(canvas, skillsBox, "SKILLS", stripH, scale);
        drawSummaryPanel(canvas, movesBox, "MOVES", stripH, scale);

        // HP with the bar, five stats, then the EXP bar: seven rows.
        float left = skillsBox.left + pad;
        float right = skillsBox.right - pad;
        float rowsTop = skillsBox.top + stripH + 8;
        float rowStep = (skillsBox.bottom - rowsTop - pad * 0.6f) / 7f;
        float y = rowsTop + (rowStep - GbaFont.LINE_HEIGHT * scale) / 2;
        f.draw(canvas, "HP", left, y, scale, TEXT_DARK, TEXT_SHADOW);
        String hpText = mon.hp + "/" + mon.maxHp;
        float hpW = f.measure(hpText, scale);
        f.draw(canvas, hpText, right - hpW, y, scale, TEXT_DARK, TEXT_SHADOW);
        int level = hpBarLevel(mon.hp, mon.maxHp);
        float barLeft = left + f.measure("SP. ATK", scale) + 16;
        float barH = rowStep * 0.32f;
        drawGbaBar(canvas, barLeft, y + (GbaFont.LINE_HEIGHT * scale - barH) / 2,
                right - hpW - 16 - barLeft, barH,
                mon.maxHp > 0 ? mon.hp / (float) mon.maxHp : 0,
                PARTY_HP_TOP[level], PARTY_HP_BODY[level]);
        for (int i = 0; i < 5; i++) {
            y = rowsTop + (i + 1) * rowStep + (rowStep - GbaFont.LINE_HEIGHT * scale) / 2;
            f.draw(canvas, STAT_NAMES[i], left, y, scale, TEXT_DARK, TEXT_SHADOW);
            String v = Integer.toString(mon.stats[STAT_ORDER[i]]);
            f.draw(canvas, v, right - f.measure(v, scale), y, scale, TEXT_DARK, TEXT_SHADOW);
        }
        y = rowsTop + 6 * rowStep + (rowStep - GbaFont.LINE_HEIGHT * scale) / 2;
        f.draw(canvas, "EXP", left, y, scale, TEXT_DARK, TEXT_SHADOW);
        drawGbaBar(canvas, barLeft, y + (GbaFont.LINE_HEIGHT * scale - barH) / 2,
                right - barLeft, barH, mon.expPct / 100f, PARTY_MALE, SUMMARY_BLUE);

        // Moves with PP, like the Moves page.
        float moveStep = (movesBox.bottom - movesBox.top - stripH - 8 - pad * 0.6f) / 4f;
        for (int i = 0; i < 4; i++) {
            y = movesBox.top + stripH + 8 + i * moveStep + (moveStep - GbaFont.LINE_HEIGHT * scale) / 2;
            if (i < mon.moves.size()) {
                DualScreenState.Move move = mon.moves.get(i);
                float moveBadgeH = moveStep * 0.55f;
                drawTypeIcon(canvas, move.type, movesBox.left + pad,
                        y + (GbaFont.LINE_HEIGHT * scale - moveBadgeH) / 2, moveBadgeH);
                f.draw(canvas, move.name, movesBox.left + pad + moveBadgeH * 2 + 12, y,
                        scale, TEXT_DARK, TEXT_SHADOW);
                String pp = "PP " + move.pp + "/" + move.maxPp;
                float w = f.measure(pp, scale * 0.85f);
                f.draw(canvas, pp, movesBox.right - pad - w, y, scale * 0.85f,
                        move.pp == 0 ? HP_RED : TEXT_DARK, TEXT_SHADOW);
            } else {
                f.draw(canvas, "-", movesBox.left + pad, y, scale, TEXT_DARK, TEXT_SHADOW);
            }
        }

        // Held item row, with the real item icon.
        RectF itemBox = new RectF(pad, skillsBox.bottom + pad * 0.5f,
                getWidth() - pad, skillsBox.bottom + pad * 0.5f + itemH);
        drawBar(canvas, itemBox, false, null, scale);
        float itemY = itemBox.centerY() - GbaFont.LINE_HEIGHT * scale / 2;
        f.draw(canvas, "ITEM", itemBox.left + pad, itemY, scale, TEXT_DARK, TEXT_SHADOW);
        float itemTextLeft = itemBox.left + pad + f.measure("ITEM", scale) + 18;
        if (mon.item > 0) {
            Bitmap itemGfx = itemIcon(mon.item);
            float iconH = itemBox.height() * 0.8f;
            if (itemGfx != null) {
                canvas.drawBitmap(itemGfx, null,
                        new RectF(itemTextLeft, itemBox.centerY() - iconH / 2,
                                  itemTextLeft + iconH, itemBox.centerY() + iconH / 2), pixelPaint);
                itemTextLeft += iconH + 10;
            }
            f.draw(canvas, mon.itemName, itemTextLeft, itemY, scale, TEXT_DARK, TEXT_SHADOW);
        } else {
            f.draw(canvas, "NONE", itemTextLeft, itemY, scale, TEXT_DARK, TEXT_SHADOW);
        }
    }

    private void drawBattleMonCard(Canvas canvas, DualScreenState.Mon mon, RectF rect,
                                   String namePrefix, float scale) {
        GbaFont f = font();
        if (f == null || mon == null) {
            return;
        }
        paint.setColor(PANEL_WHITE);
        canvas.drawRoundRect(rect, 10, 10, paint);
        paint.setColor(BAR_BORDER);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(3);
        canvas.drawRoundRect(rect, 10, 10, paint);
        paint.setStyle(Paint.Style.FILL);

        float inset = rect.height() * 0.14f;
        float iconSize = rect.height() * 0.68f;
        Bitmap icon = monIcon(mon.species);
        if (icon != null) {
            canvas.drawBitmap(icon, null,
                    new RectF(rect.left + inset, rect.centerY() - iconSize / 2,
                              rect.left + inset + iconSize, rect.centerY() + iconSize / 2), pixelPaint);
        }
        float textLeft = rect.left + inset + iconSize + inset;
        String header = namePrefix + mon.nick + "  Lv" + mon.level
                + (mon.gender == 0 ? " ♂" : mon.gender == 1 ? " ♀" : "");
        f.draw(canvas, header, textLeft, rect.top + inset, scale, TEXT_DARK, TEXT_SHADOW);

        float badgeH = rect.height() * 0.14f;
        drawTypeIcon(canvas, mon.types[0], rect.right - inset - badgeH * 2,
                rect.bottom - inset - badgeH, badgeH);
        if (mon.types[1] != mon.types[0]) {
            drawTypeIcon(canvas, mon.types[1], rect.right - inset - badgeH * 4 - 8,
                    rect.bottom - inset - badgeH, badgeH);
        }

        float barTop = rect.top + inset + GbaFont.LINE_HEIGHT * scale + 10;
        drawHpBar(canvas, textLeft, barTop, rect.right - inset - textLeft,
                rect.height() * 0.1f, mon.hp, mon.maxHp);
        f.draw(canvas, mon.hp + "/" + mon.maxHp, textLeft,
                barTop + rect.height() * 0.1f + 8, scale * 0.85f, TEXT_DARK, TEXT_SHADOW);
        float underBarTop = barTop + rect.height() * 0.1f + GbaFont.LINE_HEIGHT * scale * 0.85f + 14;
        if (namePrefix.isEmpty()) {
            // Player's mon: slim exp bar, Emerald blue.
            float expFullW = rect.right - inset - textLeft;
            paint.setColor(0xFFE8E8E0);
            canvas.drawRoundRect(new RectF(textLeft, underBarTop, textLeft + expFullW, underBarTop + 8), 4, 4, paint);
            paint.setColor(0xFF3890F0);
            canvas.drawRoundRect(new RectF(textLeft, underBarTop,
                    textLeft + Math.max(expFullW * mon.expPct / 100f, 4), underBarTop + 8), 4, 4, paint);
        } else {
            // Foe's mon: the weakness strip takes the exp bar's slot, ending
            // above the type badges in the card's bottom-right corner.
            drawWeaknessRow(canvas, mon, textLeft, underBarTop,
                    rect.right - inset, rect.bottom - inset - badgeH - rect.height() * 0.02f,
                    scale);
        }
        String status = statusLabel(mon.status, mon.hp);
        if (status != null) {
            float w = f.measure(status, scale * 0.85f);
            f.draw(canvas, status, rect.right - inset - w,
                    barTop + rect.height() * 0.1f + 8, scale * 0.85f, HP_RED, TEXT_SHADOW);
        }
    }

    /**
     * "WEAK" strip on a foe card: the attacking types this mon takes
     * super-effective damage from, 4x entries first and carrying the same
     * double caret the move grid uses for 4x. Icons scale down to fit the
     * strip, and entries that still would not fit are dropped from the end —
     * the 2x tail — so the doubles layout's narrow cards just show fewer.
     */
    private void drawWeaknessRow(Canvas canvas, DualScreenState.Mon mon, float left,
                                 float top, float right, float bottom, float scale) {
        GbaFont f = font();
        if (f == null || mon.weaknesses.isEmpty() || bottom <= top
                || DualScreenBridge.nativeGetPlatformSetting(DualScreenBridge.SETTING_BATTLE_HINTS) == 0) {
            return; // hints are opt-in (SET tab)
        }
        float band = bottom - top;
        float labelScale = scale * 0.7f;
        float labelW = f.measure("WEAK", labelScale) + band * 0.25f;

        // Entry widths, in icon-heights: the type icon (2:1, or 3.1:1 for the
        // drawn-badge fallback when the icon sheet is unavailable), a gap, and
        // for a 4x weakness the caret marker in between.
        float iconU = typeIconBitmap(mon.weaknesses.get(0).type) != null ? 2f : 3.1f;
        float units = 0;
        for (DualScreenState.Weakness w : mon.weaknesses) {
            units += iconU + (w.tier == 5 ? 1.1f : 0.35f);
        }
        float iconH = Math.min(band, (right - left - labelW) / Math.max(units, 0.001f));
        if (iconH < band * 0.45f) {
            iconH = band * 0.45f; // stop shrinking; drop the overflow instead
        }
        if (iconH <= 0 || left + labelW + iconH * iconU > right) {
            return; // not even one icon fits
        }

        f.draw(canvas, "WEAK", left, top + (band - GbaFont.LINE_HEIGHT * labelScale) / 2f,
                labelScale, 0xFF70707A, TEXT_SHADOW);
        float x = left + labelW;
        float y = top + (band - iconH) / 2f;
        for (DualScreenState.Weakness w : mon.weaknesses) {
            float entryW = iconH * (iconU + (w.tier == 5 ? 1.1f : 0.35f));
            if (x + entryW - iconH * 0.35f > right) {
                break;
            }
            drawTypeIcon(canvas, w.type, x, y, iconH);
            if (w.tier == 5) {
                drawEffMarker(canvas, 5, x + iconH * (iconU + 0.35f), y + iconH / 2f, iconH * 0.2f);
            }
            x += entryW;
        }
    }

    private void drawBattle(Canvas canvas) {
        GbaFont f = font();
        if (!state.inBattle || state.battlePlayerMon == null || f == null) {
            drawCenteredMessage(canvas, "Not in battle");
            return;
        }
        float contentHeight = getHeight(); // battle owns the full screen
        float pad = getWidth() * 0.02f;
        float scale = getWidth() / 430f;

        for (RectF r : battleButtons) r.setEmpty();
        battleCancel.setEmpty();
        battleUseButton.setEmpty();
        int menu = liveMenu();
        battleButtonsMenu = menu;

        // Gen 4-style takeover panels own the whole bottom screen.
        if (battlePanel == 1) {
            drawBattleBag(canvas);
            return;
        }
        if (battlePanel == 2 || battlePanel == 3) {
            drawBattleParty(canvas);
            return;
        }

        DualScreenState.Mon self = state.battlePlayerMon;

        if (menu == 0) {
            drawBattleIdle(canvas, pad, scale);
            return;
        }

        if (menu == 1) {
            drawBattleCommands(canvas, pad, scale);
        } else {
            float gridTop = drawBattleMenuHeader(canvas, pad, scale);
            // Move grid: name, type icon, PP, PWR/ACC, effectiveness hint.
            boolean active = menu == 2;
            int cursor = liveCursor(2, state.moveCursor);
            float cancelH = active ? contentHeight * 0.085f : 0;
            float cellH = (contentHeight - gridTop - pad * 2 - cancelH - (active ? pad : 0)) / 2;
            float cellW = (getWidth() - pad * 3) / 2;
            for (int i = 0; i < 4; i++) {
                float left = pad + (i % 2) * (cellW + pad);
                float top = gridTop + (i / 2) * (cellH + pad);
                RectF cell = new RectF(left, top, left + cellW, top + cellH);
                if (i < self.moves.size()) {
                    if (active) {
                        battleButtons[i].set(cell);
                    }
                    // The engine's move cursor picks the highlighted cell.
                    // Before, every cell was drawn selected, which made the
                    // bar's own highlight mean nothing.
                    boolean onCursor = active && i == cursor;
                    drawBar(canvas, cell, onCursor, null, scale);
                    DualScreenState.Move move = self.moves.get(i);
                    float inset = cellH * 0.14f;
                    float nameScale = scale * 1.35f;
                    f.draw(canvas, move.name, cell.left + inset, cell.top + inset,
                            nameScale, TEXT_DARK, TEXT_SHADOW);
                    String info = "PWR " + (move.power == 0 ? "—" : Integer.toString(move.power))
                            + "   ACC " + (move.accuracy == 0 ? "—" : Integer.toString(move.accuracy));
                    f.draw(canvas, info, cell.left + inset,
                            cell.top + inset + GbaFont.LINE_HEIGHT * nameScale + 6,
                            scale * 0.85f, 0xFF70707A, TEXT_SHADOW);
                    float iconH = cellH * 0.22f;
                    drawTypeIcon(canvas, move.type, cell.left + inset,
                            cell.bottom - inset - iconH, iconH);
                    String pp = "PP " + move.pp + "/" + move.maxPp;
                    float ppScale = scale * 0.95f;
                    float w = f.measure(pp, ppScale);
                    f.draw(canvas, pp, cell.right - inset - w,
                            cell.bottom - inset - iconH
                                    + (iconH - GbaFont.LINE_HEIGHT * ppScale) / 2,
                            ppScale, move.pp == 0 ? HP_RED : TEXT_DARK, TEXT_SHADOW);
                    // Effectiveness corner markers: one per live foe in
                    // doubles (right foe outermost), one in singles. Absent
                    // foes and normal effectiveness draw nothing.
                    float mr = cellH * 0.085f;
                    float mx = cell.right - inset * 0.6f - mr * 2.0f;
                    float my = cell.top + inset * 0.6f + mr * 1.6f;
                    if (!state.battleDouble) {
                        drawEffMarker(canvas, move.eff[0], mx, my, mr);
                    } else {
                        drawEffMarker(canvas, move.eff[1], mx, my, mr);
                        drawEffMarker(canvas, move.eff[0], mx - mr * 4.2f, my, mr);
                    }
                    if (onCursor) {
                        drawCursorRing(canvas, cell, 6);
                    }
                } else {
                    paint.setColor(0x44A88848);
                    canvas.drawRoundRect(cell, 8, 8, paint);
                }
            }
            if (active) {
                RectF cancel = new RectF(pad, gridTop + cellH * 2 + pad * 2,
                        getWidth() - pad, gridTop + cellH * 2 + pad * 2 + cancelH);
                battleCancel.set(cancel);
                drawBar(canvas, cancel, false, "CANCEL", scale * 0.9f);
            }
        }
        // A menu is open, so the cursor can move under the player's thumb at
        // any moment. Snapshot updates alone would repaint at about 8fps;
        // this keeps the ring following the d-pad. Only runs while a menu is
        // up - the idle screen and the takeover panels have their own pacing.
        postInvalidateDelayed(CURSOR_REPAINT_MS);
    }

    /**
     * Idle companion cards while the engine is animating or between menus:
     * singles keeps the two big cards, doubles shows all four battlers
     * (player pair left, foes right). A pulsing dots pill signals that
     * nothing down here is tappable right now.
     */
    private void drawBattleIdle(Canvas canvas, float pad, float scale) {
        float contentHeight = getHeight();
        DualScreenState.Mon self = state.battlePlayerMon;
        DualScreenState.Mon enemy = state.battleEnemyMon;
        String foePrefix = state.battleKind == 1 ? "FOE " : "WILD ";
        float waitH = contentHeight * 0.075f;
        float area = contentHeight - waitH - pad;
        boolean four = state.battleDouble
                && (state.battlePlayerMon2 != null || state.battleEnemyMon2 != null);

        if (four) {
            float cardH = (area - pad * 3) / 2;
            float cardW = (getWidth() - pad * 3) / 2;
            float cardScale = scale * 0.85f;
            DualScreenState.Mon[] mons = {self, state.battlePlayerMon2,
                                          enemy, state.battleEnemyMon2};
            for (int i = 0; i < 4; i++) {
                if (mons[i] == null) {
                    continue;
                }
                int col = i / 2; // players left, foes right
                int row = i % 2;
                float left = pad + col * (cardW + pad);
                float top = pad + row * (cardH + pad);
                drawBattleMonCard(canvas, mons[i],
                        new RectF(left, top, left + cardW, top + cardH),
                        col == 1 ? foePrefix : "", cardScale);
            }
        } else {
            float cardH = (area - pad * 3) / 2;
            float cardScale = scale * 1.5f;
            RectF enemyRect = new RectF(pad, pad, getWidth() - pad, pad + cardH);
            if (enemy != null) {
                drawBattleMonCard(canvas, enemy, enemyRect, foePrefix, cardScale);
            }
            RectF selfRect = new RectF(pad, enemyRect.bottom + pad,
                    getWidth() - pad, enemyRect.bottom + pad + cardH);
            drawBattleMonCard(canvas, self, selfRect, "", cardScale);
        }

        // Waiting pill: three pulsing dots, centered under the cards.
        float cx = getWidth() / 2f;
        float cy = contentHeight - pad - waitH / 2;
        float r = waitH * 0.14f;
        float gap = r * 3.2f;
        int phase = (int) ((System.currentTimeMillis() / 350) % 3);
        paint.setColor(0x50304038);
        canvas.drawRoundRect(new RectF(cx - gap * 2.1f, cy - r * 2.6f,
                cx + gap * 2.1f, cy + r * 2.6f), r * 2.6f, r * 2.6f, paint);
        for (int i = 0; i < 3; i++) {
            paint.setColor(i == phase ? 0xFFF8F8F8 : 0x80FFFFFF);
            canvas.drawCircle(cx + (i - 1) * gap, cy, r, paint);
        }
        postInvalidateDelayed(350);
    }

    // DP-layout command screen: FIGHT dominant on top, BAG / POKéMON / RUN
    // in a row below, each in its own accent family. All shapes and glyphs
    // are drawn from scratch on the Canvas.
    private static final String[] CMD_LABELS = {"FIGHT", "BAG", "POKéMON", "RUN"};
    private static final int[] CMD_FILL  = {0xFFE05838, 0xFFE8B030, 0xFF48A860, 0xFF4878C8};
    private static final int[] CMD_LIGHT = {0xFFF88860, 0xFFF8D060, 0xFF78D088, 0xFF78A8E8};
    private static final int[] CMD_DARK  = {0xFFA03820, 0xFFA87818, 0xFF287840, 0xFF285090};

    private void drawBattleCommands(Canvas canvas, float pad, float scale) {
        float contentHeight = getHeight();
        float gridTop = drawBattleMenuHeader(canvas, pad, scale);
        boolean runDisabled = state.battleKind == 1; // no running from trainers
        int cursor = liveCursor(1, state.actionCursor);

        float fightH = (contentHeight - gridTop - pad * 2) * 0.56f;
        RectF fight = new RectF(pad, gridTop, getWidth() - pad, gridTop + fightH);
        battleButtons[0].set(fight);
        drawCommandButton(canvas, fight, 0, battlePressedCmd == 0, true, scale * 2.1f);
        if (cursor == 0) {
            drawCursorRing(canvas, fight, 16);
        }

        float smallTop = fight.bottom + pad;
        float smallW = (getWidth() - pad * 4) / 3f;
        for (int pos = 0; pos < CMD_ROW.length; pos++) {
            int cmd = CMD_ROW[pos];
            float left = pad + pos * (smallW + pad);
            RectF cell = new RectF(left, smallTop, left + smallW, contentHeight - pad);
            battleButtons[cmd].set(cell);
            drawCommandButton(canvas, cell, cmd, battlePressedCmd == cmd,
                    !(cmd == 3 && runDisabled), scale * 1.1f);
            if (cursor == cmd) {
                drawCursorRing(canvas, cell, 16);
            }
        }
    }

    /**
     * Status band above the battle menus. The DS games keep the top eighth of
     * the touch screen clear of buttons and put the active mon's readout
     * there (HGSS carries selectedMon/hp/maxHp on its main-menu struct for
     * exactly this). Ours shows both sides, because opening a menu otherwise
     * hides the idle cards and takes the HP readout away with them. Returns
     * the y the buttons start at.
     */
    private float drawBattleMenuHeader(Canvas canvas, float pad, float scale) {
        GbaFont f = font();
        float band = getHeight() * 0.125f;
        float top = pad * 0.4f;
        float h = band - top - pad * 0.5f;
        if (f == null || h <= 0) {
            return band;
        }
        DualScreenState.Mon[] sides = {state.battlePlayerMon, state.battleEnemyMon};
        float half = (getWidth() - pad * 3) / 2f;
        for (int i = 0; i < 2; i++) {
            DualScreenState.Mon mon = sides[i];
            if (mon == null) {
                continue;
            }
            float left = pad + i * (half + pad);
            float textScale = scale * 0.85f;
            Bitmap icon = monIcon(mon.species);
            float iconSize = h;
            if (icon != null) {
                canvas.drawBitmap(icon, null,
                        new RectF(left, top, left + iconSize, top + iconSize), pixelPaint);
            }
            float textLeft = left + iconSize + pad * 0.3f;
            f.draw(canvas, mon.nick + "  Lv" + mon.level, textLeft, top,
                    textScale, TEXT_WHITE, 0xFF303840);
            float barTop = top + GbaFont.LINE_HEIGHT * textScale + 4;
            float barW = left + half - textLeft;
            drawHpBar(canvas, textLeft, barTop, barW, h * 0.22f, mon.hp, mon.maxHp);
            if (i == 0) {
                String hp = mon.hp + "/" + mon.maxHp;
                f.draw(canvas, hp, left + half - f.measure(hp, textScale * 0.9f),
                        barTop + h * 0.22f + 2, textScale * 0.9f, TEXT_WHITE, 0xFF303840);
            }
        }
        return band;
    }

    /** One beveled command button with its glyph, label, and pressed state. */
    private void drawCommandButton(Canvas canvas, RectF cell, int cmd, boolean pressed,
                                   boolean enabled, float labelScale) {
        GbaFont f = font();
        int fill = enabled ? CMD_FILL[cmd] : 0xFF9AA2AA;
        int light = enabled ? CMD_LIGHT[cmd] : 0xFFC0C6CC;
        int dark = enabled ? CMD_DARK[cmd] : 0xFF6A7278;

        RectF inner = new RectF(cell);
        paint.setColor(dark);
        canvas.drawRoundRect(cell, 16, 16, paint);
        inner.inset(4, 4);
        paint.setColor(pressed ? blendColor(fill, dark, 0.35f) : fill);
        canvas.drawRoundRect(inner, 12, 12, paint);

        // Bevel: light lip on top, shaded base below; pressed inverts it.
        float lip = Math.max(6, inner.height() * 0.09f);
        paint.setColor(pressed ? dark : light);
        canvas.drawRoundRect(new RectF(inner.left + 5, inner.top + 4,
                inner.right - 5, inner.top + 4 + lip), lip / 2, lip / 2, paint);
        if (!pressed) {
            paint.setColor(blendColor(fill, dark, 0.55f));
            canvas.drawRoundRect(new RectF(inner.left + 5, inner.bottom - 4 - lip,
                    inner.right - 5, inner.bottom - 4), lip / 2, lip / 2, paint);
        }

        float shift = pressed ? 3 : 0;
        float glyphR = Math.min(inner.height() * 0.16f, inner.width() * 0.11f);
        float labelH = GbaFont.LINE_HEIGHT * labelScale;
        float totalH = glyphR * 2 + 10 + labelH;
        float contentTop = inner.centerY() - totalH / 2 + shift;
        drawCommandGlyph(canvas, cmd, inner.centerX(), contentTop + glyphR, glyphR,
                enabled ? TEXT_WHITE : 0xFFD8DCE0);
        if (f != null) {
            String label = CMD_LABELS[cmd];
            float w = f.measure(label, labelScale);
            f.draw(canvas, label, inner.centerX() - w / 2, contentTop + glyphR * 2 + 10,
                    labelScale, enabled ? TEXT_WHITE : 0xFFE0E4E8,
                    enabled ? dark : 0xFF787E84);
        }
    }

    /** Small hand-drawn motifs: crossed bars, bag, ball, running chevrons. */
    private void drawCommandGlyph(Canvas canvas, int cmd, float cx, float cy, float r, int color) {
        paint.setColor(color);
        switch (cmd) {
        case 0: // FIGHT: two crossed bars
            for (int d = -1; d <= 1; d += 2) {
                int save = canvas.save();
                canvas.rotate(45 * d, cx, cy);
                canvas.drawRoundRect(new RectF(cx - r, cy - r * 0.22f, cx + r, cy + r * 0.22f),
                        r * 0.22f, r * 0.22f, paint);
                canvas.restoreToCount(save);
            }
            break;
        case 1: // BAG: body with a handle arc
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(r * 0.28f);
            canvas.drawArc(new RectF(cx - r * 0.45f, cy - r * 1.05f, cx + r * 0.45f, cy - r * 0.05f),
                    180, 180, false, paint);
            paint.setStyle(Paint.Style.FILL);
            canvas.drawRoundRect(new RectF(cx - r * 0.8f, cy - r * 0.5f, cx + r * 0.8f, cy + r),
                    r * 0.3f, r * 0.3f, paint);
            break;
        case 2: // POKéMON: a plain ball outline
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(r * 0.26f);
            canvas.drawCircle(cx, cy, r * 0.85f, paint);
            canvas.drawLine(cx - r * 0.85f, cy, cx + r * 0.85f, cy, paint);
            paint.setStyle(Paint.Style.FILL);
            canvas.drawCircle(cx, cy, r * 0.3f, paint);
            break;
        case 3: // RUN: double chevron out the door
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(r * 0.3f);
            paint.setStrokeCap(Paint.Cap.ROUND);
            for (int i = 0; i < 2; i++) {
                float x = cx - r * 0.75f + i * r * 0.75f;
                android.graphics.Path p = new android.graphics.Path();
                p.moveTo(x, cy - r * 0.7f);
                p.lineTo(x + r * 0.6f, cy);
                p.lineTo(x, cy + r * 0.7f);
                canvas.drawPath(p, paint);
            }
            paint.setStrokeCap(Paint.Cap.BUTT);
            paint.setStyle(Paint.Style.FILL);
            break;
        }
    }

    /** Transient in-battle message (e.g. "It won't have any effect."). */
    private void drawBattleNotice(Canvas canvas, float scale) {
        if (battleNotice.isEmpty() || System.currentTimeMillis() - battleNoticeMs > 2500) {
            return;
        }
        GbaFont f = font();
        if (f == null) {
            return;
        }
        float w = f.measure(battleNotice, scale);
        RectF box = new RectF((getWidth() - w) / 2 - 20, getHeight() * 0.40f,
                (getWidth() + w) / 2 + 20,
                getHeight() * 0.40f + GbaFont.LINE_HEIGHT * scale + 22);
        paint.setColor(0xE0303840);
        canvas.drawRoundRect(box, 10, 10, paint);
        f.draw(canvas, battleNotice, box.left + 20, box.top + 11, scale, TEXT_WHITE, 0xFF181C20);
        postInvalidateDelayed(2600); // let the notice clear itself
    }

    /** The battle bag: pocket tabs, usable-item list, description + USE, CANCEL. */
    private void drawBattleBag(Canvas canvas) {
        GbaFont f = font();
        if (f == null) {
            return;
        }
        float height = getHeight();
        float pad = getWidth() * 0.02f;
        float scale = getWidth() / 460f;
        int[] pockets = battleVisiblePockets();
        if (battleBagPocket >= pockets.length) {
            battleBagPocket = 0;
        }
        int pocket = pockets[battleBagPocket];
        int accent = POCKET_ACCENTS[pocket];

        for (int i = 0; i < pockets.length; i++) {
            RectF r = battlePocketRect(i, pockets.length);
            r.inset(5, 5);
            drawPocketTab(canvas, r, i == battleBagPocket, POCKET_NAMES[pockets[i]],
                    scale * 0.85f, POCKET_ACCENTS[pockets[i]], POCKET_ACCENTS_DARK[pockets[i]]);
        }

        float cancelH = height * 0.085f;
        RectF cancel = new RectF(pad, height - cancelH - pad * 0.4f,
                getWidth() - pad, height - pad * 0.4f);
        battleCancel.set(cancel);
        drawBar(canvas, cancel, false, "CANCEL", scale * 0.9f);

        float descH = height * 0.24f;
        RectF descBox = new RectF(pad, cancel.top - pad * 0.5f - descH,
                getWidth() - pad, cancel.top - pad * 0.5f);
        battleBagListTop = height * 0.095f + pad * 0.6f;
        battleBagListBottom = descBox.top - pad * 0.6f;
        battleBagRowH = height * 0.082f;

        java.util.List<DualScreenState.BagItem> items = battleBagItems();
        if (battleBagSelected >= items.size()) {
            battleBagSelected = items.size() - 1;
        }
        battleBagScroll = Math.max(0, Math.min(battleBagMaxScroll(), battleBagScroll));

        if (items.isEmpty()) {
            String message = "No usable items";
            f.draw(canvas, message, (getWidth() - f.measure(message, scale)) / 2,
                    (battleBagListTop + battleBagListBottom) / 2 - GbaFont.LINE_HEIGHT * scale / 2,
                    scale, TEXT_DARK, TEXT_SHADOW);
        } else {
            int save = canvas.save();
            canvas.clipRect(0, battleBagListTop, getWidth(), battleBagListBottom);
            float iconSize = battleBagRowH * 0.85f;
            float rowLeft = pad * 1.6f;
            float rowRight = getWidth() - pad * 1.6f;
            for (int i = 0; i < items.size(); i++) {
                float top = battleBagListTop + i * battleBagRowH - battleBagScroll;
                if (top + battleBagRowH < battleBagListTop || top > battleBagListBottom) {
                    continue;
                }
                DualScreenState.BagItem item = items.get(i);
                if (i == battleBagSelected) {
                    drawSelectionBar(canvas, rowLeft, rowRight, top, battleBagRowH, accent);
                }
                float iconLeft = rowLeft + battleBagRowH * 0.62f;
                Bitmap icon = itemIcon(item.id);
                if (icon != null) {
                    canvas.drawBitmap(icon, null,
                            new RectF(iconLeft, top + (battleBagRowH - iconSize) / 2,
                                      iconLeft + iconSize, top + (battleBagRowH + iconSize) / 2),
                            pixelPaint);
                }
                float textY = top + (battleBagRowH - GbaFont.LINE_HEIGHT * scale) / 2;
                f.draw(canvas, item.name, iconLeft + iconSize + pad * 0.8f, textY,
                        scale, TEXT_DARK, TEXT_SHADOW);
                String qty = "x" + item.quantity;
                float w = f.measure(qty, scale);
                f.draw(canvas, qty, rowRight - pad * 0.8f - w, textY, scale, TEXT_DARK, TEXT_SHADOW);
            }
            canvas.restoreToCount(save);
        }

        // Description box with the USE button, dark like the in-game bag's.
        drawDescBox(canvas, descBox);
        float inset = descBox.height() * 0.14f;
        if (battleBagSelected >= 0 && battleBagSelected < items.size()) {
            DualScreenState.BagItem sel = items.get(battleBagSelected);
            float bigIcon = descBox.height() - inset * 2;
            Bitmap icon = itemIcon(sel.id);
            if (icon != null) {
                canvas.drawBitmap(icon, null,
                        new RectF(descBox.left + inset, descBox.top + inset,
                                  descBox.left + inset + bigIcon, descBox.bottom - inset), pixelPaint);
            }
            RectF use = new RectF(descBox.right - inset - descBox.height() * 1.15f,
                    descBox.top + inset, descBox.right - inset, descBox.bottom - inset);
            battleUseButton.set(use);
            boolean ready = state.battleSub == 1;
            paint.setColor(ready ? 0xFF58A868 : 0xFF8898A0);
            canvas.drawRoundRect(use, 10, 10, paint);
            float useScale = scale * 1.2f;
            f.draw(canvas, "USE", use.centerX() - f.measure("USE", useScale) / 2,
                    use.centerY() - GbaFont.LINE_HEIGHT * useScale / 2, useScale,
                    TEXT_WHITE, ready ? 0xFF3C7A48 : 0xFF687880);

            float textLeft = descBox.left + inset + bigIcon + inset;
            float textRight = use.left - inset;
            f.draw(canvas, sel.name, textLeft, descBox.top + inset, scale,
                    descNameColor(), descShadowColor());
            float descScale = scale * 0.9f;
            float lineY = descBox.top + inset + GbaFont.LINE_HEIGHT * scale * 1.15f;
            for (String line : wrapText(f, itemDescription(sel.id), descScale, textRight - textLeft)) {
                if (lineY + GbaFont.LINE_HEIGHT * descScale > descBox.bottom - inset * 0.5f) {
                    break;
                }
                f.draw(canvas, line, textLeft, lineY, descScale, descTextColor(), descShadowColor());
                lineY += GbaFont.LINE_HEIGHT * descScale * 1.05f;
            }
        } else {
            String hint = "Choose an item.";
            f.draw(canvas, hint, descBox.left + inset,
                    descBox.centerY() - GbaFont.LINE_HEIGHT * scale / 2, scale,
                    descTextColor(), descShadowColor());
        }
        drawBattleNotice(canvas, scale);
    }

    /**
     * The battle party staircase: the same layout as the party tab, with
     * untappable slots (fainted, eggs, on the field, engine-forbidden)
     * drawn in the game's fainted recolor and dimmed. Serves both the
     * switch step (panel 2) and the item-target step (panel 3).
     */
    private void drawBattleParty(Canvas canvas) {
        GbaFont f = font();
        float height = getHeight();
        float pad = getWidth() * 0.02f;
        float scale = getWidth() / 440f;

        boolean sendOut = battlePanel == 2 && battleSendOutWait();
        float cancelH = height * 0.085f;
        RectF cancel = new RectF(pad, height - cancelH - pad * 0.4f,
                getWidth() - pad, height - pad * 0.4f);
        float contentHeight;
        if (sendOut) {
            // Forced send-out is mandatory: no CANCEL bar at all (the rect
            // stays empty, so taps down there fall through to nothing).
            battleCancel.setEmpty();
            contentHeight = height - pad * 0.5f;
        } else {
            battleCancel.set(cancel);
            drawBar(canvas, cancel, false, battlePanel == 3 ? "BACK" : "CANCEL", scale * 0.9f);
            contentHeight = cancel.top - pad * 0.5f;
        }
        int s = (int) Math.min(getWidth() / 240f, contentHeight / 160f);
        if (s < 1) s = 1;
        float ox = (getWidth() - 240f * s) / 2f;
        float oy = (contentHeight - 160f * s) / 2f;
        float nameScale = s * 0.8f;
        float subScale = s * 0.65f;
        if (battlePartyFocus < 0) {
            // Park the button cursor on the first slot this panel would
            // actually accept, so it is visible before any key is pressed.
            battlePartyFocus = firstEnabledPartySlot(
                    Math.min(battlePartyCards.length, state.party.size()));
        }

        for (int i = 0; i < 6; i++) {
            boolean present = i < state.party.size();
            DualScreenState.Mon mon = present ? state.party.get(i) : null;
            boolean lead = i == 0;
            boolean enabled = present && battlePartySlotEnabled(i);
            boolean fainted = present && !mon.isEgg && mon.hp == 0;
            int kind = !present ? 4 : lead ? (mon.isEgg ? 1 : 0) : (mon.isEgg ? 3 : 2);
            float wx = ox + (lead ? 8 : 96) * s;
            float wy = oy + (lead ? 24 : 8 + 24 * (i - 1)) * s;
            RectF slot = new RectF(wx, wy, wx + (lead ? 80 : 144) * s, wy + (lead ? 56 : 24) * s);
            battlePartyCards[i].set(slot);
            drawSlotChrome(canvas, slot, kind, present && (fainted || !enabled) ? 1 : 0);
            if (!present || f == null) continue;

            Bitmap icon = mon.isEgg ? null : monIcon(mon.species);
            float ix = lead ? ox : wx - 8 * s;
            float iy = lead ? wy : wy - 6 * s;
            if (icon != null) {
                canvas.drawBitmap(icon, null, new RectF(ix, iy, ix + 32 * s, iy + 32 * s), pixelPaint);
            }

            String title = mon.isEgg ? "EGG" : mon.nick;
            float nickX = wx + (lead ? 24 : 22) * s;
            float nickY = wy + (lead ? 11 : 3) * s;
            f.draw(canvas, title, nickX, nickY, nameScale, PARTY_TEXT, PARTY_TEXT_SHADOW);
            if (!mon.isEgg) {
                float subY = wy + (lead ? 20 : 12) * s;
                int statusIdx = statusIconIndex(mon.status, mon.hp);
                if (statusIdx < 0) {
                    f.draw(canvas, "Lv" + mon.level, wx + (lead ? 32 : 30) * s, subY,
                            subScale, PARTY_TEXT, PARTY_TEXT_SHADOW);
                    if (mon.gender == 0) {
                        f.draw(canvas, "♂", wx + (lead ? 64 : 62) * s, subY,
                                subScale, PARTY_MALE, PARTY_MALE_SHADOW);
                    } else if (mon.gender == 1) {
                        f.draw(canvas, "♀", wx + (lead ? 64 : 62) * s, subY,
                                subScale, PARTY_FEMALE, PARTY_FEMALE_SHADOW);
                    }
                } else {
                    Bitmap tag = statusIcon(statusIdx);
                    float tx = lead ? ox + 34 * s : wx + 24 * s;
                    float ty = lead ? wy + 24 * s : wy + 15 * s;
                    if (tag != null) {
                        canvas.drawBitmap(tag, null,
                                new RectF(tx, ty, tx + 32 * s, ty + 8 * s), pixelPaint);
                    }
                }

                float barX = wx + (lead ? 24 : 88) * s;
                float barY = wy + (lead ? 35 : 10) * s;
                float fillPx = mon.maxHp > 0 ? 48f * mon.hp / mon.maxHp : 0;
                if (mon.hp > 0 && fillPx < 1) fillPx = 1;
                int level = hpBarLevel(mon.hp, mon.maxHp);
                paint.setColor(PARTY_HP_TOP[level]);
                canvas.drawRect(barX, barY, barX + fillPx * s, barY + s, paint);
                paint.setColor(PARTY_HP_BODY[level]);
                canvas.drawRect(barX, barY + s, barX + fillPx * s, barY + 3 * s, paint);

                String hpText = mon.hp + "/" + mon.maxHp;
                float hpRight = wx + (lead ? 77 : 141) * s;
                float hpY = wy + (lead ? 37 : 12) * s;
                f.draw(canvas, hpText, hpRight - f.measure(hpText, subScale), hpY,
                        subScale, PARTY_TEXT, PARTY_TEXT_SHADOW);
            }

            if (!enabled) {
                paint.setColor(0x50000000);
                canvas.drawRoundRect(slot, 4, 4, paint);
            }
            if (i == battlePartyFocus && enabled) {
                // The game's own cursor lightens the box as well as bordering
                // it (AnimatePartySlot swaps the whole box palette), which is
                // most of why the real one reads at a glance.
                paint.setColor(0x38FFFFFF);
                canvas.drawRoundRect(slot, 4, 4, paint);
                drawCursorRing(canvas, slot, 4, false);
            }
        }

        if (f != null) {
            String message;
            if (battlePanel == 3) {
                message = "Use on which POKéMON?";
            } else if (sendOut) {
                message = "Send out which POKéMON?";
            } else if (state.battleSubCase == 2) {
                message = "Can't switch out now!";
            } else if (state.battleSubCase == 4) {
                message = "An ability prevents switching!";
            } else {
                message = "Switch to which POKéMON?";
            }
            f.draw(canvas, message, ox + 10 * s, oy + 134 * s,
                    s * 0.9f, TEXT_DARK, TEXT_SHADOW);
        }
        drawBattleNotice(canvas, scale);
    }

    private void ensureMapData() {
        if (mapEntries == null) {
            mapEntries = new java.util.ArrayList<>();
            try {
                org.json.JSONArray entries = new org.json.JSONArray(DualScreenBridge.nativeGetRegionMapJson());
                for (int i = 0; i < entries.length(); i++) {
                    org.json.JSONObject o = entries.getJSONObject(i);
                    MapEntry e = new MapEntry();
                    e.id = o.optInt("id");
                    e.x = o.optInt("x");
                    e.y = o.optInt("y");
                    e.w = Math.max(o.optInt("w"), 1);
                    e.h = Math.max(o.optInt("h"), 1);
                    e.name = o.optString("n");
                    mapEntries.add(e);
                }
            } catch (org.json.JSONException ignored) {
            }
        }
        if (regionMap == null) {
            int[] pixels = DualScreenBridge.nativeGetRegionMapImage();
            if (pixels != null && pixels.length == 240 * 160) {
                regionMap = Bitmap.createBitmap(pixels, 240, 160, Bitmap.Config.ARGB_8888);
            }
        }
    }

    private void drawMap(Canvas canvas) {
        ensureMapData();
        GbaFont f = font();
        float contentHeight = getHeight() - tabBarHeight();

        if (regionMap == null) {
            drawCenteredMessage(canvas, "Map unavailable");
            return;
        }

        // Integer-scale the 240x160 map, centered.
        int scale = (int) Math.min(getWidth() / 240f, contentHeight / 160f);
        if (scale < 1) scale = 1;
        float mapW = 240 * scale;
        float mapH = 160 * scale;
        float originX = (getWidth() - mapW) / 2f;
        float originY = (contentHeight - mapH) / 2f;

        paint.setColor(SEA_BLUE);
        canvas.drawRect(new RectF(0, 0, getWidth(), contentHeight), paint);
        canvas.drawBitmap(regionMap, null, new RectF(originX, originY, originX + mapW, originY + mapH), pixelPaint);

        // Player marker: grid starts at tile (1, 2); blink like the game.
        if (state.mapsec >= 0) {
            for (MapEntry e : mapEntries) {
                if (e.id != state.mapsec) continue;
                boolean blink = (System.currentTimeMillis() / 400) % 2 == 0;
                float cx = originX + ((e.x + 1) * 8 + e.w * 4) * scale;
                float cy = originY + ((e.y + 2) * 8 + e.h * 4) * scale;
                paint.setColor(blink ? 0xFFF83030 : 0xFFF8A0A0);
                paint.setStyle(Paint.Style.STROKE);
                paint.setStrokeWidth(3f * scale / 2);
                RectF marker = new RectF(
                        originX + (e.x + 1) * 8 * scale, originY + (e.y + 2) * 8 * scale,
                        originX + ((e.x + 1) * 8 + e.w * 8) * scale, originY + ((e.y + 2) * 8 + e.h * 8) * scale);
                canvas.drawRect(marker, paint);
                paint.setStyle(Paint.Style.FILL);
                canvas.drawCircle(cx, cy, 3f * scale / 2, paint);
                break;
            }
        }

        // Location label box, like the in-game map.
        if (f != null && !state.mapName.isEmpty()) {
            float labelScale = scale * 0.9f;
            float textW = f.measure(state.mapName, labelScale);
            RectF box = new RectF(originX + 8 * scale,
                    originY + mapH - GbaFont.LINE_HEIGHT * labelScale - 18 * scale / 2f,
                    originX + 8 * scale + textW + 24,
                    originY + mapH - 4 * scale / 2f);
            paint.setColor(PANEL_WHITE);
            canvas.drawRoundRect(box, 6, 6, paint);
            paint.setColor(PANEL_BORDER);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(3);
            canvas.drawRoundRect(box, 6, 6, paint);
            paint.setStyle(Paint.Style.FILL);
            f.draw(canvas, state.mapName, box.left + 12,
                    box.centerY() - GbaFont.LINE_HEIGHT * labelScale / 2, labelScale, TEXT_DARK, TEXT_SHADOW);
        }
    }

    /** A pocket tab, like drawBar but themed with the pocket's accent color. */
    private void drawPocketTab(Canvas canvas, RectF r, boolean selected, String label,
                               float textScale, int accent, int accentDark) {
        paint.setColor(accentDark);
        canvas.drawRoundRect(r, 6, 6, paint);
        RectF inner = new RectF(r);
        inner.inset(3, 3);
        paint.setColor(selected ? accent : BAR_CREAM_DARK);
        canvas.drawRoundRect(inner, 4, 4, paint);
        if (selected) {
            paint.setColor(BAR_CREAM);
            RectF edge = new RectF(inner.left, inner.bottom - 6, inner.right, inner.bottom);
            canvas.drawRoundRect(edge, 3, 3, paint);
        }
        GbaFont f = font();
        if (f != null && label != null) {
            float w = f.measure(label, textScale);
            f.draw(canvas, label, r.centerX() - w / 2,
                    r.centerY() - GbaFont.LINE_HEIGHT * textScale / 2, textScale,
                    selected ? TEXT_WHITE : TEXT_DARK,
                    selected ? accentDark : TEXT_SHADOW);
        }
    }

    /** The selected row's cream bar with the pocket-accent cursor arrow. */
    private void drawSelectionBar(Canvas canvas, float rowLeft, float rowRight,
                                  float top, float rowH, int accent) {
        RectF bar = new RectF(rowLeft, top + 2, rowRight, top + rowH - 2);
        paint.setColor(BAR_CREAM);
        canvas.drawRoundRect(bar, 8, 8, paint);
        paint.setColor(BAR_BORDER);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(3);
        canvas.drawRoundRect(bar, 8, 8, paint);
        paint.setStyle(Paint.Style.FILL);
        android.graphics.Path cursor = new android.graphics.Path();
        float cx = rowLeft + rowH * 0.22f;
        float cy = top + rowH / 2;
        cursor.moveTo(cx, cy - rowH * 0.18f);
        cursor.lineTo(cx + rowH * 0.26f, cy);
        cursor.lineTo(cx, cy + rowH * 0.18f);
        cursor.close();
        paint.setColor(accent);
        canvas.drawPath(cursor, paint);
    }

    /** The item description box, dark like the in-game bag's. */
    private void drawDescBox(Canvas canvas, RectF descBox) {
        // Light, with dark text on it. The in-game bag has no dark panel
        // anywhere; slate was what made this read as a modern app.
        paint.setColor(LIST_PANEL);
        canvas.drawRoundRect(descBox, 10, 10, paint);
        paint.setColor(PANEL_BORDER);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(3);
        canvas.drawRoundRect(descBox, 10, 10, paint);
        paint.setStyle(Paint.Style.FILL);
    }

    // Text colors inside the description box.
    private int descNameColor()   { return 0xFF906020; }
    private int descTextColor()   { return TEXT_DARK; }
    private int descShadowColor() { return TEXT_SHADOW; }

    /** Greedy word wrap with the game font; the bridge sends single lines. */
    private java.util.List<String> wrapText(GbaFont f, String text, float scale, float maxWidth) {
        java.util.List<String> lines = new java.util.ArrayList<>();
        StringBuilder line = new StringBuilder();
        for (String word : text.split(" ")) {
            String candidate = line.length() == 0 ? word : line + " " + word;
            if (line.length() > 0 && f.measure(candidate, scale) > maxWidth) {
                lines.add(line.toString());
                line = new StringBuilder(word);
            } else {
                line = new StringBuilder(candidate);
            }
        }
        if (line.length() > 0) {
            lines.add(line.toString());
        }
        return lines;
    }

    private java.util.List<DualScreenState.BagItem> bagItems() {
        return bagPocket < state.bag.size() ? state.bag.get(bagPocket)
                : java.util.Collections.<DualScreenState.BagItem>emptyList();
    }

    private float bagMaxScroll() {
        return Math.max(0, bagItems().size() * bagRowH - (bagListBottom - bagListTop));
    }

    private void handleBagTouch(float x, float y) {
        for (int i = 0; i < POCKET_NAMES.length; i++) {
            if (pocketRect(i).contains(x, y)) {
                if (bagPocket != i) {
                    bagPocket = i;
                    bagScroll = 0;
                }
                invalidate();
                return;
            }
        }
        if (y >= bagListTop && y < bagListBottom && bagRowH > 0) {
            int index = (int) ((y - bagListTop + bagScroll) / bagRowH);
            if (index >= 0 && index < bagItems().size()) {
                bagSelected[bagPocket] = index;
                invalidate();
            }
        }
    }

    private void drawBag(Canvas canvas) {
        GbaFont f = font();
        float contentHeight = getHeight() - tabBarHeight();
        drawContentBackdrop(canvas, contentHeight, BAG_BG_BASE, BAG_BG_WEAVE);
        float pad = getWidth() * 0.02f;
        float scale = getWidth() / 460f;
        int accent = POCKET_ACCENTS[bagPocket];

        for (int i = 0; i < POCKET_NAMES.length; i++) {
            RectF r = pocketRect(i);
            r.inset(5, 5);
            drawPocketTab(canvas, r, i == bagPocket, POCKET_NAMES[i], scale * 0.85f,
                    POCKET_ACCENTS[i], POCKET_ACCENTS_DARK[i]);
        }
        if (f == null) {
            return;
        }

        // Description box docked at the bottom, dark like the in-game bag's.
        float descH = contentHeight * 0.26f;
        RectF descBox = new RectF(pad, contentHeight - descH, getWidth() - pad,
                contentHeight - pad * 0.4f);
        bagListTop = contentHeight * 0.105f + pad * 0.6f;
        bagListBottom = descBox.top - pad * 0.6f;
        bagRowH = contentHeight * 0.082f;

        // The bag screen's light list window behind the rows, so the dark
        // row text stays readable over the slate backdrop.
        paint.setColor(LIST_PANEL);
        canvas.drawRoundRect(new RectF(pad * 0.6f, bagListTop - pad * 0.35f,
                getWidth() - pad * 0.6f, bagListBottom + pad * 0.35f), 10, 10, paint);

        java.util.List<DualScreenState.BagItem> items = bagItems();
        if (items.isEmpty()) {
            float messageY = (bagListTop + bagListBottom) / 2 - GbaFont.LINE_HEIGHT * scale / 2;
            String message = "Empty pocket";
            f.draw(canvas, message, (getWidth() - f.measure(message, scale)) / 2, messageY,
                    scale, TEXT_DARK, TEXT_SHADOW);
            return;
        }
        int selected = Math.min(bagSelected[bagPocket], items.size() - 1);
        bagSelected[bagPocket] = selected;
        bagScroll = Math.max(0, Math.min(bagMaxScroll(), bagScroll));

        // The scrolling item list, clipped so rows slide under the chrome.
        int save = canvas.save();
        canvas.clipRect(0, bagListTop, getWidth(), bagListBottom);
        float iconSize = bagRowH * 0.85f;
        float rowLeft = pad * 1.6f;
        float rowRight = getWidth() - pad * 1.6f;
        for (int i = 0; i < items.size(); i++) {
            float top = bagListTop + i * bagRowH - bagScroll;
            if (top + bagRowH < bagListTop || top > bagListBottom) {
                continue;
            }
            DualScreenState.BagItem item = items.get(i);
            if (i == selected) {
                drawSelectionBar(canvas, rowLeft, rowRight, top, bagRowH, accent);
            }
            float iconLeft = rowLeft + bagRowH * 0.62f;
            Bitmap icon = itemIcon(item.id);
            if (icon != null) {
                canvas.drawBitmap(icon, null,
                        new RectF(iconLeft, top + (bagRowH - iconSize) / 2,
                                  iconLeft + iconSize, top + (bagRowH + iconSize) / 2), pixelPaint);
            }
            float textY = top + (bagRowH - GbaFont.LINE_HEIGHT * scale) / 2;
            f.draw(canvas, item.name, iconLeft + iconSize + pad * 0.8f, textY,
                    scale, TEXT_DARK, TEXT_SHADOW);
            if (bagPocket != 4) { // key items carry no quantity
                String qty = "x" + item.quantity;
                float w = f.measure(qty, scale);
                f.draw(canvas, qty, rowRight - pad * 0.8f - w, textY, scale, TEXT_DARK, TEXT_SHADOW);
            }
        }
        canvas.restoreToCount(save);

        // Docked description of the selected item.
        DualScreenState.BagItem sel = items.get(selected);
        drawDescBox(canvas, descBox);
        float inset = descBox.height() * 0.14f;
        float bigIcon = descBox.height() - inset * 2;
        Bitmap icon = itemIcon(sel.id);
        if (icon != null) {
            canvas.drawBitmap(icon, null,
                    new RectF(descBox.left + inset, descBox.top + inset,
                              descBox.left + inset + bigIcon, descBox.bottom - inset), pixelPaint);
        }
        float textLeft = descBox.left + inset + bigIcon + inset;
        f.draw(canvas, sel.name, textLeft, descBox.top + inset, scale,
                descNameColor(), descShadowColor());
        float descScale = scale * 0.9f;
        float lineY = descBox.top + inset + GbaFont.LINE_HEIGHT * scale * 1.15f;
        for (String line : wrapText(f, itemDescription(sel.id), descScale,
                descBox.right - inset - textLeft)) {
            if (lineY + GbaFont.LINE_HEIGHT * descScale > descBox.bottom - inset * 0.5f) {
                break;
            }
            f.draw(canvas, line, textLeft, lineY, descScale, descTextColor(), descShadowColor());
            lineY += GbaFont.LINE_HEIGHT * descScale * 1.05f;
        }
    }

    private int controllerFocus = -1;
    private String controllerPage = "";
    private final java.util.Map<String, Integer> controllerFocusByPage = new java.util.HashMap<>();
    private final Paint controllerOutlinePaint = new Paint(Paint.ANTI_ALIAS_FLAG);

    public boolean extraControllerAvailable() {
        return moreControllerInputs() && !state.inBattle;
    }

    private boolean moreControllerInputs() {
        return getContext().getSharedPreferences("controller_settings", Context.MODE_PRIVATE)
                .getBoolean("more_inputs", false);
    }

    private static final class ControllerTarget {
        final int id, scroll;
        final RectF rect;
        final Runnable activate;
        ControllerTarget(int id, RectF rect, int scroll, Runnable activate) {
            this.id = id;
            this.rect = new RectF(rect);
            this.scroll = scroll;
            this.activate = activate;
        }
    }

    private void addTarget(java.util.List<ControllerTarget> targets, int id, RectF rect,
                           int scroll, Runnable activate) {
        if (!rect.isEmpty()) targets.add(new ControllerTarget(id, rect, scroll, activate));
    }

    private void addTapTarget(java.util.List<ControllerTarget> targets, int id, RectF rect) {
        final float x = rect.centerX(), y = rect.centerY();
        addTarget(targets, id, rect, 0, () -> {
            if (tab == TAB_SETTINGS) handleSettingsTouch(x, y);
            else if (tab == TAB_BAG) handleBagTouch(x, y);
        });
    }

    private java.util.List<ControllerTarget> controllerTargets() {
        // Pocket buttons belong to one bag page: activating BERRIES must keep
        // focus on BERRIES. Party details have their own focus, separate from
        // the party list, so returning restores the Pokémon that opened them.
        String page = tab + ":" + state.inGame
                + (tab == TAB_SETTINGS ? ":" + secretSettings : "")
                + (tab == TAB_PARTY ? ":" + (detailMon >= 0 ? "detail" : "list") : "");
        if (!page.equals(controllerPage)) {
            controllerFocusByPage.put(controllerPage, controllerFocus);
            controllerPage = page;
            Integer remembered = controllerFocusByPage.get(page);
            controllerFocus = remembered == null ? -1 : remembered;
        }
        java.util.List<ControllerTarget> targets = new java.util.ArrayList<>();
        if (tab == TAB_SETTINGS) {
            if (secretSettings) {
                addTapTarget(targets, 0, moreControllerRect);
                addTapTarget(targets, 1, changeSavePathRect);
                addTapTarget(targets, 2, settingsBackRect);
            } else {
                for (int i = 0; i <= settingRows.length; i++) {
                    final int index = i;
                    RectF rect = i == settingRows.length ? secretSettingsRect : settingRows[i].rect;
                    addTarget(targets, i, rect, 1, () -> {
                        RectF current = index == settingRows.length ? secretSettingsRect : settingRows[index].rect;
                        handleSettingsTouch(current.centerX(), current.centerY());
                    });
                }
            }
        } else if (state.inGame && tab == TAB_PARTY) {
            if (detailMon >= 0) {
                addTarget(targets, 0, new RectF(0, 0, getWidth(), getHeight() - tabBarHeight()), 0,
                        () -> { detailMon = -1; invalidate(); });
            } else {
                for (int i = 0; i < Math.min(state.party.size(), partyCards.length); i++) {
                    final int index = i;
                    addTarget(targets, i, partyCards[i], 0, () -> { detailMon = index; invalidate(); });
                }
            }
        } else if (state.inGame && tab == TAB_BAG) {
            for (int i = 0; i < POCKET_NAMES.length; i++) addTapTarget(targets, i, pocketRect(i));
            for (int i = 0; i < bagItems().size(); i++) {
                final int index = i;
                float y = bagListTop + i * bagRowH - bagScroll;
                addTarget(targets, 100 + i, new RectF(getWidth() * .032f, y,
                        getWidth() * .968f, y + bagRowH), 2,
                        () -> { bagSelected[bagPocket] = index; invalidate(); });
            }
        }
        return targets;
    }

    public void switchControllerTab(int step) {
        if (!moreControllerInputs() || state.inBattle) return;
        tab = (tab + step + TAB_NAMES.length) % TAB_NAMES.length;
        detailMon = -1;
        invalidate();
    }

    private float targetTop(ControllerTarget target) {
        return target.scroll == 1 ? GbaFont.LINE_HEIGHT * (getWidth() / 440f) * 1.6f
                : bagListTop;
    }

    private float targetBottom(ControllerTarget target) {
        return target.scroll == 1 ? getHeight() - tabBarHeight()
                : bagListBottom;
    }

    private void revealControllerTarget(ControllerTarget target) {
        if (target.scroll == 0) return;
        float delta = target.rect.top < targetTop(target) ? target.rect.top - targetTop(target)
                : Math.max(0, target.rect.bottom - targetBottom(target));
        if (target.scroll == 1) settingsScroll = Math.max(0, Math.min(settingsMaxScroll(), settingsScroll + delta));
        if (target.scroll == 2) bagScroll = Math.max(0, Math.min(bagMaxScroll(), bagScroll + delta));
        if (delta != 0) invalidate();
    }

    private ControllerTarget focusedTarget(java.util.List<ControllerTarget> targets) {
        for (ControllerTarget target : targets) if (target.id == controllerFocus) return target;
        for (ControllerTarget target : targets) {
            if (target.scroll == 0 || (target.rect.top >= targetTop(target)
                    && target.rect.bottom <= targetBottom(target))) {
                controllerFocus = target.id;
                return target;
            }
        }
        if (targets.isEmpty()) return null;
        controllerFocus = targets.get(0).id;
        return targets.get(0);
    }

    public void navigateExtra(int action) {
        if (!extraControllerAvailable()) return;
        java.util.List<ControllerTarget> targets = controllerTargets();
        ControllerTarget from = focusedTarget(targets);
        if (from == null) return;
        if (action == NAV_CONFIRM) {
            from.activate.run();
            invalidate();
            return;
        }
        ControllerTarget best = null;
        float score = Float.MAX_VALUE;
        boolean vertical = action == NAV_UP || action == NAV_DOWN;
        // Walk scrolling rows in order, including those outside the viewport.
        if (vertical && from.scroll != 0) {
            int next = from.id + (action == NAV_DOWN ? 1 : -1);
            for (ControllerTarget to : targets)
                if (to.scroll == from.scroll && to.id == next) best = to;
        }
        if (best == null) for (ControllerTarget to : targets) {
            if (to == from) continue;
            if (to.scroll != 0 && (to.rect.centerY() < targetTop(to)
                    || to.rect.centerY() > targetBottom(to))) continue;
            float dx = to.rect.centerX() - from.rect.centerX();
            float dy = to.rect.centerY() - from.rect.centerY();
            boolean ahead = action == NAV_UP ? dy < -1 : action == NAV_DOWN ? dy > 1
                    : action == NAV_LEFT ? dx < -1 : dx > 1;
            float distance = Math.abs(vertical ? dy : dx) + 2 * Math.abs(vertical ? dx : dy);
            if (ahead && distance < score) { best = to; score = distance; }
        }
        if (best != null) {
            controllerFocus = best.id;
            revealControllerTarget(best);
            invalidate();
        }
    }

    private void drawControllerFocus(Canvas canvas) {
        if (!extraControllerAvailable()) return;
        // Details use a full-page return target without a focus outline.
        if (tab == TAB_PARTY && detailMon >= 0) return;
        ControllerTarget target = focusedTarget(controllerTargets());
        if (target == null) return;
        canvas.save();
        if (target.scroll != 0) canvas.clipRect(0, targetTop(target), getWidth(), targetBottom(target));
        float strokeWidth = (tab == TAB_PARTY || tab == TAB_BAG ? 4f : 3f)
                * getResources().getDisplayMetrics().density;
        controllerOutlinePaint.setStyle(Paint.Style.STROKE);
        controllerOutlinePaint.setStrokeWidth(strokeWidth);
        controllerOutlinePaint.setColor(0xFF303030); // Opaque darker gray.
        RectF highlight = new RectF(target.rect);
        if (tab == TAB_PARTY) {
            // Trim one artwork pixel on top, two on the right, and four on the bottom.
            highlight.top += target.rect.height() / 56f;
            highlight.right -= 2f * target.rect.width() / 80f;
            highlight.bottom -= 4f * target.rect.height() / 56f;
        }
        // Keep the stroke inside the previously adjusted highlight bounds.
        highlight.inset(strokeWidth / 2f, strokeWidth / 2f);
        canvas.drawRoundRect(highlight, 8, 8, controllerOutlinePaint);
        canvas.restore();
    }

    private static final class SettingRow {
        final String label;
        final int setting;
        final int valueScale; // stored value = index * valueScale
        final String[] values;
        final RectF rect = new RectF();

        SettingRow(String label, int setting, int valueScale, String... values) {
            this.label = label;
            this.setting = setting;
            this.valueScale = valueScale;
            this.values = values;
        }
    }

    private final SettingRow[] settingRows = {
        new SettingRow("BACKGROUND", DualScreenBridge.SETTING_BACKGROUND_MODE, 1, "ART", "BLACK", "WHITE"),
        new SettingRow("WIDESCREEN", DualScreenBridge.SETTING_WIDESCREEN, 1, "OFF", "ON"),
        new SettingRow("TOUCH CONTROLS", DualScreenBridge.SETTING_TOUCH_CONTROLS, 1, "OFF", "ON"),
        new SettingRow("BATTLE MENUS", DualScreenBridge.SETTING_BATTLE_UI_TOP, 1, "BOTTOM", "TOP"),
        new SettingRow("FAST FORWARD", DualScreenBridge.SETTING_FAST_FORWARD, 1, "OFF", "2X", "3X", "4X"),
        new SettingRow("FF MUSIC", DualScreenBridge.SETTING_FF_AUDIO, 1, "NORMAL", "SPED UP"),
        new SettingRow("VOLUME", DualScreenBridge.SETTING_VOLUME, 2, "0", "2", "4", "6", "8", "10"),
        new SettingRow("HINTS", DualScreenBridge.SETTING_BATTLE_HINTS, 1, "OFF", "ON"),
    };
    private boolean secretSettings;
    private Runnable changeSavePathListener;
    private final RectF secretSettingsRect = new RectF();
    private final RectF changeSavePathRect = new RectF();
    private final RectF moreControllerRect = new RectF();
    private final RectF settingsBackRect = new RectF();

    public void setChangeSavePathListener(Runnable listener) {
        changeSavePathListener = listener;
    }

    private float settingsScroll;
    private float settingsTouchDownY;
    private float settingsScrollStart;
    private boolean settingsDragging;

    private void handleSettingsTouch(float x, float y) {
        float headerBottom = GbaFont.LINE_HEIGHT * (getWidth() / 440f) * 1.6f;
        if (y < headerBottom || y >= getHeight() - tabBarHeight()) return;
        if (secretSettings) {
            if (moreControllerRect.contains(x, y)) {
                getContext().getSharedPreferences("controller_settings", Context.MODE_PRIVATE)
                        .edit().putBoolean("more_inputs", !moreControllerInputs()).apply();
                controllerFocus = -1;
                invalidate();
            } else if (settingsBackRect.contains(x, y)) {
                secretSettings = false;
                invalidate();
            } else if (changeSavePathRect.contains(x, y) && changeSavePathListener != null) {
                changeSavePathListener.run();
            }
            return;
        }
        if (secretSettingsRect.contains(x, y)) {
            secretSettings = true;
            invalidate();
            return;
        }
        for (SettingRow row : settingRows) {
            if (row.rect.contains(x, y)) {
                int index = DualScreenBridge.nativeGetPlatformSetting(row.setting) / row.valueScale;
                index = (index + 1) % row.values.length;
                DualScreenBridge.nativeSetPlatformSetting(row.setting, index * row.valueScale);
                if (settingsListener != null) {
                    settingsListener.run();
                }
                invalidate();
                return;
            }
        }
    }

    private float settingsMaxScroll() {
        float contentHeight = getHeight() - tabBarHeight();
        float pad = getWidth() * 0.03f;
        float scale = getWidth() / 440f;
        float headerBottom = GbaFont.LINE_HEIGHT * scale * 1.6f;
        float rowH = contentHeight * 0.145f;
        float total = (settingRows.length + 1) * (rowH + pad * 0.6f);
        return Math.max(0, total + pad - (contentHeight - headerBottom));
    }

    private void drawSettings(Canvas canvas) {
        GbaFont f = font();
        if (f == null) {
            return;
        }
        float contentHeight = getHeight() - tabBarHeight();
        float pad = getWidth() * 0.03f;
        float scale = getWidth() / 440f;
        float headerBottom = GbaFont.LINE_HEIGHT * scale * 1.6f;
        float rowH = contentHeight * 0.145f;

        if (secretSettings) {
            settingsBackRect.set(pad, contentHeight - pad - rowH, getWidth() - pad, contentHeight - pad);
            changeSavePathRect.set(pad, settingsBackRect.top - pad - rowH,
                    getWidth() - pad, settingsBackRect.top - pad);
            moreControllerRect.set(pad, changeSavePathRect.top - pad - rowH,
                    getWidth() - pad, changeSavePathRect.top - pad);
            drawBar(canvas, moreControllerRect, false, null, scale);
            float inset = rowH * 0.2f;
            String value = moreControllerInputs() ? "ON" : "OFF";
            float chipScale = scale * 0.9f;
            float chipH = rowH * 0.62f;
            float textW = f.measure(value, chipScale);
            RectF chip = new RectF(moreControllerRect.right - inset - textW - chipH,
                    moreControllerRect.centerY() - chipH / 2, moreControllerRect.right - inset,
                    moreControllerRect.centerY() + chipH / 2);
            String label = "MORE CONTROLLER INPUTS";
            float labelScale = Math.min(scale,
                    (chip.left - moreControllerRect.left - inset * 2) / f.measure(label, 1));
            f.draw(canvas, label, moreControllerRect.left + inset,
                    moreControllerRect.centerY() - GbaFont.LINE_HEIGHT * labelScale / 2,
                    labelScale, TEXT_DARK, TEXT_SHADOW);
            paint.setColor(HEADER_GREEN);
            canvas.drawRoundRect(chip, 8, 8, paint);
            f.draw(canvas, value, chip.centerX() - textW / 2,
                    chip.centerY() - GbaFont.LINE_HEIGHT * chipScale / 2,
                    chipScale, TEXT_WHITE, TEXT_GREEN_SHADOW);
            drawSettingsButton(canvas, changeSavePathRect, "CHANGE SAVE FILE PATH", scale);
            drawSettingsButton(canvas, settingsBackRect, "BACK", scale);
            drawHeader(canvas, "SUPER SECRET SETTINGS", scale);
            return;
        }

        canvas.save();
        canvas.clipRect(0, headerBottom, getWidth(), contentHeight);
        float top = headerBottom + pad - settingsScroll;
        for (SettingRow row : settingRows) {
            RectF r = new RectF(pad, top, getWidth() - pad, top + rowH);
            row.rect.set(r);
            if (r.bottom > headerBottom && r.top < contentHeight) {
                drawBar(canvas, r, false, null, scale);
                float inset = rowH * 0.2f;
                f.draw(canvas, row.label, r.left + inset,
                        r.centerY() - GbaFont.LINE_HEIGHT * scale / 2, scale, TEXT_DARK, TEXT_SHADOW);

                int value = DualScreenBridge.nativeGetPlatformSetting(row.setting) / row.valueScale;
                String valueText = row.values[Math.min(value, row.values.length - 1)];
                float chipScale = scale * 0.9f;
                float chipTextW = f.measure(valueText, chipScale);
                float chipH = rowH * 0.62f;
                RectF chip = new RectF(r.right - inset - chipTextW - chipH,
                        r.centerY() - chipH / 2, r.right - inset, r.centerY() + chipH / 2);
                paint.setColor(HEADER_GREEN);
                canvas.drawRoundRect(chip, 8, 8, paint);
                f.draw(canvas, valueText, chip.centerX() - chipTextW / 2,
                        chip.centerY() - GbaFont.LINE_HEIGHT * chipScale / 2,
                        chipScale, TEXT_WHITE, TEXT_GREEN_SHADOW);
            }
            top += rowH + pad * 0.6f;
        }

        secretSettingsRect.set(pad, top, getWidth() - pad, top + rowH);
        drawSettingsButton(canvas, secretSettingsRect, "SUPER SECRET SETTINGS", scale);
        canvas.restore();

        // Header drawn last so rows scroll underneath it.
        drawHeader(canvas, "SETTINGS", scale);
    }

    private void drawSettingsButton(Canvas canvas, RectF rect, String label, float scale) {
        drawBar(canvas, rect, false, null, scale);
        float textScale = Math.min(scale, (rect.width() * 0.9f) / font().measure(label, 1));
        font().draw(canvas, label, rect.centerX() - font().measure(label, textScale) / 2,
                rect.centerY() - GbaFont.LINE_HEIGHT * textScale / 2,
                textScale, TEXT_DARK, TEXT_SHADOW);
    }

    private Bitmap[] badgeSprites;
    private Bitmap trainerPic;
    private int trainerPicGender = -1;

    private void ensureCardAssets() {
        if (badgeSprites == null) {
            int[] pixels = DualScreenBridge.nativeGetBadges();
            if (pixels != null && pixels.length == 8 * 256) {
                badgeSprites = new Bitmap[8];
                for (int i = 0; i < 8; i++) {
                    int[] one = new int[256];
                    System.arraycopy(pixels, i * 256, one, 0, 256);
                    badgeSprites[i] = Bitmap.createBitmap(one, 16, 16, Bitmap.Config.ARGB_8888);
                }
            }
        }
        if (trainerPic == null || trainerPicGender != state.playerGender) {
            int[] pixels = DualScreenBridge.nativeGetTrainerPic(state.playerGender);
            if (pixels != null && pixels.length == 64 * 64) {
                trainerPic = Bitmap.createBitmap(pixels, 64, 64, Bitmap.Config.ARGB_8888);
                trainerPicGender = state.playerGender;
            }
        }
    }

    private void drawTrainerCard(Canvas canvas) {
        GbaFont f = font();
        if (f == null) {
            return;
        }
        ensureCardAssets();
        float contentHeight = getHeight() - tabBarHeight();
        float pad = getWidth() * 0.04f;
        float scale = getWidth() / 420f;
        RectF card = new RectF(pad, pad * 1.5f, getWidth() - pad, contentHeight - pad * 1.5f);

        // Card body color follows the earned stars, like the real card.
        int[] bodyA = {0xFF98D8B0, 0xFFC8B890, 0xFFC8C8D0, 0xFFF8E888, 0xFFF8E888};
        int[] bodyB = {0xFFB8E8C8, 0xFFD8CCA8, 0xFFDCDCE4, 0xFFFFF4A8, 0xFFFFF4A8};
        int[] border = {0xFF58A878, 0xFF988858, 0xFF888898, 0xFFB09838, 0xFFB09838};
        int tier = Math.min(state.stars, 4);

        paint.setColor(border[tier]);
        canvas.drawRoundRect(card, 24, 24, paint);
        RectF body = new RectF(card);
        body.inset(6, 6);
        int save = canvas.save();
        android.graphics.Path clip = new android.graphics.Path();
        clip.addRoundRect(body, 20, 20, android.graphics.Path.Direction.CW);
        canvas.clipPath(clip);
        float stripeH = body.height() / 14f;
        for (int i = 0; i < 15; i++) {
            paint.setColor(i % 2 == 0 ? bodyA[tier] : bodyB[tier]);
            canvas.drawRect(body.left, body.top + i * stripeH, body.right,
                    body.top + (i + 1) * stripeH, paint);
        }

        float inset = body.width() * 0.045f;
        // Like the real card's tilemap, the text rows sit on solid lighter
        // panel strips in the card's own palette family, so the dark text
        // (with its light shadow) reads over the stripe art.
        int panel = blendColor(bodyB[tier], 0xFFFFFFFF, 0.55f);

        // Title chip + ID number.
        RectF chip = new RectF(body.left + inset, body.top + inset,
                body.left + inset + f.measure("TRAINER CARD", scale) + 40,
                body.top + inset + GbaFont.LINE_HEIGHT * scale + 16);
        paint.setColor(0xFF68A8E0);
        canvas.drawRoundRect(chip, 8, 8, paint);
        f.draw(canvas, "TRAINER CARD", chip.left + 20, chip.top + 8, scale, TEXT_DARK, 0xFF4880B8);
        float idLeft = body.right - inset - f.measure("IDNo.00000", scale);
        paint.setColor(panel);
        canvas.drawRoundRect(new RectF(idLeft - 12, chip.top,
                body.right - inset + 12, chip.bottom), 8, 8, paint);
        f.draw(canvas, "IDNo." + String.format("%05d", state.trainerId),
                idLeft, chip.top + 8, scale, TEXT_DARK, TEXT_SHADOW);

        // Trainer portrait, right side.
        float picSize = body.height() * 0.42f;
        if (trainerPic != null) {
            RectF picRect = new RectF(body.right - inset - picSize,
                    body.centerY() - picSize * 0.55f,
                    body.right - inset, body.centerY() - picSize * 0.55f + picSize);
            paint.setColor(bodyB[tier]);
            canvas.drawCircle(picRect.centerX(), picRect.centerY(), picSize * 0.62f, paint);
            paint.setColor(border[tier]);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(4);
            canvas.drawCircle(picRect.centerX(), picRect.centerY(), picSize * 0.62f, paint);
            paint.setStyle(Paint.Style.FILL);
            canvas.drawBitmap(trainerPic, null, picRect, pixelPaint);
        }

        // Name with underline and stars, on its panel strip.
        float nameY = chip.bottom + body.height() * 0.075f;
        paint.setColor(panel);
        canvas.drawRoundRect(new RectF(body.left + inset * 0.55f,
                nameY - GbaFont.LINE_HEIGHT * scale * 0.3f,
                body.left + body.width() * 0.58f,
                nameY + GbaFont.LINE_HEIGHT * scale * 1.4f), 8, 8, paint);
        f.draw(canvas, "NAME: " + state.playerName, body.left + inset, nameY,
                scale * 1.1f, TEXT_DARK, TEXT_SHADOW);
        float underlineY = nameY + GbaFont.LINE_HEIGHT * scale * 1.1f + 6;
        paint.setColor(0xAA606060);
        canvas.drawRect(body.left + inset, underlineY, body.left + body.width() * 0.55f,
                underlineY + 4, paint);
        for (int i = 0; i < state.stars; i++) {
            drawStar(canvas, body.left + body.width() * 0.38f + i * scale * 16,
                    underlineY + scale * 9, scale * 6, 0xFFE8B020);
        }

        // Info rows, values right-aligned like the real card.
        String[][] rowsData = {
            {"MONEY", "$" + state.money},
            {"POKéDEX", Integer.toString(state.dexCaught)},
            {"TIME", state.hours + ":" + String.format("%02d", state.minutes)},
        };
        float rowY = underlineY + body.height() * 0.06f;
        float rowStep = body.height() * 0.135f;
        float valueRight = body.left + body.width() * 0.55f;
        for (int i = 0; i < rowsData.length; i++) {
            // One panel strip per row, like the card tilemap.
            paint.setColor(panel);
            canvas.drawRoundRect(new RectF(body.left + inset * 0.55f,
                    rowY + i * rowStep - GbaFont.LINE_HEIGHT * scale * 0.3f,
                    valueRight + inset * 0.45f,
                    rowY + i * rowStep + GbaFont.LINE_HEIGHT * scale * 1.3f), 8, 8, paint);
            f.draw(canvas, rowsData[i][0], body.left + inset, rowY + i * rowStep,
                    scale, TEXT_DARK, TEXT_SHADOW);
            float w = f.measure(rowsData[i][1], scale);
            f.draw(canvas, rowsData[i][1], valueRight - w, rowY + i * rowStep,
                    scale, TEXT_DARK, TEXT_SHADOW);
        }

        // BADGES band with the real sprites: like the real card, the badge
        // row gets its own solid light band (the same panel treatment as
        // the text rows) so the sprites read over the stripe art.
        float badgesY = body.bottom - body.height() * 0.20f;
        float badgeSize = body.height() * 0.145f;
        float badgeLabelScale = scale * 0.85f;
        float badgeLabelY = badgesY - GbaFont.LINE_HEIGHT * badgeLabelScale - 6;
        paint.setColor(panel);
        canvas.drawRoundRect(new RectF(body.left + inset * 0.55f,
                badgeLabelY - GbaFont.LINE_HEIGHT * badgeLabelScale * 0.3f,
                body.right - inset * 0.55f,
                badgesY + badgeSize * 1.12f), 8, 8, paint);
        f.draw(canvas, "BADGES", body.left + inset, badgeLabelY,
                badgeLabelScale, TEXT_DARK, TEXT_SHADOW);
        float badgeStep = (body.width() - inset * 2) / 8f;
        for (int i = 0; i < 8; i++) {
            if ((state.badgeFlags & (1 << i)) == 0 || badgeSprites == null) {
                continue;
            }
            float cx = body.left + inset + badgeStep * i + badgeStep / 2;
            canvas.drawBitmap(badgeSprites[i], null,
                    new RectF(cx - badgeSize / 2, badgesY, cx + badgeSize / 2, badgesY + badgeSize),
                    pixelPaint);
        }
        canvas.restoreToCount(save);
    }

    private void drawStar(Canvas canvas, float cx, float cy, float r, int color) {
        android.graphics.Path path = new android.graphics.Path();
        for (int i = 0; i < 10; i++) {
            double angle = -Math.PI / 2 + i * Math.PI / 5;
            float radius = (i % 2 == 0) ? r : r * 0.45f;
            float x = cx + (float) Math.cos(angle) * radius;
            float y = cy + (float) Math.sin(angle) * radius;
            if (i == 0) path.moveTo(x, y); else path.lineTo(x, y);
        }
        path.close();
        paint.setColor(color);
        canvas.drawPath(path, paint);
    }
}
