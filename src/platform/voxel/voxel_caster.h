#ifndef GUARD_VOXEL_CASTER_H
#define GUARD_VOXEL_CASTER_H
#include <stdbool.h>
#include <stddef.h>

// Exact double-sided occlusion geometry for our carved, grid-aligned hull
// quads. Merge coplanar rectangles independently of texture and shading;
// leave the visible mesh alone. Output is malloc-owned xyz triangle data.
// Unsupported geometry returns false so callers can retain the source.
bool VoxelCaster_MergeQuads(const void *vertices, unsigned count, size_t stride,
                           float **positions, unsigned *positionCount);
#endif
