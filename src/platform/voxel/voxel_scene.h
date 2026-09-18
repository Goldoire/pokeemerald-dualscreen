#ifndef GUARD_VOXEL_SCENE_H
#define GUARD_VOXEL_SCENE_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// SCENERY THE BEHAVIOUR BYTE DOES NOT NAME (lib/Gen3.lua `buildScenery`;
// DramaticShapes, MIT).
//
// Emerald writes MB_NORMAL on its trees, houses, fences and signs alike, so
// what stands on a blocked cell is read off the art and its neighbours:
//
//   canopy     the top-left cell of a tree drawn as a 2x2 (or Mossdeep's 3x3)
//              block of DIFFERENT metatiles -- one round hull over the block
//   cylinder   a partner cell of a canopy, or a lone blocked leafy cell (a
//              hedge, a tiled wood) -- a hull of its own; a boulder too
//   signpost   one blocked cell standing in the open: a board on a post
//   fence      a thin rail with the ground showing round it
//   prop,
//   tabletop   indoors: a small object with floor showing round it
//
// The grid covers the map body plus a ring of VOXEL_SCENE_RING cells, where
// the map's border patch is tiled outward: that is where most of the trees
// anybody sees stand.
// ---------------------------------------------------------------------------

#define VOXEL_SCENE_RING 8

enum {
    SC_NONE = 0,
    SC_CANOPY,
    SC_CYLINDER,
    SC_SIGNPOST,
    SC_FENCE,
    SC_PROP,
    SC_TABLETOP,
};

struct VoxelInstanceTag;

// Fill the instance's scenery grid. Needs the cells and the art.
void VoxelScene_Classify(struct VoxelInstanceTag *inst);

// Scenery at map cell (cx, cy) of the instance, SC_NONE outside the grid.
int VoxelScene_At(const struct VoxelInstanceTag *inst, int cx, int cy);
int VoxelScene_SpanAt(const struct VoxelInstanceTag *inst, int cx, int cy);     // hull span in cells (2, 3), 0 else
bool VoxelScene_SharedAt(const struct VoxelInstanceTag *inst, int cx, int cy);  // an anchor standing on another tree's cell

// The metatile at map cell (cx, cy), the border patch outside the body.
int VoxelScene_MetatileAt(const struct VoxelInstanceTag *inst, int cx, int cy);

#endif
#endif
#endif // GUARD_VOXEL_SCENE_H
