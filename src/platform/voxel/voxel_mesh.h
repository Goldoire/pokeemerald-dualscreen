#ifndef GUARD_VOXEL_MESH_H
#define GUARD_VOXEL_MESH_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "voxel_world.h"

// A cached triangle mesh for one map instance, textured from that
// instance's atlas. Built on the CPU when the world model changes, kept in
// a vertex buffer object, and drawn with vertex arrays, which both desktop
// GL 2.1 and GLES 1.1 take.
typedef struct {
    float x, y, z;
    float u, v;
    uint8_t r, g, b, a;
} VoxelVertex;

// A run of vertices belonging to one column, tree, canopy plate or
// building, so the draw can ghost the ones standing between the camera and
// the player.
typedef struct {
    int32_t first;        // first vertex
    int32_t count;
    int16_t worldX, worldZ;  // anchor cell (south row for stacks, west column for pairs)
    int16_t building;     // building index, or -1
    int16_t rows, cols;   // footprint (rows extend north of the anchor)
    float   top;          // column top, world Y
    float   base;
} VoxelMeshRange;

// What the build made, for the dump the gates read.
typedef struct {
    int trees, treePairs, bushes, standees, standeeBoxes, standeeTall;
    int boxes, cliffs, flats, supports, lips, lipFaces, mats, matSkirts;
    int canopies, decks, deckFascias, pillars, furniture, buildings, gables, eaves;
    int contactQuads;     // ground quads darkened where something tall stands beside them
} VoxelMeshStats;

// A sample of the contact-shaded ground quads, for the dump.
#define VOXEL_CONTACT_KEEP 40
typedef struct {
    int16_t x, z;
    uint8_t corner[4];    // NW, NE, SE, SW shades
} VoxelContactRecord;

// One building's roof as built: the ridge, its slopes and its gables.
#define VOXEL_ROOF_KEEP 48
typedef struct {
    int building;
    float peak;           // the ridge height
    int frontSlopes;      // slope strips from the eave to the ridge
    int backSlopes;       // strips from the ridge to the back wall
    int frontRows;        // drawn roof rows laid on the front slope
    int gables;
    float eave;           // overhang past the facade, cells
} VoxelRoofRecord;

// One tree hull's measured profile, kept for the dump: the drawing's row
// widths every four pixels from the top and the ring radii built from them.
#define VOXEL_PROFILE_RINGS 17
#define VOXEL_PROFILE_KEEP 40
typedef struct {
    int rows;
    bool pair;
    int metas[4];                         // top row first
    uint8_t width[VOXEL_PROFILE_RINGS];
    float radius[VOXEL_PROFILE_RINGS];
    int rings;
} VoxelTreeProfile;

typedef struct {
    VoxelVertex *verts;
    int count, capacity;
    VoxelMeshRange *ranges;
    int rangeCount, rangeCapacity;
    // Ranges are sorted by `first`; the opaque pass skips ghosted ones.
    bool *ghost;          // per range, this frame
    unsigned vbo;         // GL buffer name, 0 until first drawn
    bool vboDirty;        // vertices changed since the last upload
    VoxelMeshStats stats;
    VoxelTreeProfile profiles[VOXEL_PROFILE_KEEP];
    int profileCount;
    VoxelRoofRecord roofs[VOXEL_ROOF_KEEP];
    int roofCount;
    VoxelContactRecord contacts[VOXEL_CONTACT_KEEP];
    int contactCount;
} VoxelMesh;

void VoxelMesh_Init(VoxelMesh *mesh);
void VoxelMesh_Free(VoxelMesh *mesh);
void VoxelMesh_Clear(VoxelMesh *mesh);

// The GL context was lost: forget the buffer name, re-upload on next draw.
void VoxelMesh_InvalidateGL(VoxelMesh *mesh);

// Build the whole instance: ground, water, ledges with lips, grass mats,
// decks with fascias and pillars, canopy plates, trees, bushes, standees,
// rock and wall columns, buildings, furniture and interior shells.
void VoxelMesh_BuildInstance(VoxelMesh *mesh, const VoxelInstance *inst);

// Decide which ranges hide the player from the camera this frame.
void VoxelMesh_ComputeGhosts(VoxelMesh *mesh, float playerX, float playerY, float playerZ, float camTanPitch);

// Draw with the instance's atlas bound. Opaque pass skips ghosted ranges.
void VoxelMesh_DrawOpaque(VoxelMesh *mesh);
void VoxelMesh_DrawGhosted(VoxelMesh *mesh);

// The build counts as JSON.
void VoxelMesh_WriteStatsJson(FILE *f, const VoxelMesh *mesh);

#endif
#endif
#endif // GUARD_VOXEL_MESH_H
