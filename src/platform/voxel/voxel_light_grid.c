#include "voxel_light_grid.h"
#if defined(PLATFORM_SDL2) && (defined(NATIVE_LINUX) || defined(__ANDROID__))
#include <math.h>
#include <stdlib.h>
#include <string.h>

static unsigned Bucket(int x, int z)
{
    unsigned h = (unsigned)x * 73856093u ^ (unsigned)z * 19349663u;
    h ^= h >> 16;
    return h & (VOXEL_LIGHT_BUCKETS - 1);
}

void VoxelLightGrid_Free(VoxelLightGrid *grid)
{
    free(grid->nodes);
    memset(grid, 0, sizeof(*grid));
}

bool VoxelLightGrid_Build(VoxelLightGrid *grid, const VoxelLightBounds *bounds, unsigned count)
{
    unsigned slots = 0;
    VoxelLightGrid_Free(grid);
    if (count > VOXEL_LIGHT_GRID_ITEMS || (count && !bounds)) return false;
    for (unsigned i = 0; i < count; ++i) {
        const VoxelLightBounds *b = &bounds[i];
        if (!isfinite(b->x0) || !isfinite(b->x1) || !isfinite(b->z0) || !isfinite(b->z1) ||
            fabsf(b->x0) > 1000000 || fabsf(b->x1) > 1000000 ||
            fabsf(b->z0) > 1000000 || fabsf(b->z1) > 1000000 || b->x0 > b->x1 || b->z0 > b->z1)
            return false;
        unsigned w = (int)floorf(b->x1/8) - (int)floorf(b->x0/8) + 1;
        unsigned h = (int)floorf(b->z1/8) - (int)floorf(b->z0/8) + 1;
        if (w > VOXEL_LIGHT_GRID_LIMIT / h || w*h > VOXEL_LIGHT_GRID_LIMIT - slots) return false;
        slots += w*h;
    }
    if (!slots) return true;
    grid->nodes = malloc(slots * sizeof(*grid->nodes));
    if (!grid->nodes) return false;
    for (unsigned k = 0; k < VOXEL_LIGHT_BUCKETS; ++k) grid->heads[k] = -1;
    for (unsigned i = 0; i < count; ++i) {
        const VoxelLightBounds *b = &bounds[i];
        int x0 = floorf(b->x0/8), x1 = floorf(b->x1/8);
        int z0 = floorf(b->z0/8), z1 = floorf(b->z1/8);
        for (int z = z0; z <= z1; ++z) for (int x = x0; x <= x1; ++x) {
            unsigned hash = Bucket(x, z), index = grid->count++;
            grid->nodes[index] = (VoxelLightGridNode){grid->heads[hash], x, z, i};
            grid->heads[hash] = index;
        }
    }
    return true;
}

static int Match(const VoxelLightGrid *grid, int node, int x, int z)
{
    while (node >= 0 && (grid->nodes[node].x != x || grid->nodes[node].z != z))
        node = grid->nodes[node].next;
    return node;
}

int VoxelLightGrid_First(const VoxelLightGrid *grid, float x, float z)
{
    if (!grid->count || !isfinite(x) || !isfinite(z) || fabsf(x) > 1000000 || fabsf(z) > 1000000)
        return -1;
    int bx = floorf(x/8), bz = floorf(z/8);
    return Match(grid, grid->heads[Bucket(bx, bz)], bx, bz);
}

int VoxelLightGrid_Next(const VoxelLightGrid *grid, int node)
{
    if (node < 0 || (unsigned)node >= grid->count) return -1;
    const VoxelLightGridNode *n = &grid->nodes[node];
    return Match(grid, n->next, n->x, n->z);
}
#endif
