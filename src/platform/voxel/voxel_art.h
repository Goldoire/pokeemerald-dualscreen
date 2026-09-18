#ifndef GUARD_VOXEL_ART_H
#define GUARD_VOXEL_ART_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Reading the drawing (lib/Gen3.lua `analyse`, `bakeShape`,
// `shapeDataForMap`; DramaticShapes, MIT).
//
// Emerald draws an object on its top layer over the ground on its bottom
// layer, so the ground colours identify themselves: the bottom layer under a
// nearly full top layer (a roof, a crown) is ground by construction. Those
// colours, minus any the object art leans on, are the BACKGROUND set; the
// SHAPE SURFACE is the picture with every below-player pixel painted in a
// background colour, and no above-player pixel over it, cut to alpha 0.
// Everything that makes a shape -- hulls, per-pixel props, grass, flowers --
// reads that surface.
//
// "Below" and "above" are the player's layers: a COVERED metatile draws both
// its tile layers below the player and nothing above.
// ---------------------------------------------------------------------------

typedef struct {
    // --- the reference's per-metatile record (Gen3.analyse) ---
    uint16_t n1, n2;      // opaque pixels drawn below / above the player
    uint16_t top2, bot2;  // above-player pixels in the upper / lower tile row
    float solid;          // non-background share of the cell (below non-bg + all above), 0..1
    float leafFrac;       // foliage share of the drawn pixels
    float leafFrac2;      // foliage share carried on the above-player layer
    float mr, mg, mb;     // mean colour of the drawn (object) pixels, 0..1
    float warmth, darkness, brightness;
    uint8_t rows2[16];    // above-player pixels per row
    uint8_t rowsCovered;  // rows with at least half their width above the player
    bool leafy;           // solid > 0.25, mostly foliage, the blue taken out
    bool overhead;        // n2 >= 192: a surface seen from above over ground
    bool overhang;        // above-player art on >= 14 rows: a roof's eave
    bool seamless;        // tiles with itself (a floor)
    float mapSolid;       // share of the cell that survives THIS map's carve (per-map floor widened)

    // --- the carve's silhouette, measured off the map's shape surface ---
    uint8_t solid8;       // carved pixels, 0..255 of the 256 in the cell
    uint8_t x0, x1;       // silhouette column extent, inclusive (x1 < x0: none)
    uint8_t y0, y1;       // silhouette row extent, inclusive (0 = top)
    uint8_t green;        // green-dominant silhouette pixels, 0..255
    uint8_t brown;        // brown/wood silhouette pixels, 0..255
    uint8_t topWidth;     // widest silhouette row among rows 0..3
    uint8_t midWidth;     // widest silhouette row among rows 5..10
    uint8_t botWidth;     // widest silhouette row among rows 12..15
    uint8_t boxy;         // silhouette pixels / bbox area, 0..255 (255 = a filled rectangle)
    uint8_t bgFill;       // the metatile's own dominant background colour is known
    uint8_t rails;        // rows drawn nearly edge to edge (a fence's rails)
    uint8_t plate;        // rows of the upper part drawn at least 10 wide (a signboard)
    uint8_t rowFill[16];  // silhouette pixels per row
    uint8_t colFill[16];  // silhouette pixels per column
} VoxelArtInfo;

typedef struct {
    const bool *used;          // [1024] metatiles the map lays
    const uint8_t *layerType;  // [1024] METATILE_LAYER_TYPE_*
    const bool *floor;         // [1024] this map's floor metatiles (top walkable ones)
    const uint8_t *topMask;    // [1024 * 32] pixels the top TILE layer set
    uint8_t *background;       // out, optional: [VOXEL_ART_BG_BYTES] bit per 15-bit colour, the map's background set
} VoxelArtInput;

#define VOXEL_ART_BG_BYTES 4096
static inline bool VoxelArt_BgHas(const uint8_t *bits, uint32_t rgba)
{
    uint32_t k = ((rgba & 0xFF) >> 3) | ((((rgba >> 8) & 0xFF) >> 3) << 5) | ((((rgba >> 16) & 0xFF) >> 3) << 10);
    return bits && ((bits[k >> 3] >> (k & 7)) & 1);
}

// On entry the atlas (voxel_atlas.h layout) holds the composed picture in
// its first quadrant, the bottom tile layer in the ground quadrant (alpha 0
// where it draws nothing) and the top tile layer in the top quadrant.
// Measures every used metatile into `art` and rewrites the ground quadrant
// (the picture with the object filled in ground colour: what lies under a
// cutout) and the carved quadrant (the shape surface).
void VoxelArt_Build(const VoxelArtInput *in, uint32_t *rgba1024, VoxelArtInfo *art /*1024*/);

#endif
#endif
#endif // GUARD_VOXEL_ART_H
