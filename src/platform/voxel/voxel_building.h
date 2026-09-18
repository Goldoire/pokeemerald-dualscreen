#ifndef GUARD_VOXEL_BUILDING_H
#define GUARD_VOXEL_BUILDING_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

// ---------------------------------------------------------------------------
// BUILDINGS ARE THE RUNS ABOVE THE DOORS (lib/Gen3.lua ctx.buildings;
// DramaticShapes, MIT).
//
// A door is a WARP and warps are map data. Walk the blocked run directly
// north of each warp, then take the neighbouring columns while they carry a
// run whose roofline is within a row of the door's. That is the house, it
// stops at the street, and it needs no palette and no map name. Everything
// blocked that no warp reaches is landscape -- except a free-standing block
// that states its own roof on every column (Rustboro's doorless apartments).
//
// Each building is then measured (lib/Structures.lua buildVolume and
// foundGen3Buildings): its facade the rows of its drawing that are not roof
// plan, its roof a one-cell shed rising from the south eave, the whole thing
// standing on the street in front of its door.
// ---------------------------------------------------------------------------

struct VoxelInstanceTag;

// Find and measure the instance's buildings (needs cells, art and scenery).
// Marks every footprint cell's `building`.
void VoxelBuildings_Find(struct VoxelInstanceTag *inst);

#endif
#endif
#endif // GUARD_VOXEL_BUILDING_H
