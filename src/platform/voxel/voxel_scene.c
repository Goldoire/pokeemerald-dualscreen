#include "voxel_scene.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "fieldmap.h"
#include "voxel_world.h"
#include "voxel_paling.h"
#include "voxel_roles.h"
#include "voxel_atlas.h"

#define MB_MOUNTAIN_TOP     0x0C
#define MB_BERRY_TREE_SOIL  0xA0

#define RING VOXEL_SCENE_RING

typedef struct {
    VoxelInstance *inst;
    int x0, y0, x1, y1;       // grid bounds in map cells (inclusive)
    int stride;
    uint8_t *claimed;
} Ctx;

static int Idx(const Ctx *c, int cx, int cy)
{
    return (cy - c->y0) * c->stride + (cx - c->x0);
}

static bool InGrid(const Ctx *c, int cx, int cy)
{
    return cx >= c->x0 && cy >= c->y0 && cx <= c->x1 && cy <= c->y1;
}

static bool OffMap(const VoxelInstance *inst, int cx, int cy)
{
    return cx < 0 || cy < 0 || cx >= inst->width || cy >= inst->height;
}

int VoxelScene_MetatileAt(const VoxelInstance *inst, int cx, int cy)
{
    const struct MapLayout *layout = inst->header->mapLayout;
    if (OffMap(inst, cx, cy))
    {
        // THE BORDER PATCH TILES; IT DOES NOT SMEAR.
        if (layout->border == NULL)
            return -1;
        return layout->border[(((cy % 2) + 2) % 2) * 2 + (((cx % 2) + 2) % 2)] & MAPGRID_METATILE_ID_MASK;
    }
    return inst->cells[cy * inst->width + cx].metatile;
}

// Off the map reads as solid: you cannot walk off the edge of Hoenn.
static bool Blocked(const VoxelInstance *inst, int cx, int cy)
{
    if (OffMap(inst, cx, cy))
        return true;
    return (inst->cells[cy * inst->width + cx].flags & VCF_BLOCKED) != 0;
}

static bool Authored(const VoxelInstance *inst, int m)
{
    bool walkablePin;
    return m >= 0 && VoxelWorld_PinFor(inst->header->mapLayout, m, &walkablePin) != VC_COUNT;
}

static const VoxelRole *RoleOf(const VoxelInstance *inst, int m)
{
    const struct MapLayout *layout = inst->header->mapLayout;
    if (m < 0)
        return NULL;
    return VoxelRoles_Find(layout->primaryTileset, layout->secondaryTileset, m);
}

static const VoxelArtInfo *ArtOf(const VoxelInstance *inst, int m)
{
    if (m < 0 || m >= 1024 || !inst->used[m])
        return NULL;
    return &inst->art[m];
}

static uint8_t BehaviorOf(const VoxelInstance *inst, int m)
{
    return (m >= 0 && m < 1024) ? inst->meta[m].behavior : 0;
}

// ---- foliage (Gen3.lua leafAt / crownAt / crownTop / footAt) ----
static bool LeafAt(const Ctx *c, int cx, int cy)
{
    const VoxelInstance *inst = c->inst;
    const VoxelArtInfo *st;
    const VoxelRole *role;
    int m;
    if (!InGrid(c, cx, cy))
        return false;
    if (!OffMap(inst, cx, cy) && !Blocked(inst, cx, cy))
        return false;
    m = VoxelScene_MetatileAt(inst, cx, cy);
    if (m < 0 || Authored(inst, m))
        return false;
    st = ArtOf(inst, m);
    if (!(st && st->leafy && st->solid > 0.5f))
        return false;
    // ...AND HOENN DRAWS EVERY LEAF IN GREEN (ash reads as leafy texture).
    role = RoleOf(inst, m);
    if (role && role->material != ROLE_MAT_GREEN)
        return false;
    return true;
}

static bool CrownAt(const Ctx *c, int cx, int cy)
{
    const VoxelInstance *inst = c->inst;
    const VoxelArtInfo *st;
    const VoxelRole *role;
    int m;
    if (!InGrid(c, cx, cy))
        return false;
    m = VoxelScene_MetatileAt(inst, cx, cy);
    if (m < 0 || Authored(inst, m))
        return false;
    st = ArtOf(inst, m);
    if (!(st && st->leafy))
        return false;
    if (!(st->leafFrac2 > 0.5f && st->leafFrac2 >= st->leafFrac * 0.9f))
        return false;
    role = RoleOf(inst, m);
    return role && role->material == ROLE_MAT_GREEN;
}

static bool CrownTop(const Ctx *c, int cx, int cy)
{
    if (!CrownAt(c, cx, cy))
        return false;
    return !(LeafAt(c, cx, cy - 1) || CrownAt(c, cx, cy - 1));
}

static bool FootAt(const Ctx *c, int cx, int cy)
{
    const VoxelInstance *inst = c->inst;
    const VoxelArtInfo *st;
    const VoxelRole *role;
    int m;
    if (!InGrid(c, cx, cy))
        return false;
    if (!OffMap(inst, cx, cy) && !Blocked(inst, cx, cy))
        return false;
    m = VoxelScene_MetatileAt(inst, cx, cy);
    if (m < 0 || Authored(inst, m))
        return false;
    st = ArtOf(inst, m);
    if (!st)
        return false;
    if (!(st->leafFrac > 0.25f && st->leafFrac2 <= st->leafFrac * 0.1f))
        return false;
    role = RoleOf(inst, m);
    return role && role->material == ROLE_MAT_GREEN;
}

// ---- rock (rocky / lump / tapers / onLand) ----
static bool Rocky(const Ctx *c, int cx, int cy)
{
    const VoxelInstance *inst = c->inst;
    const VoxelArtInfo *st;
    int m;
    if (!InGrid(c, cx, cy))
        return false;
    if (inst->scene[Idx(c, cx, cy)])
        return false;
    if (OffMap(inst, cx, cy) || !Blocked(inst, cx, cy))
        return false;
    m = VoxelScene_MetatileAt(inst, cx, cy);
    if (m < 0 || Authored(inst, m))
        return false;
    st = ArtOf(inst, m);
    if (!(st && st->solid > 0.5f))
        return false;
    if (st->overhead)
        return false;
    if (LeafAt(c, cx, cy))
        return false;
    return true;
}

// The size of the rocky lump this cell belongs to, or -1 past LUMP_MAX.
#define LUMP_MAX 9
static int Lump(const Ctx *c, int cx, int cy)
{
    int qx[LUMP_MAX + 8], qy[LUMP_MAX + 8], n = 1, head = 0, i;
    static const int dx[4] = {1, -1, 0, 0};
    static const int dy[4] = {0, 0, 1, -1};
    qx[0] = cx; qy[0] = cy;
    while (head < n)
    {
        int x = qx[head], y = qy[head], d;
        head++;
        if (n > LUMP_MAX)
            return -1;
        for (d = 0; d < 4; d++)
        {
            int nx = x + dx[d], ny = y + dy[d];
            bool seen = false;
            for (i = 0; i < n; i++)
                if (qx[i] == nx && qy[i] == ny) { seen = true; break; }
            if (!seen && Rocky(c, nx, ny))
            {
                if (n >= LUMP_MAX + 4)
                    return -1;
                qx[n] = nx; qy[n] = ny; n++;
            }
        }
    }
    return n;
}

// A LATHE IS NOT A MODELLER: a drawing whose rows are all one width is not round.
static bool Tapers(const VoxelInstance *inst, int m)
{
    const VoxelArtInfo *a = ArtOf(inst, m);
    int y, wmin = 99, wmax = -1;
    if (!a)
        return false;
    for (y = 0; y < 16; y++)
    {
        int x, lo = 99, hi = -1;
        int ax = (m % VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE;
        int ay = (m / VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + VOXEL_ATLAS_QUAD;
        for (x = 0; x < 16; x++)
            if (inst->pixels[(ay + y) * VOXEL_ATLAS_SIZE + ax + x] >> 24)
            {
                if (x < lo) lo = x;
                if (x > hi) hi = x;
            }
        if (hi >= 0)
        {
            int w = hi - lo + 1;
            if (w < wmin) wmin = w;
            if (w > wmax) wmax = w;
        }
    }
    return wmax >= 0 && (wmax - wmin) > 1;
}

static bool OnLand(const Ctx *c, int cx, int cy)
{
    static const int dx[4] = {1, -1, 0, 0};
    static const int dy[4] = {0, 0, 1, -1};
    int d;
    for (d = 0; d < 4; d++)
    {
        int nx = cx + dx[d], ny = cy + dy[d];
        if (!OffMap(c->inst, nx, ny) && !Blocked(c->inst, nx, ny))
            if (c->inst->cells[ny * c->inst->width + nx].cls != VC_WATER)
                return true;
    }
    return false;
}

// FURNITURE IS A DISCRETE OBJECT; A WALL IS A RUN: the blocked run a cell
// belongs to, counted indoors only (outdoors a boulder is routinely part of
// a big blocked mass).
static int BlockedRun(const Ctx *c, int cx, int cy)
{
    const VoxelInstance *inst = c->inst;
    int w = inst->width, h = inst->height, n = 0, head = 0;
    int *q;
    uint8_t *seen;
    if (!inst->indoor || OffMap(inst, cx, cy) || !Blocked(inst, cx, cy))
        return 0;
    q = malloc(sizeof(int) * w * h);
    seen = calloc(w * h, 1);
    q[n++] = cy * w + cx;
    seen[cy * w + cx] = 1;
    while (head < n)
    {
        int k = q[head++], x = k % w, y = k / w, d;
        static const int dx[4] = {1, -1, 0, 0};
        static const int dy[4] = {0, 0, 1, -1};
        for (d = 0; d < 4; d++)
        {
            int nx = x + dx[d], ny = y + dy[d];
            if (nx >= 0 && ny >= 0 && nx < w && ny < h && !seen[ny * w + nx] && Blocked(inst, nx, ny))
            {
                seen[ny * w + nx] = 1;
                q[n++] = ny * w + nx;
            }
        }
    }
    free(q);
    free(seen);
    return n;
}

// A SIGN IS A BOARD ON A POST: one blocked cell with open ground east, west
// and south (the north may be the building the sign names, drawn in a
// different metatile).
static bool Lone(const Ctx *c, int cx, int cy)
{
    const VoxelInstance *inst = c->inst;
    const VoxelArtInfo *st;
    int m, d, sides = 0;
    if (!InGrid(c, cx, cy) || inst->scene[Idx(c, cx, cy)])
        return false;
    if (OffMap(inst, cx, cy) || !Blocked(inst, cx, cy))
        return false;
    m = VoxelScene_MetatileAt(inst, cx, cy);
    if (m < 0 || Authored(inst, m))
        return false;
    st = ArtOf(inst, m);
    if (!st || st->leafy || st->overhead || st->overhang)
        return false;
    if (st->solid < 0.4f)
        return false;
    if (!OffMap(inst, cx, cy + 1) && Blocked(inst, cx, cy + 1))
        return false;
    // Relaxed past the reference on the east and west: a mailbox standing
    // against the house it belongs to (Littleroot's (7,8) and (12,8)) is still
    // a board on a post when ONE flank is a different drawing -- the mod
    // leaves those boxed. A facade sign (the Center's and Mart's) is walled on
    // both flanks and stays part of its building.
    for (d = -1; d <= 1; d += 2)
    {
        int nx = cx + d;
        if (!OffMap(inst, nx, cy) && Blocked(inst, nx, cy))
        {
            int mn = VoxelScene_MetatileAt(inst, nx, cy);
            if (mn < 0 || mn == m)
                return false;
            sides++;
        }
    }
    if (sides > 1)
        return false;
    if (!OffMap(inst, cx, cy - 1) && Blocked(inst, cx, cy - 1))
    {
        int mn = VoxelScene_MetatileAt(inst, cx, cy - 1);
        if (mn < 0 || mn == m)
            return false;
    }
    return true;
}

// A RAIL STANDS ON GROUND: surface art with a cap and no face.
static bool RailStands(const VoxelInstance *inst, int m)
{
    const VoxelRole *role;
    if (BehaviorOf(inst, m) == MB_MOUNTAIN_TOP)
        return false;
    role = RoleOf(inst, m);
    if (!role)
        return false;
    return role->cap >= 1 && role->face <= 1;
}

// The ROM names a behaviour sea (gen3_shapes.lua `behaviour`).
static bool SeaBehavior(uint8_t b)
{
    return (b >= 0x10 && b <= 0x1A && b != 0x13) || b == 0x22 || b == 0x28 || b == 0x2A || b == 0x2B
        || (b >= 0x50 && b <= 0x53);
}

// ...or names it something in particular: a ledge, a slope, a bridge, a
// wall, a counter. A rock is plain ground, grass, or nothing named.
static bool NamedOther(uint8_t b)
{
    if (SeaBehavior(b) || b == 0x13)
        return true;
    if ((b >= 0x38 && b <= 0x3F) || b == 0xD0 || b == 0xD1)
        return true;
    if ((b >= 0x70 && b <= 0x7F && b != 0x79 && b != 0x7E) || (b >= 0xD3 && b <= 0xD6))
        return true;
    return b == 0x60 || b == 0x61 || b == 0x69 || b == 0x6C || b == 0x85 || (b >= 0x8B && b <= 0x8D)
        || b == 0xBE || b == 0xEA || b == 0x80 || b == 0x83 || b == 0x84;
}

// Off the map is sea: outside the body there is no answer, so take none.
static bool SeaCell(const Ctx *c, int cx, int cy)
{
    const VoxelInstance *inst = c->inst;
    int m;
    if (cx < 0 || cy < 0 || cx >= inst->width || cy >= inst->height)
        return true;
    m = VoxelScene_MetatileAt(inst, cx, cy);
    return m >= 0 && SeaBehavior(BehaviorOf(inst, m));
}

static bool RockCell(const Ctx *c, int cx, int cy)
{
    const VoxelInstance *inst = c->inst;
    const VoxelArtInfo *st;
    int m;
    if (cx < 0 || cy < 0 || cx >= inst->width || cy >= inst->height || !Blocked(inst, cx, cy))
        return false;
    m = VoxelScene_MetatileAt(inst, cx, cy);
    if (m < 0 || Authored(inst, m) || NamedOther(BehaviorOf(inst, m)))
        return false;
    // drawn, and not foliage: a bush in a pond is still a bush
    st = ArtOf(inst, m);
    return st && st->solid > 0.4f && !st->leafy && !st->overhead;
}

void VoxelScene_Classify(VoxelInstance *inst)
{
    Ctx c;
    int cx, cy, n;
    uint8_t *scene, *span, *shared;

    c.inst = inst;
    c.x0 = -RING; c.y0 = -RING;
    c.x1 = inst->width + RING - 1; c.y1 = inst->height + RING - 1;
    c.stride = c.x1 - c.x0 + 1;
    n = c.stride * (c.y1 - c.y0 + 1);
    free(inst->scene); free(inst->sceneSpan); free(inst->sceneShared);
    inst->scene = scene = calloc(n, 1);
    inst->sceneSpan = span = calloc(n, 1);
    inst->sceneShared = shared = calloc(n, 1);
    inst->sceneW = c.stride;
    inst->sceneH = c.y1 - c.y0 + 1;
    c.claimed = calloc(n, 1);
    if (!scene || !span || !shared || !c.claimed || inst->pixels == NULL)
    {
        free(c.claimed);
        return;
    }

    // ---- palings: an authored fence claims its cells first, before a
    // cylinder can lathe its end pieces or a carve stand its arms (a piece on
    // a passable cell is decoration, and is left alone) ----
    for (cy = 0; cy < inst->height; cy++)
        for (cx = 0; cx < inst->width; cx++)
        {
            int m = VoxelScene_MetatileAt(inst, cx, cy);
            if (m >= 0 && Blocked(inst, cx, cy) && VoxelPaling_Find(inst->header->mapLayout, m, NULL))
            {
                c.claimed[Idx(&c, cx, cy)] = 1;
                scene[Idx(&c, cx, cy)] = SC_PALING;
            }
        }

    // ---- trees: row-major greedy from the top-left of every mass ----
    for (cy = c.y0; cy <= c.y1; cy++)
        for (cx = c.x0; cx <= c.x1; cx++)
        {
            int i = Idx(&c, cx, cy);
            bool anchorTop, nine = false, motif, widen, four;
            int mA, mB, mC;
            if (c.claimed[i] || !(LeafAt(&c, cx, cy) || CrownTop(&c, cx, cy)))
                continue;
            anchorTop = CrownTop(&c, cx, cy);
            // ...AND HOENN DRAWS TREES AT TWO SIZES: two rows of crown over
            // one row of foot, nine different metatiles (Mossdeep).
            if (anchorTop && cx + 2 <= c.x1 && cy + 2 <= c.y1)
            {
                int seenM[9], ns = 0, dx, dy, k;
                nine = true;
                for (dy = 0; dy < 3 && nine; dy++)
                    for (dx = 0; dx < 3 && nine; dx++)
                    {
                        int mm = VoxelScene_MetatileAt(inst, cx + dx, cy + dy);
                        if (c.claimed[Idx(&c, cx + dx, cy + dy)]) nine = false;
                        for (k = 0; k < ns; k++)
                            if (seenM[k] == mm) nine = false;
                        if (mm < 0) nine = false;
                        seenM[ns++] = mm;
                        if (dy < 2) { if (!CrownAt(&c, cx + dx, cy + dy)) nine = false; }
                        else if (!FootAt(&c, cx + dx, cy + dy)) nine = false;
                    }
            }
            if (nine)
            {
                int dx, dy;
                for (dy = 0; dy < 3; dy++)
                    for (dx = 0; dx < 3; dx++)
                    {
                        c.claimed[Idx(&c, cx + dx, cy + dy)] = 1;
                        scene[Idx(&c, cx + dx, cy + dy)] = SC_CYLINDER;
                    }
                scene[i] = SC_CANOPY;
                span[i] = 3;
                continue;
            }
            // AND THE 2x2 HAS TO BE A 2x2 DRAWING: four different quarters.
            mA = VoxelScene_MetatileAt(inst, cx, cy);
            mB = VoxelScene_MetatileAt(inst, cx + 1, cy);
            mC = VoxelScene_MetatileAt(inst, cx, cy + 1);
            motif = mA >= 0 && mB >= 0 && mC >= 0 && mA != mB && mA != mC;
            widen = anchorTop && (FootAt(&c, cx, cy + 1) || FootAt(&c, cx + 1, cy + 1));
#define QUARTER(qx, qy) (LeafAt(&c, qx, qy) || (widen && CrownAt(&c, qx, qy)))
            four = motif && cx < c.x1 && cy < c.y1
                && (LeafAt(&c, cx, cy) || widen)
                && QUARTER(cx + 1, cy) && QUARTER(cx, cy + 1) && QUARTER(cx + 1, cy + 1)
                && !c.claimed[Idx(&c, cx + 1, cy)] && !c.claimed[Idx(&c, cx, cy + 1)]
                && !c.claimed[Idx(&c, cx + 1, cy + 1)];
#undef QUARTER
            if (four)
            {
                c.claimed[i] = c.claimed[Idx(&c, cx + 1, cy)] = 1;
                c.claimed[Idx(&c, cx, cy + 1)] = c.claimed[Idx(&c, cx + 1, cy + 1)] = 1;
                scene[i] = SC_CANOPY;
                scene[Idx(&c, cx + 1, cy)] = SC_CYLINDER;
                scene[Idx(&c, cx, cy + 1)] = SC_CYLINDER;
                scene[Idx(&c, cx + 1, cy + 1)] = SC_CYLINDER;
                span[i] = 2;
            }
            else if (LeafAt(&c, cx, cy))
            {
                // A LONE CROWN IS NOT AN OBJECT; a blocked leaf is a hull.
                c.claimed[i] = 1;
                scene[i] = SC_CYLINDER;
            }
        }

    // ...AND TWO TREES MAY STAND IN THE SAME CELL (Mossdeep): a whole tree,
    // phased off its feet, over cells another claim already took.
    for (cy = c.y0; cy < c.y1; cy++)
        for (cx = c.x0; cx < c.x1; cx++)
        {
            int i = Idx(&c, cx, cy), ie = Idx(&c, cx + 1, cy), is = Idx(&c, cx, cy + 1), id = Idx(&c, cx + 1, cy + 1);
            bool whole = scene[i] != SC_CANOPY && scene[ie] != SC_CANOPY && scene[is] != SC_CANOPY && scene[id] != SC_CANOPY
                      && CrownAt(&c, cx, cy) && CrownAt(&c, cx + 1, cy) && FootAt(&c, cx, cy + 1) && FootAt(&c, cx + 1, cy + 1);
            if (whole)
            {
                int ms[4], k, j;
                ms[0] = VoxelScene_MetatileAt(inst, cx, cy);
                ms[1] = VoxelScene_MetatileAt(inst, cx + 1, cy);
                ms[2] = VoxelScene_MetatileAt(inst, cx, cy + 1);
                ms[3] = VoxelScene_MetatileAt(inst, cx + 1, cy + 1);
                for (k = 0; k < 4; k++)
                    for (j = k + 1; j < 4; j++)
                        if (ms[k] == ms[j] || ms[k] < 0) whole = false;
            }
            if (whole)
            {
                int rs = cx, re = cx + 1;
                while (FootAt(&c, rs - 1, cy + 1)) rs--;
                while (FootAt(&c, re + 1, cy + 1)) re++;
                if ((re - rs + 1) % 2 != 0 || (cx - rs) % 2 != 0)
                    whole = false;
            }
            if (whole && (c.claimed[i] || c.claimed[ie] || c.claimed[is] || c.claimed[id]))
            {
                c.claimed[i] = c.claimed[ie] = c.claimed[is] = c.claimed[id] = 1;
                scene[i] = SC_CANOPY;
                if (scene[ie] != SC_CANOPY) scene[ie] = SC_CYLINDER;
                if (scene[is] != SC_CANOPY) scene[is] = SC_CYLINDER;
                if (scene[id] != SC_CANOPY) scene[id] = SC_CYLINDER;
                span[i] = 2;
                shared[i] = 1;
            }
        }

    // ---- A BOULDER IS A ROCK STANDING IN A FIELD, NOT A PIECE OF CLIFF ----
    for (cy = c.y0; cy <= c.y1; cy++)
        for (cx = c.x0; cx <= c.x1; cx++)
        {
            int i = Idx(&c, cx, cy), lump, mA, mB2, mC2;
            const VoxelRole *rA, *rB;
            bool four;
            if (c.claimed[i] || !Rocky(&c, cx, cy))
                continue;
            lump = Lump(&c, cx, cy);
            if (lump < 0)
                continue;
            mA = VoxelScene_MetatileAt(inst, cx, cy);
            mB2 = VoxelScene_MetatileAt(inst, cx + 1, cy);
            mC2 = VoxelScene_MetatileAt(inst, cx, cy + 1);
            rA = RoleOf(inst, mA);
            rB = RoleOf(inst, mB2);
            four = mA >= 0 && mB2 >= 0 && mC2 >= 0 && mA != mB2 && mA != mC2
                && cx < c.x1 && cy < c.y1
                && rA && rA->cap > 0 && rB && rB->cap > 0
                && (!inst->indoor || BlockedRun(&c, cx, cy) > 8)
                && Rocky(&c, cx + 1, cy) && Rocky(&c, cx, cy + 1) && Rocky(&c, cx + 1, cy + 1)
                && !c.claimed[Idx(&c, cx + 1, cy)] && !c.claimed[Idx(&c, cx, cy + 1)]
                && !c.claimed[Idx(&c, cx + 1, cy + 1)]
                && lump <= 6 && OnLand(&c, cx, cy);
            if (four)
            {
                c.claimed[i] = c.claimed[Idx(&c, cx + 1, cy)] = 1;
                c.claimed[Idx(&c, cx, cy + 1)] = c.claimed[Idx(&c, cx + 1, cy + 1)] = 1;
                scene[i] = SC_CANOPY;
                scene[Idx(&c, cx + 1, cy)] = SC_CYLINDER;
                scene[Idx(&c, cx, cy + 1)] = SC_CYLINDER;
                scene[Idx(&c, cx + 1, cy + 1)] = SC_CYLINDER;
                span[i] = 2;
            }
            else if (rA && rA->cap > 0 && lump <= 4 && OnLand(&c, cx, cy) && Tapers(inst, mA)
                     && BlockedRun(&c, cx, cy) <= 8)
            {
                c.claimed[i] = 1;
                scene[i] = SC_CYLINDER;
            }
        }

    // ---- A ROCK STANDING IN THE SEA (Gen3.lua buildScenery waterRocks):
    // Emerald draws one as a 2x2 block -- a boulder with the water disturbed
    // round it -- and blocks all four cells. Read as collision it is a
    // two-cell wall with the ripple painted on its face; what is there is
    // flat sea with a rock in it. The test is the surroundings: most of the
    // ring is sea, and anything else in it is more rock. ----
    if (!inst->indoor)
        for (cy = 0; cy + 1 < inst->height; cy++)
            for (cx = 0; cx + 1 < inst->width; cx++)
            {
                static const int8_t ring[12][2] = {
                    {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {-1, 0}, {2, 0},
                    {-1, 1}, {2, 1}, {-1, 2}, {0, 2}, {1, 2}, {2, 2},
                };
                int k, sea = 0, dx, dy;
                bool ringed = true;
                for (dy = 0; dy < 2 && ringed; dy++)
                    for (dx = 0; dx < 2 && ringed; dx++)
                        if (!RockCell(&c, cx + dx, cy + dy) || c.claimed[Idx(&c, cx + dx, cy + dy)])
                            ringed = false;
                if (!ringed)
                    continue;
                for (k = 0; k < 12; k++)
                {
                    int nx = cx + ring[k][0], ny = cy + ring[k][1];
                    if (SeaCell(&c, nx, ny))
                        sea++;
                    else if (!RockCell(&c, nx, ny))
                    {
                        ringed = false;
                        break;
                    }
                }
                if (!ringed || sea < 7)
                    continue;
                for (dy = 0; dy < 2; dy++)
                    for (dx = 0; dx < 2; dx++)
                    {
                        c.claimed[Idx(&c, cx + dx, cy + dy)] = 1;
                        scene[Idx(&c, cx + dx, cy + dy)] = SC_SEAROCK;
                    }
                span[Idx(&c, cx, cy)] = 2;
            }

    // ---- signs and rails: outdoors only ----
    if (!inst->indoor)
    {
        for (cy = 0; cy < inst->height; cy++)
            for (cx = 0; cx < inst->width; cx++)
                if (!scene[Idx(&c, cx, cy)] && Lone(&c, cx, cy))
                    scene[Idx(&c, cx, cy)] = SC_SIGNPOST;
        for (cy = 0; cy < inst->height; cy++)
            for (cx = 0; cx < inst->width; cx++)
            {
                int i = Idx(&c, cx, cy), m;
                const VoxelArtInfo *st;
                if (scene[i] || !Blocked(inst, cx, cy))
                    continue;
                m = VoxelScene_MetatileAt(inst, cx, cy);
                if (m < 0 || Authored(inst, m))
                    continue;
                st = ArtOf(inst, m);
                if (st && st->solid > 0.06f && st->solid < 0.35f && !st->leafy && !st->overhead && !st->overhang
                    && RailStands(inst, m))
                    scene[i] = SC_FENCE;
            }
    }

    // A ledge or a berry plot is named by the cartridge; the carve does not
    // get to overrule it.
    for (cy = 0; cy < inst->height; cy++)
        for (cx = 0; cx < inst->width; cx++)
        {
            int i = Idx(&c, cx, cy);
            uint8_t b = inst->cells[cy * inst->width + cx].behavior;
            if (scene[i] && scene[i] != SC_PALING && ((b >= 0x38 && b <= 0x3F) || b == MB_BERRY_TREE_SOIL))
                scene[i] = SC_NONE;
        }
    free(c.claimed);
}

int VoxelScene_At(const VoxelInstance *inst, int cx, int cy)
{
    int gx = cx + RING, gy = cy + RING;
    if (inst->scene == NULL || gx < 0 || gy < 0 || gx >= inst->sceneW || gy >= inst->sceneH)
        return SC_NONE;
    return inst->scene[gy * inst->sceneW + gx];
}

int VoxelScene_SpanAt(const VoxelInstance *inst, int cx, int cy)
{
    int gx = cx + RING, gy = cy + RING;
    if (inst->sceneSpan == NULL || gx < 0 || gy < 0 || gx >= inst->sceneW || gy >= inst->sceneH)
        return 0;
    return inst->sceneSpan[gy * inst->sceneW + gx];
}

bool VoxelScene_SharedAt(const VoxelInstance *inst, int cx, int cy)
{
    int gx = cx + RING, gy = cy + RING;
    if (inst->sceneShared == NULL || gx < 0 || gy < 0 || gx >= inst->sceneW || gy >= inst->sceneH)
        return false;
    return inst->sceneShared[gy * inst->sceneW + gx] != 0;
}

#endif
#endif
