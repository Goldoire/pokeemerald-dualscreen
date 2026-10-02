#include "voxel_shadow.h"
#include "voxel_light.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static float area(const VoxelShadowVertex *p, int n)
{
    float a = 0;
    for (int i = 0; i < n; ++i) a += p[i].x*p[(i+1)%n].z - p[(i+1)%n].x*p[i].z;
    return fabsf(a)*0.5f;
}

int main(void)
{
    VoxelShadowVertex q[4], out[12];
    VoxelShadow_Project(q, -0.2f, -0.4f, 2, 1, 2, 2);
    assert(fabsf(q[2].x-q[1].x - 2*VOXEL_SUN_DX) < .0001f);
    assert(fabsf(q[2].z-q[1].z - 2*VOXEL_SUN_DZ) < .0001f);
    float total = 0;
    for (int z = -2; z < 4; ++z) for (int x = -2; x < 4; ++x) {
        int n = VoxelShadow_ClipCell(q, x, z, out);
        assert(n >= 0 && n <= 12);
        for (int i = 0; i < n; ++i) {
            assert(out[i].x >= x-.00001f && out[i].x <= x+1.00001f);
            assert(out[i].z >= z-.00001f && out[i].z <= z+1.00001f);
            assert(out[i].u >= -.00001f && out[i].u <= 1.00001f);
            assert(out[i].v >= -.00001f && out[i].v <= 1.00001f);
            // Texture v retains its affine projection, including clipped edges.
            assert(fabsf(out[i].z - (-.4f + (1-out[i].v)*2*VOXEL_SUN_DZ)) < .0001f);
        }
        total += area(out, n);
    }
    assert(fabsf(total-area(q, 4)) < .0001f);
    assert(VoxelShadow_ClipCell(q, 100, 100, out) == 0);
    VoxelShadow_Project(q, 0, 0, 2, 1, 2, 1);
    assert(fabsf(q[0].x - (-.5f+VOXEL_SUN_DX)) < .0001f);
    assert(fabsf(q[0].z - VOXEL_SUN_DZ) < .0001f);
    VoxelShadow_Project(q, 0, 0, 2, 1, 2, 2.5f);
    assert(fabsf(q[0].x + .5f) < .0001f);
    assert(fabsf(q[0].v - .75f) < .0001f);
    assert(fabsf(q[2].z - 1.5f*VOXEL_SUN_DZ) < .0001f);
    int n = VoxelShadow_ClipRect(q, 0, 0, .5f, .5f, out);
    for (int i = 0; i < n; ++i) {
        assert(out[i].x >= -.00001f && out[i].x <= .50001f);
        assert(out[i].z >= -.00001f && out[i].z <= .50001f);
    }
    puts("PASS: shadow direction, negative cells, terrain height/cropping, stair rectangles, UV interpolation and clipped area");
    return 0;
}
