#ifndef GUARD_VOXEL_WORLD_H
#define GUARD_VOXEL_WORLD_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// The voxel world model.
//
// Everything the renderer knows about the shape of the map comes from data the
// cartridge states outright rather than from guessing at pixels:
//
//   * the metatile BEHAVIOUR byte (tall grass, water, a ledge, a door, a
//     counter, a bridge deck...),
//   * the cell's COLLISION bits (can the player stand here),
//   * the cell's ELEVATION (which terrace it sits on),
//   * the metatile LAYER TYPE plus how much of its top layer is drawn ABOVE
//     the player (a treetop, a roof, an upper storey),
//   * the map's WARP EVENTS: a door is a warp, so the blocked run above one is
//     a building rather than landscape.
//
// The shape rules follow the Gen 3 profile of the DramaticShapes voxel mod
// (data/gen3_shapes.lua, lib/Gen3.lua; MIT), translated to C and reduced to
// what the cartridge data alone can decide.
//
// World units: one map cell is 1.0 on X/Z, one "course" (16 px of art, the
// height of one metatile) is 1.0 on Y. +X is east, +Z is south, +Y is up.
// ---------------------------------------------------------------------------

typedef enum {
    VC_VOID = 0,     // outside the map, or an all-black interior filler cell
    VC_GROUND,       // walkable floor at its terrace height
    VC_GRASS,        // tall grass: flat ground (tufts are drawn as decals)
    VC_WATER,        // recessed water surface
    VC_LEDGE,        // a jump ledge: a low step with a fascia
    VC_BRIDGE,       // a deck standing over what it spans
    VC_LOG,          // a Pacifidlog raft riding just clear of the sea
    VC_SLOPE,        // a ramp between terraces: walkable, takes the lower level
    VC_DOOR,         // walkable, but part of the facade it is cut into
    VC_WALL,         // a blocked cell standing as a column (landscape, trees,
                     // fences, interior wall bands); stacks with the row above
    VC_CLIFF,        // a blocked cell that is the drop between two terraces
    VC_BUILDING,     // a blocked cell claimed by a door-run building
    // interior furniture (heights from the Gen 3 profile, in px / 16)
    VC_COUNTER,
    VC_CONSOLE,      // TV, PC, cable box
    VC_BOOKCASE,
    VC_CYLINDER,     // vase
    VC_CAN,          // trash can
    VC_TABLETOP,
    VC_CHAIR,        // walkable pin: you stand on the seat
    VC_BED,
    VC_PROP,         // an interaction the profile has no model for
    VC_WORKTOP,
    VC_SINK,
    VC_APPLIANCE,
    VC_TV,
    VC_CABINET,
    VC_COUNT
} VoxelClass;

// Per-cell flags.
#define VCF_BLOCKED     0x01  // collision bits nonzero
#define VCF_WARP        0x02  // a warp event lands on this cell
#define VCF_ABOVE       0x04  // metatile draws opaque art above the player
#define VCF_CONSUMED    0x08  // absorbed into the column of the cell south of it
#define VCF_FADEABLE    0x10  // column may be ghosted when it hides the player
#define VCF_SHELL_N     0x20  // interior: draw the room shell on the north edge
#define VCF_SHELL_E     0x40
#define VCF_SHELL_W     0x80

typedef struct {
    uint16_t metatile;   // 0..1023
    uint8_t  behavior;
    uint8_t  collision;  // 0..3
    uint8_t  elevation;  // 0..15
    uint8_t  layerType;  // METATILE_LAYER_TYPE_*
    uint8_t  cls;        // VoxelClass
    uint8_t  flags;      // VCF_*
    int16_t  building;   // index into the instance's building list, or -1
    uint8_t  stack;      // rows stacked into this column (anchor cells only)
    uint8_t  pad;
    float    groundH;    // walkable surface height, or the column's base
    float    topH;       // column top (blocked / furniture / deck) else == groundH
    float    underH;     // bridges: the surface under the deck
    uint16_t underMeta;  // bridges: the art laid under the deck
} VoxelCell;

// A door-run building: every column it owns has rows [top..facadeRow].
#define MAX_VOXEL_BUILDINGS 96
typedef struct {
    int16_t minX, maxX;      // world columns, inclusive
    int16_t facadeRow;       // world row of the door / facade (south edge)
    int16_t minTop;          // northmost row any column reaches
    uint8_t wallRows;        // rows that stand vertical (K)
    uint8_t roofRows;        // rows laid back over the footprint as roof
    float   baseH;           // the street it is founded on
    uint8_t colTop[48];      // per column (x - minX): top row, relative: rows = facadeRow - colTop + 1; 0 = column not owned
    bool    fade;            // ghosted this frame
} VoxelBuilding;

// What the atlas analysis learned about one metatile of a map's tileset pair.
typedef struct {
    uint8_t layerType;
    uint8_t behavior;
    uint8_t topOpaque;    // opaque pixels in the layer drawn above the player, /256 -> 0..255
    uint8_t botOpaque;    // opaque pixels in the layer drawn at/below the player
    uint8_t avgR, avgG, avgB;
    uint8_t allBlack;     // every pixel of both layers is black: interior void
} VoxelMetaInfo;

struct MapHeader;
struct MapLayout;

typedef struct {
    const struct MapHeader *header;
    int mapGroup;
    int mapNum;
    int originX;
    int originY;
    int width;
    int height;
    bool indoor;          // interior rules (no elevation, room shell)
    bool useElevation;    // rank the elevation field into terraces
    VoxelCell *cells;     // width*height, row-major
    VoxelMetaInfo meta[1024];
    VoxelBuilding buildings[MAX_VOXEL_BUILDINGS];
    int buildingCount;
    uint16_t shellMeta;   // interior: art for the room shell
    float rankH[16];      // elevation -> height in courses
    uint32_t layoutHash;  // blockdata checksum (current map only)
} VoxelInstance;

#define MAX_VOXEL_MAP_INSTANCES 16
extern VoxelInstance gVoxelInstances[MAX_VOXEL_MAP_INSTANCES];
extern int gVoxelInstanceCount;

// Rebuild the instance list (current map + its connections) and every
// instance's cell model. Cheap when nothing changed. Returns true when any
// instance's geometry needs remeshing.
bool VoxelWorld_Update(void);
void VoxelWorld_Reset(void);

// Instance/cell lookup in world coordinates.
const VoxelInstance *VoxelWorld_InstanceAt(int worldX, int worldY);
const VoxelCell *VoxelWorld_CellAt(int worldX, int worldY);   // NULL outside every instance
bool VoxelWorld_IsMapAvailable(void);

// Player.
void VoxelWorld_GetPlayerWorldCoords(float *wx, float *wz);
float VoxelWorld_WalkHeightAt(int worldX, int worldY);         // where a walker's feet go

// Metatile art analysis, filled in by the atlas builder.
void VoxelWorld_SetMetaInfo(VoxelInstance *inst, int metatile, const VoxelMetaInfo *info);

// Class heights, in courses.
float VoxelWorld_ClassHeight(VoxelClass cls);

// Debug dumps through SDL_Log.
void VoxelWorld_DumpInstance(const VoxelInstance *inst);
void VoxelWorld_DumpPlayerCell(void);

#endif // NATIVE_LINUX || __ANDROID__
#endif // PLATFORM_SDL2
#endif // GUARD_VOXEL_WORLD_H
