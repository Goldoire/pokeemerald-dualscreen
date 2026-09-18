#include "voxel_roles.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stddef.h>
#include "global.h"

// Metatile ids 0..511 belong to the pair's PRIMARY tileset and 512..1023 to
// its SECONDARY, so the census keys rows by the tileset that owns the id.
static const VoxelRole *FindIn(const struct Tileset *owner, int m)
{
    int i;
    for (i = 0; i < gVoxelRoleSetCount; i++)
    {
        const VoxelRoleSet *s = &gVoxelRoleSets[i];
        int lo = 0, hi;
        if (s->tileset != owner)
            continue;
        hi = s->count - 1;
        while (lo <= hi)
        {
            int mid = (lo + hi) / 2;
            if (s->rows[mid].meta == m)
                return &s->rows[mid];
            if (s->rows[mid].meta < m)
                lo = mid + 1;
            else
                hi = mid - 1;
        }
        return NULL;
    }
    return NULL;
}

const VoxelRole *VoxelRoles_Find(const struct Tileset *primary, const struct Tileset *secondary, int m)
{
    if (m < 0 || m >= 1024)
        return NULL;
    return FindIn(m < 512 ? primary : secondary, m);
}

#endif
#endif
