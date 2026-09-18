#ifndef GUARD_VOXEL_BATTLE_H
#define GUARD_VOXEL_BATTLE_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>

// ---------------------------------------------------------------------------
// A battle fought on the map (DramaticShapes' 3D-BTL: lib/OverworldBattle.lua,
// lib/BattleArena.lua, lib/BattleCam.lua; MIT).
//
// Nothing moves: the arena is where the camera goes, not the player. It is
// the open stretch of map nearest the player that holds a north-south line
// of three cells -- the foe at its north end, your Pokemon at its south --
// and it is shot from a rig solved so the two land where Emerald's own
// battle screen puts them, which is what lets the game's own move effects
// and health boxes sit on them without being moved.
// ---------------------------------------------------------------------------

typedef struct {
    bool valid;
    int mapGroup, mapNum;
    float foeX, foeZ;          // world cells: the foe's feet
    float allyX, allyZ;        // your Pokemon's feet
    float groundY;             // the floor both stand on
    float midX, midZ;
    int rig;                   // 0: the tele rig on Emerald's own feet; 1: the close wide rig
} VoxelArena;

// Find the arena for a battle starting now (the player on the current map).
bool VoxelBattle_FindArena(VoxelArena *arena);

// The rig: eye and focus in world units, and the vertical field of view
// (degrees) that frames the GBA screen's 160 rows.
void VoxelBattle_Rig(const VoxelArena *arena, float *eye, float *focus, float *fovDeg);

#endif
#endif
#endif // GUARD_VOXEL_BATTLE_H
