#ifndef GUARD_VOXEL_MESH_H
#define GUARD_VOXEL_MESH_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include "voxel_world.h"

// Draw a single map tile as 3D geometry at world position (wx, wz)
// Uses OpenGL immediate mode (glBegin/glEnd)
void VoxelMesh_DrawTile(int mapX, int mapY, VoxelVisualShape shape, int metatileId);

// Draw the player billboard at world position (wx, wz)
void VoxelMesh_DrawPlayerBillboard(float wx, float wy, float wz, float camX, float camY, float camZ, unsigned int tex, int w, int h);

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2
#endif // GUARD_VOXEL_MESH_H
