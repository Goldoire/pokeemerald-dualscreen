#include "voxel_world.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "event_object_movement.h"
#include "constants/map_types.h"
#include "voxel_atlas.h"

VoxelInstance gVoxelInstances[MAX_VOXEL_MAP_INSTANCES];
int gVoxelInstanceCount = 0;

static const struct MapLayout *sLastLayout = NULL;
static u8 sLastGroup = 0xFF;
static u8 sLastNum = 0xFF;

extern const struct Tileset gTileset_BrendansMaysHouse;
extern const struct Tileset gTileset_PokemonCenter;

// ---------------------------------------------------------------------------
// Class heights, in courses (px / 16). From the DramaticShapes Gen 3 profile.
// ---------------------------------------------------------------------------
static const float sClassHeight[VC_COUNT] = {
    [VC_VOID] = 0.0f,
    [VC_GROUND] = 0.0f,
    [VC_GRASS] = 0.0f,
    [VC_WATER] = -0.125f,
    [VC_LEDGE] = 0.375f,
    [VC_BRIDGE] = 0.25f,
    [VC_LOG] = 0.125f,
    [VC_SLOPE] = 0.0f,
    [VC_DOOR] = 0.0f,
    [VC_WALL] = 1.0f,
    [VC_CLIFF] = 1.0f,
    [VC_BUILDING] = 1.0f,
    [VC_COUNTER] = 0.5f,
    [VC_CONSOLE] = 1.0f,
    [VC_BOOKCASE] = 2.0f,
    [VC_CYLINDER] = 1.0f,
    [VC_CAN] = 0.5625f,
    [VC_TABLETOP] = 0.75f,
    [VC_CHAIR] = 0.5f,
    [VC_BED] = 0.4375f,
    [VC_PROP] = 1.0f,
    [VC_WORKTOP] = 1.125f,
    [VC_SINK] = 1.125f,
    [VC_APPLIANCE] = 1.875f,
    [VC_TV] = 1.0f,
    [VC_CABINET] = 1.0f,
};

float VoxelWorld_ClassHeight(VoxelClass cls)
{
    if (cls < 0 || cls >= VC_COUNT)
        return 0.0f;
    return sClassHeight[cls];
}

// ---------------------------------------------------------------------------
// BEHAVIOUR BYTE -> CLASS (data/gen3_shapes.lua, `behaviour`).
//
// The byte says what a cell IS; whether it can be stood on is the cell's own
// collision bits. Classes marked "standing" only apply to blocked cells: a
// walkable MB_MOUNTAIN_TOP is a shelf, not a wall. Doors are the exception,
// walkable and part of the facade regardless.
// ---------------------------------------------------------------------------
static VoxelClass ClassFromBehavior(u8 b, bool *standing)
{
    *standing = false;
    switch (b)
    {
    case 0x02: case 0x03: case 0x09: case 0x24:          // tall/long/ash grass
        return VC_GRASS;
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
    case 0x18: case 0x19: case 0x1A: case 0x22: case 0x28: case 0x2A: case 0x2B:
    case 0x50: case 0x51: case 0x52: case 0x53:          // every water, currents, waterfall
        return VC_WATER;
    case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x3C: case 0x3D: case 0x3E: case 0x3F:
        return VC_LEDGE;                                  // MB_JUMP_*
    case 0xD0: case 0xD1:                                 // muddy / bumpy slope
        return VC_SLOPE;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x78:
    case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7F:
    case 0xD3: case 0xD4: case 0xD5: case 0xD6:          // bridges, Fortree walk, Route 119 rails
        return VC_BRIDGE;
    case 0x74: case 0x75: case 0x76: case 0x77:          // Pacifidlog logs
        return VC_LOG;
    case 0x60: case 0x61: case 0x69: case 0x6C: case 0x8B: case 0x8C: case 0x8D: case 0xBE: case 0xEA:
        return VC_DOOR;                                   // doors, ladders, shutters
    case 0x80:                                            // MB_COUNTER
        *standing = true; return VC_COUNTER;
    case 0x83: case 0x84: case 0x86: case 0xB0: case 0xB1: case 0xC5: case 0xE7: case 0xE8: case 0xE9:
        *standing = true; return VC_CONSOLE;              // PC, TV, cable/wireless boxes, timer
    case 0x8A:                                            // MB_ROULETTE: drawn from above
        *standing = true; return VC_TABLETOP;
    case 0xE3:                                            // MB_VASE
        *standing = true; return VC_CYLINDER;
    case 0xE4:                                            // MB_TRASH_CAN
        *standing = true; return VC_CAN;
    case 0x87: case 0x89: case 0xB2: case 0xB4: case 0xB5: case 0xB8: case 0xBF: case 0xC3: case 0xC4: case 0xC6:
        *standing = true; return VC_PROP;                 // feeder, slot machine, secret base decor
    case 0x85: case 0x8E: case 0x8F: case 0xC7: case 0xE0: case 0xE1: case 0xE2: case 0xE5: case 0xE6:
    case 0x01: case 0x90: case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97:
    case 0x98: case 0x99: case 0x9A: case 0x9B: case 0x9C: case 0x9D: case 0xB7: case 0xB9:
        *standing = true; return VC_WALL;                 // shelves, posters, secret base walls
    default:
        return VC_GROUND;
    }
}

static bool IsBridgeLike(VoxelClass c) { return c == VC_BRIDGE || c == VC_LOG; }

static bool IsWalkableClass(VoxelClass c)
{
    return c == VC_GROUND || c == VC_GRASS || c == VC_LEDGE || c == VC_SLOPE || c == VC_DOOR
        || c == VC_CHAIR || c == VC_BRIDGE || c == VC_LOG;
}

static bool IsFurnitureClass(VoxelClass c)
{
    return c >= VC_COUNTER && c < VC_COUNT;
}

// ---------------------------------------------------------------------------
// Metatile pins (data/gen3_shapes.lua, `metatiles`): the only way to say
// "that cell is a fridge". Keyed on the tileset that owns the id.
// ---------------------------------------------------------------------------
static VoxelClass PinFor(const struct MapLayout *layout, int metatile, bool *walkablePin)
{
    *walkablePin = false;
    if (layout->secondaryTileset == &gTileset_BrendansMaysHouse)
    {
        switch (metatile)
        {
        case 568: return VC_APPLIANCE;                     // fridge
        case 569: return VC_SINK;
        case 570: return VC_WORKTOP;
        case 571: case 572: case 601: return VC_CABINET;
        case 576: case 577: case 614: return VC_TV;
        case 538: case 539: case 546: case 547: return VC_TABLETOP;
        case 537: case 540: case 545: case 548: case 610:
            *walkablePin = true; return VC_CHAIR;
        case 643: case 644: case 645: case 651: case 652: case 653: case 657:
            return VC_BED;
        }
    }
    else if (layout->secondaryTileset == &gTileset_PokemonCenter)
    {
        switch (metatile)
        {
        case 600: case 545: case 601: return VC_COUNTER;   // the nurse's desk
        case 672: *walkablePin = true; return VC_GROUND;    // 2F stair tread, not a table
        }
    }
    return VC_COUNT;
}

// ---------------------------------------------------------------------------
// Instances
// ---------------------------------------------------------------------------
static VoxelCell *CellPtr(VoxelInstance *inst, int lx, int ly)
{
    if (lx < 0 || ly < 0 || lx >= inst->width || ly >= inst->height)
        return NULL;
    return &inst->cells[ly * inst->width + lx];
}

static u16 RawBlock(const VoxelInstance *inst, int lx, int ly)
{
    if (inst == &gVoxelInstances[0])
    {
        int bx = lx + MAP_OFFSET;
        int by = ly + MAP_OFFSET_Y;
        if (gBackupMapLayout.map && bx >= 0 && by >= 0 && bx < gBackupMapLayout.width && by < gBackupMapLayout.height)
            return gBackupMapLayout.map[bx + gBackupMapLayout.width * by];
    }
    return inst->header->mapLayout->map[ly * inst->header->mapLayout->width + lx];
}

static uint32_t HashLiveMap(const VoxelInstance *inst)
{
    uint32_t h = 2166136261u;
    int x, y;
    for (y = 0; y < inst->height; y++)
        for (x = 0; x < inst->width; x++)
            h = (h ^ RawBlock(inst, x, y)) * 16777619u;
    return h;
}

bool VoxelWorld_IsMapAvailable(void)
{
    extern void CB2_Overworld(void);
    extern void CB2_OverworldBasic(void);
    if (gMapHeader.mapLayout == NULL || gBackupMapLayout.map == NULL)
        return false;
    return gMain.callback2 == CB2_Overworld || gMain.callback2 == CB2_OverworldBasic;
}

const VoxelInstance *VoxelWorld_InstanceAt(int worldX, int worldY)
{
    int i;
    for (i = 0; i < gVoxelInstanceCount; i++)
    {
        const VoxelInstance *inst = &gVoxelInstances[i];
        int lx = worldX - inst->originX;
        int ly = worldY - inst->originY;
        if (lx >= 0 && ly >= 0 && lx < inst->width && ly < inst->height)
            return inst;
    }
    return NULL;
}

const VoxelCell *VoxelWorld_CellAt(int worldX, int worldY)
{
    const VoxelInstance *inst = VoxelWorld_InstanceAt(worldX, worldY);
    if (inst == NULL || inst->cells == NULL)
        return NULL;
    return &inst->cells[(worldY - inst->originY) * inst->width + (worldX - inst->originX)];
}

void VoxelWorld_GetPlayerWorldCoords(float *wx, float *wz)
{
    struct ObjectEvent *playerObj = &gObjectEvents[gPlayerAvatar.objectEventId];
    if (wx) *wx = (float)(playerObj->currentCoords.x - MAP_OFFSET);
    if (wz) *wz = (float)(playerObj->currentCoords.y - MAP_OFFSET_Y);
}

float VoxelWorld_WalkHeightAt(int worldX, int worldY)
{
    const VoxelCell *c = VoxelWorld_CellAt(worldX, worldY);
    if (c == NULL)
        return 0.0f;
    if (c->cls == VC_LEDGE)
        return c->groundH;
    if (IsWalkableClass((VoxelClass)c->cls))
        return c->topH;
    if (c->cls == VC_WATER)
        return c->groundH + 0.125f;   // surfing rides on the surface
    return c->groundH;
}

void VoxelWorld_SetMetaInfo(VoxelInstance *inst, int metatile, const VoxelMetaInfo *info)
{
    if (metatile >= 0 && metatile < 1024)
        inst->meta[metatile] = *info;
}

// ---------------------------------------------------------------------------
// Elevation -> height. Levels are labels, not rungs: rank the ones the map
// uses and space consecutive ranks one course apart, datum at level 3.
// ---------------------------------------------------------------------------
static void BuildElevationRanks(VoxelInstance *inst)
{
    bool seen[16] = {false};
    int order[16], n = 0, i, datum = -1;
    int x, y;

    for (i = 0; i < 16; i++)
        inst->rankH[i] = 0.0f;
    if (!inst->useElevation)
        return;

    for (y = 0; y < inst->height; y++)
        for (x = 0; x < inst->width; x++)
        {
            VoxelCell *c = &inst->cells[y * inst->width + x];
            if (c->cls == VC_VOID)
                continue;
            if (c->elevation == 0 || c->elevation == 1 || c->elevation == 15)
                continue;
            seen[c->elevation] = true;
        }
    for (i = 0; i < 16; i++)
        if (seen[i])
        {
            if (i == 3) datum = n;
            order[n++] = i;
        }
    if (n == 0)
        return;
    if (datum < 0)
        datum = 0;
    for (i = 0; i < n; i++)
        inst->rankH[order[i]] = (float)(i - datum);
}

// ---------------------------------------------------------------------------
// Buildings are the runs above the doors (lib/Gen3.lua). A warp names a door,
// the blocked run north of it is the building, and neighbouring columns join
// while their run top stays within a row of the last one's.
// ---------------------------------------------------------------------------
#define BUILDING_MAX_ROWS 8
#define BUILDING_MAX_COLS 48

// Outdoors, Emerald keeps every building in a town's SECONDARY tileset
// (ids 512+) and the landscape -- trees, cliffs, rocks -- in the shared
// primary one, so a run that crosses into primary art has left the house
// for the tree line or the hill it backs onto.
static bool IsMassCell(VoxelInstance *inst, int lx, int ly)
{
    VoxelCell *c = CellPtr(inst, lx, ly);
    if (c == NULL || !(c->flags & VCF_BLOCKED))
        return false;
    if (c->building >= 0)
        return false;
    if (!inst->indoor && c->metatile < 512)
        return false;
    return c->cls == VC_WALL || c->cls == VC_CLIFF || IsFurnitureClass((VoxelClass)c->cls);
}

static bool IsDoorCell(VoxelInstance *inst, int lx, int ly)
{
    VoxelCell *c = CellPtr(inst, lx, ly);
    return c != NULL && (c->cls == VC_DOOR || (c->flags & VCF_WARP));
}

// Top row of the blocked run that includes (lx, fromY), walking north, capped.
static int RunTop(VoxelInstance *inst, int lx, int fromY, int facadeRow)
{
    int top = fromY;
    while (top - 1 >= 0 && IsMassCell(inst, lx, top - 1) && (facadeRow - (top - 1)) < BUILDING_MAX_ROWS)
        top--;
    return top;
}

// The run a candidate column contributes: its facade-row cell must be mass or
// a door, and at least one mass row must stand above it.
static bool ColumnRun(VoxelInstance *inst, int lx, int facadeRow, int *outTop)
{
    int start;
    if (IsMassCell(inst, lx, facadeRow))
        start = facadeRow;
    else if (IsDoorCell(inst, lx, facadeRow) && IsMassCell(inst, lx, facadeRow - 1))
        start = facadeRow - 1;
    else
        return false;
    *outTop = RunTop(inst, lx, start, facadeRow);
    if (facadeRow - *outTop + 1 < 2)
        return false;
    return true;
}

static void ClaimBuilding(VoxelInstance *inst, int b)
{
    VoxelBuilding *bd = &inst->buildings[b];
    int x, y, r;
    int roofFrom = -1;   // first row (from the facade) drawn wholly above the player
    int maxRows = 0;

    for (x = bd->minX; x <= bd->maxX; x++)
    {
        int top = bd->colTop[x - bd->minX];
        int rows = bd->facadeRow - top + 1;
        if (rows > maxRows)
            maxRows = rows;
        for (y = top; y <= bd->facadeRow; y++)
        {
            VoxelCell *c = CellPtr(inst, x, y);
            if (c == NULL)
                continue;
            c->building = b;
            if (c->cls != VC_DOOR)
                c->cls = VC_BUILDING;
        }
    }

    // A roof plan is drawn above the player right across its row, apart from
    // its two end cells (the corner pieces are drawn at the player's level).
    for (r = 1; r < maxRows; r++)
    {
        int row = bd->facadeRow - r;
        int inner = 0, above = 0;
        for (x = bd->minX; x <= bd->maxX; x++)
        {
            VoxelCell *c;
            if (bd->maxX - bd->minX >= 2 && (x == bd->minX || x == bd->maxX))
                continue;
            if (row < bd->colTop[x - bd->minX])
                continue;
            c = CellPtr(inst, x, row);
            if (c == NULL)
                continue;
            inner++;
            if (inst->meta[c->metatile].topOpaque >= 200)
                above++;
        }
        if (inner > 0 && above == inner)
        {
            roofFrom = r;
            break;
        }
    }
    if (roofFrom < 0)
        roofFrom = (maxRows + 1) / 2;
    if (roofFrom < 1) roofFrom = 1;
    if (roofFrom > 3) roofFrom = 3;
    if (roofFrom > maxRows) roofFrom = maxRows;
    bd->wallRows = roofFrom;
    bd->roofRows = maxRows - roofFrom;
    bd->minTop = bd->facadeRow;
    for (x = bd->minX; x <= bd->maxX; x++)
        if (bd->colTop[x - bd->minX] < bd->minTop)
            bd->minTop = bd->colTop[x - bd->minX];
}

static void FindBuildings(VoxelInstance *inst)
{
    const struct MapEvents *events = inst->header->events;
    int i;

    inst->buildingCount = 0;
    if (inst->indoor || events == NULL || events->warps == NULL)
        return;

    for (i = 0; i < events->warpCount && inst->buildingCount < MAX_VOXEL_BUILDINGS; i++)
    {
        const struct WarpEvent *w = &events->warps[i];
        int wx = w->x, wy = w->y;
        int massY, top, c, prevTop;
        VoxelBuilding *bd;
        VoxelCell *cell = CellPtr(inst, wx, wy);

        if (cell == NULL || cell->cls == VC_VOID)
            continue;
        // A door is drawn in the building's own art; a warp on primary art is
        // a cave mouth, a hole or a stairway in the landscape.
        if (!inst->indoor && cell->metatile < 512)
            continue;
        massY = wy - 1;
        if (massY < 0)
            continue;
        if (!IsMassCell(inst, wx, massY))
        {
            // A walk-behind lintel: a hole in a wall over the door (gate
            // houses, the shipyard porch). At most two rows, mass either side.
            int n = 0;
            while (n < 2 && massY - n >= 0 && !IsMassCell(inst, wx, massY - n)
                   && IsMassCell(inst, wx - 1, massY - n) && IsMassCell(inst, wx + 1, massY - n))
                n++;
            if (n == 0)
                continue;
            massY -= n;
            if (massY < 0 || !IsMassCell(inst, wx, massY))
                continue;
        }
        if (CellPtr(inst, wx, massY)->building >= 0)
            continue;   // a second door of a building already claimed
        top = RunTop(inst, wx, massY, wy);

        bd = &inst->buildings[inst->buildingCount];
        memset(bd, 0, sizeof(*bd));
        bd->facadeRow = wy;
        bd->minX = bd->maxX = wx;
        bd->colTop[0] = top;

        prevTop = top;
        for (c = wx - 1; c >= 0 && bd->maxX - c < BUILDING_MAX_COLS; c--)
        {
            int ct;
            if (!ColumnRun(inst, c, wy, &ct))
                break;
            if (ct > prevTop + 1 || ct < prevTop - 1)
                break;
            bd->minX = c;
            prevTop = ct;
        }
        // colTop is indexed from minX, so fill the west side now
        for (c = bd->minX; c < wx; c++)
        {
            int ct;
            ColumnRun(inst, c, wy, &ct);
            bd->colTop[c - bd->minX] = ct;
        }
        bd->colTop[wx - bd->minX] = top;
        prevTop = top;
        for (c = wx + 1; c < inst->width && c - bd->minX < BUILDING_MAX_COLS; c++)
        {
            int ct;
            if (!ColumnRun(inst, c, wy, &ct))
                break;
            if (ct > prevTop + 1 || ct < prevTop - 1)
                break;
            bd->maxX = c;
            bd->colTop[c - bd->minX] = ct;
            prevTop = ct;
        }
        ClaimBuilding(inst, inst->buildingCount);
        printf("[Voxel] building %d on %d:%d: x %d..%d, rows %d..%d, wall %d roof %d (door at %d,%d)" "\n",
               inst->buildingCount, inst->mapGroup, inst->mapNum, bd->minX, bd->maxX, bd->minTop, bd->facadeRow,
               bd->wallRows, bd->roofRows, wx, wy);
        inst->buildingCount++;
    }
}

// ---------------------------------------------------------------------------
// Cell model
// ---------------------------------------------------------------------------
static bool PassableGround(const VoxelCell *c)
{
    return c != NULL && c->cls != VC_VOID && !(c->flags & VCF_BLOCKED)
        && (IsWalkableClass((VoxelClass)c->cls) || c->cls == VC_WATER);
}

// Carry a reference height inward from resolved cells; `want` says which
// cells need one. Water contributes its recessed surface.
static void CarryInward(VoxelInstance *inst, const bool *resolved0, bool (*want)(const VoxelCell *),
                        float *ref, uint16_t *refMeta)
{
    int pass, x, y;
    bool *resolved = malloc(inst->width * inst->height * sizeof(bool));
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};

    memcpy(resolved, resolved0, inst->width * inst->height * sizeof(bool));
    for (pass = 0; pass < 12; pass++)
    {
        bool any = false;
        for (y = 0; y < inst->height; y++)
            for (x = 0; x < inst->width; x++)
            {
                int i = y * inst->width + x, d;
                float best = 0.0f;
                uint16_t bestMeta = 0;
                bool found = false;
                if (resolved[i] || !want(&inst->cells[i]))
                    continue;
                for (d = 0; d < 4; d++)
                {
                    int nx = x + dx[d], ny = y + dy[d];
                    int j;
                    if (nx < 0 || ny < 0 || nx >= inst->width || ny >= inst->height)
                        continue;
                    j = ny * inst->width + nx;
                    if (!resolved0[j] && !resolved[j])
                        continue;
                    if (resolved0[j] || pass > 0)
                    {
                        if (!found || ref[j] < best)
                        {
                            best = ref[j];
                            bestMeta = refMeta ? refMeta[j] : 0;
                        }
                        found = true;
                    }
                }
                if (found)
                {
                    ref[i] = best;
                    if (refMeta) refMeta[i] = bestMeta;
                    resolved[i] = true;
                    any = true;
                }
            }
        if (!any)
            break;
    }
    free(resolved);
}

static bool WantAll(const VoxelCell *c) { (void)c; return true; }
static bool WantBridge(const VoxelCell *c) { return IsBridgeLike((VoxelClass)c->cls); }

static void BuildCells(VoxelInstance *inst)
{
    const struct MapLayout *layout = inst->header->mapLayout;
    int w = inst->width, h = inst->height;
    int x, y, i;
    float *ref = malloc(w * h * sizeof(float));
    bool *resolved = malloc(w * h * sizeof(bool));
    uint16_t *refMeta = malloc(w * h * sizeof(uint16_t));
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};

    // ---- A. classify ----
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            VoxelCell *c = &inst->cells[y * w + x];
            u16 block = RawBlock(inst, x, y);
            const VoxelMetaInfo *mi;
            bool standing = false, walkablePin = false;
            VoxelClass cls, pin;

            memset(c, 0, sizeof(*c));
            c->building = -1;
            c->metatile = block & MAPGRID_METATILE_ID_MASK;
            c->collision = (block & MAPGRID_COLLISION_MASK) >> MAPGRID_COLLISION_SHIFT;
            c->elevation = (block & MAPGRID_ELEVATION_MASK) >> MAPGRID_ELEVATION_SHIFT;
            mi = &inst->meta[c->metatile];
            c->behavior = mi->behavior;
            c->layerType = mi->layerType;
            if (c->collision != 0)
                c->flags |= VCF_BLOCKED;
            if (mi->topOpaque >= 64)
                c->flags |= VCF_ABOVE;

            // Interior filler is black, and a room's outermost columns are
            // often a blocked strip of near-black wall shadow: both are the
            // void the room shell should stand at, not a floor or a column.
            if (mi->allBlack
                || (inst->indoor && (c->flags & VCF_BLOCKED) && mi->avgR + mi->avgG + mi->avgB < 72))
            {
                c->cls = VC_VOID;
                continue;
            }
            cls = ClassFromBehavior(c->behavior, &standing);
            if (c->flags & VCF_BLOCKED)
            {
                if (cls == VC_GROUND || cls == VC_GRASS || cls == VC_SLOPE)
                    cls = VC_WALL;
                else if (cls == VC_DOOR)
                    cls = VC_WALL;
                else if (cls == VC_LEDGE || IsBridgeLike(cls))
                    cls = VC_WALL;
                // water, furniture keep their class
            }
            else if (standing)
            {
                cls = VC_GROUND;   // the passability guard
            }
            pin = PinFor(layout, c->metatile, &walkablePin);
            if (pin != VC_COUNT)
            {
                if ((c->flags & VCF_BLOCKED) || walkablePin)
                    cls = pin;
            }
            c->cls = cls;
        }

    // warps
    if (inst->header->events && inst->header->events->warps)
    {
        for (i = 0; i < inst->header->events->warpCount; i++)
        {
            const struct WarpEvent *wp = &inst->header->events->warps[i];
            VoxelCell *c = CellPtr(inst, wp->x, wp->y);
            if (c)
                c->flags |= VCF_WARP;
        }
    }

    // ---- B. heights ----
    BuildElevationRanks(inst);
    for (i = 0; i < w * h; i++)
    {
        VoxelCell *c = &inst->cells[i];
        bool real = inst->useElevation && c->elevation != 0 && c->elevation != 1 && c->elevation != 15;
        ref[i] = 0.0f;
        refMeta[i] = c->metatile;
        resolved[i] = false;
        if (c->cls == VC_VOID)
            continue;
        if (!(c->flags & VCF_BLOCKED) && IsWalkableClass((VoxelClass)c->cls) && !IsBridgeLike((VoxelClass)c->cls)
            && c->cls != VC_SLOPE && (real || !inst->useElevation))
        {
            ref[i] = inst->useElevation ? inst->rankH[c->elevation] : 0.0f;
            resolved[i] = true;
        }
    }
    // Transition cells, slopes, water and blocked cells take the LOWEST real
    // level around them: a ramp read low is a step the terrace above hides.
    CarryInward(inst, resolved, WantAll, ref, NULL);

    for (i = 0; i < w * h; i++)
    {
        VoxelCell *c = &inst->cells[i];
        c->groundH = ref[i];
        c->topH = ref[i];
        c->underH = ref[i];
        c->underMeta = c->metatile;
        switch (c->cls)
        {
        case VC_WATER:
            c->groundH = ref[i] - 0.125f;
            c->topH = c->groundH;
            break;
        case VC_LEDGE:
            c->topH = c->groundH + 0.375f;
            break;
        case VC_CHAIR:
            c->topH = c->groundH + 0.5f;
            break;
        default:
            if (IsFurnitureClass((VoxelClass)c->cls) && (c->flags & VCF_BLOCKED))
                c->topH = c->groundH + sClassHeight[c->cls];
            break;
        }
    }
    // Bridges: what they span comes from the nearest non-bridge surface.
    for (i = 0; i < w * h; i++)
    {
        VoxelCell *c = &inst->cells[i];
        resolved[i] = c->cls != VC_VOID && !IsBridgeLike((VoxelClass)c->cls) && !(c->flags & VCF_BLOCKED);
        ref[i] = c->groundH;
        refMeta[i] = c->metatile;
    }
    CarryInward(inst, resolved, WantBridge, ref, refMeta);
    for (i = 0; i < w * h; i++)
    {
        VoxelCell *c = &inst->cells[i];
        if (!IsBridgeLike((VoxelClass)c->cls))
            continue;
        c->underH = ref[i];
        c->underMeta = refMeta[i];
        // Land carried in is the deck's own level; water carried in sits an
        // eighth below it, so the deck rides the plank height over the bank.
        c->groundH = (c->underH < c->groundH - 0.01f) ? c->underH + 0.125f : c->groundH;
        c->topH = c->groundH + sClassHeight[c->cls];
        if (c->underH >= c->topH - 0.01f)
            c->underH = c->topH - 0.25f;
    }

    // ---- C. buildings ----
    FindBuildings(inst);
    for (i = 0; i < inst->buildingCount; i++)
    {
        VoxelBuilding *bd = &inst->buildings[i];
        float base = 0.0f;
        bool found = false;
        for (x = bd->minX; x <= bd->maxX; x++)
        {
            int top = bd->colTop[x - bd->minX];
            for (y = top; y <= bd->facadeRow; y++)
            {
                int d;
                for (d = 0; d < 4; d++)
                {
                    VoxelCell *n = CellPtr(inst, x + dx[d], y + dy[d]);
                    if (n && n->building < 0 && PassableGround(n) && n->cls != VC_WATER)
                    {
                        if (!found || n->groundH < base)
                            base = n->groundH;
                        found = true;
                    }
                }
            }
        }
        bd->baseH = base;
        for (x = bd->minX; x <= bd->maxX; x++)
            for (y = bd->colTop[x - bd->minX]; y <= bd->facadeRow; y++)
            {
                VoxelCell *c = CellPtr(inst, x, y);
                if (c == NULL) continue;
                c->groundH = base;
                c->topH = base + bd->wallRows + (bd->roofRows > 0 ? 1.0f : 0.0f);
            }
    }

    // ---- D. columns: stack blocked rows into one mass, south row first ----
    for (y = h - 1; y >= 0; y--)
        for (x = 0; x < w; x++)
        {
            VoxelCell *c = &inst->cells[y * w + x];
            int rows = 1, cap = inst->indoor ? 3 : 2, k, d;
            u16 seen[4];
            float base = 0.0f, top;
            bool haveBase = false, haveN = false, haveS = false;
            float northG = 0.0f, southG = 0.0f;
            VoxelCell *s;

            if (c->cls != VC_WALL || (c->flags & VCF_CONSUMED) || c->building >= 0)
                continue;
            seen[0] = c->metatile;
            while (rows < cap)
            {
                VoxelCell *n = CellPtr(inst, x, y - rows);
                bool dup = false;
                if (n == NULL || n->cls != VC_WALL || (n->flags & VCF_CONSUMED) || n->building >= 0)
                    break;
                for (k = 0; k < rows; k++)
                    if (seen[k] == n->metatile)
                        dup = true;
                if (dup)
                    break;
                seen[rows] = n->metatile;
                rows++;
            }
            // the ground around the whole stack
            for (k = 0; k < rows; k++)
                for (d = 0; d < 4; d++)
                {
                    VoxelCell *n = CellPtr(inst, x + dx[d], y - k + dy[d]);
                    if (n == NULL || n->cls == VC_VOID)
                        continue;
                    if (PassableGround(n) && n->cls != VC_WATER)
                    {
                        if (!haveBase || n->groundH < base)
                            base = n->groundH;
                        haveBase = true;
                    }
                }
            if (!haveBase)
                base = c->groundH;
            s = CellPtr(inst, x, y + 1);
            if (s && PassableGround(s) && s->cls != VC_WATER) { haveS = true; southG = s->groundH; }
            s = CellPtr(inst, x, y - rows);
            if (s && PassableGround(s) && s->cls != VC_WATER) { haveN = true; northG = s->groundH; }

            if (haveN && northG > base + 0.01f && (!haveS || northG > southG + 0.01f))
            {
                // the drop between two terraces
                top = northG;
                for (k = 0; k < rows; k++)
                    CellPtr(inst, x, y - k)->cls = VC_CLIFF;
            }
            else
            {
                top = base + rows;
            }
            // Indoors a wall stub on the room's edge (the Pokemon Center's
            // entrance jambs) is part of the wall around the room: give it
            // the shell's two courses rather than a one-course block.
            if (inst->indoor && top < base + 2.0f)
            {
                for (k = 0; k < rows; k++)
                {
                    VoxelCell *e = CellPtr(inst, x + 1, y - k);
                    VoxelCell *wc = CellPtr(inst, x - 1, y - k);
                    if (e == NULL || e->cls == VC_VOID || wc == NULL || wc->cls == VC_VOID)
                        top = base + 2.0f;
                }
            }
            for (k = 0; k < rows; k++)
            {
                VoxelCell *n = CellPtr(inst, x, y - k);
                n->groundH = base;
                n->topH = top;
                n->stack = 0;
                if (k > 0)
                    n->flags |= VCF_CONSUMED;
            }
            c->stack = rows;
            c->flags |= VCF_FADEABLE;
        }

    // ---- E. interior room shell ----
    if (inst->indoor)
    {
        int counts[1024];
        int best = -1;
        memset(counts, 0, sizeof(counts));
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
            {
                VoxelCell *c = &inst->cells[y * w + x];
                VoxelCell *n;
                if (c->cls == VC_VOID)
                    continue;
                if (c->flags & VCF_BLOCKED)
                {
                    n = CellPtr(inst, x, y - 1);
                    if (n == NULL || n->cls == VC_VOID)
                        counts[c->metatile] += 4;
                    else
                        counts[c->metatile] += 1;
                }
                n = CellPtr(inst, x, y - 1);
                if (n == NULL || n->cls == VC_VOID) c->flags |= VCF_SHELL_N;
                n = CellPtr(inst, x + 1, y);
                if (n == NULL || n->cls == VC_VOID) c->flags |= VCF_SHELL_E;
                n = CellPtr(inst, x - 1, y);
                if (n == NULL || n->cls == VC_VOID) c->flags |= VCF_SHELL_W;
            }
        for (i = 0; i < 1024; i++)
            if (best < 0 || counts[i] > counts[best])
                best = i;
        inst->shellMeta = best < 0 ? 0 : best;
    }

    inst->layoutHash = HashLiveMap(inst);
    free(ref);
    free(resolved);
    free(refMeta);

    if (getenv("EMERALD_VOXEL_DUMP"))
    {
        // One character per cell: void ' ', ground '.', grass ',', water '~',
        // ledge '_', deck '=', door 'D', wall '#', cliff '^', building 'B',
        // furniture 'f'; shells on the borders are shown by lower-case n/e/w
        // flags in a second grid.
        char line[256];
        printf("[Voxel] cells of %d:%d (%dx%d) indoor=%d\n", inst->mapGroup, inst->mapNum, w, h, inst->indoor);
        for (y = 0; y < h && y < 250; y++)
        {
            for (x = 0; x < w && x < 250; x++)
            {
                const VoxelCell *c = &inst->cells[y * w + x];
                char ch = '?';
                switch (c->cls)
                {
                case VC_VOID: ch = ' '; break;
                case VC_GROUND: ch = '.'; break;
                case VC_GRASS: ch = ','; break;
                case VC_WATER: ch = '~'; break;
                case VC_LEDGE: ch = '_'; break;
                case VC_BRIDGE: case VC_LOG: ch = '='; break;
                case VC_DOOR: ch = 'D'; break;
                case VC_WALL: ch = (c->flags & VCF_CONSUMED) ? '+' : '#'; break;
                case VC_CLIFF: ch = '^'; break;
                case VC_BUILDING: ch = 'B'; break;
                default: ch = 'f'; break;
                }
                line[x] = ch;
            }
            line[x] = 0;
            printf("[Voxel] %3d |%s|\n", y, line);
        }
        if (inst->indoor)
        {
            for (y = 0; y < h; y++)
            {
                const VoxelCell *a = &inst->cells[y * w];
                const VoxelCell *b = &inst->cells[y * w + w - 1];
                printf("[Voxel] edge row %d: west meta %d cls %d flags %02x avg %d | east meta %d cls %d flags %02x avg %d\n",
                       y, a->metatile, a->cls, a->flags, inst->meta[a->metatile].avgR + inst->meta[a->metatile].avgG + inst->meta[a->metatile].avgB,
                       b->metatile, b->cls, b->flags, inst->meta[b->metatile].avgR + inst->meta[b->metatile].avgG + inst->meta[b->metatile].avgB);
            }
            printf("[Voxel] shell metatile %d\n", inst->shellMeta);
        }
    }
}

static void FreeInstances(void)
{
    int i;
    for (i = 0; i < MAX_VOXEL_MAP_INSTANCES; i++)
    {
        free(gVoxelInstances[i].cells);
        gVoxelInstances[i].cells = NULL;
    }
    gVoxelInstanceCount = 0;
}

static void InitInstance(VoxelInstance *inst, const struct MapHeader *header, int group, int num, int ox, int oy)
{
    memset(inst, 0, sizeof(*inst));
    inst->header = header;
    inst->mapGroup = group;
    inst->mapNum = num;
    inst->originX = ox;
    inst->originY = oy;
    inst->width = header->mapLayout->width;
    inst->height = header->mapLayout->height;
    inst->indoor = header->mapType == MAP_TYPE_INDOOR || header->mapType == MAP_TYPE_SECRET_BASE;
    inst->useElevation = !inst->indoor;
    inst->cells = calloc(inst->width * inst->height, sizeof(VoxelCell));
}

static void BuildInstances(void)
{
    VoxelInstance *cur;
    FreeInstances();
    if (gMapHeader.mapLayout == NULL)
        return;

    cur = &gVoxelInstances[0];
    InitInstance(cur, &gMapHeader, gSaveBlock1Ptr->location.mapGroup, gSaveBlock1Ptr->location.mapNum, 0, 0);
    gVoxelInstanceCount = 1;

    if (gMapHeader.connections)
    {
        int count = gMapHeader.connections->count;
        const struct MapConnection *conn = gMapHeader.connections->connections;
        int i;
        for (i = 0; i < count; i++, conn++)
        {
            const struct MapHeader *nh;
            int ox = 0, oy = 0;
            if (gVoxelInstanceCount >= MAX_VOXEL_MAP_INSTANCES)
                break;
            if (conn->direction == CONNECTION_DIVE || conn->direction == CONNECTION_EMERGE)
                continue;
            nh = GetMapHeaderFromConnection(conn);
            if (nh == NULL || nh->mapLayout == NULL)
                continue;
            if (conn->direction == CONNECTION_NORTH)      { ox = conn->offset; oy = -nh->mapLayout->height; }
            else if (conn->direction == CONNECTION_SOUTH) { ox = conn->offset; oy = gMapHeader.mapLayout->height; }
            else if (conn->direction == CONNECTION_WEST)  { ox = -nh->mapLayout->width; oy = conn->offset; }
            else if (conn->direction == CONNECTION_EAST)  { ox = gMapHeader.mapLayout->width; oy = conn->offset; }
            else continue;
            InitInstance(&gVoxelInstances[gVoxelInstanceCount], nh, conn->mapGroup, conn->mapNum, ox, oy);
            gVoxelInstanceCount++;
        }
    }
}

void VoxelWorld_Reset(void)
{
    FreeInstances();
    sLastLayout = NULL;
    sLastGroup = 0xFF;
    sLastNum = 0xFF;
}

bool VoxelWorld_Update(void)
{
    int i;
    bool full;

    if (!VoxelWorld_IsMapAvailable())
        return false;

    full = gMapHeader.mapLayout != sLastLayout
        || gSaveBlock1Ptr->location.mapGroup != sLastGroup
        || gSaveBlock1Ptr->location.mapNum != sLastNum
        || gVoxelInstanceCount == 0;
    if (full)
    {
        BuildInstances();
        for (i = 0; i < gVoxelInstanceCount; i++)
        {
            VoxelInstance *inst = &gVoxelInstances[i];
            // Analyse the art first: classification reads the layer info.
            VoxelAtlas_DecodeFromRom(inst->header->mapLayout, NULL, 0, NULL, inst->meta);
            BuildCells(inst);
        }
        sLastLayout = gMapHeader.mapLayout;
        sLastGroup = gSaveBlock1Ptr->location.mapGroup;
        sLastNum = gSaveBlock1Ptr->location.mapNum;
        printf("[Voxel] world rebuilt: %d instances, %d buildings on %d:%d (%dx%d)\n",
               gVoxelInstanceCount, gVoxelInstances[0].buildingCount,
               sLastGroup, sLastNum, gVoxelInstances[0].width, gVoxelInstances[0].height);
        return true;
    }
    // Scripts change metatiles on the live map (doors, cut trees, puzzles).
    if (HashLiveMap(&gVoxelInstances[0]) != gVoxelInstances[0].layoutHash)
    {
        BuildCells(&gVoxelInstances[0]);
        return true;
    }
    return false;
}

#endif // NATIVE_LINUX || __ANDROID__
#endif // PLATFORM_SDL2
