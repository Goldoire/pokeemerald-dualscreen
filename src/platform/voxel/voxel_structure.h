#ifndef GUARD_VOXEL_STRUCTURE_H
#define GUARD_VOXEL_STRUCTURE_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>
#ifdef __ANDROID__
#include "gles1_compat.h"
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif
#include "voxel_world.h"

#define MAX_VOXEL_STRUCTURES 256
#define MAX_TILES_PER_STRUCTURE 1024

typedef enum {
    STRUCT_TYPE_BUILDING = 0,
    STRUCT_TYPE_TREE,
} VoxelStructureType;

typedef struct {
    int x, y; // World coordinates
} VoxelStructTile;

typedef struct {
    VoxelStructureType type;
    int minX, minY, maxX, maxY;
    int tileCount;
    VoxelStructTile tiles[MAX_TILES_PER_STRUCTURE];
    bool consumed;
} VoxelStructure;

extern VoxelStructure gVoxelStructures[MAX_VOXEL_STRUCTURES];
extern int gVoxelStructureCount;

// Extract structures from all active instances
void VoxelStructure_ExtractAll(void);

// Render all structures
void VoxelStructure_RenderAll(GLuint atlasTex);

// Check if a tile belongs to any structure
bool VoxelStructure_IsTileInStructure(int worldX, int worldY);

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2

#endif // GUARD_VOXEL_STRUCTURE_H
