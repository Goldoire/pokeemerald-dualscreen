#ifndef GUARD_VOXEL_LIGHT_H
#define GUARD_VOXEL_LIGHT_H

#include <stdbool.h>
#include <stddef.h>

// Fixed northwest sun and ambient floor adapted from ZallaxDev's voxel
// lighting at b27f5c64. See ZALLAX-LICENSE.txt and NOTICE.md. The CPU triangle
// acceleration structure below is specific to our GL meshes and carved hulls.
#define VOXEL_SUN_DX 0.85f
#define VOXEL_SUN_DZ 0.55f
#define VOXEL_SUN_AMBIENT 0.70f
#define VOXEL_SUN_REACH 8.0f
#define VOXEL_LIGHT_TRIANGLE_LIMIT 400000u

typedef struct {
    struct VoxelLightTriangle *triangles;
    struct VoxelLightNode *nodes;
    unsigned triangleCount, nodeCount, nodeCapacity;
} VoxelLightTree;

// Vertex positions must occupy the first three floats of each vertex. Tree
// ownership is CPU-only; it survives GL context loss. Build/free are called
// by the renderer thread. Rebuild whenever source positions change.
bool VoxelLightTree_Build(VoxelLightTree *tree, const void *vertices, unsigned vertexCount, size_t stride);
void VoxelLightTree_Free(VoxelLightTree *tree);
bool VoxelLightTree_Occluded(const VoxelLightTree *tree, const float origin[3], const float direction[3], float reach);
size_t VoxelLightTree_Bytes(const VoxelLightTree *tree);
float VoxelLight_Face(float nx, float ny, float nz);
void VoxelLight_Normal(const float *a, const float *b, const float *c, float normal[3]);

#endif
