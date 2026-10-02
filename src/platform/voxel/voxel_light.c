#include "voxel_light.h"
#if defined(PLATFORM_SDL2) && (defined(NATIVE_LINUX) || defined(__ANDROID__))
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct VoxelLightTriangle { float p[3][3]; } Triangle;
typedef struct VoxelLightNode {
    float lo[3], hi[3];
    unsigned first, count, left, right;
} Node;

float VoxelLight_Face(float nx, float ny, float nz)
{
    // n.sun / up.sun: level ground stays at full strength. This is the
    // donor's face-light calculation, independent of its Citro3D backend.
    float length = sqrtf(nx * nx + ny * ny + nz * nz), facing;
    if (!isfinite(length) || length <= 0.0f) return 1.0f;
    facing = (ny - VOXEL_SUN_DX * nx - VOXEL_SUN_DZ * nz) / length;
    if (facing <= 0.0f) return VOXEL_SUN_AMBIENT;
    if (facing >= 1.0f) return 1.0f;
    return VOXEL_SUN_AMBIENT + (1.0f - VOXEL_SUN_AMBIENT) * facing;
}

void VoxelLight_Normal(const float *a, const float *b, const float *c, float n[3])
{
    float u[3], v[3];
    for (int k = 0; k < 3; ++k) { u[k] = b[k] - a[k]; v[k] = c[k] - a[k]; }
    n[0] = u[1] * v[2] - u[2] * v[1];
    n[1] = u[2] * v[0] - u[0] * v[2];
    n[2] = u[0] * v[1] - u[1] * v[0];
}

static int Compare(const void *a, const void *b, int axis)
{
    const Triangle *ta = a, *tb = b;
    float ca = ta->p[0][axis] + ta->p[1][axis] + ta->p[2][axis];
    float cb = tb->p[0][axis] + tb->p[1][axis] + tb->p[2][axis];
    return (ca > cb) - (ca < cb);
}
static int CompareX(const void *a, const void *b) { return Compare(a, b, 0); }
static int CompareY(const void *a, const void *b) { return Compare(a, b, 1); }
static int CompareZ(const void *a, const void *b) { return Compare(a, b, 2); }

static unsigned BuildNode(VoxelLightTree *tree, unsigned first, unsigned count)
{
    unsigned index = tree->nodeCount++;
    Node *n = &tree->nodes[index];
    *n = (Node){.first = first, .count = count};
    for (int axis = 0; axis < 3; ++axis)
        n->lo[axis] = n->hi[axis] = tree->triangles[first].p[0][axis];
    for (unsigned i = first; i < first + count; ++i)
        for (int p = 0; p < 3; ++p)
            for (int axis = 0; axis < 3; ++axis) {
                float value = tree->triangles[i].p[p][axis];
                if (value < n->lo[axis]) n->lo[axis] = value;
                if (value > n->hi[axis]) n->hi[axis] = value;
            }
    if (count > 8) {
        int axis = 0;
        int (*comparators[])(const void *, const void *) = {CompareX, CompareY, CompareZ};
        for (int k = 1; k < 3; ++k)
            if (n->hi[k] - n->lo[k] > n->hi[axis] - n->lo[axis]) axis = k;
        qsort(tree->triangles + first, count, sizeof(Triangle), comparators[axis]);
        n->left = BuildNode(tree, first, count / 2);
        n->right = BuildNode(tree, first + count / 2, count - count / 2);
        n->count = 0;
    }
    return index;
}

void VoxelLightTree_Free(VoxelLightTree *tree)
{
    free(tree->triangles);
    free(tree->nodes);
    memset(tree, 0, sizeof(*tree));
}

bool VoxelLightTree_Build(VoxelLightTree *tree, const void *vertices, unsigned vertexCount, size_t stride)
{
    unsigned count = vertexCount / 3;
    VoxelLightTree_Free(tree);
    if (vertexCount % 3 || count > VOXEL_LIGHT_TRIANGLE_LIMIT || stride < 3 * sizeof(float)) return false;
    if (count == 0) return true;
    if (!vertices || stride > (size_t)-1 / vertexCount) return false;
    tree->triangles = malloc(count * sizeof(Triangle));
    // Bound the depth of median splits rather than allocating one node per
    // triangle. Leaves contain <=8 triangles; a complete tree at that depth
    // bounds every partial tree and substantially reduces resident memory.
    unsigned leaves = 1;
    while (leaves < (count + 7) / 8) leaves *= 2;
    tree->nodeCapacity = 2 * leaves - 1;
    tree->nodes = malloc(tree->nodeCapacity * sizeof(Node));
    if (!tree->triangles || !tree->nodes) { VoxelLightTree_Free(tree); return false; }
    for (unsigned t = 0; t < count; ++t)
        for (unsigned p = 0; p < 3; ++p) {
            float xyz[3];
            memcpy(xyz, (const unsigned char *)vertices + (t * 3 + p) * stride, sizeof(xyz));
            for (int k = 0; k < 3; ++k) {
                if (!isfinite(xyz[k]) || fabsf(xyz[k]) > 100000.0f) {
                    VoxelLightTree_Free(tree);
                    return false;
                }
                tree->triangles[t].p[p][k] = xyz[k];
            }
        }
    tree->triangleCount = count;
    BuildNode(tree, 0, count);
    return true;
}

size_t VoxelLightTree_Bytes(const VoxelLightTree *tree)
{
    return tree->triangleCount * sizeof(Triangle) + tree->nodeCapacity * sizeof(Node);
}

bool VoxelLightTree_Bounds(const VoxelLightTree *tree, float lo[3], float hi[3])
{
    if (!tree->nodeCount) return false;
    memcpy(lo, tree->nodes[0].lo, 3 * sizeof(float));
    memcpy(hi, tree->nodes[0].hi, 3 * sizeof(float));
    return true;
}

static bool RayBox(const Node *n, const float o[3], const float d[3], float reach)
{
    float near = 0.025f, far = reach;
    for (int k = 0; k < 3; ++k) {
        if (fabsf(d[k]) < 0.000001f) {
            if (o[k] < n->lo[k] || o[k] > n->hi[k]) return false;
        } else {
            float a = (n->lo[k] - o[k]) / d[k], b = (n->hi[k] - o[k]) / d[k];
            if (a > b) { float tmp = a; a = b; b = tmp; }
            if (a > near) near = a;
            if (b < far) far = b;
            if (near > far) return false;
        }
    }
    return true;
}

static bool RayTriangle(const Triangle *tri, const float o[3], const float d[3], float reach)
{
    float e1[3], e2[3], p[3], t[3], q[3], det, inv, u, v, distance;
    for (int k = 0; k < 3; ++k) {
        e1[k] = tri->p[1][k] - tri->p[0][k];
        e2[k] = tri->p[2][k] - tri->p[0][k];
        t[k] = o[k] - tri->p[0][k];
    }
    p[0] = d[1] * e2[2] - d[2] * e2[1];
    p[1] = d[2] * e2[0] - d[0] * e2[2];
    p[2] = d[0] * e2[1] - d[1] * e2[0];
    det = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
    if (fabsf(det) < 0.0000001f) return false;
    inv = 1.0f / det;
    u = (t[0] * p[0] + t[1] * p[1] + t[2] * p[2]) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    q[0] = t[1] * e1[2] - t[2] * e1[1];
    q[1] = t[2] * e1[0] - t[0] * e1[2];
    q[2] = t[0] * e1[1] - t[1] * e1[0];
    v = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    distance = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
    return distance > 0.025f && distance <= reach;
}

static bool Trace(const VoxelLightTree *tree, unsigned index, const float o[3], const float d[3], float reach)
{
    const Node *n = &tree->nodes[index];
    if (!RayBox(n, o, d, reach)) return false;
    if (n->count) {
        for (unsigned i = n->first; i < n->first + n->count; ++i)
            if (RayTriangle(&tree->triangles[i], o, d, reach)) return true;
        return false;
    }
    return Trace(tree, n->left, o, d, reach) || Trace(tree, n->right, o, d, reach);
}

bool VoxelLightTree_Occluded(const VoxelLightTree *tree, const float o[3], const float d[3], float reach)
{
    if (!tree->nodeCount || !isfinite(reach) || reach <= 0.025f) return false;
    for (int k = 0; k < 3; ++k) if (!isfinite(o[k]) || !isfinite(d[k])) return false;
    return Trace(tree, 0, o, d, reach);
}
#endif
