#include "voxel_hull.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "voxel_world.h"
#include "voxel_atlas.h"

#define MAXC 48
#define MAXPX (MAXC * MAXC)

// ROUND_SHADE (Structures.lua): the drawing on the front at full strength,
// the back and flanks turned from the sun, the underside darkest.
#define SH_FRONT  255
#define SH_BACK   173     // 0.68
#define SH_SIDE   199     // 0.78
#define SH_TOP    255
#define SH_BOTTOM 140     // 0.55

// Ground contact (ChunkMesher groundShades): the floor blocks half the sky,
// so a voxel within AO_RISE px of the foot darkens toward it.
#define AO_RISE   6.0f
#define AO_GROUND 0.288f

enum { CLS_OFF, CLS_BLACK, CLS_DARK, CLS_LIGHT, CLS_WHITE };

typedef struct {
    const VoxelInstance *inst;
    int nx, ny, n2, cols;
    const int *metas;
} Canvas;

// The atlas pixel (composed quadrant) of canvas pixel (px, py).
static void Texel(const Canvas *cv, int px, int py, int *ax, int *ay)
{
    int m = cv->metas[(py / 16) * cv->cols + (px / 16)];
    *ax = (m % VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + (px % 16);
    *ay = (m / VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + (py % 16);
}

static uint32_t Composed(const Canvas *cv, int px, int py)
{
    int ax, ay;
    Texel(cv, px, py, &ax, &ay);
    return cv->inst->pixels[ay * VOXEL_ATLAS_SIZE + ax];
}

static bool Carved(const Canvas *cv, int px, int py)
{
    int ax, ay;
    Texel(cv, px, py, &ax, &ay);
    return (cv->inst->pixels[(ay + VOXEL_ATLAS_QUAD) * VOXEL_ATLAS_SIZE + ax] >> 24) != 0;
}

// Structures.shadeClass on min(r, g, b)
static int ShadeClass(uint32_t c)
{
    int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
    int lo = r < g ? r : g;
    if (b < lo) lo = b;
    if (lo <= 64) return CLS_BLACK;     // 0.25
    if (lo <= 140) return CLS_DARK;     // 0.55
    if (lo <= 217) return CLS_LIGHT;    // 0.85
    return CLS_WHITE;
}

typedef struct {
    VoxelHullVertex *v;
    int count, cap;
} Sink;

static void Push(Sink *s, const float *xyz, const float *uv, int shade)
{
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    int k;
    if (s->count + 6 > s->cap)
    {
        s->cap = s->cap ? s->cap * 2 : 1536;
        s->v = realloc(s->v, s->cap * sizeof(VoxelHullVertex));
    }
    for (k = 0; k < 6; k++)
    {
        int i = order[k];
        VoxelHullVertex *o = &s->v[s->count++];
        float y = xyz[i * 3 + 1];
        float sh = (float)shade;
        o->x = xyz[i * 3 + 0] / 16.0f;
        o->y = y / 16.0f;
        o->z = xyz[i * 3 + 2] / 16.0f;
        o->u = uv[i * 2 + 0] / VOXEL_ATLAS_SIZE;
        o->v = uv[i * 2 + 1] / VOXEL_ATLAS_SIZE;
        if (y < AO_RISE)
            sh *= 1.0f - AO_GROUND * (1.0f - y / AO_RISE);
        o->r = o->g = o->b = (uint8_t)(sh < 0 ? 0 : sh);
        o->a = 255;
    }
}

// a quad of one flat texel
static void PushFlat(Sink *s, const float *xyz, float u, float v, int shade)
{
    float uv[8] = {u, v, u, v, u, v, u, v};
    Push(s, xyz, uv, shade);
}

// WATER IS BACKGROUND FOR A ROCK STANDING IN IT. The sea is blue well past
// its red and green; Emerald's rock is a blue-grey only a little past them,
// and the wave highlights, which fall in the rock's range, are the palest
// thing in the drawing.
static bool BlueOff(uint32_t c)
{
    float r = (c & 0xFF) / 255.0f, g = ((c >> 8) & 0xFF) / 255.0f, b = ((c >> 16) & 0xFF) / 255.0f;
    float d1 = b - (r > g ? r : g), lo = r < g ? r : g;
    if (b < lo) lo = b;
    return d1 > 0.15f || (lo > 0.62f && d1 > 0.04f);
}

bool VoxelHull_Build(VoxelHullTemplate *tpl, const VoxelInstance *inst, int nx, int ny,
                     const int *metas, const int *ground, int groundCount, bool blueBg)
{
    static uint8_t cls[MAXPX], mask[MAXPX];
    static int16_t z0[MAXPX], z1[MAXPX], src[MAXPX];
    static int16_t loRow[MAXC], hiRow[MAXC];
    Canvas cv;
    Sink sink = {0};
    int NX = nx, NY = ny, N2 = nx / 2, i, ix, iy, yBot = -1, any = 0;

    memset(tpl, 0, sizeof(*tpl));
    tpl->nx = nx; tpl->ny = ny; tpl->bgMeta = -1; tpl->blueBg = blueBg;
    tpl->nMetas = (nx / 16) * (ny / 16);
    for (i = 0; i < tpl->nMetas && i < 9; i++)
        tpl->metas[i] = metas[i];
    if (inst->pixels == NULL || nx > MAXC || ny > MAXC || nx % 16 || ny % 16)
        return false;
    cv.inst = inst; cv.nx = nx; cv.ny = ny; cv.n2 = N2; cv.cols = nx / 16; cv.metas = metas;
    for (i = 0; i < tpl->nMetas; i++)
        if (metas[i] < 0 || metas[i] >= 1024)
            return false;

    // GEN 3 STATES ITS OWN SILHOUETTE: the mask is the carve, exactly.
    for (iy = 0; iy < NY; iy++)
        for (ix = 0; ix < NX; ix++)
        {
            int p = iy * NX + ix;
            if (Carved(&cv, ix, iy) && !(blueBg && BlueOff(Composed(&cv, ix, iy))))
            {
                cls[p] = (uint8_t)ShadeClass(Composed(&cv, ix, iy));
                mask[p] = 1;
                any = 1;
            }
            else
            {
                cls[p] = CLS_OFF;
                mask[p] = 0;
            }
        }
    if (!any)
        return false;

    // CLOSE THE TOP OF A CLIPPED CROWN: where the drawing is still more than
    // a third wide at its top row, taper the top rows into a dome a radius
    // deep (a quarter-ellipse), keeping every row's own art.
    {
        int top = -1, widest = 0, lo0 = -1, hi0 = -1;
        for (iy = 0; iy < NY; iy++)
        {
            int lo = -1, hi = -1;
            for (ix = 0; ix < NX; ix++)
                if (mask[iy * NX + ix]) { if (lo < 0) lo = ix; hi = ix; }
            if (lo >= 0)
            {
                if (top < 0) { top = iy; lo0 = lo; hi0 = hi; }
                if (hi - lo + 1 > widest) widest = hi - lo + 1;
            }
        }
        if (top >= 0 && widest > 0 && (hi0 - lo0 + 1) > widest * 0.34f)
        {
            int dome = widest / 2, k;
            if (dome < 4) dome = 4;
            for (k = 0; k < dome; k++)
            {
                int lo = -1, hi = -1;
                float t, keep, mid, half;
                iy = top + k;
                if (iy >= NY)
                    break;
                for (ix = 0; ix < NX; ix++)
                    if (mask[iy * NX + ix]) { if (lo < 0) lo = ix; hi = ix; }
                if (lo < 0)
                    continue;
                t = (float)(k + 1) / dome;
                keep = sqrtf(1.0f - (1.0f - t) * (1.0f - t));
                mid = (lo + hi) / 2.0f;
                half = ((hi - lo + 1) * keep) / 2.0f;
                for (ix = lo; ix <= hi; ix++)
                    if (fabsf(ix + 0.5f - (mid + 0.5f)) > half)
                        mask[iy * NX + ix] = 0;
            }
        }
    }

    // the ground the ball stands on: score every candidate floor against the
    // canvas's unmasked light pixels and keep the closest
    if (ground && groundCount > 0)
    {
        double best = -1.0;
        int g;
        for (g = 0; g < groundCount; g++)
        {
            int t = ground[g], gx = (t % VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE, gy = (t / VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE;
            double score = 0.0;
            int n = 0;
            if (t < 0 || t >= 1024 || !inst->used[t])
                continue;
            for (iy = 0; iy < NY; iy++)
                for (ix = 0; ix < NX; ix++)
                {
                    int p = iy * NX + ix;
                    uint32_t c1, c2;
                    int dr, dg, db;
                    if (mask[p])
                        continue;
                    c1 = Composed(&cv, ix, iy);
                    if (ShadeClass(c1) < CLS_LIGHT)
                        continue;
                    c2 = inst->pixels[(gy + iy % 16) * VOXEL_ATLAS_SIZE + gx + ix % 16];
                    dr = (int)(c1 & 0xFF) - (int)(c2 & 0xFF);
                    dg = (int)((c1 >> 8) & 0xFF) - (int)((c2 >> 8) & 0xFF);
                    db = (int)((c1 >> 16) & 0xFF) - (int)((c2 >> 16) & 0xFF);
                    score += dr * dr + dg * dg + db * db;
                    n++;
                }
            if (n > 0)
            {
                score /= n;
                if (best < 0 || score < best) { best = score; tpl->bgMeta = t; }
            }
        }
    }

    // discs: per mask pixel a z chord from its row's own span circle
    for (i = 0; i < NX * NY; i++) { z0[i] = -1; z1[i] = -1; src[i] = -1; }
    for (iy = 0; iy < NY; iy++)
    {
        int lo = -1, hi = -1;
        loRow[iy] = hiRow[iy] = -1;
        for (ix = 0; ix < NX; ix++)
            if (mask[iy * NX + ix]) { if (lo < 0) lo = ix; hi = ix; }
        if (lo < 0)
            continue;
        loRow[iy] = lo; hiRow[iy] = hi;
        yBot = iy;
        {
            float c = (lo + hi + 1) / 2.0f, hw = (hi - lo + 1) / 2.0f;
            for (ix = lo; ix <= hi; ix++)
            {
                int p = iy * NX + ix, n = 1;
                float dx = ix + 0.5f - c;
                if (!mask[p])
                    continue;
                if (hw * hw > dx * dx)
                {
                    n = (int)floorf(2.0f * sqrtf(hw * hw - dx * dx) + 0.5f);
                    if (n < 1) n = 1;
                }
                z0[p] = (int16_t)floorf(N2 - n / 2.0f + 0.5f);
                z1[p] = z0[p] + n;
                src[p] = iy;
            }
        }
    }
    if (yBot < 0)
        return false;
    // foot: rows under the mask repeat the bottom row's discs down to the floor
    for (iy = yBot + 1; iy < NY; iy++)
    {
        loRow[iy] = loRow[yBot]; hiRow[iy] = hiRow[yBot];
        for (ix = loRow[yBot]; ix <= hiRow[yBot]; ix++)
        {
            int b = yBot * NX + ix, p = iy * NX + ix;
            if (z0[b] >= 0) { z0[p] = z0[b]; z1[p] = z1[b]; src[p] = yBot; }
        }
    }

#define SOLID(x, y, z) ((x) >= 0 && (x) < NX && (y) >= 0 && (y) < NY && z0[(y) * NX + (x)] >= 0 \
                        && (z) >= z0[(y) * NX + (x)] && (z) < z1[(y) * NX + (x)])

    for (iy = 0; iy < NY; iy++)
    {
        float yB = (float)(NY - 1 - iy), yT = (float)(NY - iy);
        if (loRow[iy] < 0)
            continue;
        // front and back: the drawing per pixel, columns merged where they
        // share a chord plane, never across a metatile seam
        ix = loRow[iy];
        while (ix <= hiRow[iy])
        {
            int p = iy * NX + ix, ix2 = ix, ax0, ay0, ax1, ay1;
            float x0, x1, zF, zB, u0, u1, v0, v1;
            if (z0[p] < 0) { ix++; continue; }
            while (ix2 + 1 <= hiRow[iy])
            {
                int j = iy * NX + ix2 + 1;
                if (z0[j] == z0[p] && z1[j] == z1[p] && src[j] == src[p] && (ix2 + 1) / 16 == ix / 16)
                    ix2++;
                else
                    break;
            }
            x0 = (float)(ix - N2); x1 = (float)(ix2 - N2 + 1);
            Texel(&cv, ix, src[p], &ax0, &ay0);
            Texel(&cv, ix2, src[p], &ax1, &ay1);
            u0 = ax0 + 0.05f; u1 = ax1 + 0.95f;
            v0 = ay0 + 0.05f; v1 = ay0 + 0.95f;
            zF = (float)(z1[p] - N2);
            zB = (float)(z0[p] - N2);
            {
                float q[12] = { x0, yB, zF,  x1, yB, zF,  x1, yT, zF,  x0, yT, zF };
                float uv[8] = { u0, v1,  u1, v1,  u1, v0,  u0, v0 };
                Push(&sink, q, uv, SH_FRONT);
            }
            {
                float q[12] = { x1, yB, zB,  x0, yB, zB,  x0, yT, zB,  x1, yT, zB };
                float uv[8] = { u1, v1,  u0, v1,  u0, v0,  u1, v0 };
                Push(&sink, q, uv, SH_BACK);
            }
            ix = ix2 + 1;
        }
        // sides, tops, undersides: constant-texel quads over the z runs a
        // neighbouring column does not cover
        for (ix = loRow[iy]; ix <= hiRow[iy]; ix++)
        {
            int p = iy * NX + ix, ax, ay, sax, say, pass;
            float x0 = (float)(ix - N2), x1 = (float)(ix - N2 + 1), u, v, su, sv;
            if (z0[p] < 0)
                continue;
            Texel(&cv, ix, src[p], &ax, &ay);
            u = ax + 0.5f; v = ay + 0.5f;
            // side walls read as material, not outline: walk inward past
            // black pixels
            {
                int r = (iy > yBot) ? yBot : src[p];
                int dir = (ix + ix < loRow[iy] + hiRow[iy]) ? 1 : -1, step;
                sax = -1;
                for (step = 0; step <= 3; step++)
                {
                    int x2 = ix + dir * step;
                    if (x2 < 0 || x2 > NX - 1 || !mask[r * NX + x2])
                        break;
                    if (cls[r * NX + x2] != CLS_BLACK) { Texel(&cv, x2, r, &sax, &say); break; }
                }
                if (sax < 0)
                    Texel(&cv, ix, r, &sax, &say);
                su = sax + 0.5f; sv = say + 0.5f;
            }
            for (pass = 0; pass < 4; pass++)
            {
                int nxp = ix, nyp = iy, iz;
                if (pass == 0) nxp = ix - 1;
                else if (pass == 1) nxp = ix + 1;
                else if (pass == 2) nyp = iy - 1;
                else { nyp = iy + 1; if (iy >= NY - 1) continue; }
                iz = z0[p];
                while (iz < z1[p])
                {
                    int iz2;
                    float zA, zB2;
                    if (SOLID(nxp, nyp, iz)) { iz++; continue; }
                    iz2 = iz;
                    while (iz2 + 1 < z1[p] && !SOLID(nxp, nyp, iz2 + 1))
                        iz2++;
                    zA = (float)(iz - N2); zB2 = (float)(iz2 - N2 + 1);
                    if (pass == 0)
                    {
                        float q[12] = { x0, yB, zA,  x0, yB, zB2,  x0, yT, zB2,  x0, yT, zA };
                        PushFlat(&sink, q, su, sv, SH_SIDE);
                    }
                    else if (pass == 1)
                    {
                        float q[12] = { x1, yB, zB2,  x1, yB, zA,  x1, yT, zA,  x1, yT, zB2 };
                        PushFlat(&sink, q, su, sv, SH_SIDE);
                    }
                    else if (pass == 2)
                    {
                        if (iz == z0[p] && iz2 == z1[p] - 1 && iz2 - iz >= 2)
                        {
                            // the dome cap: outline on the rim cells, the
                            // canopy a couple of rows down inside
                            int dax = ax, day = ay, iy2;
                            for (iy2 = iy + 2; iy2 <= iy + 4 && iy2 < NY; iy2++)
                                if (mask[iy2 * NX + ix] && cls[iy2 * NX + ix] != CLS_BLACK)
                                {
                                    Texel(&cv, ix, iy2, &dax, &day);
                                    break;
                                }
                            {
                                float q[12] = { x0, yT, zA,  x1, yT, zA,  x1, yT, zA + 1,  x0, yT, zA + 1 };
                                PushFlat(&sink, q, u, v, SH_TOP);
                            }
                            {
                                float q[12] = { x0, yT, zA + 1,  x1, yT, zA + 1,  x1, yT, zB2 - 1,  x0, yT, zB2 - 1 };
                                PushFlat(&sink, q, dax + 0.5f, day + 0.5f, SH_TOP);
                            }
                            {
                                float q[12] = { x0, yT, zB2 - 1,  x1, yT, zB2 - 1,  x1, yT, zB2,  x0, yT, zB2 };
                                PushFlat(&sink, q, u, v, SH_TOP);
                            }
                        }
                        else
                        {
                            float q[12] = { x0, yT, zA,  x1, yT, zA,  x1, yT, zB2,  x0, yT, zB2 };
                            PushFlat(&sink, q, u, v, SH_TOP);
                        }
                    }
                    else
                    {
                        float q[12] = { x0, yB, zB2,  x1, yB, zB2,  x1, yB, zA,  x0, yB, zA };
                        PushFlat(&sink, q, u, v, SH_BOTTOM);
                    }
                    iz = iz2 + 1;
                }
            }
        }
    }
#undef SOLID
    tpl->verts = sink.v;
    tpl->count = sink.count;
    return tpl->count > 0;
}

void VoxelHull_Free(VoxelHullTemplate *tpl)
{
    free(tpl->verts);
    tpl->verts = NULL;
    tpl->count = 0;
}

#endif
#endif
