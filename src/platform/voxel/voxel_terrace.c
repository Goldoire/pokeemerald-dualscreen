#include "voxel_terrace.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stddef.h>
#include "global.h"
#include "constants/map_groups.h"

// ROUTE 112: one flight (the General tileset's 175/207 at 20..21, 41..43)
// climbs north off the grass through the rock wall onto the ash plateau, and
// the cartridge sorts the thirty-one terraces itself -- MB_MOUNTAIN_TOP on
// every plateau cell, on none of the low ground. Three courses, because the
// cliff is drawn three rows deep and the flight is three stacked treads.
static const VoxelTerraceAnchor sRoute112[] = {
    { 26,  6, 0 },  // the north grass, below the plateau's north face
    { 23, 19, 3 }, { 21, 20, 3 }, { 20, 21, 3 }, { 22, 21, 3 }, { 24, 22, 3 }, { 30, 22, 3 },
    { 22, 23, 3 }, { 34, 23, 3 }, { 33, 25, 3 }, { 11, 26, 3 }, { 21, 27, 3 },
    { 28, 27, 3 },  // the ash plateau's main floor
    { 22, 28, 3 }, { 19, 29, 3 }, { 34, 29, 3 }, { 16, 30, 3 }, { 35, 31, 3 }, { 34, 33, 3 },
    { 15, 34, 3 },
    { 35, 35, 0 }, { 34, 36, 0 }, { 35, 41, 0 }, { 31, 42, 0 }, { 14, 43, 0 },
    { 26, 43, 0 },  // the south grass, the staircase's bottom
    { 36, 43, 0 }, { 16, 44, 0 }, {  6, 46, 0 }, { 11, 46, 0 }, { 18, 54, 0 },
};

// SOOTOPOLIS CITY: eighty terraces, 0 at the shore through 7 at the rim --
// a terrace is as high as the number of flights climbed to reach it.
static const VoxelTerraceAnchor sSootopolis[] = {
    {16,0,6}, {18,0,6}, {39,0,5}, {55,0,4}, {13,2,6}, {34,2,5}, {43,2,5}, {25,3,5}, {38,3,5}, {7,5,7},
    {15,5,6}, {37,5,5}, {41,5,5}, {57,5,4}, {47,6,5}, {9,7,7}, {5,8,7}, {43,8,5}, {7,9,6}, {27,9,4},
    {19,10,5}, {40,11,4}, {45,11,4}, {56,11,4}, {59,11,4}, {0,12,5}, {10,12,5}, {23,12,3}, {37,12,3}, {3,13,5},
    {18,13,4}, {25,13,3}, {53,13,4}, {58,13,4}, {5,14,4}, {12,14,4}, {19,15,4}, {22,15,2}, {36,15,3}, {24,16,2},
    {31,16,1}, {39,16,3}, {42,16,3}, {35,17,1}, {16,18,3}, {47,18,3}, {59,18,4}, {7,21,3}, {52,21,3}, {9,22,3},
    {20,22,2}, {55,22,2}, {5,23,2}, {44,23,2}, {51,23,3}, {12,24,2}, {0,25,2}, {18,25,1}, {21,25,1}, {55,26,2},
    {0,29,2}, {3,30,2}, {7,30,2}, {15,30,2}, {27,32,0}, {42,32,1}, {54,32,2}, {58,32,1}, {4,34,2}, {53,34,1},
    {15,35,1}, {2,36,2}, {5,36,2}, {19,36,0}, {40,37,0}, {57,37,1}, {0,38,0}, {2,38,0}, {17,38,0}, {50,48,0},
};

const VoxelTerraceAnchor *VoxelTerrace_For(int group, int num, int *count)
{
    if (group == (MAP_ROUTE112 >> 8) && num == (MAP_ROUTE112 & 0xFF))
    {
        *count = sizeof(sRoute112) / sizeof(sRoute112[0]);
        return sRoute112;
    }
    if (group == (MAP_SOOTOPOLIS_CITY >> 8) && num == (MAP_SOOTOPOLIS_CITY & 0xFF))
    {
        *count = sizeof(sSootopolis) / sizeof(sSootopolis[0]);
        return sSootopolis;
    }
    *count = 0;
    return NULL;
}

#endif
#endif
