#ifndef GUARD_VOXEL_TERRACE_H
#define GUARD_VOXEL_TERRACE_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdint.h>

// ---------------------------------------------------------------------------
// Sculpted terrace heights (DramaticShapes data/gen3_terraces.lua; MIT).
//
// A map whose ROM states a single land level draws its relief instead --
// Route 112's plateau behind three rows of cliff, Sootopolis' crater town
// stepping down to its lake -- and every inference of that relief has a map
// it cannot read. So the levels are data: one line per terrace, named by its
// ANCHOR (the first cell of its walkable component in reading order), the
// level in courses above the shore. A listed map takes its levels verbatim.
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t x, y, level;
} VoxelTerraceAnchor;

// The anchors for the map (group, num), NULL when the map is not sculpted.
const VoxelTerraceAnchor *VoxelTerrace_For(int group, int num, int *count);

#endif
#endif
#endif // GUARD_VOXEL_TERRACE_H
