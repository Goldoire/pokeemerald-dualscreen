package com.pokeemerald.experimental;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;

import java.util.HashMap;
import java.util.Map;

/**
 * Renders text with the game's own normal Latin font, exported over JNI as a
 * 2bpp glyph atlas. Glyphs are 16x16 with per-glyph advance widths; color
 * index 1 is the text color, 2 the shadow color.
 */
public final class GbaFont {
    public static final int GLYPH_SIZE = 16;
    /** Visual height of a text line in font pixels (glyphs are 15 tall). */
    public static final int LINE_HEIGHT = 15;

    private static GbaFont instance;

    private final int[] widths = new int[256];
    private final int[][] glyphs = new int[256][];
    private final Map<Long, Bitmap> cache = new HashMap<>();
    private final Paint paint = new Paint();

    private GbaFont(int[] atlas) {
        for (int i = 0; i < 256; i++) {
            widths[i] = atlas[i];
            glyphs[i] = new int[GLYPH_SIZE * GLYPH_SIZE];
            System.arraycopy(atlas, 256 + i * 256, glyphs[i], 0, 256);
        }
        paint.setFilterBitmap(false);
    }

    public static GbaFont get() {
        if (instance == null) {
            int[] atlas = DualScreenBridge.nativeGetFontAtlas();
            if (atlas == null || atlas.length < 256 + 256 * 256) {
                return null;
            }
            instance = new GbaFont(atlas);
        }
        return instance;
    }

    /** ASCII to GBA charcode; -1 for unmapped characters. */
    private static int charToGlyph(char c) {
        if (c == ' ') return 0x00;
        if (c >= '0' && c <= '9') return 0xA1 + (c - '0');
        if (c >= 'A' && c <= 'Z') return 0xBB + (c - 'A');
        if (c >= 'a' && c <= 'z') return 0xD5 + (c - 'a');
        switch (c) {
        case '.': return 0xAD;
        case '-': return 0xAE;
        case '—': return 0xAE; // em dash: the game's hyphen glyph
        case ',': return 0xB8;
        case '/': return 0xBA;
        case ':': return 0xF0;
        case '!': return 0xAB;
        case '?': return 0xAC;
        case '\'': return 0xB4;
        case '%': return 0x5B;
        case '(': return 0x5C;
        case ')': return 0x5D;
        case '~': return 0xB0; // ellipsis
        case '$': return 0xB7; // Pokédollar
        case '♂': return 0xB5; // male
        case '♀': return 0xB6; // female
        case 'é': return 0x1B;
        }
        return -1;
    }

    private Bitmap glyphBitmap(int glyph, int fgColor, int shadowColor) {
        long key = ((long) glyph << 40) ^ ((long) (fgColor & 0xFFFFFF) << 8) ^ (shadowColor & 0xFF);
        Bitmap cached = cache.get(key);
        if (cached != null) {
            return cached;
        }
        int[] pixels = new int[GLYPH_SIZE * GLYPH_SIZE];
        int[] indices = glyphs[glyph];
        for (int i = 0; i < pixels.length; i++) {
            pixels[i] = indices[i] == 1 ? fgColor : indices[i] == 2 ? shadowColor : 0;
        }
        Bitmap bitmap = Bitmap.createBitmap(pixels, GLYPH_SIZE, GLYPH_SIZE, Bitmap.Config.ARGB_8888);
        cache.put(key, bitmap);
        return bitmap;
    }

    public float measure(String text, float scale) {
        float w = 0;
        for (int i = 0; i < text.length(); i++) {
            int glyph = charToGlyph(text.charAt(i));
            if (glyph >= 0) {
                w += widths[glyph] * scale;
            }
        }
        return w;
    }

    /** Draws text with its top-left corner at (x, y). Returns the end x. */
    public float draw(Canvas canvas, String text, float x, float y, float scale, int fgColor, int shadowColor) {
        Rect src = new Rect(0, 0, GLYPH_SIZE, GLYPH_SIZE);
        for (int i = 0; i < text.length(); i++) {
            int glyph = charToGlyph(text.charAt(i));
            if (glyph < 0) {
                continue;
            }
            Bitmap bitmap = glyphBitmap(glyph, fgColor, shadowColor);
            RectF dst = new RectF(x, y, x + GLYPH_SIZE * scale, y + GLYPH_SIZE * scale);
            canvas.drawBitmap(bitmap, src, dst, paint);
            x += widths[glyph] * scale;
        }
        return x;
    }
}
