#include "voxel_world.h"

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include "global.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "metatile_behavior.h"
#include "event_object_movement.h"
#include "constants/map_types.h"
#include "constants/metatile_behaviors.h"

// Removed gVoxelTerrainHeight

bool VoxelWorld_IsMapAvailable(void)
{
    extern void CB2_Overworld(void);
    extern void CB2_OverworldBasic(void);
    if (gMapHeader.mapLayout == NULL) return false;
    return (gMain.callback2 == CB2_Overworld || gMain.callback2 == CB2_OverworldBasic);
}

void VoxelWorld_GetMapDimensions(int *width, int *height)
{
    if (gMapHeader.mapLayout) {
        *width = gMapHeader.mapLayout->width;
        *height = gMapHeader.mapLayout->height;
    } else {
        *width = 0;
        *height = 0;
    }
}

void VoxelWorld_GetPlayerCoords(int *x, int *y)
{
    struct ObjectEvent *playerObj = &gObjectEvents[gPlayerAvatar.objectEventId];
    *x = playerObj->currentCoords.x - MAP_OFFSET;
    *y = playerObj->currentCoords.y - MAP_OFFSET;
}

struct VoxelMapInstance gVoxelMapInstances[MAX_VOXEL_MAP_INSTANCES];
int gVoxelMapInstanceCount = 0;

void VoxelWorld_BuildInstances(void)
{
    gVoxelMapInstanceCount = 0;
    if (!gMapHeader.mapLayout) return;

    // Instance 0 is always the current map at origin (0, 0)
    gVoxelMapInstances[0].header = &gMapHeader;
    gVoxelMapInstances[0].mapGroup = gSaveBlock1Ptr->location.mapGroup;
    gVoxelMapInstances[0].mapNum = gSaveBlock1Ptr->location.mapNum;
    gVoxelMapInstances[0].originX = 0;
    gVoxelMapInstances[0].originY = 0;
    gVoxelMapInstanceCount = 1;

    // Add connections
    if (gMapHeader.connections) {
        int count = gMapHeader.connections->count;
        const struct MapConnection *conn = gMapHeader.connections->connections;
        for (int i = 0; i < count; i++, conn++) {
            if (gVoxelMapInstanceCount >= MAX_VOXEL_MAP_INSTANCES) break;

            if (conn->direction == CONNECTION_DIVE || conn->direction == CONNECTION_EMERGE) continue;

            const struct MapHeader *nh = GetMapHeaderFromConnection(conn);
            if (!nh || !nh->mapLayout) continue;

            struct VoxelMapInstance *inst = &gVoxelMapInstances[gVoxelMapInstanceCount++];
            inst->header = nh;
            inst->mapGroup = conn->mapGroup;
            inst->mapNum = conn->mapNum;

            if (conn->direction == CONNECTION_NORTH) {
                inst->originX = conn->offset;
                inst->originY = -nh->mapLayout->height;
            } else if (conn->direction == CONNECTION_SOUTH) {
                inst->originX = conn->offset;
                inst->originY = gMapHeader.mapLayout->height;
            } else if (conn->direction == CONNECTION_WEST) {
                inst->originX = -nh->mapLayout->width;
                inst->originY = conn->offset;
            } else if (conn->direction == CONNECTION_EAST) {
                inst->originX = gMapHeader.mapLayout->width;
                inst->originY = conn->offset;
            }
        }
    }
}

const struct VoxelMapInstance *VoxelWorld_GetInstanceAt(int worldX, int worldY)
{
    // Check current map first (optimization)
    if (gVoxelMapInstanceCount > 0) {
        int w = gVoxelMapInstances[0].header->mapLayout->width;
        int h = gVoxelMapInstances[0].header->mapLayout->height;
        if (worldX >= 0 && worldX < w && worldY >= 0 && worldY < h) {
            return &gVoxelMapInstances[0];
        }
    }

    // Check connections
    for (int i = 1; i < gVoxelMapInstanceCount; i++) {
        const struct VoxelMapInstance *inst = &gVoxelMapInstances[i];
        int localX = worldX - inst->originX;
        int localY = worldY - inst->originY;
        if (localX >= 0 && localX < inst->header->mapLayout->width &&
            localY >= 0 && localY < inst->header->mapLayout->height) {
            return inst;
        }
    }

    return NULL; // Outside map bounds
}

static u16 VoxelWorld_GetRawBlock(int worldX, int worldY, const struct VoxelMapInstance **outInst)
{
    const struct VoxelMapInstance *inst = VoxelWorld_GetInstanceAt(worldX, worldY);
    if (outInst) *outInst = inst;
    if (!inst) return 0; // Void block (metatile 0)

    int localX = worldX - inst->originX;
    int localY = worldY - inst->originY;

    if (inst == &gVoxelMapInstances[0]) {
        // Current map: use dynamic backup layout to see live events/changes
        int bx = localX + MAP_OFFSET;
        int by = localY + MAP_OFFSET;
        if (bx >= 0 && bx < gBackupMapLayout.width &&
            by >= 0 && by < gBackupMapLayout.height) {
            return gBackupMapLayout.map[bx + gBackupMapLayout.width * by];
        }
    } else {
        // Connected map: use static ROM layout
        if (localX >= 0 && localX < inst->header->mapLayout->width &&
            localY >= 0 && localY < inst->header->mapLayout->height) {
            return inst->header->mapLayout->map[localY * inst->header->mapLayout->width + localX];
        }
    }
    return 0;
}

void VoxelWorld_GetPlayerWorldCoords(float *wx, float *wz)
{
    int px = 0, py = 0;
    VoxelWorld_GetPlayerCoords(&px, &py);
    if (gVoxelMapInstanceCount > 0) {
        if (wx) *wx = (float)(px + gVoxelMapInstances[0].originX);
        if (wz) *wz = (float)(py + gVoxelMapInstances[0].originY);
    } else {
        if (wx) *wx = (float)px;
        if (wz) *wz = (float)py;
    }
}

int VoxelWorld_GetPlayerFacingDirection(void)
{
    if (gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_CONTROLLABLE) {
        return GetPlayerFacingDirection(); // usually 1=south, 2=north, 3=west, 4=east
    }
    return 1;
}

int VoxelWorld_GetMetatileId(int worldX, int worldY)
{
    u16 block = VoxelWorld_GetRawBlock(worldX, worldY, NULL);
    // UNPACK_METATILE (data & 0x3FF)
    return block & 0x3FF;
}

const struct VoxelMapInstance *VoxelWorld_GetMetatileIdAndInstance(int worldX, int worldY, int *outMetatileId)
{
    const struct VoxelMapInstance *inst = NULL;
    u16 block = VoxelWorld_GetRawBlock(worldX, worldY, &inst);
    if (outMetatileId) *outMetatileId = block & 0x3FF;
    return inst;
}

unsigned char VoxelWorld_GetCollision(int worldX, int worldY)
{
    u16 block = VoxelWorld_GetRawBlock(worldX, worldY, NULL);
    // UNPACK_COLLISION ((data >> 10) & 3)
    return (block >> 10) & 3;
}

static u16 VoxelWorld_GetMetatileAttribute(const struct VoxelMapInstance *inst, int metatileId)
{
    if (!inst || !inst->header || !inst->header->mapLayout) return 0;
    
    if (metatileId < 512) {
        if (inst->header->mapLayout->primaryTileset)
            return inst->header->mapLayout->primaryTileset->metatileAttributes[metatileId];
    } else if (metatileId < 1024) {
        if (inst->header->mapLayout->secondaryTileset)
            return inst->header->mapLayout->secondaryTileset->metatileAttributes[metatileId - 512];
    }
    return 0;
}

unsigned char VoxelWorld_GetMetatileBehavior(int worldX, int worldY)
{
    const struct VoxelMapInstance *inst = NULL;
    u16 block = VoxelWorld_GetRawBlock(worldX, worldY, &inst);
    int m = block & 0x3FF;
    u16 attr = VoxelWorld_GetMetatileAttribute(inst, m);
    // UNPACK_BEHAVIOR (attr & 0xFF)
    return attr & 0xFF;
}

VoxelVisualShape VoxelWorld_ClassifyTile(int worldX, int worldY)
{
    const struct VoxelMapInstance *inst = VoxelWorld_GetInstanceAt(worldX, worldY);
    if (!inst) return VOXEL_SHAPE_VOID;

    int metatileId = VoxelWorld_GetMetatileId(worldX, worldY);
    unsigned char behavior = VoxelWorld_GetMetatileBehavior(worldX, worldY);
    unsigned char collision = VoxelWorld_GetCollision(worldX, worldY);

    if (inst->header->mapType == MAP_TYPE_INDOOR || inst->header->mapType == MAP_TYPE_SECRET_BASE) {
        if (metatileId == 622) return VOXEL_SHAPE_VOID; // Black out-of-bounds area

        // Common Furniture Behaviors
        if (behavior == MB_PC || behavior == MB_TELEVISION || metatileId == 570) return VOXEL_SHAPE_FURNITURE;
        if (behavior == MB_PICTURE_BOOK_SHELF || behavior == MB_BOOKSHELF || behavior == MB_POKEMON_CENTER_BOOKSHELF || behavior == MB_SHOP_SHELF || metatileId == 533 || metatileId == 534) return VOXEL_SHAPE_WALL;
        if (behavior == MB_COUNTER) return VOXEL_SHAPE_COUNTER;
        
        // Some specific visual exceptions
        if (metatileId == 576 || metatileId == 577 || metatileId == 584 || metatileId == 585 || metatileId == 586) return VOXEL_SHAPE_TABLE;
        if (metatileId == 565 || metatileId == 558 || metatileId == 566) return VOXEL_SHAPE_FURNITURE; // Chair
        if (metatileId == 578) return VOXEL_SHAPE_SIGN; // Vertical sprite (sign/potted plant)
        if (metatileId == 589) return VOXEL_SHAPE_WALL; // Stairwells are drawn as vertical walls
        if (MetatileBehavior_IsWarpDoor(behavior) || MetatileBehavior_IsDoor(behavior)) return VOXEL_SHAPE_WALL; // actual doors
        if (metatileId == 514 || metatileId == 515 || metatileId == 516 || metatileId == 517) return VOXEL_SHAPE_DECAL; // Floor mats
        
        // Beds (usually top-down perspective, drawn low)
        if (metatileId == 567 || metatileId == 568 || metatileId == 575 || metatileId == 576) return VOXEL_SHAPE_BED;

        // Dynamic Wall Classification
        if (collision != 0) {
            unsigned char aboveCollision = VoxelWorld_GetCollision(worldX, worldY - 1);
            unsigned char belowCollision = VoxelWorld_GetCollision(worldX, worldY + 1);
            
            if (aboveCollision != 0) {
                return VOXEL_SHAPE_WALL; // Tall wall or continuous impassable structure
            } else if (belowCollision != 0) {
                return VOXEL_SHAPE_ROOF; // Top of the wall
            } else {
                return VOXEL_SHAPE_LOW; // Short obstacle
            }
        } else {
            // Check for wall alcoves
            unsigned char nColl = VoxelWorld_GetCollision(worldX, worldY - 1);
            unsigned char eColl = VoxelWorld_GetCollision(worldX + 1, worldY);
            unsigned char wColl = VoxelWorld_GetCollision(worldX - 1, worldY);
            if (nColl != 0 && eColl != 0 && wColl != 0) {
                return VOXEL_SHAPE_WALL; // Draw it as part of the vertical wall
            }
        }
        
        return VOXEL_SHAPE_FLAT;
    }

    // OUTDOORS
    if (MetatileBehavior_IsSurfableWaterOrUnderwater(behavior)) return VOXEL_SHAPE_WATER;
    if (MetatileBehavior_IsTallGrass(behavior)) return VOXEL_SHAPE_FLAT; // Grass is flat for now
    if (MetatileBehavior_IsJumpSouth(behavior) || MetatileBehavior_IsJumpNorth(behavior) ||
        MetatileBehavior_IsJumpEast(behavior) || MetatileBehavior_IsJumpWest(behavior)) {
        return VOXEL_SHAPE_LEDGE;
    }
    
    // Dynamic Wall Classification for Overworld
    if (collision != 0) {
        unsigned char aboveCollision = VoxelWorld_GetCollision(worldX, worldY - 1);
        unsigned char belowCollision = VoxelWorld_GetCollision(worldX, worldY + 1);
        
        // Simple heuristic for trees/buildings:
        // A tree is typically continuous vertically and has a round/leafy top. We don't know for sure, so we use WALL/ROOF generically.
        if (aboveCollision != 0) {
            return VOXEL_SHAPE_WALL; // Cliff, tree, building facade
        } else if (belowCollision != 0) {
            return VOXEL_SHAPE_ROOF; // Top edge of cliff or building or tree canopy
        } else {
            return VOXEL_SHAPE_LOW; // Fence, rock, sign, stump
        }
    }

    return VOXEL_SHAPE_FLAT;
}

#endif // NATIVE_LINUX
#endif // PLATFORM_SDL2
