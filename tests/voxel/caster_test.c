#include "voxel_caster.h"
#include "voxel_light.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void quad(float *p, int axis, float plane, float u0, float v0, float u1, float v1)
{
    int u = (axis+1)%3, v = (axis+2)%3;
    static const int us[] = {0,1,1,0,1,0}, vs[] = {0,0,1,0,1,1};
    for (int k = 0; k < 6; ++k) {
        p[k*3+axis] = plane;
        p[k*3+u] = us[k] ? u1 : u0;
        p[k*3+v] = vs[k] ? v1 : v0;
    }
}

int main(void)
{
    float source[18*16], *merged;
    unsigned count;
    VoxelLightTree original = {0}, compact = {0};
    for (int axis = 0; axis < 3; ++axis) {
        // A 4x4 plate with a missing interior voxel must retain its hole.
        int faces = 0;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            if (x == 1 && y == 1) continue;
            quad(source+18*faces++, axis, 2, x/16.f-.25f, y/16.f-.25f,
                 (x+1)/16.f-.25f, (y+1)/16.f-.25f);
        }
        assert(VoxelCaster_MergeQuads(source, faces*6, 12, &merged, &count));
        assert(count < (unsigned)faces*6);
        assert(VoxelLightTree_Build(&original, source, faces*6, 12));
        assert(VoxelLightTree_Build(&compact, merged, count, 12));
        // Compare rays in both directions, with all nonzero axes, including
        // boundaries, holes and negative local positions.
        for (int side = -1; side <= 1; side += 2)
            for (int y = -8; y < 40; ++y) for (int x = -8; x < 40; ++x) {
                float o[3], d[3] = {0};
                o[axis] = 2-side;
                o[(axis+1)%3] = x/128.f-.25f;
                o[(axis+2)%3] = y/128.f-.25f;
                d[axis] = side;
                assert(VoxelLightTree_Occluded(&original, o, d, 4) ==
                       VoxelLightTree_Occluded(&compact, o, d, 4));
                d[(axis+1)%3] = -.15f; d[(axis+2)%3] = .08f;
                assert(VoxelLightTree_Occluded(&original, o, d, 4) ==
                       VoxelLightTree_Occluded(&compact, o, d, 4));
            }
        free(merged);
    }
    // Repeated coincident faces, including reverse winding, occlude once.
    quad(source, 1, 2, 0, 0, 1, 1);
    memcpy(source+18, source, 18*sizeof(float));
    assert(VoxelCaster_MergeQuads(source, 12, 12, &merged, &count));
    assert(count == 6); free(merged);
    // Texture/color attributes are irrelevant to caster ownership.
    float interleaved[6][6];
    for (int v = 0; v < 6; ++v) memcpy(interleaved[v], source+v*3, 12);
    assert(VoxelCaster_MergeQuads(interleaved, 6, sizeof(interleaved[0]), &merged, &count));
    assert(count == 6); free(merged);
    // Never replace non-grid/diagonal or malformed geometry with a box.
    source[0] += .01f;
    assert(!VoxelCaster_MergeQuads(source, 6, 12, &merged, &count));
    assert(!merged && !count);
    quad(source, 1, 2, 0, 0, 1, 1); source[1] += 1;
    assert(!VoxelCaster_MergeQuads(source, 6, 12, &merged, &count));
    assert(!VoxelCaster_MergeQuads(NULL, 6, 12, &merged, &count));
    assert(!VoxelCaster_MergeQuads(NULL, 5, 12, &merged, &count));
    assert(VoxelCaster_MergeQuads(NULL, 0, 12, &merged, &count));
    assert(!merged && !count);
    VoxelLightTree_Free(&original); VoxelLightTree_Free(&compact);
    puts("PASS: exact caster merging, all axes, negative coordinates, holes, ray parity and unsupported geometry");
}
