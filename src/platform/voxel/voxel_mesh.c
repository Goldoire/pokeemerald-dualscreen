#include "voxel_mesh.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#ifdef __ANDROID__
#include "gles1_compat.h"
#else
#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "global.h"
#include "voxel_atlas.h"
#include "voxel_hull.h"
#include "voxel_scene.h"
#include "voxel_paling.h"

// ---------------------------------------------------------------------------
// Geometry helpers
// ---------------------------------------------------------------------------
#define ATLAS_TEXEL (1.0f / VOXEL_ATLAS_SIZE)
#define UV_INSET (0.5f * ATLAS_TEXEL)

// Face shading (lib/Voxel3D.lua FACE_SHADE): the sun hangs in the south-east,
// so south and east are the lit flanks, north and west the shaded ones.
#define SHADE_TOP   255     // 1.00
#define SHADE_SOUTH 230     // 0.90
#define SHADE_EAST  214     // 0.84
#define SHADE_WEST  184     // 0.72
#define SHADE_NORTH 173     // 0.68

// Shapes (lib/Structures.lua): a standee is PINNED_DEPTH.prop voxels thick,
// a tall-grass mat GRASS_MAT high, a deck a quarter course of planking, a
// canopy plate hangs at treetop height, a tree crown is a solid of
// revolution with an octagonal section.
#define STANDEE_DEPTH  0.3125f
#define DECK_THICK     0.25f
#define RAIL_THICK     0.125f
#define PILLAR_W       0.12f
#define CANOPY_H       2.0f
#define CROWN_SIDES    10
#define TREE_R_PAIR    0.94f
#define TREE_R_SINGLE  0.47f

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float u0, v0, u1, v1; } UvRect;

// A region of one metatile's art in one atlas variant, in pixels (0..16).
static UvRect UvRegion(int m, int variant, float px0, float px1, float py0, float py1)
{
    float ax = (float)((m % VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + ((variant & 1) ? VOXEL_ATLAS_QUAD : 0));
    float ay = (float)((m / VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + ((variant & 2) ? VOXEL_ATLAS_QUAD : 0));
    UvRect r;
    r.u0 = (ax + px0) * ATLAS_TEXEL + UV_INSET;
    r.u1 = (ax + px1) * ATLAS_TEXEL - UV_INSET;
    r.v0 = (ay + py0) * ATLAS_TEXEL + UV_INSET;
    r.v1 = (ay + py1) * ATLAS_TEXEL - UV_INSET;
    return r;
}

static UvRect UvFor(int m, int variant)
{
    return UvRegion(m, variant, 0.0f, 16.0f, 0.0f, 16.0f);
}

// A vertical slice of a metatile's art: rows [t0..t1] of 1 (0 = top).
static UvRect UvSlice(int m, int variant, float t0, float t1)
{
    return UvRegion(m, variant, 0.0f, 16.0f, t0 * 16.0f, t1 * 16.0f);
}

// The centre of one pixel, for faces painted in one colour.
static void UvPixel(int m, int variant, int px, int py, float *u, float *v)
{
    float ax = (float)((m % VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + ((variant & 1) ? VOXEL_ATLAS_QUAD : 0));
    float ay = (float)((m / VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + ((variant & 2) ? VOXEL_ATLAS_QUAD : 0));
    *u = (ax + px + 0.5f) * ATLAS_TEXEL;
    *v = (ay + py + 0.5f) * ATLAS_TEXEL;
}

// Alpha of one pixel of the carved variant: the silhouette mask.
static int CarvedAlpha(const VoxelInstance *inst, int m, int px, int py)
{
    int ax = (m % VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + px;
    int ay = (m / VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + VOXEL_ATLAS_QUAD + py;
    if (inst->pixels == NULL)
        return 255;
    return (int)(inst->pixels[ay * VOXEL_ATLAS_SIZE + ax] >> 24);
}

void VoxelMesh_Init(VoxelMesh *mesh)
{
    memset(mesh, 0, sizeof(*mesh));
    mesh->vboDirty = true;
}

static void FreeHulls(VoxelMesh *mesh)
{
    int i;
    for (i = 0; i < mesh->hullTplCount; i++)
    {
        if (mesh->hullTpls[i].vbo)
            glDeleteBuffers(1, (GLuint *)&mesh->hullTpls[i].vbo);
        VoxelHull_Free(&mesh->hullTpls[i]);
    }
    mesh->hullTplCount = 0;
    mesh->stampCount = 0;
}

void VoxelMesh_Free(VoxelMesh *mesh)
{
    FreeHulls(mesh);
    free(mesh->hullTpls);
    free(mesh->stamps);
    free(mesh->verts);
    free(mesh->ranges);
    free(mesh->ghost);
    if (mesh->vbo)
        glDeleteBuffers(1, (GLuint *)&mesh->vbo);
    memset(mesh, 0, sizeof(*mesh));
    mesh->vboDirty = true;
}

void VoxelMesh_Clear(VoxelMesh *mesh)
{
    FreeHulls(mesh);
    mesh->count = 0;
    mesh->rangeCount = 0;
    mesh->vboDirty = true;
    memset(&mesh->stats, 0, sizeof(mesh->stats));
    mesh->profileCount = 0;
    mesh->roofCount = 0;
    mesh->contactCount = 0;
}

void VoxelMesh_InvalidateGL(VoxelMesh *mesh)
{
    int i;
    for (i = 0; i < mesh->hullTplCount; i++)
        mesh->hullTpls[i].vbo = 0;
    mesh->vbo = 0;
    mesh->vboDirty = true;
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

static void Tri(VoxelMesh *mesh,
                float x0, float y0, float z0, float u0, float v0,
                float x1, float y1, float z1, float u1, float v1,
                float x2, float y2, float z2, float u2, float v2,
                int shade)
{
    VoxelVertex *v = Reserve(mesh, 3);
    Vtx(&v[0], x0, y0, z0, u0, v0, shade, 255);
    Vtx(&v[1], x1, y1, z1, u1, v1, shade, 255);
    Vtx(&v[2], x2, y2, z2, u2, v2, shade, 255);
    mesh->count += 3;
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

// Horizontal quad over an arbitrary rectangle.
static void TopRect(VoxelMesh *mesh, float x0, float x1, float z0, float z1, float y, UvRect uv, int shade)
{
    Quad(mesh,
         x0, y, z0, uv.u0, uv.v0,
         x1, y, z0, uv.u1, uv.v0,
         x1, y, z1, uv.u1, uv.v1,
         x0, y, z1, uv.u0, uv.v1,
         shade);
}

// Horizontal quad covering cell (x, z) with a shade per corner (NW, NE, SE,
// SW), so contact shadow can creep in from the corners that touch a wall.
static void TopQuadShaded(VoxelMesh *mesh, float x, float z, float y, UvRect uv, const int *sh)
{
    VoxelVertex *v = Reserve(mesh, 6);
    Vtx(&v[0], x,        y, z,        uv.u0, uv.v0, sh[0], 255);
    Vtx(&v[1], x + 1.0f, y, z,        uv.u1, uv.v0, sh[1], 255);
    Vtx(&v[2], x + 1.0f, y, z + 1.0f, uv.u1, uv.v1, sh[2], 255);
    Vtx(&v[3], x,        y, z,        uv.u0, uv.v0, sh[0], 255);
    Vtx(&v[4], x + 1.0f, y, z + 1.0f, uv.u1, uv.v1, sh[2], 255);
    Vtx(&v[5], x,        y, z + 1.0f, uv.u0, uv.v1, sh[3], 255);
    mesh->count += 6;
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

// The same faces over arbitrary extents: a wall at plane z spanning x0..x1
// (facing south when `south`), a wall at plane x spanning z0..z1 (facing
// east when `east`).
static void WallZ(VoxelMesh *mesh, float x0, float x1, float zPlane, float y0, float y1, bool south, UvRect uv, int shade)
{
    if (south)
        Quad(mesh,
             x0, y0, zPlane, uv.u0, uv.v1,
             x1, y0, zPlane, uv.u1, uv.v1,
             x1, y1, zPlane, uv.u1, uv.v0,
             x0, y1, zPlane, uv.u0, uv.v0,
             shade);
    else
        Quad(mesh,
             x1, y0, zPlane, uv.u0, uv.v1,
             x0, y0, zPlane, uv.u1, uv.v1,
             x0, y1, zPlane, uv.u1, uv.v0,
             x1, y1, zPlane, uv.u0, uv.v0,
             shade);
}

static void WallX(VoxelMesh *mesh, float xPlane, float z0, float z1, float y0, float y1, bool east, UvRect uv, int shade)
{
    if (east)
        Quad(mesh,
             xPlane, y0, z1, uv.u0, uv.v1,
             xPlane, y0, z0, uv.u1, uv.v1,
             xPlane, y1, z0, uv.u1, uv.v0,
             xPlane, y1, z1, uv.u0, uv.v0,
             shade);
    else
        Quad(mesh,
             xPlane, y0, z0, uv.u0, uv.v1,
             xPlane, y0, z1, uv.u1, uv.v1,
             xPlane, y1, z1, uv.u1, uv.v0,
             xPlane, y1, z0, uv.u0, uv.v0,
             shade);
}

// A box painted in one flat colour (one pixel of the atlas): pillars, posts.
static void ColourBox(VoxelMesh *mesh, float x0, float x1, float y0, float y1, float z0, float z1, float u, float v, int shade)
{
    UvRect uv = { u, v, u, v };
    WallZ(mesh, x0, x1, z1, y0, y1, true, uv, shade);
    WallZ(mesh, x0, x1, z0, y0, y1, false, uv, shade - 60);
    WallX(mesh, x1, z0, z1, y0, y1, true, uv, shade - 30);
    WallX(mesh, x0, z0, z1, y0, y1, false, uv, shade - 45);
    TopRect(mesh, x0, x1, z0, z1, y1, uv, shade + 15 > 255 ? 255 : shade + 15);
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

static bool IsDeck(const VoxelCell *c)
{
    return c && (c->cls == VC_BRIDGE || c->cls == VC_LOG);
}

// A column whose mass does not fill its cell: the ground shows around it.
static bool IsOpenColumn(const VoxelCell *c)
{
    return c && (c->cls == VC_WALL) && (c->shape == SH_TREE || c->shape == SH_BUSH || c->shape == SH_STANDEE
                                         || c->shape == SH_STANDEE_BOX || c->shape == SH_STANDEE_TALL
                                         || c->shape == SH_NONE || c->shape == SH_SUPPORT);
}

// Contact shadow: something tall standing beside a floor cell darkens the
// floor's corners it touches -- a wall or building by a full step, a tree
// or a fence post by a lighter one -- the way the mod's ambient occlusion
// seats its columns in the ground.
static bool IsTallBeside(const VoxelCell *n, float top)
{
    if (n == NULL || n->cls == VC_VOID)
        return false;
    if (n->shape == SH_TREE || n->shape == SH_BUSH || n->shape == SH_STANDEE
        || n->shape == SH_STANDEE_TALL || n->shape == SH_STANDEE_BOX)
        return false;                          // handled as a light contact
    if (!(n->flags & VCF_BLOCKED))
        return false;
    if (n->building >= 0 || IsColumn(n))
        return n->topH > top + 0.4f;
    return false;
}

static bool IsLightBeside(const VoxelCell *n)
{
    return n && n->cls != VC_VOID && (n->shape == SH_TREE || n->shape == SH_BUSH || n->shape == SH_STANDEE
                                       || n->shape == SH_STANDEE_TALL || n->shape == SH_STANDEE_BOX);
}

// Shade of the corner (cx, cz) in {0,1}^2 of cell (lx, ly) whose top is `top`.
static int CornerShade(const VoxelInstance *inst, int lx, int ly, int cx, int cz, float top)
{
    int dark = 0, i;
    static const int ox[3] = {-1, 0, -1};   // the three other cells sharing the corner:
    static const int oz[3] = {-1, -1, 0};   // (cx-1,cz-1) (cx,cz-1) (cx-1,cz) relative to (lx+cx, ly+cz)
    for (i = 0; i < 3; i++)
    {
        int nx = lx + cx + ox[i], nz = ly + cz + oz[i];
        const VoxelCell *n;
        if (nx == lx && nz == ly)
            continue;
        n = Nb(inst, nx, nz);
        if (IsTallBeside(n, top)) dark += 45;
        else if (IsLightBeside(n)) dark += 22;
    }
    // the cell diagonal from the corner is not one of the three above when
    // cx/cz select it; count it too so a corner between two walls is darkest
    {
        int nx = lx + (cx ? 1 : -1), nz = ly + (cz ? 1 : -1);
        const VoxelCell *n = Nb(inst, nx, nz);
        if (IsTallBeside(n, top)) dark += 20;
        else if (IsLightBeside(n)) dark += 10;
    }
    if (dark > 105) dark = 105;
    return 255 - dark;
}

// The four corner shades of a floor cell; true when any corner is darkened.
static bool ContactShades(const VoxelInstance *inst, int lx, int ly, float top, int *sh)
{
    sh[0] = CornerShade(inst, lx, ly, 0, 0, top);
    sh[1] = CornerShade(inst, lx, ly, 1, 0, top);
    sh[2] = CornerShade(inst, lx, ly, 1, 1, top);
    sh[3] = CornerShade(inst, lx, ly, 0, 1, top);
    return sh[0] < 255 || sh[1] < 255 || sh[2] < 255 || sh[3] < 255;
}

static void NoteContact(VoxelMesh *mesh, float x, float z, const int *sh)
{
    mesh->stats.contactQuads++;
    if (mesh->contactCount < VOXEL_CONTACT_KEEP)
    {
        VoxelContactRecord *c = &mesh->contacts[mesh->contactCount++];
        int i;
        c->x = (int16_t)x; c->z = (int16_t)z;
        for (i = 0; i < 4; i++) c->corner[i] = (uint8_t)sh[i];
    }
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
    s = UvFor(inst->shellMeta, VOXEL_VAR_COMPOSED);
    if (c->flags & VCF_SHELL_N) NorthFace(mesh, x, z - 1.0f, floor, shellTop, s, SHADE_SOUTH - 60);
    if (c->flags & VCF_SHELL_E) WestFace(mesh, x + 1.0f, z, skirt, shellTop, s, SHADE_WEST);
    if (c->flags & VCF_SHELL_W) EastFace(mesh, x - 1.0f, z, skirt, shellTop, s, SHADE_EAST);
}

// Fascias from a surface's top down to lower neighbours, in the ground's
// own texture (a terrace step with no cliff art drawn).
static void GroundFasciasMeta(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly, float top, int meta, int variant)
{
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    UvRect uv = UvFor(meta, variant);
    static const int dx[4] = {0, 0, 1, -1};
    static const int dy[4] = {1, -1, 0, 0};
    int d;
    for (d = 0; d < 4; d++)
    {
        const VoxelCell *n = Nb(inst, lx + dx[d], ly + dy[d]);
        float nh;
        if (n == NULL || n->cls == VC_VOID)
        {
            if (!inst->indoor && top > 0.01f)
                nh = 0.0f;       // the map edge: drop to the world floor
            else
                continue;
        }
        else
        {
            nh = IsDeck(n) && n->underH < n->topH ? n->underH : n->topH;
            if (IsColumn(n) || IsOpenColumn(n))
                nh = n->groundH < top ? n->groundH : top;
        }
        if (nh >= top - 0.01f)
            continue;
        if (d == 0) SouthFace(mesh, x, z, nh, top, uv, SHADE_SOUTH - 70);
        else if (d == 1) NorthFace(mesh, x, z, nh, top, uv, SHADE_NORTH - 40);
        else if (d == 2) EastFace(mesh, x, z, nh, top, uv, SHADE_EAST - 60);
        else WestFace(mesh, x, z, nh, top, uv, SHADE_WEST - 60);
    }
}

static void GroundFascias(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly, float top, int variant)
{
    GroundFasciasMeta(mesh, inst, lx, ly, top, inst->cells[ly * inst->width + lx].metatile, variant);
}

// ---------------------------------------------------------------------------
// Ledges: the cell is the rim of the terrace it edges, standing the lip's
// six pixels; the rim art faces the hop side, turf the others.
// ---------------------------------------------------------------------------
static void BuildLedge(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    float top = c->topH;
    int m = c->metatile;
    static const int dx[4] = {0, 0, 1, -1};
    static const int dy[4] = {1, -1, 0, 0};
    static const int lipBit[4] = {LIP_S, LIP_N, LIP_E, LIP_W};
    int d;

    TopQuad(mesh, x, z, top, UvFor(m, VOXEL_VAR_COMPOSED), SHADE_TOP);
    mesh->stats.lips++;
    for (d = 0; d < 4; d++)
    {
        const VoxelCell *n = Nb(inst, lx + dx[d], ly + dy[d]);
        float nh;
        bool lip = (c->lipDirs & lipBit[d]) != 0;
        if (n == NULL || n->cls == VC_VOID)
            continue;
        nh = n->topH;
        if (IsColumn(n) || IsOpenColumn(n))
            nh = n->groundH;
        if (n->cls == VC_LEDGE && !lip)
            continue;                   // the rim runs on into the next ledge cell
        if (nh >= top - 0.01f)
            continue;
        if (lip)
        {
            // the drawn rim: its bottom rows on the south face, its right
            // columns on the east face, and so on
            UvRect s;
            mesh->stats.lipFaces++;
            if (d == 0) { s = UvSlice(m, VOXEL_VAR_COMPOSED, 0.6f, 1.0f); SouthFace(mesh, x, z, nh, top, s, SHADE_SOUTH - 20); }
            else if (d == 1) { s = UvSlice(m, VOXEL_VAR_COMPOSED, 0.0f, 0.4f); NorthFace(mesh, x, z, nh, top, s, SHADE_NORTH); }
            else if (d == 2)
            {
                s = UvRegion(m, VOXEL_VAR_COMPOSED, 10.0f, 16.0f, 0.0f, 16.0f);
                Quad(mesh,
                     x + 1.0f, nh,  z + 1.0f, s.u1, s.v1,
                     x + 1.0f, nh,  z,        s.u1, s.v0,
                     x + 1.0f, top, z,        s.u0, s.v0,
                     x + 1.0f, top, z + 1.0f, s.u0, s.v1,
                     SHADE_EAST);
            }
            else
            {
                s = UvRegion(m, VOXEL_VAR_COMPOSED, 0.0f, 6.0f, 0.0f, 16.0f);
                Quad(mesh,
                     x, nh,  z,        s.u0, s.v0,
                     x, nh,  z + 1.0f, s.u0, s.v1,
                     x, top, z + 1.0f, s.u1, s.v1,
                     x, top, z,        s.u1, s.v0,
                     SHADE_WEST);
            }
        }
        else
        {
            UvRect g = UvSlice(m, VOXEL_VAR_GROUND, 0.5f, 1.0f);
            if (d == 0) SouthFace(mesh, x, z, nh, top, g, SHADE_SOUTH - 50);
            else if (d == 1) NorthFace(mesh, x, z, nh, top, g, SHADE_NORTH - 30);
            else if (d == 2) EastFace(mesh, x, z, nh, top, g, SHADE_EAST - 40);
            else WestFace(mesh, x, z, nh, top, g, SHADE_WEST - 40);
        }
    }
    ShellFaces(mesh, inst, c, x, z, c->groundH);
}

// ---------------------------------------------------------------------------
// Tall grass: a mat a quarter course over the ground, skirted where no
// other mat stands against it, so a walker's feet sink into it.
// ---------------------------------------------------------------------------
static void BuildMat(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    float top = c->topH, base = c->groundH;
    int m = c->metatile;
    // Structures.lua buildGen3Grass: each skirt holds the art's own edge line
    // constant up its height, turned from the sun (0.78)
    UvRect skirtS = UvRegion(m, VOXEL_VAR_COMPOSED, 0.0f, 16.0f, 15.4f, 15.6f);
    UvRect skirtN = UvRegion(m, VOXEL_VAR_COMPOSED, 0.0f, 16.0f, 0.4f, 0.6f);
    UvRect skirtE = UvRegion(m, VOXEL_VAR_COMPOSED, 15.4f, 15.6f, 0.0f, 16.0f);
    UvRect skirtW = UvRegion(m, VOXEL_VAR_COMPOSED, 0.4f, 0.6f, 0.0f, 16.0f);
    static const int dx[4] = {0, 0, 1, -1};
    static const int dy[4] = {1, -1, 0, 0};
    int d;

    TopQuad(mesh, x, z, top, UvFor(m, VOXEL_VAR_COMPOSED), SHADE_TOP);
    mesh->stats.mats++;
    for (d = 0; d < 4; d++)
    {
        const VoxelCell *n = Nb(inst, lx + dx[d], ly + dy[d]);
        float nh;
        if (n && n->shape == SH_MAT && fabsf(n->topH - top) < 0.01f)
            continue;
        nh = (n == NULL || n->cls == VC_VOID) ? base : ((IsColumn(n) || IsOpenColumn(n)) ? n->groundH : n->topH);
        if (nh > base) nh = base;
        if (nh >= top - 0.01f)
            continue;
        mesh->stats.matSkirts++;
        if (d == 0) SouthFace(mesh, x, z, nh, top, skirtS, 199);
        else if (d == 1) NorthFace(mesh, x, z, nh, top, skirtN, 199);
        else if (d == 2)
            Quad(mesh, x + 1.0f, nh, z + 1.0f, skirtE.u0, skirtE.v1,  x + 1.0f, nh, z, skirtE.u0, skirtE.v0,
                 x + 1.0f, top, z, skirtE.u1, skirtE.v0,  x + 1.0f, top, z + 1.0f, skirtE.u1, skirtE.v1, 199);
        else
            Quad(mesh, x, nh, z, skirtW.u0, skirtW.v0,  x, nh, z + 1.0f, skirtW.u0, skirtW.v1,
                 x, top, z + 1.0f, skirtW.u1, skirtW.v1,  x, top, z, skirtW.u1, skirtW.v0, 199);
    }
    ShellFaces(mesh, inst, c, x, z, base);
}

// ---------------------------------------------------------------------------
// Decks: the world continues under the deck; the deck is a slab of planking
// with a fascia on every open side, its drawn underside where the map draws
// one, and posts down to what it spans where it stands lifted.
// ---------------------------------------------------------------------------
static void BuildDeck(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    float top = c->topH;
    bool rail = c->behavior >= 0xD3 && c->behavior <= 0xD6;
    float thick = rail ? RAIL_THICK : DECK_THICK;
    float bottom = top - thick;
    int m = c->metatile;
    static const int dx[4] = {0, 0, 1, -1};
    static const int dy[4] = {1, -1, 0, 0};
    bool open[4];
    int d;
    float pu, pv;

    if (c->underH < bottom - 0.01f)
        TopQuad(mesh, x, z, c->underH, UvFor(c->underMeta, VOXEL_VAR_COMPOSED), SHADE_TOP - 40);
    else
    {
        bottom = c->underH;
        // the plank's own ground, carved of the rail, shows around it
        if (c->underMeta == m)
            TopQuad(mesh, x, z, c->underH, UvFor(m, VOXEL_VAR_GROUND), SHADE_TOP - 10);
    }
    TopQuad(mesh, x, z, top, UvFor(m, VOXEL_VAR_COMPOSED), SHADE_TOP);
    mesh->stats.decks++;

    for (d = 0; d < 4; d++)
    {
        const VoxelCell *n = Nb(inst, lx + dx[d], ly + dy[d]);
        float lo = bottom;
        open[d] = !(IsDeck(n) && fabsf(n->topH - top) < 0.01f);
        if (!open[d])
            continue;
        if (n && n->cls != VC_VOID && !IsDeck(n) && !IsColumn(n) && n->topH > lo && n->topH < top - 0.01f)
            lo = n->topH;           // the bank meets the slab part way up
        if (n && n->cls != VC_VOID && n->topH >= top - 0.01f && !IsOpenColumn(n))
            continue;               // the bank is level with the deck: no edge shows
        if (d == 0 && c->fasciaMeta != 0xFFFF && c->lift >= 1.0f)
        {
            // the drawn underside: the pier art stretched from its ground to the deck
            float ground = n ? n->groundH : c->underH;
            SouthFace(mesh, x, z, ground, top, UvFor(c->fasciaMeta, VOXEL_VAR_COMPOSED), SHADE_SOUTH);
            mesh->stats.deckFascias++;
            continue;
        }
        mesh->stats.deckFascias++;
        {
            UvRect s = UvSlice(m, VOXEL_VAR_COMPOSED, 0.6f, 1.0f);
            if (d == 0) SouthFace(mesh, x, z, lo, top, s, SHADE_SOUTH - 30);
            else if (d == 1) NorthFace(mesh, x, z, lo, top, s, SHADE_NORTH);
            else if (d == 2) EastFace(mesh, x, z, lo, top, s, SHADE_EAST);
            else WestFace(mesh, x, z, lo, top, s, SHADE_WEST);
        }
    }

    // Posts under a lifted deck: on its flanks and at its open ends.
    if (c->lift >= 1.0f && c->underH < bottom - 0.05f)
    {
        UvPixel(m, VOXEL_VAR_COMPOSED, 8, 15, &pu, &pv);
        if (open[3] || open[0])
        {
            ColourBox(mesh, x + 0.02f, x + 0.02f + PILLAR_W, c->underH, bottom, z + 1.0f - 0.02f - PILLAR_W, z + 1.0f - 0.02f, pu, pv, 120);
            mesh->stats.pillars++;
        }
        if (open[2] || open[0])
        {
            ColourBox(mesh, x + 1.0f - 0.02f - PILLAR_W, x + 1.0f - 0.02f, c->underH, bottom, z + 1.0f - 0.02f - PILLAR_W, z + 1.0f - 0.02f, pu, pv, 120);
            mesh->stats.pillars++;
        }
        if (open[1])
        {
            if (open[3]) { ColourBox(mesh, x + 0.02f, x + 0.02f + PILLAR_W, c->underH, bottom, z + 0.02f, z + 0.02f + PILLAR_W, pu, pv, 120); mesh->stats.pillars++; }
            if (open[2]) { ColourBox(mesh, x + 1.0f - 0.02f - PILLAR_W, x + 1.0f - 0.02f, c->underH, bottom, z + 0.02f, z + 0.02f + PILLAR_W, pu, pv, 120); mesh->stats.pillars++; }
        }
    }
    ShellFaces(mesh, inst, c, x, z, c->groundH);
}

// ---------------------------------------------------------------------------
// Surfaces: ground, water, walkable pins, flat blocked cells, canopies
// ---------------------------------------------------------------------------
// The ground each vacated cell paints (per build; 0xFFFF: its own art).
static uint16_t *sGroundFor;

static void BuildSurface(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    float top = c->topH;
    int variant = c->shape == SH_SUPPORT ? VOXEL_VAR_GROUND : VOXEL_VAR_COMPOSED;
    int art = c->metatile;

    // A cell scenery stood up (a hull, a sign) or whose art moved onto a
    // hull paints the ground the drawing stands on: the matched floor
    // metatile, else its own picture with the object filled in ground.
    if (c->shape == SH_HULL || c->shape == SH_SIGN || c->shape == SH_PALING || (c->flags & VCF_VACATED))
    {
        uint16_t g = sGroundFor ? sGroundFor[ly * inst->width + lx] : 0xFFFF;
        if (g != 0xFFFF) { art = g; variant = VOXEL_VAR_COMPOSED; }
        else variant = VOXEL_VAR_GROUND;
    }

    if (c->shape == SH_LIP) { BuildLedge(mesh, inst, lx, ly); return; }
    if (c->shape == SH_MAT) { BuildMat(mesh, inst, lx, ly); return; }
    if (c->shape == SH_DECK) { BuildDeck(mesh, inst, lx, ly); return; }

    if (c->shape == SH_SUPPORT) mesh->stats.supports++;
    else if (c->shape == SH_NONE && (c->flags & VCF_BLOCKED)) mesh->stats.flats++;
    {
        int sh[4];
        if (ContactShades(inst, lx, ly, top, sh))
        {
            TopQuadShaded(mesh, x, z, top, UvFor(art, variant), sh);
            NoteContact(mesh, x, z, sh);
        }
        else
            TopQuad(mesh, x, z, top, UvFor(art, variant), SHADE_TOP);
    }
    GroundFasciasMeta(mesh, inst, lx, ly, top, art, variant);
    ShellFaces(mesh, inst, c, x, z, c->groundH);

    if (c->overhead)
    {
        // the drawn treetop over the path, hung at treetop height
        VoxelMeshRange r;
        float y = c->groundH + CANOPY_H;
        BeginRange(mesh, &r, (int)x, (int)z, -1, y - 0.05f, y);
        TopQuad(mesh, x, z, y, UvFor(c->metatile, VOXEL_VAR_TOP), SHADE_TOP);
        // its underside, lit from below by nothing: dark
        Quad(mesh,
             x,        y - 0.02f, z + 1.0f, 0, 0,
             x + 1.0f, y - 0.02f, z + 1.0f, 0, 0,
             x + 1.0f, y - 0.02f, z,        0, 0,
             x,        y - 0.02f, z,        0, 0,
             90);
        {
            // the underside quad above got dummy UVs: give it the top art too
            VoxelVertex *v = &mesh->verts[mesh->count - 6];
            UvRect uv = UvFor(c->metatile, VOXEL_VAR_TOP);
            v[0].u = uv.u0; v[0].v = uv.v1;
            v[1].u = uv.u1; v[1].v = uv.v1;
            v[2].u = uv.u1; v[2].v = uv.v0;
            v[3].u = uv.u0; v[3].v = uv.v1;
            v[4].u = uv.u1; v[4].v = uv.v0;
            v[5].u = uv.u0; v[5].v = uv.v0;
        }
        EndRange(mesh, &r);
        mesh->stats.canopies++;
    }
}

// ---------------------------------------------------------------------------
// Columns: stacked blocked rows drawn as one solid mass (rocks, cliffs,
// hedges, interior walls)
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
    if (c->cls == VC_CLIFF) mesh->stats.cliffs++; else mesh->stats.boxes++;
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
    // each row's own drawing laid flat -- Emerald draws rock as a top-down
    // motif, so seen from above the mass reads as the map does -- and each
    // side shows the rows stacked as courses.
    for (j = 0; j < rows; j++)
        TopQuad(mesh, x, z - j, top, UvFor(rowMeta[j], VOXEL_VAR_COMPOSED), SHADE_TOP);
    (void)topMeta;

    hS = SideHeight(Nb(inst, lx, ly + 1));
    hN = SideHeight(Nb(inst, lx, ly - rows));
    for (k = 0; k < rows; k++)
    {
        float y0 = base + courseH * k;
        float y1 = base + courseH * (k + 1);
        float lo;
        lo = hS > y0 ? hS : y0;
        if (lo < y1 - 0.01f)
            SouthFace(mesh, x, z, lo, y1, UvSlice(rowMeta[k], VOXEL_VAR_COMPOSED, (lo - y0) / courseH, 1.0f), SHADE_SOUTH);
        lo = hN > y0 ? hN : y0;
        if (lo < y1 - 0.01f)
            NorthFace(mesh, x, z - (rows - 1), lo, y1, UvSlice(rowMeta[k], VOXEL_VAR_COMPOSED, (lo - y0) / courseH, 1.0f), SHADE_NORTH);
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
                EastFace(mesh, x, z - j, lo, y1, UvSlice(rowMeta[k], VOXEL_VAR_COMPOSED, (lo - y0) / courseH, 1.0f), SHADE_EAST);
            lo = hW > y0 ? hW : y0;
            if (lo < y1 - 0.01f)
                WestFace(mesh, x, z - j, lo, y1, UvSlice(rowMeta[k], VOXEL_VAR_COMPOSED, (lo - y0) / courseH, 1.0f), SHADE_WEST);
        }
    }
    EndRange(mesh, &r);
}

// ---------------------------------------------------------------------------
// Trees and bushes: the ground shows under them, a trunk stands in it, and
// the crown is a solid of revolution -- an octagonal drum capped by a
// hemisphere (a ball's cap is as deep as it is wide), wearing the drawn
// crown projected from above so the camera sees the picture it knows.
// ---------------------------------------------------------------------------
// A tree is the solid of revolution of its own drawing (Structures.lua
// roundTemplate): every row of the stacked silhouette becomes a disc of that
// row's drawn width, and the top rows taper under a quarter-ellipse dome as
// deep as half the widest row, so a crown that fills its cell still comes to
// a rounded top. The hull wears the drawing projected from the FRONT: the
// drawing's columns run across it, its rows run down it, so the drawn
// highlight sits at the apex and the drawn trunk at the foot. A 2x2 tree
// sums its two halves' rows into one hull.
#define CROWN_BAND_PX 4

// Row widths of a stack's drawing, top row first, in pixels (halves summed
// for a pair).
static void StackWidths(const VoxelInstance *inst, const int *metaTop, const int *metaTopR, int rows, uint8_t *w)
{
    int k, y;
    for (k = 0; k < rows; k++)
    {
        const VoxelArtInfo *a = &inst->art[metaTop[k]];
        const VoxelArtInfo *b = metaTopR ? &inst->art[metaTopR[k]] : NULL;
        for (y = 0; y < 16; y++)
        {
            int v = a->rowFill[y] + (b ? b->rowFill[y] : 0);
            // two narrow strokes side by side (a tall tree's twin trunk
            // rows) revolve as one trunk, not as a stump as wide as both
            if (b && a->rowFill[y] <= 10 && b->rowFill[y] <= 10)
                v = a->rowFill[y] > b->rowFill[y] ? a->rowFill[y] : b->rowFill[y];
            w[k * 16 + y] = (uint8_t)(v > 32 ? 32 : v);
        }
    }
}

// Ring widths and radii every CROWN_BAND_PX rows from the top: the drawn
// width's local maximum about the ring, 32 px to the world unit, capped by
// the dome.
static int ProfileRings(const uint8_t *w, int H, uint8_t *ringW, float *radius)
{
    int p, q, W = 0, dome, n = 0;
    for (p = 0; p < H; p++)
        if (w[p] > W) W = w[p];
    dome = W / 2;
    if (dome < 4) dome = 4;
    for (p = 0; p <= H && n < VOXEL_PROFILE_RINGS; p += CROWN_BAND_PX)
    {
        int lo = p - 2, hi = p + 2, m = 0;
        if (lo < 0) lo = 0;
        if (hi > H - 1) hi = H - 1;
        for (q = lo; q <= hi; q++)
            if (w[q] > m) m = w[q];
        ringW[n] = (uint8_t)m;
        radius[n] = m / 32.0f;
        if (p < dome)
        {
            float t = (float)(p + 1) / (float)dome;
            float cap = (W / 32.0f) * sqrtf(1.0f - (1.0f - t) * (1.0f - t));
            if (radius[n] > cap) radius[n] = cap;
        }
        n++;
    }
    // a crown never floats: the foot rings keep at least a stem
    if (n >= 2 && W > 0)
    {
        if (radius[n - 1] < 0.1f) radius[n - 1] = 0.1f;
        if (radius[n - 2] < 0.1f) radius[n - 2] = 0.1f;
    }
    return n;
}

// The hull about (cx, cz) from `top` down, `rows` courses tall; `metaTop`
// lists the stack's metatiles top row first (and `metaTopR` the east halves
// of a pair, or NULL). Records the profile for the dump.
static void BuildProfileHull(VoxelMesh *mesh, const VoxelInstance *inst, float cx, float cz, float top,
                             const int *metaTop, const int *metaTopR, int rows, bool contiguous)
{
    uint8_t w[64], ringW[VOXEL_PROFILE_RINGS];
    float radius[VOXEL_PROFILE_RINGS], ringY[VOXEL_PROFILE_RINGS];
    float cs[CROWN_SIDES + 1], sn[CROWN_SIDES + 1];
    float halfW = metaTopR ? 1.0f : 0.5f;
    int H = rows * 16, n, i, s;

    StackWidths(inst, metaTop, metaTopR, rows, w);
    n = ProfileRings(w, H, ringW, radius);
    for (i = 0; i < n; i++)
        ringY[i] = top - (float)(i * CROWN_BAND_PX) / 16.0f;
    for (s = 0; s <= CROWN_SIDES; s++)
    {
        float a = (float)s / CROWN_SIDES * 2.0f * (float)M_PI + (float)M_PI / CROWN_SIDES;
        cs[s] = cosf(a);
        sn[s] = sinf(a);
    }
    // the dump keeps a sample: the first two-row hulls, and every tall one
    if (mesh->profileCount < VOXEL_PROFILE_KEEP && (rows >= 3 || mesh->profileCount < VOXEL_PROFILE_KEEP / 2))
    {
        VoxelTreeProfile *pr = &mesh->profiles[mesh->profileCount++];
        memset(pr, 0, sizeof(*pr));
        pr->rows = rows;
        pr->pair = metaTopR != NULL;
        for (i = 0; i < rows && i < 4; i++) pr->metas[i] = metaTop[i];
        for (i = 0; i < n; i++) { pr->width[i] = ringW[i]; pr->radius[i] = radius[i]; }
        pr->rings = n;
    }
    // the apex: a fan from a point just over the first ring
    {
        int k = 0;
        UvRect art = UvFor(metaTop[0], VOXEL_VAR_COMPOSED);
        float uMid, uHalf;
        if (metaTopR && contiguous) art.u1 = UvFor(metaTopR[0], VOXEL_VAR_COMPOSED).u1;
        uMid = (art.u0 + art.u1) * 0.5f; uHalf = (art.u1 - art.u0) * 0.5f;
        (void)k;
        for (s = 0; s < CROWN_SIDES; s++)
        {
            float r = radius[0];
            float u0 = uMid + uHalf * (r * cs[s]) / halfW;
            float u1 = uMid + uHalf * (r * cs[s + 1]) / halfW;
            Tri(mesh,
                cx, top + 0.06f, cz, uMid, art.v0,
                cx + r * cs[s + 1], ringY[0], cz + r * sn[s + 1], u1, art.v0,
                cx + r * cs[s],     ringY[0], cz + r * sn[s],     u0, art.v0,
                255);
        }
    }
    for (i = 0; i + 1 < n; i++)
    {
        float ra = radius[i], rb = radius[i + 1];
        float ya = ringY[i], yb = ringY[i + 1];
        int pa = i * CROWN_BAND_PX, pb = (i + 1) * CROWN_BAND_PX;
        int pmid = (pa + pb) / 2, k = pmid / 16;
        UvRect art;
        float uMid, uHalf, va, vb;
        int shade = 255 - (pmid * 100) / (H > 0 ? H : 1);
        if (ra < 0.02f && rb < 0.02f)
            continue;
        if (k > rows - 1) k = rows - 1;
        art = UvFor(metaTop[k], VOXEL_VAR_COMPOSED);
        if (metaTopR && contiguous) art.u1 = UvFor(metaTopR[k], VOXEL_VAR_COMPOSED).u1;
        uMid = (art.u0 + art.u1) * 0.5f; uHalf = (art.u1 - art.u0) * 0.5f;
        va = art.v0 + (art.v1 - art.v0) * ((float)(pa - k * 16) / 16.0f);
        vb = art.v0 + (art.v1 - art.v0) * ((float)(pb - k * 16) / 16.0f);
        if (va < art.v0) va = art.v0;
        if (vb > art.v1) vb = art.v1;
        if (shade < 150) shade = 150;
        for (s = 0; s < CROWN_SIDES; s++)
        {
            float ua0 = uMid + uHalf * (ra * cs[s]) / halfW;
            float ua1 = uMid + uHalf * (ra * cs[s + 1]) / halfW;
            float ub0 = uMid + uHalf * (rb * cs[s]) / halfW;
            float ub1 = uMid + uHalf * (rb * cs[s + 1]) / halfW;
            Quad(mesh,
                 cx + ra * cs[s],     ya, cz + ra * sn[s],     ua0, va,
                 cx + rb * cs[s],     yb, cz + rb * sn[s],     ub0, vb,
                 cx + rb * cs[s + 1], yb, cz + rb * sn[s + 1], ub1, vb,
                 cx + ra * cs[s + 1], ya, cz + ra * sn[s + 1], ua1, va,
                 shade);
        }
    }
}

// A tree standing on the anchor (lx, ly) and, when `pair`, on the column
// east of it too: the stack's rows, feet at the anchor, crown north of it.
static void BuildTree(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly, bool pair)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    int rows = c->stack < 2 ? 2 : (c->stack > 4 ? 4 : c->stack);
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    float base = c->groundH, top = c->topH;
    float cx = pair ? x + 1.0f : x + 0.5f;
    int metaTop[4], metaTopR[4];
    bool contiguous = pair;
    VoxelMeshRange r;
    int j;

    if (top <= base + 0.05f)
        return;
    mesh->stats.trees++;
    if (pair) mesh->stats.treePairs++;

    // the ground the tree grows out of, and the rows of its drawing
    for (j = 0; j < rows; j++)
    {
        const VoxelCell *cj = Nb(inst, lx, ly - j);
        const VoxelCell *ej = pair ? Nb(inst, lx + 1, ly - j) : NULL;
        metaTop[rows - 1 - j] = cj ? cj->metatile : c->metatile;
        metaTopR[rows - 1 - j] = ej ? ej->metatile : metaTop[rows - 1 - j];
        if (cj)
        {
            TopQuad(mesh, x, z - j, base, UvFor(cj->metatile, VOXEL_VAR_GROUND), SHADE_TOP);
            GroundFascias(mesh, inst, lx, ly - j, base, VOXEL_VAR_GROUND);
        }
        if (ej)
        {
            TopQuad(mesh, x + 1.0f, z - j, base, UvFor(ej->metatile, VOXEL_VAR_GROUND), SHADE_TOP);
            GroundFascias(mesh, inst, lx + 1, ly - j, base, VOXEL_VAR_GROUND);
        }
        if (pair && !(metaTopR[rows - 1 - j] == metaTop[rows - 1 - j] + 1
                      && (metaTop[rows - 1 - j] % VOXEL_ATLAS_COLS) != VOXEL_ATLAS_COLS - 1))
            contiguous = false;
    }

    BeginRange(mesh, &r, (int)x, (int)z, -1, base, top);
    r.rows = rows;
    r.cols = pair ? 2 : 1;
    // the hull is centred on the stack: its rows run from the feet's south
    // edge to the crown's north edge, `rows` courses tall from the ground
    BuildProfileHull(mesh, inst, cx, z + 0.5f - (float)(rows - 1) * 0.5f, base + (float)rows,
                     metaTop, pair ? metaTopR : NULL, rows, contiguous);
    EndRange(mesh, &r);
}

static void BuildBush(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    float base = c->groundH, top = c->topH;
    int metaTop[1];
    VoxelMeshRange r;

    if (top <= base + 0.05f)
        return;
    mesh->stats.bushes++;
    metaTop[0] = c->metatile;
    TopQuad(mesh, x, z, base, UvFor(c->metatile, VOXEL_VAR_GROUND), SHADE_TOP);
    GroundFascias(mesh, inst, lx, ly, base, VOXEL_VAR_GROUND);
    BeginRange(mesh, &r, (int)x, (int)z, -1, base, top);
    BuildProfileHull(mesh, inst, x + 0.5f, z + 0.5f, base + 1.0f, metaTop, NULL, 1, false);
    EndRange(mesh, &r);
}

// ---------------------------------------------------------------------------
// Standees: a fence, a sign, a post -- the silhouette stood up as a slab
// a few voxels thick, its top and side edges built pixel run by pixel run
// from the carved art so every paling and plate has a lit edge.
// ---------------------------------------------------------------------------
static void StandeeEdges(VoxelMesh *mesh, const VoxelInstance *inst, const int *rowMeta, int rows,
                         float x, float base, float z0, float z1)
{
    int H = rows * 16, px, py, run;
    int yTop[16], xL[32], xR[32];
    float u, v;

    for (px = 0; px < 16; px++)
    {
        yTop[px] = -1;
        for (py = 0; py < H; py++)
        {
            int m = rowMeta[rows - 1 - py / 16];
            if (CarvedAlpha(inst, m, px, py % 16) > 127) { yTop[px] = py; break; }
        }
    }
    for (py = 0; py < H; py++)
    {
        int m = rowMeta[rows - 1 - py / 16];
        xL[py] = -1; xR[py] = -1;
        for (px = 0; px < 16; px++)
            if (CarvedAlpha(inst, m, px, py % 16) > 127) { xL[py] = px; break; }
        for (px = 15; px >= 0; px--)
            if (CarvedAlpha(inst, m, px, py % 16) > 127) { xR[py] = px; break; }
    }
    // top edges: runs of columns sharing a top row
    for (px = 0; px < 16; px += run)
    {
        int yt = yTop[px], m;
        float y;
        UvRect uv;
        run = 1;
        while (px + run < 16 && yTop[px + run] == yt)
            run++;
        if (yt < 0)
            continue;
        m = rowMeta[rows - 1 - yt / 16];
        UvPixel(m, VOXEL_VAR_CARVED, px + run / 2, yt % 16, &u, &v);
        uv.u0 = uv.u1 = u; uv.v0 = uv.v1 = v;
        y = base + (float)(H - yt) / 16.0f;
        TopRect(mesh, x + px / 16.0f, x + (px + run) / 16.0f, z0, z1, y, uv, SHADE_TOP);
    }
    // side edges: runs of rows sharing a leftmost / rightmost column
    for (py = 0; py < H; py += run)
    {
        int xl = xL[py], m;
        float y0, y1;
        UvRect uv;
        run = 1;
        while (py + run < H && xL[py + run] == xl)
            run++;
        if (xl < 0)
            continue;
        m = rowMeta[rows - 1 - (py + run / 2) / 16];
        UvPixel(m, VOXEL_VAR_CARVED, xl, (py + run / 2) % 16, &u, &v);
        uv.u0 = uv.u1 = u; uv.v0 = uv.v1 = v;
        y1 = base + (float)(H - py) / 16.0f;
        y0 = base + (float)(H - py - run) / 16.0f;
        WallX(mesh, x + xl / 16.0f, z0, z1, y0, y1, false, uv, SHADE_WEST);
    }
    for (py = 0; py < H; py += run)
    {
        int xr = xR[py], m;
        float y0, y1;
        UvRect uv;
        run = 1;
        while (py + run < H && xR[py + run] == xr)
            run++;
        if (xr < 0)
            continue;
        m = rowMeta[rows - 1 - (py + run / 2) / 16];
        UvPixel(m, VOXEL_VAR_CARVED, xr, (py + run / 2) % 16, &u, &v);
        uv.u0 = uv.u1 = u; uv.v0 = uv.v1 = v;
        y1 = base + (float)(H - py) / 16.0f;
        y0 = base + (float)(H - py - run) / 16.0f;
        WallX(mesh, x + (xr + 1) / 16.0f, z0, z1, y0, y1, true, uv, SHADE_EAST);
    }
}

static void BuildStandee(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    int rows = c->shape == SH_STANDEE_TALL ? 2 : 1, k;
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    float base = c->groundH;
    float depth = c->depthPx ? c->depthPx / 16.0f : STANDEE_DEPTH;
    float zc = z + 0.5f, z0 = zc - depth * 0.5f, z1 = zc + depth * 0.5f;
    int rowMeta[2];
    VoxelMeshRange r;

    if (rows == 2) mesh->stats.standeeTall++; else mesh->stats.standees++;
    for (k = 0; k < rows; k++)
    {
        const VoxelCell *ck = Nb(inst, lx, ly - k);
        rowMeta[k] = ck ? ck->metatile : c->metatile;
        TopQuad(mesh, x, z - k, base, UvFor(rowMeta[k], VOXEL_VAR_GROUND), SHADE_TOP);
        GroundFascias(mesh, inst, lx, ly - k, base, VOXEL_VAR_GROUND);
    }
    BeginRange(mesh, &r, (int)x, (int)z, -1, base, base + rows);
    r.rows = rows;
    for (k = 0; k < rows; k++)
    {
        UvRect uv = UvFor(rowMeta[k], VOXEL_VAR_CARVED);
        UvRect back = uv;
        back.u0 = uv.u1; back.u1 = uv.u0;
        WallZ(mesh, x, x + 1.0f, z1, base + k, base + k + 1.0f, true, uv, SHADE_SOUTH);
        WallZ(mesh, x, x + 1.0f, z0, base + k, base + k + 1.0f, false, back, SHADE_NORTH);
    }
    StandeeEdges(mesh, inst, rowMeta, rows, x, base, z0, z1);
    EndRange(mesh, &r);
}

// A small solid object (a mailbox, a crate): the silhouette's box.
static void BuildStandeeBox(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    const VoxelArtInfo *a = &inst->art[c->metatile];
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    float base = c->groundH;
    float x0 = x + a->x0 / 16.0f, x1 = x + (a->x1 + 1) / 16.0f;
    float w = x1 - x0;
    float depth = w < 0.6f ? w : 0.6f;
    float zc = z + 0.5f, z0 = zc - depth * 0.5f, z1 = zc + depth * 0.5f;
    float top = base + (16 - a->y0) / 16.0f;
    int m = c->metatile;
    VoxelMeshRange r;
    UvRect front = UvRegion(m, VOXEL_VAR_CARVED, a->x0, a->x1 + 1, a->y0, a->y1 + 1);
    UvRect back = front;
    UvRect side = UvRegion(m, VOXEL_VAR_COMPOSED, a->x0, a->x0 + 2, a->y0, a->y1 + 1);
    UvRect lid = UvRegion(m, VOXEL_VAR_COMPOSED, a->x0, a->x1 + 1, a->y0, a->y0 + 3);

    mesh->stats.standeeBoxes++;
    back.u0 = front.u1; back.u1 = front.u0;
    TopQuad(mesh, x, z, base, UvFor(m, VOXEL_VAR_GROUND), SHADE_TOP);
    GroundFascias(mesh, inst, lx, ly, base, VOXEL_VAR_GROUND);
    if (top <= base + 0.05f || w <= 0.05f)
        return;
    BeginRange(mesh, &r, (int)x, (int)z, -1, base, top);
    WallZ(mesh, x0, x1, z1, base, top, true, front, SHADE_SOUTH);
    WallZ(mesh, x0, x1, z0, base, top, false, back, SHADE_NORTH);
    WallX(mesh, x1, z0, z1, base, top, true, side, SHADE_EAST);
    WallX(mesh, x0, z0, z1, base, top, false, side, SHADE_WEST);
    TopRect(mesh, x0, x1, z0, z1, top, lid, SHADE_TOP);
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
    UvRect uv = UvFor(c->metatile, VOXEL_VAR_COMPOSED);
    const VoxelCell *n;
    float nh;
    bool faceOn = c->cls == VC_CONSOLE || c->cls == VC_TV || c->cls == VC_APPLIANCE
               || c->cls == VC_CABINET || c->cls == VC_WORKTOP || c->cls == VC_SINK || c->cls == VC_PROP
               || c->cls == VC_BOOKCASE;

    mesh->stats.furniture++;
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
        TopQuad(mesh, x, z, top, UvSlice(c->metatile, VOXEL_VAR_COMPOSED, 0.0f, 0.2f), SHADE_TOP - 20);
        n = Nb(inst, lx, ly + 1); nh = SideHeight(n);
        if (nh < top - 0.01f) SouthFace(mesh, x, z, base, top, uv, SHADE_SOUTH);
    }
    else
    {
        TopQuad(mesh, x, z, top, uv, SHADE_TOP);
        n = Nb(inst, lx, ly + 1); nh = SideHeight(n);
        if (nh < top - 0.01f) SouthFace(mesh, x, z, base, top, UvSlice(c->metatile, VOXEL_VAR_COMPOSED, 0.7f, 1.0f), SHADE_SOUTH - 40);
    }
    n = Nb(inst, lx, ly - 1); nh = SideHeight(n);
    if (nh < top - 0.01f) NorthFace(mesh, x, z, base, top, UvSlice(c->metatile, VOXEL_VAR_COMPOSED, 0.0f, 0.3f), SHADE_NORTH);
    n = Nb(inst, lx + 1, ly); nh = SideHeight(n);
    if (nh < top - 0.01f) EastFace(mesh, x, z, base, top, UvSlice(c->metatile, VOXEL_VAR_COMPOSED, 0.2f, 1.0f), SHADE_EAST - 30);
    n = Nb(inst, lx - 1, ly); nh = SideHeight(n);
    if (nh < top - 0.01f) WestFace(mesh, x, z, base, top, UvSlice(c->metatile, VOXEL_VAR_COMPOSED, 0.2f, 1.0f), SHADE_WEST - 30);
}

// ---------------------------------------------------------------------------
// Buildings (voxel_building.h), meshed the way ChunkMesher meshes a volume
// run, one 8px tile column at a time:
//
//   * the facade folds the drawing up its south face one tile row per 8px
//     course from the base, clamped at the building's top row -- never
//     repeated, never stretched;
//   * the flanks wear the same column's facade stack, turned from the sun;
//   * the roof is a shed: 16px of rise over the two tile rows behind the
//     south eave, then flat, wearing the roof plan's rows stretched over the
//     whole footprint -- the drawing states a shallow roof over a deep box;
//   * the outer tile columns hip down to the eave, and every step between
//     roof surfaces is closed.
// ---------------------------------------------------------------------------
static int FloorDiv2(int v)
{
    return v >= 0 ? v / 2 : -((-v + 1) / 2);
}

// Rows [v0, v1] (0..8, top down) of the tile at instance tile (tx, ty).
static UvRect TileUv(const VoxelInstance *inst, int tx, int ty, float v0, float v1)
{
    int m = VoxelScene_MetatileAt(inst, FloorDiv2(tx), FloorDiv2(ty));
    float qx = (float)((tx & 1) * 8), qy = (float)((ty & 1) * 8);
    if (m < 0) m = 0;
    return UvRegion(m, VOXEL_VAR_COMPOSED, qx, qx + 8.0f, qy + v0, qy + v1);
}

// The tile rows [north, front] building `b` stands on in tile column tx, or
// false where the column is not the building's.
static bool BldColumn(const VoxelInstance *inst, int b, int tx, int *north, int *front)
{
    const VoxelBuilding *bd = &inst->buildings[b];
    int cx = FloorDiv2(tx), c = cx - bd->minX;
    if (cx < bd->minX || cx > bd->maxX || c >= BLD_COLS || bd->colTop[c] < 0)
        return false;
    *north = bd->colTop[c] * 2;
    *front = bd->colFront[c] * 2 + 1;
    return true;
}

static bool BldHas(const VoxelInstance *inst, int b, int tx, int ty)
{
    int n, f;
    return BldColumn(inst, b, tx, &n, &f) && ty >= n && ty <= f;
}

// What the cell beside a wall presents to it.
static float BesideH(const VoxelInstance *inst, int b, int tx, int ty, float base)
{
    const VoxelCell *n = Nb(inst, FloorDiv2(tx), FloorDiv2(ty));
    float h;
    if (n == NULL || n->cls == VC_VOID)
        return base < 0.0f ? base : 0.0f;
    if (n->building >= 0 && n->building != b)
        return inst->buildings[n->building].eave;
    if (n->building == b)
        return base;
    h = n->topH;
    if (IsColumn(n) || IsOpenColumn(n) || (n->flags & (VCF_SCENE | VCF_VACATED)))
        h = n->groundH;
    if (IsDeck(n) && n->underH < n->topH)
        h = n->underH;
    return h;
}

// One wall of tile (tx, ty) facing `dir` (0 S, 1 N, 2 E, 3 W), from `lo` up
// to the eave, cut into 8px courses counted from the base. `rowOf(bb)` names
// the tile row course bb wears.
static void BldWall(VoxelMesh *mesh, const VoxelInstance *inst, int dir, int tx, int ty, float lo, float eave,
                    float base, int north, int front, int artNorth)
{
    float x0 = (float)inst->originX + tx * 0.5f, x1 = x0 + 0.5f;
    float z0 = (float)inst->originY + ty * 0.5f, z1 = z0 + 0.5f;
    int k, kLo, kHi, shade;
    if (lo >= eave - 0.001f)
        return;
    kLo = (int)floorf((lo - base) / 0.5f + 0.0001f);
    kHi = (int)ceilf((eave - base) / 0.5f - 0.0001f) - 1;
    shade = dir == 0 ? 255 : dir == 1 ? SHADE_NORTH : dir == 2 ? SHADE_EAST : SHADE_WEST;
    for (k = kLo; k <= kHi; k++)
    {
        float y0 = base + k * 0.5f, y1 = y0 + 0.5f, vTop, vBot;
        int bb = k < 0 ? 0 : k, row;
        UvRect uv;
        if (y0 < lo) y0 = lo;
        if (y1 > eave) y1 = eave;
        if (y1 <= y0 + 0.001f)
            continue;
        // partial courses show the tile's upper rows
        vTop = (k + 1) * 8.0f - (y1 - base) * 16.0f;
        vBot = (k + 1) * 8.0f - (y0 - base) * 16.0f;
        if (vTop < 0) vTop = 0;
        if (vBot > 8) vBot = 8;
        if (dir == 1)
            row = artNorth + bb > front ? front : artNorth + bb;
        else
            row = front - bb < artNorth ? artNorth : front - bb;
        uv = TileUv(inst, tx, row, vTop, vBot);
        if (dir == 0)
            Quad(mesh, x0, y0, z1, uv.u0, uv.v1,  x1, y0, z1, uv.u1, uv.v1,  x1, y1, z1, uv.u1, uv.v0,  x0, y1, z1, uv.u0, uv.v0, shade);
        else if (dir == 1)
            Quad(mesh, x1, y0, z0, uv.u0, uv.v1,  x0, y0, z0, uv.u1, uv.v1,  x0, y1, z0, uv.u1, uv.v0,  x1, y1, z0, uv.u0, uv.v0, shade);
        else if (dir == 2)
            Quad(mesh, x1, y0, z1, uv.u0, uv.v1,  x1, y0, z0, uv.u1, uv.v1,  x1, y1, z0, uv.u1, uv.v0,  x1, y1, z1, uv.u0, uv.v0, shade);
        else
            Quad(mesh, x0, y0, z0, uv.u0, uv.v1,  x0, y0, z1, uv.u1, uv.v1,  x0, y1, z1, uv.u1, uv.v0,  x0, y1, z0, uv.u0, uv.v0, shade);
    }
    (void)north;
}

// the shed: rise over two tile rows from the south eave, then flat
static float ShedH(const VoxelBuilding *bd, int d)
{
    float t = d / 2.0f;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return bd->eave + bd->rise * t;
}

static void BuildBuilding(VoxelMesh *mesh, const VoxelInstance *inst, int b)
{
    const VoxelBuilding *bd = &inst->buildings[b];
    int tx, ty;
    VoxelMeshRange r;
    VoxelRoofRecord rec;
    int roofArtRows = bd->roofRows > 0 ? bd->roofRows * 2 : 2;

    mesh->stats.buildings++;
    memset(&rec, 0, sizeof(rec));
    rec.building = b;
    rec.peak = bd->peak;
    rec.eave = bd->eave;
    rec.frontRows = roofArtRows;
    BeginRange(mesh, &r, bd->minX + inst->originX, bd->doorY + inst->originY, b, bd->base, bd->peak);
    r.rows = bd->doorY - bd->minY + 1;
    r.cols = bd->maxX - bd->minX + 1;

    for (tx = bd->minX * 2; tx <= bd->maxX * 2 + 1; tx++)
    {
        int north, front, extent, artTop, artRows, artNorth;
        if (!BldColumn(inst, b, tx, &north, &front))
            continue;
        extent = front - north + 1;
        artNorth = north;
        artTop = north;
        artRows = roofArtRows < extent ? roofArtRows : extent;
        if (bd->roofArtAbove)
        {
            artTop -= 2;
            artRows += 2;
        }

        // walls: the south face at the front, the back at the north, flanks
        // wherever the neighbouring tile column does not carry the building
        BldWall(mesh, inst, 0, tx, front, BesideH(inst, b, tx, front + 1, bd->base), bd->eave, bd->base, north, front, artNorth);
        if (!BldHas(inst, b, tx, north - 1))
            BldWall(mesh, inst, 1, tx, north, BesideH(inst, b, tx, north - 1, bd->base), bd->eave, bd->base, north, front, artNorth);
        for (ty = north; ty <= front; ty++)
        {
            if (!BldHas(inst, b, tx + 1, ty))
                BldWall(mesh, inst, 2, tx, ty, BesideH(inst, b, tx + 1, ty, bd->base), bd->eave, bd->base, north, front, artNorth);
            if (!BldHas(inst, b, tx - 1, ty))
                BldWall(mesh, inst, 3, tx, ty, BesideH(inst, b, tx - 1, ty, bd->base), bd->eave, bd->base, north, front, artNorth);
        }

        // the roof, one tile at a time
        for (ty = north; ty <= front; ty++)
        {
            float x0 = (float)inst->originX + tx * 0.5f, x1 = x0 + 0.5f;
            float z0 = (float)inst->originY + ty * 0.5f, z1 = z0 + 0.5f;
            int d0 = front - ty, ai, row;
            float band = artRows * 8.0f, p0, p1, rv0, rv1, hS, hN, swY, seY, neY, nwY;
            bool hipW = !BldHas(inst, b, tx - 1, ty), hipE = !BldHas(inst, b, tx + 1, ty);
            UvRect uv;
            if (bd->rise > 0.001f)
            {
                // STRETCHED, NOT TILED: the roof band maps continuously from
                // the eave at the south edge to its top at the north edge
                float a = (1.0f - (float)d0 / extent), c = (1.0f - (float)(d0 + 1) / extent);
                if (a < 0) a = 0;
                if (c < 0) c = 0;
                a *= band; c *= band;
                p0 = a < c ? a : c;
                p1 = a < c ? c : a;
            }
            else
            {
                int d = ty - north;
                p0 = (float)d / extent * band;
                p1 = (float)(d + 1) / extent * band;
            }
            if (p1 - p0 < 0.5f)
            {
                if (p0 <= 0.001f) { p0 = 4.0f; p1 = 8.0f; }
                else p1 = p0 + 0.5f;
            }
            ai = (int)floorf((p0 + p1) / 2.0f / 8.0f);
            if (ai < 0) ai = 0;
            if (ai > artRows - 1) ai = artRows - 1;
            row = artTop + ai;
            rv0 = p0 - ai * 8.0f;
            if (rv0 < 0) rv0 = 0;
            if (rv0 > 7.5f) rv0 = 7.5f;
            rv1 = p1 - ai * 8.0f;
            if (rv1 > 8.0f) rv1 = 8.0f;
            if (rv1 < rv0 + 0.5f) rv1 = rv0 + 0.5f;
            uv = TileUv(inst, tx, row, rv0, rv1);

            hS = ShedH(bd, d0);
            hN = ShedH(bd, d0 + 1);
            swY = seY = hS;
            nwY = neY = hN;
            if (hipW) { swY = hS - 0.5f > bd->eave ? hS - 0.5f : bd->eave; nwY = hN - 0.5f > bd->eave ? hN - 0.5f : bd->eave; }
            if (hipE) { seY = hS - 0.5f > bd->eave ? hS - 0.5f : bd->eave; neY = hN - 0.5f > bd->eave ? hN - 0.5f : bd->eave; }
            Quad(mesh,
                 x0, swY, z1, uv.u0, uv.v1,
                 x1, seY, z1, uv.u1, uv.v1,
                 x1, neY, z0, uv.u1, uv.v0,
                 x0, nwY, z0, uv.u0, uv.v0,
                 242);
            if (bd->rise > 0.001f)
                rec.frontSlopes++;

            // CLOSE THE SIDE OF THE ROOF: the step to a neighbouring column
            // whose roof stands lower here, and the back above the eave
            {
                int nNorth, nFront, side;
                for (side = 0; side < 2; side++)
                {
                    int ntx = side ? tx + 1 : tx - 1;
                    float sY = side ? seY : swY, nY = side ? neY : nwY, bS = bd->eave, bN = bd->eave, fx = side ? x1 : x0;
                    if (BldColumn(inst, b, ntx, &nNorth, &nFront) && ty >= nNorth && ty <= nFront)
                    {
                        float es = ShedH(bd, nFront - ty), en = ShedH(bd, nFront - ty + 1);
                        bS = es < sY ? es : sY;
                        bN = en < nY ? en : nY;
                        if (bS < bd->eave) bS = bd->eave;
                        if (bN < bd->eave) bN = bd->eave;
                    }
                    if (sY - bS < 0.01f && nY - bN < 0.01f)
                        continue;
                    if (side)
                        Quad(mesh, fx, bS, z1, uv.u0, uv.v1,  fx, bN, z0, uv.u1, uv.v1,  fx, nY, z0, uv.u1, uv.v0,  fx, sY, z1, uv.u0, uv.v0, SHADE_EAST);
                    else
                        Quad(mesh, fx, bN, z0, uv.u0, uv.v1,  fx, bS, z1, uv.u1, uv.v1,  fx, sY, z1, uv.u1, uv.v0,  fx, nY, z0, uv.u0, uv.v0, SHADE_WEST);
                }
                if (ty == north && (nwY > bd->eave + 0.01f || neY > bd->eave + 0.01f))
                    Quad(mesh, x1, bd->eave, z0, uv.u0, uv.v1,  x0, bd->eave, z0, uv.u1, uv.v1,
                         x0, nwY, z0, uv.u1, uv.v0,  x1, neY, z0, uv.u0, uv.v0, SHADE_NORTH);
            }
        }
    }
    EndRange(mesh, &r);
    if (mesh->roofCount < VOXEL_ROOF_KEEP)
        mesh->roofs[mesh->roofCount++] = rec;
}

// ---------------------------------------------------------------------------
// The border ring (Structures.lua gen3RingCarved, standGen3Apron): past an
// unconnected edge the map's border patch carries on. A foliage border is
// carved into tree hulls two cells deep -- four where no neighbouring map's
// own ring can reach -- with a floor laid under them at the height of the
// map edge; a sea border carries the sea on; nothing is laid under a
// connected neighbour's body, which draws itself there.
// ---------------------------------------------------------------------------
#define RING_ROUND 2        // cells: always carved
#define RING_DEEP  4        // cells: carved where no other map reaches
#define RING_REACH 6        // cells: how far another map's own ring reaches
#define RING_SEA   6        // cells: a sea border runs to the mod's horizon

enum { BORDER_OTHER, BORDER_FOLIAGE, BORDER_WATER };

static uint8_t *sRingCarve;       // per scene-grid cell: carved ring (1), or not
static uint16_t *sRingGround;     // per scene-grid cell: the ground a hull left there

static bool IsWaterBehavior(uint8_t b)
{
    return (b >= 0x10 && b <= 0x1A) || b == 0x22 || b == 0x28 || b == 0x2A || b == 0x2B || (b >= 0x50 && b <= 0x53);
}

static int BorderKind(const VoxelInstance *inst)
{
    const struct MapLayout *layout = inst->header->mapLayout;
    int i, leafy = 0, water = 0;
    if (layout->border == NULL)
        return BORDER_OTHER;
    for (i = 0; i < 4; i++)
    {
        int m = layout->border[i] & 0x3FF;
        if (inst->used[m] && inst->art[m].leafy)
            leafy++;
        if (IsWaterBehavior(inst->meta[m].behavior))
            water++;
    }
    if (water >= 3)
        return BORDER_WATER;
    if (leafy >= 3)
        return BORDER_FOLIAGE;
    return BORDER_OTHER;
}

// Is world cell (wx, wz) inside another instance's body, grown by `grow`?
static bool OtherBodyNear(const VoxelInstance *inst, int wx, int wz, int grow)
{
    int i;
    for (i = 0; i < gVoxelInstanceCount; i++)
    {
        const VoxelInstance *o = &gVoxelInstances[i];
        if (o == inst || o->cells == NULL)
            continue;
        if (wx >= o->originX - grow && wx < o->originX + o->width + grow
            && wz >= o->originY - grow && wz < o->originY + o->height + grow)
            return true;
    }
    return false;
}

static void MarkRing(const VoxelInstance *inst, int kind)
{
    int x, y, n = inst->sceneW * inst->sceneH;
    sRingCarve = calloc(n, 1);
    sRingGround = malloc(n * sizeof(uint16_t));
    memset(sRingGround, 0xFF, n * sizeof(uint16_t));
    if (inst->indoor || inst->scene == NULL)
        return;
    for (y = -VOXEL_SCENE_RING; y < inst->height + VOXEL_SCENE_RING; y++)
        for (x = -VOXEL_SCENE_RING; x < inst->width + VOXEL_SCENE_RING; x++)
        {
            int dtx = x < 0 ? -x : (x >= inst->width ? x - inst->width + 1 : 0);
            int dty = y < 0 ? -y : (y >= inst->height ? y - inst->height + 1 : 0);
            int dt = dtx > dty ? dtx : dty, wx = x + inst->originX, wz = y + inst->originY, limit;
            if (dt == 0)
                continue;
            if (OtherBodyNear(inst, wx, wz, 0))
                continue;     // a connected neighbour draws itself here
            limit = kind == BORDER_WATER ? RING_SEA : RING_DEEP;
            if (dt > limit)
                continue;
            if (dt > RING_ROUND && OtherBodyNear(inst, wx, wz, RING_REACH))
                continue;     // another map's own ring reaches here
            sRingCarve[(y + VOXEL_SCENE_RING) * inst->sceneW + (x + VOXEL_SCENE_RING)] = 1;
        }
}

static bool RingCarved(const VoxelInstance *inst, int x, int y)
{
    int gx = x + VOXEL_SCENE_RING, gy = y + VOXEL_SCENE_RING;
    if (sRingCarve == NULL || gx < 0 || gy < 0 || gx >= inst->sceneW || gy >= inst->sceneH)
        return false;
    return sRingCarve[gy * inst->sceneW + gx] != 0;
}

// The height the map edge nearest a ring cell stands at.
static const VoxelCell *EdgeCell(const VoxelInstance *inst, int x, int y)
{
    int ex = x < 0 ? 0 : (x >= inst->width ? inst->width - 1 : x);
    int ey = y < 0 ? 0 : (y >= inst->height ? inst->height - 1 : y);
    return &inst->cells[ey * inst->width + ex];
}

static void BuildRing(VoxelMesh *mesh, const VoxelInstance *inst, int kind)
{
    int x, y;
    if (sRingCarve == NULL)
        return;
    for (y = -VOXEL_SCENE_RING; y < inst->height + VOXEL_SCENE_RING; y++)
        for (x = -VOXEL_SCENE_RING; x < inst->width + VOXEL_SCENE_RING; x++)
        {
            int gi = (y + VOXEL_SCENE_RING) * inst->sceneW + (x + VOXEL_SCENE_RING);
            const VoxelCell *e;
            float h, wx = (float)(x + inst->originX), wz = (float)(y + inst->originY);
            int m;
            if (!sRingCarve[gi])
                continue;
            e = EdgeCell(inst, x, y);
            if (e->cls == VC_VOID)
                continue;
            m = VoxelScene_MetatileAt(inst, x, y);
            if (m < 0)
                continue;
            if (kind == BORDER_WATER)
            {
                // the sea carries on at the sea's own level
                h = e->cls == VC_WATER ? e->groundH : e->groundH - 0.125f;
                if (e->cls != VC_WATER && e->groundH > 0.0f)
                    h = -0.125f;
                TopQuad(mesh, wx, wz, h, UvFor(m, VOXEL_VAR_COMPOSED), SHADE_TOP);
                continue;
            }
            // THE FOREST FLOOR: a floor at the height of the map edge, the
            // ground the hull standing here matched, else the patch's own art
            h = (e->flags & VCF_BLOCKED) || e->cls == VC_WATER ? e->groundH : e->topH;
            if (e->cls == VC_WATER)
                h = e->groundH + 0.125f;
            if (sRingGround[gi] != 0xFFFF)
                TopQuad(mesh, wx, wz, h, UvFor(sRingGround[gi], VOXEL_VAR_COMPOSED), SHADE_TOP);
            else if (inst->hullGrid && inst->hullGrid[gi] != VH_NONE && inst->floorCount > 0)
                TopQuad(mesh, wx, wz, h, UvFor(inst->floorMetas[0], VOXEL_VAR_COMPOSED), SHADE_TOP);
            else
                TopQuad(mesh, wx, wz, h, UvFor(m, VOXEL_VAR_GROUND), SHADE_TOP);
        }
}

// ---------------------------------------------------------------------------
// Hulls (voxel_hull.h): one template per distinct drawing, stamped wherever
// it stands (Structures.lua buildCylinders).
// ---------------------------------------------------------------------------
static int HullTemplate(VoxelMesh *mesh, const VoxelInstance *inst, int nx, int ny, const int *metas)
{
    int n = (nx / 16) * (ny / 16), i;
    VoxelHullTemplate *t;
    for (i = 0; i < mesh->hullTplCount; i++)
    {
        t = &mesh->hullTpls[i];
        if (t->nx == nx && t->ny == ny && memcmp(t->metas, metas, n * sizeof(int)) == 0)
            return i;
    }
    if (mesh->hullTplCount + 1 > mesh->hullTplCap)
    {
        mesh->hullTplCap = mesh->hullTplCap ? mesh->hullTplCap * 2 : 32;
        mesh->hullTpls = realloc(mesh->hullTpls, mesh->hullTplCap * sizeof(VoxelHullTemplate));
    }
    t = &mesh->hullTpls[mesh->hullTplCount++];
    // a drawing that carves to nothing is remembered too, so it is not
    // carved again for every cell that lays it
    VoxelHull_Build(t, inst, nx, ny, metas, inst->floorMetas, inst->floorCount);
    mesh->stats.hullTemplates++;
    return mesh->hullTplCount - 1;
}

static void AddStamp(VoxelMesh *mesh, int tpl, float x, float y, float z, float halfW, float height)
{
    VoxelHullStamp *s;
    if (mesh->stampCount + 1 > mesh->stampCap)
    {
        mesh->stampCap = mesh->stampCap ? mesh->stampCap * 2 : 64;
        mesh->stamps = realloc(mesh->stamps, mesh->stampCap * sizeof(VoxelHullStamp));
    }
    s = &mesh->stamps[mesh->stampCount++];
    s->tpl = tpl;
    s->x = x; s->y = y; s->z = z;
    s->halfW = halfW;
    s->height = height;
    s->ghost = false;
    mesh->stats.hulls++;
    mesh->stats.hullQuads += mesh->hullTpls[tpl].count / 6;
}

static void SetGround(const VoxelInstance *inst, int x, int y, int meta)
{
    if (x < 0 || y < 0 || x >= inst->width || y >= inst->height || meta < 0)
        return;
    sGroundFor[y * inst->width + x] = (uint16_t)meta;
}

// The height a hull anchored at (x, y) stands at: the anchor cell's foot, or
// for an anchor in the border ring the first body cell of its footprint.
static float HullFoot(const VoxelInstance *inst, int x, int y, int span)
{
    int dx, dy;
    const VoxelCell *e;
    for (dy = 0; dy < span; dy++)
        for (dx = 0; dx < span; dx++)
        {
            int cx = x + dx, cy = y + dy;
            if (cx >= 0 && cy >= 0 && cx < inst->width && cy < inst->height)
                return inst->cells[cy * inst->width + cx].groundH;
        }
    // in the border ring: the map edge it abuts
    {
        int ex = x < 0 ? 0 : (x >= inst->width ? inst->width - 1 : x);
        int ey = y < 0 ? 0 : (y >= inst->height ? inst->height - 1 : y);
        e = &inst->cells[ey * inst->width + ex];
    }
    return (e->flags & VCF_BLOCKED) ? e->groundH : e->topH;
}

static void BuildHulls(VoxelMesh *mesh, const VoxelInstance *inst)
{
    int x, y;
    if (inst->hullGrid == NULL || inst->pixels == NULL)
        return;
    for (y = -VOXEL_SCENE_RING; y < inst->height + VOXEL_SCENE_RING; y++)
        for (x = -VOXEL_SCENE_RING; x < inst->width + VOXEL_SCENE_RING; x++)
        {
            int gi = (y + VOXEL_SCENE_RING) * inst->sceneW + (x + VOXEL_SCENE_RING);
            int code = inst->hullGrid[gi], metas[9], span = 1, nx, ny, tpl, dx, dy;
            float foot;
            if (code == VH_NONE || code == VH_PARTNER)
                continue;
            if (code == VH_CANOPY)
            {
                span = VoxelScene_SpanAt(inst, x, y);
                if (span < 2) span = 2;
                for (dy = 0; dy < span; dy++)
                    for (dx = 0; dx < span; dx++)
                        metas[dy * span + dx] = VoxelScene_MetatileAt(inst, x + dx, y + dy);
                nx = ny = 16 * span;
            }
            else if (code == VH_STACKED)
            {
                metas[0] = VoxelScene_MetatileAt(inst, x, y - 1);
                metas[1] = VoxelScene_MetatileAt(inst, x, y);
                nx = 16; ny = 32;
            }
            else
            {
                metas[0] = VoxelScene_MetatileAt(inst, x, y);
                nx = ny = 16;
            }
            // a hull stands where its footprint reaches the body or the
            // carved border ring
            {
                bool reach = false;
                for (dy = 0; dy < span && !reach; dy++)
                    for (dx = 0; dx < span && !reach; dx++)
                    {
                        int cx = x + dx, cy = y + dy;
                        if (cx >= 0 && cy >= 0 && cx < inst->width && cy < inst->height)
                            reach = true;
                        else if (RingCarved(inst, cx, cy))
                            reach = true;
                    }
                if (!reach)
                    continue;
            }
            tpl = HullTemplate(mesh, inst, nx, ny, metas);
            if (mesh->hullTpls[tpl].count == 0)
                continue;
            foot = HullFoot(inst, x, y, span);
            AddStamp(mesh, tpl, (float)(x + inst->originX) + span * 0.5f, foot,
                     (float)(y + inst->originY) + span * 0.5f, span * 0.5f, ny / 16.0f);
            for (dy = 0; dy < span; dy++)
                for (dx = 0; dx < span; dx++)
                {
                    int gx = x + dx + VOXEL_SCENE_RING, gy = y + dy + VOXEL_SCENE_RING;
                    SetGround(inst, x + dx, y + dy, mesh->hullTpls[tpl].bgMeta);
                    if (sRingGround && gx >= 0 && gy >= 0 && gx < inst->sceneW && gy < inst->sceneH
                        && mesh->hullTpls[tpl].bgMeta >= 0)
                        sRingGround[gy * inst->sceneW + gx] = (uint16_t)mesh->hullTpls[tpl].bgMeta;
                }
            if (code == VH_STACKED && y - 1 >= 0 && x >= 0 && x < inst->width
                && (inst->cells[(y - 1) * inst->width + x].flags & VCF_VACATED))
                SetGround(inst, x, y - 1, mesh->hullTpls[tpl].bgMeta);
        }
}

// ---------------------------------------------------------------------------
// Signs (Structures.lua extractObjects + buildObject, forced `signpost`
// pool): the drawing cut out per pixel and stood up two voxels thick, one
// drawn row to one pixel of height, in the depth band of the tile row its
// foot is drawn in. The cell under it paints its neighbours' floor.
// ---------------------------------------------------------------------------
#define SIGN_DEPTH 2
#define OBJ_FRONT  255
#define OBJ_BACK   173
#define OBJ_SIDE   199
#define OBJ_TOP    255
#define OBJ_BOTTOM 140

// The floor a vacated cell paints: the commonest walkable, non-water
// metatile beside it, a ring at a time out to three cells, else the map's floor.
static int VoteGround(const VoxelInstance *inst, int x, int y)
{
    int r;
    for (r = 1; r <= 3; r++)
    {
        static const int ddx[4] = {1, -1, 0, 0};
        static const int ddy[4] = {0, 0, 1, -1};
        int metas[4], counts[4], n = 0, i, d, best = -1, bestN = 0;
        for (d = 0; d < 4; d++)
        {
            int nx = x + ddx[d] * r, ny = y + ddy[d] * r;
            const VoxelCell *nb;
            if (nx < 0 || ny < 0 || nx >= inst->width || ny >= inst->height)
                continue;
            nb = &inst->cells[ny * inst->width + nx];
            if ((nb->flags & (VCF_BLOCKED | VCF_VACATED)) || nb->cls == VC_VOID || nb->cls == VC_WATER
                || nb->shape == SH_MAT || nb->shape == SH_DECK || nb->shape == SH_LIP)
                continue;
            for (i = 0; i < n; i++)
                if (metas[i] == nb->metatile) { counts[i]++; break; }
            if (i == n) { metas[n] = nb->metatile; counts[n] = 1; n++; }
        }
        for (i = 0; i < n; i++)
            if (counts[i] > bestN) { bestN = counts[i]; best = metas[i]; }
        if (best >= 0)
            return best;
    }
    return inst->floorCount > 0 ? inst->floorMetas[0] : -1;
}

// A quad with per-corner shades from each corner's height over the foot
// (ChunkMesher groundShades: the floor blocks half the sky near it).
static void ObjQuad(VoxelMesh *mesh, const float *p, const float *uv, int shade, float foot)
{
    static const int order[6] = {0, 1, 2, 0, 2, 3};
    VoxelVertex *v = Reserve(mesh, 6);
    int k;
    for (k = 0; k < 6; k++)
    {
        int i = order[k];
        float h = (p[i * 3 + 1] - foot) * 16.0f, sh = (float)shade;
        if (h < 6.0f)
            sh *= 1.0f - 0.288f * (1.0f - (h < 0 ? 0 : h) / 6.0f);
        Vtx(&v[k], p[i * 3], p[i * 3 + 1], p[i * 3 + 2], uv[i * 2], uv[i * 2 + 1], (int)sh, 255);
    }
    mesh->count += 6;
    mesh->stats.signQuads++;
}

static void BuildSign(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    int m = c->metatile, px, py, i;
    uint8_t fig[256], air[18 * 18], comp[256];
    int lowY[64], nComp = 0;
    int stack[18 * 18], sp = 0;
    float ox = (float)(lx + inst->originX), oz = (float)(ly + inst->originY), foot = c->groundH;
    VoxelMeshRange r;

    // opaque pixels are the figure; transparent ones the flood from the
    // ring cannot reach are holes in it, textured like the rest
    memset(air, 0, sizeof(air));
    for (py = -1; py <= 16; py++)
        for (px = -1; px <= 16; px++)
            if (px < 0 || py < 0 || px > 15 || py > 15)
            {
                air[(py + 1) * 18 + px + 1] = 1;
                stack[sp++] = (py + 1) * 18 + px + 1;
            }
    while (sp > 0)
    {
        int k = stack[--sp], kx = k % 18, ky = k / 18, d;
        static const int ddx[4] = {1, -1, 0, 0};
        static const int ddy[4] = {0, 0, 1, -1};
        for (d = 0; d < 4; d++)
        {
            int nx = kx + ddx[d], ny = ky + ddy[d], j;
            if (nx < 0 || ny < 0 || nx >= 18 || ny >= 18)
                continue;
            j = ny * 18 + nx;
            if (air[j])
                continue;
            if (nx >= 1 && ny >= 1 && nx <= 16 && ny <= 16 && CarvedAlpha(inst, m, nx - 1, ny - 1))
                continue;
            air[j] = 1;
            stack[sp++] = j;
        }
    }
    for (py = 0; py < 16; py++)
        for (px = 0; px < 16; px++)
            fig[py * 16 + px] = !air[(py + 1) * 18 + px + 1];
    // 8-connected components, each standing in the depth band of the tile
    // row its lowest pixel is drawn in
    memset(comp, 0xFF, sizeof(comp));
    for (i = 0; i < 256; i++)
    {
        int s2 = 0, st[256];
        if (!fig[i] || comp[i] != 0xFF || nComp >= 64)
            continue;
        comp[i] = (uint8_t)nComp;
        lowY[nComp] = i / 16;
        st[s2++] = i;
        while (s2 > 0)
        {
            int k = st[--s2], kx = k % 16, ky = k / 16, ddx, ddy;
            if (ky > lowY[nComp]) lowY[nComp] = ky;
            for (ddy = -1; ddy <= 1; ddy++)
                for (ddx = -1; ddx <= 1; ddx++)
                {
                    int nx = kx + ddx, ny = ky + ddy, j;
                    if (nx < 0 || ny < 0 || nx > 15 || ny > 15)
                        continue;
                    j = ny * 16 + nx;
                    if (fig[j] && comp[j] == 0xFF)
                    {
                        comp[j] = (uint8_t)nComp;
                        st[s2++] = j;
                    }
                }
        }
        nComp++;
    }
    if (nComp == 0)
        return;
    mesh->stats.signs++;
    BeginRange(mesh, &r, (int)ox, (int)oz, -1, foot, foot + 1.0f);

#define FIG(x, y) ((x) >= 0 && (y) >= 0 && (x) < 16 && (y) < 16 && fig[(y) * 16 + (x)])
    for (py = 0; py < 16; py++)
    {
        px = 0;
        while (px < 16)
        {
            int k = py * 16 + px, px2 = px, cc;
            float x0, x1, y0, y1, z0, z1, u0, u1, v0, v1, ua, va;
            if (!fig[k]) { px++; continue; }
            cc = comp[k];
            while (px2 + 1 < 16 && fig[k + (px2 + 1 - px)] && comp[k + (px2 + 1 - px)] == cc)
                px2++;
            z0 = oz + (float)((lowY[cc] / 8) * 8 + (8 - SIGN_DEPTH) / 2) / 16.0f;
            z1 = z0 + SIGN_DEPTH / 16.0f;
            y0 = foot + (float)(lowY[cc] - py) / 16.0f;
            y1 = y0 + 1.0f / 16.0f;
            x0 = ox + px / 16.0f;
            x1 = ox + (px2 + 1) / 16.0f;
            UvPixel(m, VOXEL_VAR_COMPOSED, px, py, &u0, &v0);
            UvPixel(m, VOXEL_VAR_COMPOSED, px2, py, &u1, &v1);
            {
                // the front and back of the run, the drawing across it
                float du = 0.45f * ATLAS_TEXEL;
                float q[12] = { x0, y0, z1,  x1, y0, z1,  x1, y1, z1,  x0, y1, z1 };
                float uv[8] = { u0 - du, v0 + du,  u1 + du, v0 + du,  u1 + du, v0 - du,  u0 - du, v0 - du };
                float qb[12] = { x1, y0, z0,  x0, y0, z0,  x0, y1, z0,  x1, y1, z0 };
                float uvb[8] = { u1 + du, v0 + du,  u0 - du, v0 + du,  u0 - du, v0 - du,  u1 + du, v0 - du };
                ObjQuad(mesh, q, uv, OBJ_FRONT, foot);
                ObjQuad(mesh, qb, uvb, OBJ_BACK, foot);
            }
            // lids, undersides and ends: one texel each
            for (i = px; i <= px2; i++)
            {
                float xa = ox + i / 16.0f, xb = xa + 1.0f / 16.0f;
                UvPixel(m, VOXEL_VAR_COMPOSED, i, py, &ua, &va);
                {
                    float fl[8] = { ua, va, ua, va, ua, va, ua, va };
                    if (!FIG(i, py - 1))
                    {
                        float q[12] = { xa, y1, z0,  xb, y1, z0,  xb, y1, z1,  xa, y1, z1 };
                        ObjQuad(mesh, q, fl, OBJ_TOP, foot);
                    }
                    if (lowY[cc] - py > 0 && !FIG(i, py + 1))
                    {
                        float q[12] = { xa, y0, z1,  xb, y0, z1,  xb, y0, z0,  xa, y0, z0 };
                        ObjQuad(mesh, q, fl, OBJ_BOTTOM, foot);
                    }
                    if (!FIG(i - 1, py))
                    {
                        float q[12] = { xa, y0, z0,  xa, y0, z1,  xa, y1, z1,  xa, y1, z0 };
                        ObjQuad(mesh, q, fl, OBJ_SIDE, foot);
                    }
                    if (!FIG(i + 1, py))
                    {
                        float q[12] = { xb, y0, z1,  xb, y0, z0,  xb, y1, z0,  xb, y1, z1 };
                        ObjQuad(mesh, q, fl, OBJ_SIDE, foot);
                    }
                }
            }
            px = px2 + 1;
        }
    }
#undef FIG
    EndRange(mesh, &r);
}

// ---------------------------------------------------------------------------
// Palings (Structures.lua buildGen3Palings emitPost/emitRail): every face
// maps one drawn pixel onto one world pixel. A post's four sides wear the
// elevation's drawn rows (row y1 its foot), its lid the plan's drawn head; a
// rail is centred in the post's depth -- the drawing shows the posts
// occluding it -- and wears the elevation's own columns along its span.
// ---------------------------------------------------------------------------
#define PAL_TOP   255
#define PAL_SOUTH 255
#define PAL_NORTH 173
#define PAL_SIDE  199

// The padded texel rect of columns c0..c1, rows r0..r1 of metatile m.
static void PalUv(int m, int c0, int c1, int r0, int r1, float *uL, float *uR, float *vT, float *vB)
{
    float u0, v0, u1, v1, d = 0.45f * ATLAS_TEXEL;
    UvPixel(m, VOXEL_VAR_COMPOSED, c0, r0, &u0, &v0);
    UvPixel(m, VOXEL_VAR_COMPOSED, c1, r1, &u1, &v1);
    *uL = u0 - d; *uR = u1 + d; *vT = v0 - d; *vB = v1 + d;
}

// A quad from four corners and their (u, v), foot-shaded like any object.
static void PalQuad(VoxelMesh *mesh, float x0, float y0, float z0, float x1, float y1, float z1,
                    float x2, float y2, float z2, float x3, float y3, float z3,
                    float ua, float va, float ub, float vb, float uc, float vc, float ud, float vd,
                    int shade, float foot)
{
    float q[12] = { x0, y0, z0, x1, y1, z1, x2, y2, z2, x3, y3, z3 };
    float uv[8] = { ua, va, ub, vb, uc, vc, ud, vd };
    ObjQuad(mesh, q, uv, shade, foot);
}

static void PalPost(VoxelMesh *mesh, const VoxelPalingFigure *f, float X0, float Z0, float by)
{
    float X1 = X0 + f->postW / 16.0f, Z1 = Z0 + f->postD / 16.0f, yT = by + f->postH / 16.0f;
    float uL, uR, vT, vB, uD, dummy;
    // the lid: the plan's drawn post head
    PalUv(f->topMeta, f->topX, f->topX + f->postW - 1, f->topZ, f->topZ + f->postD - 1, &uL, &uR, &vT, &vB);
    PalQuad(mesh, X0, yT, Z0, X1, yT, Z0, X1, yT, Z1, X0, yT, Z1, uL, vT, uR, vT, uR, vB, uL, vB, PAL_TOP, by);
    // the sides: the elevation's drawn rows, y1 at the foot
    PalUv(f->faceMeta, f->faceX, f->faceX + f->postW - 1, f->faceY0, f->faceY1, &uL, &uR, &vT, &vB);
    PalUv(f->faceMeta, f->faceX, f->faceX + f->postD - 1, f->faceY0, f->faceY1, &dummy, &uD, &dummy, &dummy);
    PalQuad(mesh, X0, by, Z1, X1, by, Z1, X1, yT, Z1, X0, yT, Z1, uL, vB, uR, vB, uR, vT, uL, vT, PAL_SOUTH, by);
    PalQuad(mesh, X1, by, Z0, X0, by, Z0, X0, yT, Z0, X1, yT, Z0, uL, vB, uR, vB, uR, vT, uL, vT, PAL_NORTH, by);
    PalQuad(mesh, X0, by, Z0, X0, by, Z1, X0, yT, Z1, X0, yT, Z0, uL, vB, uD, vB, uD, vT, uL, vT, PAL_SIDE, by);
    PalQuad(mesh, X1, by, Z1, X1, by, Z0, X1, yT, Z0, X1, yT, Z1, uL, vB, uD, vB, uD, vT, uL, vT, PAL_SIDE, by);
}

static void PalRail(VoxelMesh *mesh, const VoxelPalingFigure *f, const VoxelPalingSpan *sp, float ox, float oz, float by)
{
    int i;
    if (sp->to <= sp->from)
        return;
    for (i = 0; i < f->nRails; i++)
    {
        const VoxelPalingRail *r = &f->rails[i];
        int depth = r->top0 >= 0 ? r->top1 - r->top0 + 1 : r->depth;
        int ta = r->top0 >= 0 ? r->top0 : r->face0, tb = r->top0 >= 0 ? r->top1 : r->face1;
        int c0 = sp->lane + (f->postD - depth) / 2, c1 = c0 + depth;
        float yLo = by + (f->faceY1 - r->face1) / 16.0f, yHi = by + (f->faceY1 - r->face0 + 1) / 16.0f;
        float uL, uR, vT, vB, tT, tB, dummy;
        PalUv(f->faceMeta, sp->from, sp->to - 1, r->face0, r->face1, &uL, &uR, &vT, &vB);
        PalUv(f->faceMeta, sp->from, sp->to - 1, ta, tb, &dummy, &dummy, &tT, &tB);
        if (sp->axis == 'x')
        {
            float X0 = ox + sp->from / 16.0f, X1 = ox + sp->to / 16.0f, Z0 = oz + c0 / 16.0f, Z1 = oz + c1 / 16.0f;
            PalQuad(mesh, X0, yLo, Z1, X1, yLo, Z1, X1, yHi, Z1, X0, yHi, Z1, uL, vB, uR, vB, uR, vT, uL, vT, PAL_SOUTH, by);
            PalQuad(mesh, X1, yLo, Z0, X0, yLo, Z0, X0, yHi, Z0, X1, yHi, Z0, uL, vB, uR, vB, uR, vT, uL, vT, PAL_NORTH, by);
            PalQuad(mesh, X0, yHi, Z0, X1, yHi, Z0, X1, yHi, Z1, X0, yHi, Z1, uL, tT, uR, tT, uR, tB, uL, tB, PAL_TOP, by);
        }
        else
        {
            float Z0 = oz + sp->from / 16.0f, Z1 = oz + sp->to / 16.0f, X0 = ox + c0 / 16.0f, X1 = ox + c1 / 16.0f;
            PalQuad(mesh, X1, yLo, Z1, X1, yLo, Z0, X1, yHi, Z0, X1, yHi, Z1, uL, vB, uR, vB, uR, vT, uL, vT, PAL_SIDE, by);
            PalQuad(mesh, X0, yLo, Z0, X0, yLo, Z1, X0, yHi, Z1, X0, yHi, Z0, uL, vB, uR, vB, uR, vT, uL, vT, PAL_SIDE, by);
            PalQuad(mesh, X0, yHi, Z0, X1, yHi, Z0, X1, yHi, Z1, X0, yHi, Z1, uL, tT, uL, tB, uR, tB, uR, tT, PAL_TOP, by);
        }
    }
}

static void BuildPaling(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    const VoxelPalingFigure *f = NULL;
    const VoxelPalingPiece *p = VoxelPaling_Find(inst->header->mapLayout, c->metatile, &f);
    float ox = (float)(lx + inst->originX), oz = (float)(ly + inst->originY), foot = c->groundH;
    VoxelMeshRange r;
    int i;
    if (p == NULL || f == NULL)
        return;
    mesh->stats.signs++;
    BeginRange(mesh, &r, (int)ox, (int)oz, -1, foot, foot + 1.0f);
    for (i = 0; i < p->nPosts; i++)
        PalPost(mesh, f, ox + p->posts[i].x / 16.0f, oz + p->posts[i].z / 16.0f, foot);
    for (i = 0; i < p->nSpans; i++)
        PalRail(mesh, f, &p->spans[i], ox, oz, foot);
    EndRange(mesh, &r);
}

// ---------------------------------------------------------------------------
// Instance build
// ---------------------------------------------------------------------------
// Two tree anchors side by side whose four metatiles are the consecutive
// quarters of one 2x2 drawing (468/469 over 476/477 in Littleroot) are one
// tree: a single round crown over one trunk, the phase read off the feet.
static bool TreePairAt(const VoxelInstance *inst, int lx, int ly, const bool *done)
{
    const VoxelCell *a = &inst->cells[ly * inst->width + lx];
    const VoxelCell *e = (lx + 1 < inst->width) ? &inst->cells[ly * inst->width + lx + 1] : NULL;
    const VoxelCell *aN = Nb(inst, lx, ly - 1);
    const VoxelCell *eN = (lx + 1 < inst->width) ? Nb(inst, lx + 1, ly - 1) : NULL;
    if (e == NULL || aN == NULL || eN == NULL)
        return false;
    if (done[ly * inst->width + lx + 1])
        return false;
    if (e->cls != VC_WALL || e->shape != SH_TREE || e->stack != a->stack || e->building >= 0)
        return false;
    // Emerald's tree quarters come in even/odd pairs: the left half is the
    // even id. Either row may carry an edge variant, so one matching row
    // with an even west half names the tree.
    if (!((e->metatile == a->metatile + 1 && (a->metatile & 1) == 0)
          || (eN->metatile == aN->metatile + 1 && (aN->metatile & 1) == 0)
          || (e->metatile == a->metatile && eN->metatile == aN->metatile)))   // a wall of one repeated quarter
        return false;
    if (fabsf(e->groundH - a->groundH) > 0.01f || fabsf(e->topH - a->topH) > 0.01f)
        return false;
    return true;
}

void VoxelMesh_BuildInstance(VoxelMesh *mesh, const VoxelInstance *inst)
{
    int x, y, b;
    bool *done;
    VoxelMesh_Clear(mesh);
    if (inst->cells == NULL)
        return;
    done = calloc(inst->width * inst->height, sizeof(bool));
    sGroundFor = malloc(inst->width * inst->height * sizeof(uint16_t));
    memset(sGroundFor, 0xFF, inst->width * inst->height * sizeof(uint16_t));
    {
        int kind = BorderKind(inst);
        MarkRing(inst, kind);
        BuildHulls(mesh, inst);
        BuildRing(mesh, inst, kind);
        free(sRingCarve);
        free(sRingGround);
        sRingCarve = NULL;
        sRingGround = NULL;
    }
    for (y = 0; y < inst->height; y++)
        for (x = 0; x < inst->width; x++)
        {
            const VoxelCell *c = &inst->cells[y * inst->width + x];
            if (c->shape == SH_SIGN || c->shape == SH_PALING)
                sGroundFor[y * inst->width + x] = (uint16_t)VoteGround(inst, x, y);
        }

    for (y = 0; y < inst->height; y++)
        for (x = 0; x < inst->width; x++)
        {
            const VoxelCell *c = &inst->cells[y * inst->width + x];
            if (c->cls == VC_VOID)
                continue;
            if (c->building >= 0 && ((c->flags & VCF_BLOCKED) || c->cls == VC_DOOR))
                continue;   // the building draws it; a walkable cell under its eave keeps its ground
            if ((c->cls == VC_WALL || c->cls == VC_CLIFF) && c->shape != SH_NONE && c->shape != SH_SUPPORT
                && c->shape != SH_HULL && c->shape != SH_SIGN && c->shape != SH_PALING)
                continue;   // columns below
            if ((c->cls == VC_WALL || c->cls == VC_CLIFF) && (c->flags & VCF_CONSUMED))
                continue;
            if (c->cls >= VC_COUNTER && c->cls < VC_COUNT && (c->flags & VCF_BLOCKED))
                continue;   // furniture below
            BuildSurface(mesh, inst, x, y);
        }
    for (y = inst->height - 1; y >= 0; y--)
        for (x = 0; x < inst->width; x++)
        {
            const VoxelCell *c = &inst->cells[y * inst->width + x];
            if (c->building >= 0 || done[y * inst->width + x])
                continue;
            if ((c->cls == VC_WALL || c->cls == VC_CLIFF) && c->stack > 0)
            {
                switch (c->shape)
                {
                case SH_TREE:
                    if (c->stack >= 2 && TreePairAt(inst, x, y, done))
                    {
                        done[y * inst->width + x + 1] = true;
                        BuildTree(mesh, inst, x, y, true);
                    }
                    else
                        BuildTree(mesh, inst, x, y, false);
                    break;
                case SH_BUSH: BuildBush(mesh, inst, x, y); break;
                case SH_STANDEE: case SH_STANDEE_TALL: BuildStandee(mesh, inst, x, y); break;
                case SH_STANDEE_BOX: BuildStandeeBox(mesh, inst, x, y); break;
                case SH_NONE: case SH_SUPPORT: break;   // drawn as surfaces above
                default: BuildColumn(mesh, inst, x, y); break;
                }
            }
            else if (c->cls >= VC_COUNTER && c->cls < VC_COUNT && (c->flags & VCF_BLOCKED))
                BuildFurniture(mesh, inst, x, y);
        }
    for (b = 0; b < inst->buildingCount; b++)
        BuildBuilding(mesh, inst, b);
    for (y = 0; y < inst->height; y++)
        for (x = 0; x < inst->width; x++)
            if (inst->cells[y * inst->width + x].shape == SH_SIGN)
                BuildSign(mesh, inst, x, y);
            else if (inst->cells[y * inst->width + x].shape == SH_PALING)
                BuildPaling(mesh, inst, x, y);
    free(done);
    free(sGroundFor);
    sGroundFor = NULL;
    mesh->vboDirty = true;

    if (getenv("EMERALD_VOXEL_DUMP") && inst == &gVoxelInstances[0])
        printf("[Voxel] mesh %d:%d verts %d ranges %d trees %d (pairs %d) bushes %d standees %d/%d/%d boxes %d lips %d mats %d canopies %d decks %d pillars %d\n",
               inst->mapGroup, inst->mapNum, mesh->count, mesh->rangeCount, mesh->stats.trees, mesh->stats.treePairs,
               mesh->stats.bushes, mesh->stats.standees, mesh->stats.standeeBoxes, mesh->stats.standeeTall, mesh->stats.boxes,
               mesh->stats.lips, mesh->stats.mats, mesh->stats.canopies, mesh->stats.decks, mesh->stats.pillars);
}

void VoxelMesh_WriteStatsJson(FILE *f, const VoxelMesh *mesh)
{
    const VoxelMeshStats *s = &mesh->stats;
    fprintf(f, "{\"hulls\":%d,\"hullTemplates\":%d,\"hullQuads\":%d,\"signs\":%d,\"signQuads\":%d,",
            s->hulls, s->hullTemplates, s->hullQuads, s->signs, s->signQuads);
    fprintf(f, "\"verts\":%d,\"ranges\":%d,\"trees\":%d,\"treePairs\":%d,\"bushes\":%d,\"standees\":%d,\"standeeBoxes\":%d,"
               "\"standeeTall\":%d,\"boxes\":%d,\"cliffs\":%d,\"flats\":%d,\"supports\":%d,\"lips\":%d,\"lipFaces\":%d,"
               "\"mats\":%d,\"matSkirts\":%d,\"canopies\":%d,\"decks\":%d,\"deckFascias\":%d,\"pillars\":%d,\"furniture\":%d,\"buildings\":%d,"
               "\"gables\":%d,\"eaves\":%d,\"contactQuads\":%d",
            mesh->count, mesh->rangeCount, s->trees, s->treePairs, s->bushes, s->standees, s->standeeBoxes, s->standeeTall,
            s->boxes, s->cliffs, s->flats, s->supports, s->lips, s->lipFaces, s->mats, s->matSkirts, s->canopies, s->decks,
            s->deckFascias, s->pillars, s->furniture, s->buildings, s->gables, s->eaves, s->contactQuads);
    {
        int i;
        fprintf(f, ",\"contacts\":[");
        for (i = 0; i < mesh->contactCount; i++)
        {
            const VoxelContactRecord *cr = &mesh->contacts[i];
            fprintf(f, "%s{\"x\":%d,\"z\":%d,\"c\":[%d,%d,%d,%d]}", i ? "," : "", cr->x, cr->z,
                    cr->corner[0], cr->corner[1], cr->corner[2], cr->corner[3]);
        }
        fprintf(f, "]");
    }
    {
        int i;
        fprintf(f, ",\"roofs\":[");
        for (i = 0; i < mesh->roofCount; i++)
        {
            const VoxelRoofRecord *rr = &mesh->roofs[i];
            fprintf(f, "%s{\"building\":%d,\"peak\":%.3f,\"frontSlopes\":%d,\"backSlopes\":%d,\"frontRows\":%d,\"gables\":%d,\"eave\":%.2f}",
                    i ? "," : "", rr->building, rr->peak, rr->frontSlopes, rr->backSlopes, rr->frontRows, rr->gables, rr->eave);
        }
        fprintf(f, "]");
    }
    // the first hulls' measured profiles
    {
        int i, k;
        fprintf(f, ",\"profiles\":[");
        for (i = 0; i < mesh->profileCount; i++)
        {
            const VoxelTreeProfile *pr = &mesh->profiles[i];
            fprintf(f, "%s{\"rows\":%d,\"pair\":%s,\"m\":[", i ? "," : "", pr->rows, pr->pair ? "true" : "false");
            for (k = 0; k < pr->rows && k < 4; k++) fprintf(f, "%s%d", k ? "," : "", pr->metas[k]);
            fprintf(f, "],\"widths\":[");
            for (k = 0; k < pr->rings; k++) fprintf(f, "%s%d", k ? "," : "", pr->width[k]);
            fprintf(f, "],\"radii\":[");
            for (k = 0; k < pr->rings; k++) fprintf(f, "%s%.3f", k ? "," : "", pr->radius[k]);
            fprintf(f, "]}");
        }
        fprintf(f, "]}");
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
            dz = northZ - (pz + 0.5f);
            if (dz >= -0.5f && dz < 6.0f && px >= (float)r->worldX - 0.3f
                && px <= (float)(r->worldX + r->cols) + 0.3f)
            {
                rayY = py + 0.9f + (dz > 0.0f ? dz : 0.0f) * camTanPitch;
                if (r->top > rayY)
                    ghost = true;
            }
        }
        else
        {
            // columns, trees, plates: the mass's near edge is its northmost
            // row; sight the ray from the sprite's midriff so a knee-high
            // fence stays put while a hedge that swallows the body fades
            float cx = (float)r->worldX + 0.5f * r->cols;
            float northZ = (float)r->worldZ - (float)r->rows + 1.0f;
            float reach = 1.6f + 0.5f * (r->cols - 1);
            dz = northZ - (pz + 0.5f);
            // a cell either side as well, so the opening reads as a gap in
            // the hedge rather than a slot cut for the player
            // the cell directly south counts too: a crown centred on the
            // row boundary overhangs the walker's feet -- but there the
            // sight line starts above a one-course fence or rock, which
            // only reaches the walker's waist and stays put
            if (fabsf(cx - px) <= reach && dz >= -0.5f && dz < 4.0f)
            {
                rayY = dz < 0.0f ? py + 1.05f : py + 0.9f + dz * camTanPitch;
                if (r->top > rayY)
                    ghost = true;
            }
        }
        mesh->ghost[i] = ghost;
    }
    // hulls: like a column, the mass's near edge is its northmost row
    for (i = 0; i < mesh->stampCount; i++)
    {
        VoxelHullStamp *s = &mesh->stamps[i];
        float northZ = s->z - s->halfW, dz = northZ - (pz + 0.5f), reach = 1.1f + s->halfW, rayY;
        s->ghost = false;
        if (gVoxelDebugFlags & 2u)
            continue;
        if (fabsf(s->x - px) <= reach && dz >= -0.5f - s->halfW && dz < 4.0f)
        {
            rayY = dz < 0.0f ? py + 1.05f : py + 0.9f + dz * camTanPitch;
            if (s->y + s->height > rayY && s->z > pz + 0.2f)
                s->ghost = true;
        }
    }
}

// Hull stamps: each template drawn where it stands, translated. `ghosted`
// picks the pass; stamps further than `radius` from (cx, cz) are skipped.
void VoxelMesh_DrawHulls(VoxelMesh *mesh, bool ghosted, float cx, float cz, float radius)
{
    int i, bound = -1;
    if (mesh->stampCount == 0)
        return;
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    if (!ghosted)
        glEnableClientState(GL_COLOR_ARRAY);
    for (i = 0; i < mesh->stampCount; i++)
    {
        const VoxelHullStamp *s = &mesh->stamps[i];
        VoxelHullTemplate *t = &mesh->hullTpls[s->tpl];
        if (s->ghost != ghosted || t->count == 0)
            continue;
        if (fabsf(s->x - cx) > radius || fabsf(s->z - cz) > radius)
            continue;
        if (s->tpl != bound)
        {
            if (t->vbo == 0)
            {
                glGenBuffers(1, (GLuint *)&t->vbo);
                glBindBuffer(GL_ARRAY_BUFFER, t->vbo);
                glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)t->count * sizeof(VoxelHullVertex), t->verts, GL_STATIC_DRAW);
            }
            else
                glBindBuffer(GL_ARRAY_BUFFER, t->vbo);
            glVertexPointer(3, GL_FLOAT, sizeof(VoxelHullVertex), (const void *)0);
            glTexCoordPointer(2, GL_FLOAT, sizeof(VoxelHullVertex), (const void *)(3 * sizeof(float)));
            if (!ghosted)
                glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(VoxelHullVertex), (const void *)(5 * sizeof(float)));
            bound = s->tpl;
        }
        glPushMatrix();
        glTranslatef(s->x, s->y, s->z);
        glDrawArrays(GL_TRIANGLES, 0, t->count);
        glPopMatrix();
    }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void BindMesh(VoxelMesh *mesh)
{
    if (mesh->vbo == 0)
    {
        glGenBuffers(1, (GLuint *)&mesh->vbo);
        mesh->vboDirty = true;
    }
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
    if (mesh->vboDirty)
    {
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)mesh->count * sizeof(VoxelVertex), mesh->verts, GL_STATIC_DRAW);
        mesh->vboDirty = false;
    }
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(VoxelVertex), (const void *)0);
    glTexCoordPointer(2, GL_FLOAT, sizeof(VoxelVertex), (const void *)(3 * sizeof(float)));
}

static void UnbindMesh(void)
{
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void VoxelMesh_DrawOpaque(VoxelMesh *mesh)
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
    BindMesh(mesh);
    glEnableClientState(GL_COLOR_ARRAY);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(VoxelVertex), (const void *)(5 * sizeof(float)));
    // carved silhouettes and canopy plates leave their background at alpha 0
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
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
    glDisable(GL_ALPHA_TEST);
    UnbindMesh();
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);
#ifndef __ANDROID__
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
#endif
}

void VoxelMesh_DrawGhosted(VoxelMesh *mesh)
{
    int i;
    bool any = false;
    if (getenv("EMERALD_VOXEL_NOGHOST"))
        return;
    for (i = 0; i < mesh->rangeCount; i++)
        if (mesh->ghost[i]) { any = true; break; }
    if (!any || mesh->count == 0)
        return;
    BindMesh(mesh);
    glDisableClientState(GL_COLOR_ARRAY);
    glColor4f(0.85f, 0.85f, 0.85f, 0.38f);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.1f);
    for (i = 0; i < mesh->rangeCount; i++)
    {
        const VoxelMeshRange *r = &mesh->ranges[i];
        if (mesh->ghost[i])
            glDrawArrays(GL_TRIANGLES, r->first, r->count);
    }
    glDisable(GL_ALPHA_TEST);
    UnbindMesh();
}

#endif
#endif
