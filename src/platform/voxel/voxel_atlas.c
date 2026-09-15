#include "voxel_atlas.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "fieldmap.h"
#include "decompress.h"

extern unsigned char PLTT[];
extern unsigned char VRAM_[];

typedef struct {
    const uint8_t *tiles[2];       // 4bpp tile data for ids 0..511 and 512..1023
    const uint16_t *palettes[16];  // 16 colours each
} TileSource;

static inline uint32_t Bgr15ToRgba(uint16_t c)
{
    uint32_t r = (c & 0x1F) * 255 / 31;
    uint32_t g = ((c >> 5) & 0x1F) * 255 / 31;
    uint32_t b = ((c >> 10) & 0x1F) * 255 / 31;
    return r | (g << 8) | (b << 16) | (255u << 24);
}

// Compose one metatile into 16x16 RGBA. Returns opaque pixel counts per layer.
static void ComposeMetatile(const TileSource *src, const uint16_t *metatileData,
                            uint32_t *out /*256*/, int *opaqueBot, int *opaqueTop)
{
    int layer, i, px, py;
    int counts[2] = {0, 0};

    // Layer 0 colour 0 is the backdrop: black. It counts as opaque ground.
    for (i = 0; i < 256; i++)
        out[i] = 255u << 24;

    for (layer = 0; layer < 2; layer++)
    {
        for (i = 0; i < 4; i++)
        {
            int dstX = (i & 1) * 8;
            int dstY = (i >> 1) * 8;
            uint16_t entry = metatileData[layer * 4 + i];
            uint32_t tileId = entry & 0x3FF;
            uint32_t pal = (entry >> 12) & 0xF;
            bool flipX = (entry >> 10) & 1;
            bool flipY = (entry >> 11) & 1;
            const uint8_t *tile;
            const uint16_t *palette = src->palettes[pal];

            if (tileId < 512)
                tile = src->tiles[0] ? src->tiles[0] + tileId * 32 : NULL;
            else
                tile = src->tiles[1] ? src->tiles[1] + (tileId - 512) * 32 : NULL;
            if (tile == NULL || palette == NULL)
                continue;

            for (py = 0; py < 8; py++)
            {
                for (px = 0; px < 8; px++)
                {
                    int sx = flipX ? 7 - px : px;
                    int sy = flipY ? 7 - py : py;
                    uint8_t byte = tile[sy * 4 + sx / 2];
                    uint8_t idx = (sx & 1) ? (byte >> 4) : (byte & 0xF);
                    if (idx == 0)
                        continue;
                    counts[layer]++;
                    out[(dstY + py) * 16 + dstX + px] = Bgr15ToRgba(palette[idx]);
                }
            }
        }
    }
    if (opaqueBot) *opaqueBot = counts[0];
    if (opaqueTop) *opaqueTop = counts[1];
}

static const uint16_t *MetatileData(const struct MapLayout *layout, int m)
{
    if (m < 512)
    {
        if (layout->primaryTileset && layout->primaryTileset->metatiles)
            return layout->primaryTileset->metatiles + m * 8;
    }
    else
    {
        if (layout->secondaryTileset && layout->secondaryTileset->metatiles)
            return layout->secondaryTileset->metatiles + (m - 512) * 8;
    }
    return NULL;
}

static uint16_t MetatileAttributes(const struct MapLayout *layout, int m)
{
    if (m < 512)
    {
        if (layout->primaryTileset && layout->primaryTileset->metatileAttributes)
            return layout->primaryTileset->metatileAttributes[m];
    }
    else
    {
        if (layout->secondaryTileset && layout->secondaryTileset->metatileAttributes)
            return layout->secondaryTileset->metatileAttributes[m - 512];
    }
    return 0;
}

// Only metatiles the map lays are decoded: a tileset's metatile table may be
// shorter than 512 entries, so walking every id would read past it.
static void FindUsedMetatiles(const struct MapLayout *layout, const uint16_t *liveMap, int liveCount, bool *used)
{
    int i, area = layout->width * layout->height;
    memset(used, 0, 1024 * sizeof(bool));
    for (i = 0; i < area; i++)
        used[layout->map[i] & 0x3FF] = true;
    if (layout->border)
        for (i = 0; i < 4; i++)
            used[layout->border[i] & 0x3FF] = true;
    for (i = 0; liveMap && i < liveCount; i++)
        used[liveMap[i] & 0x3FF] = true;
}

static void Decode(const struct MapLayout *layout, const TileSource *src, const uint16_t *liveMap, int liveCount,
                   uint32_t *rgba, VoxelMetaInfo *meta)
{
    static bool used[1024];
    int m;

    FindUsedMetatiles(layout, liveMap, liveCount, used);
    if (rgba)
        memset(rgba, 0, VOXEL_ATLAS_SIZE * VOXEL_ATLAS_SIZE * 4);

    for (m = 0; m < 1024; m++)
    {
        uint32_t px[256];
        int bot = 0, top = 0;
        const uint16_t *data;

        if (!used[m])
            continue;
        data = MetatileData(layout, m);
        if (data == NULL)
            continue;
        ComposeMetatile(src, data, px, &bot, &top);

        if (rgba)
        {
            int ax = (m % VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE;
            int ay = (m / VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE;
            int y;
            for (y = 0; y < 16; y++)
                memcpy(&rgba[(ay + y) * VOXEL_ATLAS_SIZE + ax], &px[y * 16], 16 * 4);
        }
        if (meta)
        {
            VoxelMetaInfo *mi = &meta[m];
            uint16_t attr = MetatileAttributes(layout, m);
            uint32_t sr = 0, sg = 0, sb = 0;
            int i, dark = 0;
            for (i = 0; i < 256; i++)
            {
                uint32_t c = px[i];
                uint32_t r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
                sr += r; sg += g; sb += b;
                if (r < 12 && g < 12 && b < 12)
                    dark++;
            }
            mi->layerType = (attr & METATILE_ATTR_LAYER_MASK) >> METATILE_ATTR_LAYER_SHIFT;
            mi->behavior = attr & METATILE_ATTR_BEHAVIOR_MASK;
            // The top layer goes to the background drawn above sprites unless
            // the metatile is COVERED (both halves at or below the player).
            mi->topOpaque = (mi->layerType == METATILE_LAYER_TYPE_COVERED) ? 0 : (top > 255 ? 255 : top);
            mi->botOpaque = bot > 255 ? 255 : bot;
            mi->avgR = sr / 256;
            mi->avgG = sg / 256;
            mi->avgB = sb / 256;
            // Interior filler is drawn black; a Pokemon Center's cut-off
            // corners keep a stray lit pixel or two, so ask for nearly all.
            mi->allBlack = dark >= 244;
        }
    }
}

void VoxelAtlas_DecodeFromRom(const struct MapLayout *layout, const uint16_t *liveMap, int liveCount,
                              uint32_t *rgba, VoxelMetaInfo *meta)
{
    TileSource src;
    uint8_t *prim = NULL, *sec = NULL;
    int p;

    if (layout == NULL)
        return;
    memset(&src, 0, sizeof(src));
    if (meta)
        memset(meta, 0, 1024 * sizeof(VoxelMetaInfo));

    if (layout->primaryTileset && layout->primaryTileset->tiles)
    {
        prim = calloc(1, 32 * 512);
        if (layout->primaryTileset->isCompressed)
            LZDecompressWram(layout->primaryTileset->tiles, prim);
        else
            memcpy(prim, layout->primaryTileset->tiles, 32 * 512);
        src.tiles[0] = prim;
    }
    if (layout->secondaryTileset && layout->secondaryTileset->tiles)
    {
        sec = calloc(1, 32 * 512);
        if (layout->secondaryTileset->isCompressed)
            LZDecompressWram(layout->secondaryTileset->tiles, sec);
        else
            memcpy(sec, layout->secondaryTileset->tiles, 32 * 512);
        src.tiles[1] = sec;
    }
    for (p = 0; p < 16; p++)
    {
        const struct Tileset *ts = (p < NUM_PALS_IN_PRIMARY) ? layout->primaryTileset : layout->secondaryTileset;
        if (ts && ts->palettes && p < NUM_PALS_TOTAL)
            src.palettes[p] = ts->palettes[p];
    }

    Decode(layout, &src, liveMap, liveCount, rgba, meta);
    free(prim);
    free(sec);
}

void VoxelAtlas_DecodeFromVram(const struct MapLayout *layout, const uint16_t *liveMap, int liveCount, uint32_t *rgba)
{
    TileSource src;
    int p;

    if (layout == NULL || rgba == NULL)
        return;
    src.tiles[0] = VRAM_;
    src.tiles[1] = VRAM_ + 512 * 32;
    for (p = 0; p < 16; p++)
        src.palettes[p] = (const uint16_t *)(PLTT + p * 32);
    Decode(layout, &src, liveMap, liveCount, rgba, NULL);
}

uint32_t VoxelAtlas_VramChecksum(void)
{
    // FNV-style over the BG tile block and the 13 BG palettes; tile animations
    // and fades touch these and nothing else the atlas reads.
    const uint32_t *w = (const uint32_t *)VRAM_;
    const uint32_t *pal = (const uint32_t *)PLTT;
    uint32_t h = 2166136261u;
    int i;
    for (i = 0; i < (1024 * 32) / 4; i += 2)
        h = (h ^ w[i]) * 16777619u;
    for (i = 0; i < (NUM_PALS_TOTAL * 32) / 4; i++)
        h = (h ^ pal[i]) * 16777619u;
    return h;
}

#endif
#endif
