#include "voxel_mesh.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#ifdef __ANDROID__
#include "gles1_compat.h"
#else
#include <GL/gl.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "global.h"
#include "voxel_atlas.h"

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------
#define ATLAS_TEXEL (1.0f / VOXEL_ATLAS_SIZE)
#define UV_INSET (0.5f * ATLAS_TEXEL)

// Face shading. Light from the south-east and above.
#define SHADE_TOP   255
#define SHADE_SOUTH 236
#define SHADE_EAST  200
#define SHADE_WEST  178
#define SHADE_NORTH 150

typedef struct { float u0, v0, u1, v1; } UvRect;

static UvRect UvFor(int metatile)
{
    UvRect r;
    r.u0 = (metatile % VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE * ATLAS_TEXEL + UV_INSET;
    r.v0 = (metatile / VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE * ATLAS_TEXEL + UV_INSET;
    r.u1 = r.u0 + VOXEL_ATLAS_TILE * ATLAS_TEXEL - 2 * UV_INSET;
    r.v1 = r.v0 + VOXEL_ATLAS_TILE * ATLAS_TEXEL - 2 * UV_INSET;
    return r;
}

// A vertical slice of a metatile's art: rows [t0..t1] of 16 (0 = top).
static UvRect UvSlice(int metatile, float t0, float t1)
{
    UvRect r = UvFor(metatile);
    float h = r.v1 - r.v0;
    UvRect s;
    s.u0 = r.u0; s.u1 = r.u1;
    s.v0 = r.v0 + h * t0;
    s.v1 = r.v0 + h * t1;
    return s;
}

void VoxelMesh_Init(VoxelMesh *mesh)
{
    memset(mesh, 0, sizeof(*mesh));
}

void VoxelMesh_Free(VoxelMesh *mesh)
{
    free(mesh->verts);
    free(mesh->ranges);
    free(mesh->ghost);
    memset(mesh, 0, sizeof(*mesh));
}

void VoxelMesh_Clear(VoxelMesh *mesh)
{
    mesh->count = 0;
    mesh->rangeCount = 0;
}

static VoxelVertex *Reserve(VoxelMesh *mesh, int n)
{
    if (mesh->count + n > mesh->capacity)
    {
        int cap = mesh->capacity ? mesh->capacity * 2 : 4096;
        while (cap < mesh->count + n)
            cap *= 2;
        mesh->verts = realloc(mesh->verts, cap * sizeof(VoxelVertex));
        mesh->capacity = cap;
    }
    return &mesh->verts[mesh->count];
}

static void Vtx(VoxelVertex *v, float x, float y, float z, float u, float tv, int shade, int a)
{
    v->x = x; v->y = y; v->z = z;
    v->u = u; v->v = tv;
    v->r = v->g = v->b = (uint8_t)shade;
    v->a = (uint8_t)a;
}

// A quad from four corners (counter-clockwise as seen from its front) with
// per-corner UVs.
static void Quad(VoxelMesh *mesh,
                 float x0, float y0, float z0, float u0, float v0,
                 float x1, float y1, float z1, float u1, float v1,
                 float x2, float y2, float z2, float u2, float v2,
                 float x3, float y3, float z3, float u3, float v3,
                 int shade)
{
    VoxelVertex *v = Reserve(mesh, 6);
    Vtx(&v[0], x0, y0, z0, u0, v0, shade, 255);
    Vtx(&v[1], x1, y1, z1, u1, v1, shade, 255);
    Vtx(&v[2], x2, y2, z2, u2, v2, shade, 255);
    Vtx(&v[3], x0, y0, z0, u0, v0, shade, 255);
    Vtx(&v[4], x2, y2, z2, u2, v2, shade, 255);
    Vtx(&v[5], x3, y3, z3, u3, v3, shade, 255);
    mesh->count += 6;
}

// Horizontal quad covering cell (x, z) at height y with art `uv`.
static void TopQuad(VoxelMesh *mesh, float x, float z, float y, UvRect uv, int shade)
{
    Quad(mesh,
         x,        y, z,        uv.u0, uv.v0,
         x + 1.0f, y, z,        uv.u1, uv.v0,
         x + 1.0f, y, z + 1.0f, uv.u1, uv.v1,
         x,        y, z + 1.0f, uv.u0, uv.v1,
         shade);
}

// Vertical faces of cell (x, z) between heights y0 (bottom) and y1 (top).
// `uv` maps its v0 to the top and v1 to the bottom.
static void SouthFace(VoxelMesh *mesh, float x, float z, float y0, float y1, UvRect uv, int shade)
{
    Quad(mesh,
         x,        y0, z + 1.0f, uv.u0, uv.v1,
         x + 1.0f, y0, z + 1.0f, uv.u1, uv.v1,
         x + 1.0f, y1, z + 1.0f, uv.u1, uv.v0,
         x,        y1, z + 1.0f, uv.u0, uv.v0,
         shade);
}

static void NorthFace(VoxelMesh *mesh, float x, float z, float y0, float y1, UvRect uv, int shade)
{
    Quad(mesh,
         x + 1.0f, y0, z, uv.u0, uv.v1,
         x,        y0, z, uv.u1, uv.v1,
         x,        y1, z, uv.u1, uv.v0,
         x + 1.0f, y1, z, uv.u0, uv.v0,
         shade);
}

static void EastFace(VoxelMesh *mesh, float x, float z, float y0, float y1, UvRect uv, int shade)
{
    Quad(mesh,
         x + 1.0f, y0, z + 1.0f, uv.u0, uv.v1,
         x + 1.0f, y0, z,        uv.u1, uv.v1,
         x + 1.0f, y1, z,        uv.u1, uv.v0,
         x + 1.0f, y1, z + 1.0f, uv.u0, uv.v0,
         shade);
}

static void WestFace(VoxelMesh *mesh, float x, float z, float y0, float y1, UvRect uv, int shade)
{
    Quad(mesh,
         x, y0, z,        uv.u0, uv.v1,
         x, y0, z + 1.0f, uv.u1, uv.v1,
         x, y1, z + 1.0f, uv.u1, uv.v0,
         x, y1, z,        uv.u0, uv.v0,
         shade);
}

// ---------------------------------------------------------------------------
// Ranges
// ---------------------------------------------------------------------------
static void BeginRange(VoxelMesh *mesh, VoxelMeshRange *r, int worldX, int worldZ, int building, float base, float top)
{
    r->first = mesh->count;
    r->count = 0;
    r->worldX = worldX;
    r->worldZ = worldZ;
    r->building = building;
    r->base = base;
    r->top = top;
    r->rows = 1;
    r->cols = 1;
}

static void EndRange(VoxelMesh *mesh, const VoxelMeshRange *r)
{
    VoxelMeshRange *dst;
    if (mesh->count == r->first)
        return;
    if (mesh->rangeCount + 1 > mesh->rangeCapacity)
    {
        int cap = mesh->rangeCapacity ? mesh->rangeCapacity * 2 : 256;
        mesh->ranges = realloc(mesh->ranges, cap * sizeof(VoxelMeshRange));
        mesh->ghost = realloc(mesh->ghost, cap * sizeof(bool));
        mesh->rangeCapacity = cap;
    }
    dst = &mesh->ranges[mesh->rangeCount++];
    *dst = *r;
    dst->count = mesh->count - r->first;
}

// ---------------------------------------------------------------------------
// Neighbour queries in instance-local space
// ---------------------------------------------------------------------------
static const VoxelCell *Nb(const VoxelInstance *inst, int lx, int ly)
{
    if (lx < 0 || ly < 0 || lx >= inst->width || ly >= inst->height)
        return VoxelWorld_CellAt(lx + inst->originX, ly + inst->originY);
    return &inst->cells[ly * inst->width + lx];
}

// The height a neighbour presents on its side facing us: a column's top, a
// building's roof, or a surface's top.
static float SideHeight(const VoxelCell *n)
{
    if (n == NULL || n->cls == VC_VOID)
        return -1000.0f;
    return n->topH;
}

static bool IsColumn(const VoxelCell *c)
{
    return c && (c->cls == VC_WALL || c->cls == VC_CLIFF || c->cls == VC_BUILDING
                 || (c->cls >= VC_COUNTER && c->cls < VC_COUNT && (c->flags & VCF_BLOCKED)));
}

// Interior room shell: the walls the GBA never draws, two courses tall
// along every edge that borders the void (north, east and west; the south
// edge stays open for the camera).
static void ShellFaces(VoxelMesh *mesh, const VoxelInstance *inst, const VoxelCell *c, float x, float z, float floor)
{
    UvRect s;
    float shellTop = floor + 2.0f;
    float skirt = floor - 1.0f;   // carry on below the floor: the camera looks down past its edge
    if (!inst->indoor || !(c->flags & (VCF_SHELL_N | VCF_SHELL_E | VCF_SHELL_W)))
        return;
    s = UvFor(inst->shellMeta);
    if (c->flags & VCF_SHELL_N) NorthFace(mesh, x, z - 1.0f, floor, shellTop, s, SHADE_SOUTH - 60);
    if (c->flags & VCF_SHELL_E) WestFace(mesh, x + 1.0f, z, skirt, shellTop, s, SHADE_WEST);
    if (c->flags & VCF_SHELL_W) EastFace(mesh, x - 1.0f, z, skirt, shellTop, s, SHADE_EAST);
}

// ---------------------------------------------------------------------------
// Surfaces: ground, grass, water, ledges, decks, walkable pins
// ---------------------------------------------------------------------------
static void BuildSurface(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    UvRect uv = UvFor(c->metatile);
    float top = c->topH;
    int d;
    static const int dx[4] = {0, 0, 1, -1};
    static const int dy[4] = {1, -1, 0, 0};

    if (c->cls == VC_BRIDGE || c->cls == VC_LOG)
    {
        // The world continues under the deck.
        UvRect under = UvFor(c->underMeta);
        TopQuad(mesh, x, z, c->underH, under, SHADE_TOP - 40);
    }

    TopQuad(mesh, x, z, top, uv, SHADE_TOP);

    // Fascias down to lower neighbours, wearing this cell's own art.
    for (d = 0; d < 4; d++)
    {
        const VoxelCell *n = Nb(inst, lx + dx[d], ly + dy[d]);
        float nh;
        float bottom;
        if (n == NULL || n->cls == VC_VOID)
        {
            if (!inst->indoor && top > 0.01f)
                nh = 0.0f;       // the map edge: drop to the world floor
            else
                continue;
        }
        else
        {
            nh = (n->cls == VC_BRIDGE || n->cls == VC_LOG) && n->underH < n->topH ? n->underH : n->topH;
            if (IsColumn(n))
                nh = n->groundH < top ? n->groundH : top;
            if (n->cls == VC_WATER && (c->cls == VC_BRIDGE || c->cls == VC_LOG))
                nh = n->topH;
        }
        if (nh >= top - 0.01f)
            continue;
        bottom = nh;
        if (c->cls == VC_LEDGE || c->cls == VC_BRIDGE || c->cls == VC_LOG)
        {
            // a ledge's lip / a deck's plank edge: the art's bottom rows
            UvRect s = UvSlice(c->metatile, 0.6f, 1.0f);
            if (c->cls != VC_LEDGE && bottom < top - 0.25f)
                bottom = top - 0.25f;
            if (d == 0) SouthFace(mesh, x, z, bottom, top, s, SHADE_SOUTH - 30);
            else if (d == 1) NorthFace(mesh, x, z, bottom, top, s, SHADE_NORTH);
            else if (d == 2) EastFace(mesh, x, z, bottom, top, s, SHADE_EAST);
            else WestFace(mesh, x, z, bottom, top, s, SHADE_WEST);
        }
        else
        {
            // a terrace step with no cliff art: the ground's own texture, dark
            if (d == 0) SouthFace(mesh, x, z, bottom, top, uv, SHADE_SOUTH - 70);
            else if (d == 1) NorthFace(mesh, x, z, bottom, top, uv, SHADE_NORTH - 40);
            else if (d == 2) EastFace(mesh, x, z, bottom, top, uv, SHADE_EAST - 60);
            else WestFace(mesh, x, z, bottom, top, uv, SHADE_WEST - 60);
        }
    }

    ShellFaces(mesh, inst, c, x, z, c->groundH);
}

// ---------------------------------------------------------------------------
// Columns: stacked blocked rows (landscape, trees, fences, interior walls)
// ---------------------------------------------------------------------------
static void BuildColumn(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    int rows = c->stack, j, k;
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    float base = c->groundH, top = c->topH;
    float height = top - base;
    float courseH;
    VoxelMeshRange r;
    float hS, hN;
    int topMeta;
    int rowMeta[4];

    if (rows <= 0 || rows > 4 || height <= 0.01f)
        return;
    courseH = height / rows;
    for (k = 0; k < rows; k++)
    {
        const VoxelCell *ck = Nb(inst, lx, ly - k);
        rowMeta[k] = ck ? ck->metatile : c->metatile;
    }
    topMeta = rowMeta[rows - 1];

    BeginRange(mesh, &r, (int)x, (int)z, -1, base, top);
    r.rows = rows;

    // The stack is one mass standing on every row it claimed. Its lid wears
    // the northmost row's art (a tree's crown, a wall's top trim) over the
    // whole footprint, and each side shows the rows stacked as courses:
    // trunk below crown, wall body below its cornice.
    for (j = 0; j < rows; j++)
        TopQuad(mesh, x, z - j, top, UvFor(topMeta), SHADE_TOP);

    hS = SideHeight(Nb(inst, lx, ly + 1));
    hN = SideHeight(Nb(inst, lx, ly - rows));
    for (k = 0; k < rows; k++)
    {
        float y0 = base + courseH * k;
        float y1 = base + courseH * (k + 1);
        float lo;
        lo = hS > y0 ? hS : y0;
        if (lo < y1 - 0.01f)
            SouthFace(mesh, x, z, lo, y1, UvSlice(rowMeta[k], (lo - y0) / courseH, 1.0f), SHADE_SOUTH);
        lo = hN > y0 ? hN : y0;
        if (lo < y1 - 0.01f)
            NorthFace(mesh, x, z - (rows - 1), lo, y1, UvSlice(rowMeta[k], (lo - y0) / courseH, 1.0f), SHADE_NORTH);
    }
    for (j = 0; j < rows; j++)
    {
        const VoxelCell *cj = Nb(inst, lx, ly - j);
        float hE = SideHeight(Nb(inst, lx + 1, ly - j));
        float hW = SideHeight(Nb(inst, lx - 1, ly - j));
        if (cj)
            ShellFaces(mesh, inst, cj, x, z - j, base);
        for (k = 0; k < rows; k++)
        {
            float y0 = base + courseH * k;
            float y1 = base + courseH * (k + 1);
            float lo;
            lo = hE > y0 ? hE : y0;
            if (lo < y1 - 0.01f)
                EastFace(mesh, x, z - j, lo, y1, UvSlice(rowMeta[k], (lo - y0) / courseH, 1.0f), SHADE_EAST);
            lo = hW > y0 ? hW : y0;
            if (lo < y1 - 0.01f)
                WestFace(mesh, x, z - j, lo, y1, UvSlice(rowMeta[k], (lo - y0) / courseH, 1.0f), SHADE_WEST);
        }
    }
    EndRange(mesh, &r);
}

// ---------------------------------------------------------------------------
// Furniture: a box of its class height wearing its art on top and front
// ---------------------------------------------------------------------------
static void BuildFurniture(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    float base = c->groundH, top = c->topH;
    UvRect uv = UvFor(c->metatile);
    const VoxelCell *n;
    float nh;
    bool faceOn = c->cls == VC_CONSOLE || c->cls == VC_TV || c->cls == VC_APPLIANCE
               || c->cls == VC_CABINET || c->cls == VC_WORKTOP || c->cls == VC_SINK || c->cls == VC_PROP
               || c->cls == VC_BOOKCASE;

    ShellFaces(mesh, inst, c, x, z, base);
    if (top <= base + 0.01f)
    {
        TopQuad(mesh, x, z, top, uv, SHADE_TOP);
        return;
    }
    if (faceOn)
    {
        // The drawing is a front elevation: fold it up the south face and
        // give the top a plain lid from the art's top rows.
        TopQuad(mesh, x, z, top, UvSlice(c->metatile, 0.0f, 0.2f), SHADE_TOP - 20);
        n = Nb(inst, lx, ly + 1); nh = SideHeight(n);
        if (nh < top - 0.01f) SouthFace(mesh, x, z, base, top, uv, SHADE_SOUTH);
    }
    else
    {
        TopQuad(mesh, x, z, top, uv, SHADE_TOP);
        n = Nb(inst, lx, ly + 1); nh = SideHeight(n);
        if (nh < top - 0.01f) SouthFace(mesh, x, z, base, top, UvSlice(c->metatile, 0.7f, 1.0f), SHADE_SOUTH - 40);
    }
    n = Nb(inst, lx, ly - 1); nh = SideHeight(n);
    if (nh < top - 0.01f) NorthFace(mesh, x, z, base, top, UvSlice(c->metatile, 0.0f, 0.3f), SHADE_NORTH);
    n = Nb(inst, lx + 1, ly); nh = SideHeight(n);
    if (nh < top - 0.01f) EastFace(mesh, x, z, base, top, UvSlice(c->metatile, 0.2f, 1.0f), SHADE_EAST - 30);
    n = Nb(inst, lx - 1, ly); nh = SideHeight(n);
    if (nh < top - 0.01f) WestFace(mesh, x, z, base, top, UvSlice(c->metatile, 0.2f, 1.0f), SHADE_WEST - 30);
}

// ---------------------------------------------------------------------------
// Buildings: a vertical facade of the wall rows, a roof laid back over the
// footprint from the drawing's roof rows, textured flanks.
// ---------------------------------------------------------------------------
static void BuildBuilding(VoxelMesh *mesh, const VoxelInstance *inst, int b)
{
    const VoxelBuilding *bd = &inst->buildings[b];
    int K = bd->wallRows;
    float rise = bd->roofRows > 0 ? 1.0f : 0.0f;
    float base = bd->baseH;
    float wallTop = base + K;
    int col, k;
    VoxelMeshRange r;
    float zFront = (float)(bd->facadeRow + inst->originY) + 1.0f;

    BeginRange(mesh, &r, bd->minX + inst->originX, bd->facadeRow + inst->originY, b, base, wallTop + rise);
    r.rows = bd->facadeRow - bd->minTop + 1;
    r.cols = bd->maxX - bd->minX + 1;

    for (col = bd->minX; col <= bd->maxX; col++)
    {
        int top = bd->colTop[col - bd->minX];
        int rows = bd->facadeRow - top + 1;
        int wallRows = rows < K ? rows : K;
        int roofRows = rows - wallRows;
        float x = (float)(col + inst->originX);
        float zBack = (float)(top + inst->originY);
        float depth = zFront - zBack;
        float colWallTop = base + wallRows;
        float colPeak = colWallTop + (roofRows > 0 ? rise : 0.0f);
        int rowAt;
        float zz;

        // facade: rows facadeRow .. facadeRow-wallRows+1, bottom up
        for (k = 0; k < wallRows; k++)
        {
            const VoxelCell *cellK = Nb(inst, col, bd->facadeRow - k);
            SouthFace(mesh, x, zFront - 1.0f, base + k, base + k + 1, UvFor(cellK->metatile), SHADE_SOUTH);
        }

        // roof: a plane from the facade top at the front to the peak at the
        // back, one cell of the drawing per cell of depth, the eave row
        // repeated where the footprint is deeper than the drawing's roof.
        for (zz = zBack; zz < zFront - 0.01f; zz += 1.0f)
        {
            float t0 = (zz - zBack) / depth;
            float t1 = (zz + 1.0f - zBack) / depth;
            float y0 = colPeak - (colPeak - colWallTop) * t0;   // north edge (higher)
            float y1 = colPeak - (colPeak - colWallTop) * t1;   // south edge
            UvRect uv;
            rowAt = (int)(zz - inst->originY);
            if (roofRows > 0)
            {
                int lastRoof = bd->facadeRow - wallRows;   // southmost roof row
                if (rowAt > lastRoof)
                    rowAt = lastRoof;
                if (rowAt < top)
                    rowAt = top;
            }
            else
            {
                rowAt = top;
            }
            uv = UvFor(Nb(inst, col, rowAt)->metatile);
            Quad(mesh,
                 x,        y0, zz,        uv.u0, uv.v0,
                 x + 1.0f, y0, zz,        uv.u1, uv.v0,
                 x + 1.0f, y1, zz + 1.0f, uv.u1, uv.v1,
                 x,        y1, zz + 1.0f, uv.u0, uv.v1,
                 SHADE_TOP);
        }

        // back face (north)
        {
            const VoxelCell *n = Nb(inst, col, top - 1);
            float nh = SideHeight(n);
            const VoxelCell *edge = Nb(inst, col, bd->facadeRow - (wallRows - 1));
            if (nh < colPeak - 0.01f)
            {
                float lo = nh > base ? nh : base;
                NorthFace(mesh, x, zBack, lo, colPeak, UvFor(edge->metatile), SHADE_NORTH);
            }
        }

        // flanks: only where the neighbouring column is not this building
        {
            bool westOpen = (col == bd->minX);
            bool eastOpen = (col == bd->maxX);
            int nTop;
            if (!westOpen)
            {
                nTop = bd->colTop[col - 1 - bd->minX];
                if (nTop > top) westOpen = true;   // our column reaches further north
            }
            if (!eastOpen)
            {
                nTop = bd->colTop[col + 1 - bd->minX];
                if (nTop > top) eastOpen = true;
            }
            if (westOpen || eastOpen)
            {
                for (zz = zBack; zz < zFront - 0.01f; zz += 1.0f)
                {
                    float t0 = (zz - zBack) / depth;
                    float t1 = (zz + 1.0f - zBack) / depth;
                    float yN = colPeak - (colPeak - colWallTop) * t0;
                    float yS = colPeak - (colPeak - colWallTop) * t1;
                    int rowHere = (int)(zz - inst->originY);
                    const VoxelCell *side;
                    const VoxelCell *nb;
                    float nh, lo;
                    UvRect uv;
                    int wallRowMeta;
                    // the wall part of the flank wears the facade's edge row art
                    side = Nb(inst, col, bd->facadeRow);
                    wallRowMeta = side->metatile;
                    if (westOpen)
                    {
                        nb = Nb(inst, col - 1, rowHere);
                        nh = SideHeight(nb);
                        if (nb && nb->building == b) nh = 1000.0f;
                        lo = nh > base ? nh : base;
                        if (lo < colWallTop - 0.01f)
                            WestFace(mesh, x, zz, lo, colWallTop, UvSlice(wallRowMeta, 0.0f, 1.0f), SHADE_WEST);
                        if (roofRows > 0 && lo < yN - 0.01f)
                        {
                            uv = UvFor(Nb(inst, col, top)->metatile);
                            Quad(mesh,
                                 x, colWallTop, zz,        uv.u0, uv.v1,
                                 x, colWallTop, zz + 1.0f, uv.u1, uv.v1,
                                 x, yS,         zz + 1.0f, uv.u1, uv.v0,
                                 x, yN,         zz,        uv.u0, uv.v0,
                                 SHADE_WEST);
                        }
                    }
                    if (eastOpen)
                    {
                        nb = Nb(inst, col + 1, rowHere);
                        nh = SideHeight(nb);
                        if (nb && nb->building == b) nh = 1000.0f;
                        lo = nh > base ? nh : base;
                        if (lo < colWallTop - 0.01f)
                            EastFace(mesh, x, zz, lo, colWallTop, UvSlice(wallRowMeta, 0.0f, 1.0f), SHADE_EAST);
                        if (roofRows > 0 && lo < yN - 0.01f)
                        {
                            uv = UvFor(Nb(inst, col, top)->metatile);
                            Quad(mesh,
                                 x + 1.0f, colWallTop, zz + 1.0f, uv.u0, uv.v1,
                                 x + 1.0f, colWallTop, zz,        uv.u1, uv.v1,
                                 x + 1.0f, yN,         zz,        uv.u1, uv.v0,
                                 x + 1.0f, yS,         zz + 1.0f, uv.u0, uv.v0,
                                 SHADE_EAST);
                        }
                    }
                }
            }
        }
    }
    EndRange(mesh, &r);
}

// ---------------------------------------------------------------------------
// Instance build
// ---------------------------------------------------------------------------
void VoxelMesh_BuildInstance(VoxelMesh *mesh, const VoxelInstance *inst)
{
    int x, y, b;
    VoxelMesh_Clear(mesh);
    if (inst->cells == NULL)
        return;

    for (y = 0; y < inst->height; y++)
        for (x = 0; x < inst->width; x++)
        {
            const VoxelCell *c = &inst->cells[y * inst->width + x];
            if (c->cls == VC_VOID || c->building >= 0)
                continue;
            if (c->cls == VC_WALL || c->cls == VC_CLIFF)
                continue;   // columns below
            if (c->cls >= VC_COUNTER && c->cls < VC_COUNT && (c->flags & VCF_BLOCKED))
                continue;   // furniture below
            BuildSurface(mesh, inst, x, y);
        }
    for (y = inst->height - 1; y >= 0; y--)
        for (x = 0; x < inst->width; x++)
        {
            const VoxelCell *c = &inst->cells[y * inst->width + x];
            if (c->building >= 0)
                continue;
            if ((c->cls == VC_WALL || c->cls == VC_CLIFF) && c->stack > 0)
                BuildColumn(mesh, inst, x, y);
            else if (c->cls >= VC_COUNTER && c->cls < VC_COUNT && (c->flags & VCF_BLOCKED))
                BuildFurniture(mesh, inst, x, y);
        }
    for (b = 0; b < inst->buildingCount; b++)
        BuildBuilding(mesh, inst, b);

    if (getenv("EMERALD_VOXEL_DUMP") && inst == &gVoxelInstances[0])
    {
        int r, v, shown = 0;
        for (r = 0; r < mesh->rangeCount && shown < 2; r++)
        {
            const VoxelMeshRange *rg = &mesh->ranges[r];
            const VoxelCell *c = VoxelWorld_CellAt(rg->worldX, rg->worldZ);
            if (rg->building >= 0 || c == NULL || c->stack < 2)
                continue;
            printf("[Dump] range %d cell (%d,%d) stack %d base %.2f top %.2f first %d count %d\n",
                   r, rg->worldX, rg->worldZ, c->stack, rg->base, rg->top, rg->first, rg->count);
            for (v = rg->first; v < rg->first + rg->count; v++)
            {
                const VoxelVertex *vv = &mesh->verts[v];
                printf("[Dump]  %3d: %6.2f %6.2f %6.2f  uv %.4f %.4f  c %d\n", v - rg->first, vv->x, vv->y, vv->z, vv->u, vv->v, vv->r);
            }
            shown++;
        }
    }
}

// ---------------------------------------------------------------------------
// Ghosting: a range hides the player when its mass lies on the line from the
// player's head back toward the camera (south and up).
// ---------------------------------------------------------------------------
void VoxelMesh_ComputeGhosts(VoxelMesh *mesh, float px, float py, float pz, float camTanPitch)
{
    int i;
    extern unsigned gVoxelDebugFlags;
    for (i = 0; i < mesh->rangeCount; i++)
    {
        const VoxelMeshRange *r = &mesh->ranges[i];
        float headY = py + 1.6f;
        float dz, rayY;
        bool ghost = false;
        if (gVoxelDebugFlags & 2u)
        {
            mesh->ghost[i] = false;
            continue;
        }
        if (r->building >= 0)
        {
            // buildings: the mass starts at its north edge; that is where
            // the ray from the player's head first meets it
            float northZ = (float)r->worldZ - (float)r->rows + 1.0f;
            dz = northZ - pz;
            if (dz >= -0.5f && dz < 6.0f && px + 0.5f >= (float)r->worldX - 0.5f
                && px + 0.5f <= (float)(r->worldX + r->cols) + 0.5f)
            {
                rayY = headY + (dz > 0.0f ? dz : 0.0f) * camTanPitch;
                if (r->top > rayY + 0.2f)
                    ghost = true;
            }
        }
        else
        {
            float cx = (float)r->worldX + 0.5f;
            dz = (float)r->worldZ - pz;
            if (fabsf(cx - px) <= 0.75f && dz >= 0.5f && dz < 5.0f)
            {
                rayY = headY + (dz - 0.5f) * camTanPitch;
                if (r->top > rayY + 0.2f)
                    ghost = true;
            }
        }
        mesh->ghost[i] = ghost;
    }
}

static void SetPointers(const VoxelMesh *mesh)
{
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(VoxelVertex), &mesh->verts[0].x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(VoxelVertex), &mesh->verts[0].u);
}

void VoxelMesh_DrawOpaque(const VoxelMesh *mesh)
{
    int i, cursor = 0;
    if (mesh->count == 0)
        return;
#ifndef __ANDROID__
    if (getenv("EMERALD_VOXEL_NOTEX"))
        glDisable(GL_TEXTURE_2D);
    if (getenv("EMERALD_VOXEL_WIRE"))
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    if (getenv("EMERALD_VOXEL_NODEPTH"))
        glDisable(GL_DEPTH_TEST);
#endif
    SetPointers(mesh);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(VoxelVertex), &mesh->verts[0].r);
    for (i = 0; i < mesh->rangeCount; i++)
    {
        const VoxelMeshRange *r = &mesh->ranges[i];
        if (!mesh->ghost[i])
            continue;
        if (r->first > cursor)
            glDrawArrays(GL_TRIANGLES, cursor, r->first - cursor);
        cursor = r->first + r->count;
    }
    if (mesh->count > cursor)
        glDrawArrays(GL_TRIANGLES, cursor, mesh->count - cursor);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
#ifndef __ANDROID__
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
}

void VoxelMesh_DrawGhosted(const VoxelMesh *mesh)
{
    int i;
    bool any = false;
    if (getenv("EMERALD_VOXEL_NOGHOST"))
        return;
    for (i = 0; i < mesh->rangeCount; i++)
        if (mesh->ghost[i]) { any = true; break; }
    if (!any || mesh->count == 0)
        return;
    SetPointers(mesh);
    glDisableClientState(GL_COLOR_ARRAY);
    glColor4f(0.85f, 0.85f, 0.85f, 0.38f);
    for (i = 0; i < mesh->rangeCount; i++)
    {
        const VoxelMeshRange *r = &mesh->ranges[i];
        if (mesh->ghost[i])
            glDrawArrays(GL_TRIANGLES, r->first, r->count);
    }
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

#endif
#endif
