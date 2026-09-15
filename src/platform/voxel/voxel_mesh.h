#ifndef GUARD_VOXEL_MESH_H
#define GUARD_VOXEL_MESH_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>
#include <stdint.h>
#include "voxel_world.h"

// A cached triangle mesh for one map instance, textured from that
// instance's atlas. Built on the CPU when the world model changes and drawn
// with client vertex arrays, which both desktop GL 2.1 and GLES 1.1 take.
typedef struct {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a;
} VoxelVertex;

// A run of vertices belonging to one column or building, so the draw can
// ghost the ones standing between the camera and the player.
typedef struct {
    int32_t first;        // first vertex
    int32_t count;
    int16_t worldX, worldZ;  // anchor cell (south row for stacks)
    int16_t building;     // building index, or -1
    int16_t rows, cols;   // footprint (rows extend north of the anchor)
    float   top;          // column top, world Y
    float   base;
} VoxelMeshRange;

typedef struct {
    VoxelVertex *verts;
    int count, capacity;
    VoxelMeshRange *ranges;
    int rangeCount, rangeCapacity;
    // Ranges are sorted by `first`; the opaque pass skips ghosted ones.
    bool *ghost;          // per range, this frame
} VoxelMesh;

void VoxelMesh_Init(VoxelMesh *mesh);
void VoxelMesh_Free(VoxelMesh *mesh);
void VoxelMesh_Clear(VoxelMesh *mesh);

// Build the whole instance: ground, water, ledges, decks, columns,
// buildings, furniture and interior shells.
void VoxelMesh_BuildInstance(VoxelMesh *mesh, const VoxelInstance *inst);

// Decide which ranges hide the player from the camera this frame.
void VoxelMesh_ComputeGhosts(VoxelMesh *mesh, float playerX, float playerY, float playerZ, float camTanPitch);

// Draw with the instance's atlas bound. Opaque pass skips ghosted ranges.
void VoxelMesh_DrawOpaque(const VoxelMesh *mesh);
void VoxelMesh_DrawGhosted(const VoxelMesh *mesh);

#endif
#endif
#endif // GUARD_VOXEL_MESH_H
