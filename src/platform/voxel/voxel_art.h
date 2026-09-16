#ifndef GUARD_VOXEL_ART_H
#define GUARD_VOXEL_ART_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Reading the drawing.
//
// The cartridge states what a cell is; it does not state what shape the thing
// standing on it has. That is in the pixels. Following DramaticShapes'
// Gen 3 shape surface: find the map's GROUND colours (what its walkable floor
// is painted in, minus any colour the object art leans on), carve them out of
// every metatile, and what survives is the silhouette of whatever stands
// there: a tree's crown and trunk, a fence's palings, a signboard on its post.
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t solid;        // silhouette pixels, 0..255 of the 256 in the cell
    uint8_t x0, x1;       // silhouette column extent, inclusive (x1 < x0: none)
    uint8_t y0, y1;       // silhouette row extent, inclusive (0 = top)
    uint8_t green;        // green-dominant silhouette pixels, 0..255
    uint8_t brown;        // brown/wood silhouette pixels, 0..255
    uint8_t topWidth;     // widest silhouette row among rows 0..3
    uint8_t midWidth;     // widest silhouette row among rows 5..10
    uint8_t botWidth;     // widest silhouette row among rows 12..15
    uint8_t boxy;         // silhouette pixels / bbox area, 0..255 (255 = a filled rectangle)
    uint8_t bgFill;       // the metatile's own dominant background colour is known
    uint8_t rowFill[16];  // silhouette pixels per row
    uint8_t colFill[16];  // silhouette pixels per column
} VoxelArtInfo;

struct VoxelInstanceTag;

// Compute ground colours from the instance's floor cells, then the silhouette
// record for every used metatile, and write the ground-filled and carved
// variants into the 1024x1024 atlas buffer (see voxel_atlas.h for the layout).
// `usedByFloor[m]` marks metatiles laid on walkable floor cells;
// `topAbove[m]` says the metatile's top layer is drawn above the player (so
// its pixels are object art, never ground).
// `useCount[m]` is how many cells lay the metatile: a colour is ground only
// when the floor shows more of it than the objects do.
void VoxelArt_Build(const bool *used, const bool *usedByFloor, const bool *topAbove, const uint16_t *useCount,
                    const uint8_t *topMask, uint32_t *rgba1024, VoxelArtInfo *art /*1024*/);

#endif
#endif
#endif // GUARD_VOXEL_ART_H
