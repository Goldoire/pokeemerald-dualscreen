#include "voxel_art.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdlib.h>
#include <string.h>
#include "voxel_atlas.h"

// The GBA's 15-bit colour space is small enough to histogram directly.
#define COLOR_KEYS 32768

static inline uint16_t KeyOf(uint32_t rgba)
{
    uint32_t r = (rgba & 0xFF) >> 3, g = ((rgba >> 8) & 0xFF) >> 3, b = ((rgba >> 16) & 0xFF) >> 3;
    return (uint16_t)(r | (g << 5) | (b << 10));
}

static inline bool IsGreen(uint32_t c)
{
    int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    return g > 48 && g > r + 12 && g > b + 12;
}

static inline bool IsBrown(uint32_t c)
{
    int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    return r > g && g >= b && r > 60 && r - b > 30;
}

static uint32_t *PixelAt(uint32_t *rgba, int m, int x, int y, int variant)
{
    int ax = (m % VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + x + ((variant & 1) ? VOXEL_ATLAS_QUAD : 0);
    int ay = (m / VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + y + ((variant & 2) ? VOXEL_ATLAS_QUAD : 0);
    return &rgba[ay * VOXEL_ATLAS_SIZE + ax];
}

// Flood the ground colours in from the cell's four edges (Structures.lua
// `floodOutside`). What the flood cannot reach is the silhouette: a crown's
// lawn-green interior stays inside its outline, a path cell empties.
static void FloodOutside(const uint8_t *bg, uint8_t *reach)
{
    int stack[256], sp = 0, p;
    memset(reach, 0, 256);
    for (p = 0; p < 256; p++)
    {
        int x = p & 15, y = p >> 4;
        if ((x == 0 || x == 15 || y == 0 || y == 15) && bg[p] && !reach[p])
        {
            reach[p] = 1;
            stack[sp++] = p;
        }
    }
    while (sp > 0)
    {
        int x, y;
        p = stack[--sp];
        x = p & 15; y = p >> 4;
        if (x > 0 && bg[p - 1] && !reach[p - 1]) { reach[p - 1] = 1; stack[sp++] = p - 1; }
        if (x < 15 && bg[p + 1] && !reach[p + 1]) { reach[p + 1] = 1; stack[sp++] = p + 1; }
        if (y > 0 && bg[p - 16] && !reach[p - 16]) { reach[p - 16] = 1; stack[sp++] = p - 16; }
        if (y < 15 && bg[p + 16] && !reach[p + 16]) { reach[p + 16] = 1; stack[sp++] = p + 16; }
    }
}

void VoxelArt_Build(const bool *used, const bool *usedByFloor, const bool *topAbove, const uint16_t *useCount,
                    const uint8_t *topMask, uint32_t *rgba, VoxelArtInfo *art)
{
    uint32_t *floorHist = calloc(COLOR_KEYS, sizeof(uint32_t));
    uint32_t *objHist = calloc(COLOR_KEYS, sizeof(uint32_t));
    uint8_t *isBg = calloc(COLOR_KEYS, 1);
    uint32_t floorTotal = 0;
    uint32_t mapFloorColour = 0;
    uint32_t mapFloorCount = 0;
    int m, x, y, k;

    memset(art, 0, 1024 * sizeof(VoxelArtInfo));
    if (floorHist == NULL || objHist == NULL || isBg == NULL)
    {
        free(floorHist); free(objHist); free(isBg);
        return;
    }

    // 1. What the floor is painted in (the drawing of every cell of walkable
    //    land, minus any layer drawn above the player) against what the
    //    objects are painted in (every other cell), each weighted by how
    //    many cells lay the metatile. A colour is ground when the floor
    //    shows more of it than the objects do: the lawn's green is, the
    //    crown green that a tree-line shadow borrows is not.
    for (m = 0; m < 1024; m++)
    {
        uint32_t weight = useCount[m] ? useCount[m] : 1;
        if (!used[m])
            continue;
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
            {
                uint32_t c = *PixelAt(rgba, m, x, y, 0);
                bool fromTop = (topMask[m * 32 + (y * 16 + x) / 8] >> ((y * 16 + x) & 7)) & 1;
                if (usedByFloor[m] && !(fromTop && topAbove[m]))
                {
                    floorHist[KeyOf(c)] += weight;
                    floorTotal += weight;
                }
                else
                    objHist[KeyOf(c)] += weight;
            }
    }
    for (k = 0; k < COLOR_KEYS; k++)
    {
        if (floorHist[k] == 0)
            continue;
        if (floorHist[k] * 500 >= floorTotal && floorHist[k] >= objHist[k])
            isBg[k] = 1;
        if (floorHist[k] > mapFloorCount)
        {
            mapFloorCount = floorHist[k];
            mapFloorColour = k;
        }
    }
    // Nothing walkable on this map (an all-water dive map): treat the most
    // common colour of every used tile as the ground so silhouettes still exist.
    if (floorTotal == 0)
    {
        for (m = 0; m < 1024; m++)
        {
            if (!used[m]) continue;
            for (y = 0; y < 16; y++)
                for (x = 0; x < 16; x++)
                    floorHist[KeyOf(*PixelAt(rgba, m, x, y, 0))]++;
        }
        for (k = 0; k < COLOR_KEYS; k++)
            if (floorHist[k] > mapFloorCount) { mapFloorCount = floorHist[k]; mapFloorColour = k; }
        isBg[mapFloorColour] = 1;
    }

    // 2. Silhouettes and the two variants.
    for (m = 0; m < 1024; m++)
    {
        VoxelArtInfo *a = &art[m];
        uint8_t bg[256], reach[256];
        uint32_t bgHist[16];      // small per-metatile histogram of background colours
        uint32_t bgKeys[16];
        int bgN = 0, i;
        uint32_t fill;
        int solid = 0, green = 0, brown = 0, w;
        int x0 = 16, x1 = -1, y0 = 16, y1 = -1;

        if (!used[m])
            continue;
        memset(bgHist, 0, sizeof(bgHist));
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
            {
                uint32_t c = *PixelAt(rgba, m, x, y, 0);
                uint16_t key = KeyOf(c);
                bg[y * 16 + x] = isBg[key];
                if (isBg[key])
                {
                    for (i = 0; i < bgN; i++)
                        if (bgKeys[i] == key) { bgHist[i]++; break; }
                    if (i == bgN && bgN < 16) { bgKeys[bgN] = key; bgHist[bgN] = 1; bgN++; }
                }
            }
        FloodOutside(bg, reach);
        // A cell the map's ground colours do not touch at all may stand on
        // a lawn of its own tileset's green (a sign on the primary tileset's
        // grass in a town paved from the secondary): carve again through the
        // green or earth colours that make up its own border, and keep the
        // result only when a clear silhouette survives.
        {
            int reached = 0;
            for (i = 0; i < 256; i++) reached += reach[i];
            if (reached < 6)
            {
                uint32_t borderKeys[64];
                int borderCount[64], nb = 0, p, total = 0;
                uint8_t bg2[256], reach2[256];
                for (p = 0; p < 256; p++)
                {
                    int bx = p & 15, by = p >> 4;
                    uint32_t c;
                    uint16_t key;
                    if (bx != 0 && bx != 15 && by != 0 && by != 15)
                        continue;
                    c = *PixelAt(rgba, m, bx, by, 0);
                    if (!IsGreen(c) && !IsBrown(c))
                        continue;
                    key = KeyOf(c);
                    for (i = 0; i < nb; i++)
                        if (borderKeys[i] == key) { borderCount[i]++; break; }
                    if (i == nb && nb < 64) { borderKeys[nb] = key; borderCount[nb] = 1; nb++; }
                    total++;
                }
                memset(bg2, 0, sizeof(bg2));
                for (p = 0; p < 256; p++)
                {
                    uint16_t key = KeyOf(*PixelAt(rgba, m, p & 15, p >> 4, 0));
                    for (i = 0; i < nb; i++)
                        if (borderKeys[i] == key && borderCount[i] * 5 >= 60) { bg2[p] = 1; break; }
                }
                if (total >= 20)
                {
                    int reached2 = 0;
                    FloodOutside(bg2, reach2);
                    for (i = 0; i < 256; i++) reached2 += reach2[i];
                    if (reached2 >= 26 && reached2 <= 230)
                        memcpy(reach, reach2, 256);
                }
            }
        }
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
            {
                uint32_t c = *PixelAt(rgba, m, x, y, 0);
                if (reach[y * 16 + x])
                    continue;
                solid++;
                a->rowFill[y]++;
                a->colFill[x]++;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
                if (IsGreen(c)) green++;
                if (IsBrown(c)) brown++;
            }
        a->solid = solid > 255 ? 255 : solid;
        a->green = green > 255 ? 255 : green;
        a->brown = brown > 255 ? 255 : brown;
        a->x0 = x0; a->x1 = x1 < 0 ? 0 : x1;
        a->y0 = y0; a->y1 = y1 < 0 ? 0 : y1;
        if (x1 < x0) { a->x0 = 1; a->x1 = 0; a->y0 = 1; a->y1 = 0; }
        for (y = 0; y < 16; y++)
        {
            if (a->rowFill[y] >= 13) a->rails++;
            if (y < 10 && a->rowFill[y] >= 10) a->plate++;
        }
        w = 0; for (y = 0; y < 4; y++) if (a->rowFill[y] > w) w = a->rowFill[y]; a->topWidth = w;
        w = 0; for (y = 5; y <= 10; y++) if (a->rowFill[y] > w) w = a->rowFill[y]; a->midWidth = w;
        w = 0; for (y = 12; y < 16; y++) if (a->rowFill[y] > w) w = a->rowFill[y]; a->botWidth = w;
        if (x1 >= x0 && y1 >= y0)
        {
            int area = (x1 - x0 + 1) * (y1 - y0 + 1);
            a->boxy = (uint8_t)(solid * 255 / area);
        }
        // the metatile's own dominant background colour, else the map's floor
        {
            uint32_t best = 0, bestKey = mapFloorColour;
            for (i = 0; i < bgN; i++)
                if (bgHist[i] > best) { best = bgHist[i]; bestKey = bgKeys[i]; }
            a->bgFill = bgN > 0;
            fill = ((bestKey & 0x1F) * 255 / 31) | ((((bestKey >> 5) & 0x1F) * 255 / 31) << 8)
                 | ((((bestKey >> 10) & 0x1F) * 255 / 31) << 16) | (255u << 24);
        }
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
            {
                uint32_t c = *PixelAt(rgba, m, x, y, 0);
                bool ground = reach[y * 16 + x] != 0;
                *PixelAt(rgba, m, x, y, 1) = ground ? c : fill;                 // ground under a standee
                *PixelAt(rgba, m, x, y, 2) = ground ? (c & 0x00FFFFFFu) : c;   // the standee itself
            }
    }

    free(floorHist);
    free(objHist);
    free(isBg);
}

#endif
#endif
