#ifndef GUARD_VOXEL_SHADOW_H
#define GUARD_VOXEL_SHADOW_H
typedef struct { float x, z, u, v; } VoxelShadowVertex;
// Project an upright sprite along the fixed sun onto a horizontal receiver.
void VoxelShadow_Project(VoxelShadowVertex quad[4], float x, float z, float base, float width, float height, float receiver);
// Clip to one receiving terrain cell, preserving interpolated texture UVs.
// Output needs room for 12 vertices. The result is a convex polygon.
int VoxelShadow_ClipCell(const VoxelShadowVertex quad[4], int x, int z, VoxelShadowVertex output[12]);
int VoxelShadow_ClipRect(const VoxelShadowVertex quad[4], float x0, float z0, float x1, float z1, VoxelShadowVertex output[12]);
#endif
