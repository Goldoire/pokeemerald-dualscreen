#include "voxel_art.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "fieldmap.h"
#include "voxel_atlas.h"

// The GBA's 15-bit colour space is small enough to histogram directly, and
// indexed art makes colour equality exact: no distance threshold to pick.
#define COLOR_KEYS 32768

// A metatile's top layer is "full" when it covers three quarters of the
// cell; below that it is an object with a silhouette and its bottom layer is
// not necessarily ground (Gen3.lua GROUND_SAMPLE).
#define GROUND_SAMPLE 192

static inline uint16_t KeyOf(uint32_t rgba)
{
    uint32_t r = (rgba & 0xFF) >> 3, g = ((rgba >> 8) & 0xFF) >> 3, b = ((rgba >> 16) & 0xFF) >> 3;
    return (uint16_t)(r | (g << 5) | (b << 10));
}

static inline uint32_t RgbaOfKey(uint32_t k)
{
    return ((k & 0x1F) * 255 / 31) | ((((k >> 5) & 0x1F) * 255 / 31) << 8)
         | ((((k >> 10) & 0x1F) * 255 / 31) << 16) | (255u << 24);
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

// FOLIAGE IS GREEN WITH THE BLUE TAKEN OUT: Hoenn's lawn is a bright
// yellow-green with a strong blue component, its canopies a deep green with
// almost none.
static inline bool IsLeaf(uint32_t c)
{
    float r = (float)(c & 0xFF), g = (float)((c >> 8) & 0xFF), b = (float)((c >> 16) & 0xFF);
    return g > r * 1.10f && g > b * 1.35f;
}

static uint32_t *PixelAt(uint32_t *rgba, int m, int x, int y, int variant)
{
    int ax = (m % VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + x + ((variant & 1) ? VOXEL_ATLAS_QUAD : 0);
    int ay = (m / VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + y + ((variant & 2) ? VOXEL_ATLAS_QUAD : 0);
    return &rgba[ay * VOXEL_ATLAS_SIZE + ax];
}

// The two layers the PLAYER sees: below and above. A COVERED metatile
// draws both of its tile layers below the player (the composed picture,
// wherever either layer drew) and nothing above.
static void Layers(const VoxelArtInput *in, uint32_t *rgba, int m, uint32_t *below, uint32_t *above)
{
    int x, y;
    bool covered = in->layerType[m] == METATILE_LAYER_TYPE_COVERED;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
        {
            int p = y * 16 + x;
            bool topSet = (in->topMask[m * 32 + p / 8] >> (p & 7)) & 1;
            uint32_t bot = *PixelAt(rgba, m, x, y, VOXEL_VAR_GROUND);
            uint32_t top = *PixelAt(rgba, m, x, y, VOXEL_VAR_TOP);
            if (covered)
            {
                below[p] = topSet ? top : bot;
                above[p] = 0;
            }
            else
            {
                below[p] = bot;
                above[p] = topSet ? top : 0;
            }
        }
}

static inline bool Opaque(uint32_t c) { return (c >> 24) != 0; }

void VoxelArt_Build(const VoxelArtInput *in, uint32_t *rgba, VoxelArtInfo *art)
{
    uint32_t *objectHist = calloc(COLOR_KEYS, sizeof(uint32_t));
    uint32_t *groundHist = calloc(COLOR_KEYS, sizeof(uint32_t));
    uint32_t *seamHist = calloc(COLOR_KEYS, sizeof(uint32_t));
    uint32_t *floorHist = calloc(COLOR_KEYS, sizeof(uint32_t));
    uint8_t *bg = calloc(COLOR_KEYS, 1);       // the pair's background (Gen3.analyse)
    uint8_t *mapBg = calloc(COLOR_KEYS, 1);    // ...widened by this map's floors (shapeDataForMap)
    uint32_t groundTotal = 0, floorTotal = 0, mapFloorKey = 0, mapFloorCount = 0;
    uint32_t below[256], above[256];
    int m, p, k;

    memset(art, 0, 1024 * sizeof(VoxelArtInfo));
    if (!objectHist || !groundHist || !seamHist || !floorHist || !bg || !mapBg)
        goto done;

    // PASS ONE and TWO: the above-player layer's colours (object art, the
    // guard against a ground colour that is also something's paint), and the
    // below-player layer under a full top layer (ground by construction).
    for (m = 0; m < 1024; m++)
    {
        VoxelArtInfo *a = &art[m];
        if (!in->used[m])
            continue;
        Layers(in, rgba, m, below, above);
        for (p = 0; p < 256; p++)
            if (Opaque(above[p]))
            {
                a->n2++;
                if (p < 128) a->top2++; else a->bot2++;
                a->rows2[p >> 4]++;
                objectHist[KeyOf(above[p])]++;
            }
        for (p = 0; p < 256; p++)
            if (Opaque(below[p]))
                a->n1++;
        if (a->n2 >= GROUND_SAMPLE)
            for (p = 0; p < 256; p++)
                if (Opaque(below[p]))
                {
                    groundHist[KeyOf(below[p])]++;
                    groundTotal++;
                }
        // WHICH METATILES TILE: the left edge continues the right, the top
        // row the bottom, compared as sequences. A real floor is patterned
        // (at least three colours) and is not foliage.
        if (a->n2 == 0 && a->n1 == 256)
        {
            bool okH = true, okV = true;
            int i, distinct = 0, leafish = 0;
            for (i = 0; i < 16; i++)
            {
                if (KeyOf(below[i * 16]) != KeyOf(below[i * 16 + 15])) okH = false;
                if (KeyOf(below[i]) != KeyOf(below[15 * 16 + i])) okV = false;
            }
            if (okH && okV)
            {
                uint16_t seen[256];
                int ns = 0, j;
                for (p = 0; p < 256; p++)
                {
                    uint16_t key = KeyOf(below[p]);
                    for (j = 0; j < ns; j++)
                        if (seen[j] == key) break;
                    if (j == ns) seen[ns++] = key;
                    if (IsLeaf(below[p])) leafish++;
                }
                distinct = ns;
                if (distinct >= 3 && leafish <= 128)
                {
                    a->seamless = true;
                    for (p = 0; p < 256; p++)
                        seamHist[KeyOf(below[p])]++;
                }
            }
        }
        if (in->floor && in->floor[m])
            for (p = 0; p < 256; p++)
                if (Opaque(below[p]))
                {
                    floorHist[KeyOf(below[p])]++;
                    floorTotal++;
                }
    }
    // INDOORS NOTHING IS DRAWN ON TOP OF THE FLOOR: take the samples from
    // the metatiles that tile instead.
    if (groundTotal < 2048)
        for (k = 0; k < COLOR_KEYS; k++)
            if (seamHist[k])
            {
                groundHist[k] += seamHist[k];
                groundTotal += seamHist[k];
            }
    // THE BACKGROUND SET: used by the ground samples, not leaned on by
    // object art.
    for (k = 0; k < COLOR_KEYS; k++)
    {
        if (groundHist[k] == 0)
            continue;
        if ((double)groundHist[k] >= groundTotal * 0.002 && (double)objectHist[k] < groundHist[k] * 0.5)
            bg[k] = mapBg[k] = 1;
    }
    // ...and THE FLOOR OF THIS PARTICULAR MAP, under the same guard.
    for (k = 0; k < COLOR_KEYS; k++)
    {
        if (floorHist[k] == 0)
            continue;
        if ((double)floorHist[k] >= floorTotal * 0.002 && (double)objectHist[k] < floorHist[k] * 0.5)
            mapBg[k] = 1;
        if (floorHist[k] > mapFloorCount)
        {
            mapFloorCount = floorHist[k];
            mapFloorKey = k;
        }
    }
    if (in->background)
    {
        memset(in->background, 0, VOXEL_ART_BG_BYTES);
        for (k = 0; k < COLOR_KEYS; k++)
            if (mapBg[k])
                in->background[k >> 3] |= 1 << (k & 7);
    }

    // PASS THREE: the object's own colours -- every above-player pixel and
    // every below-player pixel that is not background -- then the shape
    // surface and the ground-filled picture.
    for (m = 0; m < 1024; m++)
    {
        VoxelArtInfo *a = &art[m];
        uint32_t solidN = 0, leaf = 0, leaf2 = 0, warm = 0, dark = 0, bright = 0, mapSolidN = 0;
        float sr = 0, sg = 0, sb = 0;
        uint16_t fillKeys[16];
        uint32_t fillCount[16];
        int nFill = 0, i, x, y, w;
        int x0 = 16, x1 = -1, y0 = 16, y1 = -1, carved = 0, green = 0, brown = 0;
        uint32_t fill;
        uint8_t shape[256];

        if (!in->used[m])
            continue;
        Layers(in, rgba, m, below, above);
        for (p = 0; p < 256; p++)
        {
            int layer;
            for (layer = 1; layer <= 2; layer++)
            {
                uint32_t c = layer == 1 ? below[p] : above[p];
                float rf, gf, bf, mx;
                if (!Opaque(c))
                    continue;
                if (layer == 1 && bg[KeyOf(c)])
                    continue;
                solidN++;
                rf = (c & 0xFF) / 255.0f; gf = ((c >> 8) & 0xFF) / 255.0f; bf = ((c >> 16) & 0xFF) / 255.0f;
                sr += rf; sg += gf; sb += bf;
                if (IsLeaf(c))
                {
                    leaf++;
                    if (layer == 2) leaf2++;
                }
                if (rf > gf * 1.05f && rf > bf * 1.10f) warm++;
                mx = rf > gf ? rf : gf;
                if (bf > mx) mx = bf;
                if (mx < 0.35f) dark++;
                if (mx > 0.80f) bright++;
            }
            // this map's carve: the shape surface
            shape[p] = Opaque(above[p]) || (Opaque(below[p]) && !mapBg[KeyOf(below[p])]);
            if (shape[p])
                mapSolidN++;
            else if (Opaque(below[p]))
            {
                uint16_t key = KeyOf(below[p]);
                for (i = 0; i < nFill; i++)
                    if (fillKeys[i] == key) { fillCount[i]++; break; }
                if (i == nFill && nFill < 16) { fillKeys[nFill] = key; fillCount[nFill] = 1; nFill++; }
            }
        }
        {
            float n = solidN > 0 ? (float)solidN : 1.0f;
            a->solid = solidN / 256.0f;
            if (a->solid > 1.0f) a->solid = 1.0f;
            a->mr = sr / n; a->mg = sg / n; a->mb = sb / n;
            a->leafFrac = leaf / n;
            a->leafFrac2 = leaf2 / n;
            a->leafy = a->solid > 0.25f && a->leafFrac > 0.5f && a->mb < a->mg * 0.66f;
            a->overhead = a->n2 >= GROUND_SAMPLE;
            for (y = 0; y < 16; y++)
                if (a->rows2[y] >= 8) a->rowsCovered++;
            a->overhang = a->rowsCovered >= 14;
            a->warmth = warm / n;
            a->darkness = dark / n;
            a->brightness = bright / n;
            a->mapSolid = mapSolidN / 256.0f;
        }

        // the silhouette the carve leaves, for the readers that measure it
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
            {
                uint32_t c = *PixelAt(rgba, m, x, y, VOXEL_VAR_COMPOSED);
                if (!shape[y * 16 + x])
                    continue;
                carved++;
                a->rowFill[y]++;
                a->colFill[x]++;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
                if (IsGreen(c)) green++;
                if (IsBrown(c)) brown++;
            }
        a->solid8 = carved > 255 ? 255 : carved;
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
            a->boxy = (uint8_t)(carved * 255 / ((x1 - x0 + 1) * (y1 - y0 + 1)));

        // what lies under a cutout: the cell's own commonest ground colour,
        // else the map's floor
        {
            uint32_t best = 0, bestKey = mapFloorKey;
            for (i = 0; i < nFill; i++)
                if (fillCount[i] > best) { best = fillCount[i]; bestKey = fillKeys[i]; }
            a->bgFill = nFill > 0;
            fill = RgbaOfKey(bestKey);
        }
        for (y = 0; y < 16; y++)
            for (x = 0; x < 16; x++)
            {
                uint32_t c = *PixelAt(rgba, m, x, y, VOXEL_VAR_COMPOSED);
                bool object = shape[y * 16 + x];
                *PixelAt(rgba, m, x, y, VOXEL_VAR_GROUND) = object ? fill : c;
                *PixelAt(rgba, m, x, y, VOXEL_VAR_CARVED) = object ? (c | 0xFF000000u) : (c & 0x00FFFFFFu);
            }
    }

done:
    free(objectHist);
    free(groundHist);
    free(seamHist);
    free(floorHist);
    free(bg);
    free(mapBg);
}

#endif
#endif
