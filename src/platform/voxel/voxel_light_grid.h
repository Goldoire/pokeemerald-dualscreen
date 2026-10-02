#ifndef GUARD_VOXEL_LIGHT_GRID_H
#define GUARD_VOXEL_LIGHT_GRID_H
#include <stdbool.h>
#define VOXEL_LIGHT_BUCKETS 4096u
#define VOXEL_LIGHT_GRID_LIMIT 262144u
#define VOXEL_LIGHT_GRID_ITEMS 65536u
typedef struct { float x0, z0, x1, z1; } VoxelLightBounds;
typedef struct { int next, x, z; unsigned item; } VoxelLightGridNode;
typedef struct {
    int heads[VOXEL_LIGHT_BUCKETS];
    VoxelLightGridNode *nodes;
    unsigned count;
} VoxelLightGrid;

// Bounds describe possible receiver positions, already expanded along the
// shadow direction. Eight-cell buckets return a conservative candidate set.
// Hash collisions are filtered by signed bucket coordinates; no omissions.
bool VoxelLightGrid_Build(VoxelLightGrid *grid, const VoxelLightBounds *bounds, unsigned count);
void VoxelLightGrid_Free(VoxelLightGrid *grid);
int VoxelLightGrid_First(const VoxelLightGrid *grid, float x, float z);
int VoxelLightGrid_Next(const VoxelLightGrid *grid, int node);
#endif
