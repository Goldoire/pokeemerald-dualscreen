#include "voxel_shadow.h"
#if defined(PLATFORM_SDL2) && (defined(NATIVE_LINUX) || defined(__ANDROID__))
#include "voxel_light.h"
#include <string.h>

void VoxelShadow_Project(VoxelShadowVertex q[4], float x, float z, float base, float width, float height, float receiver)
{
    float low = base - receiver, high = low + height, bottomV = 1.0f;
    // Tall grass or a higher receiving plane covers the sprite's bottom.
    // Crop that part rather than projecting its below-ground pixels.
    if (low < 0 && height > 0) { bottomV = 1 + low / height; low = 0; }
    if (high < 0) { high = 0; bottomV = 0; }
    q[0] = (VoxelShadowVertex){x-width/2 + VOXEL_SUN_DX*low, z+VOXEL_SUN_DZ*low, 0,bottomV};
    q[1] = (VoxelShadowVertex){x+width/2 + VOXEL_SUN_DX*low, z+VOXEL_SUN_DZ*low, 1,bottomV};
    q[2] = (VoxelShadowVertex){x+width/2 + VOXEL_SUN_DX*high, z+VOXEL_SUN_DZ*high, 1,0};
    q[3] = (VoxelShadowVertex){x-width/2 + VOXEL_SUN_DX*high, z+VOXEL_SUN_DZ*high, 0,0};
}

static int Clip(const VoxelShadowVertex *input, int count, VoxelShadowVertex *out, int axis, float edge, int greater)
{
    if (!count) return 0;
    int written = 0;
    VoxelShadowVertex prev = input[count-1];
    float pd = (axis ? prev.z : prev.x) - edge;
    if (!greater) pd = -pd;
    for (int i = 0; i < count; ++i) {
        VoxelShadowVertex cur = input[i];
        float cd = (axis ? cur.z : cur.x) - edge;
        if (!greater) cd = -cd;
        if ((pd >= 0) != (cd >= 0)) {
            float t = pd / (pd-cd);
            out[written++] = (VoxelShadowVertex){
                prev.x+(cur.x-prev.x)*t, prev.z+(cur.z-prev.z)*t,
                prev.u+(cur.u-prev.u)*t, prev.v+(cur.v-prev.v)*t};
        }
        if (cd >= 0) out[written++] = cur;
        prev = cur; pd = cd;
    }
    return written;
}

int VoxelShadow_ClipRect(const VoxelShadowVertex q[4], float x0, float z0, float x1, float z1, VoxelShadowVertex out[12])
{
    VoxelShadowVertex a[12], b[12];
    memcpy(a, q, 4*sizeof(*q));
    int n = Clip(a, 4, b, 0, x0, 1);
    n = Clip(b, n, a, 0, x1, 0);
    n = Clip(a, n, b, 1, z0, 1);
    return Clip(b, n, out, 1, z1, 0);
}

int VoxelShadow_ClipCell(const VoxelShadowVertex q[4], int x, int z, VoxelShadowVertex out[12])
{
    return VoxelShadow_ClipRect(q, x, z, x+1, z+1, out);
}
#endif
