#ifndef GUARD_VOXEL_ROLES_H
#define GUARD_VOXEL_ROLES_H

#ifdef PLATFORM_SDL2
#if defined(NATIVE_LINUX) || defined(__ANDROID__)

#include <stdint.h>

// ---------------------------------------------------------------------------
// WHAT EVERY METATILE HOENN PLACES IS MADE OF (DramaticShapes
// data/gen3_metatiles.lua, generated from all 518 maps; MIT).
//
//   art       surface: every pixel row is a top (grass, sand, worked stone)
//             face:    every row is a vertical material
//             brow:    surface rows and then face rows -- the drawn lip of a
//                      drop; `cap` is the row it turns over at, `face` how
//                      many wall rows follow
//             banded:  alternating rows of worked material -- treads, decking
//   material  what it is DRAWN in: rock, green, sand, stone, manmade, water
//   kind      the cartridge's own word where it has one
//   motif     nothing with a face is ever drawn beneath it: foliage or a
//             standalone object, not the top of a drop
//   ledge     the drawing holds a hard shadow line (one course of masonry)
//   axis      a tread's climb direction (x: vertical bands, y: horizontal)
//   facing    which side of a cliff run is the higher ground (N / S)
// ---------------------------------------------------------------------------
enum { ROLE_ART_NONE, ROLE_ART_SURFACE, ROLE_ART_FACE, ROLE_ART_BROW, ROLE_ART_BANDED };
enum { ROLE_MAT_NONE, ROLE_MAT_ROCK, ROLE_MAT_GREEN, ROLE_MAT_SAND, ROLE_MAT_STONE, ROLE_MAT_MANMADE,
       ROLE_MAT_WATER, ROLE_MAT_OTHER };
enum { ROLE_KIND_NONE, ROLE_KIND_WATER, ROLE_KIND_LEDGE, ROLE_KIND_TREE, ROLE_KIND_MOUNTAIN };
#define ROLE_FLAG_MOTIF 1
#define ROLE_FLAG_LEDGE 2
enum { ROLE_AXIS_NONE, ROLE_AXIS_X, ROLE_AXIS_Y };
enum { ROLE_FACING_NONE, ROLE_FACING_N, ROLE_FACING_S };

typedef struct {
    uint16_t meta;
    uint8_t art, material;
    int8_t cap, face;
    uint8_t kind, flags, axis, facing;
} VoxelRole;

struct Tileset;
typedef struct {
    const struct Tileset *tileset;
    const VoxelRole *rows;   // sorted by metatile id
    int count;
    int primary;
} VoxelRoleSet;

extern const VoxelRoleSet gVoxelRoleSets[];
extern const int gVoxelRoleSetCount;

// The role of metatile `m` (0..1023) of the pair (primary, secondary), or
// NULL when the census never saw it placed.
const VoxelRole *VoxelRoles_Find(const struct Tileset *primary, const struct Tileset *secondary, int m);

#endif
#endif
#endif // GUARD_VOXEL_ROLES_H
