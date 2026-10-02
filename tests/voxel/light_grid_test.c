#include "voxel_light_grid.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    VoxelLightGrid grid = {0};
    VoxelLightBounds boxes[1000];
    assert(VoxelLightGrid_Build(&grid, NULL, 0));
    assert(VoxelLightGrid_First(&grid, 0, 0) == -1);
    for (int i = 0; i < 1000; ++i)
        boxes[i] = (VoxelLightBounds){i*8.f-4000, (i%31)*8.f-120,
                                     i*8.f-3980, (i%31)*8.f-108};
    assert(VoxelLightGrid_Build(&grid, boxes, 1000));
    for (int i = 0; i < 1000; ++i) {
        // Query edges and interiors with negative coordinates and many hash
        // collisions. Every actual overlap must appear exactly once.
        for (int corner = 0; corner < 3; ++corner) {
            float x = boxes[i].x0 + corner*10, z = boxes[i].z0 + corner*6;
            unsigned seen[1000] = {0}, candidates = 0;
            for (int n = VoxelLightGrid_First(&grid, x, z); n >= 0; n = VoxelLightGrid_Next(&grid, n)) {
                unsigned item = grid.nodes[n].item;
                assert(item < 1000 && !seen[item]); seen[item] = 1;
                ++candidates;
            }
            assert(candidates < 10); // Far-away/hash-colliding cells are excluded.
            for (int k = 0; k < 1000; ++k)
                if (x >= boxes[k].x0 && x <= boxes[k].x1 && z >= boxes[k].z0 && z <= boxes[k].z1)
                    assert(seen[k]);
        }
    }
    assert(VoxelLightGrid_First(&grid, NAN, 0) == -1);
    assert(VoxelLightGrid_Next(&grid, -1) == -1);
    boxes[0] = (VoxelLightBounds){-1000000, -1000000, 1000000, 1000000};
    assert(!VoxelLightGrid_Build(&grid, boxes, 1));
    assert(grid.count == 0);
    boxes[0] = (VoxelLightBounds){1, 1, -1, -1};
    assert(!VoxelLightGrid_Build(&grid, boxes, 1));
    boxes[0].x0 = NAN;
    assert(!VoxelLightGrid_Build(&grid, boxes, 1));
    assert(!VoxelLightGrid_Build(&grid, NULL, 1));
    assert(!VoxelLightGrid_Build(&grid, boxes, VOXEL_LIGHT_GRID_ITEMS+1));
    VoxelLightGrid_Free(&grid); VoxelLightGrid_Free(&grid);
    puts("PASS: caster buckets, negative coordinates, inclusive edges, hash collisions, no duplicates and bounded admission");
}
