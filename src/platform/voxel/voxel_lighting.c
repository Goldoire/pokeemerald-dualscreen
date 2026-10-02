#include "voxel_lighting.h"
#if defined(PLATFORM_SDL2) && (defined(NATIVE_LINUX) || defined(__ANDROID__))
#include "voxel_light.h"
#include "voxel_caster.h"
#include "voxel_light_grid.h"
#include "voxel_hull.h"
#include <SDL.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static VoxelLightTree sTerrain[MAX_VOXEL_MAP_INSTANCES];
typedef struct { VoxelLightTree tree; float *positions; unsigned count; } HullCaster;
static HullCaster *sHulls[MAX_VOXEL_MAP_INSTANCES];
static int sHullCounts[MAX_VOXEL_MAP_INSTANCES];
static VoxelMesh *sMeshes;
typedef struct { int instance, stamp; } StampRef;
static StampRef *sStampRefs;
static VoxelLightGrid sStampGrid;
static int sCount;
static unsigned sRays;
static bool sActive;
#define LIGHT_SAMPLES 4096u
typedef struct { float x, y, z, light; bool valid; } Sample;
static Sample sSamples[LIGHT_SAMPLES];

void VoxelLighting_Reset(void)
{
    VoxelLightGrid_Free(&sStampGrid);
    free(sStampRefs);
    sStampRefs = NULL;
    for (int i = 0; i < MAX_VOXEL_MAP_INSTANCES; ++i) {
        VoxelLightTree_Free(&sTerrain[i]);
        for (int k = 0; k < sHullCounts[i]; ++k) {
            VoxelLightTree_Free(&sHulls[i][k].tree);
            free(sHulls[i][k].positions);
        }
        free(sHulls[i]);
        sHulls[i] = NULL;
        sHullCounts[i] = 0;
    }
    memset(sSamples, 0, sizeof(sSamples));
    sActive = false;
    sMeshes = NULL;
    sCount = 0;
    sRays = 0;
}

bool VoxelLighting_Active(void) { return sActive; }

float VoxelLighting_SunAt(float x, float y, float z)
{
    unsigned bits[3], hash;
    float point[] = {x, y + 0.04f, z}, sun[] = {-VOXEL_SUN_DX, 1.0f, -VOXEL_SUN_DZ};
    if (!sActive) return 1.0f;
    memcpy(&bits[0], &x, sizeof(x)); memcpy(&bits[1], &y, sizeof(y)); memcpy(&bits[2], &z, sizeof(z));
    hash = bits[0] * 73856093u ^ bits[1] * 19349663u ^ bits[2] * 83492791u;
    hash ^= hash >> 16; hash *= 0x85ebca6bu; hash ^= hash >> 13;
    Sample *sample = &sSamples[hash & (LIGHT_SAMPLES - 1)];
    if (sample->valid && sample->x == x && sample->y == y && sample->z == z) return sample->light;
    float light = 1.0f;
    ++sRays;
    for (int i = 0; i < sCount && light == 1.0f; ++i) {
        if (gVoxelInstances[i].indoor) continue;
        if (VoxelLightTree_Occluded(&sTerrain[i], point, sun, VOXEL_SUN_REACH)) light = VOXEL_SUN_AMBIENT;
    }
    for (int node = VoxelLightGrid_First(&sStampGrid, x, z); node >= 0 && light == 1.0f;
         node = VoxelLightGrid_Next(&sStampGrid, node)) {
        const StampRef *ref = &sStampRefs[sStampGrid.nodes[node].item];
        const VoxelHullStamp *stamp = &sMeshes[ref->instance].stamps[ref->stamp];
        float local[] = {point[0] - stamp->x, point[1] - stamp->y, point[2] - stamp->z};
        if (VoxelLightTree_Occluded(&sHulls[ref->instance][stamp->tpl].tree, local, sun, VOXEL_SUN_REACH))
            light = VOXEL_SUN_AMBIENT;
    }
    *sample = (Sample){x, y, z, light, true};
    return light;
}

static bool IndexStamps(VoxelMesh *meshes, int count)
{
    unsigned capacity = 0, items = 0;
    for (int i = 0; i < count; ++i) {
        if (gVoxelInstances[i].indoor) continue;
        if ((unsigned)meshes[i].stampCount > VOXEL_LIGHT_GRID_ITEMS - capacity) return false;
        capacity += meshes[i].stampCount;
    }
    if (!capacity) return true;
    sStampRefs = malloc(capacity * sizeof(*sStampRefs));
    VoxelLightBounds *bounds = malloc(capacity * sizeof(*bounds));
    if (!sStampRefs || !bounds) { free(bounds); return false; }
    for (int i = 0; i < count; ++i) {
        if (gVoxelInstances[i].indoor) continue;
        for (int k = 0; k < meshes[i].stampCount; ++k) {
            const VoxelHullStamp *stamp = &meshes[i].stamps[k];
            float lo[3], hi[3];
            if ((unsigned)stamp->tpl >= (unsigned)sHullCounts[i] ||
                !VoxelLightTree_Bounds(&sHulls[i][stamp->tpl].tree, lo, hi)) continue;
            bounds[items] = (VoxelLightBounds){lo[0]+stamp->x, lo[2]+stamp->z,
                hi[0]+stamp->x+VOXEL_SUN_DX*VOXEL_SUN_REACH,
                hi[2]+stamp->z+VOXEL_SUN_DZ*VOXEL_SUN_REACH};
            sStampRefs[items++] = (StampRef){i, k};
        }
    }
    bool built = VoxelLightGrid_Build(&sStampGrid, bounds, items);
    free(bounds);
    return built;
}

static float OldFace(const float n[3], bool hull)
{
    if (fabsf(n[1]) >= fabsf(n[0]) && fabsf(n[1]) >= fabsf(n[2]))
        return hull && n[1] < 0 ? 140.0f : 255.0f;
    if (fabsf(n[2]) >= fabsf(n[0])) return n[2] > 0 ? (hull ? 255.0f : 230.0f) : 173.0f;
    if (hull) return 199.0f;
    return n[0] > 0 ? 214.0f : 184.0f;
}

static void Shade(void *vertices, int count, size_t stride, bool hull)
{
    for (int i = 0; i + 2 < count; i += 3) {
        float n[3], p[3][3];
        for (int k = 0; k < 3; ++k) memcpy(p[k], (char *)vertices + (i+k) * stride, sizeof(p[k]));
        VoxelLight_Normal(p[0], p[1], p[2], n);
        float old = OldFace(n, hull);
        // Terrain tops wind downward in our mesh. Hull undersides really do
        // face down, and should retain their own ambient/contact treatment.
        if (!hull && n[1] < 0) for (int k = 0; k < 3; ++k) n[k] = -n[k];
        float face = VoxelLight_Face(n[0], n[1], n[2]);
        float length = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        bool ground = !hull && length > 0 && n[1] > 0.9f * length;
        for (int k = 0; k < 3; ++k) {
            unsigned char *color = (unsigned char *)vertices + (i+k)*stride + 5*sizeof(float);
            float sun = ground ? VoxelLighting_SunAt(p[k][0], p[k][1], p[k][2]) : 1.0f;
            float light = sun < 1.0f ? VOXEL_SUN_AMBIENT : face;
            for (int c = 0; c < 3; ++c) {
                float value = color[c] * (255.0f / old) * light;
                color[c] = (unsigned char)(value > 255 ? 255 : value);
            }
        }
    }
}

bool VoxelLighting_Build(VoxelMesh *meshes, int count)
{
    Uint32 start = SDL_GetTicks();
    size_t bytes = 0;
    unsigned triangles = 0, sourceTriangles = 0;
    VoxelLighting_Reset();
    if (count <= 0 || count > MAX_VOXEL_MAP_INSTANCES || !meshes || gVoxelInstances[0].indoor) return false;
    // Prepare exact merged caster faces, then admit the entire scene before
    // building trees or modifying any colors. Visible hull meshes stay intact.
    for (int i = 0; i < count; ++i) {
        if (gVoxelInstances[i].indoor) continue;
        unsigned n = (unsigned)meshes[i].count / 3;
        sourceTriangles += n;
        if (n > VOXEL_LIGHT_TRIANGLE_LIMIT - triangles) goto budget;
        triangles += n;
        if (meshes[i].hullTplCount) {
            sHulls[i] = calloc(meshes[i].hullTplCount, sizeof(HullCaster));
            if (!sHulls[i]) goto fail;
            sHullCounts[i] = meshes[i].hullTplCount;
        }
        for (int k = 0; k < meshes[i].hullTplCount; ++k) {
            VoxelHullTemplate *hull = &meshes[i].hullTpls[k];
            HullCaster *caster = &sHulls[i][k];
            sourceTriangles += (unsigned)hull->count / 3;
            if (!VoxelCaster_MergeQuads(hull->verts, hull->count, sizeof(VoxelHullVertex),
                                       &caster->positions, &caster->count))
                caster->count = hull->count; // Unsupported future hulls retain exact source geometry.
            n = caster->count / 3;
            if (n > VOXEL_LIGHT_TRIANGLE_LIMIT - triangles) goto budget;
            triangles += n;
        }
    }
    for (int i = 0; i < count; ++i) {
        if (gVoxelInstances[i].indoor) continue;
        VoxelMesh *mesh = &meshes[i];
        if (!VoxelLightTree_Build(&sTerrain[i], mesh->verts, mesh->count, sizeof(VoxelVertex))) goto fail;
        bytes += VoxelLightTree_Bytes(&sTerrain[i]);
        for (int k = 0; k < mesh->hullTplCount; ++k) {
            VoxelHullTemplate *hull = &mesh->hullTpls[k];
            HullCaster *caster = &sHulls[i][k];
            const void *vertices = caster->positions ? (const void *)caster->positions : (const void *)hull->verts;
            size_t stride = caster->positions ? 3 * sizeof(float) : sizeof(VoxelHullVertex);
            if (!VoxelLightTree_Build(&caster->tree, vertices, caster->count, stride)) goto fail;
            bytes += VoxelLightTree_Bytes(&caster->tree);
            free(caster->positions);
            caster->positions = NULL;
        }
    }
    if (!IndexStamps(meshes, count)) goto fail;
    bytes += sStampGrid.count * sizeof(VoxelLightGridNode);
    sMeshes = meshes;
    sCount = count;
    sActive = true;
    for (int i = 0; i < count; ++i) {
        if (gVoxelInstances[i].indoor) continue;
        Shade(meshes[i].verts, meshes[i].count, sizeof(VoxelVertex), false);
        meshes[i].vboDirty = true;
        for (int k = 0; k < meshes[i].hullTplCount; ++k)
            Shade(meshes[i].hullTpls[k].verts, meshes[i].hullTpls[k].count, sizeof(VoxelHullVertex), true);
    }
    SDL_Log("[Voxel] sunlight: %u caster triangles (%u source), %zu CPU bytes, %u rays, %u ms",
            triangles, sourceTriangles, bytes, sRays, SDL_GetTicks()-start);
    return true;
budget:
    SDL_Log("[Voxel] sunlight unavailable: scene exceeds the %u caster-triangle limit", VOXEL_LIGHT_TRIANGLE_LIMIT);
fail:
    VoxelLighting_Reset();
    SDL_Log("[Voxel] sunlight unavailable; retaining original shading");
    return false;
}
#endif
