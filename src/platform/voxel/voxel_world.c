#include "voxel_world.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __ANDROID__
#include <SDL.h>
#else
#include <SDL2/SDL.h>
#endif
#include "global.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "event_object_movement.h"
#include "constants/map_types.h"
#include "constants/map_groups.h"
#include "constants/maps.h"
#include "voxel_atlas.h"
#include "voxel_art.h"
#include "voxel_scene.h"
#include "voxel_building.h"
#include "voxel_roles.h"

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

// The lip of a jump ledge (6 px, standGen3Ledges), the lift of a tall-grass
// mat (GRASS_MAT, 4 px), the height a canopy plate hangs at (two courses:
// the top of an Emerald tree).
#define LEDGE_RIM   0.375f
#define GRASS_MAT   0.25f
#define CANOPY_H    2.0f

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
// HOW FAR A BRIDGE STANDS OVER WHAT IT SPANS, in courses (`bridge_lift`).
// Emerald's bridge behaviours are an ordered height index: pond LOW/MED/HIGH
// map to 0/1/2 in MetatileBehavior_GetBridgeType, which is why Route 120
// carries a low bridge across the south of its pond and a high one across the
// north. Rails (Route 119's bike rails) are planks laid at their bank.
// ---------------------------------------------------------------------------
static float LiftFor(u8 b)
{
    switch (b)
    {
    case 0x70: return 2.0f;                 // MB_BRIDGE_OVER_OCEAN
    case 0x71: return 1.0f;                 // MB_BRIDGE_OVER_POND_LOW
    case 0x72: return 2.0f;                 // MB_BRIDGE_OVER_POND_MED
    case 0x73: return 3.0f;                 // MB_BRIDGE_OVER_POND_HIGH
    case 0x7A: case 0x7B: return 2.0f;      // the MED bridge's edge pieces
    case 0x7C: case 0x7D: return 3.0f;      // the HIGH bridge's
    case 0x7F: return 2.0f;                 // MB_BIKE_BRIDGE_OVER_BARRIER
    case 0x78: return 2.0f;                 // MB_FORTREE_BRIDGE
    case 0xD3: case 0xD4: case 0xD5: case 0xD6: return 0.0f;
    default: return 1.0f;
    }
}

static bool IsRail(u8 b) { return b >= 0xD3 && b <= 0xD6; }

// The side of a ledge cell the hop leaves by: the rim is drawn there.
static u8 LipDirsFor(u8 b)
{
    switch (b)
    {
    case 0x38: return LIP_E;            // MB_JUMP_EAST
    case 0x39: return LIP_W;
    case 0x3A: return LIP_N;
    case 0x3B: return LIP_S;
    case 0x3C: return LIP_N | LIP_E;
    case 0x3D: return LIP_N | LIP_W;
    case 0x3E: return LIP_S | LIP_E;
    case 0x3F: return LIP_S | LIP_W;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Per-map profiles (data/gen3_shapes.lua `maps`): the corrections the data
// alone gets wrong.
// ---------------------------------------------------------------------------
static VoxelMapProfile ProfileFor(int group, int num)
{
    VoxelMapProfile p;
    memset(&p, 0, sizeof(p));
    p.courseScale = 1.0f;
    if (group == MAP_GROUP(MAP_ROUTE110) && num == MAP_NUM(MAP_ROUTE110))
    {
        // Route 110's elevation field is traffic control for the cycling
        // road, not terrain: the land stays on the datum and every bridge
        // cell takes its full lift over it.
        p.causeway = true;
    }
    else if (group == MAP_GROUP(MAP_FORTREE_CITY) && num == MAP_NUM(MAP_FORTREE_CITY))
    {
        // A town in the canopy: walkways at elevation 4 over trees at 3, two
        // courses apart so the walk clears the treetops, which stand one.
        p.courseScale = 2.0f;
        p.canopyWalk = true;
    }
    else if (group == MAP_GROUP(MAP_SOOTOPOLIS_CITY) && num == MAP_NUM(MAP_SOOTOPOLIS_CITY))
    {
        // MB_MOUNTAIN_TOP is the crater wall: two courses, or the city sits
        // in a saucer instead of a caldera.
        p.mountainTopCliff = true;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Metatile pins (data/gen3_shapes.lua, `metatiles`): the only way to say
// "that cell is a fridge". Keyed on the tileset that owns the id.
// ---------------------------------------------------------------------------
VoxelClass VoxelWorld_PinFor(const struct MapLayout *layout, int metatile, bool *walkablePin)
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
    if (c->cls == VC_LEDGE || c->cls == VC_GRASS)
        return c->groundH;            // the rim and the mat stand over the feet
    if (IsWalkableClass((VoxelClass)c->cls))
        return c->topH;
    if (c->cls == VC_WATER)
        return c->groundH + 0.125f;   // surfing rides on the surface
    return c->groundH;
}

// ---------------------------------------------------------------------------
// Elevation -> height. Levels are labels, not rungs: rank the ones the map
// uses and space consecutive ranks one course apart (Fortree: two), datum at
// level 3.
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
        inst->rankH[order[i]] = (float)(i - datum) * inst->profile.courseScale;
}

// ---------------------------------------------------------------------------
// Cell model
// ---------------------------------------------------------------------------
static bool PassableGround(const VoxelCell *c)
{
    return c != NULL && c->cls != VC_VOID && !(c->flags & VCF_BLOCKED)
        && (IsWalkableClass((VoxelClass)c->cls) || c->cls == VC_WATER);
}

static bool IsLand(const VoxelCell *c)
{
    return PassableGround(c) && c->cls != VC_WATER && !IsBridgeLike((VoxelClass)c->cls);
}

// Carry a reference height inward from resolved cells; `want` says which
// cells need one. `high` takes the highest neighbour (a bridge's bank),
// otherwise the lowest (the water a bridge spans, the step a ramp hides).
static void CarryInward(VoxelInstance *inst, const bool *resolved0, bool (*want)(const VoxelCell *),
                        float *ref, uint16_t *refMeta, bool high)
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
                        if (!found || (high ? ref[j] > best : ref[j] < best))
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
    float *land = malloc(w * h * sizeof(float));
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
            c->fasciaMeta = 0xFFFF;
            c->groundMeta = 0xFFFF;
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
                else if (IsBridgeLike(cls))
                    cls = VC_WALL;
                // A jump ledge is blocked in the collision field (the hop
                // carries you over it), and stays the rim it is drawn as.
                // Water and furniture keep their class too.
            }
            else if (standing)
            {
                cls = VC_GROUND;   // the passability guard
            }
            pin = VoxelWorld_PinFor(layout, c->metatile, &walkablePin);
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
        bool walkable = !(c->flags & VCF_BLOCKED) && IsWalkableClass((VoxelClass)c->cls) && c->cls != VC_SLOPE;
        ref[i] = 0.0f;
        refMeta[i] = c->metatile;
        resolved[i] = false;
        if (c->cls == VC_VOID)
            continue;
        if (walkable && !IsBridgeLike((VoxelClass)c->cls) && (real || !inst->useElevation))
        {
            ref[i] = inst->useElevation ? inst->rankH[c->elevation] : 0.0f;
            resolved[i] = true;
        }
        // A deck with a stated level (Route 119's rails, a walkway that is a
        // bridge behaviour on a terrace) stands at that level.
        else if (walkable && IsBridgeLike((VoxelClass)c->cls) && real && !inst->profile.causeway)
        {
            ref[i] = inst->rankH[c->elevation];
            resolved[i] = true;
        }
    }
    // Transition cells, slopes, water and blocked cells take the LOWEST real
    // level around them: a ramp read low is a step the terrace above hides.
    CarryInward(inst, resolved, WantAll, ref, NULL, false);

    for (i = 0; i < w * h; i++)
    {
        VoxelCell *c = &inst->cells[i];
        c->groundH = ref[i];
        c->topH = ref[i];
        c->underH = ref[i];
        c->landH = ref[i];
        c->lift = 0.0f;
        c->underMeta = c->metatile;
        switch (c->cls)
        {
        case VC_WATER:
            c->groundH = ref[i] - 0.125f;
            c->topH = c->groundH;
            break;
        case VC_LEDGE:
            c->topH = c->groundH + LEDGE_RIM;
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

    // Bridges: what they span is the lowest surface around (the water), the
    // bank they are founded on the highest land around; the deck stands its
    // lift over the bank (over the datum on a causeway map).
    for (i = 0; i < w * h; i++)
    {
        VoxelCell *c = &inst->cells[i];
        resolved[i] = c->cls != VC_VOID && !IsBridgeLike((VoxelClass)c->cls) && !(c->flags & VCF_BLOCKED);
        ref[i] = c->groundH;
        refMeta[i] = c->metatile;
    }
    CarryInward(inst, resolved, WantBridge, ref, refMeta, false);
    for (i = 0; i < w * h; i++)
    {
        VoxelCell *c = &inst->cells[i];
        resolved[i] = IsLand(c);
        land[i] = c->groundH;
    }
    CarryInward(inst, resolved, WantBridge, land, NULL, true);
    for (i = 0; i < w * h; i++)
    {
        VoxelCell *c = &inst->cells[i];
        bool real = inst->useElevation && c->elevation != 0 && c->elevation != 1 && c->elevation != 15;
        float lift;
        if (!IsBridgeLike((VoxelClass)c->cls))
            continue;
        c->underH = ref[i];
        c->underMeta = refMeta[i];
        c->landH = inst->profile.causeway ? 0.0f : land[i];
        lift = LiftFor(c->behavior);
        if (c->cls == VC_LOG)
        {
            // a raft rides just clear of the sea it floats on
            c->groundH = c->underH + 0.125f;
            c->topH = c->groundH + 0.125f;
        }
        else if ((c->elevation == 15 || inst->profile.causeway) && !IsRail(c->behavior))
        {
            c->lift = lift;
            c->groundH = c->landH + lift;
            c->topH = c->groundH;
        }
        else if (!real)
        {
            // a deck on the surf level with no lift stated: it is its bank
            c->groundH = c->landH;
            c->topH = c->groundH;
        }
        else
        {
            // a plank laid on its own terrace (Route 119's rails): what is
            // under it is the ground its own drawing shows around the rail
            c->underH = c->topH - 0.125f;
            c->underMeta = c->metatile;
        }
        if (c->underH >= c->topH - 0.01f)
            c->underH = c->topH - (IsRail(c->behavior) ? 0.125f : 0.25f);
    }

    inst->layoutHash = HashLiveMap(inst);
    free(ref);
    free(land);
    free(resolved);
    free(refMeta);
}

// A trunk row: a narrow wooden silhouette on open ground (Route 119's
// 124/169 under the 22/23 leaves and the 198/199 crown).
static bool IsTrunkArt(const VoxelArtInfo *a)
{
    return a->solid8 < 160 && a->solid8 >= 10 && a->botWidth <= 10 && a->brown >= 12 && a->green < 64;
}

static bool IsLeafyArt(const VoxelArtInfo *a)
{
    return a->green >= 64 && a->solid8 >= 64;
}

// Blocked rows stack into columns once the art is read: landscape stands at
// most two courses (GEN3_MAX_LAND), and the Fortree tileset's tall trees --
// one or two trunk rows under two leafy rows -- stand up to four. Wood only
// carries wood or leaves, rock never carries wood, so a rock under a trunk
// does not swallow it.
static void BuildColumns(VoxelInstance *inst)
{
    int w = inst->width, h = inst->height, x, y, i;
    static const int dx[4] = {0, 0, -1, 1};
    static const int dy[4] = {-1, 1, 0, 0};

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

            if (c->cls != VC_WALL || (c->flags & (VCF_CONSUMED | VCF_SCENE)) || c->building >= 0)
                continue;
            seen[0] = c->metatile;
            if (!inst->indoor && IsTrunkArt(&inst->art[c->metatile]))
                cap = 4;
            while (rows < cap)
            {
                VoxelCell *n = CellPtr(inst, x, y - rows);
                bool dup = false;
                if (n == NULL || n->cls != VC_WALL || (n->flags & (VCF_CONSUMED | VCF_SCENE)) || n->building >= 0)
                    break;
                if (!inst->indoor)
                {
                    const VoxelArtInfo *lower = &inst->art[seen[rows - 1]];
                    const VoxelArtInfo *upper = &inst->art[n->metatile];
                    bool lowerTrunk = IsTrunkArt(lower), upperTrunk = IsTrunkArt(upper), upperLeafy = IsLeafyArt(upper);
                    if (lowerTrunk && !(upperTrunk || upperLeafy))
                        break;                       // a trunk carries wood or leaves
                    if (!lowerTrunk && upperTrunk)
                        break;                       // rock or leaves never carry a trunk
                    if (cap == 4 && !lowerTrunk && !upperLeafy)
                        break;                       // above the trunk, only leaves
                    if (cap == 4 && !upperTrunk)
                    {
                        int leafy = 0;
                        for (k = 0; k < rows; k++)
                            if (IsLeafyArt(&inst->art[seen[k]])) leafy++;
                        if (leafy >= 2)
                            break;                   // two rows of leaves make the crown
                    }
                }
                for (k = 0; k < rows; k++)
                    if (seen[k] == n->metatile && !(!inst->indoor && IsLeafyArt(&inst->art[n->metatile])))
                        dup = true;                  // a wall of one repeated row stops at one, leaves may repeat
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
            // A tree or rock standing on the terrace above hides the ground
            // there, but stands at its height all the same.
            else if (s && s->cls != VC_VOID && (s->flags & VCF_BLOCKED) && s->building < 0 && IsFurnitureClass((VoxelClass)s->cls) == false)
            {
                int k2, d2;
                static const int ddx[4] = {0, 0, -1, 1};
                static const int ddy[4] = {-1, 1, 0, 0};
                float g = 0.0f;
                bool have = false;
                for (k2 = 0; k2 < 2 && !have; k2++)
                    for (d2 = 0; d2 < 4; d2++)
                    {
                        VoxelCell *n2 = CellPtr(inst, x + ddx[d2], y - rows - k2 + ddy[d2]);
                        if (n2 && PassableGround(n2) && n2->cls != VC_WATER && (!have || n2->groundH < g))
                        {
                            g = n2->groundH;
                            have = true;
                        }
                    }
                if (have) { haveN = true; northG = g; }
            }

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
            // Sootopolis: MB_MOUNTAIN_TOP is the crater wall, two courses.
            if (inst->profile.mountainTopCliff && c->behavior == 0x0C && top < base + 2.0f)
                top = base + 2.0f;
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
    (void)dx; (void)dy;
}

// ---------------------------------------------------------------------------
// Shapes: what the drawing says stands on each cell, decided from the
// silhouettes the art pass measured (voxel_art.h).
//
//   * a two-row column whose top row is green and drawn above the player
//     is a tree: a round crown over a trunk;
//   * a one-row column that is green with carved corners is a bush;
//   * a column whose silhouette leaves the ground showing is a standee: a
//     thin slab (fence, sign, post) or a small box (mailbox, crate);
//   * a low band of non-ground colour (a cliff's shadow) or a filled cell is
//     a solid box: rocks, cliffs, hedges, walls.
// ---------------------------------------------------------------------------
// A standee's thickness is its kind's (Structures.lua PINNED_DEPTH): a
// fence of rails is post-thick (6 px), a signboard on a stick is paper on a
// post (2 px), anything else is a prop (5 px).
static uint8_t StandeeDepthFor(const VoxelArtInfo *a)
{
    if (a->rails >= 2) return 6;
    if (a->plate >= 5 && a->botWidth <= 4) return 2;
    return 5;
}

#define ART_GREEN         80    // green silhouette pixels that make a row leafy
#define ART_LEAF_SOLID    96    // and enough of them drawn to be a crown

#define ART_GREEN         80    // green silhouette pixels that make a row leafy
#define ART_LEAF_SOLID    96    // and enough of them drawn to be a crown
#define ART_STANDEE_SOLID 160   // less of the cell than this drawn: an object stands in the open
#define ART_BOXY          230   // silhouette fills its bounding box: a box, not a slab

static void BuildShapes(VoxelInstance *inst)
{
    int w = inst->width, h = inst->height, x, y, k;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
        {
            VoxelCell *c = &inst->cells[y * w + x];
            const VoxelMetaInfo *mi = &inst->meta[c->metatile];
            c->shape = SH_NONE;
            c->lipDirs = 0;
            c->overhead = 0;
            if (c->cls == VC_VOID)
                continue;
            switch (c->cls)
            {
            case VC_LEDGE:
                c->shape = SH_LIP;
                c->lipDirs = LipDirsFor(c->behavior);
                break;
            case VC_GRASS:
                // Tall grass alone lifts as a mat (GRASS_MAT); long grass
                // and ash grass draw a mass and a layer of no stated height.
                if (c->behavior == 0x02 && !(c->flags & VCF_BLOCKED))
                {
                    c->shape = SH_MAT;
                    c->topH = c->groundH + GRASS_MAT;
                }
                break;
            case VC_BRIDGE:
            case VC_LOG:
                c->shape = SH_DECK;
                break;
            default:
                break;
            }
            // A walkable cell whose art is drawn above the player: an awning
            // (Slateport's market), an eave, a ladder's head. The drawn top
            // hangs as a plate two courses up. A tree's lower fringe over the
            // cell south of it is leafy and stays where the crown already is.
            if (!inst->indoor && c->building < 0 && !(c->flags & (VCF_BLOCKED | VCF_VACATED))
                && IsWalkableClass((VoxelClass)c->cls) && c->cls != VC_GRASS && c->cls != VC_DOOR
                && c->cls != VC_LEDGE && !IsBridgeLike((VoxelClass)c->cls)
                && mi->topOpaque >= 96 && inst->art[c->metatile].green < 48)
                c->overhead = 1;
        }

    for (y = h - 1; y >= 0; y--)
        for (x = 0; x < w; x++)
        {
            VoxelCell *c = &inst->cells[y * w + x];
            const VoxelArtInfo *a[4];
            const VoxelCell *rowCell[4];
            int rows = c->stack, shape = SH_BOX, solidMax = 0, bboxRows;
            bool topAbove, inMass = false;

            if ((c->cls != VC_WALL && c->cls != VC_CLIFF) || rows <= 0 || c->building >= 0 || (c->flags & VCF_SCENE))
                continue;
            if (rows > 4) rows = 4;
            for (k = 0; k < rows; k++)
            {
                rowCell[k] = CellPtr(inst, x, y - k);
                a[k] = &inst->art[rowCell[k] ? rowCell[k]->metatile : c->metatile];
                if (a[k]->solid8 > solidMax) solidMax = a[k]->solid8;
            }
            topAbove = rowCell[rows - 1] && (rowCell[rows - 1]->flags & VCF_ABOVE);
            bboxRows = a[0]->y1 >= a[0]->y0 ? a[0]->y1 - a[0]->y0 + 1 : 0;

            // A thin silhouette inside a mass of blocked cells (a crater
            // wall's shading band, a rock ledge's dark edge) is rock, not a
            // fence: a fence has open ground on at least two of its sides.
            {
                int massN = 0, openN = 0, d2, k2;
                static const int mdx[4] = {0, 0, -1, 1};
                static const int mdy[4] = {-1, 1, 0, 0};
                for (d2 = 0; d2 < 4; d2++)
                {
                    const VoxelCell *nb = CellPtr(inst, x + mdx[d2], y + mdy[d2]);
                    if (nb && (nb->flags & VCF_BLOCKED) && nb->cls != VC_VOID
                        && (nb->building >= 0 || inst->art[nb->metatile].solid8 >= ART_STANDEE_SOLID))
                        massN++;
                }
                // open sides around the whole stack (water and ledges count as open)
                for (k2 = 0; k2 < rows; k2++)
                    for (d2 = 0; d2 < 4; d2++)
                    {
                        const VoxelCell *nb = CellPtr(inst, x + mdx[d2], y - k2 + mdy[d2]);
                        if (nb == NULL || nb->cls == VC_VOID)
                            continue;
                        if (k2 > 0 && d2 == 1) continue;            // inside the stack
                        if (k2 < rows - 1 && d2 == 0) continue;
                        if (!(nb->flags & VCF_BLOCKED) || nb->cls == VC_LEDGE)
                            openN++;
                    }
                inMass = massN >= 3 || openN < 2;
            }
            if (inst->indoor)
            {
                // Indoors the carve is trusted for one thing only: a potted
                // plant, thin and green on the floor, stands as a slab. Wall
                // bands, counters and everything pinned stay boxes.
                if (rows == 1 && a[0]->green >= 40 && a[0]->solid8 < ART_STANDEE_SOLID && a[0]->solid8 >= 10 && bboxRows >= 8)
                    shape = SH_STANDEE;
                else
                    shape = SH_BOX;
            }
            else if (c->cls == VC_CLIFF || c->behavior == 0x0C)
                shape = SH_BOX;     // MB_MOUNTAIN_TOP blocked: the rock of a terrace, whatever it carves to
            else if (rows == 2 && a[1]->green >= ART_GREEN && a[1]->solid8 >= ART_LEAF_SOLID
                     && (topAbove || a[0]->brown >= 6 || a[0]->green >= 64))
                shape = SH_TREE;
            else if (rows >= 3 && a[rows - 1]->green >= ART_GREEN && a[rows - 1]->solid8 >= ART_LEAF_SOLID
                     && a[rows - 2]->green >= ART_GREEN)
                shape = SH_TREE;
            else if (rows == 1 && a[0]->green >= ART_LEAF_SOLID && a[0]->solid8 >= 100
                     && a[0]->solid8 < 236 && a[0]->boxy < ART_BOXY)
                shape = SH_BUSH;
            else if (solidMax < 10)
                shape = SH_NONE;          // a blocked cell painted as ground: nothing stands here
            else if (rows == 2 && a[0]->solid8 < ART_STANDEE_SOLID && a[1]->solid8 < ART_STANDEE_SOLID
                     && a[1]->solid8 >= 10 && !inMass)
                shape = SH_STANDEE_TALL;
            else if (rows == 1 && a[0]->solid8 < ART_STANDEE_SOLID && bboxRows >= 8 && !inMass)
                shape = (a[0]->boxy >= ART_BOXY && a[0]->solid8 >= 40) ? SH_STANDEE_BOX : SH_STANDEE;
            else
                shape = SH_BOX;

            if (inst->profile.mountainTopCliff && c->behavior == 0x0C)
                shape = SH_BOX;
            c->shape = shape;
            c->depthPx = StandeeDepthFor(a[0]);
            if (shape == SH_NONE)
            {
                for (k = 0; k < rows; k++)
                    if (rowCell[k])
                        ((VoxelCell *)rowCell[k])->topH = c->groundH;
            }
            // In the canopy town the trees stand one course under the walkways.
            if (shape == SH_TREE && inst->profile.canopyWalk)
                for (k = 0; k < rows; k++)
                    if (rowCell[k])
                        ((VoxelCell *)rowCell[k])->topH = c->groundH + 1.0f;

            // A one-row blocked cell south of a lifted deck is the deck's
            // drawn underside (Route 110's timber piers): its art becomes
            // the deck's fascia and the cell itself lies flat.
            if (rows == 1 && shape != SH_TREE && shape != SH_BUSH)
            {
                VoxelCell *n = CellPtr(inst, x, y - 1);
                if (n && n->shape == SH_DECK && n->lift >= 1.0f && c->topH < n->topH - 0.01f)
                {
                    c->shape = SH_SUPPORT;
                    c->topH = c->groundH;
                    n->fasciaMeta = c->metatile;
                }
            }
        }
}

// A metatile is one thing wherever the map lays it: a fence piece walled in
// by rocks on three sides is still the fence it is everywhere else. Every
// one-row column takes the shape most of its metatile's columns took.
static void SettleShapesByMetatile(VoxelInstance *inst)
{
    static uint16_t votes[1024][3];   // box, standee, standee box
    int i, m;
    memset(votes, 0, sizeof(votes));
    for (i = 0; i < inst->width * inst->height; i++)
    {
        const VoxelCell *c = &inst->cells[i];
        if (c->stack != 1 || c->building >= 0 || (c->cls != VC_WALL))
            continue;
        if (c->shape == SH_BOX) votes[c->metatile][0]++;
        else if (c->shape == SH_STANDEE) votes[c->metatile][1]++;
        else if (c->shape == SH_STANDEE_BOX) votes[c->metatile][2]++;
    }
    for (i = 0; i < inst->width * inst->height; i++)
    {
        VoxelCell *c = &inst->cells[i];
        int best = 0, k;
        if (c->stack != 1 || c->building >= 0 || c->cls != VC_WALL)
            continue;
        if (c->shape != SH_BOX && c->shape != SH_STANDEE && c->shape != SH_STANDEE_BOX)
            continue;
        m = c->metatile;
        for (k = 1; k < 3; k++)
            if (votes[m][k] > votes[m][best])
                best = k;
        if (votes[m][0] + votes[m][1] + votes[m][2] < 2)
            continue;
        c->shape = best == 0 ? SH_BOX : (best == 1 ? SH_STANDEE : SH_STANDEE_BOX);
        c->depthPx = StandeeDepthFor(&inst->art[m]);
        if (c->shape == SH_BOX && c->topH < c->groundH + 0.99f)
            c->topH = c->groundH + 1.0f;
    }
}

// Metatiles laid on walkable land: their bottom-layer colours are the ground.
static void MarkFloorMetatiles(VoxelInstance *inst)
{
    int i;
    memset(inst->usedByFloor, 0, sizeof(inst->usedByFloor));
    for (i = 0; i < inst->width * inst->height; i++)
    {
        const VoxelCell *c = &inst->cells[i];
        if (c->building >= 0)
            continue;          // under a building's eave: its art is the roof's
        if (c->cls == VC_GROUND || c->cls == VC_GRASS || c->cls == VC_SLOPE
            || (c->cls == VC_LEDGE && !(c->flags & VCF_BLOCKED)))
            inst->usedByFloor[c->metatile] = true;
    }
}

// ---------------------------------------------------------------------------
// Scenery (voxel_scene.h) onto the cells: which hull each claimed cell
// belongs to (Structures.lua buildCylinders), and the height it stands at.
// ---------------------------------------------------------------------------

// A TREE STANDS IN THE GROUND IT GROWS OUT OF: the lowest walkable land
// beside it; a cell with none keeps its own reading (`grounded` false).
static float StandZ(VoxelInstance *inst, int x, int y, bool *grounded)
{
    static const int dx[4] = {1, -1, 0, 0};
    static const int dy[4] = {0, 0, 1, -1};
    float best = 0.0f;
    bool have = false;
    int d;
    for (d = 0; d < 4; d++)
    {
        VoxelCell *n = CellPtr(inst, x + dx[d], y + dy[d]);
        if (n == NULL || (n->flags & VCF_BLOCKED) || n->cls == VC_VOID || n->cls == VC_WATER)
            continue;
        if (!have || n->groundH < best)
            best = n->groundH;
        have = true;
    }
    if (grounded) *grounded = have;
    if (have)
        return best;
    {
        VoxelCell *c = CellPtr(inst, x, y);
        return c ? c->groundH : 0.0f;
    }
}

static int SceneGridIdx(const VoxelInstance *inst, int x, int y)
{
    int gx = x + VOXEL_SCENE_RING, gy = y + VOXEL_SCENE_RING;
    if (gx < 0 || gy < 0 || gx >= inst->sceneW || gy >= inst->sceneH)
        return -1;
    return gy * inst->sceneW + gx;
}

static bool RoundAt(const VoxelInstance *inst, int x, int y)
{
    int s = VoxelScene_At(inst, x, y);
    return s == SC_CANOPY || s == SC_CYLINDER;
}

// Mark cell (x, y) of the body as belonging to a hull anchored at (ax, ay).
static void ClaimForHull(VoxelInstance *inst, int x, int y, int ax, int ay)
{
    VoxelCell *c = CellPtr(inst, x, y);
    bool grounded;
    float z;
    if (c == NULL || c->cls == VC_VOID)
        return;
    if (c->flags & VCF_BLOCKED)
        c->flags |= VCF_SCENE;
    else
        c->flags |= VCF_VACATED;
    c->shape = SH_HULL;
    // per cell: a quarter with no ground of its own takes the stamp's
    z = StandZ(inst, x, y, &grounded);
    if (!grounded)
        z = StandZ(inst, ax, ay, NULL);
    c->groundH = z;
    c->topH = z;
}

// ---------------------------------------------------------------------------
// A LAMP IS A HEAD ON A POST (Structures.lua buildGen3Lamps): a thin shaft
// drawn in a blocked cell under a compact head drawn in the walkable cell
// above it -- you pass behind the head -- stood up as one 16x32 hull.
// ---------------------------------------------------------------------------
#define LAMP_SHAFT_MAX 5
#define LAMP_HEAD_MIN  8
#define LAMP_EDGE_SLOP 2

static bool CarvedPx(const VoxelInstance *inst, int m, int x, int y)
{
    int ax = (m % VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + x;
    int ay = (m / VOXEL_ATLAS_COLS) * VOXEL_ATLAS_TILE + VOXEL_ATLAS_QUAD + y;
    return (inst->pixels[ay * VOXEL_ATLAS_SIZE + ax] >> 24) != 0;
}

static int RowInk(const VoxelInstance *inst, int m, int y, int *lo, int *hi)
{
    int x, n = 0;
    *lo = 99; *hi = -1;
    for (x = 0; x < 16; x++)
        if (CarvedPx(inst, m, x, y))
        {
            n++;
            if (x < *lo) *lo = x;
            if (x > *hi) *hi = x;
        }
    return n;
}

static bool Inset(const VoxelInstance *inst, int m)
{
    static const int cols[4] = {0, 1, 14, 15};
    int y, k, n = 0;
    for (y = 0; y < 16; y++)
        for (k = 0; k < 4; k++)
            if (CarvedPx(inst, m, cols[k], y))
                n++;
    return n <= LAMP_EDGE_SLOP;
}

static bool OpenGround(const VoxelCell *c)
{
    return c && !(c->flags & VCF_BLOCKED) && (c->cls == VC_GROUND || c->cls == VC_GRASS);
}

static bool LampAt(VoxelInstance *inst, int x, int y)
{
    VoxelCell *post = CellPtr(inst, x, y), *head = CellPtr(inst, x, y - 1);
    int rows = 0, shaftMax = 0, shaftLo = -1, shaftHi = -1, k, lo, hi;
    int headMax = 0, headMaxY = -1, headRows = 0, bn, blo, bhi;
    int sc;
    if (inst->indoor || post == NULL || head == NULL || !(post->flags & VCF_BLOCKED) || post->building >= 0
        || head->building >= 0 || !OpenGround(head))
        return false;
    sc = VoxelScene_At(inst, x, y);
    if (sc != SC_NONE && sc != SC_FENCE)
        return false;
    for (k = 0; k < 16; k++)
    {
        int n = RowInk(inst, post->metatile, k, &lo, &hi);
        if (n > 0) rows++;
        if (k < 8)
        {
            if (n > shaftMax) shaftMax = n;
            if (shaftLo < 0 && n > 0) { shaftLo = lo; shaftHi = hi; }
        }
    }
    if (!(rows >= 12 && shaftMax <= LAMP_SHAFT_MAX && shaftLo >= 0))
        return false;
    for (k = 0; k < 16; k++)
    {
        int n = RowInk(inst, head->metatile, k, &lo, &hi);
        if (n > 0) headRows++;
        if (n > headMax) { headMax = n; headMaxY = k; }
    }
    bn = RowInk(inst, head->metatile, 15, &blo, &bhi);
    if (!(bn > 0 && blo <= shaftHi + 2 && bhi >= shaftLo - 2))
        return false;
    // the head stands alone in its cell; the shaft may lean on open ground
    if (!((Inset(inst, post->metatile) || (OpenGround(CellPtr(inst, x - 1, y)) && OpenGround(CellPtr(inst, x + 1, y))))
          && Inset(inst, head->metatile)))
        return false;
    return headMax >= LAMP_HEAD_MIN && headMax >= 2 * shaftMax && headMaxY >= 0 && headMaxY <= 9 && headRows >= 8;
}

static void ApplyScenery(VoxelInstance *inst)
{
    int x, y, i;
    uint8_t *grouped;
    free(inst->hullGrid);
    inst->hullGrid = calloc(inst->sceneW * inst->sceneH, 1);
    grouped = calloc(inst->sceneW * inst->sceneH, 1);
    for (i = 0; i < inst->width * inst->height; i++)
        inst->cells[i].hull = VH_NONE;
    if (inst->hullGrid == NULL || grouped == NULL || inst->scene == NULL)
    {
        free(grouped);
        return;
    }

    for (y = -VOXEL_SCENE_RING; y < inst->height + VOXEL_SCENE_RING; y++)
        for (x = -VOXEL_SCENE_RING; x < inst->width + VOXEL_SCENE_RING; x++)
        {
            int gi = SceneGridIdx(inst, x, y), sc = VoxelScene_At(inst, x, y);
            VoxelCell *c = CellPtr(inst, x, y);
            if (c && LampAt(inst, x, y))
            {
                VoxelCell *head = CellPtr(inst, x, y - 1);
                inst->hullGrid[gi] = VH_STACKED;
                inst->hullGrid[SceneGridIdx(inst, x, y - 1)] = VH_PARTNER;
                inst->scene[gi] = SC_CYLINDER;
                ClaimForHull(inst, x, y, x, y);
                c->hull = VH_STACKED;
                ClaimForHull(inst, x, y - 1, x, y);
                head->hull = VH_PARTNER;
                continue;
            }
            if (sc == SC_NONE || (c && (c->building >= 0 || c->cls == VC_VOID)))
                continue;
            if (grouped[gi] && !(sc == SC_CANOPY && VoxelScene_SharedAt(inst, x, y)))
                continue;
            if (sc == SC_CANOPY)
            {
                int span = VoxelScene_SpanAt(inst, x, y), dx, dy;
                bool whole = true;
                if (span < 2) span = 2;
                for (dy = 0; dy < span; dy++)
                    for (dx = 0; dx < span; dx++)
                        if ((dx || dy) && !RoundAt(inst, x + dx, y + dy))
                            whole = false;
                if (!whole)
                    continue;
                inst->hullGrid[gi] = VH_CANOPY;
                for (dy = 0; dy < span; dy++)
                    for (dx = 0; dx < span; dx++)
                    {
                        int gj = SceneGridIdx(inst, x + dx, y + dy);
                        VoxelCell *p = CellPtr(inst, x + dx, y + dy);
                        if (gj < 0) continue;
                        if (dx || dy)
                        {
                            grouped[gj] = 1;
                            if (inst->hullGrid[gj] == VH_NONE)
                                inst->hullGrid[gj] = VH_PARTNER;
                        }
                        if (p && p->building < 0)
                        {
                            ClaimForHull(inst, x + dx, y + dy, x, y);
                            p->hull = (dx || dy) ? (p->hull == VH_CANOPY ? VH_CANOPY : VH_PARTNER) : VH_CANOPY;
                        }
                    }
            }
            else if (sc == SC_CYLINDER)
            {
                // A TREE IN A STAND IS TWO CELLS TALL when a crown is drawn
                // over it: leafy, and either full-bleed or all overhang.
                int mB = VoxelScene_MetatileAt(inst, x, y - 1);
                bool stacked = false, crownWalk = false;
                if (!inst->indoor && mB >= 0 && inst->used[mB] && inst->art[mB].leafy
                    && SceneGridIdx(inst, x, y - 1) >= 0)
                {
                    const VoxelRole *r = VoxelRoles_Find(inst->header->mapLayout->primaryTileset,
                                                         inst->header->mapLayout->secondaryTileset, mB);
                    bool fullBleed = inst->art[mB].solid >= 0.99f;
                    bool allOverhang = r && r->material == ROLE_MAT_GREEN && r->cap == 0 && r->face > 0;
                    bool blockedAbove = inst->profile.canopyWalk;
                    float floorZ = c ? c->groundH : 0.0f;
                    static const int ndx[5] = {0, 1, -1, 0, 0};
                    static const int ndy[5] = {0, 0, 0, 1, -1};
                    int d;
                    for (d = 0; d < 5; d++)
                    {
                        VoxelCell *n = CellPtr(inst, x + ndx[d], y + ndy[d]);
                        if (n && !(n->flags & VCF_BLOCKED) && n->cls != VC_VOID && n->groundH >= floorZ + 1.0f)
                            blockedAbove = true;
                    }
                    if ((fullBleed || allOverhang) && !blockedAbove)
                    {
                        VoxelCell *north = CellPtr(inst, x, y - 1);
                        stacked = true;
                        crownWalk = north && !(north->flags & VCF_BLOCKED);
                    }
                }
                inst->hullGrid[gi] = stacked ? VH_STACKED : VH_SINGLE;
                if (c && c->building < 0)
                {
                    ClaimForHull(inst, x, y, x, y);
                    c->hull = inst->hullGrid[gi];
                }
                if (crownWalk)
                {
                    VoxelCell *north = CellPtr(inst, x, y - 1);
                    if (north && north->building < 0 && north->hull == VH_NONE)
                    {
                        ClaimForHull(inst, x, y - 1, x, y);
                        north->hull = VH_PARTNER;
                    }
                }
            }
            else if (sc == SC_SIGNPOST && c && (c->flags & VCF_BLOCKED))
            {
                bool grounded;
                c->flags |= VCF_SCENE;
                c->shape = SH_SIGN;
                c->groundH = c->topH = StandZ(inst, x, y, &grounded);
            }
        }
    free(grouped);
}

// The whole model for one instance: art, cells, silhouettes, shapes.
static void RebuildInstance(VoxelInstance *inst)
{
    const uint16_t *live = NULL;
    int liveCount = 0;
    if (inst == &gVoxelInstances[0] && gBackupMapLayout.map)
    {
        live = gBackupMapLayout.map;
        liveCount = gBackupMapLayout.width * gBackupMapLayout.height;
    }
    // The art first: classification reads the layer info it measures.
    VoxelAtlas_DecodeFromRom(inst->header->mapLayout, live, liveCount, inst->pixels, inst->meta, inst->topMask, inst->used);
    BuildCells(inst);
    MarkFloorMetatiles(inst);   // needs the classes, not the columns
    if (inst->pixels && inst->topMask)
    {
        // THE FLOOR OF THIS PARTICULAR MAP (Gen3.lua floorMetatiles): the
        // metatiles laid on walkable cells, commonest first, kept while they
        // cover the bulk of the walkable area -- at most twelve, each laid
        // at least three times or still inside the first 60%.
        static uint8_t layerType[1024];
        static bool floorSet[1024];
        static int counts[1024];
        int order[1024], n = 0, total = 0, acc = 0, i, j, m;
        VoxelArtInput ai;
        memset(counts, 0, sizeof(counts));
        memset(floorSet, 0, sizeof(floorSet));
        for (i = 0; i < inst->width * inst->height; i++)
            if (!(inst->cells[i].flags & VCF_BLOCKED) && inst->cells[i].cls != VC_VOID)
            {
                counts[inst->cells[i].metatile]++;
                total++;
            }
        for (m = 0; m < 1024; m++)
            if (counts[m] > 0)
                order[n++] = m;
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && counts[order[j]] > counts[order[j - 1]]; j--)
            {
                int t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
            }
        for (i = 0; i < n && i < 12; i++)
            if (counts[order[i]] >= 3 || acc * 10 < total * 6)
            {
                floorSet[order[i]] = true;
                acc += counts[order[i]];
            }
        for (m = 0; m < 1024; m++)
            layerType[m] = inst->meta[m].layerType;
        ai.used = inst->used;
        ai.layerType = layerType;
        ai.floor = floorSet;
        ai.topMask = inst->topMask;
        ai.background = inst->background;
        VoxelArt_Build(&ai, inst->pixels, inst->art);
        inst->floorCount = 0;
        for (i = 0; i < n && inst->floorCount < 12; i++)
            if (floorSet[order[i]])
                inst->floorMetas[inst->floorCount++] = order[i];
        // Scenery first (a roof is never scenery), then the buildings (a
        // building's cell is never scenery), then the scenery that is left.
        VoxelScene_Classify(inst);
        VoxelBuildings_Find(inst);
        ApplyScenery(inst);     // hulls and signs leave the column machinery
    }
    else
        memset(inst->art, 0, sizeof(inst->art));
    BuildColumns(inst);         // stacks read the art: wood under leaves, never under rock
    BuildShapes(inst);
    SettleShapesByMetatile(inst);
    // BuildShapes resets every cell's shape: put the scenery's back
    {
        int i;
        for (i = 0; i < inst->width * inst->height; i++)
        {
            VoxelCell *c = &inst->cells[i];
            if (c->hull != VH_NONE)
                c->shape = SH_HULL;
            else if (c->flags & VCF_SCENE)
                c->shape = SH_SIGN;
        }
    }

    if (getenv("EMERALD_VOXEL_DUMP"))
        VoxelWorld_DumpInstance(inst);
}

// The cell model as text, for the harness and for logcat: one character per
// cell (void ' ', ground '.', grass ',', water '~', ledge '_', deck '=',
// door 'D', wall '#' or '+' when stacked into the row south of it, cliff
// '^', building 'B', furniture 'f'), then a second grid of shapes (tree 'T',
// bush 'b', standee '|', standee box 'o', tall standee 'I', box '#', lip '_',
// mat ',', deck '=', support 'u', canopy over a walkable cell '^').
void VoxelWorld_DumpInstance(const VoxelInstance *inst)
{
    int w = inst->width, h = inst->height, x, y;
    char line[256];
    SDL_Log("[Voxel] cells of %d:%d (%dx%d) indoor=%d origin (%d,%d)", inst->mapGroup, inst->mapNum, w, h,
            inst->indoor, inst->originX, inst->originY);
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
        SDL_Log("[Voxel] %3d |%s|", y, line);
    }
    SDL_Log("[Voxel] shapes of %d:%d", inst->mapGroup, inst->mapNum);
    for (y = 0; y < h && y < 250; y++)
    {
        for (x = 0; x < w && x < 250; x++)
        {
            const VoxelCell *c = &inst->cells[y * w + x];
            char ch = ' ';
            switch (c->shape)
            {
            case SH_TREE: ch = 'T'; break;
            case SH_BUSH: ch = 'b'; break;
            case SH_STANDEE: ch = '|'; break;
            case SH_STANDEE_BOX: ch = 'o'; break;
            case SH_STANDEE_TALL: ch = 'I'; break;
            case SH_BOX: ch = '#'; break;
            case SH_LIP: ch = '_'; break;
            case SH_MAT: ch = ','; break;
            case SH_DECK: ch = '='; break;
            case SH_SUPPORT: ch = 'u'; break;
            default: ch = c->overhead ? '^' : ((c->flags & VCF_CONSUMED) ? '+' : (c->cls == VC_VOID ? ' ' : '.')); break;
            }
            line[x] = ch;
        }
        line[x] = 0;
        SDL_Log("[Voxel] %3d |%s|", y, line);
    }
    if (inst->indoor)
        SDL_Log("[Voxel] shell metatile %d", inst->shellMeta);
}

// The player's own cell, for the same log.
void VoxelWorld_DumpPlayerCell(void)
{
    int px, py;
    const VoxelCell *c;
    struct ObjectEvent *playerObj = &gObjectEvents[gPlayerAvatar.objectEventId];
    px = playerObj->currentCoords.x - MAP_OFFSET;
    py = playerObj->currentCoords.y - MAP_OFFSET_Y;
    c = VoxelWorld_CellAt(px, py);
    if (c == NULL)
    {
        SDL_Log("[Voxel] player at (%d,%d): no cell", px, py);
        return;
    }
    SDL_Log("[Voxel] player at (%d,%d) elev %d: meta %d beh %02x cls %d shape %d flags %02x ground %.3f top %.3f walk %.3f stack %d",
            px, py, playerObj->currentElevation, c->metatile, c->behavior, c->cls, c->shape, c->flags, c->groundH, c->topH,
            VoxelWorld_WalkHeightAt(px, py), c->stack);
}

// The model as JSON, one cell per line, for the gate scripts.
void VoxelWorld_WriteCellsJson(FILE *f, const VoxelInstance *inst)
{
    int x, y, m, first = 1;
    fprintf(f, "{\"map\":[%d,%d],\"w\":%d,\"h\":%d,\"origin\":[%d,%d],\"indoor\":%s,\"useElevation\":%s,\n",
            inst->mapGroup, inst->mapNum, inst->width, inst->height, inst->originX, inst->originY,
            inst->indoor ? "true" : "false", inst->useElevation ? "true" : "false");
    fprintf(f, "\"profile\":{\"causeway\":%s,\"mountainTopCliff\":%s,\"courseScale\":%.2f,\"canopyWalk\":%s},\n",
            inst->profile.causeway ? "true" : "false", inst->profile.mountainTopCliff ? "true" : "false",
            inst->profile.courseScale, inst->profile.canopyWalk ? "true" : "false");
    fprintf(f, "\"rankH\":[");
    for (m = 0; m < 16; m++)
        fprintf(f, "%s%.2f", m ? "," : "", inst->rankH[m]);
    fprintf(f, "],\n\"buildings\":[");
    for (m = 0; m < inst->buildingCount; m++)
    {
        const VoxelBuilding *b = &inst->buildings[m];
        fprintf(f, "%s{\"minX\":%d,\"maxX\":%d,\"minY\":%d,\"maxY\":%d,\"door\":[%d,%d],\"rows\":%d,\"roofRows\":%d,"
                   "\"overhead\":%d,\"roofed\":%d,\"flat\":%d,\"base\":%.3f,\"eave\":%.3f,\"peak\":%.3f",
                m ? "," : "", b->minX, b->maxX, b->minY, b->maxY, b->doorX, b->doorY, b->bldRows, b->roofRows,
                b->overheadRows, b->roofed, b->flatRoof, b->base, b->eave, b->peak);
        {
            int c;
            fprintf(f, ",\"cols\":[");
            for (c = 0; c <= b->maxX - b->minX && c < BLD_COLS; c++)
                fprintf(f, "%s[%d,%d]", c ? "," : "", b->colTop[c], b->colFront[c]);
            fprintf(f, "],\"artAbove\":%d}", b->roofArtAbove);
        }
    }
    fprintf(f, "],\n\"art\":[");
    for (m = 0; m < 1024; m++)
    {
        const VoxelArtInfo *a = &inst->art[m];
        const VoxelMetaInfo *mi = &inst->meta[m];
        const VoxelRole *role;
        if (!inst->used[m])
            continue;
        role = VoxelRoles_Find(inst->header->mapLayout->primaryTileset, inst->header->mapLayout->secondaryTileset, m);
        fprintf(f, "%s\n{\"m\":%d,\"floor\":%s,\"beh\":%d,\"layer\":%d,\"topOpaque\":%d,\"botOpaque\":%d,\"solid\":%d,\"green\":%d,\"brown\":%d,\"boxy\":%d,"
                   "\"x0\":%d,\"x1\":%d,\"y0\":%d,\"y1\":%d,\"topW\":%d,\"midW\":%d,\"botW\":%d,\"bgFill\":%d,\"rails\":%d,\"plate\":%d,"
                   "\"n1\":%d,\"n2\":%d,\"solidF\":%.3f,\"leafFrac\":%.3f,\"leafFrac2\":%.3f,\"leafy\":%d,\"overhead\":%d,\"overhang\":%d,"
                   "\"mapSolid\":%.3f,\"mrgb\":[%.2f,%.2f,%.2f],\"role\":[%d,%d,%d,%d]}",
                first ? "" : ",", m, inst->usedByFloor[m] ? "true" : "false", mi->behavior, mi->layerType, mi->topOpaque, mi->botOpaque,
                a->solid8, a->green, a->brown, a->boxy, a->x0, a->x1, a->y0, a->y1, a->topWidth, a->midWidth, a->botWidth, a->bgFill, a->rails, a->plate,
                a->n1, a->n2, a->solid, a->leafFrac, a->leafFrac2, a->leafy, a->overhead, a->overhang, a->mapSolid, a->mr, a->mg, a->mb,
                role ? role->art : -1, role ? role->material : -1, role ? role->cap : -1, role ? role->face : -1);
        first = 0;
    }
    fprintf(f, "],\n\"cells\":[");
    first = 1;
    for (y = 0; y < inst->height; y++)
        for (x = 0; x < inst->width; x++)
        {
            const VoxelCell *c = &inst->cells[y * inst->width + x];
            fprintf(f, "%s\n{\"x\":%d,\"y\":%d,\"m\":%d,\"beh\":%d,\"col\":%d,\"e\":%d,\"cls\":%d,\"fl\":%d,\"sh\":%d,\"st\":%d,"
                       "\"lip\":%d,\"ov\":%d,\"g\":%.3f,\"t\":%.3f,\"u\":%.3f,\"land\":%.3f,\"lift\":%.2f,\"um\":%d,\"fm\":%d,\"bd\":%d,\"sd\":%d,\"sc\":%d,\"hull\":%d}",
                    first ? "" : ",", x, y, c->metatile, c->behavior, c->collision, c->elevation, c->cls, c->flags, c->shape, c->stack,
                    c->lipDirs, c->overhead, c->groundH, c->topH, c->underH, c->landH, c->lift, c->underMeta,
                    c->fasciaMeta == 0xFFFF ? -1 : c->fasciaMeta, c->building, c->depthPx, VoxelScene_At(inst, x, y), c->hull);
            first = 0;
        }
    fprintf(f, "]}\n");
}

static void FreeInstances(void)
{
    int i;
    for (i = 0; i < MAX_VOXEL_MAP_INSTANCES; i++)
    {
        free(gVoxelInstances[i].cells);
        free(gVoxelInstances[i].pixels);
        free(gVoxelInstances[i].topMask);
        free(gVoxelInstances[i].scene);
        free(gVoxelInstances[i].sceneSpan);
        free(gVoxelInstances[i].sceneShared);
        free(gVoxelInstances[i].hullGrid);
        gVoxelInstances[i].cells = NULL;
        gVoxelInstances[i].pixels = NULL;
        gVoxelInstances[i].topMask = NULL;
        gVoxelInstances[i].scene = NULL;
        gVoxelInstances[i].sceneSpan = NULL;
        gVoxelInstances[i].sceneShared = NULL;
        gVoxelInstances[i].hullGrid = NULL;
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
    inst->profile = ProfileFor(group, num);
    inst->useElevation = !inst->indoor && !inst->profile.causeway;
    inst->cells = calloc(inst->width * inst->height, sizeof(VoxelCell));
    inst->pixels = malloc((size_t)VOXEL_ATLAS_SIZE * VOXEL_ATLAS_SIZE * 4);
    inst->topMask = malloc(1024 * 32);
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
            RebuildInstance(&gVoxelInstances[i]);
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
        RebuildInstance(&gVoxelInstances[0]);
        return true;
    }
    return false;
}

#endif // NATIVE_LINUX || __ANDROID__
#endif // PLATFORM_SDL2
