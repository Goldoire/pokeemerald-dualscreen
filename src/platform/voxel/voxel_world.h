#ifndef GUARD_VOXEL_WORLD_H
#define GUARD_VOXEL_WORLD_H

#ifdef PLATFORM_SDL2
#ifdef NATIVE_LINUX

#include <stdbool.h>

typedef enum {
    VOXEL_SHAPE_FLAT = 0,
    VOXEL_SHAPE_DECAL,
    VOXEL_SHAPE_LOW,
    VOXEL_SHAPE_LEDGE,
    VOXEL_SHAPE_WALL,
    VOXEL_SHAPE_TREE,
    VOXEL_SHAPE_BUILDING,
    VOXEL_SHAPE_ROOF,
    VOXEL_SHAPE_FURNITURE,
    VOXEL_SHAPE_BED,
    VOXEL_SHAPE_TABLE,
    VOXEL_SHAPE_COUNTER,
    VOXEL_SHAPE_SIGN,
    VOXEL_SHAPE_STAIRS,
    VOXEL_SHAPE_WATER,
    VOXEL_SHAPE_VOID,
    VOXEL_SHAPE_COUNT
} VoxelVisualShape;

struct MapHeader; // forward declaration

struct VoxelMapInstance {
    const struct MapHeader *header;
    int mapGroup;
    int mapNum;
    int originX;
    int originY;
};

#define MAX_VOXEL_MAP_INSTANCES 16
extern struct VoxelMapInstance gVoxelMapInstances[MAX_VOXEL_MAP_INSTANCES];
extern int gVoxelMapInstanceCount;

void VoxelWorld_BuildInstances(void);
const struct VoxelMapInstance *VoxelWorld_GetInstanceAt(int worldX, int worldY);
const struct VoxelMapInstance *VoxelWorld_GetMetatileIdAndInstance(int worldX, int worldY, int *outMetatileId);

// Get metatile ID at raw map coordinates (mapX, mapY)
int VoxelWorld_GetMetatileId(int mapX, int mapY);

// Classify a map tile at raw map coordinates (mapX, mapY) into a visual shape
VoxelVisualShape VoxelWorld_ClassifyTile(int mapX, int mapY);

// Get the active map dimensions (playable area without border)
void VoxelWorld_GetMapDimensions(int *width, int *height);

// Check if map data is available
bool VoxelWorld_IsMapAvailable(void);

// Get player map coordinates
void VoxelWorld_GetPlayerCoords(int *x, int *y);
void VoxelWorld_GetPlayerWorldCoords(float *wx, float *wz);

// Get player facing direction (DIR_SOUTH=1, DIR_NORTH=2, DIR_WEST=3, DIR_EAST=4)
int VoxelWorld_GetPlayerFacingDirection(void);

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2
#endif // GUARD_VOXEL_WORLD_H
