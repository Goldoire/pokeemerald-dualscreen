#ifndef GUARD_VOXEL_LIGHTING_H
#define GUARD_VOXEL_LIGHTING_H
#include "voxel_mesh.h"

#if defined(PLATFORM_SDL2) && (defined(NATIVE_LINUX) || defined(__ANDROID__))
// False leaves the original mesh colors intact. CPU geometry is bounded to
// VOXEL_LIGHT_TRIANGLE_LIMIT across the complete connected scene.
bool VoxelLighting_Build(VoxelMesh *meshes, int count);
void VoxelLighting_Reset(void);
bool VoxelLighting_Active(void);
float VoxelLighting_SunAt(float x, float y, float z);
#endif
#endif
