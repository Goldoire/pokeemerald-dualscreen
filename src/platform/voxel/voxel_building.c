#include "voxel_building.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "fieldmap.h"
#include "voxel_world.h"
#include "voxel_scene.h"
#include "voxel_roles.h"

// How rare a metatile has to be to count as a building's own art rather than
// the landscape it backs onto: a building repeats a motif across its own
// frontage, landscape tiles in the tens and hundreds.
#define RARE_BUILD 4
// the deepest recess Emerald draws over a door (the shipyard's porch)
#define LINTEL_MAX 2
// a walk-behind band across the middle of a building (Trainer Hill)
#define WALK_BEHIND_GAP 1

typedef struct { int16_t x, y; } Cell;

typedef struct {
    VoxelInstance *inst;
    int w, h;
    uint16_t counts[1024];   // how many times each metatile is laid on the map
    int16_t *owner;          // per cell: the building that claimed it, or -1
    uint8_t *warp;           // per cell: a warp event lands here
} Ctx;

static bool InMap(const Ctx *b, int cx, int cy)
{
    return cx >= 0 && cy >= 0 && cx < b->w && cy < b->h;
}

// blocked, and inside the map (the column walks do not leave the body)
static bool BlockedCell(const Ctx *b, int cx, int cy)
{
    return InMap(b, cx, cy) && (b->inst->cells[cy * b->w + cx].flags & VCF_BLOCKED);
}

// blocked, off the map counting as solid
static bool BlockedAt(const Ctx *b, int cx, int cy)
{
    return !InMap(b, cx, cy) || (b->inst->cells[cy * b->w + cx].flags & VCF_BLOCKED);
}

static int Meta(const Ctx *b, int cx, int cy)
{
    return VoxelScene_MetatileAt(b->inst, cx, cy);
}

static const VoxelArtInfo *Art(const Ctx *b, int m)
{
    if (m < 0 || m >= 1024 || !b->inst->used[m])
        return NULL;
    return &b->inst->art[m];
}

static bool Rare(const Ctx *b, int m)
{
    return m >= 0 && m < 1024 && b->counts[m] <= RARE_BUILD;
}

static bool Outdoor(const Ctx *b)
{
    return !b->inst->indoor;
}

static bool Authored(const Ctx *b, int m)
{
    bool walkablePin;
    return m >= 0 && VoxelWorld_PinFor(b->inst->header->mapLayout, m, &walkablePin) != VC_COUNT;
}

static const VoxelRole *Role(const Ctx *b, int m)
{
    const struct MapLayout *layout = b->inst->header->mapLayout;
    return m < 0 ? NULL : VoxelRoles_Find(layout->primaryTileset, layout->secondaryTileset, m);
}

// A hole in a wall over a door: wall both sides in its own row, art that
// fills its cell, and bespoke.
static bool LintelCell(const Ctx *b, int cx, int cy)
{
    int m = Meta(b, cx, cy);
    const VoxelArtInfo *st = Art(b, m);
    if (m < 0 || !Outdoor(b))
        return false;
    if (!(BlockedCell(b, cx - 1, cy) && BlockedCell(b, cx + 1, cy)))
        return false;
    if (!(st && st->solid >= 0.9f))
        return false;
    return Rare(b, m);
}

// drawn above the player, filling its cell, and bespoke: a row of this
// building you walk behind
static bool OverheadBespoke(const Ctx *b, int cx, int cy)
{
    int m = Meta(b, cx, cy);
    const VoxelArtInfo *st = Art(b, m);
    if (!(st && st->overhead && st->solid >= 0.9f))
        return false;
    return Rare(b, m);
}

// The blocked run directly north of the door at (cx, doorY): [top, bottom].
static bool RunAbove(const Ctx *b, int cx, int doorY, int *topOut, int *botOut)
{
    int y = doorY - 1, mass, top;
    if (y < 0)
        return false;
    mass = y;
    if (!BlockedCell(b, cx, mass))
    {
        // A DOORWAY YOU WALK INTO IS STILL A DOORWAY IN A BUILDING -- only
        // over a door of this column's own.
        int n = 0;
        if (!InMap(b, cx, doorY) || !b->warp[doorY * b->w + cx])
            return false;
        while (n < LINTEL_MAX && mass - n >= 0 && !BlockedCell(b, cx, mass - n) && LintelCell(b, cx, mass - n))
            n++;
        if (n == 0)
            return false;
        mass -= n;
        if (mass < 0 || !BlockedCell(b, cx, mass))
            return false;
    }
    top = mass;
    while (top - 1 >= 0 && BlockedCell(b, cx, top - 1) && (y - (top - 1)) < 8)
        top--;
    // ...AND A WALK-BEHIND ROW IN THE MIDDLE OF A BUILDING IS STILL THE
    // BUILDING, where its own art continues on both sides of the gap.
    while (Outdoor(b) && Rare(b, Meta(b, cx, top)))
    {
        int g = 0, nxt;
        while (g < WALK_BEHIND_GAP && top - 1 - g >= 0 && !BlockedCell(b, cx, top - 1 - g)
               && OverheadBespoke(b, cx, top - 1 - g))
            g++;
        if (g == 0)
            break;
        nxt = top - g - 1;
        if (nxt < 0 || !BlockedCell(b, cx, nxt))
            break;
        if (!Rare(b, Meta(b, cx, nxt)))
            break;
        if (y - nxt >= 8)
            break;
        top = nxt;
        while (top - 1 >= 0 && BlockedCell(b, cx, top - 1) && (y - (top - 1)) < 8)
            top--;
    }
    *topOut = top;
    *botOut = y;
    return true;
}

// A ROOF: blocked, its top half drawn above the player and filling the cell,
// not foliage and not scenery (lib/Gen3.lua ctx.roofAt).
static bool RoofAt(const Ctx *b, int cx, int cy)
{
    int m;
    const VoxelArtInfo *st;
    if (!Outdoor(b) || !BlockedAt(b, cx, cy))
        return false;
    m = Meta(b, cx, cy);
    if (m < 0 || Authored(b, m))
        return false;
    if (VoxelScene_At(b->inst, cx, cy) != SC_NONE)
        return false;
    st = Art(b, m);
    return st && st->overhead && st->solid > 0.75f && !st->leafy;
}

// The first roof row of the column's run, looked for from `floorY` down.
static bool RoofTop(const Ctx *b, int cx, int doorY, int floorY, int *row)
{
    int t, bt, y;
    if (!RunAbove(b, cx, doorY, &t, &bt))
        return false;
    if (floorY >= 0 && floorY > t)
        t = floorY;
    for (y = t; y <= bt; y++)
        if (RoofAt(b, cx, y))
        {
            *row = y;
            return true;
        }
    return false;
}

// drawn as foliage and nothing else
static bool FoliageCell(const Ctx *b, int cx, int cy)
{
    int m = Meta(b, cx, cy);
    const VoxelArtInfo *st = Art(b, m);
    const VoxelRole *r;
    if (!(st && st->leafy && st->solid > 0.5f))
        return false;
    r = Role(b, m);
    return r && r->material == ROLE_MAT_GREEN;
}

static bool LeafyAt(const Ctx *b, int cx, int cy)
{
    const VoxelArtInfo *st = Art(b, Meta(b, cx, cy));
    return st && st->leafy;
}

// the bridge behaviours (`bridge_lift`): road, whatever mass it is welded to
static bool BridgeBehaviour(uint8_t beh)
{
    return (beh >= 0x70 && beh <= 0x73) || (beh >= 0x7A && beh <= 0x7D) || beh == 0x7F || beh == 0x78;
}

typedef struct {
    Cell *cells;
    int n, cap;
} CellList;

static void Push(CellList *l, int x, int y)
{
    if (l->n + 1 > l->cap)
    {
        l->cap = l->cap ? l->cap * 2 : 64;
        l->cells = realloc(l->cells, l->cap * sizeof(Cell));
    }
    l->cells[l->n].x = (int16_t)x;
    l->cells[l->n].y = (int16_t)y;
    l->n++;
}

static void Claim(Ctx *b, CellList *l, int x, int y, int id)
{
    int k;
    if (!InMap(b, x, y))
        return;
    k = y * b->w + x;
    if (b->owner[k] < 0)
    {
        b->owner[k] = (int16_t)id;
        Push(l, x, y);
    }
}

typedef struct { int cx, top, raw; } Taken;

// ---------------------------------------------------------------------------
// Measurement (lib/Structures.lua buildVolume + foundGen3Buildings)
// ---------------------------------------------------------------------------
static bool RoofUnseen(const Ctx *b, int cx, int cy)
{
    const VoxelArtInfo *st;
    if (BlockedAt(b, cx, cy))
        return false;
    st = Art(b, Meta(b, cx, cy));
    return st && st->overhead && st->solid >= 0.9f && !st->leafy;
}

// A ROOF PLAN: roof across the row's interior (the end cells may be the
// eave's corners), or art the census reads as a surface with no face.
static bool PlanAt(const Ctx *b, const CellList *l, int id, int cx, int cy, bool allowTiled)
{
    const VoxelRole *r;
    int i, lo = 1 << 30, hi = -1, n = 0;
    for (i = 0; i < l->n; i++)
        if (l->cells[i].y == cy)
        {
            n++;
            if (l->cells[i].x < lo) lo = l->cells[i].x;
            if (l->cells[i].x > hi) hi = l->cells[i].x;
        }
    if (RoofAt(b, cx, cy))
    {
        bool whole = true;
        for (i = 0; i < l->n; i++)
        {
            int x = l->cells[i].x;
            if (l->cells[i].y != cy)
                continue;
            if (n >= 3 && (x == lo || x == hi))
                continue;
            if (!RoofAt(b, x, cy)) { whole = false; break; }
        }
        (void)id;
        if (whole)
            return true;
    }
    r = Role(b, Meta(b, cx, cy));
    if (r && r->cap >= 12 && r->face <= 2)
        return true;
    // Past the reference: a roof drawn on the bottom layer (Birch's lab, its
    // green body at 533) states nothing either test can read, but it is
    // TILED from one repeated piece across the row, where a wall row varies
    // with its windows and its door.
    {
        int hist[16][2], nh = 0, most = 0, k, inner = 0;
        for (i = 0; i < l->n; i++)
        {
            int x = l->cells[i].x, m;
            if (l->cells[i].y != cy || (n >= 3 && (x == lo || x == hi)))
                continue;
            m = Meta(b, x, cy);
            inner++;
            for (k = 0; k < nh; k++)
                if (hist[k][0] == m) { hist[k][1]++; break; }
            if (k == nh && nh < 16) { hist[nh][0] = m; hist[nh][1] = 1; nh++; }
        }
        for (k = 0; k < nh; k++)
            if (hist[k][1] > most) most = hist[k][1];
        return allowTiled && inner >= 2 && most * 10 >= inner * 6;
    }
}

static void Measure(Ctx *b, VoxelBuilding *bd, const CellList *l, int id)
{
    VoxelInstance *inst = b->inst;
    int i, x, rows = 0, E, P, wallPx, riseRows, hPx, capH, measured, riseTiles, flatCols = 0;
    bool vault, inferred = false;
    float base = 0.0f;
    bool haveBase = false;
    static const int dx[4] = {1, -1, 0, 0};
    static const int dy[4] = {0, 0, 1, -1};

    bd->minX = bd->minY = 32767;
    bd->maxX = bd->maxY = -32768;
    for (i = 0; i < l->n; i++)
    {
        if (l->cells[i].x < bd->minX) bd->minX = l->cells[i].x;
        if (l->cells[i].x > bd->maxX) bd->maxX = l->cells[i].x;
        if (l->cells[i].y < bd->minY) bd->minY = l->cells[i].y;
        if (l->cells[i].y > bd->maxY) bd->maxY = l->cells[i].y;
    }
    if (bd->maxX - bd->minX + 1 > BLD_COLS)
        bd->maxX = bd->minX + BLD_COLS - 1;
    for (x = 0; x < BLD_COLS; x++)
        bd->colTop[x] = bd->colFront[x] = -1;
    for (i = 0; i < l->n; i++)
    {
        int c = l->cells[i].x - bd->minX;
        if (c < 0 || c >= BLD_COLS)
            continue;
        if (bd->colTop[c] < 0 || l->cells[i].y < bd->colTop[c]) bd->colTop[c] = l->cells[i].y;
        if (l->cells[i].y > bd->colFront[c]) bd->colFront[c] = l->cells[i].y;
    }
    // A DOOR IS PART OF THE FACADE: the door row's door cells stand with the
    // wall they are cut into, so their column's front is the door row.
    for (x = bd->minX; x <= bd->maxX; x++)
    {
        int c = x - bd->minX;
        const VoxelCell *dc;
        if (bd->colTop[c] < 0 || !InMap(b, x, bd->doorY) || bd->colFront[c] >= bd->doorY)
            continue;
        if (bd->colFront[c] != bd->doorY - 1)
            continue;
        dc = &inst->cells[bd->doorY * b->w + x];
        if (dc->cls == VC_DOOR || (dc->flags & VCF_WARP) || x == bd->doorX)
            bd->colFront[c] = bd->doorY;
    }
    for (x = bd->minX; x <= bd->maxX; x++)
    {
        int c = x - bd->minX;
        if (bd->colTop[c] >= 0 && bd->colFront[c] - bd->colTop[c] + 1 > rows)
            rows = bd->colFront[c] - bd->colTop[c] + 1;
    }
    bd->bldRows = (uint8_t)rows;

    // how deep the roof plan is, from the top of each column
    bd->roofRows = 0;
    for (x = bd->minX; x <= bd->maxX; x++)
    {
        int c = x - bd->minX, y, n = 0;
        if (bd->colTop[c] < 0)
            continue;
        for (y = bd->colTop[c]; y < bd->colFront[c]; y++)
        {
            // a walk-behind row at the top is the roof's own edge, drawn
            // over your head: roof, not wall (the reference skips it)
            if (RoofUnseen(b, x, y))
            {
                n++;
                continue;
            }
            if (!PlanAt(b, l, id, x, y, (n + 1) * 2 <= rows))
                break;
            n++;
        }
        if (n > bd->roofRows)
            bd->roofRows = (uint8_t)n;
        // A FLAT ROOFTOP (a gym): the roof band's rows repeat down the column
        if (bd->colFront[c] - bd->colTop[c] + 1 >= 3)
        {
            int m0 = Meta(b, x, bd->colTop[c]), m1 = Meta(b, x, bd->colTop[c] + 1), m2 = Meta(b, x, bd->colTop[c] + 2);
            if (m0 >= 0 && (m0 == m1 || m1 == m2))
                flatCols++;
        }
    }
    bd->flatRoof = flatCols >= 2 && bd->roofRows > 0 && bd->roofed;

    // THE ROOF'S DRAWING MAY START A ROW NORTH OF THE BUILDING
    // (Structures.lua roofArtAbove): a walkable row drawn over the player,
    // not foliage, across every column -- the Center's roof curve.
    {
        bool above = true;
        for (x = bd->minX; x <= bd->maxX && above; x++)
        {
            int c = x - bd->minX, ay, mm;
            const VoxelArtInfo *st;
            if (bd->colTop[c] < 0)
                continue;
            ay = bd->colTop[c] - 1;
            if (ay < 0 || BlockedAt(b, x, ay) || b->owner[ay * b->w + x] >= 0)
            {
                above = false;
                break;
            }
            mm = Meta(b, x, ay);
            st = Art(b, mm);
            if (!(st && st->n2 > 0 && st->solid >= 0.30f && !st->leafy))
                above = false;
        }
        bd->roofArtAbove = above;
    }
    if (bd->roofRows >= rows)
        bd->roofRows = rows > 1 ? rows - 1 : 0;

    // the street it stands on: the lowest walkable land beside it, raised
    // to its doorstep
    for (i = 0; i < l->n; i++)
    {
        int d;
        for (d = 0; d < 4; d++)
        {
            int nx = l->cells[i].x + dx[d], ny = l->cells[i].y + dy[d];
            const VoxelCell *nc;
            if (!InMap(b, nx, ny) || b->owner[ny * b->w + nx] == id)
                continue;
            nc = &inst->cells[ny * b->w + nx];
            if ((nc->flags & VCF_BLOCKED) || nc->cls == VC_WATER || nc->cls == VC_VOID || nc->building >= 0)
                continue;
            if (!haveBase || nc->groundH < base)
                base = nc->groundH;
            haveBase = true;
        }
    }
    {
        static const int sx[6] = {0, 0, -1, 1, -1, 1};
        static const int sy[6] = {1, 2, 1, 1, 0, 0};
        int k;
        for (k = 0; k < 6; k++)
        {
            int nx = bd->doorX + sx[k], ny = bd->doorY + sy[k];
            const VoxelCell *nc;
            if (!InMap(b, nx, ny))
                continue;
            nc = &inst->cells[ny * b->w + nx];
            if ((nc->flags & VCF_BLOCKED) || nc->cls == VC_WATER || nc->cls == VC_VOID)
                continue;
            if (!haveBase || nc->groundH > base)
                base = nc->groundH;
            haveBase = true;
            break;
        }
    }
    bd->base = base;

    // THE FACADE AND THE SHED (Structures.lua, the stated-roof branch of
    // buildVolume, then foundGen3Buildings' cap). Tile rows: E deep, P of it
    // roof plan. A roof no layer states is the top cell row, inferred.
    E = rows * 2;
    P = bd->roofRows * 2;
    if (P == 0 && rows >= 2)
    {
        P = 2;
        inferred = true;
    }
    wallPx = (E - P) * 8;
    if (wallPx < 8) wallPx = 8;
    vault = P * 2 >= E && P >= 3;
    riseRows = vault ? (P < 4 ? P : 4) : (P < 2 ? P : 2);
    hPx = wallPx + riseRows * 8;
    capH = 40 + bd->overheadRows * 16;
    if (capH > rows * 16) capH = rows * 16;
    if (capH < 40) capH = 40;
    {
        int roofCells = inferred ? 1 : bd->roofRows;
        if (capH < (rows - roofCells) * 16 + 16) capH = (rows - roofCells) * 16 + 16;
    }
    if (capH > rows * 16) capH = rows * 16;
    measured = hPx < capH ? hPx : capH;
    riseTiles = P;
    if (riseTiles > measured / 8 - 1) riseTiles = measured / 8 - 1;
    if (riseTiles > 2) riseTiles = 2;
    if (riseTiles < 0 || bd->flatRoof) riseTiles = 0;
    bd->peak = base + measured / 16.0f;
    bd->rise = riseTiles * 8 / 16.0f;
    bd->eave = bd->peak - bd->rise;
}

void VoxelBuildings_Find(VoxelInstance *inst)
{
    Ctx b;
    int w = inst->width, h = inst->height, i, x, y;
    const struct MapEvents *events = inst->header->events;
    CellList cells = {0};
    Taken taken[16];

    inst->buildingCount = 0;
    for (i = 0; i < w * h; i++)
        inst->cells[i].building = -1;
    memset(&b, 0, sizeof(b));
    b.inst = inst;
    b.w = w; b.h = h;
    b.owner = malloc(w * h * sizeof(int16_t));
    b.warp = calloc(w * h, 1);
    for (i = 0; i < w * h; i++)
    {
        b.owner[i] = -1;
        if (b.counts[inst->cells[i].metatile] < 0xFFFF)
            b.counts[inst->cells[i].metatile]++;
    }
    if (events && events->warps)
        for (i = 0; i < events->warpCount; i++)
            if (InMap(&b, events->warps[i].x, events->warps[i].y))
                b.warp[events->warps[i].y * w + events->warps[i].x] = 1;

    for (i = 0; events && events->warps && i < events->warpCount && inst->buildingCount < MAX_VOXEL_BUILDINGS; i++)
    {
        int wx = events->warps[i].x, wy = events->warps[i].y;
        int t0, bt0, t0r = -1, id = inst->buildingCount, nTaken = 0, overheadRows = 0, k, walkY0 = -1;
        bool hasRoof;
        if (!InMap(&b, wx, wy))
            continue;
        hasRoof = RoofTop(&b, wx, wy, -1, &t0r);
        if (hasRoof)
            t0 = t0r;
        else if (!RunAbove(&b, wx, wy, &t0, &bt0))
            continue;
        // A DOOR ALREADY INSIDE A BUILDING IS THAT BUILDING'S SECOND DOOR.
        if (b.owner[wy * w + wx] >= 0 || b.owner[t0 * w + wx] >= 0)
            continue;
        cells.n = 0;

        // ---- the columns: the door's, then its neighbours while they keep
        //      the door's roofline ----
        {
            int pass, dxs;
            for (pass = 0; pass < 3; pass++)
            {
                for (dxs = (pass == 0 ? 0 : 1); dxs <= (pass == 0 ? 0 : 5); dxs++)
                {
                    int cx = pass == 2 ? wx - dxs : wx + dxs, t = 0, bt = -1, rawTop = 0, grew = 0, yy;
                    bool ok = true;
                    if (!RunAbove(&b, cx, wy, &t, &bt))
                        ok = false;
                    if (ok)
                    {
                        rawTop = t;
                        if (hasRoof)
                        {
                            int r;
                            if (!RoofTop(&b, cx, wy, t0r - 1, &r) || abs(r - t0r) > 1)
                                ok = false;
                            else
                                t = r;
                        }
                        else if (abs(t - t0) > 1)
                            ok = false;
                    }
                    if (ok)
                    {
                        // A BUILDING STOPS WHERE ITS OWN ART STOPS: walk down
                        // from the top while the art is common landscape
                        if (!hasRoof)
                            while (t < bt && b.counts[Meta(&b, cx, t) < 0 ? 0 : Meta(&b, cx, t)] > RARE_BUILD)
                                t++;
                        // ...AND A BUILDING YOU WALK BEHIND IS STILL THE
                        // BUILDING: overhead, solid, bespoke rows above it
                        for (k = 0; k < 4; k++)
                        {
                            int ny = t - 1, mm;
                            const VoxelArtInfo *st;
                            if (ny < 0 || BlockedAt(&b, cx, ny))
                                break;
                            mm = Meta(&b, cx, ny);
                            st = Art(&b, mm);
                            if (!(st && st->overhead && st->solid >= 0.9f && Rare(&b, mm)))
                                break;
                            t = ny;
                            grew++;
                        }
                        if (grew > overheadRows)
                            overheadRows = grew;
                        // A COLUMN OF FOLIAGE IS NOT A COLUMN OF THE BUILDING
                        if (Outdoor(&b))
                        {
                            bool allLeaf = true;
                            for (yy = t; yy <= bt; yy++)
                                if (!FoliageCell(&b, cx, yy)) { allLeaf = false; break; }
                            if (allLeaf)
                                ok = false;
                        }
                    }
                    if (!ok)
                        break;      // (the door column's refusal is not the flanks')
                    if (nTaken < 16)
                    {
                        taken[nTaken].cx = cx;
                        taken[nTaken].top = t;
                        taken[nTaken].raw = rawTop;
                        nTaken++;
                    }
                    for (yy = t; yy <= bt; yy++)
                        Claim(&b, &cells, cx, yy, id);
                }
            }
        }
        for (k = 0; k < cells.n; k++)
            if (walkY0 < 0 || cells.cells[k].y < walkY0)
                walkY0 = cells.cells[k].y;

        // ---- a roof's apex is bespoke: whole columns of rare art beside it ----
        if (cells.n > 0)
        {
            int x0 = 1 << 30, x1 = -1, d2, side;
            for (k = 0; k < cells.n; k++)
            {
                if (cells.cells[k].x < x0) x0 = cells.cells[k].x;
                if (cells.cells[k].x > x1) x1 = cells.cells[k].x;
            }
            for (side = 0; side < 2; side++)
                for (d2 = 1; d2 <= 3; d2++)
                {
                    int cx = side == 0 ? x1 + d2 : x0 - d2, t = 0, bt = -1, yy;
                    bool ok = cx >= 0 && cx < w && RunAbove(&b, cx, wy, &t, &bt);
                    for (yy = t; ok && yy <= bt; yy++)
                        if (!Rare(&b, Meta(&b, cx, yy)))
                            ok = false;
                    if (!ok)
                        break;
                    for (yy = t; yy <= bt; yy++)
                        Claim(&b, &cells, cx, yy, id);
                    if (nTaken < 16)
                    {
                        taken[nTaken].cx = cx; taken[nTaken].top = t; taken[nTaken].raw = t;
                        nTaken++;
                    }
                }
            // ...AND THE SHOPFRONT THE DOOR IS SET INTO: the door row's
            // blocked cells, bespoke or under the building's own mass
            x0 = 1 << 30; x1 = -1;
            for (k = 0; k < cells.n; k++)
            {
                if (cells.cells[k].x < x0) x0 = cells.cells[k].x;
                if (cells.cells[k].x > x1) x1 = cells.cells[k].x;
            }
            for (x = x0; x <= x1; x++)
            {
                if (InMap(&b, x, wy) && b.owner[wy * w + x] < 0 && BlockedAt(&b, x, wy))
                {
                    int mm = Meta(&b, x, wy);
                    bool under = Outdoor(&b) && wy > 0 && b.owner[(wy - 1) * w + x] == id;
                    if (mm >= 0 && (Rare(&b, mm) || under))
                        Claim(&b, &cells, x, wy, id);
                }
            }
        }

        // ---- A BUILDING HAS ONE ROOFLINE: fill every column up to the
        //      highest any reached, stopping at foliage ----
        if (Outdoor(&b) && nTaken > 1)
        {
            int tMin = 1 << 30;
            for (k = 0; k < nTaken; k++)
                if (taken[k].top < tMin) tMin = taken[k].top;
            for (k = 0; k < nTaken; k++)
            {
                int newTop = tMin < taken[k].raw ? taken[k].raw : tMin;
                for (y = taken[k].top - 1; y >= newTop; y--)
                {
                    if (LeafyAt(&b, taken[k].cx, y))
                        break;
                    Claim(&b, &cells, taken[k].cx, y, id);
                    taken[k].top = y;
                }
            }
            // ...AND THE WALK-BEHIND EDGE OF ONE ROOF IS ONE EDGE. Past the
            // reference: where some columns grew up through the roof's
            // overhead edge and their neighbours did not (the same drawing
            // laid on more than four houses is not "rare"), the rest take
            // the same overhead rows, or the building's ends stand a row
            // proud of its middle.
            for (k = 0; k < nTaken; k++)
                for (y = taken[k].top - 1; y >= tMin; y--)
                {
                    const VoxelArtInfo *st = Art(&b, Meta(&b, taken[k].cx, y));
                    if (BlockedAt(&b, taken[k].cx, y) || !(st && st->overhead && st->solid >= 0.9f && !st->leafy))
                        break;
                    Claim(&b, &cells, taken[k].cx, y, id);
                    taken[k].top = y;
                }
        }

        // ---- two more rows of one-off art across the top ----
        if (cells.n > 0)
        {
            int x0 = 1 << 30, x1 = -1, top = 1 << 30, rep;
            for (k = 0; k < cells.n; k++)
            {
                if (cells.cells[k].x < x0) x0 = cells.cells[k].x;
                if (cells.cells[k].x > x1) x1 = cells.cells[k].x;
            }
            for (k = 0; k < cells.n; k++)
                if (cells.cells[k].y < top) top = cells.cells[k].y;
            for (rep = 0; rep < 2; rep++)
            {
                int row = top - 1;
                bool ok = row >= 0;
                for (x = x0; ok && x <= x1; x++)
                {
                    int mm = Meta(&b, x, row);
                    if (!BlockedAt(&b, x, row) || mm < 0 || b.counts[mm] != 1)
                        ok = false;
                }
                if (!ok)
                    break;
                for (x = x0; x <= x1; x++)
                    Claim(&b, &cells, x, row, id);
                top = row;
            }
        }

        // ---- A CAVE MOUTH IS A HOLE IN A MASS, NOT A BUILDING ----
        if (cells.n > 0 && !hasRoof)
        {
            int y0 = 1 << 30, y1 = -1, nc = 0, rare = 0, above = 0, yCut = Outdoor(&b) ? wy - 1 : (1 << 30);
            for (k = 0; k < cells.n; k++)
            {
                int mm;
                if (cells.cells[k].y > yCut)
                    continue;
                if (cells.cells[k].y < y0) y0 = cells.cells[k].y;
                if (cells.cells[k].y > y1) y1 = cells.cells[k].y;
                nc++;
                mm = Meta(&b, cells.cells[k].x, cells.cells[k].y);
                if (mm >= 0 && b.counts[mm] <= RARE_BUILD * 8)
                    rare++;
            }
            if (nc > 0 && y0 == y1 && y0 > 0)
            {
                for (k = 0; k < cells.n; k++)
                    if (cells.cells[k].y == y0 && BlockedAt(&b, cells.cells[k].x, y0 - 1))
                        above++;
                if (above * 2 >= nc && rare * 5 < nc * 2)
                {
                    for (k = 0; k < cells.n; k++)
                        b.owner[cells.cells[k].y * w + cells.cells[k].x] = -1;
                    cells.n = 0;
                }
            }
        }
        // ---- A SPAN THE CARTRIDGE CALLS A BRIDGE IS NOT PART OF A BUILDING ----
        {
            int keep = 0;
            for (k = 0; k < cells.n; k++)
            {
                const VoxelCell *c = &inst->cells[cells.cells[k].y * w + cells.cells[k].x];
                if (BridgeBehaviour(c->behavior))
                    b.owner[cells.cells[k].y * w + cells.cells[k].x] = -1;
                else
                    cells.cells[keep++] = cells.cells[k];
            }
            cells.n = keep;
        }
        if (cells.n > 0)
        {
            VoxelBuilding *bd = &inst->buildings[inst->buildingCount++];
            memset(bd, 0, sizeof(*bd));
            bd->doorX = wx; bd->doorY = wy;
            bd->overheadRows = (uint8_t)overheadRows;
            bd->roofed = hasRoof;
            Measure(&b, bd, &cells, id);
        }
    }

    // ---- A BUILDING EMERALD GIVES NO DOOR IS STILL A BUILDING: a filled
    //      free-standing block, two cells each way, a roof over every
    //      column, art that fills its cells ----
    if (Outdoor(&b))
    {
        uint8_t *seen = calloc(w * h, 1);
        int *stack = malloc(w * h * sizeof(int));
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
            {
                int sp = 0, x0 = x, x1 = x, y0 = y, y1 = y, k;
                bool ok;
                if (seen[y * w + x] || b.owner[y * w + x] >= 0 || !BlockedCell(&b, x, y))
                    continue;
                if (inst->buildingCount >= MAX_VOXEL_BUILDINGS)
                    break;
                cells.n = 0;
                seen[y * w + x] = 1;
                stack[sp++] = y * w + x;
                while (sp > 0)
                {
                    int kk = stack[--sp], cx = kk % w, cy = kk / w, d;
                    static const int ddx[4] = {1, -1, 0, 0};
                    static const int ddy[4] = {0, 0, 1, -1};
                    Push(&cells, cx, cy);
                    if (cx < x0) x0 = cx;
                    if (cx > x1) x1 = cx;
                    if (cy < y0) y0 = cy;
                    if (cy > y1) y1 = cy;
                    for (d = 0; d < 4; d++)
                    {
                        int nx = cx + ddx[d], ny = cy + ddy[d];
                        if (InMap(&b, nx, ny) && !seen[ny * w + nx] && b.owner[ny * w + nx] < 0 && BlockedCell(&b, nx, ny))
                        {
                            seen[ny * w + nx] = 1;
                            stack[sp++] = ny * w + nx;
                        }
                    }
                }
                ok = (x1 - x0 + 1) >= 2 && (y1 - y0 + 1) >= 2 && cells.n == (x1 - x0 + 1) * (y1 - y0 + 1);
                for (k = 0; ok && k < cells.n; k++)
                {
                    int d;
                    static const int ddx[4] = {1, -1, 0, 0};
                    static const int ddy[4] = {0, 0, 1, -1};
                    for (d = 0; d < 4; d++)
                    {
                        int nx = cells.cells[k].x + ddx[d], ny = cells.cells[k].y + ddy[d];
                        if (nx >= x0 && nx <= x1 && ny >= y0 && ny <= y1)
                            continue;
                        if (!InMap(&b, nx, ny) || BlockedAt(&b, nx, ny)) { ok = false; break; }
                    }
                }
                for (k = x0; ok && k <= x1; k++)
                    if (!RoofAt(&b, k, y0))
                        ok = false;
                for (k = 0; ok && k < cells.n; k++)
                {
                    const VoxelArtInfo *st = Art(&b, Meta(&b, cells.cells[k].x, cells.cells[k].y));
                    if (!(st && st->solid >= 0.9f))
                        ok = false;
                }
                if (ok)
                {
                    int id = inst->buildingCount;
                    VoxelBuilding *bd = &inst->buildings[inst->buildingCount++];
                    for (k = 0; k < cells.n; k++)
                        b.owner[cells.cells[k].y * w + cells.cells[k].x] = (int16_t)id;
                    memset(bd, 0, sizeof(*bd));
                    bd->doorX = x0 + (x1 - x0) / 2;
                    bd->doorY = y1;
                    bd->roofed = 1;
                    Measure(&b, bd, &cells, id);
                }
            }
        free(seen);
        free(stack);
    }

    // ---- the cells: every footprint cell, and the door cells standing in
    //      a facade, belong to their building ----
    for (i = 0; i < inst->buildingCount; i++)
    {
        VoxelBuilding *bd = &inst->buildings[i];
        for (x = bd->minX; x <= bd->maxX; x++)
        {
            int c = x - bd->minX;
            if (bd->colTop[c] < 0)
                continue;
            for (y = bd->colTop[c]; y <= bd->colFront[c]; y++)
            {
                VoxelCell *cell;
                if (!InMap(&b, x, y))
                    continue;
                cell = &inst->cells[y * w + x];
                if (b.owner[y * w + x] != i && !(y == bd->doorY && y == bd->colFront[c]))
                    continue;
                cell->building = (int16_t)i;
                if (cell->flags & VCF_BLOCKED)
                    cell->cls = VC_BUILDING;
                cell->groundH = bd->base;
                cell->topH = bd->eave;
            }
            // the row whose drawing moved onto the roof paints plain ground
            if (bd->roofArtAbove && InMap(&b, x, bd->colTop[c] - 1))
                inst->cells[(bd->colTop[c] - 1) * w + x].flags |= VCF_VACATED;
        }
    }
    free(cells.cells);
    free(b.owner);
    free(b.warp);
}

#endif
#endif
