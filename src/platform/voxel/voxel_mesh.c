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

void VoxelMesh_Free(VoxelMesh *mesh)
{
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
static void GroundFascias(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly, float top, int variant)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    UvRect uv = UvFor(c->metatile, variant);
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
    UvRect skirt = UvRegion(m, VOXEL_VAR_COMPOSED, 0.0f, 16.0f, 7.0f, 9.0f);   // a plain band, not the tuft bottoms
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
        if (d == 0) SouthFace(mesh, x, z, nh, top, skirt, SHADE_SOUTH - 30);
        else if (d == 1) NorthFace(mesh, x, z, nh, top, skirt, SHADE_NORTH);
        else if (d == 2) EastFace(mesh, x, z, nh, top, skirt, SHADE_EAST);
        else WestFace(mesh, x, z, nh, top, skirt, SHADE_WEST);
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
static void BuildSurface(VoxelMesh *mesh, const VoxelInstance *inst, int lx, int ly)
{
    const VoxelCell *c = &inst->cells[ly * inst->width + lx];
    float x = (float)(lx + inst->originX);
    float z = (float)(ly + inst->originY);
    float top = c->topH;
    int variant = c->shape == SH_SUPPORT ? VOXEL_VAR_GROUND : VOXEL_VAR_COMPOSED;

    if (c->shape == SH_LIP) { BuildLedge(mesh, inst, lx, ly); return; }
    if (c->shape == SH_MAT) { BuildMat(mesh, inst, lx, ly); return; }
    if (c->shape == SH_DECK) { BuildDeck(mesh, inst, lx, ly); return; }

    if (c->shape == SH_SUPPORT) mesh->stats.supports++;
    else if (c->shape == SH_NONE && (c->flags & VCF_BLOCKED)) mesh->stats.flats++;
    {
        int sh[4];
        if (ContactShades(inst, lx, ly, top, sh))
        {
            TopQuadShaded(mesh, x, z, top, UvFor(c->metatile, variant), sh);
            NoteContact(mesh, x, z, sh);
        }
        else
            TopQuad(mesh, x, z, top, UvFor(c->metatile, variant), SHADE_TOP);
    }
    GroundFascias(mesh, inst, lx, ly, top, variant);
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
// Buildings: a vertical facade of the wall rows, a hipped roof -- a front
// slope from an eave past the facade up to a ridge at mid-depth wearing the
// drawn roof rows, a back slope down to the back wall -- gables closing the
// open flanks, textured flanks and back.
// ---------------------------------------------------------------------------
#define ROOF_EAVE 0.25f

static void BuildBuilding(VoxelMesh *mesh, const VoxelInstance *inst, int b)
{
    const VoxelBuilding *bd = &inst->buildings[b];
    int K = bd->wallRows;
    float rise = bd->roofRows > 0 ? 1.0f : 0.0f;
    float base = bd->baseH;
    float wallTop = base + K;
    int col, k;
    VoxelMeshRange r;
    VoxelRoofRecord rec;
    float zFront = (float)(bd->facadeRow + inst->originY) + 1.0f;

    mesh->stats.buildings++;
    memset(&rec, 0, sizeof(rec));
    rec.building = b;
    rec.peak = wallTop + rise;
    rec.eave = rise > 0.0f ? ROOF_EAVE : 0.0f;
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
        float colRise = roofRows > 0 ? rise : 0.0f;
        float colPeak = colWallTop + colRise;
        float zEave = zFront + (colRise > 0.0f ? ROOF_EAVE : 0.0f);
        float zRidge = zFront - (depth >= 2.0f ? depth * 0.5f : 0.5f);
        int topRoofMeta = Nb(inst, col, top)->metatile;
        int i;

        // facade: rows facadeRow .. facadeRow-wallRows+1, bottom up
        for (k = 0; k < wallRows; k++)
        {
            const VoxelCell *cellK = Nb(inst, col, bd->facadeRow - k);
            SouthFace(mesh, x, zFront - 1.0f, base + k, base + k + 1, UvFor(cellK->metatile, VOXEL_VAR_COMPOSED), SHADE_SOUTH);
        }

        if (colRise > 0.0f)
        {
            // front slope: the drawn roof rows from the eave (lowest roof
            // row, its art's bottom at the eave) up to the ridge
            float len = zEave - zRidge;
            if (roofRows > rec.frontRows) rec.frontRows = roofRows;
            for (i = 0; i < roofRows; i++)
            {
                int rowAt = bd->facadeRow - wallRows - i;      // eave row first
                UvRect uv = UvFor(Nb(inst, col, rowAt)->metatile, VOXEL_VAR_COMPOSED);
                float t0 = (float)i / roofRows, t1 = (float)(i + 1) / roofRows;
                float z0 = zEave - len * t0, z1 = zEave - len * t1;
                float y0 = colWallTop + colRise * t0, y1 = colWallTop + colRise * t1;
                Quad(mesh,
                     x,        y1, z1, uv.u0, uv.v0,
                     x + 1.0f, y1, z1, uv.u1, uv.v0,
                     x + 1.0f, y0, z0, uv.u1, uv.v1,
                     x,        y0, z0, uv.u0, uv.v1,
                     SHADE_TOP);
                rec.frontSlopes++;
            }
            // back slope: the ridge row's art laid down to the back wall
            if (zRidge > zBack + 0.01f)
            {
                UvRect uv = UvFor(topRoofMeta, VOXEL_VAR_COMPOSED);
                Quad(mesh,
                     x,        colWallTop, zBack,  uv.u0, uv.v0,
                     x + 1.0f, colWallTop, zBack,  uv.u1, uv.v0,
                     x + 1.0f, colPeak,    zRidge, uv.u1, uv.v1,
                     x,        colPeak,    zRidge, uv.u0, uv.v1,
                     SHADE_TOP - 40);
                rec.backSlopes++;
            }
            // the eave's underside, so the overhang is a slab and not a sheet
            Quad(mesh,
                 x,        colWallTop - 0.08f, zFront, 0.0f, 0.0f,
                 x + 1.0f, colWallTop - 0.08f, zFront, 0.0f, 0.0f,
                 x + 1.0f, colWallTop - 0.08f, zEave,  0.0f, 0.0f,
                 x,        colWallTop - 0.08f, zEave,  0.0f, 0.0f,
                 SHADE_NORTH);
            {
                VoxelVertex *v = &mesh->verts[mesh->count - 6];
                UvRect uv = UvSlice(topRoofMeta, VOXEL_VAR_COMPOSED, 0.9f, 1.0f);
                for (i = 0; i < 6; i++) { v[i].u = uv.u0; v[i].v = uv.v1; }
            }
            SouthFace(mesh, x, zEave - 1.0f, colWallTop - 0.08f, colWallTop + 0.02f, UvSlice(topRoofMeta, VOXEL_VAR_COMPOSED, 0.85f, 1.0f), SHADE_SOUTH - 30);
            mesh->stats.eaves++;
        }
        else
        {
            // no roof rows drawn: a flat lid of the top row
            TopQuad(mesh, x, zBack, colWallTop, UvFor(topRoofMeta, VOXEL_VAR_COMPOSED), SHADE_TOP);
            for (i = 1; i < (int)depth; i++)
                TopQuad(mesh, x, zBack + i, colWallTop, UvFor(topRoofMeta, VOXEL_VAR_COMPOSED), SHADE_TOP);
        }

        // back face (north): the wall up to the roof's back edge
        {
            const VoxelCell *n = Nb(inst, col, top - 1);
            float nh = SideHeight(n);
            const VoxelCell *edge = Nb(inst, col, bd->facadeRow - (wallRows - 1));
            if (nh < colWallTop - 0.01f)
            {
                float lo = nh > base ? nh : base;
                NorthFace(mesh, x, zBack, lo, colWallTop, UvFor(edge->metatile, VOXEL_VAR_COMPOSED), SHADE_NORTH);
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
                const VoxelCell *side = Nb(inst, col, bd->facadeRow);
                int wallRowMeta = side->metatile;
                UvRect gableUv = UvSlice(topRoofMeta, VOXEL_VAR_COMPOSED, 0.3f, 0.7f);
                float zz;
                for (zz = zBack; zz < zFront - 0.01f; zz += 1.0f)
                {
                    int rowHere = (int)(zz - inst->originY);
                    const VoxelCell *nb;
                    float nh, lo;
                    if (westOpen)
                    {
                        nb = Nb(inst, col - 1, rowHere);
                        nh = SideHeight(nb);
                        if (nb && nb->building == b) nh = 1000.0f;
                        if (nb && IsOpenColumn(nb)) nh = nb->groundH;
                        lo = nh > base ? nh : base;
                        if (lo < colWallTop - 0.01f)
                            WestFace(mesh, x, zz, lo, colWallTop, UvSlice(wallRowMeta, VOXEL_VAR_COMPOSED, 0.0f, 1.0f), SHADE_WEST);
                    }
                    if (eastOpen)
                    {
                        nb = Nb(inst, col + 1, rowHere);
                        nh = SideHeight(nb);
                        if (nb && nb->building == b) nh = 1000.0f;
                        if (nb && IsOpenColumn(nb)) nh = nb->groundH;
                        lo = nh > base ? nh : base;
                        if (lo < colWallTop - 0.01f)
                            EastFace(mesh, x, zz, lo, colWallTop, UvSlice(wallRowMeta, VOXEL_VAR_COMPOSED, 0.0f, 1.0f), SHADE_EAST);
                    }
                }
                // the gable: a triangle from the eave over the ridge to the back
                if (colRise > 0.0f)
                {
                    if (westOpen)
                    {
                        Tri(mesh,
                            x, colWallTop, zEave,  gableUv.u0, gableUv.v1,
                            x, colPeak,    zRidge, gableUv.u1, gableUv.v0,
                            x, colWallTop, zBack,  gableUv.u1, gableUv.v1,
                            SHADE_WEST);
                        rec.gables++; mesh->stats.gables++;
                    }
                    if (eastOpen)
                    {
                        Tri(mesh,
                            x + 1.0f, colWallTop, zBack,  gableUv.u0, gableUv.v1,
                            x + 1.0f, colPeak,    zRidge, gableUv.u1, gableUv.v0,
                            x + 1.0f, colWallTop, zEave,  gableUv.u1, gableUv.v1,
                            SHADE_EAST);
                        rec.gables++; mesh->stats.gables++;
                    }
                }
            }
        }
    }
    EndRange(mesh, &r);
    if (rise > 0.0f && mesh->roofCount < VOXEL_ROOF_KEEP)
        mesh->roofs[mesh->roofCount++] = rec;
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

    for (y = 0; y < inst->height; y++)
        for (x = 0; x < inst->width; x++)
        {
            const VoxelCell *c = &inst->cells[y * inst->width + x];
            if (c->cls == VC_VOID)
                continue;
            if (c->building >= 0 && ((c->flags & VCF_BLOCKED) || c->cls == VC_DOOR))
                continue;   // the building draws it; a walkable cell under its eave keeps its ground
            if ((c->cls == VC_WALL || c->cls == VC_CLIFF) && c->shape != SH_NONE && c->shape != SH_SUPPORT)
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
    free(done);
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
    fprintf(f, "{\"verts\":%d,\"ranges\":%d,\"trees\":%d,\"treePairs\":%d,\"bushes\":%d,\"standees\":%d,\"standeeBoxes\":%d,"
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
