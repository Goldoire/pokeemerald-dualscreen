#include "voxel_battle.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <math.h>
#include <string.h>
#include "global.h"
#include "fieldmap.h"
#include "field_player_avatar.h"
#include "event_object_movement.h"
#include "voxel_world.h"

// ---------------------------------------------------------------------------
// The rig, in world pixels (16 to a cell), solved with lib/BattleCam.lua's
// six equations for Emerald's own feet: the player's Pokemon lands at
// (72, 112) and the foe at (176, 72) on the 240x160 screen, a cell spanning
// 64 px at the player and 56 at the foe -- so the game's own health boxes,
// move animations and shadows sit where they always did.
// ---------------------------------------------------------------------------
typedef struct { float side, back, height, lookX, lookY, frameH; } Rig;
static const Rig sRigs[2] = {
    { 97.242f, 169.612f, 44.535f, -2.490f, 2.098f, 37.289f },   // tele, on Emerald's own feet
    // WIDE (BattleCam.lua): the close rig for a map with no long clear
    // sightline -- a clearing ringed by trees. Its marks do not land on the
    // GBA's own, so it is only taken where the tele rig cannot see.
    { 41.98f, 41.16f, 28.48f, -3.24f, -1.35f, 55.62f },
};
#define RIG_SIDE   (sRigs[0].side)
#define RIG_BACK   (sRigs[0].back)

// The two arena shapes (BattleArena.lua), offsets from the north-west
// corner: a wide one with room round the pair, a narrow one that fits a
// path. Both put the pair three cells apart on a north-south line.
typedef struct { int w, h, fx, fy, ax, ay; } Shape;
static const Shape sShapes[2] = {
    { 3, 6, 1, 1, 1, 4 },
    { 1, 4, 0, 0, 0, 3 },
};

static bool OpenCell(const VoxelInstance *inst, int x, int y, bool surfing)
{
    const VoxelCell *c;
    if (x < 0 || y < 0 || x >= inst->width || y >= inst->height)
        return false;
    c = &inst->cells[y * inst->width + x];
    if (c->flags & VCF_WARP)
        return false;
    if (c->cls == VC_WATER)
        return surfing && !(c->flags & VCF_BLOCKED);
    if (c->flags & VCF_BLOCKED)
        return false;
    return c->cls != VC_VOID && c->cls != VC_DOOR;
}

// What is drawn standing at a world cell, its top in world units: a column's
// top, a building's roof, a hull's crown (BattleArena.lua drawnTop).
static float DrawnTop(int wx, int wz)
{
    const VoxelCell *c = VoxelWorld_CellAt(wx, wz);
    const VoxelInstance *inst = VoxelWorld_InstanceAt(wx, wz);
    if (c == NULL || inst == NULL)
        return 2.0f;       // off every map: the border's trees
    if (c->building >= 0 && c->building < inst->buildingCount)
        return inst->buildings[c->building].peak;
    switch (c->hull)
    {
    case VH_SINGLE: return c->groundH + 1.0f;
    case VH_STACKED: case VH_CANOPY: case VH_PARTNER: return c->groundH + 2.0f;
    }
    return c->topH > c->groundH ? c->topH : c->groundH;
}

#define CLEAR_EPS (1.5f / 16.0f)

// CLEARANCE: the eye looks in over the map, and the rays from it to each
// battler -- at its feet, its middle and its head -- clear everything drawn
// between (BattleArena.lua clearance).
static bool Clear(const VoxelArena *a)
{
    float eye[3], focus[3], fov;
    static const float heights[3] = {1.0f / 16.0f, 8.0f / 16.0f, 16.0f / 16.0f};
    int mark, h;
    VoxelBattle_Rig(a, eye, focus, &fov);
    if (VoxelWorld_CellAt((int)floorf(eye[0]), (int)floorf(eye[2])) == NULL)
        return false;
    if (DrawnTop((int)floorf(eye[0]), (int)floorf(eye[2])) > eye[1] - CLEAR_EPS)
        return false;
    for (mark = 0; mark < 2; mark++)
    {
        float mx = mark ? a->foeX : a->allyX, mz = mark ? a->foeZ : a->allyZ;
        for (h = 0; h < 3; h++)
        {
            float my = a->groundY + heights[h], dx = mx - eye[0], dy = my - eye[1], dz = mz - eye[2];
            float len = sqrtf(dx * dx + dz * dz), t;
            int steps = (int)(len / 0.25f), k;
            for (k = 1; k < steps; k++)
            {
                float px, py, pz;
                t = (float)k / steps;
                px = eye[0] + dx * t; py = eye[1] + dy * t; pz = eye[2] + dz * t;
                if (DrawnTop((int)floorf(px), (int)floorf(pz)) > py + CLEAR_EPS)
                    return false;
            }
        }
    }
    return true;
}

static void Place(VoxelArena *a, const VoxelInstance *inst, const Shape *s, int x, int y)
{
    a->foeX = (float)(x + s->fx + inst->originX) + 0.5f;
    a->foeZ = (float)(y + s->fy + inst->originY) + 0.5f;
    a->allyX = (float)(x + s->ax + inst->originX) + 0.5f;
    a->allyZ = (float)(y + s->ay + inst->originY) + 0.5f;
    a->groundY = VoxelWorld_WalkHeightAt(x + s->ax + inst->originX, y + s->ay + inst->originY);
    a->midX = (a->foeX + a->allyX) * 0.5f;
    a->midZ = (a->foeZ + a->allyZ) * 0.5f;
}

static bool Fits(const VoxelInstance *inst, const Shape *s, int x0, int y0, bool surfing, bool level)
{
    int dx, dy;
    float h0 = 0.0f;
    bool first = true;
    for (dy = 0; dy < s->h; dy++)
        for (dx = 0; dx < s->w; dx++)
        {
            float h;
            if (!OpenCell(inst, x0 + dx, y0 + dy, surfing))
                return false;
            h = VoxelWorld_WalkHeightAt(x0 + dx + inst->originX, y0 + dy + inst->originY);
            if (first) { h0 = h; first = false; }
            else if (level && fabsf(h - h0) > 0.01f)
                return false;
        }
    return true;
}

bool VoxelBattle_FindArena(VoxelArena *arena)
{
    const VoxelInstance *inst;
    int px, py, pass, si;
    bool surfing;
    memset(arena, 0, sizeof(*arena));
    if (gVoxelInstanceCount <= 0 || gVoxelInstances[0].cells == NULL)
        return false;
    inst = &gVoxelInstances[0];
    px = gSaveBlock1Ptr->pos.x;
    py = gSaveBlock1Ptr->pos.y;
    surfing = TestPlayerAvatarFlags(PLAYER_AVATAR_FLAG_SURFING);
    arena->mapGroup = inst->mapGroup;
    arena->mapNum = inst->mapNum;

    // BattleArena.lua search: per shape, the passes in order -- clear and
    // level, clear, then unproved -- and the first pass that finds anything
    // wins with its candidate nearest the player. On land, the whole search
    // runs again over the water if it found nothing.
    for (si = 0; si < 4; si++)
    {
        const Shape *s = &sShapes[si & 1];
        bool surf = surfing || si >= 2;
        for (pass = 0; pass < 4; pass++)
        {
            bool level = pass == 0, clear = pass < 3;
            int rig = pass == 2 ? 1 : 0;
            int x, y, bestX = 0, bestY = 0;
            float best = -1.0f;
            for (y = 0; y + s->h <= inst->height; y++)
                for (x = 0; x + s->w <= inst->width; x++)
                {
                    float cx = x + (s->w - 1) / 2.0f - px, cy = y + (s->h - 1) / 2.0f - py, d = cx * cx + cy * cy;
                    if (best >= 0.0f && d >= best)
                        continue;
                    if (!Fits(inst, s, x, y, surf, level))
                        continue;
                    if (clear)
                    {
                        VoxelArena probe = *arena;
                        probe.rig = rig;
                        Place(&probe, inst, s, x, y);
                        if (!Clear(&probe))
                            continue;
                    }
                    best = d;
                    bestX = x;
                    bestY = y;
                }
            if (best >= 0.0f)
            {
                Place(arena, inst, s, bestX, bestY);
                arena->rig = rig;
                arena->valid = true;
                return true;
            }
        }
    }
    // nowhere fits: stand the pair where the player is
    arena->allyX = (float)(px + inst->originX) + 0.5f;
    arena->allyZ = (float)(py + inst->originY) + 0.5f;
    arena->foeX = arena->allyX;
    arena->foeZ = arena->allyZ - 3.0f;
    arena->groundY = VoxelWorld_WalkHeightAt(px + inst->originX, py + inst->originY);
    arena->midX = arena->allyX;
    arena->midZ = arena->allyZ - 1.5f;
    arena->valid = true;
    return true;
}

void VoxelBattle_Rig(const VoxelArena *arena, float *eye, float *focus, float *fovDeg)
{
    const Rig *r = &sRigs[arena->rig ? 1 : 0];
    float dx, dy, dz, dist;
    eye[0] = arena->midX + r->side / 16.0f;
    eye[1] = arena->groundY + r->height / 16.0f;
    eye[2] = arena->midZ + r->back / 16.0f;
    focus[0] = arena->midX + r->lookX / 16.0f;
    focus[1] = arena->groundY + r->lookY / 16.0f;
    focus[2] = arena->midZ;
    dx = eye[0] - focus[0];
    dy = eye[1] - focus[1];
    dz = eye[2] - focus[2];
    dist = sqrtf(dx * dx + dy * dy + dz * dz);
    // the field of view that frames the rig's frame height at the focus
    *fovDeg = 2.0f * atanf((r->frameH / 16.0f) / 2.0f / dist) * 180.0f / 3.14159265f;
}

#endif
#endif
