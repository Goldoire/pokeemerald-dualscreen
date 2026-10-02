#include "voxel_caster.h"
#if defined(PLATFORM_SDL2) && (defined(NATIVE_LINUX) || defined(__ANDROID__))
#include "voxel_light.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int axis, plane, u0, u1, v0, v1; } Rect;

static int Order(int a, int b) { return (a > b) - (a < b); }
static int Horizontal(const void *a, const void *b)
{
    const Rect *x = a, *y = b;
    int c;
    if ((c = Order(x->axis, y->axis))) return c;
    if ((c = Order(x->plane, y->plane))) return c;
    if ((c = Order(x->v0, y->v0))) return c;
    if ((c = Order(x->v1, y->v1))) return c;
    if ((c = Order(x->u0, y->u0))) return c;
    return Order(x->u1, y->u1);
}
static int Vertical(const void *a, const void *b)
{
    const Rect *x = a, *y = b;
    int c;
    if ((c = Order(x->axis, y->axis))) return c;
    if ((c = Order(x->plane, y->plane))) return c;
    if ((c = Order(x->u0, y->u0))) return c;
    if ((c = Order(x->u1, y->u1))) return c;
    if ((c = Order(x->v0, y->v0))) return c;
    return Order(x->v1, y->v1);
}

static unsigned Merge(Rect *rects, unsigned count, bool vertical)
{
    unsigned out = 0;
    qsort(rects, count, sizeof(Rect), vertical ? Vertical : Horizontal);
    for (unsigned i = 0; i < count; ++i) {
        Rect r = rects[i];
        if (out) {
            Rect *previous = &rects[out - 1];
            bool plane = previous->axis == r.axis && previous->plane == r.plane;
            bool same = vertical ? previous->u0 == r.u0 && previous->u1 == r.u1
                                 : previous->v0 == r.v0 && previous->v1 == r.v1;
            int *end = vertical ? &previous->v1 : &previous->u1;
            int begin = vertical ? r.v0 : r.u0, limit = vertical ? r.v1 : r.u1;
            if (plane && same && begin <= *end) {
                if (limit > *end) *end = limit;
                continue;
            }
        }
        rects[out++] = r;
    }
    return out;
}

bool VoxelCaster_MergeQuads(const void *vertices, unsigned count, size_t stride,
                           float **positions, unsigned *positionCount)
{
    unsigned rectCount = 0;
    *positions = NULL;
    *positionCount = 0;
    if (count % 6 || count > VOXEL_LIGHT_TRIANGLE_LIMIT * 3 ||
        stride < 3 * sizeof(float) || (count && (!vertices || stride > (size_t)-1 / count)))
        return false;
    if (!count) return true;
    Rect *rects = malloc((count / 6) * sizeof(Rect));
    if (!rects) return false;
    for (unsigned first = 0; first < count; first += 6) {
        int p[6][3], lo[3], hi[3];
        for (int v = 0; v < 6; ++v) {
            float xyz[3];
            memcpy(xyz, (const unsigned char *)vertices + (first + v) * stride, sizeof(xyz));
            for (int axis = 0; axis < 3; ++axis) {
                float grid = xyz[axis] * 16.0f;
                if (!isfinite(grid) || fabsf(grid) > 4096 || grid != roundf(grid)) goto unsupported;
                p[v][axis] = (int)grid;
                if (!v || p[v][axis] < lo[axis]) lo[axis] = p[v][axis];
                if (!v || p[v][axis] > hi[axis]) hi[axis] = p[v][axis];
            }
        }
        // Hull Push emits two triangles in the 0,1,2 / 0,2,3 order.
        if (memcmp(p[0], p[3], sizeof(p[0])) || memcmp(p[2], p[4], sizeof(p[0]))) goto unsupported;
        int axis = -1;
        for (int k = 0; k < 3; ++k) if (lo[k] == hi[k]) {
            if (axis != -1) { axis = -2; break; }
            axis = k;
        }
        if (axis == -2) continue; // Zero-area faces do not occlude a ray.
        if (axis < 0) goto unsupported;
        int u = (axis + 1) % 3, v = (axis + 2) % 3;
        unsigned corners = 0;
        static const int unique[] = {0, 1, 2, 5};
        for (int k = 0; k < 4; ++k) {
            const int *point = p[unique[k]];
            if ((point[u] != lo[u] && point[u] != hi[u]) ||
                (point[v] != lo[v] && point[v] != hi[v])) goto unsupported;
            corners |= 1u << ((point[u] == hi[u]) + 2 * (point[v] == hi[v]));
        }
        // Also require a true diagonal: an arbitrary order could form
        // overlapping triangles or omit part of the bounding rectangle.
        if (corners != 15 || p[0][u] == p[2][u] || p[0][v] == p[2][v]) goto unsupported;
        rects[rectCount++] = (Rect){axis, lo[axis], lo[u], hi[u], lo[v], hi[v]};
    }
    unsigned previous;
    do {
        previous = rectCount;
        rectCount = Merge(rects, rectCount, false);
        rectCount = Merge(rects, rectCount, true);
    } while (rectCount < previous);
    if (!rectCount) { free(rects); return true; }
    float *out = malloc(rectCount * 18 * sizeof(float));
    if (!out) goto unsupported;
    for (unsigned i = 0; i < rectCount; ++i) {
        const Rect *r = &rects[i];
        int u = (r->axis + 1) % 3, v = (r->axis + 2) % 3;
        static const int us[] = {0, 1, 1, 0, 1, 0}, vs[] = {0, 0, 1, 0, 1, 1};
        for (int k = 0; k < 6; ++k) {
            float *xyz = out + i * 18 + k * 3;
            xyz[r->axis] = r->plane / 16.0f;
            xyz[u] = (us[k] ? r->u1 : r->u0) / 16.0f;
            xyz[v] = (vs[k] ? r->v1 : r->v0) / 16.0f;
        }
    }
    *positions = out;
    *positionCount = rectCount * 6;
    free(rects);
    return true;
unsupported:
    free(rects);
    return false;
}
#endif
