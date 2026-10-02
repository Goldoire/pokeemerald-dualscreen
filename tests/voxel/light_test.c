#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "voxel_light.h"

static void near(float a, float b) { assert(fabsf(a - b) < 0.0001f); }
static void roof(float *p, float x, float y, float z)
{
    float vertices[] = {x,y,z, x+2,y,z, x+2,y,z+2, x,y,z, x+2,y,z+2, x,y,z+2};
    for (int i = 0; i < 18; ++i) p[i] = vertices[i];
}

int main(void)
{
    VoxelLightTree tree = {0};
    const float sun[] = {-VOXEL_SUN_DX, 1.0f, -VOXEL_SUN_DZ};
    float vertices[18], o[3] = {2.7f, 0.0f, 2.1f};
    near(VoxelLight_Face(0, 1, 0), 1);
    near(VoxelLight_Face(1, 0, 0), VOXEL_SUN_AMBIENT);
    near(VoxelLight_Face(0, 0, 1), VOXEL_SUN_AMBIENT);
    assert(VoxelLight_Face(-1, 0, 0) > VoxelLight_Face(0, 0, -1));
    near(VoxelLight_Face(-1, 0, 0), VoxelLight_Face(-10, 0, 0));
    assert(VoxelLightTree_Build(&tree, NULL, 0, 12));
    assert(!VoxelLightTree_Occluded(&tree, o, sun, 8));
    roof(vertices, 0, 2, 0);
    assert(VoxelLightTree_Build(&tree, vertices, 6, 12));
    assert(VoxelLightTree_Occluded(&tree, o, sun, 8));
    assert(!VoxelLightTree_Occluded(&tree, o, sun, 1));
    float northwest[] = {-2, 0, -2}, self[] = {1, 2, 1}, below[] = {1, 0, 1}, vertical[] = {0,1,0};
    assert(!VoxelLightTree_Occluded(&tree, northwest, sun, 8));
    assert(!VoxelLightTree_Occluded(&tree, self, sun, 8));
    assert(VoxelLightTree_Occluded(&tree, below, vertical, 8));
    // Winding is irrelevant: casters are double-sided, as is our renderer.
    for (int k = 0; k < 3; ++k) {
        float tmp = vertices[3+k]; vertices[3+k] = vertices[6+k]; vertices[6+k] = tmp;
    }
    assert(VoxelLightTree_Build(&tree, vertices, 6, 12));
    assert(VoxelLightTree_Occluded(&tree, o, sun, 8));
    // Scene changes require rebuilding; tree storage does not alias meshes.
    roof(vertices, -30, 2, -40);
    assert(VoxelLightTree_Occluded(&tree, o, sun, 8));
    assert(VoxelLightTree_Build(&tree, vertices, 6, 12));
    assert(!VoxelLightTree_Occluded(&tree, o, sun, 8));
    float connected[] = {-27.3f,0,-37.9f};
    assert(VoxelLightTree_Occluded(&tree, connected, sun, 8));
    // Many independent roofs exercise internal BVH nodes and empty gaps.
    float *forest = malloc(18 * 1000 * sizeof(float));
    assert(forest);
    for (int i = 0; i < 1000; ++i) roof(forest + 18*i, 4.0f*i, 2, 0);
    assert(VoxelLightTree_Build(&tree, forest, 6000, 12));
    assert(tree.nodeCount <= tree.nodeCapacity);
    assert(VoxelLightTree_Bytes(&tree) < 2000 * 76);
    for (int i = 0; i < 1000; ++i) {
        float hit[] = {4.0f*i+1, 0, 1}, gap[] = {4.0f*i+3,0,1};
        assert(VoxelLightTree_Occluded(&tree, hit, vertical, 8));
        assert(!VoxelLightTree_Occluded(&tree, gap, vertical, 8));
    }
    free(forest);
    // All split boundaries up to a few thousand triangles stay inside the
    // complete-tree capacity bound, including partial last levels.
    float *same = calloc(9000 * 3, sizeof(float));
    assert(same);
    for (unsigned n = 1; n <= 3000; n += (n < 64 ? 1 : 31)) {
        assert(VoxelLightTree_Build(&tree, same, n*3, 12));
        assert(tree.nodeCount <= tree.nodeCapacity);
    }
    free(same);
    assert(!VoxelLightTree_Build(&tree, vertices, 5, 12));
    assert(tree.nodeCount == 0);
    vertices[0] = NAN;
    assert(!VoxelLightTree_Build(&tree, vertices, 6, 12));
    assert(!VoxelLightTree_Build(&tree, NULL, 6, 12));
    assert(!VoxelLightTree_Build(&tree, NULL, (VOXEL_LIGHT_TRIANGLE_LIMIT+1)*3, 12));
    VoxelLightTree_Free(&tree);
    VoxelLightTree_Free(&tree);
    puts("PASS: sun faces, cast direction/reach, self-shadow, winding, scene changes, BVH gaps and invalid data");
    return 0;
}
